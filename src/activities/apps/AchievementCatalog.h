#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class AchievementMetric : uint8_t {
  BooksStarted = 0,
  BooksFinished,
  Sessions,
  TotalReadingSeconds,
  GoalDays,
  LongestGoalStreak,
  Bookmarks,
  LongestSessionSeconds,
  ReadingDays,
  LongestReadingStreak,
  ScreenPagesTurned,
  SeriesStarted,
  SeriesCompleted,
  SpiceLevelsExplored,
  MorningReadingSeconds,
  NightReadingSeconds,
  WeekendReadingSeconds,
  CrossDeviceSync,
  Count,
};

struct AchievementSnapshot {
  uint32_t booksStarted = 0;
  uint32_t booksFinished = 0;
  uint32_t sessions = 0;
  uint64_t totalReadingSeconds = 0;
  uint32_t goalDays = 0;
  uint32_t longestGoalStreak = 0;
  uint32_t bookmarks = 0;
  uint32_t longestSessionSeconds = 0;
  uint32_t readingDays = 0;
  uint32_t longestReadingStreak = 0;
  uint32_t screenPagesTurned = 0;
  uint32_t seriesStarted = 0;
  uint32_t seriesCompleted = 0;
  uint32_t spiceLevelsExplored = 0;
  uint64_t morningReadingSeconds = 0;
  uint64_t nightReadingSeconds = 0;
  uint64_t weekendReadingSeconds = 0;
  uint32_t crossDeviceSync = 0;
};

struct AchievementView {
  AchievementMetric metric = AchievementMetric::BooksStarted;
  uint64_t progress = 0;
  uint64_t target = 0;
  bool unlocked = false;
};

class AchievementCatalog {
 public:
  // Uses only compact reading counters and the journal. Safe for automatic
  // checks on Home without parsing the full library catalog or bookmarks.
  static AchievementSnapshot loadLightweightSnapshot();
  static AchievementSnapshot loadSnapshot();
  static std::vector<AchievementView> buildViews(const AchievementSnapshot& snapshot);
};

const char* achievementMetricLabel(AchievementMetric metric);
std::string achievementTargetLabel(const AchievementView& achievement);
