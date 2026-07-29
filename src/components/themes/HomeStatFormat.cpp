#include "HomeStatFormat.h"

#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <numeric>

#include "CrossPointSettings.h"
#include "fontIds.h"

namespace {

void formatCompactDuration(const uint32_t seconds, char* buf, const size_t len) {
  if (seconds < 60) {
    snprintf(buf, len, "%s", tr(STR_STATS_LESS_THAN_MIN));
    return;
  }
  const uint32_t minutes = (seconds + 30u) / 60u;
  if (minutes < 60) {
    snprintf(buf, len, "%lu min", static_cast<unsigned long>(minutes));
    return;
  }
  const uint32_t hours = minutes / 60u;
  const uint32_t remainder = minutes % 60u;
  if (remainder == 0) {
    snprintf(buf, len, "%luh", static_cast<unsigned long>(hours));
  } else {
    snprintf(buf, len, "%luh %lum", static_cast<unsigned long>(hours), static_cast<unsigned long>(remainder));
  }
}

bool fallbackEstimatedTimeLeft(const BookReadingStats& stats, const float progressPercent, uint32_t& seconds) {
  seconds = 0;
  if (!stats.hasReliableTimeLeftBasis() || progressPercent <= 0.0f || progressPercent >= 100.0f) {
    return false;
  }
  const float progress = progressPercent / 100.0f;
  const float estimate = (static_cast<float>(stats.totalReadingSeconds) * (1.0f - progress)) / progress;
  if (estimate <= 0.0f) {
    return false;
  }
  seconds = static_cast<uint32_t>(estimate + 0.5f);
  return seconds > 0;
}

bool estimatedTimeLeft(const BookReadingStats& stats, const float progressPercent, uint32_t& seconds) {
  if (!stats.hasReliableTimeLeftBasis()) {
    seconds = 0;
    return false;
  }
  if (stats.estimatedTimeLeftSeconds > 0) {
    seconds = stats.estimatedTimeLeftSeconds;
    return true;
  }
  return fallbackEstimatedTimeLeft(stats, progressPercent, seconds);
}

bool estimateFinishDateFromDailyPace(const BookReadingStats& stats, const ReadingStatsDateTime& today,
                                     const uint32_t estimatedReadingSeconds, ReadingStatsDate& outDate) {
  outDate = {};
  if (!today.isValid() || !stats.startDate.isValid() || estimatedReadingSeconds == 0 ||
      stats.totalReadingSeconds == 0) {
    return false;
  }

  const uint16_t elapsedDays = readingSpanDaysElapsed(stats.startDate, today.date);
  const uint16_t readingDays = std::max<uint16_t>(1, elapsedDays);
  const uint64_t estimatedCalendarSeconds =
      (static_cast<uint64_t>(estimatedReadingSeconds) * static_cast<uint64_t>(readingDays) * 86400ULL +
       static_cast<uint64_t>(stats.totalReadingSeconds) / 2ULL) /
      static_cast<uint64_t>(stats.totalReadingSeconds);
  if (estimatedCalendarSeconds == 0) {
    return false;
  }

  ReadingStatsDateTime estimatedFinish = today;
  addSecondsToReadingStatsDateTime(estimatedFinish,
                                   static_cast<uint32_t>(std::min<uint64_t>(estimatedCalendarSeconds, UINT32_MAX)));
  outDate = estimatedFinish.date;
  return outDate.isValid();
}

uint32_t estimatedWordsRead(const uint32_t totalWords, const float progressPercent) {
  if (totalWords == 0 || progressPercent <= 0.0f) return 0;
  const float clampedProgress = std::clamp(progressPercent, 0.0f, 100.0f) / 100.0f;
  return static_cast<uint32_t>(static_cast<float>(totalWords) * clampedProgress + 0.5f);
}

uint32_t wordsPerMinute(const BookReadingStats& stats, const uint32_t totalWords, const float progressPercent) {
  constexpr uint32_t MIN_WPM_READING_SECONDS = 10;
  if (stats.totalReadingSeconds < MIN_WPM_READING_SECONDS) return 0;
  const uint32_t wordsRead = estimatedWordsRead(totalWords, progressPercent);
  if (wordsRead == 0) return 0;
  return static_cast<uint32_t>(
      (static_cast<uint64_t>(wordsRead) * 60ULL + stats.totalReadingSeconds / 2ULL) / stats.totalReadingSeconds);
}

const char* dayCountText(const uint16_t days) { return days == 1 ? tr(STR_STATS_DAY) : tr(STR_STATS_DAYS); }

}  // namespace

HomeStatContext buildHomeStatContext(const BookReadingStats& book, const float progressPercent,
                                     const GlobalReadingStats* deviceStats) {
  HomeStatContext ctx{book};
  ctx.progressPercent = progressPercent;
  ctx.deviceStats = deviceStats;
  ctx.allStats = deviceStats;
  ctx.hasEstimate = estimatedTimeLeft(book, progressPercent, ctx.estimatedSeconds);
  // Clockless devices resolve this through the Stats Date system; when no
  // valid date exists, date-based stats degrade to "-" instead of hiding.
  ctx.hasToday = getCurrentLocalReadingStatsDateTime(ctx.today);
  const ReadingStatsDate endDate = book.isCompleted && book.finishedDate.isValid()
                                       ? book.finishedDate
                                       : (ctx.hasToday ? ctx.today.date : ReadingStatsDate{});
  ctx.hasDaySpan = book.startDate.isValid() && endDate.isValid();
  ctx.daysReading = ctx.hasDaySpan ? readingSpanDaysElapsed(book.startDate, endDate) : 0;
  return ctx;
}

bool homeStatSelectionWantsAllDevices() {
  const uint8_t slots[] = {SETTINGS.homeStatSlot1, SETTINGS.homeStatSlot2, SETTINGS.homeStatSlot3,
                           SETTINGS.homeStatSlot4, SETTINGS.homeStatSlot5, SETTINGS.homeStatSlot6,
                           SETTINGS.homeStatSlot7, SETTINGS.homeFooterLeft, SETTINGS.homeFooterRight,
                           SETTINGS.homeStrip1,   SETTINGS.homeStrip2,     SETTINGS.homeStrip3,
                           SETTINGS.homeStrip4};
  for (const uint8_t kind : slots) {
    // Streak and reader-type are person-level facts: once peers exist they
    // must read the merged history or each device reports only its own days.
    if (kind == CrossPointSettings::HOME_STAT_TOTAL_TIME_ALL || kind == CrossPointSettings::HOME_STAT_BOOKS_ALL ||
        kind == CrossPointSettings::HOME_STAT_STREAK || kind == CrossPointSettings::HOME_STAT_READER_TYPE) {
      return true;
    }
  }
  return false;
}

bool homeStatDominantReaderBucket(const GlobalReadingStats* stats, ReadingTimeBucket& bucketOut) {
  if (stats == nullptr) return false;
  const auto& values = stats->timeOfDaySeconds;
  const uint32_t totalSeconds = std::accumulate(values.begin(), values.end(), 0u);
  if (totalSeconds == 0) {
    return false;
  }
  size_t bestIndex = 0;
  for (size_t i = 1; i < values.size(); ++i) {
    if (values[i] > values[bestIndex]) bestIndex = i;
  }
  bucketOut = static_cast<ReadingTimeBucket>(bestIndex);
  return true;
}

void homeStatStreakText(const GlobalReadingStats* stats, char* buf, const size_t len) {
  if (len == 0) return;
  if (stats == nullptr) {
    snprintf(buf, len, "%s", tr(STR_STATS_NO_STREAK));
    return;
  }
  ReadingStatsDateTime today;
  const uint16_t streak = getCurrentLocalReadingStatsDateTime(today) ? stats->currentReadingStreak(&today.date) : 0;
  if (streak == 0) {
    snprintf(buf, len, "%s", tr(STR_STATS_NO_STREAK));
    return;
  }
  snprintf(buf, len, tr(STR_STATS_DAY_STREAK_FORMAT), static_cast<unsigned>(streak));
}

const char* homeStatReaderTypeLabel(const GlobalReadingStats* stats) {
  ReadingTimeBucket bucket;
  if (!homeStatDominantReaderBucket(stats, bucket)) {
    return tr(STR_STATS_NEW_READER);
  }
  switch (bucket) {
    case ReadingTimeBucket::Morning:
      return tr(STR_STATS_MORNING_READER);
    case ReadingTimeBucket::Afternoon:
      return tr(STR_STATS_AFTERNOON_READER);
    case ReadingTimeBucket::Evening:
      return tr(STR_STATS_EVENING_READER);
    case ReadingTimeBucket::Night:
    default:
      return tr(STR_STATS_NIGHT_READER);
  }
}

bool formatHomeStat(const uint8_t kind, const HomeStatContext& ctx, char* value, const size_t valueLen, char* label,
                    const size_t labelLen, int& valueFontId) {
  valueFontId = UI_12_FONT_ID;
  const BookReadingStats& bookStats = ctx.book;
  char dateBuf[24];
  switch (kind) {
    case CrossPointSettings::HOME_STAT_BOOK_TIME:
      formatCompactDuration(bookStats.totalReadingSeconds, value, valueLen);
      snprintf(label, labelLen, "%s", tr(STR_HOME_STAT_TIME_READ));
      return true;
    case CrossPointSettings::HOME_STAT_TIME_LEFT:
      if (ctx.hasEstimate && !bookStats.isCompleted) {
        formatCompactDuration(ctx.estimatedSeconds, value, valueLen);
      } else if (!bookStats.isCompleted && ctx.progressPercent > 0.0f && ctx.progressPercent < 100.0f &&
                 !bookStats.hasReliableTimeLeftBasis()) {
        snprintf(value, valueLen, "--");
      } else {
        snprintf(value, valueLen, "-");
      }
      snprintf(label, labelLen, "%s", tr(STR_TIME_LEFT));
      return true;
    case CrossPointSettings::HOME_STAT_PROGRESS:
      if (ctx.progressPercent >= 0.0f) {
        snprintf(value, valueLen, "%d%%", static_cast<int>(ctx.progressPercent + 0.5f));
      } else {
        snprintf(value, valueLen, "-");
      }
      snprintf(label, labelLen, "%s", tr(STR_STATS_PROGRESS_LBL));
      return true;
    case CrossPointSettings::HOME_STAT_DAILY_AVG:
      if (ctx.hasDaySpan) {
        const uint16_t dailyAverageDays = std::max<uint16_t>(1, ctx.daysReading);
        formatCompactDuration(bookStats.totalReadingSeconds / dailyAverageDays, value, valueLen);
      } else {
        snprintf(value, valueLen, "-");
      }
      snprintf(label, labelLen, "%s", tr(STR_STATS_DAILY_AVG_LBL));
      return true;
    case CrossPointSettings::HOME_STAT_PAGES_PER_MIN:
      if (const uint32_t wpm = wordsPerMinute(bookStats, ctx.bookWordCount, ctx.progressPercent); wpm > 0) {
        snprintf(value, valueLen, "%lu", static_cast<unsigned long>(wpm));
      } else {
        snprintf(value, valueLen, "-");
      }
      snprintf(label, labelLen, "%s", tr(STR_STATS_PAGES_PER_MIN));
      return true;
    case CrossPointSettings::HOME_STAT_SESSIONS:
      snprintf(value, valueLen, "%u", static_cast<unsigned>(bookStats.sessionCount));
      snprintf(label, labelLen, "%s", tr(STR_STATS_SESSIONS_LBL));
      return true;
    case CrossPointSettings::HOME_STAT_AVG_SESSION: {
      const uint32_t avgSeconds =
          bookStats.sessionCount > 0 ? bookStats.countedSessionSeconds / bookStats.sessionCount : 0;
      formatCompactDuration(avgSeconds, value, valueLen);
      snprintf(label, labelLen, "%s", tr(STR_STATS_AVG_SESSION_LBL));
      return true;
    }
    case CrossPointSettings::HOME_STAT_DAYS_READING:
      if (ctx.hasDaySpan) {
        snprintf(value, valueLen, "%u %s", static_cast<unsigned>(ctx.daysReading), dayCountText(ctx.daysReading));
      } else {
        snprintf(value, valueLen, "-");
      }
      formatReadingStatsShortDate(bookStats.startDate, dateBuf, sizeof(dateBuf));
      snprintf(label, labelLen, "%s %s", tr(STR_STATS_STARTED), dateBuf);
      return true;
    case CrossPointSettings::HOME_STAT_EST_FINISH: {
      ReadingStatsDate finishDisplayDate;
      if (bookStats.isCompleted) {
        finishDisplayDate = bookStats.finishedDate;
      } else if (ctx.hasToday && ctx.hasEstimate) {
        if (!estimateFinishDateFromDailyPace(bookStats, ctx.today, ctx.estimatedSeconds, finishDisplayDate)) {
          ReadingStatsDateTime estimatedFinish = ctx.today;
          addSecondsToReadingStatsDateTime(estimatedFinish, ctx.estimatedSeconds);
          finishDisplayDate = estimatedFinish.date;
        }
      }
      formatReadingStatsShortDate(finishDisplayDate, value, valueLen);
      snprintf(label, labelLen, "%s",
               bookStats.isCompleted ? tr(STR_STATS_FINISHED_DATE) : tr(STR_STATS_EST_FINISH_DATE));
      return true;
    }
    case CrossPointSettings::HOME_STAT_STREAK:
      homeStatStreakText(ctx.allStats != nullptr ? ctx.allStats : ctx.deviceStats, value, valueLen);
      valueFontId = SMALL_FONT_ID;
      snprintf(label, labelLen, "%s", tr(STR_HOME_STAT_STREAK));
      return true;
    case CrossPointSettings::HOME_STAT_READER_TYPE:
      snprintf(value, valueLen, "%s",
               homeStatReaderTypeLabel(ctx.allStats != nullptr ? ctx.allStats : ctx.deviceStats));
      valueFontId = SMALL_FONT_ID;
      snprintf(label, labelLen, "%s", tr(STR_HOME_STAT_READER_TYPE));
      return true;
    case CrossPointSettings::HOME_STAT_TOTAL_TIME_DEVICE:
      formatCompactDuration(ctx.deviceStats != nullptr ? ctx.deviceStats->totalReadingSeconds : 0, value, valueLen);
      snprintf(label, labelLen, "%s", tr(STR_HOME_STAT_LBL_DEVICE_TOTAL));
      return true;
    case CrossPointSettings::HOME_STAT_TOTAL_TIME_ALL:
      formatCompactDuration(ctx.allStats != nullptr ? ctx.allStats->totalReadingSeconds : 0, value, valueLen);
      snprintf(label, labelLen, "%s", tr(STR_HOME_STAT_LBL_ALL_TOTAL));
      return true;
    case CrossPointSettings::HOME_STAT_BOOKS_DEVICE:
      snprintf(value, valueLen, "%lu",
               static_cast<unsigned long>(ctx.deviceStats != nullptr ? ctx.deviceStats->completedBooks : 0));
      snprintf(label, labelLen, "%s", tr(STR_HOME_STAT_LBL_BOOKS));
      return true;
    case CrossPointSettings::HOME_STAT_BOOKS_ALL:
      snprintf(value, valueLen, "%lu",
               static_cast<unsigned long>(ctx.allStats != nullptr ? ctx.allStats->completedBooks : 0));
      snprintf(label, labelLen, "%s", tr(STR_HOME_STAT_LBL_BOOKS_ALL));
      return true;
    case CrossPointSettings::HOME_STAT_TODAY_TIME:
      formatCompactDuration(ctx.todaySeconds, value, valueLen);
      snprintf(label, labelLen, "%s", tr(STR_HOME_STAT_TODAY));
      return true;
    case CrossPointSettings::HOME_STAT_TOTAL_SESSIONS:
      snprintf(value, valueLen, "%lu",
               static_cast<unsigned long>(ctx.deviceStats != nullptr ? ctx.deviceStats->totalSessions : 0));
      snprintf(label, labelLen, "%s", tr(STR_HOME_STAT_SESSIONS));
      return true;
    default:
      return false;
  }
}
