#include "LibraryInsights.h"

#include <Arduino.h>
#include <Epub.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>
#include <esp_mac.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#include <vector>

#include "BookReadingStats.h"

namespace {
constexpr int MIN_CATALOG_VERSION = 1;
constexpr int MAX_CATALOG_VERSION = 2;
constexpr size_t MAX_CATALOG_LINE_BYTES = 2048;
constexpr size_t MAX_CATALOG_BOOKS = 5000;
constexpr size_t MAX_CATALOG_AUTHORS = 2000;
constexpr size_t MAX_CATALOG_SERIES = 2000;
constexpr size_t MAX_CATALOG_GENRES = 256;
constexpr size_t MAX_CATALOG_SPICE_LEVELS = 64;
constexpr char CACHE_TMP_PATH[] = DUET_STATE_ROOT_PATH "/library_insights_v1.tmp";
constexpr uint32_t CACHE_MAGIC = 0x324E494Cu;  // LIN2: series capacity raised to 48
constexpr uint8_t CACHE_VERSION = 2;
constexpr size_t MAX_CACHED_NAME_BYTES = 512;
constexpr char BOOK_STATS_INDEX_PATH[] = DUET_STATE_ROOT_PATH "/library_book_stats_v1.bin";
constexpr char BOOK_STATS_INDEX_TMP_PATH[] = DUET_STATE_ROOT_PATH "/library_book_stats_v1.tmp";
constexpr char BOOK_STATS_ALIAS_PATH[] = DUET_STATE_ROOT_PATH "/library_book_aliases_v1.bin";
constexpr char BOOK_STATS_ALIAS_TMP_PATH[] = DUET_STATE_ROOT_PATH "/library_book_aliases_v1.tmp";
constexpr char SYNCED_BOOK_STATS_DIR[] = DUET_STATE_ROOT_PATH "/synced_book_stats";
constexpr char BOOK_STATS_DETAIL_PATH[] = DUET_STATE_ROOT_PATH "/library_book_details_v1.bin";
constexpr char BOOK_STATS_DETAIL_TMP_PATH[] = DUET_STATE_ROOT_PATH "/library_book_details_v1.tmp";
// Present while library_book_details_v1.bin matches the on-disk stats. Removed
// on every stats save; lets repeat syncs skip the full cache-tree rebuild.
constexpr char BOOK_STATS_DETAIL_CLEAN_PATH[] = DUET_STATE_ROOT_PATH "/library_book_details_v1.clean";
constexpr char SYNCED_BOOK_DETAILS_DIR[] = DUET_STATE_ROOT_PATH "/synced_book_details";
constexpr uint32_t BOOK_STATS_INDEX_MAGIC = 0x5844494Cu;  // LIDX, little-endian on disk
constexpr uint8_t BOOK_STATS_INDEX_VERSION = 1;
constexpr uint32_t BOOK_STATS_ALIAS_MAGIC = 0x5341494Cu;  // LIAS, little-endian on disk
constexpr uint8_t BOOK_STATS_ALIAS_VERSION = 1;
constexpr uint32_t BOOK_STATS_DETAIL_MAGIC = 0x5445444Cu;  // LDET, little-endian on disk
constexpr uint8_t BOOK_STATS_DETAIL_VERSION = 1;
constexpr uint8_t BOOK_STATS_FLAG_COMPLETED = 1u << 0;
constexpr uint8_t BOOK_STATS_FLAG_PROGRESS = 1u << 1;
constexpr uint8_t BOOK_STATS_DETAIL_FLAG_START_MANUAL = 1u << 0;
constexpr uint8_t BOOK_STATS_DETAIL_FLAG_FINISHED_MANUAL = 1u << 1;
constexpr uint32_t BOOK_STATS_INDEX_HEADER_BYTES = 8;
constexpr uint32_t BOOK_STATS_INDEX_RECORD_BYTES = 15;
constexpr uint16_t MAX_MERGED_PACE_SAMPLES = 1000;
constexpr uint16_t MAX_BOOK_STATS_ALIASES = 256;

struct Aggregate {
  uint32_t readingSeconds = 0;
  uint16_t books = 0;
  uint16_t reading = 0;
  uint16_t finished = 0;
};

struct IndexedBookStats {
  uint64_t cacheKey = 0;
  uint32_t readingSeconds = 0;
  uint16_t sessions = 0;
  uint8_t flags = 0;
};

struct BookStatsAlias {
  uint64_t currentKey = 0;
  uint64_t canonicalKey = 0;
};

struct SharedBookStatsIndex {
  bool loadAttempted = false;
  bool syncedLoadAttempted = false;
  bool aliasesLoadAttempted = false;
  std::vector<IndexedBookStats> entries;
  std::vector<IndexedBookStats> syncedEntries;
  std::vector<BookStatsAlias> aliases;
};

SharedBookStatsIndex& sharedBookStatsIndex() {
  static SharedBookStatsIndex index;
  return index;
}

void invalidateSharedBookStatsIndex() {
  auto& index = sharedBookStatsIndex();
  index.loadAttempted = false;
  index.syncedLoadAttempted = false;
  index.aliasesLoadAttempted = false;
  index.entries.clear();
  index.syncedEntries.clear();
  index.aliases.clear();
}

uint64_t cachePathKey(const std::string& cachePath) {
  uint64_t hash = 14695981039346656037ULL;
  // Existing stats indexes and peer snapshots hashed the old /.crosspoint
  // cache path. Keep that logical identity stable after moving storage to
  // /.duet/books so a namespace migration cannot make a read book look new.
  const std::string stableIdentity = DuetStorage::stableBookCacheIdentity(cachePath);
  for (const unsigned char value : stableIdentity) {
    hash ^= value;
    hash *= 1099511628211ULL;
  }
  return hash;
}

bool indexedStatsBefore(const IndexedBookStats& entry, const uint64_t key) { return entry.cacheKey < key; }

bool bookStatsAliasBefore(const BookStatsAlias& entry, const uint64_t key) { return entry.currentKey < key; }

std::vector<IndexedBookStats>::iterator findIndexedStats(std::vector<IndexedBookStats>& entries, const uint64_t key) {
  return std::lower_bound(entries.begin(), entries.end(), key, indexedStatsBefore);
}

std::vector<IndexedBookStats>::const_iterator findIndexedStats(const std::vector<IndexedBookStats>& entries,
                                                               const uint64_t key) {
  return std::lower_bound(entries.begin(), entries.end(), key, indexedStatsBefore);
}

bool loadBookStatsIndexFromFile(FsFile& file, std::vector<IndexedBookStats>& entries);
uint64_t canonicalBookStatsKey(uint64_t key);
uint64_t canonicalBookStatsKey(const std::string& cachePath);

bool isBookCacheDirectoryName(const char* name) {
  return name && (strncmp(name, "epub_", 5) == 0 || strncmp(name, "xtc_", 4) == 0 || strncmp(name, "txt_", 4) == 0);
}

bool hasDetailedStats(const BookReadingStats& stats) {
  if (stats.sessionCount > 0 || stats.totalReadingSeconds > 0 || stats.countedSessionSeconds > 0 ||
      stats.totalPagesTurned > 0 || stats.isCompleted || stats.avgSecondsPerForwardPage > 0 ||
      stats.paceSampleCount > 0 || stats.estimatedTimeLeftSeconds > 0 || stats.latestSessionReadingSeconds > 0 ||
      stats.latestSessionScreenPages > 0 || stats.startDate.isValid() || stats.finishedDate.isValid()) {
    return true;
  }
  for (const uint32_t seconds : stats.timeOfDaySeconds) {
    if (seconds > 0) return true;
  }
  for (const uint32_t seconds : stats.dayOfWeekSeconds) {
    if (seconds > 0) return true;
  }
  return false;
}

uint32_t addSaturated(const uint32_t current, const uint32_t value) {
  return std::numeric_limits<uint32_t>::max() - current < value ? std::numeric_limits<uint32_t>::max()
                                                                : current + value;
}

uint16_t addSaturated16(const uint16_t current, const uint16_t value = 1) {
  return std::numeric_limits<uint16_t>::max() - current < value ? std::numeric_limits<uint16_t>::max()
                                                                : static_cast<uint16_t>(current + value);
}

bool readLine(FsFile& file, std::string& line) {
  line.clear();
  while (file.available() > 0) {
    const int value = file.read();
    if (value < 0) break;
    if (value == '\n') return true;
    if (value == '\r') continue;
    if (line.size() >= MAX_CATALOG_LINE_BYTES) {
      LOG_ERR("LIBCAT", "Catalog line exceeds %u bytes", static_cast<unsigned>(MAX_CATALOG_LINE_BYTES));
      return false;
    }
    line.push_back(static_cast<char>(value));
  }
  return !line.empty();
}

template <size_t N>
size_t splitFields(const std::string& line, std::array<std::string_view, N>& fields) {
  size_t count = 0;
  size_t start = 0;
  while (count < N) {
    const size_t tab = line.find('\t', start);
    if (tab == std::string::npos) {
      fields[count++] = std::string_view(line).substr(start);
      break;
    }
    fields[count++] = std::string_view(line).substr(start, tab - start);
    start = tab + 1;
  }
  return count;
}

bool parseInt(const std::string_view value, int& out) {
  if (value.empty() || value.size() >= 24) return false;
  char buffer[24];
  std::copy(value.begin(), value.end(), buffer);
  buffer[value.size()] = '\0';
  char* end = nullptr;
  const long parsed = std::strtol(buffer, &end, 10);
  if (end == buffer || *end != '\0' || parsed < std::numeric_limits<int>::min() ||
      parsed > std::numeric_limits<int>::max()) {
    return false;
  }
  out = static_cast<int>(parsed);
  return true;
}

bool validCount(const int value, const size_t maximum) { return value >= 0 && static_cast<size_t>(value) <= maximum; }

bool hasBookProgress(const std::string& cachePath) {
  return Storage.existsForRead(cachePath + "/progress.bin") || Storage.existsForRead(cachePath + "/progress.bin.bak");
}

bool loadBookStatsIndex(std::vector<IndexedBookStats>& entries) {
  entries.clear();
  FsFile file;
  if (!Storage.openFileForRead("LIBIDX", BOOK_STATS_INDEX_PATH, file)) return false;
  const bool ok = loadBookStatsIndexFromFile(file, entries);
  file.close();
  return ok;
}

bool loadBookStatsIndexFromFile(FsFile& file, std::vector<IndexedBookStats>& entries) {
  entries.clear();
  uint32_t magic = 0;
  uint8_t version = 0;
  uint8_t reserved = 0;
  uint16_t count = 0;
  bool ok = serialization::tryReadPod(file, magic) && serialization::tryReadPod(file, version) &&
            serialization::tryReadPod(file, reserved) && serialization::tryReadPod(file, count) &&
            magic == BOOK_STATS_INDEX_MAGIC && version == BOOK_STATS_INDEX_VERSION && count <= MAX_CATALOG_BOOKS;
  if (ok) entries.reserve(count);
  for (uint16_t i = 0; ok && i < count; ++i) {
    IndexedBookStats entry;
    ok = serialization::tryReadPod(file, entry.cacheKey) && serialization::tryReadPod(file, entry.readingSeconds) &&
         serialization::tryReadPod(file, entry.sessions) && serialization::tryReadPod(file, entry.flags);
    if (ok && !entries.empty() && entries.back().cacheKey >= entry.cacheKey) ok = false;
    if (ok) entries.push_back(entry);
  }
  if (!ok) entries.clear();
  return ok;
}

bool loadBookStatsAliases(std::vector<BookStatsAlias>& aliases) {
  aliases.clear();
  FsFile file;
  if (!Storage.openFileForRead("LIBALS", BOOK_STATS_ALIAS_PATH, file)) return false;

  uint32_t magic = 0;
  uint8_t version = 0;
  uint8_t reserved = 0;
  uint16_t count = 0;
  bool ok = serialization::tryReadPod(file, magic) && serialization::tryReadPod(file, version) &&
            serialization::tryReadPod(file, reserved) && serialization::tryReadPod(file, count) &&
            magic == BOOK_STATS_ALIAS_MAGIC && version == BOOK_STATS_ALIAS_VERSION && count <= MAX_BOOK_STATS_ALIASES &&
            file.size() == 8u + static_cast<uint32_t>(count) * 16u;
  if (ok) aliases.reserve(count);
  for (uint16_t i = 0; ok && i < count; ++i) {
    BookStatsAlias alias;
    ok = serialization::tryReadPod(file, alias.currentKey) && serialization::tryReadPod(file, alias.canonicalKey) &&
         alias.currentKey != 0 && alias.canonicalKey != 0 && alias.currentKey != alias.canonicalKey &&
         (aliases.empty() || aliases.back().currentKey < alias.currentKey);
    if (ok) aliases.push_back(alias);
  }
  file.close();
  if (!ok) aliases.clear();
  return ok;
}

bool saveBookStatsAliases(const std::vector<BookStatsAlias>& aliases) {
  if (aliases.size() > MAX_BOOK_STATS_ALIASES || aliases.size() > std::numeric_limits<uint16_t>::max()) return false;
  for (size_t i = 0; i < aliases.size(); ++i) {
    if (aliases[i].currentKey == 0 || aliases[i].canonicalKey == 0 ||
        aliases[i].currentKey == aliases[i].canonicalKey ||
        (i > 0 && aliases[i - 1].currentKey >= aliases[i].currentKey)) {
      return false;
    }
  }

  Storage.mkdir(DUET_STATE_ROOT_PATH "");
  if (Storage.exists(BOOK_STATS_ALIAS_TMP_PATH)) Storage.remove(BOOK_STATS_ALIAS_TMP_PATH);
  FsFile file;
  if (!Storage.openFileForWrite("LIBALS", BOOK_STATS_ALIAS_TMP_PATH, file)) return false;

  const uint8_t reserved = 0;
  const uint16_t count = static_cast<uint16_t>(aliases.size());
  bool ok = serialization::tryWritePod(file, BOOK_STATS_ALIAS_MAGIC) &&
            serialization::tryWritePod(file, BOOK_STATS_ALIAS_VERSION) && serialization::tryWritePod(file, reserved) &&
            serialization::tryWritePod(file, count);
  for (const BookStatsAlias& alias : aliases) {
    ok = ok && serialization::tryWritePod(file, alias.currentKey) &&
         serialization::tryWritePod(file, alias.canonicalKey);
  }
  file.flush();
  ok = ok && file.sync();
  ok = file.close() && ok;
  if (!ok) {
    Storage.remove(BOOK_STATS_ALIAS_TMP_PATH);
    return false;
  }
  if (Storage.exists(BOOK_STATS_ALIAS_PATH) && !Storage.remove(BOOK_STATS_ALIAS_PATH)) {
    Storage.remove(BOOK_STATS_ALIAS_TMP_PATH);
    return false;
  }
  if (!Storage.rename(BOOK_STATS_ALIAS_TMP_PATH, BOOK_STATS_ALIAS_PATH)) {
    Storage.remove(BOOK_STATS_ALIAS_TMP_PATH);
    return false;
  }
  return true;
}

const std::vector<BookStatsAlias>& loadedBookStatsAliases() {
  auto& shared = sharedBookStatsIndex();
  if (!shared.aliasesLoadAttempted) {
    shared.aliasesLoadAttempted = true;
    loadBookStatsAliases(shared.aliases);
  }
  return shared.aliases;
}

uint64_t resolveBookStatsAlias(uint64_t key, const std::vector<BookStatsAlias>& aliases) {
  for (size_t depth = 0; depth <= aliases.size(); ++depth) {
    const auto found = std::lower_bound(aliases.begin(), aliases.end(), key, bookStatsAliasBefore);
    if (found == aliases.end() || found->currentKey != key || found->canonicalKey == key) return key;
    key = found->canonicalKey;
  }
  return key;
}

uint64_t canonicalBookStatsKey(const uint64_t key) { return resolveBookStatsAlias(key, loadedBookStatsAliases()); }

uint64_t canonicalBookStatsKey(const std::string& cachePath) { return canonicalBookStatsKey(cachePathKey(cachePath)); }

std::string localSyncedBookStatsFileName() {
  uint8_t mac[6] = {};
  if (esp_efuse_mac_get_default(mac) != 0) return {};

  char name[32];
  snprintf(name, sizeof(name), "device_%02x%02x%02x%02x%02x%02x.bin", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return name;
}

std::string localSyncedBookStatsPath() {
  const std::string name = localSyncedBookStatsFileName();
  return name.empty() ? std::string{} : std::string(SYNCED_BOOK_STATS_DIR) + "/" + name;
}

void mergeIndexedBookStats(std::vector<IndexedBookStats>& target, const IndexedBookStats& source) {
  const auto found = findIndexedStats(target, source.cacheKey);
  if (found != target.end() && found->cacheKey == source.cacheKey) {
    found->readingSeconds = addSaturated(found->readingSeconds, source.readingSeconds);
    found->sessions = addSaturated16(found->sessions, source.sessions);
    found->flags |= source.flags;
  } else {
    target.insert(found, source);
  }
}

bool loadSyncedBookStatsIndex(std::vector<IndexedBookStats>& entries) {
  entries.clear();
  FsFile dir = Storage.open(SYNCED_BOOK_STATS_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }

  const std::string localFileName = localSyncedBookStatsFileName();
  char name[128];
  uint16_t loadedCount = 0;
  for (FsFile file = dir.openNextFile(); file; file = dir.openNextFile()) {
    const bool isDirectory = file.isDirectory();
    const size_t nameLen = file.getName(name, sizeof(name));
    if (!isDirectory && nameLen > 0 && (localFileName.empty() || strcmp(name, localFileName.c_str()) != 0)) {
      std::vector<IndexedBookStats> peerEntries;
      if (loadBookStatsIndexFromFile(file, peerEntries)) {
        for (IndexedBookStats entry : peerEntries) {
          entry.cacheKey = canonicalBookStatsKey(entry.cacheKey);
          mergeIndexedBookStats(entries, entry);
        }
        loadedCount++;
      }
    }
    file.close();
  }
  dir.close();
  if (loadedCount > 0) {
    LOG_DBG("LIBIDX", "Loaded %u synced book stats index(es)", static_cast<unsigned>(loadedCount));
  }
  return loadedCount > 0;
}

bool saveBookStatsIndex(const std::vector<IndexedBookStats>& entries) {
  if (entries.size() > MAX_CATALOG_BOOKS || entries.size() > std::numeric_limits<uint16_t>::max()) return false;
  Storage.mkdir(DUET_STATE_ROOT_PATH "");
  if (Storage.exists(BOOK_STATS_INDEX_TMP_PATH)) Storage.remove(BOOK_STATS_INDEX_TMP_PATH);

  FsFile file;
  if (!Storage.openFileForWrite("LIBIDX", BOOK_STATS_INDEX_TMP_PATH, file)) return false;
  const uint8_t reserved = 0;
  const uint16_t count = static_cast<uint16_t>(entries.size());
  bool ok = serialization::tryWritePod(file, BOOK_STATS_INDEX_MAGIC) &&
            serialization::tryWritePod(file, BOOK_STATS_INDEX_VERSION) && serialization::tryWritePod(file, reserved) &&
            serialization::tryWritePod(file, count);
  for (const IndexedBookStats& entry : entries) {
    ok = ok && serialization::tryWritePod(file, entry.cacheKey) &&
         serialization::tryWritePod(file, entry.readingSeconds) && serialization::tryWritePod(file, entry.sessions) &&
         serialization::tryWritePod(file, entry.flags);
  }
  file.close();
  if (!ok) {
    Storage.remove(BOOK_STATS_INDEX_TMP_PATH);
    return false;
  }
  if (Storage.exists(BOOK_STATS_INDEX_PATH)) Storage.remove(BOOK_STATS_INDEX_PATH);
  if (!Storage.rename(BOOK_STATS_INDEX_TMP_PATH, BOOK_STATS_INDEX_PATH)) {
    Storage.remove(BOOK_STATS_INDEX_TMP_PATH);
    return false;
  }
  return true;
}

bool copyFileAtomically(const char* sourcePath, const char* destinationPath) {
  FsFile source;
  if (!Storage.openFileForRead("LIBIDX", sourcePath, source)) return false;

  const std::string tmpPath = std::string(destinationPath) + ".part";
  if (Storage.exists(tmpPath.c_str())) Storage.remove(tmpPath.c_str());

  FsFile destination;
  if (!Storage.openFileForWrite("LIBIDX", tmpPath, destination)) {
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

bool writeDate(FsFile& file, const ReadingStatsDate& date) {
  const uint16_t year = date.isValid() ? date.year : 0;
  const uint8_t month = date.isValid() ? date.month : 0;
  const uint8_t day = date.isValid() ? date.day : 0;
  return serialization::tryWritePod(file, year) && serialization::tryWritePod(file, month) &&
         serialization::tryWritePod(file, day);
}

bool readDate(FsFile& file, ReadingStatsDate& date) {
  if (!serialization::tryReadPod(file, date.year) || !serialization::tryReadPod(file, date.month) ||
      !serialization::tryReadPod(file, date.day)) {
    return false;
  }
  if (!date.isValid()) date.clear();
  return true;
}

bool writeDetailedBookStats(FsFile& file, const uint64_t cacheKey, const BookReadingStats& stats) {
  const uint8_t completed = stats.isCompleted ? 1 : 0;
  const uint8_t flags = (stats.startDateManual ? BOOK_STATS_DETAIL_FLAG_START_MANUAL : 0u) |
                        (stats.finishedDateManual ? BOOK_STATS_DETAIL_FLAG_FINISHED_MANUAL : 0u);
  bool ok = serialization::tryWritePod(file, cacheKey) && serialization::tryWritePod(file, stats.sessionCount) &&
            serialization::tryWritePod(file, stats.totalReadingSeconds) &&
            serialization::tryWritePod(file, stats.countedSessionSeconds) &&
            serialization::tryWritePod(file, stats.totalPagesTurned) && serialization::tryWritePod(file, completed) &&
            serialization::tryWritePod(file, stats.avgSecondsPerForwardPage) &&
            serialization::tryWritePod(file, stats.paceSampleCount) &&
            serialization::tryWritePod(file, stats.estimatedTimeLeftSeconds) &&
            serialization::tryWritePod(file, stats.latestSessionReadingSeconds) &&
            serialization::tryWritePod(file, stats.latestSessionScreenPages) &&
            serialization::tryWritePod(file, flags) && writeDate(file, stats.startDate) &&
            writeDate(file, stats.finishedDate);
  for (const uint32_t seconds : stats.timeOfDaySeconds) ok = ok && serialization::tryWritePod(file, seconds);
  for (const uint32_t seconds : stats.dayOfWeekSeconds) ok = ok && serialization::tryWritePod(file, seconds);
  return ok && serialization::tryWritePod(file, stats.latestSessionDayIndex) &&
         serialization::tryWritePod(file, stats.latestSessionStartMinute);
}

bool readDetailedBookStats(FsFile& file, uint64_t& cacheKey, BookReadingStats& stats) {
  uint8_t completed = 0;
  uint8_t flags = 0;
  bool ok = serialization::tryReadPod(file, cacheKey) && serialization::tryReadPod(file, stats.sessionCount) &&
            serialization::tryReadPod(file, stats.totalReadingSeconds) &&
            serialization::tryReadPod(file, stats.countedSessionSeconds) &&
            serialization::tryReadPod(file, stats.totalPagesTurned) && serialization::tryReadPod(file, completed) &&
            serialization::tryReadPod(file, stats.avgSecondsPerForwardPage) &&
            serialization::tryReadPod(file, stats.paceSampleCount) &&
            serialization::tryReadPod(file, stats.estimatedTimeLeftSeconds) &&
            serialization::tryReadPod(file, stats.latestSessionReadingSeconds) &&
            serialization::tryReadPod(file, stats.latestSessionScreenPages) && serialization::tryReadPod(file, flags) &&
            readDate(file, stats.startDate) && readDate(file, stats.finishedDate);
  for (uint32_t& seconds : stats.timeOfDaySeconds) ok = ok && serialization::tryReadPod(file, seconds);
  for (uint32_t& seconds : stats.dayOfWeekSeconds) ok = ok && serialization::tryReadPod(file, seconds);
  ok = ok && serialization::tryReadPod(file, stats.latestSessionDayIndex) &&
       serialization::tryReadPod(file, stats.latestSessionStartMinute);
  stats.isCompleted = completed != 0;
  stats.startDateManual = (flags & BOOK_STATS_DETAIL_FLAG_START_MANUAL) != 0;
  stats.finishedDateManual = (flags & BOOK_STATS_DETAIL_FLAG_FINISHED_MANUAL) != 0;
  return ok;
}

bool dateBefore(const ReadingStatsDate& left, const ReadingStatsDate& right) {
  return left.isValid() && (!right.isValid() || compareReadingStatsDate(left, right) < 0);
}

void mergeDate(ReadingStatsDate& target, bool& targetManual, const ReadingStatsDate& source, const bool sourceManual) {
  if (!source.isValid()) return;
  if (!target.isValid() || (sourceManual && !targetManual) ||
      (sourceManual == targetManual && dateBefore(source, target))) {
    target = source;
  }
  targetManual = targetManual || sourceManual;
}

void mergeDetailedStats(BookReadingStats& target, const BookReadingStats& source) {
  const uint32_t targetPaceSamples = target.paceSampleCount;
  const uint32_t sourcePaceSamples = source.paceSampleCount;
  if (sourcePaceSamples > 0 && source.avgSecondsPerForwardPage > 0) {
    if (targetPaceSamples > 0 && target.avgSecondsPerForwardPage > 0) {
      const uint32_t combined = targetPaceSamples + sourcePaceSamples;
      const uint64_t weighted = static_cast<uint64_t>(target.avgSecondsPerForwardPage) * targetPaceSamples +
                                static_cast<uint64_t>(source.avgSecondsPerForwardPage) * sourcePaceSamples;
      target.avgSecondsPerForwardPage = static_cast<uint16_t>(weighted / combined);
      target.paceSampleCount = static_cast<uint16_t>(std::min<uint32_t>(MAX_MERGED_PACE_SAMPLES, combined));
    } else {
      target.avgSecondsPerForwardPage = source.avgSecondsPerForwardPage;
      target.paceSampleCount = std::min<uint16_t>(MAX_MERGED_PACE_SAMPLES, source.paceSampleCount);
    }
  }

  const uint64_t targetLatest =
      static_cast<uint64_t>(target.latestSessionDayIndex) * 1440u + target.latestSessionStartMinute;
  const uint64_t sourceLatest =
      static_cast<uint64_t>(source.latestSessionDayIndex) * 1440u + source.latestSessionStartMinute;
  if ((sourceLatest > targetLatest) ||
      (targetLatest == 0 && target.latestSessionReadingSeconds == 0 && source.latestSessionReadingSeconds > 0)) {
    target.latestSessionReadingSeconds = source.latestSessionReadingSeconds;
    target.latestSessionScreenPages = source.latestSessionScreenPages;
    target.latestSessionDayIndex = source.latestSessionDayIndex;
    target.latestSessionStartMinute = source.latestSessionStartMinute;
    target.estimatedTimeLeftSeconds = source.estimatedTimeLeftSeconds;
  } else if (target.estimatedTimeLeftSeconds == 0) {
    target.estimatedTimeLeftSeconds = source.estimatedTimeLeftSeconds;
  }

  target.sessionCount = addSaturated16(target.sessionCount, source.sessionCount);
  target.totalReadingSeconds = addSaturated(target.totalReadingSeconds, source.totalReadingSeconds);
  target.countedSessionSeconds = addSaturated(target.countedSessionSeconds, source.countedSessionSeconds);
  target.totalPagesTurned = addSaturated(target.totalPagesTurned, source.totalPagesTurned);
  target.isCompleted = target.isCompleted || source.isCompleted;
  mergeDate(target.startDate, target.startDateManual, source.startDate, source.startDateManual);
  mergeDate(target.finishedDate, target.finishedDateManual, source.finishedDate, source.finishedDateManual);
  for (size_t i = 0; i < target.timeOfDaySeconds.size(); ++i) {
    target.timeOfDaySeconds[i] = addSaturated(target.timeOfDaySeconds[i], source.timeOfDaySeconds[i]);
  }
  for (size_t i = 0; i < target.dayOfWeekSeconds.size(); ++i) {
    target.dayOfWeekSeconds[i] = addSaturated(target.dayOfWeekSeconds[i], source.dayOfWeekSeconds[i]);
  }
}

bool mergeDetailedBookStatsFromFile(FsFile& file, const uint64_t wantedKey, BookReadingStats& stats) {
  uint32_t magic = 0;
  uint8_t version = 0;
  uint8_t reserved = 0;
  uint16_t count = 0;
  if (!serialization::tryReadPod(file, magic) || !serialization::tryReadPod(file, version) ||
      !serialization::tryReadPod(file, reserved) || !serialization::tryReadPod(file, count) ||
      magic != BOOK_STATS_DETAIL_MAGIC || version != BOOK_STATS_DETAIL_VERSION || count > MAX_CATALOG_BOOKS) {
    return false;
  }
  for (uint16_t i = 0; i < count; ++i) {
    uint64_t key = 0;
    BookReadingStats source;
    if (!readDetailedBookStats(file, key, source)) return false;
    if (canonicalBookStatsKey(key) == wantedKey) {
      mergeDetailedStats(stats, source);
      return true;
    }
  }
  return false;
}

bool overwriteIndexedBookStats(const IndexedBookStats& replacement) {
  FsFile file = Storage.open(BOOK_STATS_INDEX_PATH, O_RDWR);
  if (!file) return false;
  uint32_t magic = 0;
  uint8_t version = 0;
  uint8_t reserved = 0;
  uint16_t count = 0;
  bool ok = serialization::tryReadPod(file, magic) && serialization::tryReadPod(file, version) &&
            serialization::tryReadPod(file, reserved) && serialization::tryReadPod(file, count) &&
            magic == BOOK_STATS_INDEX_MAGIC && version == BOOK_STATS_INDEX_VERSION && count <= MAX_CATALOG_BOOKS;
  for (uint16_t i = 0; ok && i < count; ++i) {
    IndexedBookStats current;
    ok = serialization::tryReadPod(file, current.cacheKey) && serialization::tryReadPod(file, current.readingSeconds) &&
         serialization::tryReadPod(file, current.sessions) && serialization::tryReadPod(file, current.flags);
    if (!ok) break;
    if (current.cacheKey > replacement.cacheKey) break;
    if (current.cacheKey != replacement.cacheKey) continue;
    const uint32_t offset = BOOK_STATS_INDEX_HEADER_BYTES + static_cast<uint32_t>(i) * BOOK_STATS_INDEX_RECORD_BYTES;
    ok = file.seek(offset) && serialization::tryWritePod(file, replacement.cacheKey) &&
         serialization::tryWritePod(file, replacement.readingSeconds) &&
         serialization::tryWritePod(file, replacement.sessions) && serialization::tryWritePod(file, replacement.flags);
    file.flush();
    ok = ok && file.sync();
    file.close();
    return ok;
  }
  file.close();
  return false;
}

IndexedBookStats indexedBookStats(const std::string& cachePath, const BookReadingStats& stats) {
  IndexedBookStats entry;
  entry.cacheKey = canonicalBookStatsKey(cachePath);
  entry.readingSeconds = stats.totalReadingSeconds;
  entry.sessions = stats.sessionCount;
  if (stats.isCompleted) entry.flags |= BOOK_STATS_FLAG_COMPLETED;
  if (hasBookProgress(cachePath)) entry.flags |= BOOK_STATS_FLAG_PROGRESS;
  return entry;
}

void upsertIndexedBookStats(std::vector<IndexedBookStats>& entries, const IndexedBookStats& entry) {
  const auto found = findIndexedStats(entries, entry.cacheKey);
  if (found != entries.end() && found->cacheKey == entry.cacheKey) {
    *found = entry;
  } else {
    entries.insert(found, entry);
  }
}

BookReadingStats bookStatsFromIndex(const IndexedBookStats& entry) {
  BookReadingStats stats;
  stats.totalReadingSeconds = entry.readingSeconds;
  stats.sessionCount = entry.sessions;
  stats.isCompleted = (entry.flags & BOOK_STATS_FLAG_COMPLETED) != 0;
  return stats;
}

void addIndexToBookStats(BookReadingStats& stats, const IndexedBookStats& entry) {
  stats.totalReadingSeconds = addSaturated(stats.totalReadingSeconds, entry.readingSeconds);
  stats.sessionCount = addSaturated16(stats.sessionCount, entry.sessions);
  stats.isCompleted = stats.isCompleted || (entry.flags & BOOK_STATS_FLAG_COMPLETED) != 0;
}

void addBook(Aggregate& aggregate, const BookReadingStats& stats, const bool reading) {
  aggregate.books = addSaturated16(aggregate.books);
  aggregate.readingSeconds = addSaturated(aggregate.readingSeconds, stats.totalReadingSeconds);
  if (stats.isCompleted) {
    aggregate.finished = addSaturated16(aggregate.finished);
  } else if (reading) {
    aggregate.reading = addSaturated16(aggregate.reading);
  }
}

LibraryInsightItem itemFor(const std::string& name, const Aggregate& aggregate) {
  return {name, aggregate.readingSeconds, aggregate.books, aggregate.reading, aggregate.finished};
}

bool ranksBefore(const Aggregate& left, const Aggregate& right) {
  if (left.readingSeconds != right.readingSeconds) return left.readingSeconds > right.readingSeconds;
  if (left.finished != right.finished) return left.finished > right.finished;
  if (left.reading != right.reading) return left.reading > right.reading;
  return left.books > right.books;
}

template <size_t N>
size_t populateTopItems(const std::vector<std::string>& names, const std::vector<Aggregate>& aggregates,
                        std::array<LibraryInsightItem, N>& destination, const bool requireActivity) {
  std::vector<size_t> order;
  order.reserve(aggregates.size());
  for (size_t i = 0; i < aggregates.size(); ++i) {
    if (!names[i].empty() && (!requireActivity || aggregates[i].reading > 0 || aggregates[i].finished > 0 ||
                              aggregates[i].readingSeconds > 0)) {
      order.push_back(i);
    }
  }
  std::stable_sort(order.begin(), order.end(), [&aggregates, &names](const size_t left, const size_t right) {
    if (ranksBefore(aggregates[left], aggregates[right])) return true;
    if (ranksBefore(aggregates[right], aggregates[left])) return false;
    return names[left] < names[right];
  });
  const size_t count = std::min(N, order.size());
  for (size_t i = 0; i < count; ++i) {
    destination[i] = itemFor(names[order[i]], aggregates[order[i]]);
  }
  return count;
}

bool catalogFingerprint(uint64_t& fingerprint) {
  FsFile file;
  if (!Storage.openFileForRead("LIBCAT", LibraryInsights::CATALOG_PATH, file)) return false;

  uint64_t hash = 14695981039346656037ULL;
  uint8_t buffer[512];
  while (file.available() > 0) {
    const int count = file.read(buffer, sizeof(buffer));
    if (count <= 0) {
      file.close();
      return false;
    }
    for (int i = 0; i < count; ++i) {
      hash ^= buffer[i];
      hash *= 1099511628211ULL;
    }
  }
  file.close();
  fingerprint = hash;
  return true;
}

bool readCachedString(FsFile& file, std::string& value) {
  uint32_t length = 0;
  if (!serialization::tryReadPod(file, length) || length > MAX_CACHED_NAME_BYTES ||
      file.available() < static_cast<int>(length)) {
    return false;
  }
  value.resize(length);
  return length == 0 || file.read(&value[0], static_cast<int>(length)) == static_cast<int>(length);
}

bool writeCachedItem(FsFile& file, const LibraryInsightItem& item) {
  return item.name.size() <= MAX_CACHED_NAME_BYTES && serialization::tryWriteString(file, item.name) &&
         serialization::tryWritePod(file, item.readingSeconds) && serialization::tryWritePod(file, item.books) &&
         serialization::tryWritePod(file, item.reading) && serialization::tryWritePod(file, item.finished);
}

bool readCachedItem(FsFile& file, LibraryInsightItem& item) {
  return readCachedString(file, item.name) && serialization::tryReadPod(file, item.readingSeconds) &&
         serialization::tryReadPod(file, item.books) && serialization::tryReadPod(file, item.reading) &&
         serialization::tryReadPod(file, item.finished);
}

template <size_t N>
bool writeCachedItems(FsFile& file, const std::array<LibraryInsightItem, N>& items, const size_t count) {
  if (count > N) return false;
  for (size_t i = 0; i < count; ++i) {
    if (!writeCachedItem(file, items[i])) return false;
  }
  return true;
}

template <size_t N>
bool readCachedItems(FsFile& file, std::array<LibraryInsightItem, N>& items, const size_t count) {
  if (count > N) return false;
  for (size_t i = 0; i < count; ++i) {
    if (!readCachedItem(file, items[i])) return false;
  }
  return true;
}

bool loadCachedInsights(const uint64_t fingerprint, LibraryInsights& insights) {
  FsFile file;
  if (!Storage.openFileForRead("LIBCAT", LibraryInsights::CACHE_PATH, file)) return false;

  uint32_t magic = 0;
  uint8_t version = 0;
  uint8_t available = 0;
  uint16_t reserved = 0;
  uint64_t cachedFingerprint = 0;
  uint8_t topGenreCount = 0;
  uint8_t topAuthorCount = 0;
  uint8_t spiceLevelCount = 0;
  uint8_t seriesProgressCount = 0;
  const bool headerOk =
      serialization::tryReadPod(file, magic) && serialization::tryReadPod(file, version) &&
      serialization::tryReadPod(file, available) && serialization::tryReadPod(file, reserved) &&
      serialization::tryReadPod(file, cachedFingerprint) && magic == CACHE_MAGIC && version == CACHE_VERSION &&
      available == 1 && cachedFingerprint == fingerprint && serialization::tryReadPod(file, insights.totalBooks) &&
      serialization::tryReadPod(file, insights.unreadBooks) && serialization::tryReadPod(file, insights.readingBooks) &&
      serialization::tryReadPod(file, insights.finishedBooks) &&
      serialization::tryReadPod(file, insights.seriesStarted) &&
      serialization::tryReadPod(file, insights.totalReadingSeconds) && serialization::tryReadPod(file, topGenreCount) &&
      serialization::tryReadPod(file, topAuthorCount) && serialization::tryReadPod(file, spiceLevelCount) &&
      serialization::tryReadPod(file, seriesProgressCount);
  if (!headerOk || topGenreCount > insights.topGenres.size() || topAuthorCount > insights.topAuthors.size() ||
      spiceLevelCount > insights.spiceLevels.size() || seriesProgressCount > insights.seriesProgress.size()) {
    file.close();
    return false;
  }

  insights.topGenreCount = topGenreCount;
  insights.topAuthorCount = topAuthorCount;
  insights.spiceLevelCount = spiceLevelCount;
  insights.seriesProgressCount = seriesProgressCount;
  const bool itemsOk = readCachedItems(file, insights.topGenres, insights.topGenreCount) &&
                       readCachedItems(file, insights.topAuthors, insights.topAuthorCount) &&
                       readCachedItems(file, insights.spiceLevels, insights.spiceLevelCount) &&
                       readCachedItems(file, insights.seriesProgress, insights.seriesProgressCount);
  file.close();
  insights.available = itemsOk;
  return itemsOk;
}

bool saveCachedInsights(const uint64_t fingerprint, const LibraryInsights& insights) {
  Storage.mkdir(DUET_STATE_ROOT_PATH "");
  if (Storage.exists(CACHE_TMP_PATH)) Storage.remove(CACHE_TMP_PATH);

  FsFile file;
  if (!Storage.openFileForWrite("LIBCAT", CACHE_TMP_PATH, file)) return false;
  const uint8_t available = insights.available ? 1 : 0;
  const uint16_t reserved = 0;
  const uint8_t topGenreCount = static_cast<uint8_t>(insights.topGenreCount);
  const uint8_t topAuthorCount = static_cast<uint8_t>(insights.topAuthorCount);
  const uint8_t spiceLevelCount = static_cast<uint8_t>(insights.spiceLevelCount);
  const uint8_t seriesProgressCount = static_cast<uint8_t>(insights.seriesProgressCount);
  const bool ok =
      serialization::tryWritePod(file, CACHE_MAGIC) && serialization::tryWritePod(file, CACHE_VERSION) &&
      serialization::tryWritePod(file, available) && serialization::tryWritePod(file, reserved) &&
      serialization::tryWritePod(file, fingerprint) && serialization::tryWritePod(file, insights.totalBooks) &&
      serialization::tryWritePod(file, insights.unreadBooks) &&
      serialization::tryWritePod(file, insights.readingBooks) &&
      serialization::tryWritePod(file, insights.finishedBooks) &&
      serialization::tryWritePod(file, insights.seriesStarted) &&
      serialization::tryWritePod(file, insights.totalReadingSeconds) &&
      serialization::tryWritePod(file, topGenreCount) && serialization::tryWritePod(file, topAuthorCount) &&
      serialization::tryWritePod(file, spiceLevelCount) && serialization::tryWritePod(file, seriesProgressCount) &&
      writeCachedItems(file, insights.topGenres, insights.topGenreCount) &&
      writeCachedItems(file, insights.topAuthors, insights.topAuthorCount) &&
      writeCachedItems(file, insights.spiceLevels, insights.spiceLevelCount) &&
      writeCachedItems(file, insights.seriesProgress, insights.seriesProgressCount);
  file.close();
  if (!ok) {
    Storage.remove(CACHE_TMP_PATH);
    return false;
  }
  if (Storage.exists(LibraryInsights::CACHE_PATH)) Storage.remove(LibraryInsights::CACHE_PATH);
  if (!Storage.rename(CACHE_TMP_PATH, LibraryInsights::CACHE_PATH)) {
    Storage.remove(CACHE_TMP_PATH);
    return false;
  }
  return true;
}
}  // namespace

std::unique_ptr<LibraryInsights> LibraryInsights::load() {
  const unsigned long startedAt = millis();
  std::unique_ptr<LibraryInsights> insights(new (std::nothrow) LibraryInsights());
  if (!insights) {
    LOG_ERR("LIBCAT", "Not enough memory for library insights");
    return nullptr;
  }

  uint64_t fingerprint = 0;
  if (!catalogFingerprint(fingerprint)) {
    LOG_DBG("LIBCAT", "No library catalog at %s", CATALOG_PATH);
    return insights;
  }
  if (loadCachedInsights(fingerprint, *insights)) {
    LOG_INF("LIBCAT", "Loaded cached insights for %u books in %lums", static_cast<unsigned>(insights->totalBooks),
            millis() - startedAt);
    return insights;
  }

  FsFile file;
  if (!Storage.openFileForRead("LIBCAT", CATALOG_PATH, file)) return insights;

  std::string line;
  line.reserve(512);
  if (!readLine(file, line)) {
    file.close();
    return insights;
  }

  std::array<std::string_view, 8> fields{};
  if (splitFields(line, fields) != 7 || fields[0] != "M") {
    LOG_ERR("LIBCAT", "Invalid catalog header");
    file.close();
    return insights;
  }

  int version = 0;
  int expectedBooks = 0;
  int authorCount = 0;
  int seriesCount = 0;
  int genreCount = 0;
  int spiceCount = 0;
  if (!parseInt(fields[1], version) || version < MIN_CATALOG_VERSION || version > MAX_CATALOG_VERSION ||
      !parseInt(fields[2], expectedBooks) || !parseInt(fields[3], authorCount) || !parseInt(fields[4], seriesCount) ||
      !parseInt(fields[5], genreCount) || !parseInt(fields[6], spiceCount) ||
      !validCount(expectedBooks, MAX_CATALOG_BOOKS) || !validCount(authorCount, MAX_CATALOG_AUTHORS) ||
      !validCount(seriesCount, MAX_CATALOG_SERIES) || !validCount(genreCount, MAX_CATALOG_GENRES) ||
      !validCount(spiceCount, MAX_CATALOG_SPICE_LEVELS)) {
    LOG_ERR("LIBCAT", "Unsupported or oversized catalog header");
    file.close();
    return insights;
  }

  std::vector<std::string> authors(static_cast<size_t>(authorCount));
  std::vector<std::string> series(static_cast<size_t>(seriesCount));
  std::vector<std::string> genres(static_cast<size_t>(genreCount));
  std::vector<std::string> spices(static_cast<size_t>(spiceCount));
  std::vector<Aggregate> authorStats(authors.size());
  std::vector<Aggregate> seriesStats(series.size());
  std::vector<Aggregate> genreStats(genres.size());
  std::vector<Aggregate> spiceStats(spices.size());
  std::vector<IndexedBookStats> bookStatsIndex;
  std::vector<IndexedBookStats> syncedBookStatsIndex;
  bool bookStatsIndexChanged = !loadBookStatsIndex(bookStatsIndex);
  loadSyncedBookStatsIndex(syncedBookStatsIndex);
  size_t indexedBookCount = 0;
  size_t statsFileReadCount = 0;

  int processedBooks = 0;
  while (readLine(file, line)) {
    const size_t count = splitFields(line, fields);
    if (count < 3 || fields[0].size() != 1) continue;
    const char recordType = fields[0][0];
    int id = -1;
    if (recordType != 'B') {
      if (!parseInt(fields[1], id)) continue;
      std::vector<std::string>* dictionary = nullptr;
      if (recordType == 'A') dictionary = &authors;
      if (recordType == 'S') dictionary = &series;
      if (recordType == 'G') dictionary = &genres;
      if (recordType == 'P') dictionary = &spices;
      if (dictionary && id >= 0 && static_cast<size_t>(id) < dictionary->size()) {
        (*dictionary)[static_cast<size_t>(id)] = std::string(fields[2]);
      }
      continue;
    }

    // Catalog v2 appends title and description to the original eight fields.
    // Rejecting anything except exactly eight silently discarded every book
    // and made an installed v2 catalog look missing.
    if (count < 8) continue;
    int calibreId = 0;
    int authorId = -1;
    int seriesId = -1;
    int genreId = -1;
    int spiceId = -1;
    if (!parseInt(fields[1], calibreId) || !parseInt(fields[2], authorId) || !parseInt(fields[3], seriesId) ||
        !parseInt(fields[4], genreId) || !parseInt(fields[5], spiceId)) {
      continue;
    }
    (void)calibreId;
    if (authorId < 0 || static_cast<size_t>(authorId) >= authors.size() || genreId < 0 ||
        static_cast<size_t>(genreId) >= genres.size() || spiceId < -1 ||
        (spiceId >= 0 && static_cast<size_t>(spiceId) >= spices.size()) ||
        (seriesId >= 0 && static_cast<size_t>(seriesId) >= series.size())) {
      continue;
    }

    const std::string path(fields[7]);
    const std::string cachePath = Epub::cachePathForFilePath(path, DUET_BOOKS_ROOT_PATH "");
    const uint64_t statsKey = canonicalBookStatsKey(cachePath);
    const auto indexed = findIndexedStats(bookStatsIndex, statsKey);
    BookReadingStats stats;
    bool hasProgress = false;
    if (indexed != bookStatsIndex.end() && indexed->cacheKey == statsKey) {
      stats = bookStatsFromIndex(*indexed);
      hasProgress = (indexed->flags & BOOK_STATS_FLAG_PROGRESS) != 0;
      indexedBookCount++;
    } else {
      stats = BookReadingStats::load(cachePath);
      statsFileReadCount++;
      const IndexedBookStats entry = indexedBookStats(cachePath, stats);
      hasProgress = (entry.flags & BOOK_STATS_FLAG_PROGRESS) != 0;
      upsertIndexedBookStats(bookStatsIndex, entry);
      bookStatsIndexChanged = true;
    }
    const auto syncedIndexed = findIndexedStats(syncedBookStatsIndex, statsKey);
    if (syncedIndexed != syncedBookStatsIndex.end() && syncedIndexed->cacheKey == statsKey) {
      addIndexToBookStats(stats, *syncedIndexed);
      hasProgress = hasProgress || (syncedIndexed->flags & BOOK_STATS_FLAG_PROGRESS) != 0;
    }
    const bool reading = !stats.isCompleted && (stats.sessionCount > 0 || stats.totalReadingSeconds > 0 || hasProgress);

    insights->totalBooks = addSaturated16(insights->totalBooks);
    insights->totalReadingSeconds = addSaturated(insights->totalReadingSeconds, stats.totalReadingSeconds);
    if (stats.isCompleted) {
      insights->finishedBooks = addSaturated16(insights->finishedBooks);
    } else if (reading) {
      insights->readingBooks = addSaturated16(insights->readingBooks);
    } else {
      insights->unreadBooks = addSaturated16(insights->unreadBooks);
    }

    addBook(authorStats[static_cast<size_t>(authorId)], stats, reading);
    addBook(genreStats[static_cast<size_t>(genreId)], stats, reading);
    if (spiceId >= 0) addBook(spiceStats[static_cast<size_t>(spiceId)], stats, reading);
    if (seriesId >= 0) addBook(seriesStats[static_cast<size_t>(seriesId)], stats, reading);
    processedBooks++;
  }
  file.close();

  if (processedBooks != expectedBooks) {
    LOG_ERR("LIBCAT", "Catalog book count mismatch: expected=%d processed=%d", expectedBooks, processedBooks);
    return insights;
  }

  if (bookStatsIndexChanged && !saveBookStatsIndex(bookStatsIndex)) {
    LOG_ERR("LIBIDX", "Could not save per-book stats index");
  } else if (bookStatsIndexChanged) {
    invalidateSharedBookStatsIndex();
  }

  insights->topGenreCount = populateTopItems(genres, genreStats, insights->topGenres, false);
  insights->topAuthorCount = populateTopItems(authors, authorStats, insights->topAuthors, true);
  insights->seriesProgressCount = populateTopItems(series, seriesStats, insights->seriesProgress, true);
  insights->spiceLevelCount = std::min(insights->spiceLevels.size(), spices.size());
  for (size_t i = 0; i < insights->spiceLevelCount; ++i) {
    insights->spiceLevels[i] = itemFor(spices[i], spiceStats[i]);
  }
  for (const Aggregate& aggregate : seriesStats) {
    if (aggregate.reading > 0 || aggregate.finished > 0) {
      insights->seriesStarted = addSaturated16(insights->seriesStarted);
    }
  }
  insights->available = true;
  if (!saveCachedInsights(fingerprint, *insights)) {
    LOG_ERR("LIBCAT", "Could not save library insights cache");
  }
  LOG_INF("LIBCAT", "Rebuilt insights for %u books in %lums (index=%u file_reads=%u)",
          static_cast<unsigned>(insights->totalBooks), millis() - startedAt, static_cast<unsigned>(indexedBookCount),
          static_cast<unsigned>(statsFileReadCount));
  return insights;
}

bool LibraryInsights::lookupBookStatus(const std::string& cachePath, LibraryBookStatus& status) {
  status = LibraryBookStatus{};
  if (cachePath.empty()) return false;

  auto& shared = sharedBookStatsIndex();
  if (!shared.loadAttempted) {
    shared.loadAttempted = true;
    loadBookStatsIndex(shared.entries);
  }
  if (!shared.syncedLoadAttempted) {
    shared.syncedLoadAttempted = true;
    loadSyncedBookStatsIndex(shared.syncedEntries);
  }
  const uint64_t key = canonicalBookStatsKey(cachePath);
  const auto found = findIndexedStats(shared.entries, key);
  bool foundAny = false;
  if (found != shared.entries.end() && found->cacheKey == key) {
    status.readingSeconds = addSaturated(status.readingSeconds, found->readingSeconds);
    status.sessions = addSaturated16(status.sessions, found->sessions);
    status.completed = status.completed || (found->flags & BOOK_STATS_FLAG_COMPLETED) != 0;
    status.hasProgress = status.hasProgress || (found->flags & BOOK_STATS_FLAG_PROGRESS) != 0;
    foundAny = true;
  }

  const auto syncedFound = findIndexedStats(shared.syncedEntries, key);
  if (syncedFound != shared.syncedEntries.end() && syncedFound->cacheKey == key) {
    status.readingSeconds = addSaturated(status.readingSeconds, syncedFound->readingSeconds);
    status.sessions = addSaturated16(status.sessions, syncedFound->sessions);
    status.completed = status.completed || (syncedFound->flags & BOOK_STATS_FLAG_COMPLETED) != 0;
    status.hasProgress = status.hasProgress || (syncedFound->flags & BOOK_STATS_FLAG_PROGRESS) != 0;
    foundAny = true;
  }

  return foundAny;
}

namespace {
bool mergeDetailedBookStatsFile(FsFile& file, std::vector<uint64_t>& keys, std::vector<BookReadingStats>& stats) {
  uint32_t magic = 0;
  uint8_t version = 0;
  uint8_t reserved = 0;
  uint16_t count = 0;
  if (!serialization::tryReadPod(file, magic) || !serialization::tryReadPod(file, version) ||
      !serialization::tryReadPod(file, reserved) || !serialization::tryReadPod(file, count) ||
      magic != BOOK_STATS_DETAIL_MAGIC || version != BOOK_STATS_DETAIL_VERSION || count > MAX_CATALOG_BOOKS) {
    return false;
  }
  for (uint16_t i = 0; i < count; ++i) {
    uint64_t key = 0;
    BookReadingStats source;
    if (!readDetailedBookStats(file, key, source)) return false;
    const uint64_t canonicalKey = canonicalBookStatsKey(key);
    const auto found = std::find(keys.begin(), keys.end(), canonicalKey);
    if (found == keys.end()) {
      keys.push_back(canonicalKey);
      stats.push_back(source);
    } else {
      const size_t index = static_cast<size_t>(std::distance(keys.begin(), found));
      if (index < stats.size()) mergeDetailedStats(stats[index], source);
    }
  }
  return true;
}
}  // namespace

uint64_t LibraryInsights::keyForCachePath(const std::string& cachePath) { return canonicalBookStatsKey(cachePath); }

bool LibraryInsights::registerMovedBookStatsAlias(const std::string& oldCachePath, const std::string& newCachePath) {
  if (oldCachePath.empty() || newCachePath.empty()) return false;

  const uint64_t oldKey = cachePathKey(oldCachePath);
  const uint64_t newKey = cachePathKey(newCachePath);
  if (oldKey == newKey) return true;

  std::vector<BookStatsAlias> aliases = loadedBookStatsAliases();
  const uint64_t canonicalKey = resolveBookStatsAlias(oldKey, aliases);
  const auto found = std::lower_bound(aliases.begin(), aliases.end(), newKey, bookStatsAliasBefore);
  if (newKey == canonicalKey) {
    if (found == aliases.end() || found->currentKey != newKey) return true;
    aliases.erase(found);
  } else if (found != aliases.end() && found->currentKey == newKey) {
    if (found->canonicalKey == canonicalKey) return true;
    found->canonicalKey = canonicalKey;
  } else {
    if (aliases.size() >= MAX_BOOK_STATS_ALIASES) return false;
    aliases.insert(found, BookStatsAlias{newKey, canonicalKey});
  }

  if (!saveBookStatsAliases(aliases)) return false;
  invalidateSharedBookStatsIndex();
  invalidateDetailedStatsSnapshot();
  LOG_DBG("LIBALS", "Mapped moved book stats key %016llx -> %016llx", static_cast<unsigned long long>(newKey),
          static_cast<unsigned long long>(canonicalKey));
  return true;
}

void LibraryInsights::forEachDetailedBookStats(const DetailedBookStatsVisitor visitor, void* ctx) {
  if (visitor == nullptr) return;
  std::vector<uint64_t> mergedKeys;
  std::vector<BookReadingStats> mergedStats;
  mergedKeys.reserve(64);
  mergedStats.reserve(64);

  FsFile localFile;
  if (Storage.openFileForRead("LIBDET", BOOK_STATS_DETAIL_PATH, localFile)) {
    mergeDetailedBookStatsFile(localFile, mergedKeys, mergedStats);
    localFile.close();
  }

  FsFile dir = Storage.open(SYNCED_BOOK_DETAILS_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    for (size_t i = 0; i < mergedKeys.size() && i < mergedStats.size(); ++i)
      visitor(mergedKeys[i], mergedStats[i], ctx);
    return;
  }
  const std::string localFileName = localSyncedBookStatsFileName();
  char name[128];
  for (FsFile file = dir.openNextFile(); file; file = dir.openNextFile()) {
    const bool isDirectory = file.isDirectory();
    const size_t nameLen = file.getName(name, sizeof(name));
    if (!isDirectory && nameLen > 0 && (localFileName.empty() || strcmp(name, localFileName.c_str()) != 0)) {
      mergeDetailedBookStatsFile(file, mergedKeys, mergedStats);
    }
    file.close();
  }
  dir.close();
  for (size_t i = 0; i < mergedKeys.size() && i < mergedStats.size(); ++i) visitor(mergedKeys[i], mergedStats[i], ctx);
}

void LibraryInsights::mergeSyncedBookStats(const std::string& cachePath, BookReadingStats& stats) {
  if (cachePath.empty()) return;
  FsFile dir = Storage.open(SYNCED_BOOK_DETAILS_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  const uint64_t key = canonicalBookStatsKey(cachePath);
  const std::string localFileName = localSyncedBookStatsFileName();
  char name[128];
  for (FsFile file = dir.openNextFile(); file; file = dir.openNextFile()) {
    const bool isDirectory = file.isDirectory();
    const size_t nameLen = file.getName(name, sizeof(name));
    if (!isDirectory && nameLen > 0 && (localFileName.empty() || strcmp(name, localFileName.c_str()) != 0)) {
      mergeDetailedBookStatsFromFile(file, key, stats);
    }
    file.close();
  }
  dir.close();
}

void LibraryInsights::updateBookStatsIndex(const std::string& cachePath, const BookReadingStats& stats) {
  if (cachePath.empty()) {
    invalidateCache();
    return;
  }

  const IndexedBookStats replacement = indexedBookStats(cachePath, stats);
  if (!overwriteIndexedBookStats(replacement)) {
    std::vector<IndexedBookStats> entries;
    loadBookStatsIndex(entries);
    upsertIndexedBookStats(entries, replacement);
    if (!saveBookStatsIndex(entries)) {
      LOG_ERR("LIBIDX", "Could not update per-book stats index");
    }
  }
  invalidateSharedBookStatsIndex();
  invalidateCache();
  // Keep the published sync snapshot current in place; only a failure forces
  // the next sync to re-crawl.
  if (!updateDetailedBookStatsInPlace(cachePath, stats)) {
    invalidateDetailedStatsSnapshot();
  }
}

bool LibraryInsights::publishLocalBookStatsIndexForSync() {
  if (!Storage.existsForRead(BOOK_STATS_INDEX_PATH) || !Storage.ensureDirectoryExists(DUET_STATE_ROOT_PATH "") ||
      !Storage.ensureDirectoryExists(SYNCED_BOOK_STATS_DIR)) {
    return false;
  }

  const std::string path = localSyncedBookStatsPath();
  if (path.empty()) return false;

  return copyFileAtomically(BOOK_STATS_INDEX_PATH, path.c_str());
}

bool LibraryInsights::publishLocalDetailedBookStatsForSync(SyncPrepProgress progress, void* progressCtx) {
  const unsigned long prepStartMs = millis();
  if (!Storage.ensureDirectoryExists(DUET_STATE_ROOT_PATH "") ||
      !Storage.ensureDirectoryExists(SYNCED_BOOK_DETAILS_DIR)) {
    return false;
  }
  if (Storage.existsForRead(BOOK_STATS_DETAIL_CLEAN_PATH) && Storage.existsForRead(BOOK_STATS_DETAIL_PATH)) {
    LOG_DBG("LIBDET", "Detailed per-book stats unchanged since last publish; reusing snapshot");
    return true;
  }
  if (Storage.exists(BOOK_STATS_DETAIL_TMP_PATH)) Storage.remove(BOOK_STATS_DETAIL_TMP_PATH);

  FsFile output;
  if (!Storage.openFileForWrite("LIBDET", BOOK_STATS_DETAIL_TMP_PATH, output)) return false;
  const uint8_t reserved = 0;
  uint16_t count = 0;
  bool ok = serialization::tryWritePod(output, BOOK_STATS_DETAIL_MAGIC) &&
            serialization::tryWritePod(output, BOOK_STATS_DETAIL_VERSION) &&
            serialization::tryWritePod(output, reserved) && serialization::tryWritePod(output, count);

  // The per-book index (updated in place on every stats save) names, by path
  // hash, exactly the books that have persisted stats. Restrict full stats
  // loads to those: probing candidate stats filenames in EVERY cache
  // directory ran for minutes on large libraries and froze the Sync screen.
  // Books whose stats predate the index self-heal into it on their next save.
  std::vector<IndexedBookStats> indexEntries;
  const bool haveIndex = loadBookStatsIndex(indexEntries) && !indexEntries.empty();
  const unsigned long indexLoadedMs = millis();
  std::vector<uint64_t> indexedKeys;
  indexedKeys.reserve(indexEntries.size());
  for (const auto& entry : indexEntries) indexedKeys.push_back(entry.cacheKey);
  std::sort(indexedKeys.begin(), indexedKeys.end());

  uint16_t scannedDirs = 0;
  bool aborted = false;
  const auto scanStatsRoot = [&](const char* scanRootPath, const bool legacyRoot) {
    FsFile root = Storage.open(scanRootPath);
    if (!root || !root.isDirectory()) {
      if (root) root.close();
      return true;
    }

    char name[96];
    for (FsFile entry = root.openNextFile(); ok && entry; entry = root.openNextFile()) {
      const bool isDirectory = entry.isDirectory();
      const size_t nameLen = entry.getName(name, sizeof(name));
      entry.close();
      // Every entry: at slow-crawl pace a sparser poll made a Back tap invisible
      // (sampled input; presses between polls are lost).
      if (progress && !progress(progressCtx, ++scannedDirs, count)) {
        aborted = true;
        break;
      }
      if (!isDirectory || nameLen == 0 || !isBookCacheDirectoryName(name)) continue;

      const std::string cachePath = std::string(DUET_BOOKS_ROOT_PATH "/") + name;
      // Canonical directories are scanned first. Their individual reads still
      // fall back to legacy files when a lazy import has not copied that file.
      if (legacyRoot && Storage.exists(cachePath.c_str())) continue;
      const uint64_t statsKey = canonicalBookStatsKey(cachePath);
      if (haveIndex && !std::binary_search(indexedKeys.begin(), indexedKeys.end(), statsKey)) {
        continue;
      }
      const BookReadingStats stats = BookReadingStats::load(cachePath);
      if (!hasDetailedStats(stats)) continue;
      if (count == std::numeric_limits<uint16_t>::max()) {
        ok = false;
        break;
      }
      ok = writeDetailedBookStats(output, statsKey, stats);
      if (ok) count++;
    }
    root.close();
    return ok && !aborted;
  };

  ok = scanStatsRoot(DUET_BOOKS_ROOT_PATH, false);
  if (ok) ok = scanStatsRoot(DUET_LEGACY_BOOKS_ROOT_PATH, true);
  if (aborted) {
    output.close();
    Storage.remove(BOOK_STATS_DETAIL_TMP_PATH);
    LOG_INF("LIBDET", "Detailed stats publication aborted by user after %u dirs", scannedDirs);
    return false;
  }

  if (ok) {
    ok = output.seek(6) && serialization::tryWritePod(output, count);
    output.flush();
    ok = ok && output.sync();
  }
  ok = output.close() && ok;
  if (!ok) {
    Storage.remove(BOOK_STATS_DETAIL_TMP_PATH);
    return false;
  }
  if (Storage.exists(BOOK_STATS_DETAIL_PATH) && !Storage.remove(BOOK_STATS_DETAIL_PATH)) {
    Storage.remove(BOOK_STATS_DETAIL_TMP_PATH);
    return false;
  }
  if (!Storage.rename(BOOK_STATS_DETAIL_TMP_PATH, BOOK_STATS_DETAIL_PATH)) {
    Storage.remove(BOOK_STATS_DETAIL_TMP_PATH);
    return false;
  }
  // Clean marker: without it the next sync re-crawls the whole tree, so treat
  // a failed write as loud news, and verify it actually landed.
  FsFile cleanMarker;
  bool markerOk = false;
  if (Storage.openFileForWrite("LIBDET", BOOK_STATS_DETAIL_CLEAN_PATH, cleanMarker)) {
    markerOk = cleanMarker.close() && Storage.exists(BOOK_STATS_DETAIL_CLEAN_PATH);
  }
  if (!markerOk) {
    LOG_ERR("LIBDET", "Clean marker write FAILED — next sync will re-crawl");
  }
  // Prep timing breadcrumb for remote diagnosis of slow syncs.
  FsFile timing;
  if (Storage.openFileForWrite("LIBDET", DUET_STATE_ROOT_PATH "/sync_prep_timing.txt", timing)) {
    char buf[160];
    const int n = snprintf(buf, sizeof(buf), "index=%s indexLoad=%lums walk=%lums scanned=%u written=%u total=%lums\n",
                           haveIndex ? "yes" : "no", indexLoadedMs - prepStartMs, millis() - indexLoadedMs, scannedDirs,
                           count, millis() - prepStartMs);
    if (n > 0) timing.write(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(n));
    timing.close();
  }
  LOG_DBG("LIBDET", "Published %u detailed per-book stats record(s)", static_cast<unsigned>(count));
  return true;
}

void LibraryInsights::invalidateCache() {
  invalidateSharedBookStatsIndex();
  if (Storage.exists(CACHE_PATH)) Storage.remove(CACHE_PATH);
  if (Storage.exists(CACHE_TMP_PATH)) Storage.remove(CACHE_TMP_PATH);
}

void LibraryInsights::invalidateDetailedStatsSnapshot() {
  if (Storage.exists(BOOK_STATS_DETAIL_CLEAN_PATH)) Storage.remove(BOOK_STATS_DETAIL_CLEAN_PATH);
}

bool LibraryInsights::updateDetailedBookStatsInPlace(const std::string& cachePath, const BookReadingStats& stats) {
  if (!Storage.existsForRead(BOOK_STATS_DETAIL_PATH)) return false;
  FsFile file = Storage.open(BOOK_STATS_DETAIL_PATH, O_RDWR);
  if (!file) return false;

  uint32_t magic = 0;
  uint8_t version = 0;
  uint8_t reserved = 0;
  uint16_t count = 0;
  bool headerOk = serialization::tryReadPod(file, magic) && magic == BOOK_STATS_DETAIL_MAGIC &&
                  serialization::tryReadPod(file, version) && version == BOOK_STATS_DETAIL_VERSION &&
                  serialization::tryReadPod(file, reserved) && serialization::tryReadPod(file, count);
  const uint32_t fileSize = file.size();
  // Records are fixed-size; derive the stride from the file so this stays in
  // lockstep with writeDetailedBookStats without duplicating its layout.
  const uint32_t recordSize = (headerOk && count > 0) ? (fileSize - 8) / count : 0;
  if (!headerOk || (count > 0 && (recordSize == 0 || (fileSize - 8) % count != 0))) {
    file.close();
    return false;
  }

  const uint64_t key = canonicalBookStatsKey(cachePath);
  for (uint16_t i = 0; i < count; i++) {
    uint64_t recordKey = 0;
    if (!file.seek(8 + static_cast<uint32_t>(i) * recordSize) || !serialization::tryReadPod(file, recordKey)) {
      file.close();
      return false;
    }
    if (recordKey == key) {
      bool ok = file.seek(8 + static_cast<uint32_t>(i) * recordSize) && writeDetailedBookStats(file, key, stats);
      file.flush();
      ok = ok && file.sync();
      ok = file.close() && ok;
      return ok;
    }
  }

  if (!hasDetailedStats(stats)) {
    file.close();
    return true;
  }
  if (count == std::numeric_limits<uint16_t>::max()) {
    file.close();
    return false;
  }
  const uint32_t appendPos = 8 + static_cast<uint32_t>(count) * recordSize;
  bool ok = file.seek(appendPos) && writeDetailedBookStats(file, key, stats);
  if (ok && count > 0 && file.size() != appendPos + recordSize) {
    // Record layout drifted from the derived stride — refuse and rebuild.
    file.close();
    return false;
  }
  count++;
  ok = ok && file.seek(6) && serialization::tryWritePod(file, count);
  file.flush();
  ok = ok && file.sync();
  ok = file.close() && ok;
  return ok;
}
