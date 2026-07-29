#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "BookReadingStats.h"
#include "GlobalReadingStats.h"
#include "LibraryInsights.h"
#include "ReadingJournal.h"
#include "ReadingLedger.h"

class GfxRenderer;
class MappedInputManager;

constexpr uint8_t BOOK_STATS_VISIBLE_SESSION_ROWS = 6;
constexpr size_t BOOK_STATS_VISIBLE_SERIES_ROWS = 6;
constexpr size_t BOOK_STATS_VISIBLE_STARTED_BOOK_ROWS = 6;
constexpr size_t BOOK_STATS_VISIBLE_DATE_ROWS = 6;

struct StartedBookStatsEntry {
  std::string path;
  std::string title;
  std::string author;
  std::string coverBmpPath;
  uint32_t readingSeconds = 0;
  uint16_t sessions = 0;
  ReadingStatsDate estFinishDate{};
};

struct ReadingStreakSummary {
  uint16_t activeDays = 0;
  uint16_t current = 0;
  uint16_t longest = 0;
};

struct ReadingDateStatsEntry {
  uint64_t key = 0;
  std::string cachePath;
  std::string title;
  ReadingStatsDate startDate{};
  ReadingStatsDate finishedDate{};
  bool completed = false;
};

std::vector<ReadingDateStatsEntry> loadReadingDateStatsEntries();
std::vector<StartedBookStatsEntry> loadStartedBookStatsEntries();

ReadingStreakSummary summarizeReadingStreaks(const ReadingJournal* journal, const GlobalReadingStats& history,
                                             const ReadingSessionSnapshot& session, uint32_t todayDayIndex,
                                             uint16_t lookbackDays);

void renderStatsTabBar(GfxRenderer& renderer, const char* const* labels, size_t labelCount, size_t selectedIndex);

void renderCurrentBookStatsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                const std::string& bookTitle, const BookReadingStats& stats,
                                const ReadingSessionSnapshot& session, float progressPercent, bool hasEstimatedTimeLeft,
                                uint32_t estimatedTimeLeftSeconds, bool showButtonHints, bool showEditButton,
                                bool showMoreButton, uint32_t bookWordCount = 0);

void renderBookProgressGraphPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                 const std::string& bookTitle, const BookReadingStats& stats, float progressPercent,
                                 bool hasEstimatedTimeLeft, uint32_t estimatedTimeLeftSeconds, bool showButtonHints,
                                 bool showMoreButton, uint32_t bookWordCount = 0);

void renderPerBookStatsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const std::string& bookTitle,
                            const BookReadingStats& stats, float progressPercent, bool hasEstimatedTimeLeft,
                            uint32_t estimatedTimeLeftSeconds, bool showButtonHints, bool showEditButton,
                            bool showMoreButton, uint32_t bookWordCount = 0);

void renderGlobalStatsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const char* screenTitle,
                           const GlobalReadingStats& stats, bool showButtonHints, bool showMoreButton);

void renderReadingTrendsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                             const ReadingJournal* journal, const GlobalReadingStats& history,
                             const ReadingSessionSnapshot& session, bool showButtonHints, bool showMoreButton);

void renderWeekdayPatternPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                              const ReadingJournal* journal, const GlobalReadingStats& history,
                              const ReadingSessionSnapshot& session, bool showButtonHints, bool showMoreButton);

void renderPaceTrendPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const ReadingJournal* journal,
                         const ReadingSessionSnapshot& session, bool showButtonHints, bool showMoreButton);

void renderReadingWrappedPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                              const ReadingJournal* journal, const GlobalReadingStats& history,
                              const ReadingSessionSnapshot& session, bool showButtonHints, bool showMoreButton);

void renderYearLinePage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const ReadingJournal* journal,
                        const ReadingSessionSnapshot& session, bool showButtonHints, bool showMoreButton);

void renderSessionLengthsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                              const ReadingJournal* journal, const ReadingSessionSnapshot& session,
                              bool showButtonHints, bool showMoreButton);

void renderStreakMilestonesPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                const ReadingJournal* journal, const GlobalReadingStats& history,
                                const ReadingSessionSnapshot& session, bool showButtonHints, bool showMoreButton);

void renderStartedFinishedPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, bool showButtonHints,
                               bool showMoreButton);

void renderReadingDatesPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                            const std::vector<ReadingDateStatsEntry>& books, size_t selectedIndex, bool loading,
                            bool showButtonHints, bool showMoreButton);

void renderFastestReadsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, bool showButtonHints,
                            bool showMoreButton);

void renderReaderRadarPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const ReadingJournal* journal,
                           const GlobalReadingStats& history, const ReadingSessionSnapshot& session,
                           const LibraryInsights* insights, uint8_t goalMinutes, bool showButtonHints,
                           bool showMoreButton);

void renderReaderDnaDetailsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                const ReadingJournal* journal, const GlobalReadingStats& history,
                                const ReadingSessionSnapshot& session, bool showButtonHints, bool showMoreButton);

void renderReadingSignaturePage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                const ReadingJournal* journal, const GlobalReadingStats& history,
                                const ReadingSessionSnapshot& session, uint8_t goalMinutes, bool showButtonHints,
                                bool showMoreButton);

void renderReadingSignatureDetailsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                       const ReadingJournal* journal, const GlobalReadingStats& history,
                                       const ReadingSessionSnapshot& session, uint8_t goalMinutes, bool showButtonHints,
                                       bool showMoreButton);

void renderTimeOfDayPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                         const GlobalReadingStats& history, bool showButtonHints, bool showMoreButton);

void renderMonthlyTrendPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const ReadingJournal* journal,
                            const ReadingSessionSnapshot& session, bool showButtonHints, bool showMoreButton);

void renderDeviceSplitPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                           const GlobalReadingStats& localStats, bool showButtonHints, bool showMoreButton);

void renderReadingActivityChartPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                    const ReadingJournal* journal, const GlobalReadingStats& history,
                                    const ReadingSessionSnapshot& session, uint8_t goalMinutes, bool showButtonHints,
                                    bool showMoreButton);

void renderReadingDailyMinutesPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                   const ReadingJournal* journal, const GlobalReadingStats& history,
                                   const ReadingSessionSnapshot& session, uint8_t selectedDayOffset,
                                   bool showButtonHints, bool showMoreButton);

void renderReadingHeatmapPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                              const ReadingJournal* journal, const GlobalReadingStats& history,
                              const ReadingSessionSnapshot& session, uint8_t goalMinutes, bool showButtonHints,
                              bool showMoreButton);

void renderMonthlyReadingCalendarPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                      const ReadingJournal* journal, const GlobalReadingStats& history,
                                      const ReadingSessionSnapshot& session, uint8_t goalMinutes, bool showButtonHints,
                                      bool showMoreButton);
void renderReadingDayDetailsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                 const ReadingStatsDate& date, const ReadingLedgerDaySummary& summary,
                                 uint8_t selectedBook, bool editMode, int32_t pendingCorrectionSeconds, bool canAdjust,
                                 bool showButtonHints);

void renderReadingProfilePage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                              const ReadingJournal* journal, const GlobalReadingStats& history,
                              const ReadingSessionSnapshot& session, uint8_t goalMinutes, bool showButtonHints,
                              bool showMoreButton);

void renderReadingGoalsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const ReadingJournal* journal,
                            const GlobalReadingStats& history, const ReadingSessionSnapshot& session,
                            uint8_t goalMinutes, bool showButtonHints, bool showMoreButton);

void renderRecentReadingSessionsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                     const ReadingJournal* journal, uint32_t totalSessions, uint8_t startOffset,
                                     bool showButtonHints, bool showMoreButton);

void renderStartedBooksPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                            const std::vector<StartedBookStatsEntry>& books, size_t selectedIndex, bool loading,
                            bool showButtonHints, bool showMoreButton);

void renderLibraryInsightsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                               const LibraryInsights* insights, bool showButtonHints, bool showMoreButton);

void renderReadingTastePage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                            const LibraryInsights* insights, bool showButtonHints, bool showMoreButton);

void renderSeriesProgressPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                              const LibraryInsights* insights, size_t startOffset, bool showButtonHints,
                              bool showMoreButton);

void renderNoRtcCombinedStatsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                  const std::string& bookTitle, const BookReadingStats& bookStats,
                                  float progressPercent, bool hasEstimatedTimeLeft, uint32_t estimatedTimeLeftSeconds,
                                  const GlobalReadingStats& deviceStats, const GlobalReadingStats* allDevicesStats,
                                  bool showButtonHints, uint32_t bookWordCount = 0);

void renderEditBookDatesPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const std::string& bookTitle,
                             const BookReadingStats& stats, int selectedField, bool editMode, bool showButtonHints);
