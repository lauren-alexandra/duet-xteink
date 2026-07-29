#ifdef SIMULATOR

#include "SimulatorSmokeTest.h"

#include <Epub.h>
#include <HalStorage.h>
#include <HalTiltSensor.h>
#include <Logging.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "AchievementStore.h"
#include "CrossPointSettings.h"
#include "DictionaryStore.h"
#include "FavoritesStore.h"
#include "LauncherCatalog.h"
#include "LauncherLayoutStore.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "SettingsList.h"
#include "activities/ActivityManager.h"
#include "activities/apps/AchievementsActivity.h"
#include "activities/apps/DictionaryActivity.h"
#include "activities/apps/FavoritesActivity.h"
#include "activities/apps/TetrisActivity.h"
#include "activities/apps/UtilitiesActivity.h"
#include "activities/home/BookInfoActivity.h"
#include "activities/home/CurrentBookStats.h"
#include "activities/home/HomeActivity.h"
#include "activities/home/LibraryBookInfo.h"
#include "activities/home/LibrarySearchActivity.h"
#include "activities/home/RecentBookProgress.h"
#include "activities/reader/BookReadingStats.h"
#include "activities/reader/BookStatsView.h"
#include "activities/reader/DictionaryDefinitionActivity.h"
#include "activities/reader/EpubReaderMenuActivity.h"
#include "activities/reader/GlobalReadingStats.h"
#include "activities/reader/LibraryInsights.h"
#include "activities/reader/ReaderOptionsActivity.h"
#include "activities/reader/ReadingJournal.h"
#include "activities/reader/ReadingLedger.h"
#include "activities/reader/ReadingStatsClock.h"
#include "activities/reader/ReadingStatsUtils.h"
#include "activities/reader/StatsBackup.h"
#include "activities/settings/FontSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/themes/reading_home/ReadingHomeTheme.h"
#include "fontIds.h"
#include "util/ScreenshotUtil.h"

extern ActivityManager activityManager;
extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;

namespace {

enum class SmokeStep : uint8_t {
  Start,
  Home,
  FileBrowser,
  FileBrowserInput,
  FileBrowserAfterInput,
  RecentBooks,
  Settings,
  ReaderOptions,
  ReaderMenu,
  Sleep,
  Reader,
  ReaderInput,
  ProgressCache,
  Done,
};

class SimulatorSmokeTest {
 public:
  void tick() {
    if (!enabled()) return;

    try {
      tickImpl();
    } catch (const std::exception& e) {
      fail("Unhandled exception: %s", e.what());
    } catch (...) {
      fail("Unhandled non-standard exception");
    }
  }

 private:
  enum class ScriptActionType : uint8_t { Press, Release, Render, Capture, Wait, SetFontFamily };

  struct ScriptAction {
    ScriptActionType type;
    MappedInputManager::Button button;
    const char* label;
    int settleFrames;
  };

  SmokeStep step = SmokeStep::Start;
  int settleFrames = 0;
  const char* activeStepName = nullptr;
  std::vector<ScriptAction> inputScript;
  size_t scriptIndex = 0;
  unsigned long scriptWaitUntil = 0UL;

  static bool enabled() { return std::getenv("CROSSINK_SIMULATOR_SMOKE_TEST") != nullptr; }

  static bool verifyReaderRelayout() {
    return std::getenv("CROSSINK_SIMULATOR_SMOKE_VERIFY_READER_RELAYOUT") != nullptr;
  }

  static bool verifyChapterTransition() {
    return std::getenv("CROSSINK_SIMULATOR_SMOKE_VERIFY_CHAPTER_TRANSITION") != nullptr;
  }

  static int pageTurnCount() {
    const char* raw = std::getenv("CROSSINK_SIMULATOR_SMOKE_PAGE_TURNS");
    if (raw == nullptr || raw[0] == '\0') {
      return 2;
    }
    return std::max(0, std::atoi(raw));
  }

  static void applyRequestedTheme() {
    const char* raw = std::getenv("CROSSINK_SIMULATOR_SMOKE_THEME");
    if (raw == nullptr || raw[0] == '\0') {
      return;
    }

    const int theme = std::atoi(raw);
    if (theme < 0 || theme >= CrossPointSettings::UI_THEME_COUNT) {
      fail("Invalid smoke test theme index: %d", theme);
    }

    SETTINGS.uiTheme = static_cast<uint8_t>(theme);
    UITheme::getInstance().reload();
    LOG_INF("SMOKE", "Using theme index %d", theme);
  }

  static void applyRequestedFileBrowserDisplay() {
    const char* raw = std::getenv("CROSSINK_SIMULATOR_SMOKE_FILE_BROWSER_DISPLAY");
    if (raw == nullptr || raw[0] == '\0') {
      return;
    }

    const int display = std::atoi(raw);
    if (display < 0 || display >= CrossPointSettings::FILE_BROWSER_DISPLAY_COUNT) {
      fail("Invalid smoke test file browser display index: %d", display);
    }
    SETTINGS.fileBrowserDisplay = static_cast<uint8_t>(display);
    LOG_INF("SMOKE", "Using file browser display index %d", display);
  }

  static void applyRequestedFileBrowserGridLayout() {
    const char* raw = std::getenv("CROSSINK_SIMULATOR_SMOKE_FILE_BROWSER_GRID_LAYOUT");
    if (raw == nullptr || raw[0] == '\0') return;
    const int layout = std::atoi(raw);
    if (layout < 0 || layout >= CrossPointSettings::FILE_BROWSER_GRID_LAYOUT_COUNT) {
      fail("Invalid smoke test file browser grid layout index: %d", layout);
    }
    SETTINGS.fileBrowserGridLayout = static_cast<uint8_t>(layout);
    LOG_INF("SMOKE", "Using file browser grid layout index %d", layout);
  }

  static void applyRequestedSleepScreen() {
    if (std::getenv("CROSSINK_SIMULATOR_SMOKE_CUSTOM_SLEEP") == nullptr) return;
    SETTINGS.sleepScreen = CrossPointSettings::CUSTOM;
    SETTINGS.sleepScreenCoverFilter = CrossPointSettings::NO_FILTER;
    LOG_INF("SMOKE", "Using custom grayscale sleep screen");
  }

  static void verifyLibraryCatalogContract() {
    if (std::getenv("CROSSINK_SIMULATOR_SMOKE_EXPECT_LIBRARY_CATALOG") == nullptr) return;

    const std::unique_ptr<LibraryInsights> parsedInsights = LibraryInsights::load();
    if (!parsedInsights || !parsedInsights->available || parsedInsights->totalBooks == 0) {
      fail("Library catalog fixture did not load");
    }
    const std::unique_ptr<LibraryInsights> cachedInsights = LibraryInsights::load();
    if (!cachedInsights || !cachedInsights->available || cachedInsights->totalBooks != parsedInsights->totalBooks) {
      fail("Library insights cache did not reload cleanly");
    }
    LibraryInsights::invalidateCache();
    const std::unique_ptr<LibraryInsights> indexedInsights = LibraryInsights::load();
    if (!indexedInsights || !indexedInsights->available || indexedInsights->totalBooks != parsedInsights->totalBooks) {
      fail("Library stats index did not rebuild the aggregate cleanly");
    }

    // The organized library exposes the same EPUB through category paths and
    // /Books/00 All Books. More Info must resolve the alias by basename rather
    // than incorrectly reporting that the catalog has no description.
    const LibraryBookSearchResponse metadataCandidates = LibraryBookInfo::search("the", 80);
    bool aliasMetadataVerified = false;
    for (const LibraryBookSearchResult& candidate : metadataCandidates.books) {
      if (candidate.path.find("/00 All Books/") != std::string::npos) continue;
      const LibraryBookInfo exactInfo = LibraryBookInfo::load(candidate.path);
      if (!exactInfo.found || exactInfo.description.empty()) continue;
      const size_t separator = candidate.path.find_last_of('/');
      if (separator == std::string::npos || separator + 1 >= candidate.path.size()) continue;
      const std::string aliasPath = "/Books/00 All Books/" + candidate.path.substr(separator + 1);
      const LibraryBookInfo aliasInfo = LibraryBookInfo::load(aliasPath);
      if (!aliasInfo.found || aliasInfo.description != exactInfo.description || aliasInfo.title != exactInfo.title) {
        fail("Library catalog alias did not preserve More Info metadata for %s", aliasPath.c_str());
      }
      const std::string renamedAliasPath = "/Books/00 All Books/renamed-smoke-test.epub";
      const LibraryBookInfo renamedAliasInfo =
          LibraryBookInfo::load(renamedAliasPath, exactInfo.title, exactInfo.author);
      if (!renamedAliasInfo.found || renamedAliasInfo.description != exactInfo.description ||
          renamedAliasInfo.title != exactInfo.title) {
        fail("Library catalog title fallback did not preserve More Info metadata for %s", renamedAliasPath.c_str());
      }
      aliasMetadataVerified = true;
      break;
    }
    if (!aliasMetadataVerified) {
      fail("Library catalog fixture did not expose a described category-path book for alias verification");
    }
    LOG_INF("SMOKE", "Validated library catalog with %u books and All Books metadata aliases",
            parsedInsights->totalBooks);
  }

  static void verifyEpubCachePathContract() {
    constexpr const char* bookPath = "/Books/Author -- Smoke Book.epub";
    const std::string metadataPath = Epub::cachePathForFilePath(bookPath, DUET_BOOKS_ROOT_PATH "");
    const std::string layoutPath = Epub::layoutCachePathForFilePath(bookPath, DUET_BOOKS_ROOT_PATH "");
    if (metadataPath.find(DUET_BOOKS_ROOT_PATH "/epub_") != 0 || layoutPath.find(DUET_LAYOUTS_ROOT_PATH "/") != 0 ||
        layoutPath.find("/epub_") == std::string::npos || layoutPath == metadataPath) {
      fail("EPUB device cache paths are not split and sharded");
    }

    const std::string simulatorMetadata = Epub::cachePathForFilePath(bookPath, "/test-cache");
    const std::string simulatorLayout = Epub::layoutCachePathForFilePath(bookPath, "/test-cache");
    if (simulatorLayout != simulatorMetadata) {
      fail("EPUB custom cache path unexpectedly escaped its configured root");
    }
  }

  static bool coverBrowserNavigationRequested() {
    const char* raw = std::getenv("CROSSINK_SIMULATOR_SMOKE_FILE_BROWSER_DISPLAY");
    if (raw == nullptr) return false;
    const int display = std::atoi(raw);
    return display == static_cast<int>(CrossPointSettings::FILE_BROWSER_DISPLAY_COVERS) ||
           display == static_cast<int>(CrossPointSettings::FILE_BROWSER_DISPLAY_CAROUSEL);
  }

  static void renderRequestedStatsScreenshots() {
    if (std::getenv("CROSSINK_SIMULATOR_SMOKE_SCREENSHOT_STATS") == nullptr) return;

    BookReadingStats stats;
    stats.sessionCount = 9;
    stats.totalReadingSeconds = 7u * 3600u + 24u * 60u;
    stats.countedSessionSeconds = stats.totalReadingSeconds;
    stats.totalPagesTurned = 486;
    stats.startDate = {2026, 6, 28};
    stats.estimatedTimeLeftSeconds = 3u * 3600u + 15u * 60u;
    stats.avgSecondsPerForwardPage = 42;
    stats.paceSampleCount = 24;
    stats.timeOfDaySeconds = {4200, 7800, 9100, 1600};
    stats.dayOfWeekSeconds = {1200, 3600, 5400, 2400, 7200, 4200, 2700};
    stats.latestSessionReadingSeconds = 46u * 60u;
    stats.latestSessionScreenPages = 37;
    ReadingSessionSnapshot session;
    session.readingSeconds = 38u * 60u;
    session.screenPages = 31;
    session.startedAt = {{2026, 7, 14}, 14, 42, 0};
    session.hasStartedAt = true;

    ReadingJournal::recordSession({{2026, 7, 13}, 21, 15, 0}, 52u * 60u, 43);
    ReadingJournal::recordSession({{2026, 7, 12}, 8, 5, 0}, 27u * 60u, 22);
    ReadingJournal::recordSession({{2026, 7, 11}, 15, 40, 0}, 74u * 60u, 61);
    ReadingJournal::recordSession({{2026, 6, 28}, 10, 20, 0}, 41u * 60u, 34);
    ReadingJournal::recordSession({{2026, 5, 7}, 22, 5, 0}, 63u * 60u, 48);
    ReadingJournal::recordSession({{2026, 3, 19}, 7, 50, 0}, 24u * 60u, 19);
    ReadingJournal::recordSession({{2026, 1, 3}, 13, 15, 0}, 89u * 60u, 72);
    ReadingJournal::recordSession({{2025, 11, 21}, 19, 30, 0}, 36u * 60u, 28);
    ReadingJournal::recordSession({{2025, 9, 2}, 16, 10, 0}, 55u * 60u, 45);
    ReadingJournal::recordSession({{2026, 7, 10}, 12, 0, 0}, 33u * 60u, 0);
    std::unique_ptr<ReadingJournal> journal = ReadingJournal::load();
    if (!journal || journal->recentSessionCount() != 9 ||
        journal->secondsOnDay(readingStatsDayIndex({2026, 7, 10})) != 33u * 60u) {
      fail("Zero-page reading time was not kept separate from counted sessions");
    }

    GlobalReadingStats history;
    history.totalSessions = 15;
    for (uint8_t day = 9; day <= 14; ++day) {
      history.recordReadingSpan({{2026, 7, day}, 12, 0, 0}, 60);
    }
    const ReadingStatsDate historyToday{2026, 7, 14};
    if (!history.hasReadingOnDay(readingStatsDayIndex({2026, 7, 9})) ||
        history.currentReadingStreak(&historyToday) != 6 || history.displayLongestReadingStreak() != 6) {
      fail("Global active-day history did not preserve the six-day legacy streak fixture");
    }
    const ReadingStreakSummary legacyStreak =
        summarizeReadingStreaks(nullptr, history, ReadingSessionSnapshot{}, readingStatsDayIndex(historyToday), 30);
    if (legacyStreak.activeDays != 6 || legacyStreak.current != 6 || legacyStreak.longest != 6) {
      fail("Stats views did not preserve the legacy read-day streak without a detailed journal");
    }
    GlobalReadingStats fourDayHistory;
    for (uint8_t day = 11; day <= 14; ++day) {
      fourDayHistory.recordReadingSpan({{2026, 7, day}, 12, 0, 0}, 60);
    }
    const ReadingStatsDate dayAfterFourDayRun{2026, 7, 15};
    const ReadingStreakSummary fourDayStreak = summarizeReadingStreaks(
        nullptr, fourDayHistory, ReadingSessionSnapshot{}, readingStatsDayIndex(dayAfterFourDayRun), 30);
    if (fourDayStreak.activeDays != 4 || fourDayStreak.current != 4 || fourDayStreak.longest != 4) {
      fail("Four-day legacy streak was not preserved before today's first reading session");
    }
    if (journal->currentGoalStreak(readingStatsDayIndex(historyToday), 45u * 60u) != 1) {
      fail("Goal streak fixture was not kept separate from the read-day streak");
    }

    // Build a deterministic, data-safe media fixture around the focused
    // regression records above. The 228 generated qualifying sessions plus
    // the nine qualifying regression sessions produce 237 local sessions.
    constexpr ReadingStatsDate mediaToday{2026, 7, 14};
    const uint32_t mediaTodayIndex = readingStatsDayIndex(mediaToday);
    std::vector<uint16_t> mediaOffsets;
    mediaOffsets.reserve(228);
    for (uint16_t offset = 0; offset <= 14; ++offset) mediaOffsets.push_back(offset);
    for (uint16_t offset = 120; offset <= 150; ++offset) mediaOffsets.push_back(offset);
    std::vector<uint16_t> mediaOffsetPool;
    mediaOffsetPool.reserve(298);
    for (uint16_t offset = 21; offset <= 114; ++offset) mediaOffsetPool.push_back(offset);
    for (uint16_t offset = 156; offset <= 359; ++offset) mediaOffsetPool.push_back(offset);
    for (size_t i = 0; i < 182; ++i) {
      const size_t index = i * (mediaOffsetPool.size() - 1) / 181;
      mediaOffsets.push_back(mediaOffsetPool[index]);
    }
    std::sort(mediaOffsets.begin(), mediaOffsets.end(), std::greater<uint16_t>());
    constexpr std::array<uint16_t, 16> mediaMinutes = {18, 31, 47, 63, 26, 82, 39, 55, 21, 96, 44, 70, 34, 58, 76, 49};
    constexpr std::array<uint16_t, 16> mediaPages = {12, 24, 39, 55, 18, 73, 31, 46, 14, 86, 36, 61, 27, 50, 68, 41};
    uint32_t generatedSeconds = 0;
    uint32_t generatedPages = 0;
    for (size_t i = 0; i < mediaOffsets.size(); ++i) {
      ReadingStatsDate date;
      if (!readingStatsDateFromDayIndex(mediaTodayIndex - mediaOffsets[i], date)) {
        fail("Could not derive stats media fixture date");
      }
      const size_t patternIndex = (i * 7u + mediaOffsets[i]) % mediaMinutes.size();
      const uint32_t seconds = static_cast<uint32_t>(mediaMinutes[patternIndex]) * 60u;
      const uint16_t pages = mediaPages[(patternIndex * 5u + i) % mediaPages.size()];
      if (!ReadingJournal::recordSession(
              {date, static_cast<uint8_t>((i * 7u) % 24u), static_cast<uint8_t>((i * 13u) % 60u), 0}, seconds, pages)) {
        fail("Could not seed rich stats media fixture");
      }
      generatedSeconds += seconds;
      generatedPages += pages;
    }
    journal = ReadingJournal::load();
    const ReadingJournalPeriod mediaPeriod =
        journal ? journal->periodEndingOn(mediaTodayIndex, 366) : ReadingJournalPeriod{};
    constexpr uint32_t regressionFixtureSeconds = 494u * 60u;
    constexpr uint32_t regressionFixturePages = 372;
    if (!journal || mediaPeriod.sessions != 237 ||
        mediaPeriod.readingSeconds != regressionFixtureSeconds + generatedSeconds ||
        mediaPeriod.screenPages != regressionFixturePages + generatedPages) {
      fail("Rich stats media fixture totals are inconsistent");
    }

    if (!shouldIgnoreReadingStatsPath("/ignore_stats/test.epub") ||
        !shouldIgnoreReadingStatsPath("ignore_stats/nested/test.xtc") ||
        shouldIgnoreReadingStatsPath("/Books/ignore_stats_is_a_title.epub")) {
      fail("Ignored-stats folder path matching is incorrect");
    }

    const ReadingStatsDate ledgerDate{2026, 7, 14};
    if (!ReadingLedger::recordReadingSpan({ledgerDate, 9, 0, 0}, 20u * 60u, 8, DUET_BOOKS_ROOT_PATH "/epub_smoke_a",
                                          "Smoke Book A") ||
        !ReadingLedger::recordReadingSpan({ledgerDate, 18, 0, 0}, 10u * 60u, 0, DUET_BOOKS_ROOT_PATH "/epub_smoke_b",
                                          "Smoke Book B") ||
        !ReadingLedger::recordCorrection(ledgerDate, -5 * 60, DUET_BOOKS_ROOT_PATH "/epub_smoke_a", "Smoke Book A")) {
      fail("Reading ledger could not append its attribution fixture");
    }
    ReadingLedgerDaySummary ledgerDay;
    if (!ReadingLedger::summarizeDay(readingStatsDayIndex(ledgerDate), 25u * 60u, ledgerDay) ||
        ledgerDay.bookCount != 2 || ledgerDay.attributedSeconds != 25u * 60u ||
        ledgerDay.legacyUnattributedSeconds != 0 || strcmp(ledgerDay.books[0].title, "Smoke Book A") != 0 ||
        ledgerDay.books[0].readingSeconds != 15u * 60u) {
      fail("Reading ledger did not aggregate per-book corrections correctly");
    }
    if (!ReadingLedger::recordReadingSpan({{2026, 7, 14}, 23, 55, 0}, 10u * 60u, 1,
                                          DUET_BOOKS_ROOT_PATH "/epub_midnight", "Midnight Book")) {
      fail("Reading ledger could not append a midnight-spanning fixture");
    }
    ReadingLedgerDaySummary beforeMidnight;
    ReadingLedgerDaySummary afterMidnight;
    if (!ReadingLedger::summarizeDay(readingStatsDayIndex({2026, 7, 14}), 30u * 60u, beforeMidnight) ||
        !ReadingLedger::summarizeDay(readingStatsDayIndex({2026, 7, 15}), 5u * 60u, afterMidnight) ||
        beforeMidnight.attributedSeconds != 30u * 60u || afterMidnight.attributedSeconds != 5u * 60u) {
      fail("Reading ledger did not split a session at local midnight");
    }

    BookReadingStats correctionStats;
    correctionStats.totalReadingSeconds = 20u * 60u;
    correctionStats.timeOfDaySeconds[static_cast<size_t>(ReadingTimeBucket::Morning)] = 20u * 60u;
    correctionStats.dayOfWeekSeconds[readingStatsDayOfWeekIndex(ledgerDate)] = 20u * 60u;
    if (correctionStats.adjustReadingTime(ledgerDate, -5 * 60) != -5 * 60 ||
        correctionStats.totalReadingSeconds != 15u * 60u) {
      fail("Per-book reading-time correction did not preserve total consistency");
    }

    history.totalReadingSeconds = 4321;
    history.save();
    const ReadingStatsDate archivedFallbackDate{2026, 7, 14};
    if (!setClocklessReadingStatsDate(archivedFallbackDate)) {
      fail("Clockless stats date could not be saved");
    }
    char archiveName[80] = {};
    uint16_t archivedFiles = 0;
    uint32_t archivedBytes = 0;
    if (!exportAllReadingStats(archiveName, sizeof(archiveName), &archivedFiles, &archivedBytes) || archivedFiles < 3 ||
        archivedBytes == 0) {
      fail("Full reading-stats archive could not be created");
    }
    const std::string archivePath = std::string("/exports/") + archiveName;
    uint16_t validatedFiles = 0;
    if (!validateReadingStatsArchive(archivePath, &validatedFiles, nullptr) || validatedFiles != archivedFiles) {
      fail("Full reading-stats archive did not pass its CRC validation");
    }
    constexpr char achievementPath[] = DUET_STATE_ROOT_PATH "/achievements.bin";
    if (!Storage.existsForRead(achievementPath) || !Storage.remove(achievementPath)) {
      fail("Achievement ledger was unavailable before archive restore");
    }
    GlobalReadingStats changedHistory = GlobalReadingStats::load();
    changedHistory.totalReadingSeconds = 9999;
    changedHistory.save();
    if (!setClocklessReadingStatsDate({2026, 8, 1})) {
      fail("Clockless stats date could not be changed before restore");
    }
    char safetyName[80] = {};
    uint16_t restoredFiles = 0;
    ReadingStatsDateTime restoredFallbackDate;
    const bool imported = importAllReadingStats(archivePath, safetyName, sizeof(safetyName), &restoredFiles);
    const uint32_t restoredReadingSeconds = GlobalReadingStats::load().totalReadingSeconds;
    const bool restoredDateAvailable = getClocklessReadingStatsDateTime(restoredFallbackDate);
    const int restoredDateComparison =
        restoredDateAvailable ? compareReadingStatsDate(restoredFallbackDate.date, archivedFallbackDate) : -2;
    // X3 owns an RTC, so the runtime deliberately replaces an imported
    // clockless fallback with the device's current local date. X4 has no RTC
    // and must recover the archived fallback exactly.
    const bool restoredDateIsCorrect = restoredDateAvailable && (halClock.isAvailable() || restoredDateComparison == 0);
    if (!imported || restoredFiles != archivedFiles || restoredReadingSeconds != 4321 || safetyName[0] == '\0' ||
        !restoredDateIsCorrect || !Storage.existsForRead(achievementPath)) {
      LOG_ERR("SMOKE",
              "Stats archive restore mismatch: imported=%u files=%u/%u seconds=%lu safety=%u date=%04u-%02u-%02u "
              "expected=%04u-%02u-%02u comparison=%d",
              imported ? 1U : 0U, static_cast<unsigned>(restoredFiles), static_cast<unsigned>(archivedFiles),
              static_cast<unsigned long>(restoredReadingSeconds), safetyName[0] != '\0' ? 1U : 0U,
              static_cast<unsigned>(restoredFallbackDate.date.year),
              static_cast<unsigned>(restoredFallbackDate.date.month),
              static_cast<unsigned>(restoredFallbackDate.date.day), static_cast<unsigned>(archivedFallbackDate.year),
              static_cast<unsigned>(archivedFallbackDate.month), static_cast<unsigned>(archivedFallbackDate.day),
              restoredDateComparison);
      fail("Full reading-stats archive did not restore the protected fixture");
    }

    LibraryInsights insights;
    insights.available = true;
    insights.totalBooks = 266;
    insights.unreadBooks = 218;
    insights.readingBooks = 31;
    insights.finishedBooks = 17;
    insights.seriesStarted = 14;
    insights.totalReadingSeconds = 96u * 3600u + 18u * 60u;
    insights.topGenreCount = 3;
    insights.topGenres[0] = {"Romance", 41u * 3600u, 74, 12, 8};
    insights.topGenres[1] = {"Romantasy", 29u * 3600u, 52, 9, 5};
    insights.topGenres[2] = {"Fantasy", 17u * 3600u, 44, 6, 3};
    insights.topAuthorCount = 3;
    insights.topAuthors[0] = {"Sarah J. Maas", 18u * 3600u, 7, 2, 3};
    insights.topAuthors[1] = {"Jane Austen", 11u * 3600u, 5, 1, 3};
    insights.topAuthors[2] = {"Emily Henry", 8u * 3600u, 4, 1, 2};
    insights.spiceLevelCount = 6;
    insights.spiceLevels[0] = {"Spice 0 - None or Not Romance", 12u * 3600u, 81, 4, 3};
    insights.spiceLevels[1] = {"Spice 1 - Low", 9u * 3600u, 24, 3, 2};
    insights.spiceLevels[2] = {"Spice 2 - Moderate", 16u * 3600u, 39, 5, 3};
    insights.spiceLevels[3] = {"Spice 3 - Open Door", 22u * 3600u, 45, 7, 4};
    insights.spiceLevels[4] = {"Spice 4 - Explicit", 28u * 3600u, 56, 9, 4};
    insights.spiceLevels[5] = {"Spice 5 - Very Explicit or Dark", 9u * 3600u, 21, 3, 1};
    insights.seriesProgressCount = 9;
    insights.seriesProgress[0] = {"A Court of Thorns and Roses", 18u * 3600u, 5, 1, 3};
    insights.seriesProgress[1] = {"Harry Potter", 14u * 3600u, 7, 1, 4};
    insights.seriesProgress[2] = {"Bridgertons", 10u * 3600u, 8, 2, 2};
    insights.seriesProgress[3] = {"Devil", 7u * 3600u, 4, 1, 1};
    insights.seriesProgress[4] = {"Flesh and Fire", 6u * 3600u, 4, 1, 1};
    insights.seriesProgress[5] = {"Knockemout", 5u * 3600u, 3, 1, 1};
    insights.seriesProgress[6] = {"Dreamland Billionaires", 4u * 3600u, 3, 1, 1};
    insights.seriesProgress[7] = {"Chestnut Springs", 3u * 3600u, 5, 2, 0};
    insights.seriesProgress[8] = {"Windy City", 2u * 3600u, 5, 1, 0};

    // Match the media fixture's all-time totals and preserve explicit active
    // day history for streak pages.
    for (uint16_t offset = 0; offset <= 14; ++offset) {
      ReadingStatsDate date;
      readingStatsDateFromDayIndex(mediaTodayIndex - offset, date);
      history.recordReadingSpan({date, 12, 0, 0}, 60);
    }
    for (uint16_t offset = 120; offset <= 150; ++offset) {
      ReadingStatsDate date;
      readingStatsDateFromDayIndex(mediaTodayIndex - offset, date);
      history.recordReadingSpan({date, 12, 0, 0}, 60);
    }
    history.totalSessions = 237;
    history.totalReadingSeconds = 209u * 3600u + 57u * 60u;
    history.countedSessionSeconds = history.totalReadingSeconds - 33u * 60u;
    history.totalPagesTurned = 14963;
    history.completedBooks = 24;
    history.longestReadingStreak = 31;
    history.timeOfDaySeconds = {32u * 3600u + 18u * 60u, 49u * 3600u + 42u * 60u, 91u * 3600u + 36u * 60u,
                                36u * 3600u + 21u * 60u};
    history.dayOfWeekSeconds = {
        25u * 3600u, 28u * 3600u, 30u * 3600u, 33u * 3600u, 31u * 3600u, 36u * 3600u, 26u * 3600u + 57u * 60u};

    const auto save = [](const char* path) {
      if (!ScreenshotUtil::saveFramebufferAsBmp(path, renderer.getFrameBuffer(), renderer.getDisplayWidth(),
                                                renderer.getDisplayHeight())) {
        fail("Could not save stats smoke screenshot: %s", path);
      }
    };

    constexpr std::array<const char*, 6> paceFixturePaths = {
        "/books/smoke-stats-pace-01.epub", "/books/smoke-stats-pace-02.epub", "/books/smoke-stats-pace-03.epub",
        "/books/smoke-stats-pace-04.epub", "/books/smoke-stats-pace-05.epub", "/books/smoke-stats-pace-06.epub",
    };
    constexpr std::array<uint16_t, 6> paceFixtureWpm = {214, 247, 231, 276, 294, 318};
    constexpr std::array<uint8_t, 6> paceFixtureDayOffsets = {13, 10, 7, 4, 2, 0};

    Storage.ensureDirectoryExists(DUET_BOOKS_ROOT_PATH "");
    for (uint8_t i = 0; i < 24; ++i) {
      std::string cachePath;
      if (i < paceFixturePaths.size()) {
        cachePath = Epub::cachePathForFilePath(paceFixturePaths[i], DUET_BOOKS_ROOT_PATH "");
      } else {
        char generatedPath[96];
        snprintf(generatedPath, sizeof(generatedPath), DUET_BOOKS_ROOT_PATH "/epub_media_%02u",
                 static_cast<unsigned>(i + 1));
        cachePath = generatedPath;
      }
      Storage.ensureDirectoryExists(cachePath.c_str());
      ReadingStatsDate startDate;
      readingStatsDateFromDayIndex(mediaTodayIndex - static_cast<uint32_t>(8u + i * 11u), startDate);
      BookReadingStats detail;
      detail.sessionCount = static_cast<uint16_t>(2u + i % 9u);
      detail.totalReadingSeconds = static_cast<uint32_t>(55u + i * 17u) * 60u;
      detail.countedSessionSeconds = detail.totalReadingSeconds;
      detail.totalPagesTurned = static_cast<uint32_t>(70u + i * 31u);
      detail.startDate = startDate;
      detail.avgSecondsPerForwardPage = static_cast<uint16_t>(34u + i % 18u);
      detail.paceSampleCount = 8;
      if (i < 6) {
        Epub paceBook(paceFixturePaths[i], DUET_BOOKS_ROOT_PATH "");
        if (!paceBook.load(true, true) || paceBook.getTotalWords() == 0) {
          fail("Could not load the WPM media fixture book: %s", paceFixturePaths[i]);
        }
        detail.totalReadingSeconds = static_cast<uint32_t>(std::max<uint64_t>(
            10,
            (static_cast<uint64_t>(paceBook.getTotalWords()) * 60ULL + paceFixtureWpm[i] / 2ULL) / paceFixtureWpm[i]));
        detail.countedSessionSeconds = detail.totalReadingSeconds;
        detail.isCompleted = true;
        readingStatsDateFromDayIndex(readingStatsDayIndex(startDate) + static_cast<uint32_t>(3u + i * 2u),
                                     detail.finishedDate);
      }
      detail.save(cachePath);
      char title[64];
      snprintf(title, sizeof(title), i < paceFixturePaths.size() ? "Pace Fixture %02u" : "Fictional Media Book %02u",
               static_cast<unsigned>(i + 1));
      if (!ReadingLedger::recordReadingSpan({startDate, 19, 0, 0}, detail.totalReadingSeconds, detail.totalPagesTurned,
                                            cachePath.c_str(), title)) {
        fail("Could not seed detailed media book history");
      }
      if (i < paceFixturePaths.size()) {
        ReadingStatsDate paceDate;
        if (!readingStatsDateFromDayIndex(mediaTodayIndex - paceFixtureDayOffsets[i], paceDate) ||
            !ReadingLedger::recordReadingSpan({paceDate, 20, 0, 0}, 18u * 60u, 16u + i * 3u, cachePath.c_str(),
                                              title)) {
          fail("Could not seed recent WPM media history");
        }
      }
    }
    if (!LibraryInsights::publishLocalDetailedBookStatsForSync()) {
      fail("Could not publish detailed media book fixture");
    }

    GlobalReadingStats peerHistory = history;
    peerHistory.totalSessions = 156;
    peerHistory.totalReadingSeconds = 92u * 3600u + 48u * 60u;
    peerHistory.countedSessionSeconds = peerHistory.totalReadingSeconds;
    peerHistory.totalPagesTurned = 6842;
    peerHistory.completedBooks = 19;
    peerHistory.save();
    Storage.ensureDirectoryExists(DUET_STATE_ROOT_PATH "/synced_stats");
    if (!copySmokeFile(DUET_STATE_ROOT_PATH "/global_stats.bin",
                       DUET_STATE_ROOT_PATH "/synced_stats/device_a1b2c3d4e5f6.bin")) {
      fail("Could not seed synced stats media fixture");
    }
    history.save();
    const GlobalReadingStats allDevicesHistory = GlobalReadingStats::loadAggregated(history);

    constexpr std::array<const char*, 33> tabLabels = {
        "Current",      "Progress", "Book",        "Device",      "Synced",    "Devices",      "Trends",
        "Activity",     "90 Days",  "Calendar",    "Heatmap",     "Profile",   "Goals",        "Sessions",
        "Weekdays",     "Pace",     "Time of Day", "Months",      "Year",      "Sessions Mix", "Streaks",
        "Start/Finish", "Dates",    "Reader DNA",  "DNA Details", "Signature", "Sig Details",  "Fastest",
        "Wrapped",      "Started",  "Library",     "Taste",       "Series",
    };
    const auto drawTabs = [&](const size_t selected) {
      renderStatsTabBar(renderer, tabLabels.data(), tabLabels.size(), selected);
    };

    RenderLock screenshotLock;
    renderCurrentBookStatsPage(renderer, &mappedInputManager, "A Very Long Current Book Title for Layout Testing",
                               stats, session, 64.0f, true, stats.estimatedTimeLeftSeconds, true, true, true, 95000);
    drawTabs(0);
    save("/smoke-stats-current.bmp");
    renderCurrentBookStatsPage(renderer, &mappedInputManager, "A Very Long Current Book Title for Layout Testing",
                               stats, ReadingSessionSnapshot{}, 64.0f, true, stats.estimatedTimeLeftSeconds, true, true,
                               true, 95000);
    drawTabs(0);
    save("/smoke-stats-latest-session.bmp");
    renderBookProgressGraphPage(renderer, &mappedInputManager, "A Very Long Current Book Title for Layout Testing",
                                stats, 64.0f, true, stats.estimatedTimeLeftSeconds, true, true, 95000);
    drawTabs(1);
    save("/smoke-stats-progress.bmp");
    renderPerBookStatsPage(renderer, &mappedInputManager, "A Very Long Current Book Title for Layout Testing", stats,
                           64.0f, true, stats.estimatedTimeLeftSeconds, true, false, true, 95000);
    drawTabs(2);
    save("/smoke-stats-book-patterns.bmp");
    renderReadingTrendsPage(renderer, &mappedInputManager, journal.get(), history, session, true, true);
    drawTabs(6);
    save("/smoke-stats-trends.bmp");
    renderReadingActivityChartPage(renderer, &mappedInputManager, journal.get(), history, session, 30, true, true);
    drawTabs(7);
    save("/smoke-stats-activity.bmp");
    renderReadingDailyMinutesPage(renderer, &mappedInputManager, journal.get(), history, session, 0, true, true);
    drawTabs(8);
    save("/smoke-stats-daily-minutes.bmp");
    renderReadingDailyMinutesPage(renderer, &mappedInputManager, journal.get(), history, session, 8, true, true);
    drawTabs(8);
    save("/smoke-stats-daily-minutes-scrolled.bmp");
    renderMonthlyReadingCalendarPage(renderer, &mappedInputManager, journal.get(), history, session, 30, true, true);
    drawTabs(9);
    save("/smoke-stats-calendar.bmp");
    renderReadingDayDetailsPage(renderer, &mappedInputManager, ledgerDate, beforeMidnight, 0, false, 0, true, true);
    save("/smoke-stats-day-details.bmp");
    renderReadingDayDetailsPage(renderer, &mappedInputManager, ledgerDate, beforeMidnight, 0, true, -5 * 60, true,
                                true);
    save("/smoke-stats-day-edit.bmp");
    renderReadingHeatmapPage(renderer, &mappedInputManager, journal.get(), history, session, 30, true, true);
    drawTabs(10);
    save("/smoke-stats-heatmap.bmp");
    renderReadingProfilePage(renderer, &mappedInputManager, journal.get(), history, session, 30, true, true);
    drawTabs(11);
    save("/smoke-stats-profile.bmp");
    renderReadingGoalsPage(renderer, &mappedInputManager, journal.get(), history, session, 20, true, true);
    drawTabs(12);
    save("/smoke-stats-goals.bmp");
    renderRecentReadingSessionsPage(renderer, &mappedInputManager, journal.get(), history.totalSessions, 0, true, true);
    drawTabs(13);
    save("/smoke-stats-recent.bmp");
    renderRecentReadingSessionsPage(renderer, &mappedInputManager, journal.get(), history.totalSessions, 3, true, true);
    drawTabs(13);
    save("/smoke-stats-recent-scrolled.bmp");
    renderWeekdayPatternPage(renderer, &mappedInputManager, journal.get(), history, session, true, true);
    drawTabs(14);
    save("/smoke-stats-weekdays.bmp");
    renderPaceTrendPage(renderer, &mappedInputManager, journal.get(), session, true, true);
    drawTabs(15);
    save("/smoke-stats-pace.bmp");
    renderTimeOfDayPage(renderer, &mappedInputManager, history, true, true);
    drawTabs(16);
    save("/smoke-stats-time-of-day.bmp");
    renderMonthlyTrendPage(renderer, &mappedInputManager, journal.get(), session, true, true);
    drawTabs(17);
    save("/smoke-stats-months.bmp");
    renderYearLinePage(renderer, &mappedInputManager, journal.get(), session, true, true);
    drawTabs(18);
    save("/smoke-stats-year.bmp");
    const DeviceSplitStatsSummary deviceSplit = loadDeviceSplitStatsSummary();
    renderDeviceSplitPage(renderer, &mappedInputManager, history, deviceSplit, true, true);
    drawTabs(5);
    save("/smoke-stats-devices.bmp");
    renderSessionLengthsPage(renderer, &mappedInputManager, journal.get(), session, true, true);
    drawTabs(19);
    save("/smoke-stats-sessions-mix.bmp");
    renderStreakMilestonesPage(renderer, &mappedInputManager, journal.get(), history, session, true, true);
    drawTabs(20);
    save("/smoke-stats-streaks.bmp");
    const StartFinishStatsSummary startFinish = loadStartFinishStatsSummary();
    renderStartedFinishedPage(renderer, &mappedInputManager, startFinish, true, true);
    drawTabs(21);
    save("/smoke-stats-start-finish.bmp");
    const std::vector<ReadingDateStatsEntry> readingDates = loadReadingDateStatsEntries();
    renderReadingDatesPage(renderer, &mappedInputManager, readingDates, 0, false, true, true);
    drawTabs(22);
    save("/smoke-stats-reading-dates.bmp");
    renderReadingDatesPage(renderer, &mappedInputManager, readingDates,
                           readingDates.empty() ? 0 : readingDates.size() - 1, false, true, true);
    drawTabs(22);
    save("/smoke-stats-reading-dates-scrolled.bmp");
    renderReaderRadarPage(renderer, &mappedInputManager, journal.get(), history, session, &insights, 20, true, true);
    drawTabs(23);
    save("/smoke-stats-reader-dna.bmp");
    renderReaderDnaDetailsPage(renderer, &mappedInputManager, journal.get(), history, session, true, true);
    drawTabs(24);
    save("/smoke-stats-dna-details.bmp");
    renderReadingSignaturePage(renderer, &mappedInputManager, journal.get(), history, session, 20, true, true);
    drawTabs(25);
    save("/smoke-stats-signature.bmp");
    renderReadingSignatureDetailsPage(renderer, &mappedInputManager, journal.get(), history, session, 20, true, true);
    drawTabs(26);
    save("/smoke-stats-signature-details.bmp");
    const std::vector<FastestReadStatsEntry> fastestReads = loadFastestReadStatsEntries();
    renderFastestReadsPage(renderer, &mappedInputManager, fastestReads, false, true, true);
    drawTabs(27);
    save("/smoke-stats-fastest.bmp");
    renderReadingWrappedPage(renderer, &mappedInputManager, journal.get(), history, session, true, true);
    drawTabs(28);
    save("/smoke-stats-wrapped.bmp");
    const std::vector<StartedBookStatsEntry> startedBooks = loadStartedBookStatsEntries();
    renderStartedBooksPage(renderer, &mappedInputManager, startedBooks, 0, false, true, true);
    drawTabs(29);
    save("/smoke-stats-started.bmp");
    renderStartedBooksPage(renderer, &mappedInputManager, startedBooks,
                           startedBooks.empty() ? 0 : startedBooks.size() - 1, false, true, true);
    drawTabs(29);
    save("/smoke-stats-started-scrolled.bmp");
    renderLibraryInsightsPage(renderer, &mappedInputManager, &insights, true, true);
    drawTabs(30);
    save("/smoke-stats-library.bmp");
    renderReadingTastePage(renderer, &mappedInputManager, &insights, true, true);
    drawTabs(31);
    save("/smoke-stats-taste.bmp");
    renderSeriesProgressPage(renderer, &mappedInputManager, &insights, 0, true, true);
    drawTabs(32);
    save("/smoke-stats-series.bmp");
    renderSeriesProgressPage(renderer, &mappedInputManager, &insights, 3, true, true);
    drawTabs(32);
    save("/smoke-stats-series-scrolled.bmp");
    renderGlobalStatsPage(renderer, &mappedInputManager, "This Device", history, true, true);
    drawTabs(3);
    save("/smoke-stats-this-device.bmp");
    renderGlobalStatsPage(renderer, &mappedInputManager, "All Devices", allDevicesHistory, true, true);
    drawTabs(4);
    save("/smoke-stats-all-devices.bmp");
    stats.isCompleted = true;
    stats.finishedDate = {2026, 7, 14};
    renderEditBookDatesPage(renderer, &mappedInputManager, "A Very Long Current Book Title for Layout Testing", stats,
                            0, false, true);
    save("/smoke-stats-book-dates-locked.bmp");
    renderEditBookDatesPage(renderer, &mappedInputManager, "A Very Long Current Book Title for Layout Testing", stats,
                            0, true, true);
    save("/smoke-stats-book-dates-editing.bmp");
    LOG_INF("SMOKE", "Saved reading stats screenshots");
  }

  static void renderRequestedBookInfoScreenshot() {
    if (std::getenv("CROSSINK_SIMULATOR_SMOKE_SCREENSHOT_BOOK_INFO") == nullptr) return;
    const char* bookPath = std::getenv("CROSSINK_SIMULATOR_SMOKE_BOOK");
    if (bookPath == nullptr || bookPath[0] == '\0') fail("Book Info screenshot requires a smoke-test book");
    const char* bookTitle = std::getenv("CROSSINK_SIMULATOR_SMOKE_BOOK_TITLE");

    BookInfoActivity activity(renderer, mappedInputManager, bookPath,
                              bookTitle != nullptr && bookTitle[0] != '\0' ? bookTitle : "Smoke Test Book", "");
    activity.onEnter();
    activity.render(RenderLock{});
    if (!ScreenshotUtil::saveFramebufferAsBmp("/smoke-book-info.bmp", renderer.getFrameBuffer(),
                                              renderer.getDisplayWidth(), renderer.getDisplayHeight())) {
      fail("Could not save Book Info smoke screenshot");
    }
    delay(70);
    activity.loop();
    activity.render(RenderLock{});
    if (!ScreenshotUtil::saveFramebufferAsBmp("/smoke-book-info-loaded.bmp", renderer.getFrameBuffer(),
                                              renderer.getDisplayWidth(), renderer.getDisplayHeight())) {
      fail("Could not save hydrated Book Info smoke screenshot");
    }
    activity.onExit();
    LOG_INF("SMOKE", "Saved immediate and hydrated Book Info screenshots");
  }

  static void renderRequestedLibrarySearchScreenshot() {
    const char* query = std::getenv("CROSSINK_SIMULATOR_SMOKE_LIBRARY_SEARCH_QUERY");
    if (query == nullptr || query[0] == '\0') return;

    const LibraryBookSearchResponse matches = LibraryBookInfo::search(query, 80);
    if (!matches.catalogAvailable) fail("Library Search screenshot requires a library catalog");
    if (matches.books.empty()) fail("Library Search query returned no books: %s", query);

    LibrarySearchActivity activity(renderer, mappedInputManager, query);
    activity.onEnter();
    if (activityManager.requestUpdateAndWait() != RequestUpdateResult::Rendered) {
      fail("Could not drain the pending Library Search repaint");
    }
    activity.render(RenderLock{});
    if (!ScreenshotUtil::saveFramebufferAsBmp("/smoke-library-search.bmp", renderer.getFrameBuffer(),
                                              renderer.getDisplayWidth(), renderer.getDisplayHeight())) {
      fail("Could not save Library Search smoke screenshot");
    }
    activity.onExit();
    LOG_INF("SMOKE", "Validated Library Search query '%s' with %u result(s)", query,
            static_cast<unsigned>(matches.books.size()));
  }

  static void renderRequestedLibraryAutocompleteScreenshot() {
    const char* query = std::getenv("CROSSINK_SIMULATOR_SMOKE_LIBRARY_AUTOCOMPLETE_QUERY");
    if (query == nullptr || query[0] == '\0') return;

    const LibraryBookSuggestionResponse matches = LibraryBookInfo::suggest(query, 3);
    if (!matches.catalogAvailable) fail("Library autocomplete screenshot requires a library catalog");
    if (matches.suggestions.empty()) fail("Library autocomplete query returned no suggestions: %s", query);

    auto provider = [](const std::string& text, const size_t maxSuggestions) {
      LibraryBookSuggestionResponse response = LibraryBookInfo::suggest(text, maxSuggestions);
      std::vector<KeyboardSuggestion> suggestions;
      suggestions.reserve(response.suggestions.size());
      for (auto& suggestion : response.suggestions) {
        const char* category = "Title";
        if (suggestion.kind == LibraryBookSuggestionKind::Author) {
          category = "Author";
        } else if (suggestion.kind == LibraryBookSuggestionKind::Series) {
          category = "Series";
        }
        suggestions.push_back({std::move(suggestion.value), category});
      }
      return suggestions;
    };

    KeyboardEntryActivity activity(renderer, mappedInputManager, "Search Library", query, 128, InputType::Search, 2,
                                   provider);
    activity.onEnter();
    // Autocomplete is deliberately idle-debounced so key handling wins. Let
    // that short window elapse before exercising Next/Fill in the simulator.
    delay(130);
    activity.loop();
    activity.render(RenderLock{});
    if (!ScreenshotUtil::saveFramebufferAsBmp("/smoke-library-autocomplete.bmp", renderer.getFrameBuffer(),
                                              renderer.getDisplayWidth(), renderer.getDisplayHeight())) {
      fail("Could not save Library autocomplete smoke screenshot");
    }

    auto pressAndRelease = [&activity](const MappedInputManager::Button button) {
      mappedInputManager.simulatorClearInputFrame();
      mappedInputManager.simulatorInjectPress(button);
      activity.loop();
      mappedInputManager.simulatorClearInputFrame();
      mappedInputManager.simulatorInjectRelease(button);
      activity.loop();
      mappedInputManager.simulatorClearInputFrame();
    };
    for (int row = 0; row < 4; ++row) pressAndRelease(MappedInputManager::Button::Down);
    size_t expectedSuggestion = 0;
    if (matches.suggestions.size() > 1) {
      pressAndRelease(MappedInputManager::Button::Confirm);
      expectedSuggestion = 1;
    }
    pressAndRelease(MappedInputManager::Button::Right);
    pressAndRelease(MappedInputManager::Button::Confirm);
    if (activity.simulatorText() != matches.suggestions[expectedSuggestion].value) {
      fail("Library autocomplete Next/Fill did not apply the selected suggestion");
    }
    activity.render(RenderLock{});
    if (!ScreenshotUtil::saveFramebufferAsBmp("/smoke-library-autocomplete-filled.bmp", renderer.getFrameBuffer(),
                                              renderer.getDisplayWidth(), renderer.getDisplayHeight())) {
      fail("Could not save filled library autocomplete smoke screenshot");
    }

    activity.onExit();
    LOG_INF("SMOKE", "Validated library autocomplete query '%s'; first suggestion '%s'", query,
            matches.suggestions.front().value.c_str());
  }

  static void renderRequestedFeatureScreenshots() {
    if (std::getenv("CROSSINK_SIMULATOR_SMOKE_SCREENSHOT_FEATURES") == nullptr) return;

    const auto save = [](const char* path) {
      if (!ScreenshotUtil::saveFramebufferAsBmp(path, renderer.getFrameBuffer(), renderer.getDisplayWidth(),
                                                renderer.getDisplayHeight())) {
        fail("Could not save feature smoke screenshot: %s", path);
      }
    };

    DICTIONARIES.ensureScanned();
    if (DICTIONARIES.getEntries().size() != 1 || !DICTIONARIES.hasActiveDictionary() || !DICTIONARIES.prepareActive()) {
      fail("Dictionary fixture did not scan and prepare");
    }
    const DictionaryLookupResult lookup = DICTIONARIES.lookup("serendipity", false);
    if (lookup.status != DictionaryLookupResult::Status::Found || lookup.headword != "serendipity" ||
        lookup.definition.empty()) {
      fail("Dictionary fixture lookup did not return the expected definition");
    }
    if (std::getenv("CROSSINK_SIMULATOR_SMOKE_EXPECT_WORDNET") != nullptr) {
      const DictionaryLookupResult irregular = DICTIONARIES.lookup("went", false);
      if (irregular.status != DictionaryLookupResult::Status::Found || irregular.headword != "go" ||
          irregular.definition.empty()) {
        fail("WordNet irregular-form alias lookup did not resolve went to go");
      }
    }

    UtilitiesActivity utilities(renderer, mappedInputManager);
    utilities.onEnter();
    utilities.render(RenderLock{});
    save("/smoke-utilities.bmp");
    utilities.onExit();

    AchievementSnapshot achievementSnapshot;
    achievementSnapshot.booksStarted = 1;
    std::vector<AchievementView> achievementViews = AchievementCatalog::buildViews(achievementSnapshot);
    if (achievementViews.empty() || !achievementViews.front().unlocked ||
        !ACHIEVEMENT_STORE.reconcile(achievementViews, false)) {
      fail("Achievement fixture did not persist an unlocked milestone");
    }
    AchievementsActivity achievements(renderer, mappedInputManager);
    achievements.onEnter();
    achievements.render(RenderLock{});
    save("/smoke-achievements.bmp");
    // The default tab is pending. Exercise the completed-tab toggle too, so a
    // persisted unlock is proven all the way through to its visible list.
    mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
    achievements.loop();
    mappedInputManager.simulatorClearInputFrame();
    achievements.render(RenderLock{});
    save("/smoke-achievements-completed.bmp");
    achievements.onExit();

    constexpr char favoritePath[] = "/books/test_reader_rendering_matrix.epub";
    constexpr char movedFavoritePath[] = "/Read/test_reader_rendering_matrix.epub";
    if (!FAVORITES.addBook(favoritePath, "Smoke Favorite", "Smoke Author") ||
        !FAVORITES.updatePath(favoritePath, movedFavoritePath, "", "") || !FAVORITES.isFavorite(movedFavoritePath) ||
        FAVORITES.isFavorite(favoritePath) || !FAVORITES.updatePath(movedFavoritePath, favoritePath, "", "")) {
      fail("Favorite fixture did not survive its finished-book path migration");
    }
    FavoritesActivity favorites(renderer, mappedInputManager);
    favorites.onEnter();
    favorites.render(RenderLock{});
    save("/smoke-favorites.bmp");
    favorites.onExit();

    DictionaryActivity dictionary(renderer, mappedInputManager);
    dictionary.onEnter();
    dictionary.render(RenderLock{});
    save("/smoke-dictionary.bmp");
    dictionary.onExit();

    DictionaryDefinitionActivity definition(renderer, mappedInputManager, nullptr, lookup.headword, lookup.definition,
                                            lookup.truncated, UI_10_FONT_ID,
                                            DICTIONARIES.getDefinitionFontId(UI_10_FONT_ID), 0, 0, true);
    definition.onEnter();
    definition.render(RenderLock{});
    save("/smoke-dictionary-definition.bmp");
    definition.onExit();

    TetrisActivity tetris(renderer, mappedInputManager);
    tetris.onEnter();
    tetris.render(RenderLock{});
    save("/smoke-tetris.bmp");
    tetris.onExit();

    LOG_INF("SMOKE", "Validated Apps, Favorites migration, Achievements, Dictionary lookup, and Tetris screens");
  }

  static void renderRequestedDashboardHomeScreenshot() {
    if (std::getenv("CROSSINK_SIMULATOR_SMOKE_SCREENSHOT_DASHBOARD_HOME") == nullptr) return;
    if (static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) != CrossPointSettings::UI_THEME::DASHBOARD) {
      fail("Dashboard Home screenshot requested without the Dashboard theme");
    }

    const char* bookPath = std::getenv("CROSSINK_SIMULATOR_SMOKE_BOOK");
    if (bookPath == nullptr || bookPath[0] == '\0' || !Storage.exists(bookPath)) {
      fail("Dashboard Home screenshot fixture book is missing");
    }

    Epub book(bookPath, DUET_BOOKS_ROOT_PATH "");
    if (!book.load(true, true)) {
      fail("Dashboard Home fixture could not load EPUB metadata");
    }
    RECENT_BOOKS.addOrUpdateBook(bookPath, book.getTitle(), book.getAuthor(), book.getAdaptiveThumbBmpPath(296, 444));

    BookReadingStats currentStats = BookReadingStats::load(book.getCachePath());
    currentStats.sessionCount = 8;
    currentStats.totalReadingSeconds = 4u * 3600u + 42u * 60u;
    currentStats.countedSessionSeconds = 4u * 3600u + 21u * 60u;
    currentStats.totalPagesTurned = 386;
    currentStats.avgSecondsPerForwardPage = 42;
    currentStats.paceSampleCount = 36;
    currentStats.estimatedTimeLeftSeconds = 3u * 3600u + 15u * 60u;
    currentStats.latestSessionReadingSeconds = 28u * 60u;
    currentStats.latestSessionScreenPages = 24;
    currentStats.latestSessionDayIndex = readingStatsDayIndex({2026, 7, 14});
    currentStats.latestSessionStartMinute = 19u * 60u + 12u;
    currentStats.startDate = {2026, 7, 10};
    currentStats.finishedDate.clear();
    currentStats.isCompleted = false;
    currentStats.timeOfDaySeconds = {30u * 60u, 72u * 60u, 180u * 60u, 0};
    currentStats.dayOfWeekSeconds = {30u * 60u, 40u * 60u, 50u * 60u, 60u * 60u, 62u * 60u, 20u * 60u, 20u * 60u};
    currentStats.save(book.getCachePath());

    GlobalReadingStats globalStats;
    globalStats.totalSessions = 325;
    globalStats.totalReadingSeconds = 23u * 3600u + 52u * 60u;
    globalStats.countedSessionSeconds = 21u * 3600u + 11u * 60u;
    globalStats.totalPagesTurned = 14963;
    globalStats.completedBooks = 18;
    globalStats.timeOfDaySeconds = {190u * 60u, 260u * 60u, 712u * 60u, 270u * 60u};
    globalStats.dayOfWeekSeconds = {200u * 60u, 210u * 60u, 220u * 60u, 240u * 60u, 250u * 60u, 160u * 60u, 152u * 60u};
    globalStats.recordReadingSpan({{2026, 6, 30}, 19, 0, 0}, 60);
    for (uint8_t day = 1; day <= 14; ++day) {
      globalStats.recordReadingSpan({{2026, 7, day}, 19, 0, 0}, 60);
    }
    globalStats.longestReadingStreak = 31;
    globalStats.save();

    HomeActivity dashboardHome(renderer, mappedInputManager);
    dashboardHome.onEnter();
    dashboardHome.simulatorOverrideBookWordCount(95000);
    dashboardHome.render(RenderLock{});
    if (!ScreenshotUtil::saveFramebufferAsBmp("/smoke-dashboard-home.bmp", renderer.getFrameBuffer(),
                                              renderer.getDisplayWidth(), renderer.getDisplayHeight())) {
      fail("Could not save Dashboard Home smoke screenshot");
    }
    dashboardHome.onExit();
    LOG_INF("SMOKE", "Validated Dashboard Home with a cached cover and recent-book context");
  }

  static void renderRequestedMinimalHomeMenuScreenshot() {
    if (std::getenv("CROSSINK_SIMULATOR_SMOKE_SCREENSHOT_MINIMAL_HOME_MENU") == nullptr) return;
    if (static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) != CrossPointSettings::UI_THEME::MINIMAL) {
      fail("Minimal Home menu screenshot requested without the Minimal theme");
    }

    HomeActivity home(renderer, mappedInputManager);
    home.onEnter();
    home.render(RenderLock{});

    // onEnter suppresses a carried-over front-button release. Clear that guard
    // first, then follow the normal Home navigation path into Menu.
    mappedInputManager.simulatorClearInputFrame();
    home.loop();
    mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Down);
    home.loop();
    mappedInputManager.simulatorClearInputFrame();
    mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
    home.loop();
    mappedInputManager.simulatorClearInputFrame();
    home.render(RenderLock{});

    if (!ScreenshotUtil::saveFramebufferAsBmp("/smoke-minimal-home-menu.bmp", renderer.getFrameBuffer(),
                                              renderer.getDisplayWidth(), renderer.getDisplayHeight())) {
      fail("Could not save Minimal Home menu smoke screenshot");
    }
    home.onExit();
    LOG_INF("SMOKE", "Validated compact centered Minimal Home menu");
  }

  static void renderRequestedReadingHomeScreenshot() {
    if (std::getenv("CROSSINK_SIMULATOR_SMOKE_SCREENSHOT_READING_HOME") == nullptr) return;
    if (static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) != CrossPointSettings::UI_THEME::READING_HOME) {
      fail("Reading Home screenshot requested without the Reading Home theme");
    }

    const char* rawPaths = std::getenv("CROSSINK_SIMULATOR_SMOKE_READING_HOME_BOOKS");
    if (rawPaths == nullptr || rawPaths[0] == '\0') {
      fail("Reading Home screenshot fixture paths are missing");
    }

    std::vector<std::string> paths;
    std::string path;
    for (const char c : std::string(rawPaths)) {
      if (c == '|') {
        if (!path.empty()) paths.push_back(path);
        path.clear();
      } else {
        path += c;
      }
    }
    if (!path.empty()) paths.push_back(path);
    if (paths.size() != ReadingHomeTheme::kRecentBookCount) {
      fail("Reading Home screenshot expected %d books, got %d", ReadingHomeTheme::kRecentBookCount,
           static_cast<int>(paths.size()));
    }

    for (int index = static_cast<int>(paths.size()) - 1; index >= 0; --index) {
      if (!Storage.exists(paths[index].c_str())) {
        fail("Reading Home fixture book is missing: %s", paths[index].c_str());
      }
      Epub book(paths[index], DUET_BOOKS_ROOT_PATH "");
      if (!book.load(true, true)) {
        fail("Reading Home fixture could not build EPUB metadata: %s", paths[index].c_str());
      }
      RECENT_BOOKS.addOrUpdateBook(paths[index], book.getTitle(), book.getAuthor(), book.getThumbBmpPath());
    }

    Epub currentBook(paths.front(), DUET_BOOKS_ROOT_PATH "");
    if (!currentBook.load(false, true)) {
      fail("Reading Home fixture could not reopen current EPUB metadata");
    }
    BookReadingStats currentStats = BookReadingStats::load(currentBook.getCachePath());
    currentStats.sessionCount = 3;
    currentStats.totalReadingSeconds = 42u * 60u;
    currentStats.countedSessionSeconds = 42u * 60u;
    currentStats.totalPagesTurned = 54;
    currentStats.avgSecondsPerForwardPage = 42;
    currentStats.paceSampleCount = 24;
    currentStats.estimatedTimeLeftSeconds = 3u * 60u * 60u;
    currentStats.isCompleted = false;
    currentStats.startDate = {2026, 7, 10};
    currentStats.save(currentBook.getCachePath());

    GlobalReadingStats globalStats = GlobalReadingStats::load();
    for (uint8_t day = 11; day <= 14; ++day) {
      globalStats.recordReadingSpan({{2026, 7, day}, 12, 0, 0}, 60);
    }
    globalStats.save();

    ReadingJournal::recordSession({{2026, 7, 14}, 10, 20, 0}, 77u * 60u, 28);
    HomeActivity readingHome(renderer, mappedInputManager);
    readingHome.onEnter();
    readingHome.simulatorOverrideBookWordCount(16800);
    readingHome.render(RenderLock{});
    readingHome.render(RenderLock{});
    readingHome.render(RenderLock{});

    if (!ScreenshotUtil::saveFramebufferAsBmp("/smoke-reading-home.bmp", renderer.getFrameBuffer(),
                                              renderer.getDisplayWidth(), renderer.getDisplayHeight())) {
      fail("Could not save Reading Home smoke screenshot");
    }

    const int selectorBeforeNavigation = readingHome.simulatorSelectorIndex();
    mappedInputManager.simulatorClearInputFrame();
    mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Right);
    readingHome.loop();
    mappedInputManager.simulatorClearInputFrame();
    if (readingHome.simulatorSelectorIndex() == selectorBeforeNavigation ||
        !readingHome.simulatorReadingHomeSelectionPending()) {
      fail("Reading Home selector did not move and queue its lightweight repaint immediately");
    }
    readingHome.onExit();
    LOG_INF("SMOKE", "Validated CrossPet-derived Reading Home with four recent books");
  }

  static uint8_t requireFontSizeIndex(const SdCardFontFamilyInfo& family, const uint8_t pointSize) {
    const std::vector<uint8_t> sizes = family.availableSizes();
    const auto found = std::find(sizes.begin(), sizes.end(), pointSize);
    if (found == sizes.end()) {
      fail("Font preview family %s is missing %u pt", family.name.c_str(), pointSize);
    }
    return static_cast<uint8_t>(std::distance(sizes.begin(), found));
  }

  static void selectSdFont(const SdCardFontFamilyInfo& family, const uint8_t sizeIndex) {
    SETTINGS.fontFamily = 0;
    SETTINGS.fontSize = sizeIndex;
    const auto sizes = family.availableSizes();
    SETTINGS.sdFontPointSize = sizeIndex < sizes.size() ? sizes[sizeIndex] : 0;
    strncpy(SETTINGS.sdFontFamilyName, family.name.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
    SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
    sdFontSystem.ensureLoaded(renderer);
  }

  static void verifySdFontSizeContract() {
    const char* requestedFamily = std::getenv("CROSSINK_SIMULATOR_VERIFY_SD_FONT_SIZES");
    if (requestedFamily == nullptr || requestedFamily[0] == '\0') return;

    const SdCardFontFamilyInfo* family = sdFontSystem.registry().findFamily(requestedFamily);
    if (!family) fail("SD font size contract family is missing: %s", requestedFamily);
    const uint8_t index12 = requireFontSizeIndex(*family, 12);
    const uint8_t index14 = requireFontSizeIndex(*family, 14);

    const uint8_t originalFontFamily = SETTINGS.fontFamily;
    const uint8_t originalFontSize = SETTINGS.fontSize;
    const uint8_t originalSdFontPointSize = SETTINGS.sdFontPointSize;
    char originalSdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName)] = {};
    strncpy(originalSdFontFamilyName, SETTINGS.sdFontFamilyName, sizeof(originalSdFontFamilyName) - 1);

    selectSdFont(*family, index12);
    const int id12 = SETTINGS.getReaderFontId();
    const int lineHeight12 = renderer.getLineHeight(id12);

    SETTINGS.fontSize = index12;
    SETTINGS.sdFontPointSize = 14;
    sdFontSystem.ensureLoaded(renderer);
    const int id14 = SETTINGS.getReaderFontId();
    const int lineHeight14 = renderer.getLineHeight(id14);
    if (SETTINGS.fontSize != index14 || sdFontSystem.currentPointSize() != 14) {
      fail("Stable 14 pt selection resolved to index %u / %u pt", SETTINGS.fontSize, sdFontSystem.currentPointSize());
    }
    if (id12 == id14 || lineHeight14 <= lineHeight12) {
      fail("12 -> 14 pt reload did not change font metrics (ids %d/%d, heights %d/%d)", id12, id14, lineHeight12,
           lineHeight14);
    }

    SETTINGS.fontFamily = originalFontFamily;
    SETTINGS.fontSize = originalFontSize;
    SETTINGS.sdFontPointSize = originalSdFontPointSize;
    strncpy(SETTINGS.sdFontFamilyName, originalSdFontFamilyName, sizeof(SETTINGS.sdFontFamilyName) - 1);
    SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
    sdFontSystem.ensureLoaded(renderer);
    LOG_INF("SMOKE", "Validated %s SD font size reload: 12 pt height %d, 14 pt height %d", requestedFamily,
            lineHeight12, lineHeight14);
  }

  static void verifySdFontFamilyCount() {
    const char* requestedCount = std::getenv("CROSSINK_SIMULATOR_EXPECT_FONT_FAMILY_COUNT");
    if (requestedCount == nullptr || requestedCount[0] == '\0') return;

    const int expectedCount = std::atoi(requestedCount);
    const int actualCount = sdFontSystem.registry().getFamilyCount();
    if (actualCount != expectedCount) {
      fail("SD font registry discovered %d families instead of %d", actualCount, expectedCount);
    }
    LOG_INF("SMOKE", "Validated SD font registry family count: %d", actualCount);
  }

  static void saveFontPreviewScreenshot(const char* path) {
    if (!ScreenshotUtil::saveFramebufferAsBmp(path, renderer.getFrameBuffer(), renderer.getDisplayWidth(),
                                              renderer.getDisplayHeight())) {
      fail("Could not save font preview smoke screenshot: %s", path);
    }
  }

  static void renderRequestedFontPreviewScreenshots() {
    if (std::getenv("CROSSINK_SIMULATOR_SMOKE_SCREENSHOT_FONT_PREVIEWS") == nullptr) return;

    auto& registry = sdFontSystem.registry();
    const SdCardFontFamilyInfo* baseline = registry.findFamily("Bookerly");
    if (!baseline) fail("Font preview fixture is missing Bookerly");
    const uint8_t baseline16 = requireFontSizeIndex(*baseline, 16);

    const uint8_t originalFontFamily = SETTINGS.fontFamily;
    const uint8_t originalFontSize = SETTINGS.fontSize;
    const uint8_t originalSdFontPointSize = SETTINGS.sdFontPointSize;
    char originalSdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName)] = {};
    strncpy(originalSdFontFamilyName, SETTINGS.sdFontFamilyName, sizeof(originalSdFontFamilyName) - 1);

    struct PreviewTarget {
      const char* familyName;
      const char* beforePath;
      const char* afterPath;
    };
    constexpr std::array<PreviewTarget, 2> targets = {{
        {"Tinos", "/smoke-font-tinos-before.bmp", "/smoke-font-tinos-after.bmp"},
        {"Vollkorn", "/smoke-font-vollkorn-before.bmp", "/smoke-font-vollkorn-after.bmp"},
    }};

    const int familyCount = registry.getFamilyCount();
    for (const auto& target : targets) {
      const SdCardFontFamilyInfo* targetFamily = registry.findFamily(target.familyName);
      if (!targetFamily) fail("Font preview fixture is missing %s", target.familyName);
      const std::vector<uint8_t> targetSizes = targetFamily->availableSizes();
      if (targetSizes.size() <= baseline16) {
        fail("Font preview fixture %s needs more than %u sizes", target.familyName, baseline16);
      }

      // Recreate the old index-retention bug: Bookerly's index 2 means 16 pt,
      // while the same index in an eight-size family means 10 pt.
      selectSdFont(*targetFamily, baseline16);
      if (targetSizes[SETTINGS.fontSize] == 16) {
        fail("Font preview fixture %s does not recreate the stale-index regression", target.familyName);
      }
      FontSelectionActivity before(renderer, mappedInputManager, &registry);
      before.onEnter();
      before.render(RenderLock{});
      saveFontPreviewScreenshot(target.beforePath);
      before.onExit();

      // Exercise the real picker path. It must translate Bookerly's 16 pt to
      // the target family's own index for 16 pt before loading the preview.
      selectSdFont(*baseline, baseline16);
      FontSelectionActivity after(renderer, mappedInputManager, &registry);
      after.onEnter();
      bool foundTarget = false;
      // The picker is grouped by category and contains non-selectable section
      // headers, so navigate through its real control path instead of assuming
      // registry index distance equals visible row distance.
      for (int i = 0; i < familyCount + 8; ++i) {
        mappedInputManager.simulatorClearInputFrame();
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Down);
        after.loop();
        mappedInputManager.simulatorClearInputFrame();
        mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Confirm);
        after.loop();
        mappedInputManager.simulatorClearInputFrame();
        if (strcmp(SETTINGS.sdFontFamilyName, target.familyName) == 0) {
          foundTarget = true;
          break;
        }
      }

      if (!foundTarget) {
        fail("Font picker did not preview requested family %s", target.familyName);
      }
      if (SETTINGS.fontSize >= targetSizes.size() || targetSizes[SETTINGS.fontSize] != 16) {
        fail("Font picker previewed %s at %u pt instead of 16 pt", target.familyName,
             SETTINGS.fontSize < targetSizes.size() ? targetSizes[SETTINGS.fontSize] : 0);
      }
      after.render(RenderLock{});
      saveFontPreviewScreenshot(target.afterPath);
      after.onExit();
    }

    SETTINGS.fontFamily = originalFontFamily;
    SETTINGS.fontSize = originalFontSize;
    SETTINGS.sdFontPointSize = originalSdFontPointSize;
    strncpy(SETTINGS.sdFontFamilyName, originalSdFontFamilyName, sizeof(SETTINGS.sdFontFamilyName) - 1);
    SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
    sdFontSystem.ensureLoaded(renderer);
    LOG_INF("SMOKE", "Saved font preview regression screenshots");
  }

  static void renderRequestedSingleFontPreviewScreenshot() {
    const char* familyName = std::getenv("CROSSINK_SIMULATOR_SMOKE_FONT_PREVIEW_FAMILY");
    if (familyName == nullptr || familyName[0] == '\0') return;

    auto& registry = sdFontSystem.registry();
    const SdCardFontFamilyInfo* family = registry.findFamily(familyName);
    if (!family) fail("Single font preview fixture is missing %s", familyName);

    const uint8_t originalFontFamily = SETTINGS.fontFamily;
    const uint8_t originalFontSize = SETTINGS.fontSize;
    const uint8_t originalSdFontPointSize = SETTINGS.sdFontPointSize;
    char originalSdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName)] = {};
    strncpy(originalSdFontFamilyName, SETTINGS.sdFontFamilyName, sizeof(originalSdFontFamilyName) - 1);

    const char* requestedPointSize = std::getenv("CROSSINK_SIMULATOR_SMOKE_FONT_PREVIEW_SIZE");
    const int pointSize = requestedPointSize && requestedPointSize[0] != '\0' ? std::atoi(requestedPointSize) : 16;
    if (pointSize < 1 || pointSize > 255) fail("Invalid font preview size: %d", pointSize);

    const char* currentFamilyName = std::getenv("CROSSINK_SIMULATOR_SMOKE_FONT_CURRENT_FAMILY");
    if (currentFamilyName != nullptr && currentFamilyName[0] != '\0') {
      if (strcmp(currentFamilyName, familyName) == 0) {
        fail("Current and preview font families must differ: %s", familyName);
      }
      const SdCardFontFamilyInfo* currentFamily = registry.findFamily(currentFamilyName);
      if (!currentFamily) fail("Single font current fixture is missing %s", currentFamilyName);
      selectSdFont(*currentFamily, requireFontSizeIndex(*currentFamily, 16));
    } else {
      uint8_t baselineStoredSize = 0;
      bool foundBaselineSize = false;
      for (uint8_t i = 0; i < CrossPointSettings::FONT_SIZE_COUNT; ++i) {
        const auto size = static_cast<CrossPointSettings::FONT_SIZE>(i);
        if (CrossPointSettings::getReaderFontPointSize(size) != 16) continue;
        const uint8_t stored = CrossPointSettings::getStoredReaderFontSize(size);
        if (stored == 0xFF) continue;
        baselineStoredSize = stored;
        foundBaselineSize = true;
        break;
      }
      if (!foundBaselineSize) fail("Single font preview fixture is missing the built-in 16 pt baseline");
      SETTINGS.fontFamily = CrossPointSettings::LEXENDDECA;
      SETTINGS.fontSize = baselineStoredSize;
      SETTINGS.sdFontPointSize = 0;
      SETTINGS.sdFontFamilyName[0] = '\0';
      sdFontSystem.ensureLoaded(renderer);
    }

    FontSelectionActivity preview(renderer, mappedInputManager, &registry);
    preview.onEnter();
    bool foundTarget = false;
    const int familyCount = registry.getFamilyCount();
    for (int i = 0; i < familyCount + 12; ++i) {
      mappedInputManager.simulatorClearInputFrame();
      mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Down);
      preview.loop();
      mappedInputManager.simulatorClearInputFrame();
      mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Confirm);
      preview.loop();
      mappedInputManager.simulatorClearInputFrame();
      if (strcmp(SETTINGS.sdFontFamilyName, familyName) == 0) {
        foundTarget = true;
        break;
      }
    }
    if (!foundTarget) fail("Font picker did not preview requested family %s", familyName);

    const char* expectedSyntheticMask = std::getenv("CROSSINK_SIMULATOR_EXPECT_SYNTHETIC_STYLE_MASK");
    if (expectedSyntheticMask != nullptr && expectedSyntheticMask[0] != '\0') {
      uint8_t actualMask = 0;
      const int fontId = SETTINGS.getReaderFontId();
      constexpr std::array<EpdFontFamily::Style, 4> styles = {
          EpdFontFamily::REGULAR,
          EpdFontFamily::BOLD,
          EpdFontFamily::ITALIC,
          EpdFontFamily::BOLD_ITALIC,
      };
      for (uint8_t style = 0; style < styles.size(); ++style) {
        if (renderer.isSyntheticFontStyle(fontId, styles[style])) {
          actualMask |= static_cast<uint8_t>(1u << style);
        }
      }
      const uint8_t expectedMask = static_cast<uint8_t>(std::strtoul(expectedSyntheticMask, nullptr, 0));
      if (actualMask != expectedMask) {
        fail("Synthetic style mask for %s was 0x%X instead of 0x%X", familyName, actualMask, expectedMask);
      }
      LOG_INF("SMOKE", "Validated %s synthetic style mask: 0x%X", familyName, actualMask);
    }

    const std::string previewFamilyName = familyName;
    const bool usesCompactDyslexicSpecimen = previewFamilyName.find("Dyslex") != std::string::npos ||
                                             previewFamilyName.find("dyslex") != std::string::npos ||
                                             previewFamilyName.find("Disleks") != std::string::npos ||
                                             previewFamilyName.find("disleks") != std::string::npos;
    const int expectedPreviewPointSize = usesCompactDyslexicSpecimen ? 14 : std::min(pointSize, 16);
    if (sdFontSystem.currentPointSize() != expectedPreviewPointSize) {
      fail("Font picker previewed %s at %u pt instead of capped %d pt", familyName, sdFontSystem.currentPointSize(),
           expectedPreviewPointSize);
    }
    preview.render(RenderLock{});
    saveFontPreviewScreenshot("/smoke-font-preview.bmp");

    mappedInputManager.simulatorClearInputFrame();
    mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Confirm);
    preview.loop();
    mappedInputManager.simulatorClearInputFrame();
    if (sdFontSystem.currentPointSize() != pointSize || SETTINGS.sdFontPointSize != pointSize) {
      fail("Selecting %s from its %d pt specimen did not preserve the %d pt reading size", familyName,
           expectedPreviewPointSize, pointSize);
    }
    preview.onExit();

    SETTINGS.fontFamily = originalFontFamily;
    SETTINGS.fontSize = originalFontSize;
    SETTINGS.sdFontPointSize = originalSdFontPointSize;
    strncpy(SETTINGS.sdFontFamilyName, originalSdFontFamilyName, sizeof(SETTINGS.sdFontFamilyName) - 1);
    SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
    sdFontSystem.ensureLoaded(renderer);
    LOG_INF("SMOKE", "Saved %s %d pt specimen and preserved %d pt reading size", familyName, expectedPreviewPointSize,
            pointSize);
  }

  [[noreturn]] static void fail(const char* message) {
    LOG_ERR("SMOKE", "%s", message);
    std::_Exit(2);
  }

  template <typename... Args>
  [[noreturn]] static void fail(const char* format, Args... args) {
    logPrintf("ERR", "SMOKE", format, args...);
    logPrintf("ERR", "SMOKE", "\n");
    std::_Exit(2);
  }

  static void renderCurrentStep(const char* name) {
    LOG_INF("SMOKE", "Rendering %s", name);
    if (activityManager.requestUpdateAndWait() != RequestUpdateResult::Rendered) {
      fail("Render was rejected for %s", name);
    }
    if (std::strcmp(name, "File Browser") == 0 &&
        std::getenv("CROSSINK_SIMULATOR_SMOKE_SCREENSHOT_FILE_BROWSER") != nullptr) {
      // Thumbnail generation can queue its own repaint while the first requested
      // frame is still settling. Drain a few complete frames so the screenshot
      // cannot capture the converter's temporary framebuffer contents.
      constexpr int kSettledFileBrowserRenders = 4;
      for (int i = 0; i < kSettledFileBrowserRenders; ++i) {
        if (activityManager.requestUpdateAndWait() != RequestUpdateResult::Rendered) {
          fail("Settling File Browser render %d was rejected", i + 1);
        }
      }
      RenderLock screenshotLock;
      if (!ScreenshotUtil::saveFramebufferAsBmp("/smoke-file-browser.bmp", renderer.getFrameBuffer(),
                                                renderer.getDisplayWidth(), renderer.getDisplayHeight())) {
        fail("Could not save File Browser smoke screenshot");
      }
    }
  }

  static void verifyFilesCacheGridLayoutSetting() {
    const auto allSettings = getSettingsList(&sdFontSystem.registry());
    const auto filesCacheSettings = buildSystemFilesCacheSettingsList(allSettings);
    const auto gridLayout = std::find_if(filesCacheSettings.begin(), filesCacheSettings.end(), [](const auto& setting) {
      return setting.nameId == StrId::STR_FILE_BROWSER_GRID_LAYOUT;
    });
    if (gridLayout == filesCacheSettings.end() || gridLayout->type != SettingType::ENUM ||
        gridLayout->enumValues.size() != CrossPointSettings::FILE_BROWSER_GRID_LAYOUT_COUNT) {
      fail("Files & Cache grid-layout picker is missing or incomplete");
    }
  }

  static void verifyKOReaderSyncLauncherPlacement() {
    const uint8_t placement = LAUNCHER_LAYOUT.placement(LauncherItem::KOReaderSync);
    if (placement != LAUNCHER_APPS || launcherItemLabel(LauncherItem::KOReaderSync) != StrId::STR_KOREADER_SYNC) {
      fail("KOReader account setup must remain Apps-only");
    }
  }

  static void verifyTimeLeftReliabilityContract() {
    BookReadingStats stats;
    stats.avgSecondsPerForwardPage = 30;
    stats.paceSampleCount = BookReadingStats::MIN_RELIABLE_TIME_LEFT_PACE_SAMPLES;
    stats.totalReadingSeconds = BookReadingStats::MIN_RELIABLE_TIME_LEFT_READING_SECONDS - 1;
    if (stats.hasReliableTimeLeftBasis()) {
      fail("Time-left estimate accepted less than ten minutes of reading");
    }

    stats.totalReadingSeconds = BookReadingStats::MIN_RELIABLE_TIME_LEFT_READING_SECONDS;
    stats.paceSampleCount = BookReadingStats::MIN_RELIABLE_TIME_LEFT_PACE_SAMPLES - 1;
    if (stats.hasReliableTimeLeftBasis()) {
      fail("Time-left estimate accepted fewer than three pace samples");
    }

    stats.paceSampleCount = BookReadingStats::MIN_RELIABLE_TIME_LEFT_PACE_SAMPLES;
    if (!stats.hasReliableTimeLeftBasis()) {
      fail("Time-left estimate rejected a reliable reading sample");
    }
  }

  static bool copySmokeFile(const char* sourcePath, const char* destinationPath) {
    FsFile source;
    if (!Storage.openFileForRead("SMOKE", sourcePath, source)) return false;
    FsFile destination;
    if (!Storage.openFileForWrite("SMOKE", destinationPath, destination)) {
      source.close();
      return false;
    }
    uint8_t buffer[128];
    bool ok = true;
    while (source.available() > 0) {
      const int count = source.read(buffer, sizeof(buffer));
      if (count <= 0 || destination.write(buffer, static_cast<size_t>(count)) != static_cast<size_t>(count)) {
        ok = false;
        break;
      }
    }
    source.close();
    destination.flush();
    ok = ok && destination.sync() && destination.close();
    return ok;
  }

  // Runs only when real synced_* peer files were staged into Duet's state root.
  // (CROSSINK_SMOKE_SYNC_FIXTURES). Replays the exact stats-screen entry
  // sequence that consumes peer files on hardware.
  static void verifyPeerSyncMergeRepro() {
    FsFile dir = Storage.open(DUET_STATE_ROOT_PATH "/synced_journals");
    const bool hasFixtures = dir && dir.isDirectory();
    if (dir) dir.close();
    if (!hasFixtures) return;
    LOG_INF("SMOKE", "[SMOKE] peer sync merge repro: begin");

    if (!GlobalReadingStats::hasSyncedStats()) {
      LOG_INF("SMOKE", "[SMOKE] peer sync merge repro: no valid peer global stats");
    }
    const GlobalReadingStats aggregated = GlobalReadingStats::loadAggregated();
    (void)aggregated;

    const std::unique_ptr<ReadingJournal> journal = ReadingJournal::loadAggregated();
    if (!journal) fail("Peer journal aggregation returned null");

    constexpr char reproCachePath[] = DUET_BOOKS_ROOT_PATH "/epub_4242424242";
    BookReadingStats stats = BookReadingStats::load(reproCachePath);
    LibraryInsights::mergeSyncedBookStats(reproCachePath, stats);
    LibraryBookStatus status;
    LibraryInsights::lookupBookStatus(reproCachePath, status);

    LOG_INF("SMOKE", "[SMOKE] peer sync merge repro: complete");
  }

  static void verifyDetailedBookStatsSyncContract() {
    constexpr char originalCachePath[] = DUET_BOOKS_ROOT_PATH "/epub_4242424242";
    constexpr char movedCachePath[] = DUET_BOOKS_ROOT_PATH "/epub_4343434343";
    constexpr char detailPath[] = DUET_STATE_ROOT_PATH "/library_book_details_v1.bin";
    constexpr char detailCleanPath[] = DUET_STATE_ROOT_PATH "/library_book_details_v1.clean";
    constexpr char aliasPath[] = DUET_STATE_ROOT_PATH "/library_book_aliases_v1.bin";
    constexpr char peerPath[] = DUET_STATE_ROOT_PATH "/synced_book_details/device_010203040506.bin";
    Storage.mkdir(DUET_BOOKS_ROOT_PATH "");
    Storage.mkdir(DUET_STATE_ROOT_PATH "");
    Storage.mkdir(originalCachePath);
    Storage.mkdir(movedCachePath);
    Storage.remove(aliasPath);
    Storage.remove(detailCleanPath);
    LibraryInsights::invalidateCache();

    BookReadingStats peer;
    peer.sessionCount = 10;
    peer.totalReadingSeconds = 1000;
    peer.countedSessionSeconds = 800;
    peer.totalPagesTurned = 50;
    peer.avgSecondsPerForwardPage = 20;
    peer.paceSampleCount = 10;
    peer.estimatedTimeLeftSeconds = 900;
    peer.latestSessionReadingSeconds = 300;
    peer.latestSessionScreenPages = 12;
    peer.latestSessionDayIndex = 100;
    peer.latestSessionStartMinute = 60;
    peer.startDate = {2026, 7, 1};
    peer.isCompleted = true;
    peer.finishedDate = {2026, 7, 22};
    peer.finishedDateManual = true;
    peer.timeOfDaySeconds[0] = 1000;
    peer.dayOfWeekSeconds[1] = 1000;
    peer.save(originalCachePath);
    if (!LibraryInsights::publishLocalDetailedBookStatsForSync() || !copySmokeFile(detailPath, peerPath)) {
      fail("Detailed per-book stats snapshot could not be published");
    }
    BookReadingStats::remove(originalCachePath);
    if (!LibraryInsights::registerMovedBookStatsAlias(originalCachePath, movedCachePath) ||
        LibraryInsights::keyForCachePath(originalCachePath) != LibraryInsights::keyForCachePath(movedCachePath)) {
      fail("Moved-book stats alias did not preserve the canonical key");
    }

    BookReadingStats local;
    local.sessionCount = 2;
    local.totalReadingSeconds = 200;
    local.countedSessionSeconds = 100;
    local.totalPagesTurned = 5;
    local.avgSecondsPerForwardPage = 40;
    local.paceSampleCount = 2;
    local.estimatedTimeLeftSeconds = 500;
    local.latestSessionReadingSeconds = 120;
    local.latestSessionScreenPages = 4;
    local.latestSessionDayIndex = 101;
    local.latestSessionStartMinute = 10;
    local.startDate = {2026, 7, 2};
    local.timeOfDaySeconds[1] = 200;
    local.dayOfWeekSeconds[2] = 200;
    local.save(movedCachePath);

    BookReadingStats merged = BookReadingStats::load(movedCachePath);
    LibraryInsights::mergeSyncedBookStats(movedCachePath, merged);
    if (merged.sessionCount != 12 || merged.totalReadingSeconds != 1200 || merged.countedSessionSeconds != 900 ||
        merged.totalPagesTurned != 55 || merged.avgSecondsPerForwardPage != 23 || merged.paceSampleCount != 12 ||
        merged.latestSessionDayIndex != 101 || merged.latestSessionReadingSeconds != 120 ||
        merged.startDate.year != 2026 || merged.startDate.month != 7 || merged.startDate.day != 1 ||
        !merged.isCompleted || merged.finishedDate.year != 2026 || merged.finishedDate.month != 7 ||
        merged.finishedDate.day != 22 || !merged.finishedDateManual || merged.timeOfDaySeconds[0] != 1000 ||
        merged.timeOfDaySeconds[1] != 200 || merged.dayOfWeekSeconds[1] != 1000 || merged.dayOfWeekSeconds[2] != 200) {
      fail("Detailed per-book stats did not merge local and peer fields correctly");
    }

    Storage.remove(peerPath);
    Storage.remove(detailPath);
    Storage.remove(detailCleanPath);
    BookReadingStats::remove(originalCachePath);
    BookReadingStats::remove(movedCachePath);
    Storage.remove(aliasPath);
    LibraryInsights::invalidateCache();
    LOG_INF("SMOKE", "Validated moved-book detailed stats snapshot and aggregate merge");
  }

  static void verifyLightweightEpubProgress() {
    const char* bookPath = std::getenv("CROSSINK_SIMULATOR_SMOKE_BOOK");
    if (bookPath == nullptr || bookPath[0] == '\0') return;
    // A single-font media run deliberately changes the reader layout identity
    // before the EPUB smoke path. The ordinary X3/X4 smoke runs cover this
    // progress contract without that media-only state change.
    if (std::getenv("CROSSINK_SIMULATOR_SMOKE_FONT_PREVIEW_FAMILY") != nullptr) return;

    const std::string cachePath = Epub::cachePathForFilePath(bookPath, DUET_BOOKS_ROOT_PATH "");
    const std::string percentPath = cachePath + "/progress_pct.bin";
    if (Storage.exists(percentPath.c_str())) Storage.remove(percentPath.c_str());

    RecentBook book{bookPath, "Smoke Test", "Smoke Author", ""};
    const float rebuiltProgress = RecentBookProgress::loadPercent(book);
    if (rebuiltProgress < 0.0f || !Storage.exists(percentPath.c_str())) {
      fail("Lightweight EPUB progress could not rebuild from the metadata cache");
    }

    CurrentBookStatsTarget target;
    if (!CurrentBookStats::loadLastActive(target) || target.cachePath != cachePath || target.progressPercent < 0.0f) {
      fail("Home stats could not resolve the last active book through lightweight progress");
    }

    LOG_INF("SMOKE", "Validated lightweight Home/stats progress at %.2f%%", target.progressPercent);
  }

  static void verifyCoverBackgroundRefreshContract() {
    FileBrowserActivity browser(renderer, mappedInputManager, "/books");
    if (!browser.simulatorVerifyCoverBackgroundRefreshCoalescing()) {
      fail("Hydrated cover repaint was lost behind a pending cursor refresh");
    }
    LOG_INF("SMOKE", "Validated cover hydration repaint coalescing");
  }

  void queueStep(const char* name, SmokeStep nextStep, int framesToSettle = 3) {
    activeStepName = name;
    settleFrames = framesToSettle;
    step = nextStep;
  }

  void tickImpl() {
    mappedInputManager.simulatorClearInputFrame();

    if (settleFrames > 0) {
      --settleFrames;
      if (settleFrames == 0 && activeStepName != nullptr) {
        renderCurrentStep(activeStepName);
        activeStepName = nullptr;
      }
      return;
    }

    switch (step) {
      case SmokeStep::Start:
        LOG_INF("SMOKE", "Starting simulator smoke test");
        if (!CrossPointSettings::verifySleepTimeoutMigrationContract()) {
          fail("Sleep timeout migration contract failed");
        }
        if (!CrossPointSettings::verifySleepScreenMigrationContract()) {
          fail("Sleep screen migration contract failed");
        }
        verifySdFontFamilyCount();
        verifySdFontSizeContract();
        verifyFilesCacheGridLayoutSetting();
        verifyKOReaderSyncLauncherPlacement();
        verifyTimeLeftReliabilityContract();
        verifyCoverBackgroundRefreshContract();
        verifyDetailedBookStatsSyncContract();
        verifyPeerSyncMergeRepro();
        applyRequestedTheme();
        applyRequestedFileBrowserDisplay();
        applyRequestedFileBrowserGridLayout();
        applyRequestedSleepScreen();
        verifyLibraryCatalogContract();
        verifyEpubCachePathContract();
        renderRequestedDashboardHomeScreenshot();
        renderRequestedMinimalHomeMenuScreenshot();
        renderRequestedReadingHomeScreenshot();
        renderRequestedStatsScreenshots();
        renderRequestedBookInfoScreenshot();
        renderRequestedLibrarySearchScreenshot();
        renderRequestedLibraryAutocompleteScreenshot();
        renderRequestedFeatureScreenshots();
        renderRequestedFontPreviewScreenshots();
        renderRequestedSingleFontPreviewScreenshot();
        activityManager.goHome();
        queueStep("Home", SmokeStep::Home);
        break;

      case SmokeStep::Home:
        activityManager.goToFileBrowser("/books");
        queueStep("File Browser", SmokeStep::FileBrowser);
        break;

      case SmokeStep::FileBrowser:
        if (coverBrowserNavigationRequested()) {
          buildFileBrowserInputScript();
          step = SmokeStep::FileBrowserInput;
          break;
        }
        activityManager.goToRecentBooks();
        queueStep("Recent Books", SmokeStep::RecentBooks);
        break;

      case SmokeStep::FileBrowserInput:
        runInputScript(SmokeStep::FileBrowserAfterInput);
        break;

      case SmokeStep::FileBrowserAfterInput:
        activityManager.goToRecentBooks();
        queueStep("Recent Books", SmokeStep::RecentBooks);
        break;

      case SmokeStep::RecentBooks:
        activityManager.goToSettings();
        queueStep("Settings", SmokeStep::Settings);
        break;

      case SmokeStep::Settings:
        activityManager.replaceActivity(std::make_unique<ReaderOptionsActivity>(renderer, mappedInputManager));
        queueStep("Reader Options", SmokeStep::ReaderOptions);
        break;

      case SmokeStep::ReaderOptions:
        activityManager.replaceActivity(
            std::make_unique<EpubReaderMenuActivity>(renderer, mappedInputManager, "Smoke Test", 1, 1, 0,
                                                     SETTINGS.orientation, false, false, false, false, false));
        queueStep("Reader Menu", SmokeStep::ReaderMenu);
        break;

      case SmokeStep::ReaderMenu:
        activityManager.goToSleep();
        queueStep("Sleep", SmokeStep::Sleep);
        break;

      case SmokeStep::Sleep: {
        const char* bookPath = std::getenv("CROSSINK_SIMULATOR_SMOKE_BOOK");
        if (bookPath == nullptr || bookPath[0] == '\0') {
          LOG_INF("SMOKE", "Skipping Reader step; CROSSINK_SIMULATOR_SMOKE_BOOK is not set");
          step = SmokeStep::Reader;
          break;
        }
        if (!Storage.exists(bookPath)) {
          fail("Smoke test book is missing: %s", bookPath);
        }
        activityManager.goToReader(bookPath, true);
        queueStep("Reader", SmokeStep::Reader, 8);
        break;
      }

      case SmokeStep::Reader:
        buildReaderInputScript();
        step = SmokeStep::ReaderInput;
        break;

      case SmokeStep::ReaderInput:
        runInputScript(SmokeStep::ProgressCache);
        break;

      case SmokeStep::ProgressCache:
        activityManager.goHome();
        verifyLightweightEpubProgress();
        queueStep("Home after stats progress reload", SmokeStep::Done);
        break;

      case SmokeStep::Done:
        LOG_INF("SMOKE", "Simulator smoke test passed");
        std::_Exit(0);
    }
  }

  static ScriptAction press(MappedInputManager::Button button) { return {ScriptActionType::Press, button, nullptr, 0}; }

  static ScriptAction release(MappedInputManager::Button button) {
    return {ScriptActionType::Release, button, nullptr, 0};
  }

  static ScriptAction render(const char* label, int framesToSettle = 3) {
    return {ScriptActionType::Render, MappedInputManager::Button::Back, label, framesToSettle};
  }

  static ScriptAction capture(const char* label) {
    return {ScriptActionType::Capture, MappedInputManager::Button::Back, label, 0};
  }

  static ScriptAction wait(const int milliseconds) {
    return {ScriptActionType::Wait, MappedInputManager::Button::Back, nullptr, milliseconds};
  }

  static ScriptAction setFontFamily(const CrossPointSettings::FONT_FAMILY family) {
    return {ScriptActionType::SetFontFamily, MappedInputManager::Button::Back, nullptr, static_cast<int>(family)};
  }

  void addTap(MappedInputManager::Button button) {
    inputScript.push_back(press(button));
    inputScript.push_back(release(button));
  }

  void buildFileBrowserInputScript() {
    inputScript.clear();
    scriptIndex = 0;
    scriptWaitUntil = 0UL;

    // Move immediately, before the idle cover queue has had time to warm the
    // page. This guards the real-device contract that placeholders never block
    // cursor input.
    addTap(MappedInputManager::Button::Right);
    inputScript.push_back(capture("/smoke-file-browser-fast-right.bmp"));
    // Let the initial visible page hydrate before checking the page-crossing
    // contract. The real browser has an intentionally conservative idle gate.
    const int hydrationWaitMs =
        SETTINGS.fileBrowserDisplay == CrossPointSettings::FILE_BROWSER_DISPLAY_CAROUSEL ? 3000 : 3200;
    inputScript.push_back(wait(hydrationWaitMs));
    inputScript.push_back(capture("/smoke-file-browser-marquee.bmp"));

    addTap(MappedInputManager::Button::Down);
    inputScript.push_back(capture("/smoke-file-browser-fast-down.bmp"));
    int gridRows = 2;
    if (SETTINGS.fileBrowserGridLayout == CrossPointSettings::FILE_BROWSER_GRID_3X3) {
      gridRows = 3;
    } else if (SETTINGS.fileBrowserGridLayout == CrossPointSettings::FILE_BROWSER_GRID_4X4) {
      gridRows = 4;
    }
    for (int row = 1; row < gridRows; ++row) addTap(MappedInputManager::Button::Down);
    inputScript.push_back(capture("/smoke-file-browser-fast-page.bmp"));
    inputScript.push_back(wait(300));
    addTap(MappedInputManager::Button::Up);
    inputScript.push_back(wait(300));
    inputScript.push_back(capture("/smoke-file-browser-fast-back.bmp"));
    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("File Browser after leaving cover shelf", 3));
    inputScript.push_back(capture("/smoke-file-browser-folder-back.bmp"));
    LOG_INF("SMOKE", "Running cover-browser navigation script");
  }

  void buildReaderInputScript() {
    inputScript.clear();
    scriptIndex = 0;
    scriptWaitUntil = 0UL;

    // Leave the freshly painted reader idle long enough to exercise guarded
    // next-chapter pre-indexing before the scripted page turns interrupt it.
    inputScript.push_back(wait(verifyChapterTransition() ? 7000 : verifyReaderRelayout() ? 2500 : 1000));

    const int turns = pageTurnCount();
    for (int i = 0; i < turns; i++) {
      addTap(MappedInputManager::Button::PageForward);
      inputScript.push_back(render("Reader after page forward", 4));
    }
    if (verifyReaderRelayout()) {
      // Input must remain usable while the short preview is active. Only
      // after a genuine quiet interval should the full chapter cache replace it.
      inputScript.push_back(wait(6500));
      inputScript.push_back(render("Reader after idle chapter completion", 5));
    }
    if (verifyChapterTransition()) {
      inputScript.push_back(wait(6500));
      inputScript.push_back(render("Reader after chapter transition settled", 5));
    }

    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Quick Reader Menu opened over EPUB", 4));
    inputScript.push_back(capture("/smoke-reader-quick-menu.bmp"));

    // Exercise the inline line-spacing preset without leaving the overlay.
    const int lineSpacingIndex = halTiltSensor.isAvailable() ? 7 : 6;
    for (int i = 0; i < lineSpacingIndex; i++) addTap(MappedInputManager::Button::Down);
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Quick Reader Menu after line spacing change", 3));

    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("Reader after closing Quick Reader Menu", 5));
    if (verifyReaderRelayout()) {
      inputScript.push_back(wait(5000));
      inputScript.push_back(render("Reader after idle line-spacing relayout", 5));
      inputScript.push_back(setFontFamily(CrossPointSettings::BITTER));
      inputScript.push_back(render("Reader after font-family change preview", 5));
      inputScript.push_back(wait(5000));
      inputScript.push_back(render("Reader after idle font-family relayout", 5));
    }

    // Reopen the quick overlay and verify that More still reaches the complete menu.
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Quick Reader Menu reopened", 4));
    const int moreIndex = halTiltSensor.isAvailable() ? 9 : 8;
    for (int i = 0; i < moreIndex; i++) addTap(MappedInputManager::Button::Down);
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Full Reader Menu opened from Quick Reader Menu", 4));

    // Book Info is the eighth entry in the full Main menu (after Chapter,
    // Reader Options, Controls, Go To, Auto Turn, Reading Stats, and Sync).
    for (int i = 0; i < 8; i++) addTap(MappedInputManager::Button::Down);
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Book Info opened from Reader More", 6));
    inputScript.push_back(capture("/smoke-reader-book-info.bmp"));

    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("Reader after returning from Book Info", 4));

    LOG_INF("SMOKE", "Running reader input script with %d page turn(s)", turns);
  }

  void runInputScript(const SmokeStep completionStep) {
    if (scriptWaitUntil != 0UL) {
      if (static_cast<int32_t>(millis() - scriptWaitUntil) < 0) return;
      scriptWaitUntil = 0UL;
    }
    if (scriptIndex >= inputScript.size()) {
      step = completionStep;
      return;
    }

    const auto& action = inputScript[scriptIndex++];
    switch (action.type) {
      case ScriptActionType::Press:
        mappedInputManager.simulatorInjectPress(action.button);
        break;
      case ScriptActionType::Release:
        mappedInputManager.simulatorInjectRelease(action.button);
        break;
      case ScriptActionType::Render:
        // Resume whichever script requested the repaint. File-browser scripts
        // and reader scripts share this runner, so hard-coding ReaderInput can
        // skip the rest of the browser flow after its first rendered action.
        queueStep(action.label, step, action.settleFrames);
        break;
      case ScriptActionType::Capture:
        if (activityManager.requestUpdateAndWait() != RequestUpdateResult::Rendered) {
          fail("Render before scripted capture was rejected: %s", action.label);
        }
        {
          RenderLock screenshotLock;
          if (!ScreenshotUtil::saveFramebufferAsBmp(action.label, renderer.getFrameBuffer(), renderer.getDisplayWidth(),
                                                    renderer.getDisplayHeight())) {
            fail("Could not capture scripted framebuffer: %s", action.label);
          }
        }
        LOG_INF("SMOKE", "Captured scripted framebuffer: %s", action.label);
        break;
      case ScriptActionType::Wait:
        scriptWaitUntil = millis() + static_cast<unsigned long>(std::max(0, action.settleFrames));
        break;
      case ScriptActionType::SetFontFamily:
        SETTINGS.sdFontFamilyName[0] = '\0';
        SETTINGS.fontFamily = static_cast<uint8_t>(action.settleFrames);
        LOG_INF("SMOKE", "Changed reader font family to %d", action.settleFrames);
        break;
    }
  }
};

SimulatorSmokeTest smokeTest;

}  // namespace

void runSimulatorSmokeTestTick() { smokeTest.tick(); }

#endif
