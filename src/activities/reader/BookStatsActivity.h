#pragma once

#include <HalClock.h>

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "../Activity.h"
#include "BookReadingStats.h"
#include "BookStatsView.h"
#include "GlobalReadingStats.h"
#include "LibraryInsights.h"
#include "ReadingJournal.h"
#include "ReadingLedger.h"

class BookStatsActivity final : public Activity {
 public:
  enum class InitialPage : uint8_t { CurrentBook, Heatmap, ReadingProfile };

 private:
  enum class Page : uint8_t {
    CurrentBook,
    BookProgress,
    ReadingPatterns,
    BookPatterns,
    Trends,
    ActivityChart,
    DailyMinutes,
    MonthlyCalendar,
    Heatmap,
    ReadingProfile,
    Goals,
    RecentSessions,
    WeekdayPattern,
    PaceTrend,
    TimeOfDay,
    MonthlyTrend,
    DeviceSplit,
    YearLine,
    SessionLengths,
    StreakMilestones,
    StartedFinished,
    ReadingDates,
    ReaderRadar,
    ReaderDnaDetails,
    ReaderSignature,
    ReaderSignatureDetails,
    FastestReads,
    Wrapped,
    StartedBooks,
    LibraryOverview,
    ReadingTaste,
    SeriesProgress,
    ThisDevice,
    AllDevices,
    EditDates
  };

  std::string bookTitle;
  std::string bookCachePath;
  BookReadingStats stats;
  BookReadingStats localStats;
  GlobalReadingStats globalStats;
  GlobalReadingStats allDevicesStats;
  ReadingSessionSnapshot sessionSnapshot;
  uint32_t bookWordCount = 0;
  std::unique_ptr<ReadingJournal> readingJournal;
  std::unique_ptr<LibraryInsights> libraryInsights;
  bool showAllDevicesStats = false;
  bool returnToHomeOnExit = false;
  float progressPercent = -1.0f;
  bool hasEstimatedTimeLeft = false;
  uint32_t estimatedTimeLeftSeconds = 0;
  Page page = Page::CurrentBook;
  int selectedEditField = 0;
  bool datesEditMode = false;
  bool didChangeStats = false;
  uint8_t recentSessionOffset = 0;
  uint8_t dailyMinutesOffset = 0;
  std::vector<StartedBookStatsEntry> startedBooks;
  size_t startedBookSelected = 0;
  bool startedBooksLoaded = false;
  bool startedBooksLoadPending = false;
  size_t startedBookScanIndex = 0;
  size_t seriesOffset = 0;
  std::vector<ReadingDateStatsEntry> readingDates;
  size_t readingDatesSelected = 0;
  bool readingDatesLoaded = false;
  std::vector<FastestReadStatsEntry> fastestReads;
  bool fastestReadsLoaded = false;
  StartFinishStatsSummary startFinishSummary;
  bool startFinishSummaryLoaded = false;
  DeviceSplitStatsSummary deviceSplitSummary;
  bool dayDetailsOpen = false;
  bool dayCorrectionEditMode = false;
  ReadingStatsDate dayDetailsDate;
  ReadingLedgerDaySummary dayDetailsSummary;
  uint8_t dayDetailsSelectedBook = 0;
  int32_t pendingDayCorrectionSeconds = 0;

  bool hasEditableBook() const { return !bookCachePath.empty(); }
  bool usesNoRtcSingleScreenLayout() const { return false; }
  void refreshAllDevicesStats();
  void saveStats();
  void cycleEditField();
  void adjustSelectedDateField(int delta);
  void applyCompletedState(bool completed);
  ReadingStatsDate defaultDateForField(bool finishedField) const;
  void clearEditedDate(bool finishedField);
  bool shouldClearDateOnAdjust(const ReadingStatsDate& date, bool finishedField, int fieldIndex, int delta) const;
  void normalizeEditedDates(const bool editedFinishedField);
  void exitStatsActivity(bool viaBack);
  void ensureLibraryInsights();
  void scrollRecentSessions(int delta);
  void ensureStartedBooks();
  void processStartedBooksLoadStep();
  void moveStartedBookSelection(int delta);
  void openSelectedStartedBook();
  void scrollSeries(int delta);
  void ensureReadingDates();
  void moveReadingDatesSelection(int delta);
  void openSelectedReadingDatesBook();
  void ensureFastestReads();
  void ensureStartFinishSummary();
  void ensureDeviceSplitSummary();
  void invalidateDerivedStatsCaches();
  bool processDerivedStatsLoadStep();
  void openDayDetails(const ReadingStatsDate* date = nullptr);
  void refreshDayDetails();
  void changeDayDetailsDate(int delta);
  void moveDayDetailsSelection(int delta);
  bool canAdjustSelectedDayBook() const;
  void adjustPendingDayCorrection(int32_t deltaSeconds);
  bool saveDayCorrection();
  size_t buildVisiblePages(std::array<Page, 35>& pages) const;
  void cyclePage(int delta);
  void drawPageTabs() const;
  static const char* shortPageLabel(Page page);
  static Page pageFromInitial(InitialPage initialPage);

 public:
  BookStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                    const std::string& bookCachePath, const BookReadingStats& stats, float progressPercent,
                    bool hasEstimatedTimeLeft, uint32_t estimatedTimeLeftSeconds, const GlobalReadingStats& globalStats,
                    bool returnToHomeOnExit = false, const ReadingSessionSnapshot& sessionSnapshot = {},
                    uint32_t bookWordCount = 0, InitialPage initialPage = InitialPage::CurrentBook);
  BookStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                    const std::string& bookCachePath, const BookReadingStats& stats, float progressPercent,
                    bool hasEstimatedTimeLeft, uint32_t estimatedTimeLeftSeconds, const GlobalReadingStats& globalStats,
                    const GlobalReadingStats& allDevicesStats, bool returnToHomeOnExit = false,
                    const ReadingSessionSnapshot& sessionSnapshot = {}, uint32_t bookWordCount = 0,
                    InitialPage initialPage = InitialPage::CurrentBook);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool allowPowerAsConfirmInReaderMode() const override { return true; }
};
