#pragma once

#include <cstddef>
#include <cstdint>

#include "activities/reader/BookReadingStats.h"
#include "activities/reader/GlobalReadingStats.h"
#include "activities/reader/ReadingStatsUtils.h"

// Shared formatter behind the Settings > Display > Home Stats picker. Every
// Home theme that renders stat slots (Dashboard, Reading Home, Minimal)
// resolves CrossPointSettings::HOME_STAT values through this one catalog so a
// stat means the same thing everywhere.

// Everything a stat formatter might need, computed once per paint.
struct HomeStatContext {
  const BookReadingStats& book;
  float progressPercent = 0.0f;
  const GlobalReadingStats* deviceStats = nullptr;
  const GlobalReadingStats* allStats = nullptr;  // aggregated; falls back to deviceStats
  bool hasToday = false;
  ReadingStatsDateTime today{};
  bool hasEstimate = false;
  uint32_t estimatedSeconds = 0;
  bool hasDaySpan = false;
  uint16_t daysReading = 0;
  uint32_t bookWordCount = 0;
  // Journal-derived extras; themes that load the journal fill these in.
  uint32_t todaySeconds = 0;
  uint16_t journalStreakDays = 0;
};

HomeStatContext buildHomeStatContext(const BookReadingStats& book, float progressPercent,
                                     const GlobalReadingStats* deviceStats);

// Formats one stat slot into value/label text. Returns false for NONE or
// unknown kinds so callers can skip the slot entirely. valueFontId is set to
// a smaller font for long textual values.
bool formatHomeStat(uint8_t kind, const HomeStatContext& ctx, char* value, size_t valueLen, char* label,
                    size_t labelLen, int& valueFontId);

// True when any configured slot needs the aggregated all-devices totals, so
// themes only load them when a slot will actually display them.
bool homeStatSelectionWantsAllDevices();

// Dominant time-of-day bucket for icon selection in themes.
bool homeStatDominantReaderBucket(const GlobalReadingStats* stats, ReadingTimeBucket& bucketOut);

// Streak text ("11 day streak" / "No streak") from device global stats.
void homeStatStreakText(const GlobalReadingStats* stats, char* buf, size_t len);

// Time-of-day reader label ("Evening Reader" / "New Reader").
const char* homeStatReaderTypeLabel(const GlobalReadingStats* stats);
