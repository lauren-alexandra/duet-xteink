#include "ReadingLedger.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <freertos/task.h>
#include <uzlib.h>

#ifndef SIMULATOR
#include <esp_task_wdt.h>
#endif

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>

namespace {
constexpr char LOG_TAG[] = "RLEDGER";
constexpr char SYNCED_LEDGER_DIR[] = DUET_STATE_ROOT_PATH "/synced_ledgers";
constexpr uint32_t HEADER_MAGIC = 0x5247444Cu;  // LDGR
constexpr uint32_t RECORD_MAGIC = 0x4452474Cu;  // LGRD
constexpr uint8_t FILE_VERSION = 1;
constexpr size_t HEADER_SIZE = 12;
constexpr size_t RECORD_SIZE = 128;
constexpr size_t RECORD_CRC_OFFSET = 124;
constexpr uint8_t FLAG_SESSION = 1u << 0;
constexpr uint8_t FLAG_CORRECTION = 1u << 1;
constexpr uint32_t SCAN_YIELD_INTERVAL = 64;

void writeLe16(uint8_t* data, const size_t offset, const uint16_t value) {
  data[offset] = static_cast<uint8_t>(value & 0xffu);
  data[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
}

void writeLe32(uint8_t* data, const size_t offset, const uint32_t value) {
  data[offset] = static_cast<uint8_t>(value & 0xffu);
  data[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
  data[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xffu);
  data[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xffu);
}

uint16_t readLe16(const uint8_t* data, const size_t offset) {
  return static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8);
}

uint32_t readLe32(const uint8_t* data, const size_t offset) {
  return static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
         (static_cast<uint32_t>(data[offset + 2]) << 16) | (static_cast<uint32_t>(data[offset + 3]) << 24);
}

uint32_t crc32(const uint8_t* data, const size_t size) {
  return uzlib_crc32(data, static_cast<unsigned int>(size), 0);
}

void copyFixed(const std::string& source, uint8_t* target, const size_t size) {
  memset(target, 0, size);
  if (size == 0) return;
  const size_t copyLength = std::min(source.size(), size - 1);
  if (copyLength > 0) memcpy(target, source.data(), copyLength);
}

bool readHeader(FsFile& file) {
  uint8_t header[HEADER_SIZE] = {};
  if (!file.seek(0) || file.read(header, sizeof(header)) != static_cast<int>(sizeof(header))) return false;
  return readLe32(header, 0) == HEADER_MAGIC && header[4] == FILE_VERSION &&
         readLe16(header, 6) == RECORD_SIZE && readLe32(header, 8) == crc32(header, 8);
}

bool createLedgerFile() {
  Storage.mkdir(DUET_STATE_ROOT_PATH "");
  const std::string temporaryPath = std::string(ReadingLedger::PATH) + ".tmp";
  if (Storage.exists(temporaryPath.c_str()) && !Storage.remove(temporaryPath.c_str())) return false;

  uint8_t header[HEADER_SIZE] = {};
  writeLe32(header, 0, HEADER_MAGIC);
  header[4] = FILE_VERSION;
  writeLe16(header, 6, RECORD_SIZE);
  writeLe32(header, 8, crc32(header, 8));

  FsFile file;
  if (!Storage.openFileForWrite(LOG_TAG, temporaryPath, file)) return false;
  const bool written = file.write(header, sizeof(header)) == sizeof(header);
  file.flush();
  const bool synced = written && file.sync();
  const bool closed = file.close();
  if (!synced || !closed) {
    Storage.remove(temporaryPath.c_str());
    return false;
  }
  if (Storage.exists(ReadingLedger::PATH) && !Storage.remove(ReadingLedger::PATH)) {
    Storage.remove(temporaryPath.c_str());
    return false;
  }
  if (!Storage.rename(temporaryPath.c_str(), ReadingLedger::PATH)) {
    Storage.remove(temporaryPath.c_str());
    return false;
  }
  return true;
}

bool ensureLedgerFile() {
  if (!Storage.existsForRead(ReadingLedger::PATH)) return createLedgerFile();
  FsFile file;
  if (!Storage.openFileForRead(LOG_TAG, ReadingLedger::PATH, file)) return false;
  const bool valid = file.fileSize() >= HEADER_SIZE && readHeader(file);
  file.close();
  if (!valid) LOG_ERR(LOG_TAG, "Ledger header is invalid; preserving file and refusing destructive repair");
  return valid;
}

void yieldDuringScan(const uint32_t records) {
  if (records == 0 || records % SCAN_YIELD_INTERVAL != 0) return;
  vTaskDelay(1);
#ifndef SIMULATOR
  esp_task_wdt_reset();
#endif
}

uint32_t addSaturated(const uint32_t current, const uint32_t value) {
  return std::numeric_limits<uint32_t>::max() - current < value ? std::numeric_limits<uint32_t>::max()
                                                                : current + value;
}

bool sameBook(const ReadingLedgerDayBook& book, const char* cachePath, const char* title) {
  if (cachePath[0] != '\0' && book.cachePath[0] != '\0') {
    return DuetStorage::sameBookCacheIdentity(book.cachePath, cachePath);
  }
  return strcmp(book.title, title) == 0;
}

struct AggregatedBook {
  char cachePath[READING_LEDGER_CACHE_PATH_BYTES] = {};
  char title[READING_LEDGER_TITLE_BYTES] = {};
  int64_t readingSeconds = 0;
  uint32_t screenPages = 0;
};

}  // namespace

bool ReadingLedger::appendRecord(const uint32_t dayIndex, const int32_t readingSecondsDelta,
                                 const uint16_t screenPages, const uint8_t flags, const std::string& cachePath,
                                 const std::string& title) {
  if (dayIndex == 0 || (readingSecondsDelta == 0 && screenPages == 0) || !ensureLedgerFile()) return false;

  uint8_t record[RECORD_SIZE] = {};
  writeLe32(record, 0, RECORD_MAGIC);
  writeLe32(record, 4, dayIndex);
  writeLe32(record, 8, static_cast<uint32_t>(readingSecondsDelta));
  writeLe16(record, 12, screenPages);
  record[14] = flags;
  copyFixed(cachePath, record + 16, READING_LEDGER_CACHE_PATH_BYTES);
  copyFixed(title, record + 64, READING_LEDGER_TITLE_BYTES);
  writeLe32(record, RECORD_CRC_OFFSET, crc32(record, RECORD_CRC_OFFSET));

  FsFile file = Storage.open(PATH, O_WRONLY | O_APPEND);
  if (!file) {
    LOG_ERR(LOG_TAG, "Could not append to %s", PATH);
    return false;
  }
  const bool written = file.write(record, sizeof(record)) == sizeof(record);
  file.flush();
  const bool synced = written && file.sync();
  const bool closed = file.close();
  if (!synced || !closed) LOG_ERR(LOG_TAG, "Ledger append failed or was incomplete");
  return synced && closed;
}

bool ReadingLedger::recordReadingSpan(const ReadingStatsDateTime& localStart, const uint32_t readingSeconds,
                                      const uint16_t screenPages, const std::string& cachePath,
                                      const std::string& title) {
  if (!localStart.isValid() || (readingSeconds == 0 && screenPages == 0)) return false;

  ReadingStatsDateTime cursor = localStart;
  uint32_t remaining = readingSeconds;
  bool first = true;
  bool ok = true;
  do {
    const uint32_t secondOfDay = static_cast<uint32_t>(cursor.hour) * 3600u +
                                 static_cast<uint32_t>(cursor.minute) * 60u + cursor.second;
    const uint32_t untilMidnight = 24u * 3600u - secondOfDay;
    const uint32_t segment = std::min(remaining, untilMidnight);
    ok = appendRecord(readingStatsDayIndex(cursor.date), static_cast<int32_t>(segment), first ? screenPages : 0,
                      first && screenPages > 0 ? FLAG_SESSION : 0, cachePath, title) &&
         ok;
    if (remaining == 0) break;
    remaining -= segment;
    addSecondsToReadingStatsDateTime(cursor, segment);
    first = false;
  } while (remaining > 0);
  return ok;
}

bool ReadingLedger::recordCorrection(const ReadingStatsDate& date, const int32_t readingSecondsDelta,
                                     const std::string& cachePath, const std::string& title) {
  if (!date.isValid() || readingSecondsDelta == 0) return false;
  return appendRecord(readingStatsDayIndex(date), readingSecondsDelta, 0, FLAG_CORRECTION, cachePath, title);
}

namespace {
// Accumulates one ledger file's records for a day into the shared aggregate.
// Missing files are not an error; unreadable ones only fail for the local
// ledger (peer copies are best-effort).
bool scanLedgerFileForDay(const char* path, const uint32_t dayIndex,
                          std::array<AggregatedBook, READING_LEDGER_DAY_BOOK_LIMIT>& aggregated,
                          uint8_t& aggregateCount, int64_t& otherSeconds, ReadingLedgerDaySummary& out) {
  if (!Storage.exists(path)) return true;
  FsFile file;
  if (!Storage.openFileForRead(LOG_TAG, path, file) || !readHeader(file)) {
    if (file) file.close();
    return false;
  }
  const size_t fileSize = file.fileSize();
  size_t cursor = HEADER_SIZE;
  uint32_t scannedRecords = 0;
  uint8_t record[RECORD_SIZE] = {};
  while (cursor + 4 <= fileSize) {
    uint8_t magicBytes[4] = {};
    if (!file.seek(cursor) || file.read(magicBytes, sizeof(magicBytes)) != static_cast<int>(sizeof(magicBytes))) break;
    if (readLe32(magicBytes, 0) != RECORD_MAGIC) {
      cursor++;
      continue;
    }
    if (cursor + RECORD_SIZE > fileSize || !file.seek(cursor) ||
        file.read(record, sizeof(record)) != static_cast<int>(sizeof(record))) {
      break;
    }
    if (readLe32(record, RECORD_CRC_OFFSET) != crc32(record, RECORD_CRC_OFFSET)) {
      out.invalidRecordCount++;
      cursor++;
      continue;
    }
    cursor += RECORD_SIZE;
    scannedRecords++;
    yieldDuringScan(scannedRecords);
    if (readLe32(record, 4) != dayIndex) continue;
    out.validRecordCount++;

    const int32_t delta = static_cast<int32_t>(readLe32(record, 8));
    const uint16_t pages = readLe16(record, 12);
    const char* cachePath = reinterpret_cast<const char*>(record + 16);
    const char* title = reinterpret_cast<const char*>(record + 64);
    uint8_t match = aggregateCount;
    for (uint8_t i = 0; i < aggregateCount; ++i) {
      ReadingLedgerDayBook candidate;
      memcpy(candidate.cachePath, aggregated[i].cachePath, sizeof(candidate.cachePath));
      memcpy(candidate.title, aggregated[i].title, sizeof(candidate.title));
      if (sameBook(candidate, cachePath, title)) {
        match = i;
        break;
      }
    }
    if (match == aggregateCount) {
      if (aggregateCount >= aggregated.size()) {
        otherSeconds += delta;
        out.truncated = true;
        continue;
      }
      memcpy(aggregated[match].cachePath, cachePath, sizeof(aggregated[match].cachePath));
      memcpy(aggregated[match].title, title, sizeof(aggregated[match].title));
      aggregated[match].cachePath[sizeof(aggregated[match].cachePath) - 1] = '\0';
      aggregated[match].title[sizeof(aggregated[match].title) - 1] = '\0';
      aggregateCount++;
    }
    aggregated[match].readingSeconds += delta;
    aggregated[match].screenPages = addSaturated(aggregated[match].screenPages, pages);
  }
  file.close();
  return true;
}
}  // namespace

bool ReadingLedger::summarizeDay(const uint32_t dayIndex, const uint32_t journalSeconds,
                                 ReadingLedgerDaySummary& out) {
  out = ReadingLedgerDaySummary{};
  out.dayIndex = dayIndex;

  std::array<AggregatedBook, READING_LEDGER_DAY_BOOK_LIMIT> aggregated{};
  uint8_t aggregateCount = 0;
  int64_t otherSeconds = 0;

  if (!scanLedgerFileForDay(PATH, dayIndex, aggregated, aggregateCount, otherSeconds, out)) {
    return false;
  }

  // Peer ledgers received via Nearby Stats Sync: merged journal day totals
  // include peer reading, so peer attribution must merge too — otherwise
  // synced time shows as "Older reading (book unknown)".
  FsFile dir = Storage.open(SYNCED_LEDGER_DIR);
  if (dir && dir.isDirectory()) {
    char name[64];
    for (FsFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      const bool isDirectory = entry.isDirectory();
      const size_t nameLen = entry.getName(name, sizeof(name));
      entry.close();
      if (isDirectory || nameLen == 0) continue;
      const std::string peerPath = std::string(SYNCED_LEDGER_DIR) + "/" + name;
      if (!scanLedgerFileForDay(peerPath.c_str(), dayIndex, aggregated, aggregateCount, otherSeconds, out)) {
        LOG_DBG(LOG_TAG, "Skipping unreadable synced ledger: %s", name);
      }
    }
  }
  if (dir) dir.close();

  std::sort(aggregated.begin(), aggregated.begin() + aggregateCount,
            [](const AggregatedBook& lhs, const AggregatedBook& rhs) {
              return lhs.readingSeconds > rhs.readingSeconds;
            });
  for (uint8_t i = 0; i < aggregateCount; ++i) {
    if (aggregated[i].readingSeconds <= 0) continue;
    ReadingLedgerDayBook& book = out.books[out.bookCount++];
    memcpy(book.cachePath, aggregated[i].cachePath, sizeof(book.cachePath));
    memcpy(book.title, aggregated[i].title, sizeof(book.title));
    book.readingSeconds = static_cast<uint32_t>(std::min<int64_t>(aggregated[i].readingSeconds, UINT32_MAX));
    book.screenPages = aggregated[i].screenPages;
    out.attributedSeconds = addSaturated(out.attributedSeconds, book.readingSeconds);
  }
  if (otherSeconds > 0) {
    out.otherBookSeconds = static_cast<uint32_t>(std::min<int64_t>(otherSeconds, UINT32_MAX));
  }
  const uint32_t knownSeconds = addSaturated(out.attributedSeconds, out.otherBookSeconds);
  if (journalSeconds > knownSeconds) out.legacyUnattributedSeconds = journalSeconds - knownSeconds;
  return true;
}

namespace {
void scanLedgerFileRecords(const char* path, ReadingLedger::RecordVisitor visitor, void* ctx) {
  if (!Storage.exists(path)) return;
  FsFile file;
  if (!Storage.openFileForRead(LOG_TAG, path, file) || !readHeader(file)) {
    if (file) file.close();
    return;
  }
  const size_t fileSize = file.fileSize();
  size_t cursor = HEADER_SIZE;
  uint32_t scannedRecords = 0;
  uint8_t record[RECORD_SIZE] = {};
  while (cursor + 4 <= fileSize) {
    uint8_t magicBytes[4] = {};
    if (!file.seek(cursor) || file.read(magicBytes, sizeof(magicBytes)) != static_cast<int>(sizeof(magicBytes))) break;
    if (readLe32(magicBytes, 0) != RECORD_MAGIC) {
      cursor++;
      continue;
    }
    if (cursor + RECORD_SIZE > fileSize || !file.seek(cursor) ||
        file.read(record, sizeof(record)) != static_cast<int>(sizeof(record))) {
      break;
    }
    if (readLe32(record, RECORD_CRC_OFFSET) != crc32(record, RECORD_CRC_OFFSET)) {
      cursor++;
      continue;
    }
    cursor += RECORD_SIZE;
    scannedRecords++;
    yieldDuringScan(scannedRecords);
    const uint32_t dayIndex = readLe32(record, 4);
    const int32_t delta = static_cast<int32_t>(readLe32(record, 8));
    const char* cachePath = reinterpret_cast<const char*>(record + 16);
    const char* title = reinterpret_cast<const char*>(record + 64);
    visitor(dayIndex, delta, cachePath, title, ctx);
  }
  file.close();
}
}  // namespace

void ReadingLedger::forEachRecord(const RecordVisitor visitor, void* ctx) {
  if (visitor == nullptr) return;
  scanLedgerFileRecords(PATH, visitor, ctx);
  FsFile dir = Storage.open(SYNCED_LEDGER_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }
  char name[64];
  for (FsFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    const bool isDirectory = entry.isDirectory();
    const size_t nameLen = entry.getName(name, sizeof(name));
    entry.close();
    if (isDirectory || nameLen == 0) continue;
    const std::string peerPath = std::string(SYNCED_LEDGER_DIR) + "/" + name;
    scanLedgerFileRecords(peerPath.c_str(), visitor, ctx);
  }
  dir.close();
}

bool ReadingLedger::resetLocal() {
  if (!Storage.exists(PATH)) return true;
  const std::string backupPath = std::string(PATH) + ".reset-backup";
  if (Storage.exists(backupPath.c_str())) {
    LOG_ERR(LOG_TAG, "Refusing reset because preserved backup already exists: %s", backupPath.c_str());
    return false;
  }
  return Storage.rename(PATH, backupPath.c_str()) && createLedgerFile();
}
