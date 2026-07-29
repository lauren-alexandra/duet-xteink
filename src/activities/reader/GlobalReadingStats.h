#pragma once
#include <array>
#include <cstdint>

#include "ReadingStatsUtils.h"

// Cumulative reading statistics across all books, persisted to
// Duet's canonical global-stats file.
struct GlobalReadingStats {
  uint32_t totalSessions = 0;        // Qualifying sessions across all books
  uint32_t totalReadingSeconds = 0;  // Accumulated reading time across all books
  uint32_t countedSessionSeconds = 0;
  uint32_t totalPagesTurned = 0;     // Total forward page turns after the dwell threshold
  uint32_t completedBooks = 0;       // Books manually marked as finished
  std::array<uint32_t, READING_TIME_BUCKET_COUNT> timeOfDaySeconds{};
  std::array<uint32_t, READING_DAY_OF_WEEK_COUNT> dayOfWeekSeconds{};
  uint32_t readingHistoryAnchorDay = 0;
  std::array<uint8_t, READING_HISTORY_BYTES> readingHistoryBits{};
  uint16_t longestReadingStreak = 0;

  static constexpr uint8_t CURRENT_FILE_VERSION = 4;
  static constexpr size_t CURRENT_FILE_SIZE = 163;
  static constexpr size_t MIN_SUPPORTED_FILE_SIZE = 13;

  // Loads stats from Duet's canonical state path. Returns default-constructed
  // stats if the file is missing or the version byte does not match.
  static GlobalReadingStats load();

  // Returns true when the optional synced stats directory exists.
  static bool hasSyncedStats();

  // Loads this device's local stats plus one synced stats file per other device
  // from Duet's synced-stats directory. A stale file matching this device's MAC is
  // skipped to avoid double counting.
  static GlobalReadingStats loadAggregated();
  // Loads up to maxCount valid peer stats files received via Nearby Stats
  // Sync (excluding this device's own published copy). Returns the count.
  // names may be null; when provided, each slot receives the peer's saved
  // device name (empty when unknown).
  static uint8_t loadSyncedPeers(GlobalReadingStats* out, std::string* names, uint8_t maxCount);

  // Adds synced device stats to an already-loaded local stats snapshot. Use this
  // when the local stats may include in-memory changes that are not saved yet.
  static GlobalReadingStats loadAggregated(const GlobalReadingStats& localStats);

  // Saves stats to Duet's canonical state path.
  void save() const;

  // Replaces Duet's global-stats file with a fresh empty file without
  // rotating or deleting any backup files.
  static bool resetLocal();

  void recordReadingSpan(const ReadingStatsDateTime& localStart, uint32_t seconds);
  int32_t adjustReadingTime(const ReadingStatsDate& date, int32_t requestedDelta);
  bool hasReadingOnDay(uint32_t dayIndex) const;
  uint16_t currentReadingStreak(const ReadingStatsDate* today) const;
  uint16_t displayLongestReadingStreak() const;
};
