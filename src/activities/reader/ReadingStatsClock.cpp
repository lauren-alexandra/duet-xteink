#include "ReadingStatsClock.h"

#include <HalStorage.h>
#include <HalClock.h>
#include <Logging.h>
#include <uzlib.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <ctime>

#ifndef SIMULATOR
#include <sys/time.h>
#endif

#include "CrossPointSettings.h"

namespace {
constexpr char LOG_TAG[] = "RSTCLK";
constexpr char CLOCK_PATH[] = DUET_STATE_ROOT_PATH "/reading_stats_clock_v1.bin";
constexpr char CLOCK_BACKUP_PATH[] = DUET_STATE_ROOT_PATH "/reading_stats_clock_v1.bin.bak";
constexpr char CLOCK_TEMP_PATH[] = DUET_STATE_ROOT_PATH "/reading_stats_clock_v1.bin.tmp";
constexpr uint32_t CLOCK_MAGIC = 0x4b4c4352u;  // RCLK
constexpr uint8_t CLOCK_VERSION = 1;
constexpr size_t CLOCK_FILE_SIZE = 14;
constexpr time_t MIN_VALID_SYSTEM_TIME = 1704067200;  // 2024-01-01 UTC
constexpr int64_t UNIX_SECONDS_AT_2000 = 946684800LL;

bool cacheLoaded = false;
ReadingStatsDate cachedDate;

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

bool readDateFromPath(const char* path, ReadingStatsDate& date) {
  HalFile file;
  if (!Storage.openFileForRead(LOG_TAG, path, file) || file.fileSize() != CLOCK_FILE_SIZE) {
    if (file) file.close();
    return false;
  }

  std::array<uint8_t, CLOCK_FILE_SIZE> data{};
  const bool read = file.read(data.data(), data.size()) == static_cast<int>(data.size());
  file.close();
  if (!read || readLe32(data.data(), 0) != CLOCK_MAGIC || data[4] != CLOCK_VERSION ||
      readLe32(data.data(), 10) != uzlib_crc32(data.data(), 10, 0)) {
    return false;
  }

  date = {readLe16(data.data(), 6), data[8], data[9]};
  return date.isValid();
}

bool loadSavedDate(ReadingStatsDate& date) {
  if (cacheLoaded) {
    date = cachedDate;
    return date.isValid();
  }
  cacheLoaded = true;
  if (!readDateFromPath(CLOCK_PATH, cachedDate) && readDateFromPath(CLOCK_BACKUP_PATH, cachedDate)) {
    LOG_INF(LOG_TAG, "Recovered clockless stats date from backup");
  }
  date = cachedDate;
  return date.isValid();
}

bool saveDate(const ReadingStatsDate& date) {
  if (!date.isValid() || !Storage.ensureDirectoryExists(DUET_STATE_ROOT_PATH "")) return false;
  if (cacheLoaded && compareReadingStatsDate(cachedDate, date) == 0 && Storage.exists(CLOCK_PATH)) return true;

  std::array<uint8_t, CLOCK_FILE_SIZE> data{};
  writeLe32(data.data(), 0, CLOCK_MAGIC);
  data[4] = CLOCK_VERSION;
  writeLe16(data.data(), 6, date.year);
  data[8] = date.month;
  data[9] = date.day;
  writeLe32(data.data(), 10, uzlib_crc32(data.data(), 10, 0));

  if (Storage.exists(CLOCK_TEMP_PATH)) Storage.remove(CLOCK_TEMP_PATH);
  HalFile file;
  if (!Storage.openFileForWrite(LOG_TAG, CLOCK_TEMP_PATH, file)) return false;
  bool ok = file.write(data.data(), data.size()) == data.size();
  file.flush();
  const bool synced = file.sync();
  const bool closed = file.close();
  ok = ok && synced && closed;
  if (!ok) {
    Storage.remove(CLOCK_TEMP_PATH);
    return false;
  }

  if (Storage.exists(CLOCK_BACKUP_PATH)) Storage.remove(CLOCK_BACKUP_PATH);
  if (Storage.exists(CLOCK_PATH) && !Storage.rename(CLOCK_PATH, CLOCK_BACKUP_PATH)) {
    Storage.remove(CLOCK_TEMP_PATH);
    return false;
  }
  if (!Storage.rename(CLOCK_TEMP_PATH, CLOCK_PATH)) {
    if (Storage.exists(CLOCK_BACKUP_PATH)) Storage.rename(CLOCK_BACKUP_PATH, CLOCK_PATH);
    return false;
  }

  cachedDate = date;
  cacheLoaded = true;
  return true;
}

int localOffsetMinutes() {
  const uint8_t offsetQ = SETTINGS.clockUtcOffsetQ > 104 ? 48 : SETTINGS.clockUtcOffsetQ;
  return (static_cast<int>(offsetQ) - 48) * 15;
}

bool hardwareDateTime(ReadingStatsDateTime& outDateTime) {
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;
  if (!halClock.getDateTime(year, month, day, hour, minute)) return false;

  outDateTime = {{year, month, day}, hour, minute, 0};
  if (!outDateTime.isValid()) {
    outDateTime = {};
    return false;
  }

  int localMinutes = static_cast<int>(hour) * 60 + static_cast<int>(minute) + localOffsetMinutes();
  while (localMinutes < 0) {
    addDaysToReadingStatsDate(outDateTime.date, -1);
    localMinutes += 24 * 60;
  }
  while (localMinutes >= 24 * 60) {
    addDaysToReadingStatsDate(outDateTime.date, 1);
    localMinutes -= 24 * 60;
  }
  outDateTime.hour = static_cast<uint8_t>(localMinutes / 60);
  outDateTime.minute = static_cast<uint8_t>(localMinutes % 60);
  return outDateTime.isValid();
}

bool systemDateTime(ReadingStatsDateTime& outDateTime) {
#ifdef SIMULATOR
  (void)outDateTime;
  return false;
#else
  const time_t now = time(nullptr);
  if (now < MIN_VALID_SYSTEM_TIME) return false;
  const time_t localEpoch = now + static_cast<time_t>(localOffsetMinutes()) * 60;
  tm localTime{};
  if (gmtime_r(&localEpoch, &localTime) == nullptr) return false;
  outDateTime.date = {static_cast<uint16_t>(localTime.tm_year + 1900),
                      static_cast<uint8_t>(localTime.tm_mon + 1), static_cast<uint8_t>(localTime.tm_mday)};
  outDateTime.hour = static_cast<uint8_t>(localTime.tm_hour);
  outDateTime.minute = static_cast<uint8_t>(localTime.tm_min);
  outDateTime.second = static_cast<uint8_t>(localTime.tm_sec);
  return outDateTime.isValid();
#endif
}

bool parseBuildDate(ReadingStatsDate& date) {
  char monthToken[4] = {};
  unsigned day = 0;
  unsigned year = 0;
  if (sscanf(__DATE__, "%3s %u %u", monthToken, &day, &year) != 3) return false;
  static constexpr const char* MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  uint8_t month = 0;
  for (uint8_t index = 0; index < 12; ++index) {
    if (strcmp(monthToken, MONTHS[index]) == 0) {
      month = static_cast<uint8_t>(index + 1);
      break;
    }
  }
  date = {static_cast<uint16_t>(year), month, static_cast<uint8_t>(day)};
  return date.isValid();
}

void setSystemDate(const ReadingStatsDate& date) {
#ifndef SIMULATOR
  const int64_t localNoon = UNIX_SECONDS_AT_2000 + static_cast<int64_t>(readingStatsDayIndex(date)) * 86400LL + 43200LL;
  const int64_t utcEpoch = localNoon - static_cast<int64_t>(localOffsetMinutes()) * 60LL;
  timeval value{};
  value.tv_sec = static_cast<time_t>(utcEpoch);
  if (settimeofday(&value, nullptr) != 0) LOG_ERR(LOG_TAG, "Could not seed internal clock");
#else
  (void)date;
#endif
}
}  // namespace

bool getClocklessReadingStatsDateTime(ReadingStatsDateTime& outDateTime) {
  // The X3 has a real RTC. Use its local date when publishing the shared
  // clockless snapshot so an X4 cannot inherit a stale day anchor after sync.
  if (hardwareDateTime(outDateTime)) {
    saveDate(outDateTime.date);
    return true;
  }
  if (systemDateTime(outDateTime)) {
    saveDate(outDateTime.date);
    return true;
  }

  ReadingStatsDate date;
  if (!loadSavedDate(date)) {
    if (!parseBuildDate(date)) date = {2024, 1, 1};
    saveDate(date);
    LOG_INF(LOG_TAG, "Initialized clockless stats date to firmware build date");
  }
  if (!date.isValid()) {
    outDateTime = {};
    return false;
  }

  setSystemDate(date);
  outDateTime = {date, 12, 0, 0};
  return true;
}

bool setClocklessReadingStatsDate(const ReadingStatsDate& date) {
  if (!saveDate(date)) return false;
  setSystemDate(date);
  return true;
}

bool mergeClocklessReadingStatsDateFromFile(const char* path) {
  ReadingStatsDate peerDate;
  if (!path || !readDateFromPath(path, peerDate)) return false;

  ReadingStatsDateTime localDateTime;
  if (!getClocklessReadingStatsDateTime(localDateTime)) return false;
  if (compareReadingStatsDate(peerDate, localDateTime.date) <= 0) return true;
  return setClocklessReadingStatsDate(peerDate);
}

void resetClocklessReadingStatsDateCache() {
  cacheLoaded = false;
  cachedDate = {};
}
