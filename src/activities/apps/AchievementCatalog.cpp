#include "AchievementCatalog.h"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "BookmarkStore.h"
#include "CrossPointSettings.h"
#include "I18n.h"
#include "RecentBooksStore.h"
#include "activities/reader/GlobalReadingStats.h"
#include "activities/reader/LibraryInsights.h"
#include "activities/reader/ReadingJournal.h"
#include "activities/reader/ReadingStatsUtils.h"

namespace {
template <size_t N>
void appendMilestones(std::vector<AchievementView>& views, const AchievementMetric metric, const uint64_t progress,
                      const std::array<uint64_t, N>& targets) {
  for (const uint64_t target : targets) {
    views.push_back({metric, progress, target, progress >= target});
  }
}

uint32_t currentBookmarkCount() {
  std::vector<BookmarkedBookEntry> books;
  if (!BookmarkStore::getAllBookmarkedBooks(books)) return 0;

  uint32_t total = 0;
  for (const auto& book : books) {
    if (std::numeric_limits<uint32_t>::max() - total < book.count) {
      return std::numeric_limits<uint32_t>::max();
    }
    total += book.count;
  }
  return total;
}

uint32_t countReadingDays(const GlobalReadingStats& stats) {
  if (stats.readingHistoryAnchorDay == 0) return 0;
  uint32_t total = 0;
  for (uint32_t offset = 0; offset < READING_HISTORY_DAYS && offset <= stats.readingHistoryAnchorDay; ++offset) {
    if (stats.hasReadingOnDay(stats.readingHistoryAnchorDay - offset)) ++total;
  }
  return total;
}

bool durationMetric(const AchievementMetric metric) {
  return metric == AchievementMetric::TotalReadingSeconds || metric == AchievementMetric::LongestSessionSeconds ||
         metric == AchievementMetric::MorningReadingSeconds || metric == AchievementMetric::NightReadingSeconds ||
         metric == AchievementMetric::WeekendReadingSeconds;
}

std::string formatDuration(const uint64_t seconds) {
  const uint64_t minutes = seconds / 60u;
  if (minutes < 60) return std::to_string(minutes) + "m";
  const uint64_t hours = minutes / 60u;
  const uint64_t remainingMinutes = minutes % 60u;
  if (remainingMinutes == 0) return std::to_string(hours) + "h";
  return std::to_string(hours) + "h " + std::to_string(remainingMinutes) + "m";
}

AchievementSnapshot loadCoreSnapshot(const bool includeJournal) {
  AchievementSnapshot snapshot;
  const GlobalReadingStats global = GlobalReadingStats::loadAggregated();
  snapshot.booksFinished = global.completedBooks;
  snapshot.sessions = global.totalSessions;
  snapshot.totalReadingSeconds = global.totalReadingSeconds;
  snapshot.screenPagesTurned = global.totalPagesTurned;
  snapshot.readingDays = countReadingDays(global);
  snapshot.longestReadingStreak = global.displayLongestReadingStreak();
  snapshot.morningReadingSeconds = global.timeOfDaySeconds[static_cast<size_t>(ReadingTimeBucket::Morning)];
  snapshot.nightReadingSeconds = global.timeOfDaySeconds[static_cast<size_t>(ReadingTimeBucket::Night)];
  snapshot.weekendReadingSeconds = static_cast<uint64_t>(global.dayOfWeekSeconds[5]) + global.dayOfWeekSeconds[6];
  snapshot.crossDeviceSync = GlobalReadingStats::hasSyncedStats() ? 1 : 0;
  snapshot.booksStarted = std::max<uint32_t>(snapshot.booksFinished, RECENT_BOOKS.getCount());

  if (includeJournal) {
    const auto journal = ReadingJournal::loadAggregated();
    if (!journal) return snapshot;
    snapshot.longestSessionSeconds = journal->longestSession();
    ReadingStatsDateTime now;
    if (getCurrentLocalReadingStatsDateTime(now)) {
      const uint32_t today = readingStatsDayIndex(now.date);
      const uint32_t goalSeconds = static_cast<uint32_t>(SETTINGS.readingGoalMinutes) * 60u;
      snapshot.goalDays = journal->goalDaysEndingOn(today, ReadingJournal::HISTORY_DAYS, goalSeconds);
      snapshot.longestGoalStreak = journal->longestGoalStreak(goalSeconds);
    }
  }
  return snapshot;
}
}  // namespace

const char* achievementMetricLabel(const AchievementMetric metric) {
  switch (metric) {
    case AchievementMetric::BooksStarted:
      return tr(STR_ACHIEVEMENT_BOOKS_STARTED);
    case AchievementMetric::BooksFinished:
      return tr(STR_ACHIEVEMENT_BOOKS_FINISHED);
    case AchievementMetric::Sessions:
      return tr(STR_ACHIEVEMENT_SESSIONS);
    case AchievementMetric::TotalReadingSeconds:
      return tr(STR_ACHIEVEMENT_READING_TIME);
    case AchievementMetric::GoalDays:
      return tr(STR_ACHIEVEMENT_GOAL_DAYS);
    case AchievementMetric::LongestGoalStreak:
      return tr(STR_ACHIEVEMENT_GOAL_STREAK);
    case AchievementMetric::Bookmarks:
      return tr(STR_ACHIEVEMENT_BOOKMARKS);
    case AchievementMetric::LongestSessionSeconds:
      return tr(STR_ACHIEVEMENT_LONGEST_SESSION);
    case AchievementMetric::ReadingDays:
      return tr(STR_ACHIEVEMENT_READING_DAYS);
    case AchievementMetric::LongestReadingStreak:
      return tr(STR_ACHIEVEMENT_READING_STREAK);
    case AchievementMetric::ScreenPagesTurned:
      return tr(STR_ACHIEVEMENT_SCREEN_PAGES);
    case AchievementMetric::SeriesStarted:
      return tr(STR_ACHIEVEMENT_SERIES_STARTED);
    case AchievementMetric::SeriesCompleted:
      return tr(STR_ACHIEVEMENT_SERIES_COMPLETED);
    case AchievementMetric::SpiceLevelsExplored:
      return tr(STR_ACHIEVEMENT_SPICE_LEVELS);
    case AchievementMetric::MorningReadingSeconds:
      return tr(STR_ACHIEVEMENT_EARLY_BIRD);
    case AchievementMetric::NightReadingSeconds:
      return tr(STR_ACHIEVEMENT_NIGHT_READER);
    case AchievementMetric::WeekendReadingSeconds:
      return tr(STR_ACHIEVEMENT_WEEKEND_READER);
    case AchievementMetric::CrossDeviceSync:
      return tr(STR_ACHIEVEMENT_CROSS_DEVICE);
    case AchievementMetric::Count:
      break;
  }
  return "";
}

std::string achievementTargetLabel(const AchievementView& achievement) {
  return std::string(achievementMetricLabel(achievement.metric)) + ": " +
         (durationMetric(achievement.metric) ? formatDuration(achievement.target)
                                             : std::to_string(achievement.target));
}

AchievementSnapshot AchievementCatalog::loadLightweightSnapshot() { return loadCoreSnapshot(false); }

AchievementSnapshot AchievementCatalog::loadSnapshot() {
  AchievementSnapshot snapshot = loadCoreSnapshot(true);

  const auto insights = LibraryInsights::load();
  if (insights && insights->available) {
    snapshot.booksStarted = static_cast<uint32_t>(insights->readingBooks) + insights->finishedBooks;
    snapshot.booksFinished = std::max<uint32_t>(snapshot.booksFinished, insights->finishedBooks);
    snapshot.seriesStarted = insights->seriesStarted;
    for (size_t i = 0; i < insights->seriesProgressCount; ++i) {
      const auto& series = insights->seriesProgress[i];
      if (series.books > 0 && series.finished >= series.books) ++snapshot.seriesCompleted;
    }
    for (size_t i = 0; i < insights->spiceLevelCount; ++i) {
      if (insights->spiceLevels[i].readingSeconds > 0) ++snapshot.spiceLevelsExplored;
    }
  }
  snapshot.bookmarks = currentBookmarkCount();
  return snapshot;
}

std::vector<AchievementView> AchievementCatalog::buildViews(const AchievementSnapshot& snapshot) {
  constexpr std::array<uint64_t, 5> BOOKS_STARTED = {1, 5, 10, 25, 50};
  constexpr std::array<uint64_t, 24> BOOKS_FINISHED = {1,  2,  3,  5,  7,  10, 15, 20, 25, 30, 35, 40,
                                                       45, 50, 55, 60, 65, 70, 75, 80, 85, 90, 95, 100};
  constexpr std::array<uint64_t, 6> SESSIONS = {1, 10, 25, 50, 100, 200};
  constexpr std::array<uint64_t, 7> READING_SECONDS = {3600, 5 * 3600, 10 * 3600, 24 * 3600,
                                                       50 * 3600, 100 * 3600, 200 * 3600};
  constexpr std::array<uint64_t, 5> GOAL_DAYS = {1, 7, 30, 60, 80};
  constexpr std::array<uint64_t, 5> GOAL_STREAK = {3, 7, 14, 30, 60};
  constexpr std::array<uint64_t, 4> BOOKMARK_TARGETS = {1, 10, 25, 50};
  constexpr std::array<uint64_t, 6> SESSION_SECONDS = {15 * 60, 30 * 60, 45 * 60, 60 * 60, 90 * 60, 120 * 60};
  constexpr std::array<uint64_t, 8> READING_DAYS = {3, 7, 14, 30, 60, 100, 180, 365};
  constexpr std::array<uint64_t, 6> READING_STREAK = {3, 7, 14, 30, 60, 100};
  constexpr std::array<uint64_t, 7> SCREEN_PAGES = {100, 500, 1000, 2500, 5000, 10000, 25000};
  constexpr std::array<uint64_t, 4> SERIES = {1, 3, 5, 10};
  constexpr std::array<uint64_t, 4> SPICE_LEVELS = {2, 3, 5, 7};
  constexpr std::array<uint64_t, 4> HABIT_SECONDS = {3600, 5 * 3600, 10 * 3600, 25 * 3600};
  constexpr std::array<uint64_t, 1> CROSS_DEVICE = {1};

  std::vector<AchievementView> views;
  views.reserve(104);
  appendMilestones(views, AchievementMetric::BooksStarted, snapshot.booksStarted, BOOKS_STARTED);
  appendMilestones(views, AchievementMetric::Sessions, snapshot.sessions, SESSIONS);
  appendMilestones(views, AchievementMetric::BooksFinished, snapshot.booksFinished, BOOKS_FINISHED);
  appendMilestones(views, AchievementMetric::TotalReadingSeconds, snapshot.totalReadingSeconds, READING_SECONDS);
  appendMilestones(views, AchievementMetric::GoalDays, snapshot.goalDays, GOAL_DAYS);
  appendMilestones(views, AchievementMetric::LongestGoalStreak, snapshot.longestGoalStreak, GOAL_STREAK);
  appendMilestones(views, AchievementMetric::Bookmarks, snapshot.bookmarks, BOOKMARK_TARGETS);
  appendMilestones(views, AchievementMetric::LongestSessionSeconds, snapshot.longestSessionSeconds, SESSION_SECONDS);
  appendMilestones(views, AchievementMetric::ReadingDays, snapshot.readingDays, READING_DAYS);
  appendMilestones(views, AchievementMetric::LongestReadingStreak, snapshot.longestReadingStreak, READING_STREAK);
  appendMilestones(views, AchievementMetric::ScreenPagesTurned, snapshot.screenPagesTurned, SCREEN_PAGES);
  appendMilestones(views, AchievementMetric::SeriesStarted, snapshot.seriesStarted, SERIES);
  appendMilestones(views, AchievementMetric::SeriesCompleted, snapshot.seriesCompleted, SERIES);
  appendMilestones(views, AchievementMetric::SpiceLevelsExplored, snapshot.spiceLevelsExplored, SPICE_LEVELS);
  appendMilestones(views, AchievementMetric::MorningReadingSeconds, snapshot.morningReadingSeconds, HABIT_SECONDS);
  appendMilestones(views, AchievementMetric::NightReadingSeconds, snapshot.nightReadingSeconds, HABIT_SECONDS);
  appendMilestones(views, AchievementMetric::WeekendReadingSeconds, snapshot.weekendReadingSeconds, HABIT_SECONDS);
  appendMilestones(views, AchievementMetric::CrossDeviceSync, snapshot.crossDeviceSync, CROSS_DEVICE);
  return views;
}
