#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "ReadingStatsUtils.h"

struct ReadingJournalPeriod {
  uint32_t readingSeconds = 0;
  uint32_t screenPages = 0;
  uint16_t sessions = 0;
  uint16_t completedBooks = 0;
  uint16_t activeDays = 0;
};

struct ReadingJournalSession {
  uint32_t dayIndex = 0;
  uint16_t startMinute = 0;
  uint32_t readingSeconds = 0;
  uint16_t screenPages = 0;
};

struct ReadingSessionSnapshot {
  uint32_t readingSeconds = 0;
  uint16_t screenPages = 0;
  ReadingStatsDateTime startedAt;
  bool hasStartedAt = false;
};

// Additive day-by-day history. This intentionally lives beside global_stats.bin
// so richer history can evolve without migrating or risking existing totals.
class ReadingJournal {
 public:
  static constexpr size_t HISTORY_DAYS = 366;
  static constexpr size_t RECENT_SESSION_COUNT = 12;

  static std::unique_ptr<ReadingJournal> load();
  static std::unique_ptr<ReadingJournal> loadAggregated();
  static bool recordSession(const ReadingStatsDateTime& localStart, uint32_t readingSeconds, uint16_t screenPages);
  static bool adjustReadingTime(const ReadingStatsDate& date, int32_t readingSecondsDelta);
  static bool adjustCompletion(const ReadingStatsDate& date, int delta);
  static bool publishLocalForSync();
  static bool resetLocal();

  ReadingJournalPeriod periodEndingOn(uint32_t dayIndex, uint16_t days) const;
  uint32_t secondsOnDay(uint32_t dayIndex) const;
  uint32_t pagesOnDay(uint32_t dayIndex) const;
  uint32_t sessionsOnDay(uint32_t dayIndex) const;
  uint16_t currentGoalStreak(uint32_t todayDayIndex, uint32_t goalSeconds) const;
  uint16_t longestGoalStreak(uint32_t goalSeconds) const;
  uint16_t goalDaysEndingOn(uint32_t dayIndex, uint16_t days, uint32_t goalSeconds) const;
  uint32_t longestSession() const { return longestSessionSeconds_; }
  uint8_t recentSessionCount() const { return recentCount_; }
  bool recentSession(uint8_t newestOffset, ReadingJournalSession& out) const;

 private:
  uint32_t anchorDay_ = 0;
  uint32_t longestSessionSeconds_ = 0;
  uint8_t recentCount_ = 0;
  uint8_t recentNext_ = 0;
  std::array<uint32_t, HISTORY_DAYS> dailyReadingSeconds_{};
  std::array<uint16_t, HISTORY_DAYS> dailyScreenPages_{};
  std::array<uint8_t, HISTORY_DAYS> dailySessions_{};
  std::array<uint8_t, HISTORY_DAYS> dailyCompletedBooks_{};
  std::array<ReadingJournalSession, RECENT_SESSION_COUNT> recentSessions_{};

  static bool loadFromPath(const char* path, ReadingJournal& journal);
  bool save(bool rotateBackup = true) const;
  void advanceAnchor(uint32_t dayIndex);
  bool offsetForDay(uint32_t dayIndex, size_t& offset) const;
  void addReadingSpan(const ReadingStatsDateTime& localStart, uint32_t readingSeconds);
  void addSession(const ReadingStatsDateTime& localStart, uint32_t readingSeconds, uint16_t screenPages);
  void mergeFromPeer(const ReadingJournal& peer);
};
