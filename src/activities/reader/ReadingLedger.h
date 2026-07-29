#pragma once

#include <DuetStoragePaths.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "ReadingStatsUtils.h"

inline constexpr size_t READING_LEDGER_DAY_BOOK_LIMIT = 8;
inline constexpr size_t READING_LEDGER_CACHE_PATH_BYTES = 48;
inline constexpr size_t READING_LEDGER_TITLE_BYTES = 60;

struct ReadingLedgerDayBook {
  char cachePath[READING_LEDGER_CACHE_PATH_BYTES] = {};
  char title[READING_LEDGER_TITLE_BYTES] = {};
  uint32_t readingSeconds = 0;
  uint32_t screenPages = 0;
};

struct ReadingLedgerDaySummary {
  uint32_t dayIndex = 0;
  uint32_t attributedSeconds = 0;
  uint32_t otherBookSeconds = 0;
  uint32_t legacyUnattributedSeconds = 0;
  uint16_t validRecordCount = 0;
  uint16_t invalidRecordCount = 0;
  uint8_t bookCount = 0;
  bool truncated = false;
  std::array<ReadingLedgerDayBook, READING_LEDGER_DAY_BOOK_LIMIT> books{};
};

// Append-only, per-book daily attribution. This is deliberately separate from
// ReadingJournal so a damaged or newer ledger can never prevent legacy totals
// and streak history from loading.
class ReadingLedger {
 public:
  static constexpr const char* PATH = DUET_STATE_ROOT_PATH "/reading_ledger_v1.bin";

  static bool recordReadingSpan(const ReadingStatsDateTime& localStart, uint32_t readingSeconds,
                                uint16_t screenPages, const std::string& cachePath, const std::string& title);
  static bool recordCorrection(const ReadingStatsDate& date, int32_t readingSecondsDelta,
                               const std::string& cachePath, const std::string& title);
  static bool summarizeDay(uint32_t dayIndex, uint32_t journalSeconds, ReadingLedgerDaySummary& out);
  // Visits every valid record in the local ledger plus each synced peer
  // ledger: (dayIndex, secondsDelta, cachePath, title). Read-only; used by
  // the timeline and fastest-read stats pages.
  using RecordVisitor = void (*)(uint32_t dayIndex, int32_t secondsDelta, const char* cachePath, const char* title,
                                 void* ctx);
  static void forEachRecord(RecordVisitor visitor, void* ctx);
  static bool resetLocal();

 private:
  static bool appendRecord(uint32_t dayIndex, int32_t readingSecondsDelta, uint16_t screenPages, uint8_t flags,
                           const std::string& cachePath, const std::string& title);
};
