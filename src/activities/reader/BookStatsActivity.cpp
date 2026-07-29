#include "BookStatsActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Txt.h>
#include <Xtc.h>

#include <algorithm>
#include <cstring>
#include <limits>

#include "BookStatsView.h"
#include "CrossPointSettings.h"
#include "LibraryInsights.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/home/BookInfoActivity.h"
#include "components/CompactHeader.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long DATE_EDIT_UNLOCK_MS = 900;

bool sameDate(const ReadingStatsDate& lhs, const ReadingStatsDate& rhs) {
  return lhs.year == rhs.year && lhs.month == rhs.month && lhs.day == rhs.day;
}

uint32_t addSaturated(const uint32_t current, const uint32_t value) {
  return std::numeric_limits<uint32_t>::max() - current < value ? std::numeric_limits<uint32_t>::max()
                                                                : current + value;
}

std::string startedBookCachePath(const std::string& path) {
  if (FsHelpers::hasEpubExtension(path)) return Epub::cachePathForFilePath(path, DUET_BOOKS_ROOT_PATH "");
  if (FsHelpers::hasXtcExtension(path)) return Xtc(path, DUET_BOOKS_ROOT_PATH "").getCachePath();
  if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path)) {
    return Txt(path, DUET_BOOKS_ROOT_PATH "").getCachePath();
  }
  return {};
}

}  // namespace

BookStatsActivity::Page BookStatsActivity::pageFromInitial(const InitialPage initialPage) {
  switch (initialPage) {
    case InitialPage::Heatmap:
      return Page::Heatmap;
    case InitialPage::ReadingProfile:
      return Page::ReadingProfile;
    case InitialPage::CurrentBook:
    default:
      return Page::CurrentBook;
  }
}

BookStatsActivity::BookStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                                     const std::string& bookCachePath, const BookReadingStats& stats,
                                     const float progressPercent, const bool hasEstimatedTimeLeft,
                                     const uint32_t estimatedTimeLeftSeconds, const GlobalReadingStats& globalStats,
                                     const bool returnToHomeOnExit, const ReadingSessionSnapshot& sessionSnapshot,
                                     const uint32_t bookWordCount, const InitialPage initialPage)
    : Activity("BookStats", renderer, mappedInput),
      bookTitle(title),
      bookCachePath(bookCachePath),
      stats(stats),
      globalStats(globalStats),
      sessionSnapshot(sessionSnapshot),
      bookWordCount(bookWordCount),
      returnToHomeOnExit(returnToHomeOnExit),
      progressPercent(progressPercent),
      hasEstimatedTimeLeft(hasEstimatedTimeLeft),
      estimatedTimeLeftSeconds(estimatedTimeLeftSeconds),
      page(pageFromInitial(initialPage)) {}

BookStatsActivity::BookStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                                     const std::string& bookCachePath, const BookReadingStats& stats,
                                     const float progressPercent, const bool hasEstimatedTimeLeft,
                                     const uint32_t estimatedTimeLeftSeconds, const GlobalReadingStats& globalStats,
                                     const GlobalReadingStats& allDevicesStats, const bool returnToHomeOnExit,
                                     const ReadingSessionSnapshot& sessionSnapshot, const uint32_t bookWordCount,
                                     const InitialPage initialPage)
    : Activity("BookStats", renderer, mappedInput),
      bookTitle(title),
      bookCachePath(bookCachePath),
      stats(stats),
      globalStats(globalStats),
      allDevicesStats(allDevicesStats),
      sessionSnapshot(sessionSnapshot),
      bookWordCount(bookWordCount),
      showAllDevicesStats(true),
      returnToHomeOnExit(returnToHomeOnExit),
      progressPercent(progressPercent),
      hasEstimatedTimeLeft(hasEstimatedTimeLeft),
      estimatedTimeLeftSeconds(estimatedTimeLeftSeconds),
      page(pageFromInitial(initialPage)) {}

void BookStatsActivity::refreshAllDevicesStats() {
  if (showAllDevicesStats) {
    allDevicesStats = GlobalReadingStats::loadAggregated(globalStats);
    if (libraryInsights && libraryInsights->available) {
      // Global snapshots are device totals, so summing peers can count the
      // same finished title once per reader. The catalog merges per-book
      // detail by canonical key and is the authoritative all-device count.
      allDevicesStats.completedBooks = libraryInsights->finishedBooks;
    }
  }
}

void BookStatsActivity::saveStats() {
  if (!didChangeStats || !hasEditableBook()) {
    return;
  }

  localStats.isCompleted = stats.isCompleted;
  localStats.startDateManual = stats.startDateManual;
  localStats.finishedDateManual = stats.finishedDateManual;
  localStats.startDate = stats.startDate;
  localStats.finishedDate = stats.finishedDate;
  localStats.save(bookCachePath);
  globalStats.save();
  refreshAllDevicesStats();
  readingDatesLoaded = false;
  invalidateDerivedStatsCaches();
  didChangeStats = false;
}

void BookStatsActivity::cycleEditField() { selectedEditField = (selectedEditField + 1) % 6; }

ReadingStatsDate BookStatsActivity::defaultDateForField(const bool finishedField) const {
  if (finishedField && stats.finishedDate.isValid()) {
    return stats.finishedDate;
  }
  if (!finishedField && stats.startDate.isValid()) {
    return stats.startDate;
  }
  if (finishedField && stats.startDate.isValid()) {
    return stats.startDate;
  }
  if (!finishedField && stats.finishedDate.isValid()) {
    return stats.finishedDate;
  }

  ReadingStatsDateTime now;
  if (getCurrentLocalReadingStatsDateTime(now)) {
    return now.date;
  }
  return {2000, 1, 1};
}

void BookStatsActivity::applyCompletedState(const bool completed) {
  if (stats.isCompleted == completed) {
    return;
  }

  const ReadingStatsDate previousFinishedDate = stats.finishedDate;
  stats.isCompleted = completed;
  if (completed) {
    globalStats.completedBooks++;
    if (!stats.finishedDateManual && !stats.finishedDate.isValid()) {
      ReadingStatsDateTime now;
      if (getCurrentLocalReadingStatsDateTime(now)) {
        stats.finishedDate = now.date;
      }
    }
    ReadingJournal::adjustCompletion(stats.finishedDate, 1);
  } else if (globalStats.completedBooks > 0) {
    globalStats.completedBooks--;
    ReadingJournal::adjustCompletion(previousFinishedDate, -1);
  } else {
    ReadingJournal::adjustCompletion(previousFinishedDate, -1);
  }
  readingJournal = ReadingJournal::loadAggregated();
}

void BookStatsActivity::normalizeEditedDates(const bool editedFinishedField) {
  if (!stats.startDate.isValid() || !stats.finishedDate.isValid()) {
    return;
  }
  if (compareReadingStatsDate(stats.finishedDate, stats.startDate) >= 0) {
    return;
  }

  if (editedFinishedField) {
    stats.startDate = stats.finishedDate;
  } else {
    stats.finishedDate = stats.startDate;
  }
}

void BookStatsActivity::clearEditedDate(const bool finishedField) {
  ReadingStatsDate& date = finishedField ? stats.finishedDate : stats.startDate;

  if (finishedField) {
    applyCompletedState(false);
    date.clear();
    stats.finishedDateManual = false;
  } else {
    date.clear();
    stats.startDateManual = false;
  }

  didChangeStats = true;
  setResult(ReadingStatsResult{true});
  requestUpdate();
}

bool BookStatsActivity::shouldClearDateOnAdjust(const ReadingStatsDate& date, const bool finishedField,
                                                const int fieldIndex, const int delta) const {
  if (!date.isValid()) {
    return false;
  }

  switch (fieldIndex) {
    case 0:
      return (date.month == 1 && delta < 0) || (date.month == 12 && delta > 0);
    case 1: {
      const uint8_t monthDays = daysInMonth(date.year, date.month);
      return (date.day == 1 && delta < 0) || (date.day == monthDays && delta > 0);
    }
    case 2:
      return (date.year == 2000 && delta < 0) || (date.year == 2099 && delta > 0);
    default:
      return false;
  }
}

void BookStatsActivity::adjustSelectedDateField(const int delta) {
  const bool finishedField = selectedEditField >= 3;
  const bool wasCompleted = stats.isCompleted;
  const ReadingStatsDate previousFinishedDate = stats.finishedDate;
  ReadingStatsDate& date = finishedField ? stats.finishedDate : stats.startDate;
  const int fieldIndex = selectedEditField % 3;

  if (shouldClearDateOnAdjust(date, finishedField, fieldIndex, delta)) {
    clearEditedDate(finishedField);
    return;
  }

  if (!date.isValid()) {
    date = defaultDateForField(finishedField);
  }

  switch (fieldIndex) {
    case 0: {
      int month = static_cast<int>(date.month) + delta;
      while (month < 1) {
        month += 12;
      }
      while (month > 12) {
        month -= 12;
      }
      date.month = static_cast<uint8_t>(month);
      break;
    }
    case 1: {
      const int monthDays = daysInMonth(date.year, date.month);
      int day = static_cast<int>(date.day) + delta;
      while (day < 1) {
        day += monthDays;
      }
      while (day > monthDays) {
        day -= monthDays;
      }
      date.day = static_cast<uint8_t>(day);
      break;
    }
    case 2: {
      int year = static_cast<int>(date.year) + delta;
      if (year < 2000) {
        year = 2099;
      } else if (year > 2099) {
        year = 2000;
      }
      date.year = static_cast<uint16_t>(year);
      break;
    }
  }

  const uint8_t monthDays = daysInMonth(date.year, date.month);
  if (date.day > monthDays) {
    date.day = monthDays;
  }

  if (finishedField) {
    stats.finishedDateManual = true;
  } else {
    stats.startDateManual = true;
  }
  normalizeEditedDates(finishedField);
  if (finishedField && !wasCompleted) {
    applyCompletedState(true);
  } else if (wasCompleted && previousFinishedDate.isValid() && stats.finishedDate.isValid() &&
             compareReadingStatsDate(previousFinishedDate, stats.finishedDate) != 0) {
    ReadingJournal::adjustCompletion(previousFinishedDate, -1);
    ReadingJournal::adjustCompletion(stats.finishedDate, 1);
    readingJournal = ReadingJournal::loadAggregated();
  }

  didChangeStats = true;
  setResult(ReadingStatsResult{true});
  requestUpdate();
}

void BookStatsActivity::onEnter() {
  Activity::onEnter();
  if (hasEditableBook()) {
    localStats = BookReadingStats::load(bookCachePath);
    const bool hasLiveSession =
        sessionSnapshot.hasStartedAt || sessionSnapshot.readingSeconds > 0 || sessionSnapshot.screenPages > 0;
    if (!hasLiveSession) stats = localStats;
    LibraryInsights::mergeSyncedBookStats(bookCachePath, stats);
  }
  readingJournal = ReadingJournal::loadAggregated();
  requestUpdate();
}

void BookStatsActivity::onExit() {
  saveStats();
  Activity::onExit();
}

void BookStatsActivity::ensureLibraryInsights() {
  if (!libraryInsights) {
    libraryInsights = LibraryInsights::load();
  }
  if (showAllDevicesStats && libraryInsights && libraryInsights->available) {
    allDevicesStats.completedBooks = libraryInsights->finishedBooks;
  }
}

void BookStatsActivity::scrollRecentSessions(const int delta) {
  const uint8_t count = readingJournal ? readingJournal->recentSessionCount() : 0;
  const int maxOffset = count > BOOK_STATS_VISIBLE_SESSION_ROWS ? count - BOOK_STATS_VISIBLE_SESSION_ROWS : 0;
  const uint8_t next = static_cast<uint8_t>(std::clamp(static_cast<int>(recentSessionOffset) + delta, 0, maxOffset));
  if (next == recentSessionOffset) return;
  recentSessionOffset = next;
  requestUpdate();
}

void BookStatsActivity::ensureStartedBooks() {
  if (startedBooksLoaded || startedBooksLoadPending) return;
  startedBooks = loadStartedBookStatsEntries();
  startedBookSelected = 0;
  startedBookScanIndex = 0;
  startedBooksLoadPending = false;
  startedBooksLoaded = true;
}

void BookStatsActivity::processStartedBooksLoadStep() {
  if (!startedBooksLoaded) ensureStartedBooks();
}

void BookStatsActivity::moveStartedBookSelection(const int delta) {
  ensureStartedBooks();
  if (startedBooks.empty() || delta == 0) return;
  const size_t next = static_cast<size_t>(std::clamp<int64_t>(static_cast<int64_t>(startedBookSelected) + delta, 0,
                                                              static_cast<int64_t>(startedBooks.size() - 1)));
  if (next == startedBookSelected) return;
  startedBookSelected = next;
  requestUpdate();
}

void BookStatsActivity::openSelectedStartedBook() {
  ensureStartedBooks();
  if (startedBookSelected >= startedBooks.size()) return;
  const StartedBookStatsEntry book = startedBooks[startedBookSelected];
  if (book.path.rfind(DUET_BOOKS_ROOT_PATH, 0) == 0 || book.path.rfind(DUET_LEGACY_BOOKS_ROOT_PATH, 0) == 0) {
    saveStats();
    bookTitle = book.title;
    bookCachePath = book.path;
    localStats = BookReadingStats::load(bookCachePath);
    stats = localStats;
    LibraryInsights::mergeSyncedBookStats(bookCachePath, stats);
    progressPercent = -1.0f;
    hasEstimatedTimeLeft = stats.hasReliableTimeLeftBasis() && stats.estimatedTimeLeftSeconds > 0;
    estimatedTimeLeftSeconds = hasEstimatedTimeLeft ? stats.estimatedTimeLeftSeconds : 0;
    page = Page::CurrentBook;
    requestUpdate();
    return;
  }
  startActivityForResult(
      std::make_unique<BookInfoActivity>(renderer, mappedInput, book.path, book.title, book.coverBmpPath),
      [this](const ActivityResult&) {
        startedBooksLoaded = false;
        startedBooksLoadPending = false;
        ensureStartedBooks();
        requestUpdate();
      });
}

void BookStatsActivity::scrollSeries(const int delta) {
  ensureLibraryInsights();
  const size_t count = libraryInsights ? libraryInsights->seriesProgressCount : 0;
  const int maxOffset =
      count > BOOK_STATS_VISIBLE_SERIES_ROWS ? static_cast<int>(count - BOOK_STATS_VISIBLE_SERIES_ROWS) : 0;
  const size_t next = static_cast<size_t>(std::clamp(static_cast<int>(seriesOffset) + delta, 0, maxOffset));
  if (next == seriesOffset) return;
  seriesOffset = next;
  requestUpdate();
}

void BookStatsActivity::ensureReadingDates() {
  if (readingDatesLoaded) return;
  readingDates = loadReadingDateStatsEntries();
  readingDatesSelected = 0;
  readingDatesLoaded = true;
}

void BookStatsActivity::ensureFastestReads() {
  if (fastestReadsLoaded) return;
  fastestReads = loadFastestReadStatsEntries();
  fastestReadsLoaded = true;
}

void BookStatsActivity::ensureStartFinishSummary() {
  if (startFinishSummaryLoaded) return;
  startFinishSummary = loadStartFinishStatsSummary();
  startFinishSummaryLoaded = true;
}

void BookStatsActivity::ensureDeviceSplitSummary() {
  if (deviceSplitSummary.loaded) return;
  deviceSplitSummary = loadDeviceSplitStatsSummary();
}

void BookStatsActivity::invalidateDerivedStatsCaches() {
  fastestReadsLoaded = false;
  fastestReads.clear();
  startFinishSummaryLoaded = false;
  startFinishSummary = StartFinishStatsSummary{};
  deviceSplitSummary = DeviceSplitStatsSummary{};
}

bool BookStatsActivity::processDerivedStatsLoadStep() {
  if (page == Page::FastestReads && !fastestReadsLoaded) {
    ensureFastestReads();
    requestUpdate();
    return true;
  }
  if (page == Page::StartedFinished && !startFinishSummaryLoaded) {
    ensureStartFinishSummary();
    requestUpdate();
    return true;
  }
  if (page == Page::DeviceSplit && !deviceSplitSummary.loaded) {
    ensureDeviceSplitSummary();
    requestUpdate();
    return true;
  }
  return false;
}

void BookStatsActivity::moveReadingDatesSelection(const int delta) {
  ensureReadingDates();
  if (readingDates.empty() || delta == 0) return;
  const size_t next = static_cast<size_t>(std::clamp<int64_t>(static_cast<int64_t>(readingDatesSelected) + delta, 0,
                                                              static_cast<int64_t>(readingDates.size() - 1)));
  if (next == readingDatesSelected) return;
  readingDatesSelected = next;
  requestUpdate();
}

void BookStatsActivity::openSelectedReadingDatesBook() {
  ensureReadingDates();
  if (readingDatesSelected >= readingDates.size()) return;
  const ReadingDateStatsEntry& selected = readingDates[readingDatesSelected];
  if (selected.cachePath.empty() || !Storage.existsForRead(selected.cachePath)) return;

  saveStats();
  bookTitle = selected.title;
  bookCachePath = selected.cachePath;
  localStats = BookReadingStats::load(bookCachePath);
  stats = localStats;
  LibraryInsights::mergeSyncedBookStats(bookCachePath, stats);
  progressPercent = -1.0f;
  hasEstimatedTimeLeft = stats.hasReliableTimeLeftBasis() && stats.estimatedTimeLeftSeconds > 0;
  estimatedTimeLeftSeconds = hasEstimatedTimeLeft ? stats.estimatedTimeLeftSeconds : 0;
  selectedEditField = 0;
  datesEditMode = false;
  page = Page::EditDates;
  requestUpdate();
}

void BookStatsActivity::openDayDetails(const ReadingStatsDate* date) {
  ReadingStatsDateTime now;
  if (!getCurrentLocalReadingStatsDateTime(now)) return;
  dayDetailsDate = date && date->isValid() ? *date : now.date;
  dayDetailsSelectedBook = 0;
  dayCorrectionEditMode = false;
  pendingDayCorrectionSeconds = 0;
  dayDetailsOpen = true;
  refreshDayDetails();
  requestUpdate();
}

void BookStatsActivity::refreshDayDetails() {
  dayDetailsSummary = ReadingLedgerDaySummary{};
  if (!dayDetailsDate.isValid()) return;
  const uint32_t dayIndex = readingStatsDayIndex(dayDetailsDate);
  const uint32_t committedSeconds = readingJournal ? readingJournal->secondsOnDay(dayIndex) : 0;
  if (!ReadingLedger::summarizeDay(dayIndex, committedSeconds, dayDetailsSummary)) {
    dayDetailsSummary.dayIndex = dayIndex;
    dayDetailsSummary.legacyUnattributedSeconds = committedSeconds;
  }

  ReadingStatsDateTime now;
  const bool isToday = getCurrentLocalReadingStatsDateTime(now) && sameDate(dayDetailsDate, now.date);
  if (isToday && sessionSnapshot.readingSeconds > 0 && !bookCachePath.empty()) {
    uint8_t match = dayDetailsSummary.bookCount;
    for (uint8_t i = 0; i < dayDetailsSummary.bookCount; ++i) {
      if (strncmp(dayDetailsSummary.books[i].cachePath, bookCachePath.c_str(),
                  sizeof(dayDetailsSummary.books[i].cachePath)) == 0) {
        match = i;
        break;
      }
    }
    if (match == dayDetailsSummary.bookCount && match < dayDetailsSummary.books.size()) {
      ReadingLedgerDayBook& book = dayDetailsSummary.books[match];
      snprintf(book.cachePath, sizeof(book.cachePath), "%s", bookCachePath.c_str());
      snprintf(book.title, sizeof(book.title), "%s", bookTitle.c_str());
      dayDetailsSummary.bookCount++;
    }
    if (match < dayDetailsSummary.bookCount) {
      ReadingLedgerDayBook& book = dayDetailsSummary.books[match];
      book.readingSeconds = addSaturated(book.readingSeconds, sessionSnapshot.readingSeconds);
      book.screenPages = addSaturated(book.screenPages, sessionSnapshot.screenPages);
      dayDetailsSummary.attributedSeconds =
          addSaturated(dayDetailsSummary.attributedSeconds, sessionSnapshot.readingSeconds);
    }
  }

  if (dayDetailsSummary.bookCount == 0) {
    dayDetailsSelectedBook = 0;
  } else if (dayDetailsSelectedBook >= dayDetailsSummary.bookCount) {
    dayDetailsSelectedBook = static_cast<uint8_t>(dayDetailsSummary.bookCount - 1);
  }
}

void BookStatsActivity::changeDayDetailsDate(const int delta) {
  if (!dayDetailsDate.isValid() || delta == 0) return;
  ReadingStatsDate candidate = dayDetailsDate;
  addDaysToReadingStatsDate(candidate, delta);
  ReadingStatsDateTime now;
  if (getCurrentLocalReadingStatsDateTime(now) && compareReadingStatsDate(candidate, now.date) > 0) return;
  dayDetailsDate = candidate;
  dayDetailsSelectedBook = 0;
  pendingDayCorrectionSeconds = 0;
  refreshDayDetails();
  requestUpdate();
}

void BookStatsActivity::moveDayDetailsSelection(const int delta) {
  if (dayDetailsSummary.bookCount == 0 || delta == 0) return;
  const int next = std::clamp(static_cast<int>(dayDetailsSelectedBook) + delta, 0,
                              static_cast<int>(dayDetailsSummary.bookCount) - 1);
  if (next == dayDetailsSelectedBook) return;
  dayDetailsSelectedBook = static_cast<uint8_t>(next);
  pendingDayCorrectionSeconds = 0;
  requestUpdate();
}

bool BookStatsActivity::canAdjustSelectedDayBook() const {
  if (dayDetailsSelectedBook >= dayDetailsSummary.bookCount) return false;
  const ReadingLedgerDayBook& book = dayDetailsSummary.books[dayDetailsSelectedBook];
  if (book.cachePath[0] == '\0' || !Storage.exists(book.cachePath)) return false;

  ReadingStatsDateTime now;
  const bool isLiveCurrentBook = sessionSnapshot.readingSeconds > 0 && getCurrentLocalReadingStatsDateTime(now) &&
                                 sameDate(dayDetailsDate, now.date) &&
                                 DuetStorage::sameBookCacheIdentity(bookCachePath, book.cachePath);
  return !isLiveCurrentBook;
}

void BookStatsActivity::adjustPendingDayCorrection(const int32_t deltaSeconds) {
  if (!canAdjustSelectedDayBook() || deltaSeconds == 0) return;
  const uint32_t current = dayDetailsSummary.books[dayDetailsSelectedBook].readingSeconds;
  const int64_t requested = static_cast<int64_t>(pendingDayCorrectionSeconds) + deltaSeconds;
  const int64_t minimum = -static_cast<int64_t>(current);
  constexpr int64_t maximum = 24 * 60 * 60;
  pendingDayCorrectionSeconds = static_cast<int32_t>(std::clamp<int64_t>(requested, minimum, maximum));
  requestUpdate();
}

bool BookStatsActivity::saveDayCorrection() {
  if (!canAdjustSelectedDayBook() || pendingDayCorrectionSeconds == 0) return true;
  const ReadingLedgerDayBook selected = dayDetailsSummary.books[dayDetailsSelectedBook];
  const std::string cachePath(selected.cachePath);
  const std::string title(selected.title);
  const BookReadingStats originalBookStats = BookReadingStats::load(cachePath);
  const GlobalReadingStats originalGlobalStats = globalStats;

  int32_t requested = pendingDayCorrectionSeconds;
  if (requested < 0) {
    const uint32_t removable = std::min({selected.readingSeconds, originalBookStats.totalReadingSeconds,
                                         originalGlobalStats.totalReadingSeconds, static_cast<uint32_t>(INT32_MAX)});
    requested = std::max<int32_t>(requested, -static_cast<int32_t>(removable));
  }

  BookReadingStats revisedBookStats = originalBookStats;
  const int32_t applied = revisedBookStats.adjustReadingTime(dayDetailsDate, requested);
  if (applied == 0) return true;
  GlobalReadingStats revisedGlobalStats = originalGlobalStats;
  if (revisedGlobalStats.adjustReadingTime(dayDetailsDate, applied) != applied) return false;

  if (!ReadingJournal::adjustReadingTime(dayDetailsDate, applied)) return false;
  std::unique_ptr<ReadingJournal> revisedJournal = ReadingJournal::load();
  if (!revisedJournal) {
    ReadingJournal::adjustReadingTime(dayDetailsDate, -applied);
    return false;
  }
  if (revisedJournal->secondsOnDay(readingStatsDayIndex(dayDetailsDate)) == 0) {
    clearReadingHistoryDay(revisedGlobalStats.readingHistoryAnchorDay, revisedGlobalStats.readingHistoryBits,
                           readingStatsDayIndex(dayDetailsDate));
    revisedGlobalStats.longestReadingStreak = computeReadingHistoryLongestStreak(
        revisedGlobalStats.readingHistoryAnchorDay, revisedGlobalStats.readingHistoryBits);
  }

  revisedBookStats.save(cachePath);
  if (BookReadingStats::load(cachePath).totalReadingSeconds != revisedBookStats.totalReadingSeconds) {
    ReadingJournal::adjustReadingTime(dayDetailsDate, -applied);
    originalBookStats.save(cachePath);
    return false;
  }
  revisedGlobalStats.save();
  if (GlobalReadingStats::load().totalReadingSeconds != revisedGlobalStats.totalReadingSeconds) {
    ReadingJournal::adjustReadingTime(dayDetailsDate, -applied);
    originalBookStats.save(cachePath);
    originalGlobalStats.save();
    return false;
  }
  if (!ReadingLedger::recordCorrection(dayDetailsDate, applied, cachePath, title)) {
    ReadingJournal::adjustReadingTime(dayDetailsDate, -applied);
    originalBookStats.save(cachePath);
    originalGlobalStats.save();
    return false;
  }

  globalStats = revisedGlobalStats;
  if (cachePath == bookCachePath) {
    localStats = revisedBookStats;
    stats = revisedBookStats;
    LibraryInsights::mergeSyncedBookStats(bookCachePath, stats);
  }
  readingJournal = ReadingJournal::loadAggregated();
  pendingDayCorrectionSeconds = 0;
  refreshDayDetails();
  refreshAllDevicesStats();
  return true;
}

void BookStatsActivity::exitStatsActivity(const bool viaBack) {
  if (viaBack) {
    mappedInput.suppressNextBackRelease();
  } else {
    mappedInput.suppressNextConfirmRelease();
  }

  if (returnToHomeOnExit) {
    onGoHome();
    return;
  }

  finish();
}

size_t BookStatsActivity::buildVisiblePages(std::array<Page, 35>& pages) const {
  const std::array<Page, 33> allPages = {
      Page::CurrentBook,
      Page::BookProgress,
      Page::BookPatterns,
      Page::ThisDevice,
      Page::AllDevices,
      Page::DeviceSplit,
      Page::Trends,
      Page::ActivityChart,
      Page::DailyMinutes,
      Page::MonthlyCalendar,
      Page::Heatmap,
      Page::ReadingProfile,
      Page::Goals,
      Page::RecentSessions,
      Page::WeekdayPattern,
      Page::PaceTrend,
      Page::TimeOfDay,
      Page::MonthlyTrend,
      Page::YearLine,
      Page::SessionLengths,
      Page::StreakMilestones,
      Page::StartedFinished,
      Page::ReadingDates,
      Page::ReaderRadar,
      Page::ReaderDnaDetails,
      Page::ReaderSignature,
      Page::ReaderSignatureDetails,
      Page::FastestReads,
      Page::Wrapped,
      Page::StartedBooks,
      Page::LibraryOverview,
      Page::ReadingTaste,
      Page::SeriesProgress,
  };
  size_t count = 0;
  for (const Page candidate : allPages) {
    if (candidate == Page::AllDevices && !showAllDevicesStats) continue;
    if (candidate == Page::DeviceSplit && !showAllDevicesStats) continue;
    if (candidate == Page::BookPatterns && !hasEditableBook()) continue;
    pages[count++] = candidate;
  }
  return count;
}

const char* BookStatsActivity::shortPageLabel(const Page page) {
  switch (page) {
    case Page::CurrentBook:
      return "Current";
    case Page::BookProgress:
      return "Progress";
    case Page::ReadingPatterns:
      return "Patterns";
    case Page::BookPatterns:
      return "Book";
    case Page::Trends:
      return "Trends";
    case Page::ActivityChart:
      return "Activity";
    case Page::DailyMinutes:
      return "90 Days";
    case Page::MonthlyCalendar:
      return "Calendar";
    case Page::Heatmap:
      return "Heatmap";
    case Page::ReadingProfile:
      return "Profile";
    case Page::Goals:
      return "Goals";
    case Page::RecentSessions:
      return "Sessions";
    case Page::WeekdayPattern:
      return "Weekdays";
    case Page::PaceTrend:
      return "Pace";
    case Page::TimeOfDay:
      return "Time of Day";
    case Page::MonthlyTrend:
      return "Months";
    case Page::DeviceSplit:
      return "Devices";
    case Page::YearLine:
      return "Year";
    case Page::SessionLengths:
      return "Sessions Mix";
    case Page::StreakMilestones:
      return "Streaks";
    case Page::StartedFinished:
      return "Start/Finish";
    case Page::ReadingDates:
      return "Dates";
    case Page::ReaderRadar:
      return "Reader DNA";
    case Page::ReaderDnaDetails:
      return "DNA Details";
    case Page::ReaderSignature:
      return "Signature";
    case Page::ReaderSignatureDetails:
      return "Sig Details";
    case Page::FastestReads:
      return "Fastest";
    case Page::Wrapped:
      return "Wrapped";
    case Page::StartedBooks:
      return "Started";
    case Page::LibraryOverview:
      return "Library";
    case Page::ReadingTaste:
      return "Taste";
    case Page::SeriesProgress:
      return "Series";
    case Page::ThisDevice:
      return "Device";
    case Page::AllDevices:
      return "Synced";
    case Page::EditDates:
      return "Book Dates";
  }
  return "";
}

void BookStatsActivity::cyclePage(const int delta) {
  std::array<Page, 35> pages{};
  const size_t count = buildVisiblePages(pages);
  if (count == 0) return;
  size_t selected = 0;
  while (selected < count && pages[selected] != page) selected++;
  if (selected >= count) selected = 0;
  const int next = (static_cast<int>(selected) + delta + static_cast<int>(count)) % static_cast<int>(count);
  page = pages[static_cast<size_t>(next)];
  datesEditMode = false;
  if (page == Page::ReaderRadar || page == Page::ReaderDnaDetails || page == Page::LibraryOverview ||
      page == Page::ReadingTaste || page == Page::SeriesProgress) {
    ensureLibraryInsights();
  }
  if (page == Page::StartedBooks) ensureStartedBooks();
  if (page == Page::ReadingDates) ensureReadingDates();
  requestUpdate();
}

void BookStatsActivity::drawPageTabs() const {
  std::array<Page, 35> pages{};
  const size_t count = buildVisiblePages(pages);
  if (count == 0) return;
  size_t selected = 0;
  while (selected < count && pages[selected] != page) selected++;
  if (selected >= count) selected = 0;
  // Must match the pages array capacity: writing labels for every visible
  // page into a smaller array smashed the stack when the pager grew.
  std::array<const char*, 34> labels{};
  for (size_t i = 0; i < count && i < labels.size(); ++i) labels[i] = shortPageLabel(pages[i]);
  renderStatsTabBar(renderer, labels.data(), count, selected);
}

void BookStatsActivity::loop() {
  if (usesNoRtcSingleScreenLayout()) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      exitStatsActivity(true);
      return;
    }
    return;
  }

  if (dayDetailsOpen) {
    if (dayCorrectionEditMode) {
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        dayCorrectionEditMode = false;
        pendingDayCorrectionSeconds = 0;
        requestUpdate();
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        if (saveDayCorrection()) {
          dayCorrectionEditMode = false;
          pendingDayCorrectionSeconds = 0;
        }
        requestUpdate();
        return;
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
        adjustPendingDayCorrection(-60);
        return;
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
        adjustPendingDayCorrection(60);
        return;
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
        adjustPendingDayCorrection(5 * 60);
        return;
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
        adjustPendingDayCorrection(-5 * 60);
        return;
      }
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      dayDetailsOpen = false;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (canAdjustSelectedDayBook()) {
        dayCorrectionEditMode = true;
        pendingDayCorrectionSeconds = 0;
        requestUpdate();
      }
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      changeDayDetailsDate(-1);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      changeDayDetailsDate(1);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      moveDayDetailsSelection(-1);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      moveDayDetailsSelection(1);
      return;
    }
    return;
  }

  if (page == Page::EditDates) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      if (datesEditMode) {
        saveStats();
        datesEditMode = false;
        requestUpdate();
        return;
      }
      exitStatsActivity(true);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (!datesEditMode) {
        if (mappedInput.getHeldTime() >= DATE_EDIT_UNLOCK_MS) {
          datesEditMode = true;
          requestUpdate();
        }
        return;
      }
      cycleEditField();
      requestUpdate();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (datesEditMode) {
        saveStats();
        datesEditMode = false;
      }
      cyclePage(-1);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (datesEditMode) {
        saveStats();
        datesEditMode = false;
      }
      cyclePage(1);
      return;
    }
    if (!datesEditMode) return;
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      adjustSelectedDateField(-1);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      adjustSelectedDateField(1);
      return;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    exitStatsActivity(true);
    return;
  }

  if (page == Page::MonthlyCalendar && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openDayDetails();
    return;
  }
  if (page == Page::DailyMinutes && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    ReadingStatsDateTime now;
    if (getCurrentLocalReadingStatsDateTime(now)) {
      const uint32_t today = readingStatsDayIndex(now.date);
      ReadingStatsDate selectedDate;
      if (readingStatsDateFromDayIndex(today >= dailyMinutesOffset ? today - dailyMinutesOffset : 0, selectedDate)) {
        openDayDetails(&selectedDate);
      }
    }
    return;
  }
  if (page == Page::StartedBooks && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelectedStartedBook();
    return;
  }
  if (page == Page::ReadingDates && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelectedReadingDatesBook();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    cyclePage(-1);
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    cyclePage(1);
    return;
  }

  if (page == Page::RecentSessions) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) scrollRecentSessions(-1);
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) scrollRecentSessions(1);
    return;
  }
  if (page == Page::DailyMinutes) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Up) && dailyMinutesOffset > 0) {
      dailyMinutesOffset--;
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down) && dailyMinutesOffset < 89) {
      dailyMinutesOffset++;
      requestUpdate();
    }
    return;
  }
  if (page == Page::StartedBooks) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) moveStartedBookSelection(-1);
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) moveStartedBookSelection(1);
    processStartedBooksLoadStep();
    return;
  }
  if (page == Page::SeriesProgress) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) scrollSeries(-1);
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) scrollSeries(1);
    return;
  }
  if (page == Page::ReadingDates) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) moveReadingDatesSelection(-1);
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) moveReadingDatesSelection(1);
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    cyclePage(-1);
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    cyclePage(1);
    return;
  }
  processDerivedStatsLoadStep();
}

void BookStatsActivity::render(RenderLock&&) {
  if (usesNoRtcSingleScreenLayout()) {
    renderNoRtcCombinedStatsPage(renderer, &mappedInput, bookTitle, stats, progressPercent, hasEstimatedTimeLeft,
                                 estimatedTimeLeftSeconds, globalStats,
                                 showAllDevicesStats ? &allDevicesStats : nullptr, true, bookWordCount);
    renderer.displayBuffer();
    return;
  }

  if (dayDetailsOpen) {
    renderReadingDayDetailsPage(renderer, &mappedInput, dayDetailsDate, dayDetailsSummary, dayDetailsSelectedBook,
                                dayCorrectionEditMode, pendingDayCorrectionSeconds, canAdjustSelectedDayBook(), true);
    renderer.displayBuffer();
    return;
  }

  // Journal-backed pages show the merged (all-devices) journal, so the day
  // bitmap and session totals must be merged too — mixing the merged journal
  // with device-local history made Days Read and streaks disagree per device.
  const GlobalReadingStats& historyStats = showAllDevicesStats ? allDevicesStats : globalStats;
  switch (page) {
    case Page::CurrentBook:
      renderCurrentBookStatsPage(renderer, &mappedInput, bookTitle, stats, sessionSnapshot, progressPercent,
                                 hasEstimatedTimeLeft, estimatedTimeLeftSeconds, true, hasEditableBook(), true,
                                 bookWordCount);
      break;
    case Page::BookProgress:
      renderBookProgressGraphPage(renderer, &mappedInput, bookTitle, stats, progressPercent, hasEstimatedTimeLeft,
                                  estimatedTimeLeftSeconds, true, true, bookWordCount);
      break;
    case Page::ReadingPatterns:
      renderGlobalStatsPage(renderer, &mappedInput, tr(STR_STATS_READING_PATTERNS), globalStats, true, true);
      break;
    case Page::BookPatterns:
      renderPerBookStatsPage(renderer, &mappedInput, bookTitle, stats, progressPercent, hasEstimatedTimeLeft,
                             estimatedTimeLeftSeconds, true, false, true, bookWordCount);
      break;
    case Page::Trends:
      renderReadingTrendsPage(renderer, &mappedInput, readingJournal.get(), historyStats, sessionSnapshot, true, true);
      break;
    case Page::ActivityChart:
      renderReadingActivityChartPage(renderer, &mappedInput, readingJournal.get(), historyStats, sessionSnapshot,
                                     SETTINGS.readingGoalMinutes, true, true);
      break;
    case Page::DailyMinutes:
      renderReadingDailyMinutesPage(renderer, &mappedInput, readingJournal.get(), historyStats, sessionSnapshot,
                                    dailyMinutesOffset, true, true);
      break;
    case Page::MonthlyCalendar:
      renderMonthlyReadingCalendarPage(renderer, &mappedInput, readingJournal.get(), historyStats, sessionSnapshot,
                                       SETTINGS.readingGoalMinutes, true, true);
      break;
    case Page::Heatmap:
      renderReadingHeatmapPage(renderer, &mappedInput, readingJournal.get(), historyStats, sessionSnapshot,
                               SETTINGS.readingGoalMinutes, true, true);
      break;
    case Page::ReadingProfile:
      renderReadingProfilePage(renderer, &mappedInput, readingJournal.get(), historyStats, sessionSnapshot,
                               SETTINGS.readingGoalMinutes, true, true);
      break;
    case Page::Goals:
      renderReadingGoalsPage(renderer, &mappedInput, readingJournal.get(), historyStats, sessionSnapshot,
                             SETTINGS.readingGoalMinutes, true, true);
      break;
    case Page::RecentSessions:
      renderRecentReadingSessionsPage(renderer, &mappedInput, readingJournal.get(), historyStats.totalSessions,
                                      recentSessionOffset, true, true);
      break;
    case Page::WeekdayPattern:
      renderWeekdayPatternPage(renderer, &mappedInput, readingJournal.get(), historyStats, sessionSnapshot, true, true);
      break;
    case Page::PaceTrend:
      renderPaceTrendPage(renderer, &mappedInput, readingJournal.get(), sessionSnapshot, true, true);
      break;
    case Page::TimeOfDay:
      renderTimeOfDayPage(renderer, &mappedInput, historyStats, true, true);
      break;
    case Page::MonthlyTrend:
      renderMonthlyTrendPage(renderer, &mappedInput, readingJournal.get(), sessionSnapshot, true, true);
      break;
    case Page::DeviceSplit:
      renderDeviceSplitPage(renderer, &mappedInput, globalStats, deviceSplitSummary, true, true);
      break;
    case Page::YearLine:
      renderYearLinePage(renderer, &mappedInput, readingJournal.get(), sessionSnapshot, true, true);
      break;
    case Page::SessionLengths:
      renderSessionLengthsPage(renderer, &mappedInput, readingJournal.get(), sessionSnapshot, true, true);
      break;
    case Page::StreakMilestones:
      renderStreakMilestonesPage(renderer, &mappedInput, readingJournal.get(), historyStats, sessionSnapshot, true,
                                 true);
      break;
    case Page::StartedFinished:
      renderStartedFinishedPage(renderer, &mappedInput, startFinishSummary, true, true);
      break;
    case Page::ReadingDates:
      renderReadingDatesPage(renderer, &mappedInput, readingDates, readingDatesSelected, !readingDatesLoaded, true,
                             true);
      break;
    case Page::ReaderRadar:
      renderReaderRadarPage(renderer, &mappedInput, readingJournal.get(), historyStats, sessionSnapshot,
                            libraryInsights.get(), SETTINGS.readingGoalMinutes, true, true);
      break;
    case Page::ReaderDnaDetails:
      renderReaderDnaDetailsPage(renderer, &mappedInput, readingJournal.get(), historyStats, sessionSnapshot, true,
                                 true);
      break;
    case Page::ReaderSignature:
      renderReadingSignaturePage(renderer, &mappedInput, readingJournal.get(), historyStats, sessionSnapshot,
                                 SETTINGS.readingGoalMinutes, true, true);
      break;
    case Page::ReaderSignatureDetails:
      renderReadingSignatureDetailsPage(renderer, &mappedInput, readingJournal.get(), historyStats, sessionSnapshot,
                                        SETTINGS.readingGoalMinutes, true, true);
      break;
    case Page::FastestReads:
      renderFastestReadsPage(renderer, &mappedInput, fastestReads, !fastestReadsLoaded, true, true);
      break;
    case Page::Wrapped:
      renderReadingWrappedPage(renderer, &mappedInput, readingJournal.get(), historyStats, sessionSnapshot, true, true);
      break;
    case Page::StartedBooks:
      renderStartedBooksPage(renderer, &mappedInput, startedBooks, startedBookSelected, startedBooksLoadPending, true,
                             true);
      break;
    case Page::LibraryOverview:
      renderLibraryInsightsPage(renderer, &mappedInput, libraryInsights.get(), true, true);
      break;
    case Page::ReadingTaste:
      renderReadingTastePage(renderer, &mappedInput, libraryInsights.get(), true, true);
      break;
    case Page::SeriesProgress:
      renderSeriesProgressPage(renderer, &mappedInput, libraryInsights.get(), seriesOffset, true, true);
      break;
    case Page::ThisDevice:
      renderGlobalStatsPage(renderer, &mappedInput, tr(STR_STATS_THIS_DEVICE_SCREEN), globalStats, true,
                            showAllDevicesStats);
      break;
    case Page::AllDevices:
      renderGlobalStatsPage(renderer, &mappedInput, tr(STR_STATS_ALL_DEVICES_SCREEN), allDevicesStats, true, false);
      break;
    case Page::EditDates:
      renderEditBookDatesPage(renderer, &mappedInput, bookTitle, stats, selectedEditField, datesEditMode, true);
      break;
  }
  drawPageTabs();
  renderer.displayBuffer();
}
