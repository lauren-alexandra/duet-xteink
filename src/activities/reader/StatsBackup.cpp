#include "StatsBackup.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <freertos/task.h>
#include <uzlib.h>

#ifndef SIMULATOR
#include <esp_task_wdt.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "AchievementStore.h"
#include "GlobalReadingStats.h"
#include "LibraryInsights.h"
#include "ReadingStatsClock.h"
#include "ReadingStatsUtils.h"

namespace {
constexpr char LOG_TAG[] = "SBACK";
constexpr char GLOBAL_STATS_PATH[] = DUET_STATE_ROOT_PATH "/global_stats.bin";
constexpr char BACKUP_DIR[] = DUET_STATS_BACKUP_ROOT_PATH;
constexpr char EXPORT_DIR[] = "/exports";
constexpr int DEFAULT_BACKUP_KEEP_COUNT = 7;
constexpr uint32_t ARCHIVE_MAGIC = 0x41545343u;  // CSTA
constexpr uint16_t ARCHIVE_VERSION = 1;
constexpr size_t ARCHIVE_HEADER_SIZE = 20;
constexpr size_t ARCHIVE_ENTRY_HEADER_SIZE = 12;
constexpr size_t ARCHIVE_IO_BUFFER_SIZE = 768;
constexpr uint16_t ARCHIVE_MAX_ENTRIES = 1024;
constexpr uint32_t ARCHIVE_MAX_ENTRY_BYTES = 64u * 1024u * 1024u;

struct ArchiveEntry {
  std::string path;
  uint32_t dataOffset = 0;
  uint32_t dataLength = 0;
  uint32_t dataCrc = 0;
};

struct BackupName {
  char value[64] = {};
};

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

uint32_t crc32(const uint8_t* data, const size_t size, const uint32_t seed = 0) {
  return uzlib_crc32(data, static_cast<unsigned int>(size), seed);
}

void maintenanceYield() {
  vTaskDelay(1);
#ifndef SIMULATOR
  esp_task_wdt_reset();
#endif
}

bool isBookStatsFileName(const char* name) {
  if (!name) return false;
  if (strcmp(name, "stats.bin") == 0) return true;
  constexpr char prefix[] = "stats_v";
  constexpr size_t prefixLength = sizeof(prefix) - 1;
  if (strncmp(name, prefix, prefixLength) != 0) return false;
  const char* digits = name + prefixLength;
  const char* suffix = strstr(digits, ".bin");
  if (!suffix || suffix == digits || suffix[4] != '\0') return false;
  for (const char* cursor = digits; cursor < suffix; ++cursor) {
    if (!std::isdigit(static_cast<unsigned char>(*cursor))) return false;
  }
  return true;
}

bool isRootStatsFileName(const char* name) {
  return name != nullptr &&
         (strcmp(name, "global_stats.bin") == 0 || strcmp(name, "reading_journal.bin") == 0 ||
          strcmp(name, "reading_ledger_v1.bin") == 0 || strcmp(name, "library_book_stats_v1.bin") == 0 ||
          strcmp(name, "library_book_aliases_v1.bin") == 0 || strcmp(name, "library_book_details_v1.bin") == 0 ||
          strcmp(name, "reading_stats_clock_v1.bin") == 0 || strcmp(name, "achievements.bin") == 0);
}

bool isSyncedStatsDirectory(const char* name) {
  return name != nullptr && (strcmp(name, "synced_stats") == 0 || strcmp(name, "synced_book_stats") == 0 ||
                             strcmp(name, "synced_book_details") == 0 || strcmp(name, "synced_journals") == 0 ||
                             strcmp(name, "synced_ledgers") == 0 || strcmp(name, "synced_stats_dates") == 0 ||
                             strcmp(name, "synced_achievements") == 0 || strcmp(name, "synced_names") == 0);
}

bool isArchiveFileName(const char* name) {
  if (!name || strncmp(name, "reading_stats_", 14) != 0) return false;
  const size_t length = strlen(name);
  return length > 21 && strcmp(name + length - 7, ".cstats") == 0;
}

bool isBookCacheDirectoryName(const char* name) {
  return name != nullptr &&
         (strncmp(name, "epub_", 5) == 0 || strncmp(name, "xtc_", 4) == 0 || strncmp(name, "txt_", 4) == 0);
}

void collectChildFiles(const std::string& scanDirectoryPath, const std::string& logicalDirectoryPath,
                       const bool syncedStats, std::vector<std::string>& paths) {
  FsFile directory = Storage.open(scanDirectoryPath.c_str());
  if (!directory || !directory.isDirectory()) {
    if (directory) directory.close();
    return;
  }

  char name[128];
  for (FsFile file = directory.openNextFile(); file; file = directory.openNextFile()) {
    const bool isDirectory = file.isDirectory();
    const size_t nameLength = file.getName(name, sizeof(name));
    file.close();
    if (isDirectory || nameLength == 0) continue;
    if (!syncedStats && !isBookStatsFileName(name)) continue;
    if (syncedStats && (name[0] == '.' || strstr(name, ".tmp") != nullptr || strstr(name, ".part") != nullptr)) {
      continue;
    }
    paths.push_back(logicalDirectoryPath + "/" + name);
  }
  directory.close();
}

void collectStatsRoot(const char* scanRootPath, const char* logicalRootPath, const bool includeBookCaches,
                      const bool includeRootStats, std::vector<std::string>& paths) {
  FsFile root = Storage.open(scanRootPath);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }

  char name[128];
  for (FsFile entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    const bool isDirectory = entry.isDirectory();
    const size_t nameLength = entry.getName(name, sizeof(name));
    entry.close();
    if (nameLength == 0) continue;
    if (isDirectory) {
      const bool syncedStats = isSyncedStatsDirectory(name);
      if ((includeBookCaches && isBookCacheDirectoryName(name)) || syncedStats) {
        const std::string scanChildPath = std::string(scanRootPath) + "/" + name;
        const std::string logicalChildPath = std::string(logicalRootPath) + "/" + name;
        collectChildFiles(scanChildPath, logicalChildPath, syncedStats, paths);
      }
    } else if (includeRootStats && isRootStatsFileName(name)) {
      paths.push_back(std::string(logicalRootPath) + "/" + name);
    }
    maintenanceYield();
  }
  root.close();
}

std::vector<std::string> collectCurrentStatsPaths() {
  std::vector<std::string> paths;
  // Store canonical paths in the archive while scanning every compatible
  // source. openFileForRead() resolves a canonical path back to the legacy
  // source when a lazy per-book file has not been copied yet.
  collectStatsRoot(DUET_STATE_ROOT_PATH, DUET_STATE_ROOT_PATH, false, true, paths);
  collectStatsRoot(DUET_LEGACY_STATE_ROOT_PATH, DUET_STATE_ROOT_PATH, false, true, paths);
  collectStatsRoot(DUET_LEGACY_BOOKS_ROOT_PATH, DUET_STATE_ROOT_PATH, false, true, paths);
  collectStatsRoot(DUET_BOOKS_ROOT_PATH, DUET_BOOKS_ROOT_PATH, true, false, paths);
  collectStatsRoot(DUET_LEGACY_BOOKS_ROOT_PATH, DUET_BOOKS_ROOT_PATH, true, false, paths);
  std::sort(paths.begin(), paths.end());
  paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
  return paths;
}

bool isSafeCanonicalArchivePath(const std::string& path) {
  constexpr char hotRootPrefix[] = DUET_STATE_ROOT_PATH "/";
  constexpr char cacheRootPrefix[] = DUET_BOOKS_ROOT_PATH "/";
  const bool hotRoot = path.rfind(hotRootPrefix, 0) == 0;
  const bool cacheRoot = path.rfind(cacheRootPrefix, 0) == 0;
  const size_t rootPrefixLength = hotRoot ? sizeof(hotRootPrefix) - 1 : (cacheRoot ? sizeof(cacheRootPrefix) - 1 : 0);
  if (rootPrefixLength == 0 || path.size() <= rootPrefixLength || path.find("..") != std::string::npos ||
      path.find("//") != std::string::npos || path.back() == '/') {
    return false;
  }

  const std::string relative = path.substr(rootPrefixLength);
  const size_t separator = relative.find('/');
  if (separator == std::string::npos) return isRootStatsFileName(relative.c_str());
  if (relative.find('/', separator + 1) != std::string::npos) return false;

  const std::string directory = relative.substr(0, separator);
  const std::string fileName = relative.substr(separator + 1);
  if (directory.empty() || fileName.empty()) return false;
  if (isSyncedStatsDirectory(directory.c_str())) {
    return fileName[0] != '.' && fileName.find(".tmp") == std::string::npos &&
           fileName.find(".part") == std::string::npos;
  }
  return cacheRoot && isBookStatsFileName(fileName.c_str());
}

std::string canonicalArchivePath(const std::string& storedPath) {
  if (storedPath.rfind(DUET_STORAGE_ROOT_PATH "/", 0) == 0) return storedPath;

  constexpr char legacyStatePrefix[] = DUET_LEGACY_STATE_ROOT_PATH "/";
  if (storedPath.rfind(legacyStatePrefix, 0) == 0) {
    return std::string(DUET_STATE_ROOT_PATH "/") + storedPath.substr(sizeof(legacyStatePrefix) - 1);
  }

  constexpr char legacyBooksPrefix[] = DUET_LEGACY_BOOKS_ROOT_PATH "/";
  if (storedPath.rfind(legacyBooksPrefix, 0) != 0) return {};
  const std::string relative = storedPath.substr(sizeof(legacyBooksPrefix) - 1);
  const size_t separator = relative.find('/');
  const std::string first = relative.substr(0, separator);
  if (isBookCacheDirectoryName(first.c_str())) {
    return std::string(DUET_BOOKS_ROOT_PATH "/") + relative;
  }
  return std::string(DUET_STATE_ROOT_PATH "/") + relative;
}

std::string parentPath(const std::string& path) {
  const size_t separator = path.rfind('/');
  return separator == std::string::npos || separator == 0 ? std::string("/") : path.substr(0, separator);
}

bool writeArchiveHeader(FsFile& file, const uint16_t entryCount, const uint32_t dataBytes) {
  uint8_t header[ARCHIVE_HEADER_SIZE] = {};
  writeLe32(header, 0, ARCHIVE_MAGIC);
  writeLe16(header, 4, ARCHIVE_VERSION);
  writeLe16(header, 6, static_cast<uint16_t>(ARCHIVE_HEADER_SIZE));
  writeLe32(header, 8, entryCount);
  writeLe32(header, 12, dataBytes);
  writeLe32(header, 16, crc32(header, 16));
  return file.seek(0) && file.write(header, sizeof(header)) == sizeof(header);
}

bool appendArchiveEntry(FsFile& archive, const std::string& path, uint32_t& dataBytes) {
  FsFile source;
  if (!Storage.openFileForRead(LOG_TAG, path, source)) return false;
  const size_t sourceSize = source.fileSize();
  if (sourceSize > ARCHIVE_MAX_ENTRY_BYTES || sourceSize > UINT32_MAX || path.size() > UINT16_MAX) {
    source.close();
    return false;
  }

  std::array<uint8_t, ARCHIVE_IO_BUFFER_SIZE> buffer{};
  uint32_t checksum = 0;
  size_t remaining = sourceSize;
  while (remaining > 0) {
    const size_t amount = std::min(remaining, buffer.size());
    const int count = source.read(buffer.data(), amount);
    if (count != static_cast<int>(amount)) {
      source.close();
      return false;
    }
    checksum = crc32(buffer.data(), amount, checksum);
    remaining -= amount;
    maintenanceYield();
  }
  if (!source.seek(0)) {
    source.close();
    return false;
  }

  uint8_t entryHeader[ARCHIVE_ENTRY_HEADER_SIZE] = {};
  writeLe16(entryHeader, 0, static_cast<uint16_t>(path.size()));
  writeLe32(entryHeader, 4, static_cast<uint32_t>(sourceSize));
  writeLe32(entryHeader, 8, checksum);
  bool ok = archive.write(entryHeader, sizeof(entryHeader)) == sizeof(entryHeader) &&
            archive.write(path.data(), path.size()) == path.size();
  remaining = sourceSize;
  while (ok && remaining > 0) {
    const size_t amount = std::min(remaining, buffer.size());
    ok = source.read(buffer.data(), amount) == static_cast<int>(amount) &&
         archive.write(buffer.data(), amount) == amount;
    remaining -= amount;
    maintenanceYield();
  }
  source.close();
  if (ok) dataBytes = dataBytes > UINT32_MAX - sourceSize ? UINT32_MAX : dataBytes + sourceSize;
  return ok;
}

bool buildArchiveFileName(char* out, const size_t outLength) {
  if (!out || outLength == 0) return false;
  char base[64] = {};
  ReadingStatsDateTime now;
  if (getCurrentLocalReadingStatsDateTime(now)) {
    snprintf(base, sizeof(base), "reading_stats_%04u-%02u-%02u_%02u%02u", static_cast<unsigned>(now.date.year),
             static_cast<unsigned>(now.date.month), static_cast<unsigned>(now.date.day),
             static_cast<unsigned>(now.hour), static_cast<unsigned>(now.minute));
  } else {
    snprintf(base, sizeof(base), "reading_stats_backup");
  }

  for (unsigned suffix = 0; suffix < 1000; ++suffix) {
    const int written = suffix == 0 ? snprintf(out, outLength, "%s.cstats", base)
                                    : snprintf(out, outLength, "%s_%u.cstats", base, suffix + 1);
    if (written <= 0 || static_cast<size_t>(written) >= outLength) return false;
    const std::string fullPath = std::string(EXPORT_DIR) + "/" + out;
    if (!Storage.exists(fullPath.c_str())) return true;
  }
  return false;
}

bool parseArchive(const std::string& archivePath, std::vector<ArchiveEntry>& entries, uint32_t& dataBytes) {
  entries.clear();
  dataBytes = 0;
  FsFile archive;
  if (!Storage.openFileForRead(LOG_TAG, archivePath, archive)) return false;
  const size_t archiveSize = archive.fileSize();
  uint8_t header[ARCHIVE_HEADER_SIZE] = {};
  if (archiveSize < sizeof(header) || archive.read(header, sizeof(header)) != static_cast<int>(sizeof(header)) ||
      readLe32(header, 0) != ARCHIVE_MAGIC || readLe16(header, 4) != ARCHIVE_VERSION ||
      readLe16(header, 6) != ARCHIVE_HEADER_SIZE || readLe32(header, 16) != crc32(header, 16)) {
    archive.close();
    return false;
  }

  const uint32_t entryCount = readLe32(header, 8);
  const uint32_t expectedDataBytes = readLe32(header, 12);
  if (entryCount == 0 || entryCount > ARCHIVE_MAX_ENTRIES) {
    archive.close();
    return false;
  }
  entries.reserve(entryCount);
  size_t cursor = ARCHIVE_HEADER_SIZE;
  std::array<uint8_t, ARCHIVE_IO_BUFFER_SIZE> buffer{};
  for (uint32_t index = 0; index < entryCount; ++index) {
    uint8_t entryHeader[ARCHIVE_ENTRY_HEADER_SIZE] = {};
    if (cursor + sizeof(entryHeader) > archiveSize || !archive.seek(cursor) ||
        archive.read(entryHeader, sizeof(entryHeader)) != static_cast<int>(sizeof(entryHeader))) {
      archive.close();
      return false;
    }
    cursor += sizeof(entryHeader);
    const uint16_t pathLength = readLe16(entryHeader, 0);
    const uint32_t dataLength = readLe32(entryHeader, 4);
    const uint32_t expectedCrc = readLe32(entryHeader, 8);
    if (pathLength == 0 || pathLength >= 256 || dataLength > ARCHIVE_MAX_ENTRY_BYTES ||
        cursor + pathLength + dataLength > archiveSize) {
      archive.close();
      return false;
    }
    char pathBuffer[256] = {};
    if (!archive.seek(cursor) || archive.read(pathBuffer, pathLength) != static_cast<int>(pathLength)) {
      archive.close();
      return false;
    }
    pathBuffer[pathLength] = '\0';
    const std::string path = canonicalArchivePath(std::string(pathBuffer, pathLength));
    if (!isSafeCanonicalArchivePath(path) ||
        std::any_of(entries.begin(), entries.end(),
                    [&path](const ArchiveEntry& entry) { return entry.path == path; })) {
      archive.close();
      return false;
    }
    cursor += pathLength;
    const uint32_t dataOffset = static_cast<uint32_t>(cursor);
    uint32_t checksum = 0;
    uint32_t remaining = dataLength;
    while (remaining > 0) {
      const size_t amount = std::min<size_t>(remaining, buffer.size());
      if (!archive.seek(cursor) || archive.read(buffer.data(), amount) != static_cast<int>(amount)) {
        archive.close();
        return false;
      }
      checksum = crc32(buffer.data(), amount, checksum);
      cursor += amount;
      remaining -= static_cast<uint32_t>(amount);
      maintenanceYield();
    }
    if (checksum != expectedCrc) {
      archive.close();
      return false;
    }
    entries.push_back({path, dataOffset, dataLength, expectedCrc});
    dataBytes = dataBytes > UINT32_MAX - dataLength ? UINT32_MAX : dataBytes + dataLength;
  }
  archive.close();
  return cursor == archiveSize && dataBytes == expectedDataBytes;
}

bool stageArchiveEntry(FsFile& archive, const ArchiveEntry& entry) {
  const std::string directory = parentPath(entry.path);
  if (!Storage.ensureDirectoryExists(directory.c_str())) return false;
  const std::string temporaryPath = entry.path + ".import.tmp";
  if (Storage.exists(temporaryPath.c_str()) && !Storage.remove(temporaryPath.c_str())) return false;

  FsFile output;
  if (!Storage.openFileForWrite(LOG_TAG, temporaryPath, output) || !archive.seek(entry.dataOffset)) {
    if (output) output.close();
    Storage.remove(temporaryPath.c_str());
    return false;
  }
  std::array<uint8_t, ARCHIVE_IO_BUFFER_SIZE> buffer{};
  uint32_t remaining = entry.dataLength;
  uint32_t checksum = 0;
  bool ok = true;
  while (ok && remaining > 0) {
    const size_t amount = std::min<size_t>(remaining, buffer.size());
    ok = archive.read(buffer.data(), amount) == static_cast<int>(amount) &&
         output.write(buffer.data(), amount) == amount;
    if (ok) checksum = crc32(buffer.data(), amount, checksum);
    remaining -= static_cast<uint32_t>(amount);
    maintenanceYield();
  }
  output.flush();
  ok = ok && checksum == entry.dataCrc && output.sync() && output.close();
  if (!ok) Storage.remove(temporaryPath.c_str());
  return ok;
}

bool containsEntryPath(const std::vector<ArchiveEntry>& entries, const std::string& path) {
  return std::any_of(entries.begin(), entries.end(), [&path](const ArchiveEntry& entry) { return entry.path == path; });
}

bool preserveWhenArchiveOmits(const std::string& path) {
  constexpr char syncedAchievementsPrefix[] = DUET_STATE_ROOT_PATH "/synced_achievements/";
  return path == DUET_STATE_ROOT_PATH "/achievements.bin" || path.rfind(syncedAchievementsPrefix, 0) == 0;
}

void cleanStagedEntries(const std::vector<ArchiveEntry>& entries) {
  for (const ArchiveEntry& entry : entries) {
    const std::string temporaryPath = entry.path + ".import.tmp";
    if (Storage.exists(temporaryPath.c_str())) Storage.remove(temporaryPath.c_str());
  }
}

bool isStatsBackupFileName(const char* name) {
  if (!name || strncmp(name, "stats_", 6) != 0) return false;
  const size_t len = strlen(name);
  return len > 10 && strcmp(name + len - 4, ".bin") == 0;
}

bool copyString(const char* src, char* dst, const size_t dstLen) {
  if (!dst || dstLen == 0) return false;
  const int written = snprintf(dst, dstLen, "%s", src ? src : "");
  return written > 0 && static_cast<size_t>(written) < dstLen;
}

bool buildDatedBackupName(const ReadingStatsDateTime& dt, const bool manual, char* out, const size_t outLen) {
  int written = 0;
  if (manual) {
    written = snprintf(out, outLen, "stats_%04u-%02u-%02u_%02u%02u.bin", static_cast<unsigned>(dt.date.year),
                       static_cast<unsigned>(dt.date.month), static_cast<unsigned>(dt.date.day),
                       static_cast<unsigned>(dt.hour), static_cast<unsigned>(dt.minute));
  } else {
    written = snprintf(out, outLen, "stats_%04u-%02u-%02u.bin", static_cast<unsigned>(dt.date.year),
                       static_cast<unsigned>(dt.date.month), static_cast<unsigned>(dt.date.day));
  }
  return written > 0 && static_cast<size_t>(written) < outLen;
}

bool parseIncrementingIndex(const char* name, uint32_t& outIndex) {
  constexpr char prefix[] = "stats_backup_";
  constexpr size_t prefixLen = sizeof(prefix) - 1;
  if (!name || strncmp(name, prefix, prefixLen) != 0) return false;
  const char* digits = name + prefixLen;
  const char* suffix = strstr(digits, ".bin");
  if (!suffix || suffix == digits || suffix[4] != '\0') return false;

  uint32_t value = 0;
  for (const char* p = digits; p < suffix; ++p) {
    if (!std::isdigit(static_cast<unsigned char>(*p))) return false;
    value = value * 10u + static_cast<uint32_t>(*p - '0');
  }
  if (value == 0) return false;
  outIndex = value;
  return true;
}

bool nextIncrementingBackupName(char* out, const size_t outLen) {
  FsFile dir = Storage.open(BACKUP_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    const int written = snprintf(out, outLen, "stats_backup_%03u.bin", 1u);
    return written > 0 && static_cast<size_t>(written) < outLen;
  }

  char name[128];
  uint32_t maxIndex = 0;
  for (FsFile file = dir.openNextFile(); file; file = dir.openNextFile()) {
    const bool isDirectory = file.isDirectory();
    const size_t nameLen = file.getName(name, sizeof(name));
    file.close();
    if (isDirectory || nameLen == 0) continue;

    uint32_t index = 0;
    if (parseIncrementingIndex(name, index) && index > maxIndex) {
      maxIndex = index;
    }
  }
  dir.close();

  const int written = snprintf(out, outLen, "stats_backup_%03u.bin", static_cast<unsigned>(maxIndex + 1));
  return written > 0 && static_cast<size_t>(written) < outLen;
}

bool chooseBackupName(const bool manual, char* out, const size_t outLen) {
  ReadingStatsDateTime now;
  if (getCurrentLocalReadingStatsDateTime(now)) {
    return buildDatedBackupName(now, manual, out, outLen);
  }
  return nextIncrementingBackupName(out, outLen);
}

bool readStatsFile(std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE>& buffer, size_t& outSize) {
  outSize = 0;

  FsFile file;
  if (!Storage.openFileForRead(LOG_TAG, GLOBAL_STATS_PATH, file)) {
    LOG_ERR(LOG_TAG, "Could not open stats file for backup: %s", GLOBAL_STATS_PATH);
    return false;
  }

  const size_t fileSize = file.fileSize();
  if (fileSize < GlobalReadingStats::MIN_SUPPORTED_FILE_SIZE || fileSize > buffer.size()) {
    LOG_ERR(LOG_TAG, "Stats file has unsupported size for backup: %u bytes", static_cast<unsigned>(fileSize));
    file.close();
    return false;
  }

  const int read = file.read(buffer.data(), fileSize);
  file.close();
  if (read != static_cast<int>(fileSize)) {
    LOG_ERR(LOG_TAG, "Failed to read stats file for backup: %d/%u bytes", read, static_cast<unsigned>(fileSize));
    return false;
  }

  outSize = fileSize;
  return true;
}

bool writeBackupFile(const char* path, const uint8_t* data, const size_t size) {
  const std::string tmpPath = std::string(path) + ".tmp";
  if (Storage.exists(tmpPath.c_str()) && !Storage.remove(tmpPath.c_str())) {
    LOG_ERR(LOG_TAG, "Could not remove stale backup temp file: %s", tmpPath.c_str());
    return false;
  }

  FsFile file;
  if (!Storage.openFileForWrite(LOG_TAG, tmpPath.c_str(), file)) {
    LOG_ERR(LOG_TAG, "Could not open backup temp file: %s", tmpPath.c_str());
    return false;
  }

  const size_t written = file.write(data, size);
  if (written != size) {
    LOG_ERR(LOG_TAG, "Short write for backup temp file %s: %u/%u bytes", tmpPath.c_str(),
            static_cast<unsigned>(written), static_cast<unsigned>(size));
    file.close();
    Storage.remove(tmpPath.c_str());
    return false;
  }

  file.flush();
  if (!file.sync()) {
    LOG_ERR(LOG_TAG, "Failed to sync backup temp file: %s", tmpPath.c_str());
    file.close();
    Storage.remove(tmpPath.c_str());
    return false;
  }

  if (!file.close()) {
    LOG_ERR(LOG_TAG, "Failed to close backup temp file: %s", tmpPath.c_str());
    Storage.remove(tmpPath.c_str());
    return false;
  }

  if (Storage.exists(path) && !Storage.remove(path)) {
    LOG_ERR(LOG_TAG, "Could not replace backup file: %s", path);
    Storage.remove(tmpPath.c_str());
    return false;
  }

  if (!Storage.rename(tmpPath.c_str(), path)) {
    LOG_ERR(LOG_TAG, "Could not publish backup file: %s", path);
    Storage.remove(tmpPath.c_str());
    return false;
  }

  return true;
}
}  // namespace

bool backupGlobalStats(const bool manual, char* outFileName, const size_t outFileNameLen) {
  if (!Storage.ensureDirectoryExists(BACKUP_DIR)) {
    LOG_ERR(LOG_TAG, "Could not create stats backup directory: %s", BACKUP_DIR);
    return false;
  }

  char fileName[64];
  if (!chooseBackupName(manual, fileName, sizeof(fileName))) {
    LOG_ERR(LOG_TAG, "Could not choose stats backup filename");
    return false;
  }

  std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE> data{};
  size_t dataSize = 0;
  if (!readStatsFile(data, dataSize)) return false;

  char backupPath[128];
  const int pathWritten = snprintf(backupPath, sizeof(backupPath), "%s/%s", BACKUP_DIR, fileName);
  if (pathWritten <= 0 || static_cast<size_t>(pathWritten) >= sizeof(backupPath)) {
    LOG_ERR(LOG_TAG, "Could not build backup path");
    return false;
  }

  if (!writeBackupFile(backupPath, data.data(), dataSize)) return false;
  pruneBackups(DEFAULT_BACKUP_KEEP_COUNT);

  if (outFileName != nullptr && outFileNameLen > 0) {
    copyString(fileName, outFileName, outFileNameLen);
  }
  LOG_DBG(LOG_TAG, "Wrote stats backup: %s", backupPath);
  return true;
}

int pruneBackups(const int keep) {
  if (keep < 0) return 0;

  FsFile dir = Storage.open(BACKUP_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return 0;
  }

  char name[128];
  std::vector<BackupName> names;
  names.reserve(16);
  for (FsFile file = dir.openNextFile(); file; file = dir.openNextFile()) {
    const bool isDirectory = file.isDirectory();
    const size_t nameLen = file.getName(name, sizeof(name));
    file.close();
    if (isDirectory || nameLen == 0 || !isStatsBackupFileName(name)) continue;

    BackupName backupName;
    if (copyString(name, backupName.value, sizeof(backupName.value))) {
      names.push_back(backupName);
    }
  }
  dir.close();

  if (static_cast<int>(names.size()) <= keep) return 0;

  std::sort(names.begin(), names.end(),
            [](const BackupName& lhs, const BackupName& rhs) { return strcmp(lhs.value, rhs.value) < 0; });

  int removed = 0;
  const int toRemove = static_cast<int>(names.size()) - keep;
  for (int i = 0; i < toRemove; ++i) {
    char path[128];
    const int pathWritten = snprintf(path, sizeof(path), "%s/%s", BACKUP_DIR, names[static_cast<size_t>(i)].value);
    if (pathWritten <= 0 || static_cast<size_t>(pathWritten) >= sizeof(path)) continue;
    if (Storage.remove(path)) {
      removed++;
    } else {
      LOG_ERR(LOG_TAG, "Failed to prune stats backup: %s", path);
    }
  }

  if (removed > 0) {
    LOG_DBG(LOG_TAG, "Pruned %d old stats backup(s)", removed);
  }
  return removed;
}

bool exportAllReadingStats(char* outFileName, const size_t outFileNameLen, uint16_t* outFileCount,
                           uint32_t* outDataBytes) {
  if (!Storage.ensureDirectoryExists(EXPORT_DIR)) {
    LOG_ERR(LOG_TAG, "Could not create reading-stats export directory: %s", EXPORT_DIR);
    return false;
  }

  const std::vector<std::string> paths = collectCurrentStatsPaths();
  if (paths.empty() || paths.size() > ARCHIVE_MAX_ENTRIES) {
    LOG_ERR(LOG_TAG, "No reading-stats files to export, or too many files: %u", static_cast<unsigned>(paths.size()));
    return false;
  }

  char fileName[80] = {};
  if (!buildArchiveFileName(fileName, sizeof(fileName))) return false;
  const std::string finalPath = std::string(EXPORT_DIR) + "/" + fileName;
  const std::string temporaryPath = std::string(EXPORT_DIR) + "/.reading_stats_export.tmp";
  if (Storage.exists(temporaryPath.c_str()) && !Storage.remove(temporaryPath.c_str())) return false;

  FsFile archive;
  if (!Storage.openFileForWrite(LOG_TAG, temporaryPath, archive)) return false;
  uint8_t emptyHeader[ARCHIVE_HEADER_SIZE] = {};
  bool ok = archive.write(emptyHeader, sizeof(emptyHeader)) == sizeof(emptyHeader);
  uint32_t dataBytes = 0;
  uint16_t writtenCount = 0;
  for (const std::string& path : paths) {
    if (!ok || !appendArchiveEntry(archive, path, dataBytes)) {
      ok = false;
      break;
    }
    writtenCount++;
  }
  ok = ok && writtenCount == paths.size() && writeArchiveHeader(archive, writtenCount, dataBytes);
  archive.flush();
  ok = ok && archive.sync() && archive.close();
  if (!ok) {
    Storage.remove(temporaryPath.c_str());
    LOG_ERR(LOG_TAG, "Could not finish full reading-stats archive");
    return false;
  }
  if (!Storage.rename(temporaryPath.c_str(), finalPath.c_str())) {
    Storage.remove(temporaryPath.c_str());
    return false;
  }

  if (outFileName && outFileNameLen > 0) copyString(fileName, outFileName, outFileNameLen);
  if (outFileCount) *outFileCount = writtenCount;
  if (outDataBytes) *outDataBytes = dataBytes;
  LOG_DBG(LOG_TAG, "Exported %u reading-stats files (%lu bytes) to %s", static_cast<unsigned>(writtenCount),
          static_cast<unsigned long>(dataBytes), finalPath.c_str());
  return true;
}

std::vector<std::string> listReadingStatsArchives() {
  std::vector<std::string> paths;
  FsFile directory = Storage.open(EXPORT_DIR);
  if (!directory || !directory.isDirectory()) {
    if (directory) directory.close();
    return paths;
  }

  char name[128];
  for (FsFile file = directory.openNextFile(); file; file = directory.openNextFile()) {
    const bool isDirectory = file.isDirectory();
    const size_t nameLength = file.getName(name, sizeof(name));
    file.close();
    if (!isDirectory && nameLength > 0 && isArchiveFileName(name)) {
      paths.push_back(std::string(EXPORT_DIR) + "/" + name);
    }
  }
  directory.close();
  std::sort(paths.begin(), paths.end(), std::greater<std::string>());
  return paths;
}

bool validateReadingStatsArchive(const std::string& archivePath, uint16_t* outFileCount, uint32_t* outDataBytes) {
  std::vector<ArchiveEntry> entries;
  uint32_t dataBytes = 0;
  const bool valid = parseArchive(archivePath, entries, dataBytes);
  if (valid) {
    if (outFileCount) *outFileCount = static_cast<uint16_t>(entries.size());
    if (outDataBytes) *outDataBytes = dataBytes;
  }
  return valid;
}

bool importAllReadingStats(const std::string& archivePath, char* outSafetyFileName, const size_t outSafetyFileNameLen,
                           uint16_t* outRestoredFileCount) {
  std::vector<ArchiveEntry> entries;
  uint32_t archiveDataBytes = 0;
  if (!parseArchive(archivePath, entries, archiveDataBytes)) {
    LOG_ERR(LOG_TAG, "Refusing invalid reading-stats archive: %s", archivePath.c_str());
    return false;
  }

  char safetyFileName[80] = {};
  if (!exportAllReadingStats(safetyFileName, sizeof(safetyFileName))) {
    LOG_ERR(LOG_TAG, "Refusing restore because the current stats safety export failed");
    return false;
  }

  FsFile archive;
  if (!Storage.openFileForRead(LOG_TAG, archivePath, archive)) return false;
  bool staged = true;
  for (const ArchiveEntry& entry : entries) {
    if (!stageArchiveEntry(archive, entry)) {
      staged = false;
      break;
    }
  }
  archive.close();
  if (!staged) {
    cleanStagedEntries(entries);
    return false;
  }

  const std::vector<std::string> currentPaths = collectCurrentStatsPaths();
  std::vector<std::string> omittedPaths;
  omittedPaths.reserve(currentPaths.size());
  bool prepared = true;
  for (const std::string& path : currentPaths) {
    if (containsEntryPath(entries, path)) continue;
    if (preserveWhenArchiveOmits(path)) continue;
    const std::string omittedPath = path + ".import.omit";
    if (Storage.exists(omittedPath.c_str())) Storage.remove(omittedPath.c_str());
    if (!Storage.rename(path.c_str(), omittedPath.c_str())) {
      prepared = false;
      break;
    }
    omittedPaths.push_back(path);
  }

  size_t committedCount = 0;
  for (size_t index = 0; prepared && index < entries.size(); ++index) {
    const ArchiveEntry& entry = entries[index];
    const std::string previousPath = entry.path + ".import.prev";
    const std::string temporaryPath = entry.path + ".import.tmp";
    if (Storage.exists(previousPath.c_str())) Storage.remove(previousPath.c_str());
    if (Storage.exists(entry.path.c_str()) && !Storage.rename(entry.path.c_str(), previousPath.c_str())) {
      prepared = false;
      break;
    }
    if (!Storage.rename(temporaryPath.c_str(), entry.path.c_str())) {
      if (Storage.exists(previousPath.c_str())) Storage.rename(previousPath.c_str(), entry.path.c_str());
      prepared = false;
      break;
    }
    committedCount++;
  }

  if (!prepared) {
    for (size_t index = 0; index < entries.size(); ++index) {
      const ArchiveEntry& entry = entries[index];
      const std::string previousPath = entry.path + ".import.prev";
      const std::string temporaryPath = entry.path + ".import.tmp";
      if (index < committedCount && Storage.exists(entry.path.c_str())) Storage.remove(entry.path.c_str());
      if (Storage.exists(previousPath.c_str())) Storage.rename(previousPath.c_str(), entry.path.c_str());
      if (Storage.exists(temporaryPath.c_str())) Storage.remove(temporaryPath.c_str());
    }
    for (const std::string& path : omittedPaths) {
      const std::string omittedPath = path + ".import.omit";
      if (Storage.exists(omittedPath.c_str())) Storage.rename(omittedPath.c_str(), path.c_str());
    }
    LOG_ERR(LOG_TAG, "Reading-stats restore failed; live files rolled back");
    return false;
  }

  for (const ArchiveEntry& entry : entries) {
    const std::string previousPath = entry.path + ".import.prev";
    if (Storage.exists(previousPath.c_str())) Storage.remove(previousPath.c_str());
  }
  for (const std::string& path : omittedPaths) {
    const std::string omittedPath = path + ".import.omit";
    if (Storage.exists(omittedPath.c_str())) Storage.remove(omittedPath.c_str());
  }
  if (outSafetyFileName && outSafetyFileNameLen > 0) {
    copyString(safetyFileName, outSafetyFileName, outSafetyFileNameLen);
  }
  if (outRestoredFileCount) *outRestoredFileCount = static_cast<uint16_t>(entries.size());
  resetClocklessReadingStatsDateCache();
  ACHIEVEMENT_STORE.reloadFromDisk();
  LibraryInsights::invalidateCache();
  // A restore replaces every book's stats wholesale; the published sync
  // snapshot is genuinely stale and must rebuild on the next sync.
  LibraryInsights::invalidateDetailedStatsSnapshot();
  LOG_DBG(LOG_TAG, "Restored %u reading-stats files from %s; safety copy %s", static_cast<unsigned>(entries.size()),
          archivePath.c_str(), safetyFileName);
  return true;
}
