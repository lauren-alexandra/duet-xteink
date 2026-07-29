#include "SessionLog.h"

#include <HalStorage.h>
#include <Logging.h>
#include <uzlib.h>

#include <algorithm>
#include <cstring>

namespace {
constexpr char LOG_TAG[] = "SLOG";
constexpr uint32_t HEADER_MAGIC = 0x31474C53u;  // SLG1
constexpr uint32_t RECORD_MAGIC = 0x44524C53u;  // SLRD
constexpr uint8_t FILE_VERSION = 1;
constexpr size_t HEADER_SIZE = 12;
constexpr size_t RECORD_SIZE = 128;
constexpr size_t RECORD_CRC_OFFSET = 124;
constexpr size_t CACHE_PATH_BYTES = 44;
constexpr size_t TITLE_BYTES = 56;

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

uint32_t crc32(const uint8_t* data, const size_t size) {
  return uzlib_crc32(data, static_cast<unsigned int>(size), 0);
}

void copyFixed(const std::string& source, uint8_t* target, const size_t size) {
  memset(target, 0, size);
  if (size == 0) return;
  const size_t copyLength = std::min(source.size(), size - 1);
  if (copyLength > 0) memcpy(target, source.data(), copyLength);
}

bool ensureLogFile() {
  if (Storage.existsForRead(SessionLog::PATH)) return true;
  Storage.mkdir(DUET_STATE_ROOT_PATH "");
  uint8_t header[HEADER_SIZE] = {};
  writeLe32(header, 0, HEADER_MAGIC);
  header[4] = FILE_VERSION;
  writeLe16(header, 6, RECORD_SIZE);
  writeLe32(header, 8, crc32(header, 8));
  FsFile file;
  if (!Storage.openFileForWrite(LOG_TAG, SessionLog::PATH, file)) return false;
  const bool written = file.write(header, sizeof(header)) == sizeof(header);
  file.flush();
  const bool synced = written && file.sync();
  const bool closed = file.close();
  if (!synced || !closed) LOG_ERR(LOG_TAG, "Could not initialize session log");
  return synced && closed;
}
}  // namespace

bool SessionLog::append(const ReadingStatsDateTime& localStart, const uint32_t readingSeconds,
                        const uint16_t screenPages, const std::string& cachePath, const std::string& title) {
  if (!localStart.isValid() || readingSeconds == 0 || !ensureLogFile()) return false;

  uint8_t record[RECORD_SIZE] = {};
  writeLe32(record, 0, RECORD_MAGIC);
  writeLe32(record, 4, readingStatsDayIndex(localStart.date));
  writeLe16(record, 8,
            static_cast<uint16_t>(static_cast<uint16_t>(localStart.hour) * 60u + localStart.minute));
  record[10] = localStart.second;
  writeLe32(record, 12, readingSeconds);
  writeLe16(record, 16, screenPages);
  copyFixed(cachePath, record + 20, CACHE_PATH_BYTES);
  copyFixed(title, record + 20 + CACHE_PATH_BYTES, TITLE_BYTES);
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
  if (!synced || !closed) LOG_ERR(LOG_TAG, "Session log append incomplete");
  return synced && closed;
}
