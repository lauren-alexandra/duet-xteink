#include "ReadingJournal.h"

#include <HalStorage.h>
#include <Logging.h>
#include <esp_mac.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace {
constexpr uint32_t JOURNAL_MAGIC = 0x4C4E4A52u;  // RJNL, little-endian on disk
constexpr uint8_t JOURNAL_VERSION = 1;
constexpr uint16_t JOURNAL_FILE_SIZE = 3092;
constexpr char JOURNAL_PATH[] = DUET_STATE_ROOT_PATH "/reading_journal.bin";
constexpr char JOURNAL_BACKUP_PATH[] = DUET_STATE_ROOT_PATH "/reading_journal.bin.bak";
constexpr char SYNCED_JOURNAL_DIR[] = DUET_STATE_ROOT_PATH "/synced_journals";

template <typename T>
bool readPod(FsFile& file, T& value) {
  return file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T)) == static_cast<int>(sizeof(T));
}

template <typename T>
bool writePod(FsFile& file, const T& value) {
  return file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T)) == sizeof(T);
}

template <typename T, size_t N>
bool readArray(FsFile& file, std::array<T, N>& values) {
  const size_t byteCount = sizeof(T) * N;
  return file.read(reinterpret_cast<uint8_t*>(values.data()), byteCount) == static_cast<int>(byteCount);
}

template <typename T, size_t N>
bool writeArray(FsFile& file, const std::array<T, N>& values) {
  const size_t byteCount = sizeof(T) * N;
  return file.write(reinterpret_cast<const uint8_t*>(values.data()), byteCount) == byteCount;
}

uint32_t addSaturated(const uint32_t current, const uint32_t value) {
  return std::numeric_limits<uint32_t>::max() - current < value ? std::numeric_limits<uint32_t>::max()
                                                                : current + value;
}

uint16_t addSaturated16(const uint16_t current, const uint16_t value) {
  return std::numeric_limits<uint16_t>::max() - current < value ? std::numeric_limits<uint16_t>::max()
                                                                : static_cast<uint16_t>(current + value);
}

bool verifyFileSize(const char* path) {
  FsFile file;
  if (!Storage.openFileForRead("RJNL", path, file)) {
    return false;
  }
  const bool valid = file.fileSize() == JOURNAL_FILE_SIZE;
  file.close();
  return valid;
}

std::string localSyncedJournalFileName() {
  uint8_t mac[6] = {};
  if (esp_efuse_mac_get_default(mac) != 0) return {};

  char name[32];
  snprintf(name, sizeof(name), "device_%02x%02x%02x%02x%02x%02x.bin", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return name;
}

std::string localSyncedJournalPath() {
  const std::string name = localSyncedJournalFileName();
  return name.empty() ? std::string{} : std::string(SYNCED_JOURNAL_DIR) + "/" + name;
}

bool copyFileAtomically(const char* sourcePath, const char* destinationPath) {
  FsFile source;
  if (!Storage.openFileForRead("RJNL", sourcePath, source)) return false;

  const std::string tmpPath = std::string(destinationPath) + ".part";
  if (Storage.exists(tmpPath.c_str())) Storage.remove(tmpPath.c_str());

  FsFile destination;
  if (!Storage.openFileForWrite("RJNL", tmpPath, destination)) {
    source.close();
    return false;
  }

  uint8_t buffer[256];
  bool ok = true;
  while (ok && source.available() > 0) {
    const int read = source.read(buffer, sizeof(buffer));
    if (read <= 0) {
      ok = false;
      break;
    }
    ok = destination.write(buffer, static_cast<size_t>(read)) == static_cast<size_t>(read);
  }
  source.close();
  destination.flush();
  ok = ok && destination.sync() && destination.close();
  if (!ok) {
    Storage.remove(tmpPath.c_str());
    return false;
  }
  if (Storage.exists(destinationPath) && !Storage.remove(destinationPath)) {
    Storage.remove(tmpPath.c_str());
    return false;
  }
  if (!Storage.rename(tmpPath.c_str(), destinationPath)) {
    Storage.remove(tmpPath.c_str());
    return false;
  }
  return true;
}
}  // namespace

bool ReadingJournal::loadFromPath(const char* path, ReadingJournal& journal) {
  FsFile file;
  if (!Storage.openFileForRead("RJNL", path, file)) {
    return false;
  }
  if (file.fileSize() != JOURNAL_FILE_SIZE) {
    file.close();
    return false;
  }

  uint32_t magic = 0;
  uint8_t version = 0;
  uint8_t reserved8 = 0;
  uint16_t fileSize = 0;
  uint16_t reserved16 = 0;
  bool ok = readPod(file, magic) && readPod(file, version) && readPod(file, reserved8) && readPod(file, fileSize) &&
            magic == JOURNAL_MAGIC && version == JOURNAL_VERSION && fileSize == JOURNAL_FILE_SIZE &&
            readPod(file, journal.anchorDay_) && readPod(file, journal.longestSessionSeconds_) &&
            readPod(file, journal.recentCount_) && readPod(file, journal.recentNext_) && readPod(file, reserved16) &&
            readArray(file, journal.dailyReadingSeconds_) && readArray(file, journal.dailyScreenPages_) &&
            readArray(file, journal.dailySessions_) && readArray(file, journal.dailyCompletedBooks_);

  for (auto& session : journal.recentSessions_) {
    ok = ok && readPod(file, session.dayIndex) && readPod(file, session.startMinute) &&
         readPod(file, session.readingSeconds) && readPod(file, session.screenPages);
  }
  file.close();

  if (!ok || journal.recentCount_ > RECENT_SESSION_COUNT || journal.recentNext_ >= RECENT_SESSION_COUNT) {
    return false;
  }
  return true;
}

std::unique_ptr<ReadingJournal> ReadingJournal::load() {
  std::unique_ptr<ReadingJournal> journal(new (std::nothrow) ReadingJournal());
  if (!journal) {
    LOG_ERR("RJNL", "Not enough memory to load reading journal");
    return nullptr;
  }

  if (loadFromPath(JOURNAL_PATH, *journal)) {
    return journal;
  }

  // `*journal = ReadingJournal{}` would materialize the ~3KB object on the
  // task stack before copying; construct replacements directly on the heap.
  journal.reset(new (std::nothrow) ReadingJournal());
  if (!journal) {
    LOG_ERR("RJNL", "Not enough memory to reset reading journal");
    return nullptr;
  }
  if (loadFromPath(JOURNAL_BACKUP_PATH, *journal)) {
    LOG_DBG("RJNL", "Recovered reading journal from backup");
    return journal;
  }

  journal.reset(new (std::nothrow) ReadingJournal());
  if (!journal) {
    LOG_ERR("RJNL", "Not enough memory to reset reading journal");
  }
  return journal;
}

std::unique_ptr<ReadingJournal> ReadingJournal::loadAggregated() {
  std::unique_ptr<ReadingJournal> journal = load();
  if (!journal) return nullptr;

  FsFile dir = Storage.open(SYNCED_JOURNAL_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return journal;
  }

  const std::string localFileName = localSyncedJournalFileName();
  char name[128];
  uint16_t loadedCount = 0;
  for (FsFile file = dir.openNextFile(); file; file = dir.openNextFile()) {
    const bool isDirectory = file.isDirectory();
    const size_t nameLen = file.getName(name, sizeof(name));
    file.close();
    if (isDirectory || nameLen == 0 || (!localFileName.empty() && strcmp(name, localFileName.c_str()) == 0)) continue;

    // ~3KB object (366-day arrays): far too large for a task stack, so it is
    // constructed directly on the heap for each peer file.
    std::unique_ptr<ReadingJournal> peer(new (std::nothrow) ReadingJournal());
    if (!peer) {
      LOG_ERR("RJNL", "Not enough memory to aggregate synced journals");
      break;
    }
    const std::string path = std::string(SYNCED_JOURNAL_DIR) + "/" + name;
    if (loadFromPath(path.c_str(), *peer)) {
      journal->mergeFromPeer(*peer);
      loadedCount++;
    }
  }
  dir.close();
  if (loadedCount > 0) {
    LOG_DBG("RJNL", "Aggregated %u synced journal(s)", static_cast<unsigned>(loadedCount));
  }
  return journal;
}

bool ReadingJournal::recordSession(const ReadingStatsDateTime& localStart, const uint32_t readingSeconds,
                                   const uint16_t screenPages) {
  if (!localStart.isValid() || (readingSeconds == 0 && screenPages == 0)) {
    return false;
  }
  std::unique_ptr<ReadingJournal> journal = load();
  if (!journal) {
    return false;
  }
  journal->addReadingSpan(localStart, readingSeconds);
  if (screenPages > 0) {
    journal->addSession(localStart, readingSeconds, screenPages);
  }
  return journal->save();
}

bool ReadingJournal::adjustReadingTime(const ReadingStatsDate& date, const int32_t readingSecondsDelta) {
  if (!date.isValid() || readingSecondsDelta == 0) return false;
  std::unique_ptr<ReadingJournal> journal = load();
  if (!journal) return false;

  const uint32_t dayIndex = readingStatsDayIndex(date);
  journal->advanceAnchor(dayIndex);
  size_t offset = 0;
  if (!journal->offsetForDay(dayIndex, offset)) return false;

  uint32_t& seconds = journal->dailyReadingSeconds_[offset];
  if (readingSecondsDelta > 0) {
    seconds = addSaturated(seconds, static_cast<uint32_t>(readingSecondsDelta));
  } else {
    const uint32_t decrease = static_cast<uint32_t>(-static_cast<int64_t>(readingSecondsDelta));
    seconds = decrease >= seconds ? 0 : seconds - decrease;
  }
  return journal->save();
}

bool ReadingJournal::adjustCompletion(const ReadingStatsDate& date, const int delta) {
  if (!date.isValid() || delta == 0) {
    return false;
  }
  std::unique_ptr<ReadingJournal> journal = load();
  if (!journal) {
    return false;
  }

  const uint32_t dayIndex = readingStatsDayIndex(date);
  journal->advanceAnchor(dayIndex);
  size_t offset = 0;
  if (!journal->offsetForDay(dayIndex, offset)) {
    return false;
  }
  uint8_t& count = journal->dailyCompletedBooks_[offset];
  if (delta > 0) {
    count = static_cast<uint8_t>(std::min<int>(255, static_cast<int>(count) + delta));
  } else {
    count = static_cast<uint8_t>(std::max<int>(0, static_cast<int>(count) + delta));
  }
  return journal->save();
}

bool ReadingJournal::publishLocalForSync() {
  if (!Storage.existsForRead(JOURNAL_PATH) || !Storage.ensureDirectoryExists(DUET_STATE_ROOT_PATH "") ||
      !Storage.ensureDirectoryExists(SYNCED_JOURNAL_DIR)) {
    return false;
  }

  const std::string path = localSyncedJournalPath();
  if (path.empty()) return false;
  return copyFileAtomically(JOURNAL_PATH, path.c_str());
}

bool ReadingJournal::resetLocal() {
  // Heap-constructed: the ~3KB object must not live on a task stack.
  const std::unique_ptr<ReadingJournal> fresh(new (std::nothrow) ReadingJournal());
  if (!fresh) {
    LOG_ERR("RJNL", "Not enough memory to reset reading journal");
    return false;
  }
  return fresh->save(false);
}

void ReadingJournal::advanceAnchor(const uint32_t dayIndex) {
  if (anchorDay_ == 0 && dailyReadingSeconds_[0] == 0 && dailySessions_[0] == 0 && dailyCompletedBooks_[0] == 0) {
    anchorDay_ = dayIndex;
    return;
  }
  if (dayIndex <= anchorDay_) {
    return;
  }

  const uint32_t delta = dayIndex - anchorDay_;
  if (delta >= HISTORY_DAYS) {
    dailyReadingSeconds_.fill(0);
    dailyScreenPages_.fill(0);
    dailySessions_.fill(0);
    dailyCompletedBooks_.fill(0);
  } else {
    for (size_t i = HISTORY_DAYS; i-- > delta;) {
      dailyReadingSeconds_[i] = dailyReadingSeconds_[i - delta];
      dailyScreenPages_[i] = dailyScreenPages_[i - delta];
      dailySessions_[i] = dailySessions_[i - delta];
      dailyCompletedBooks_[i] = dailyCompletedBooks_[i - delta];
    }
    for (size_t i = 0; i < delta; ++i) {
      dailyReadingSeconds_[i] = 0;
      dailyScreenPages_[i] = 0;
      dailySessions_[i] = 0;
      dailyCompletedBooks_[i] = 0;
    }
  }
  anchorDay_ = dayIndex;
}

bool ReadingJournal::offsetForDay(const uint32_t dayIndex, size_t& offset) const {
  if (dayIndex > anchorDay_) {
    return false;
  }
  const uint32_t delta = anchorDay_ - dayIndex;
  if (delta >= HISTORY_DAYS) {
    return false;
  }
  offset = static_cast<size_t>(delta);
  return true;
}

void ReadingJournal::addReadingSpan(const ReadingStatsDateTime& localStart, const uint32_t readingSeconds) {
  ReadingStatsDateTime cursor = localStart;
  uint32_t remaining = readingSeconds;
  while (remaining > 0) {
    const uint32_t dayIndex = readingStatsDayIndex(cursor.date);
    advanceAnchor(dayIndex);
    size_t offset = 0;
    if (!offsetForDay(dayIndex, offset)) {
      return;
    }
    const uint32_t secondOfDay =
        static_cast<uint32_t>(cursor.hour) * 3600u + static_cast<uint32_t>(cursor.minute) * 60u + cursor.second;
    const uint32_t secondsUntilMidnight = 24u * 3600u - secondOfDay;
    const uint32_t segment = std::min(remaining, secondsUntilMidnight);
    dailyReadingSeconds_[offset] = addSaturated(dailyReadingSeconds_[offset], segment);
    remaining -= segment;
    addSecondsToReadingStatsDateTime(cursor, segment);
  }
}

void ReadingJournal::addSession(const ReadingStatsDateTime& localStart, const uint32_t readingSeconds,
                                const uint16_t screenPages) {
  const uint32_t dayIndex = readingStatsDayIndex(localStart.date);
  advanceAnchor(dayIndex);
  size_t offset = 0;
  if (offsetForDay(dayIndex, offset)) {
    dailySessions_[offset] = static_cast<uint8_t>(std::min<int>(255, dailySessions_[offset] + 1));
    dailyScreenPages_[offset] = addSaturated16(dailyScreenPages_[offset], screenPages);
  }

  longestSessionSeconds_ = std::max(longestSessionSeconds_, readingSeconds);
  ReadingJournalSession& recent = recentSessions_[recentNext_];
  recent.dayIndex = dayIndex;
  recent.startMinute = static_cast<uint16_t>(localStart.hour * 60u + localStart.minute);
  recent.readingSeconds = readingSeconds;
  recent.screenPages = screenPages;
  recentNext_ = static_cast<uint8_t>((recentNext_ + 1) % RECENT_SESSION_COUNT);
  recentCount_ = static_cast<uint8_t>(std::min<size_t>(RECENT_SESSION_COUNT, recentCount_ + 1));
}

ReadingJournalPeriod ReadingJournal::periodEndingOn(const uint32_t dayIndex, const uint16_t days) const {
  ReadingJournalPeriod period;
  const uint16_t clampedDays = std::min<uint16_t>(days, HISTORY_DAYS);
  for (uint16_t i = 0; i < clampedDays; ++i) {
    if (dayIndex < i) {
      break;
    }
    size_t offset = 0;
    if (!offsetForDay(dayIndex - i, offset)) {
      continue;
    }
    period.readingSeconds = addSaturated(period.readingSeconds, dailyReadingSeconds_[offset]);
    period.screenPages = addSaturated(period.screenPages, dailyScreenPages_[offset]);
    period.sessions = addSaturated16(period.sessions, dailySessions_[offset]);
    period.completedBooks = addSaturated16(period.completedBooks, dailyCompletedBooks_[offset]);
    if (dailyReadingSeconds_[offset] > 0) {
      period.activeDays++;
    }
  }
  return period;
}

uint32_t ReadingJournal::secondsOnDay(const uint32_t dayIndex) const {
  size_t offset = 0;
  return offsetForDay(dayIndex, offset) ? dailyReadingSeconds_[offset] : 0;
}

uint32_t ReadingJournal::pagesOnDay(const uint32_t dayIndex) const {
  size_t offset = 0;
  return offsetForDay(dayIndex, offset) ? dailyScreenPages_[offset] : 0;
}

uint32_t ReadingJournal::sessionsOnDay(const uint32_t dayIndex) const {
  size_t offset = 0;
  return offsetForDay(dayIndex, offset) ? dailySessions_[offset] : 0;
}

uint16_t ReadingJournal::currentGoalStreak(const uint32_t todayDayIndex, const uint32_t goalSeconds) const {
  if (goalSeconds == 0) {
    return 0;
  }
  uint32_t cursor = todayDayIndex;
  if (secondsOnDay(cursor) < goalSeconds && cursor > 0) {
    cursor--;
  }
  uint16_t streak = 0;
  while (streak < HISTORY_DAYS && secondsOnDay(cursor) >= goalSeconds) {
    streak++;
    if (cursor == 0) {
      break;
    }
    cursor--;
  }
  return streak;
}

uint16_t ReadingJournal::longestGoalStreak(const uint32_t goalSeconds) const {
  if (goalSeconds == 0) {
    return 0;
  }
  uint16_t best = 0;
  uint16_t current = 0;
  for (size_t i = HISTORY_DAYS; i-- > 0;) {
    if (dailyReadingSeconds_[i] >= goalSeconds) {
      current++;
      best = std::max(best, current);
    } else {
      current = 0;
    }
  }
  return best;
}

uint16_t ReadingJournal::goalDaysEndingOn(const uint32_t dayIndex, const uint16_t days,
                                          const uint32_t goalSeconds) const {
  if (goalSeconds == 0) {
    return 0;
  }
  uint16_t goalDays = 0;
  const uint16_t clampedDays = std::min<uint16_t>(days, HISTORY_DAYS);
  for (uint16_t i = 0; i < clampedDays && dayIndex >= i; ++i) {
    if (secondsOnDay(dayIndex - i) >= goalSeconds) {
      goalDays++;
    }
  }
  return goalDays;
}

bool ReadingJournal::recentSession(const uint8_t newestOffset, ReadingJournalSession& out) const {
  if (newestOffset >= recentCount_) {
    return false;
  }
  const size_t index = (recentNext_ + RECENT_SESSION_COUNT - 1u - newestOffset) % RECENT_SESSION_COUNT;
  out = recentSessions_[index];
  return out.readingSeconds > 0 || out.screenPages > 0;
}

void ReadingJournal::mergeFromPeer(const ReadingJournal& peer) {
  for (size_t offset = 0; offset < HISTORY_DAYS; ++offset) {
    if (peer.anchorDay_ < offset) break;
    const uint32_t seconds = peer.dailyReadingSeconds_[offset];
    const uint16_t pages = peer.dailyScreenPages_[offset];
    const uint8_t sessions = peer.dailySessions_[offset];
    const uint8_t completed = peer.dailyCompletedBooks_[offset];
    if (seconds == 0 && pages == 0 && sessions == 0 && completed == 0) continue;

    const uint32_t dayIndex = peer.anchorDay_ - static_cast<uint32_t>(offset);
    advanceAnchor(dayIndex);
    size_t targetOffset = 0;
    if (!offsetForDay(dayIndex, targetOffset)) continue;
    dailyReadingSeconds_[targetOffset] = addSaturated(dailyReadingSeconds_[targetOffset], seconds);
    dailyScreenPages_[targetOffset] = addSaturated16(dailyScreenPages_[targetOffset], pages);
    dailySessions_[targetOffset] = static_cast<uint8_t>(std::min<int>(255, dailySessions_[targetOffset] + sessions));
    dailyCompletedBooks_[targetOffset] =
        static_cast<uint8_t>(std::min<int>(255, dailyCompletedBooks_[targetOffset] + completed));
  }

  longestSessionSeconds_ = std::max(longestSessionSeconds_, peer.longestSessionSeconds_);

  std::vector<ReadingJournalSession> sessions;
  sessions.reserve(RECENT_SESSION_COUNT * 2);
  for (uint8_t i = 0; i < recentCount_; ++i) {
    ReadingJournalSession session;
    if (recentSession(i, session)) sessions.push_back(session);
  }
  for (uint8_t i = 0; i < peer.recentCount_; ++i) {
    ReadingJournalSession session;
    if (peer.recentSession(i, session)) sessions.push_back(session);
  }
  std::sort(sessions.begin(), sessions.end(), [](const ReadingJournalSession& lhs, const ReadingJournalSession& rhs) {
    if (lhs.dayIndex != rhs.dayIndex) return lhs.dayIndex < rhs.dayIndex;
    return lhs.startMinute < rhs.startMinute;
  });
  if (sessions.size() > RECENT_SESSION_COUNT) {
    sessions.erase(sessions.begin(), sessions.end() - RECENT_SESSION_COUNT);
  }
  recentSessions_ = {};
  recentCount_ = static_cast<uint8_t>(sessions.size());
  recentNext_ = 0;
  for (const ReadingJournalSession& session : sessions) {
    recentSessions_[recentNext_] = session;
    recentNext_ = static_cast<uint8_t>((recentNext_ + 1) % RECENT_SESSION_COUNT);
  }
}

bool ReadingJournal::save(const bool rotateBackup) const {
  Storage.mkdir(DUET_STATE_ROOT_PATH "");
  const std::string tmpPath = std::string(JOURNAL_PATH) + ".tmp";
  if (Storage.exists(tmpPath.c_str()) && !Storage.remove(tmpPath.c_str())) {
    return false;
  }

  FsFile file;
  if (!Storage.openFileForWrite("RJNL", tmpPath.c_str(), file)) {
    return false;
  }

  const uint8_t reserved8 = 0;
  const uint16_t reserved16 = 0;
  bool ok = writePod(file, JOURNAL_MAGIC) && writePod(file, JOURNAL_VERSION) && writePod(file, reserved8) &&
            writePod(file, JOURNAL_FILE_SIZE) && writePod(file, anchorDay_) && writePod(file, longestSessionSeconds_) &&
            writePod(file, recentCount_) && writePod(file, recentNext_) && writePod(file, reserved16) &&
            writeArray(file, dailyReadingSeconds_) && writeArray(file, dailyScreenPages_) &&
            writeArray(file, dailySessions_) && writeArray(file, dailyCompletedBooks_);
  for (const auto& session : recentSessions_) {
    ok = ok && writePod(file, session.dayIndex) && writePod(file, session.startMinute) &&
         writePod(file, session.readingSeconds) && writePod(file, session.screenPages);
  }

  if (!ok) {
    file.close();
    Storage.remove(tmpPath.c_str());
    return false;
  }
  file.flush();
  const bool synced = file.sync();
  const bool closed = file.close();
  if (!synced || !closed || !verifyFileSize(tmpPath.c_str())) {
    Storage.remove(tmpPath.c_str());
    return false;
  }

  if (rotateBackup) {
    if (Storage.exists(JOURNAL_BACKUP_PATH)) {
      Storage.remove(JOURNAL_BACKUP_PATH);
    }
    if (Storage.exists(JOURNAL_PATH) && !Storage.rename(JOURNAL_PATH, JOURNAL_BACKUP_PATH)) {
      Storage.remove(tmpPath.c_str());
      return false;
    }
  } else if (Storage.exists(JOURNAL_PATH) && !Storage.remove(JOURNAL_PATH)) {
    Storage.remove(tmpPath.c_str());
    return false;
  }

  if (!Storage.rename(tmpPath.c_str(), JOURNAL_PATH)) {
    if (rotateBackup && Storage.exists(JOURNAL_BACKUP_PATH) && !Storage.exists(JOURNAL_PATH)) {
      Storage.rename(JOURNAL_BACKUP_PATH, JOURNAL_PATH);
    }
    Storage.remove(tmpPath.c_str());
    return false;
  }
  return true;
}
