#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <InflateReader.h>
#include <MemoryBudget.h>
#include <ScratchWorkspace.h>
#include <Serialization.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cmath>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "../reader/BookReadingStats.h"
#include "../reader/BookStatsActivity.h"
#include "../reader/EpubReaderUtils.h"
#include "AchievementStore.h"
#include "BookmarkStore.h"
#include "ClippingStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "CurrentBookStats.h"
#include "GlobalActions.h"
#include "LauncherCatalog.h"
#include "LauncherLayoutStore.h"
#include "LibrarySearchActivity.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBookProgress.h"
#include "RecentBooksStore.h"
#include "SavedItemsHomeActivity.h"
#include "activities/apps/AchievementsActivity.h"
#include "activities/apps/DictionaryActivity.h"
#include "activities/apps/FavoritesActivity.h"
#include "activities/apps/IfFoundActivity.h"
#include "activities/apps/ReadMeActivity.h"
#include "activities/apps/ScreenCleanActivity.h"
#include "activities/apps/TetrisActivity.h"
#include "activities/apps/UtilitiesActivity.h"
#include "activities/reader/LibraryInsights.h"
#include "activities/reader/ReadingJournal.h"
#include "activities/reader/ReadingStatsUtils.h"
#include "activities/settings/KOReaderSettingsActivity.h"
#include "components/UITheme.h"
#include "components/themes/dashboard/DashboardTheme.h"
#include "components/themes/lyra/LyraCarouselTheme.h"
#include "components/themes/minimal/MinimalTheme.h"
#include "components/themes/reading_home/ReadingHomeTheme.h"
#include "fontIds.h"

namespace {
constexpr uint32_t CAROUSEL_CACHE_MAGIC = 0x43434152;  // "CCAR"
constexpr uint16_t CAROUSEL_CACHE_VERSION = 6;
constexpr char CAROUSEL_CACHE_PATH[] = DUET_STATE_ROOT_PATH "/home_carousel_cache.bin";
constexpr char CAROUSEL_CACHE_TMP_PATH[] = DUET_STATE_ROOT_PATH "/home_carousel_cache.tmp";
constexpr uint32_t CAROUSEL_FRAME_MIN_FREE_AFTER_ALLOC = 64U * 1024U;
constexpr uint32_t CAROUSEL_FRAME_MIN_MAX_ALLOC_AFTER_ALLOC = 24U * 1024U;
constexpr int HOME_MIN_CACHED_RECENT_COUNT = 2;
constexpr unsigned long HOME_DEFERRED_METADATA_IDLE_MS = 450;
constexpr unsigned long HOME_DEFERRED_BOOK_CONTEXT_IDLE_MS = 260;
// Between per-book stats loads: long enough for the input edge flags of a
// tap to be observed by hasActiveHomeInput() and postpone the chain.
constexpr unsigned long HOME_DEFERRED_BOOK_STATS_STEP_MS = 60;
// Cover/carousel passes hold the RenderLock and pay per-book cache-dir
// scans even when every thumbnail already exists, so they only start after
// the user has left Home genuinely idle for a while.
constexpr unsigned long HOME_DEFERRED_COVER_IDLE_MS = 3500;
constexpr unsigned long HOME_DEFERRED_CAROUSEL_IDLE_MS = 2500;
constexpr unsigned long HOME_DEFERRED_ACHIEVEMENT_IDLE_MS = 2500;
constexpr unsigned long HOME_DEFERRED_AFTER_INPUT_IDLE_MS = 900;
constexpr uint32_t HOME_JOURNAL_LOAD_MIN_FREE = 72U * 1024U;
constexpr uint32_t HOME_JOURNAL_LOAD_MIN_MAX_ALLOC = 24U * 1024U;

struct HomeMenuEntry {
  const char* label;
  UIIcon icon;
  LauncherItem item = LauncherItem::Count;
  bool continueReading = false;
};

struct HomeMenuEntries {
  static constexpr int kCapacity = static_cast<int>(LauncherLayoutStore::ITEM_COUNT) + 1;
  std::array<HomeMenuEntry, kCapacity> entries{};
  int count = 0;

  void push(const HomeMenuEntry& entry) {
    if (count >= kCapacity) return;
    entries[count++] = entry;
  }

  int size() const { return count; }

  const HomeMenuEntry& operator[](int index) const { return entries[index]; }
};

struct CarouselCacheHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t frameCount;
  uint32_t frameBufferSize;
  uint64_t keyHash;
  uint16_t screenWidth;
  uint16_t screenHeight;
  uint16_t centerCoverW;
  uint16_t centerCoverH;
  uint16_t sideCoverW;
  uint16_t sideCoverH;
};

uint64_t fnvHash64(const std::string& s) {
  uint64_t hash = 14695981039346656037ull;
  for (char c : s) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 1099511628211ull;
  }
  return hash;
}

bool hasAnyBookStats(const BookReadingStats& stats) {
  return stats.sessionCount > 0 || stats.totalReadingSeconds > 0 || stats.totalPagesTurned > 0 || stats.isCompleted ||
         stats.startDate.isValid() || stats.finishedDate.isValid();
}

bool hasAnyGlobalStats(const GlobalReadingStats& stats) {
  return stats.totalSessions > 0 || stats.totalReadingSeconds > 0 || stats.totalPagesTurned > 0 ||
         stats.completedBooks > 0 || stats.displayLongestReadingStreak() > 0;
}

bool hasHeapForCarouselFrameCache() {
  return ESP.getFreeHeap() >= CAROUSEL_FRAME_MIN_FREE_AFTER_ALLOC &&
         ESP.getMaxAllocHeap() >= CAROUSEL_FRAME_MIN_MAX_ALLOC_AFTER_ALLOC;
}

void appendHashedFileStateToKey(std::string& key, const std::string& path) {
  FsFile file;
  if (!Storage.openFileForRead("HOME", path, file)) {
    key += "missing";
    key += '\0';
    return;
  }

  uint64_t hash = 14695981039346656037ull;
  size_t totalBytes = 0;
  uint8_t buffer[64];
  while (true) {
    const int bytesRead = file.read(buffer, sizeof(buffer));
    if (bytesRead <= 0) break;
    totalBytes += static_cast<size_t>(bytesRead);
    for (int i = 0; i < bytesRead; ++i) {
      hash ^= buffer[i];
      hash *= 1099511628211ull;
    }
  }
  file.close();

  char digest[48];
  snprintf(digest, sizeof(digest), "%zu:%" PRIu64, totalBytes, static_cast<uint64_t>(hash));
  key += digest;
  key += '\0';
}

std::string getRecentBookCachePath(const RecentBook& book) {
  if (FsHelpers::hasEpubExtension(book.path)) {
    return Epub::cachePathForFilePath(book.path, DUET_BOOKS_ROOT_PATH "");
  }
  if (FsHelpers::hasXtcExtension(book.path)) {
    return DUET_BOOKS_ROOT_PATH "/xtc_" + std::to_string(std::hash<std::string>{}(book.path));
  }
  if (FsHelpers::hasTxtExtension(book.path) || FsHelpers::hasMarkdownExtension(book.path)) {
    return DUET_BOOKS_ROOT_PATH "/txt_" + std::to_string(std::hash<std::string>{}(book.path));
  }
  return "";
}

BookReadingStats loadRecentBookStats(const RecentBook& book) {
  if (!FsHelpers::hasEpubExtension(book.path) && !FsHelpers::hasXtcExtension(book.path)) {
    return BookReadingStats{};
  }

  const std::string cachePath = getRecentBookCachePath(book);
  BookReadingStats stats = BookReadingStats::load(cachePath);
  LibraryInsights::mergeSyncedBookStats(cachePath, stats);
  return stats;
}

bool loadEpubHighlightedContext(const RecentBook& book, const bool loadProgress, const bool loadChapterTitle,
                                float* progressPercent, std::string* chapterTitle, uint32_t* wordCount = nullptr) {
  if (!FsHelpers::hasEpubExtension(book.path) || (!loadProgress && !loadChapterTitle && wordCount == nullptr)) {
    return false;
  }

  Epub epub(book.path, DUET_BOOKS_ROOT_PATH "");
  if (!epub.load(false, true)) {
    return false;
  }

  if (wordCount) {
    const uint32_t words = epub.getTotalWords();
    if (words > 0) {
      *wordCount = words;
    }
  }

  EpubReaderUtils::Progress progress;
  if (!EpubReaderUtils::loadProgress(epub, progress, "HOME")) {
    return false;
  }

  if (loadProgress && progressPercent) {
    if (progress.hasPageCount && progress.pageCount > 0) {
      const float chapterProgress =
          static_cast<float>(progress.pageNumber + 1) / static_cast<float>(progress.pageCount);
      *progressPercent =
          std::clamp(epub.calculateProgress(progress.spineIndex, chapterProgress) * 100.0f, 0.0f, 100.0f);
    } else {
      *progressPercent = -1.0f;
    }
  }

  if (loadChapterTitle && chapterTitle) {
    chapterTitle->clear();
    const int spineCount = epub.getSpineItemsCount();
    if (progress.spineIndex >= 0 && progress.spineIndex < spineCount) {
      const auto spineItem = epub.getSpineItem(progress.spineIndex);
      if (spineItem.tocIndex >= 0) {
        *chapterTitle = epub.getTocItem(spineItem.tocIndex).title;
      }
    }
    if (chapterTitle->empty() && spineCount > 0 && progress.spineIndex >= 0 && progress.spineIndex < spineCount) {
      char fallback[32];
      snprintf(fallback, sizeof(fallback), "%s %d/%d", tr(STR_CHAPTER), progress.spineIndex + 1, spineCount);
      *chapterTitle = fallback;
    }
  }

  return true;
}

void updateRecentBookCoverPath(const RecentBook& book, const std::string& coverBmpPath) {
  if (!RECENT_BOOKS.updateBook(book.path, book.title, book.author, coverBmpPath)) {
    LOG_ERR("HOME", "failed to update recent book metadata: %s", book.path.c_str());
  }
}

bool hasThumbnailPlaceholder(const std::string& coverBmpPath) {
  return coverBmpPath.find("[WIDTH]") != std::string::npos || coverBmpPath.find("[HEIGHT]") != std::string::npos;
}

std::string getReusableCoverPath(const RecentBook& book) {
  if (FsHelpers::hasEpubExtension(book.path)) {
    return Epub(book.path, DUET_BOOKS_ROOT_PATH "").getThumbBmpPath();
  }
  if (FsHelpers::hasXtcExtension(book.path)) {
    return Xtc(book.path, DUET_BOOKS_ROOT_PATH "").getThumbBmpPath();
  }
  return book.coverBmpPath;
}

bool ensureReusableCoverPath(RecentBook& book) {
  if (hasThumbnailPlaceholder(book.coverBmpPath)) {
    return false;
  }

  const std::string reusablePath = getReusableCoverPath(book);
  if (reusablePath.empty() || reusablePath == book.coverBmpPath) {
    return false;
  }

  book.coverBmpPath = reusablePath;
  updateRecentBookCoverPath(book, reusablePath);
  return true;
}

const char* savedItemsLabel(bool hasBookmarks, bool hasClippings) {
  if (hasBookmarks && hasClippings) return tr(STR_BOOKMARKS_AND_CLIPPINGS);
  if (hasClippings) return tr(STR_CLIPPINGS);
  return tr(STR_BOOKMARKS);
}

void appendHomeMenuItems(HomeMenuEntries& items, bool hasOpdsServers, bool hasReadingStats, bool hasBookmarks,
                         bool hasClippings) {
  (void)hasOpdsServers;
  (void)hasReadingStats;

  auto iconForItem = [](const LauncherItem item) {
    switch (item) {
      case LauncherItem::BrowseFiles:
        return Folder;
      case LauncherItem::SearchLibrary:
      case LauncherItem::OpdsBrowser:
      case LauncherItem::Dictionary:
        return Library;
      case LauncherItem::RecentBooks:
        return Recent;
      case LauncherItem::ReadingStats:
      case LauncherItem::ReadingHeatmap:
      case LauncherItem::ReadingProfile:
      case LauncherItem::NearbyStatsSync:
        return Chart;
      case LauncherItem::SavedItems:
      case LauncherItem::Favorites:
        return BookmarkIcon;
      case LauncherItem::FileTransfer:
      case LauncherItem::KOReaderSync:
        return Transfer;
      case LauncherItem::Settings:
        return Settings;
      case LauncherItem::Sleep:
      case LauncherItem::ReadMe:
      case LauncherItem::Apps:
      case LauncherItem::Achievements:
      case LauncherItem::Tetris:
      case LauncherItem::IfFound:
      case LauncherItem::ScreenClean:
      case LauncherItem::CustomizeHomeApps:
      case LauncherItem::Count:
      default:
        return File;
    }
  };

  const size_t itemCount = LAUNCHER_LAYOUT.count(LauncherSurface::Home);
  for (size_t i = 0; i < itemCount; ++i) {
    const LauncherItem item = LAUNCHER_LAYOUT.itemAt(LauncherSurface::Home, i);
    if (item == LauncherItem::Count || item == LauncherItem::CustomizeHomeApps) continue;
    const char* label = item == LauncherItem::SavedItems ? savedItemsLabel(hasBookmarks, hasClippings)
                                                         : I18N.get(launcherItemLabel(item));
    items.push({label, iconForItem(item), item, false});
  }
}

HomeMenuEntries buildHomeMenuItems(bool hasOpdsServers, bool hasReadingStats, bool hasBookmarks, bool hasClippings) {
  HomeMenuEntries items;
  appendHomeMenuItems(items, hasOpdsServers, hasReadingStats, hasBookmarks, hasClippings);
  return items;
}

HomeMenuEntries buildMinimalMenuItems(bool hasOpdsServers, bool hasReadingStats, bool hasBookmarks, bool hasClippings) {
  HomeMenuEntries items;
  appendHomeMenuItems(items, hasOpdsServers, hasReadingStats, hasBookmarks, hasClippings);
  return items;
}

HomeMenuEntries buildSelectableHomeMenuItems(bool hasOpdsServers, bool hasReadingStats, bool hasBookmarks,
                                             bool hasClippings, bool includeContinueReading) {
  HomeMenuEntries items;
  if (includeContinueReading) {
    items.push({tr(STR_CONTINUE_READING), Book, LauncherItem::Count, true});
  }
  appendHomeMenuItems(items, hasOpdsServers, hasReadingStats, hasBookmarks, hasClippings);
  return items;
}

LauncherItem launcherItemForInitialMenuItem(HomeMenuItem item) {
  switch (item) {
    case HomeMenuItem::FILE_BROWSER:
      return LauncherItem::BrowseFiles;
    case HomeMenuItem::RECENTS:
      return LauncherItem::RecentBooks;
    case HomeMenuItem::OPDS_BROWSER:
      return LauncherItem::OpdsBrowser;
    case HomeMenuItem::FILE_TRANSFER:
      return LauncherItem::FileTransfer;
    case HomeMenuItem::SETTINGS_MENU:
      return LauncherItem::Settings;
    case HomeMenuItem::NONE:
    default:
      return LauncherItem::Count;
  }
}

int findMenuItemIndex(const HomeMenuEntries& items, const HomeMenuItem initialItem) {
  const LauncherItem target = launcherItemForInitialMenuItem(initialItem);
  for (int i = 0; i < items.size(); ++i) {
    if ((initialItem == HomeMenuItem::NONE && items[i].continueReading) ||
        (initialItem != HomeMenuItem::NONE && items[i].item == target)) {
      return i;
    }
  }
  return -1;
}

bool isMinimalTheme() {
  return static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::MINIMAL;
}

bool isDashboardTheme() {
  return static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::DASHBOARD;
}

bool isReadingHomeTheme() {
  return static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::READING_HOME;
}

bool usesMinimalHomeInteraction() { return isMinimalTheme() || isDashboardTheme(); }

bool isAnyFrontButtonPressed(const MappedInputManager& mappedInput) {
  return mappedInput.isFrontButtonPressed(HalGPIO::BTN_BACK) ||
         mappedInput.isFrontButtonPressed(HalGPIO::BTN_CONFIRM) ||
         mappedInput.isFrontButtonPressed(HalGPIO::BTN_LEFT) || mappedInput.isFrontButtonPressed(HalGPIO::BTN_RIGHT);
}

bool homeWorkDeadlineReached(const unsigned long now, const unsigned long deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

int minimalHomeNavCount(const bool hasCurrentBook) { return hasCurrentBook ? 4 : 3; }

int minimalHomeCoverWidth(int coverHeight) {
  (void)coverHeight;
  return MinimalMetrics::homeCoverImageWidth;
}

int minimalHomeCoverHeight(int coverHeight) {
  (void)coverHeight;
  return MinimalMetrics::homeCoverImageHeight;
}

std::string minimalHomeCoverPath(const RecentBook& book, int coverHeight) {
  if (book.coverBmpPath.empty()) {
    return {};
  }
  if (FsHelpers::hasEpubExtension(book.path)) {
    return Epub(book.path, DUET_BOOKS_ROOT_PATH "")
        .getAdaptiveThumbBmpPath(minimalHomeCoverWidth(coverHeight), minimalHomeCoverHeight(coverHeight));
  }
  return UITheme::getCoverThumbPath(book.coverBmpPath, minimalHomeCoverWidth(coverHeight),
                                    minimalHomeCoverHeight(coverHeight));
}

int dashboardHomeCoverWidth(int coverHeight) {
  (void)coverHeight;
  return DashboardMetrics::homeCoverImageWidth;
}

int dashboardHomeCoverHeight(int coverHeight) {
  (void)coverHeight;
  return DashboardMetrics::homeCoverImageHeight;
}

std::string dashboardHomeCoverPath(const RecentBook& book, int coverHeight) {
  if (book.coverBmpPath.empty()) {
    return {};
  }
  if (FsHelpers::hasEpubExtension(book.path)) {
    return Epub(book.path, DUET_BOOKS_ROOT_PATH "")
        .getAdaptiveThumbBmpPath(dashboardHomeCoverWidth(coverHeight), dashboardHomeCoverHeight(coverHeight));
  }
  return UITheme::getCoverThumbPath(book.coverBmpPath, dashboardHomeCoverWidth(coverHeight),
                                    dashboardHomeCoverHeight(coverHeight));
}

int readingHomeCoverWidth(int coverHeight) {
  (void)coverHeight;
  return ReadingHomeTheme::kCoverWidth;
}

int readingHomeCoverHeight(int coverHeight) {
  (void)coverHeight;
  return ReadingHomeTheme::kCoverHeight;
}

std::string readingHomeCoverPath(const RecentBook& book, int coverHeight) {
  if (book.coverBmpPath.empty()) {
    return {};
  }
  if (FsHelpers::hasEpubExtension(book.path)) {
    return Epub(book.path, DUET_BOOKS_ROOT_PATH "")
        .getAdaptiveThumbBmpPath(readingHomeCoverWidth(coverHeight), readingHomeCoverHeight(coverHeight));
  }
  return UITheme::getCoverThumbPath(book.coverBmpPath, readingHomeCoverWidth(coverHeight),
                                    readingHomeCoverHeight(coverHeight));
}

void appendCarouselCoverStateToKey(std::string& key, const RecentBook& book) {
  key += book.path;
  key += '\0';
  key += book.coverBmpPath;
  key += '\0';

  if (book.coverBmpPath.empty()) {
    key += "0:0";
    key += '\0';
    return;
  }

  const std::string centerPath =
      UITheme::getCoverThumbPath(book.coverBmpPath, LyraCarouselTheme::kCenterThumbW, LyraCarouselTheme::kCenterThumbH);
  const std::string sidePath =
      UITheme::getCoverThumbPath(book.coverBmpPath, LyraCarouselTheme::kSideCoverW, LyraCarouselTheme::kSideCoverH);
  key += Storage.existsForRead(centerPath) ? '1' : '0';
  key += ':';
  key += Storage.existsForRead(sidePath) ? '1' : '0';
  key += '\0';

  const std::string cachePath = getRecentBookCachePath(book);
  if (!cachePath.empty()) {
    appendHashedFileStateToKey(key, cachePath + "/progress.bin");
    if (FsHelpers::hasEpubExtension(book.path) || FsHelpers::hasXtcExtension(book.path)) {
      appendHashedFileStateToKey(key, cachePath + "/stats_v7.bin");
      appendHashedFileStateToKey(key, cachePath + "/stats_v6.bin");
      appendHashedFileStateToKey(key, cachePath + "/stats_v5.bin");
    }
  } else {
    key += "no-cache-path";
    key += '\0';
  }
}

void appendSyncedStatsStateToKey(std::string& key) {
  FsFile dir = Storage.open(DUET_STATE_ROOT_PATH "/synced_stats");
  if (!dir) {
    key += "no-synced-stats";
    key += '\0';
    return;
  }

  if (!dir.isDirectory()) {
    dir.close();
    key += "synced-stats-not-dir";
    key += '\0';
    return;
  }

  char name[128];
  for (FsFile file = dir.openNextFile(); file; file = dir.openNextFile()) {
    const bool isDirectory = file.isDirectory();
    const size_t nameLen = file.getName(name, sizeof(name));
    if (!isDirectory && nameLen > 0) {
      key += name;
      key += '\0';
      file.close();
      appendHashedFileStateToKey(key, std::string(DUET_STATE_ROOT_PATH "/synced_stats/") + name);
      continue;
    }
    file.close();
  }
  dir.close();
}

void appendCarouselMenuStateToKey(std::string& key, const bool hasOpdsServers, const bool hasReadingStats,
                                  const bool hasBookmarks, const bool hasClippings) {
  key += hasOpdsServers ? "opds:1" : "opds:0";
  key += '\0';
  key += hasReadingStats ? "stats:1" : "stats:0";
  key += '\0';
  key += hasBookmarks ? "bookmarks:1" : "bookmarks:0";
  key += '\0';
  key += hasClippings ? "clippings:1" : "clippings:0";
  key += '\0';
  const size_t launcherCount = LAUNCHER_LAYOUT.count(LauncherSurface::Home);
  key += "launcher:";
  key += std::to_string(launcherCount);
  key += '\0';
  for (size_t i = 0; i < launcherCount; ++i) {
    key += static_cast<char>(LAUNCHER_LAYOUT.itemAt(LauncherSurface::Home, i));
    key += '\0';
  }
}

void buildCarouselCacheKey(const std::vector<RecentBook>& recentBooks, const bool hasOpdsServers,
                           const bool hasReadingStats, const bool hasBookmarks, const bool hasClippings,
                           std::string& key, uint64_t& keyHash) {
  key.clear();
  key.reserve(512);
  // The carousel cache stores the bottom icon row too, so menu visibility must
  // be part of the key alongside book covers/progress.
  appendCarouselMenuStateToKey(key, hasOpdsServers, hasReadingStats, hasBookmarks, hasClippings);
  for (const auto& book : recentBooks) {
    appendCarouselCoverStateToKey(key, book);
  }
  appendHashedFileStateToKey(key, DUET_STATE_ROOT_PATH "/global_stats.bin");
  appendSyncedStatsStateToKey(key);
  keyHash = fnvHash64(key);
}

bool isCarouselCacheHeaderValid(const CarouselCacheHeader& header, uint64_t cacheKeyHash, int bookCount,
                                const GfxRenderer& renderer) {
  return header.magic == CAROUSEL_CACHE_MAGIC && header.version == CAROUSEL_CACHE_VERSION &&
         header.keyHash == cacheKeyHash && header.frameCount == bookCount &&
         header.frameBufferSize == renderer.getBufferSize() && header.screenWidth == renderer.getScreenWidth() &&
         header.screenHeight == renderer.getScreenHeight() && header.centerCoverW == LyraCarouselTheme::kCenterThumbW &&
         header.centerCoverH == LyraCarouselTheme::kCenterThumbH &&
         header.sideCoverW == LyraCarouselTheme::kSideCoverW && header.sideCoverH == LyraCarouselTheme::kSideCoverH;
}

bool readCarouselCacheHeader(FsFile& file, CarouselCacheHeader& header) {
  CarouselCacheHeader readHeader{};
  if (!serialization::tryReadPod(file, readHeader)) {
    return false;
  }
  header = readHeader;
  return true;
}

bool hasValidCarouselDiskCache(const std::vector<RecentBook>& recentBooks, const GfxRenderer& renderer,
                               const bool hasOpdsServers, const bool hasReadingStats, const bool hasBookmarks,
                               const bool hasClippings) {
  const int bookCount = static_cast<int>(recentBooks.size());
  if (bookCount <= 0) return false;

  std::string cacheKey;
  uint64_t cacheKeyHash = 0;
  buildCarouselCacheKey(recentBooks, hasOpdsServers, hasReadingStats, hasBookmarks, hasClippings, cacheKey,
                        cacheKeyHash);

  FsFile cacheFile;
  if (!Storage.openFileForRead("HOME", CAROUSEL_CACHE_PATH, cacheFile)) {
    return false;
  }

  CarouselCacheHeader header{};
  const bool readOk = readCarouselCacheHeader(cacheFile, header);
  cacheFile.close();
  return readOk && isCarouselCacheHeaderValid(header, cacheKeyHash, bookCount, renderer);
}

int getVisibleRecentBookCount(const std::vector<RecentBook>& recentBooks) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return std::min(static_cast<int>(recentBooks.size()), metrics.homeRecentBooksCount);
}

int getHomeMenuSelectionOffset(const std::vector<RecentBook>& recentBooks) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return metrics.homeContinueReadingInMenu ? 0 : getVisibleRecentBookCount(recentBooks);
}
}  // namespace

// ---------------------------------------------------------------------------
// Static carousel frame cache — survives HomeActivity re-creation so that
// returning to home (e.g. after settings) doesn't re-read covers from SD.
// Freed explicitly in onSelectBook() before entering the reader.
// ---------------------------------------------------------------------------
namespace {
class CarouselCache {
 public:
  uint8_t* frames[HomeActivity::kCarouselFrameCount] = {};
  int frameBookIdx[HomeActivity::kCarouselFrameCount] = {-1};
  int frameCount = 0;
  int lastCenterIdx = -1;
  std::string key;
  uint64_t keyHash = 0;

  int findFrameSlot(int bookIdx) const {
    for (int i = 0; i < HomeActivity::kCarouselFrameCount; ++i) {
      if (frameBookIdx[i] == bookIdx && frames[i] != nullptr) return i;
    }
    return -1;
  }

  void invalidate() {
    for (int i = 0; i < HomeActivity::kCarouselFrameCount; ++i) {
      if (frames[i]) {
        free(frames[i]);
        frames[i] = nullptr;
      }
      frameBookIdx[i] = -1;
    }
    frameCount = 0;
    lastCenterIdx = -1;
    key.clear();
    keyHash = 0;
  }
};

CarouselCache gCarouselCache;
}  // namespace

static_assert(HomeActivity::kMaxCachedBooks >= LyraCarouselMetrics::values.homeRecentBooksCount,
              "kMaxCachedBooks must cover all carousel slots");
static_assert(HomeActivity::kMaxCachedBooks >= ReadingHomeMetrics::values.homeRecentBooksCount,
              "kMaxCachedBooks must cover all Reading Home slots");

int HomeActivity::getMenuItemCount() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const bool includeContinueReading = metrics.homeContinueReadingInMenu && !recentBooks.empty();
  const auto menuItems =
      buildSelectableHomeMenuItems(hasOpdsServers, hasReadingStats, hasBookmarks, hasClippings, includeContinueReading);
  return getHomeMenuSelectionOffset(recentBooks) + static_cast<int>(menuItems.size());
}

HalDisplay::RefreshMode HomeActivity::nextPaintRefreshMode() {
  // One deep refresh on the first home paint of each power-on: the boot
  // splash's bold Duet mark otherwise ghosts under home, most visibly on
  // the X4 panel. Also clears sleep-image residue on quick-resume boots.
  static bool bootGhostClearPending = true;
  if (bootGhostClearPending) {
    bootGhostClearPending = false;
    return HalDisplay::FULL_REFRESH;
  }
  return cleanRefreshPending ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
}

void HomeActivity::loadRecentBooks(int maxBooks, const std::string& activeBookPath) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  // The active reader wins over a stale recents ordering. Home may show fewer
  // books than the store retains, so add it before trimming the visible list.
  const auto appendBook = [this](const RecentBook& storedBook) {
    RecentBook book = storedBook;
    if (RecentBooksStore::isMissing(book)) {
      return false;
    }

    ensureReusableCoverPath(book);
    recentBooks.push_back(std::move(book));
    return true;
  };

  if (!activeBookPath.empty()) {
    const auto activeBook = std::find_if(
        books.begin(), books.end(), [&activeBookPath](const RecentBook& book) { return book.path == activeBookPath; });
    if (activeBook != books.end()) {
      appendBook(*activeBook);
    }
  }

  for (const RecentBook& storedBook : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }
    if (storedBook.path == activeBookPath) {
      continue;
    }
    appendBook(storedBook);
  }
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  const bool isReadingHome = isReadingHomeTheme();
  const bool firstCoverPass = !isReadingHome || deferredCoverIdx == 0;
  // Thumbnail generation may need a 32 KB contiguous inflate buffer. The Home
  // cover snapshot is only a redraw cache, so release it before ZIP work.
  if (firstCoverPass && coverBuffer) {
    freeCoverBuffer();
    coverRendered = false;
  }

  recentsLoading = true;
  // EPUB cover extraction needs the ZIP inflater's 32KB history buffer. Drop
  // the saved cover tile while generating thumbnails so Home has a larger
  // contiguous heap block available.
  if (firstCoverPass) {
    freeCoverBuffer();
    deferredCoverUpdated.fill(0);
  }
  auto zipInflateScratch = ScratchWorkspace::acquire(InflateReader::STREAMING_DICT_SIZE, "Home EPUB thumbnails");
  bool showingLoading = false;
  Rect popupRect;

  const bool isCarouselTheme =
      static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_CAROUSEL;
  const bool isMinimal = isMinimalTheme();
  const bool isDashboard = isDashboardTheme();
  const size_t recentBookCount = recentBooks.size();
  // Reading Home hydrates one cover per idle slice so its cursor never waits
  // for every recent EPUB to be opened and decoded in one monolithic pass.
  const size_t firstBookIdx = isReadingHome ? std::min(static_cast<size_t>(deferredCoverIdx), recentBookCount) : 0;
  const size_t lastBookIdx = isReadingHome ? std::min(firstBookIdx + 1, recentBookCount) : recentBookCount;
  const int progressIncrement = 90 / static_cast<int>(std::max<size_t>(1, recentBookCount));

  int progress = static_cast<int>(firstBookIdx);
  for (size_t bookIdx = firstBookIdx; bookIdx < lastBookIdx; ++bookIdx) {
    RecentBook& book = recentBooks[bookIdx];
    if (!Storage.exists(book.path.c_str())) {
      progress++;
      continue;
    }
    ensureReusableCoverPath(book);
    if (!book.coverBmpPath.empty()) {
      if (isCarouselTheme) {
        // For carousel: generate exact-size thumbnails for the center image rect and side slots.
        // Load the source image once even when both sizes are missing.
        const std::string centerPath = UITheme::getCoverThumbPath(book.coverBmpPath, LyraCarouselTheme::kCenterThumbW,
                                                                  LyraCarouselTheme::kCenterThumbH);
        const std::string sidePath = UITheme::getCoverThumbPath(book.coverBmpPath, LyraCarouselTheme::kSideCoverW,
                                                                LyraCarouselTheme::kSideCoverH);
        const bool centerMissing = !Storage.existsForRead(centerPath);
        const bool sideMissing = !Storage.existsForRead(sidePath);

        if (centerMissing || sideMissing) {
          if (FsHelpers::hasEpubExtension(book.path)) {
            Epub epub(book.path, DUET_BOOKS_ROOT_PATH "");
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * progressIncrement);
            if (!epub.load(true, true)) {
              LOG_ERR("HOME", "carousel: failed to load EPUB cache for thumb generation: %s", book.path.c_str());
              updateRecentBookCoverPath(book, "");
              book.coverBmpPath = "";
              coverRendered = false;
              coverRepaintPending = true;
              progress++;
              continue;
            }
            bool success = true;
            if (centerMissing)
              success = epub.generateThumbBmp(LyraCarouselTheme::kCenterThumbW, LyraCarouselTheme::kCenterThumbH,
                                              &renderer, SETTINGS.getReaderFontId()) &&
                        success;
            if (sideMissing)
              success = epub.generateThumbBmp(LyraCarouselTheme::kSideCoverW, LyraCarouselTheme::kSideCoverH, &renderer,
                                              SETTINGS.getReaderFontId()) &&
                        success;
            if (!success) {
              updateRecentBookCoverPath(book, "");
              book.coverBmpPath = "";
            } else {
              if (bookIdx < deferredCoverUpdated.size()) deferredCoverUpdated[bookIdx] = true;
            }
            coverRendered = false;
            coverRepaintPending = true;
          } else if (FsHelpers::hasXtcExtension(book.path)) {
            Xtc xtc(book.path, DUET_BOOKS_ROOT_PATH "");
            if (xtc.load()) {
              if (!showingLoading) {
                showingLoading = true;
                popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
              }
              GUI.fillPopupProgress(renderer, popupRect, 10 + progress * progressIncrement);
              bool success = true;
              if (centerMissing)
                success =
                    xtc.generateThumbBmp(LyraCarouselTheme::kCenterThumbW, LyraCarouselTheme::kCenterThumbH) && success;
              if (sideMissing)
                success =
                    xtc.generateThumbBmp(LyraCarouselTheme::kSideCoverW, LyraCarouselTheme::kSideCoverH) && success;
              if (!success) {
                updateRecentBookCoverPath(book, "");
                book.coverBmpPath = "";
              } else {
                if (bookIdx < deferredCoverUpdated.size()) deferredCoverUpdated[bookIdx] = true;
              }
              coverRendered = false;
              coverRepaintPending = true;
            }
          }
        }
      } else {
        // Non-carousel: generate the active theme's thumbnail size.
        const bool supportsExactHomeThumb =
            FsHelpers::hasEpubExtension(book.path) || FsHelpers::hasXtcExtension(book.path);
        const bool useDashboardThumb = isDashboard && supportsExactHomeThumb;
        const bool useMinimalThumb = isMinimal && supportsExactHomeThumb;
        const bool useReadingHomeThumb = isReadingHome && supportsExactHomeThumb;
        const std::string coverPath =
            useDashboardThumb ? dashboardHomeCoverPath(book, coverHeight)
                              : (useMinimalThumb ? minimalHomeCoverPath(book, coverHeight)
                                                 : (useReadingHomeThumb
                                                        ? readingHomeCoverPath(book, coverHeight)
                                                        : UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight)));
        if (coverPath.empty() || !Storage.existsForRead(coverPath)) {
          if (FsHelpers::hasEpubExtension(book.path)) {
            Epub epub(book.path, DUET_BOOKS_ROOT_PATH "");
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * progressIncrement);
            if (!epub.load(true, true)) {
              LOG_ERR("HOME", "failed to load EPUB cache for thumb generation: %s", book.path.c_str());
              updateRecentBookCoverPath(book, "");
              book.coverBmpPath = "";
              coverRendered = false;
              coverRepaintPending = true;
              progress++;
              continue;
            }
            const bool success =
                useDashboardThumb
                    ? epub.generateAdaptiveThumbBmp(dashboardHomeCoverWidth(coverHeight),
                                                    dashboardHomeCoverHeight(coverHeight), &renderer,
                                                    SETTINGS.getReaderFontId())
                    : (useMinimalThumb
                           ? epub.generateAdaptiveThumbBmp(minimalHomeCoverWidth(coverHeight),
                                                           minimalHomeCoverHeight(coverHeight), &renderer,
                                                           SETTINGS.getReaderFontId())
                           : (useReadingHomeThumb
                                  ? epub.generateAdaptiveThumbBmp(readingHomeCoverWidth(coverHeight),
                                                                  readingHomeCoverHeight(coverHeight), &renderer,
                                                                  SETTINGS.getReaderFontId())
                                  : epub.generateThumbBmp(0, coverHeight, &renderer, SETTINGS.getReaderFontId())));
            if (!success) {
              updateRecentBookCoverPath(book, "");
              book.coverBmpPath = "";
            } else {
              if (bookIdx < deferredCoverUpdated.size())
                deferredCoverUpdated[bookIdx] = true;  // non-carousel path reuses same tracking
            }
            coverRendered = false;
            coverRepaintPending = true;
          } else if (FsHelpers::hasXtcExtension(book.path)) {
            Xtc xtc(book.path, DUET_BOOKS_ROOT_PATH "");
            if (xtc.load()) {
              if (!showingLoading) {
                showingLoading = true;
                popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
              }
              GUI.fillPopupProgress(renderer, popupRect, 10 + progress * progressIncrement);
              const bool success =
                  useDashboardThumb
                      ? xtc.generateThumbBmp(static_cast<uint16_t>(dashboardHomeCoverWidth(coverHeight)),
                                             static_cast<uint16_t>(dashboardHomeCoverHeight(coverHeight)))
                      : (useMinimalThumb
                             ? xtc.generateThumbBmp(static_cast<uint16_t>(minimalHomeCoverWidth(coverHeight)),
                                                    static_cast<uint16_t>(minimalHomeCoverHeight(coverHeight)))
                             : (useReadingHomeThumb
                                    ? xtc.generateThumbBmp(static_cast<uint16_t>(readingHomeCoverWidth(coverHeight)),
                                                           static_cast<uint16_t>(readingHomeCoverHeight(coverHeight)))
                                    : xtc.generateThumbBmp(coverHeight)));
              if (!success) {
                updateRecentBookCoverPath(book, "");
                book.coverBmpPath = "";
              } else {
                if (bookIdx < deferredCoverUpdated.size()) deferredCoverUpdated[bookIdx] = true;
              }
              coverRendered = false;
              coverRepaintPending = true;
            }
          }
        }
      }
    }
    progress++;
  }

  if (isReadingHome && lastBookIdx < recentBookCount) {
    deferredCoverIdx = static_cast<int>(lastBookIdx);
    recentsLoading = false;
    if (coverRepaintPending) {
      coverRepaintPending = false;
      requestUpdate();
    }
    return;
  }

  recentsLoaded = true;
  recentsLoading = false;
  deferredCoverIdx = static_cast<int>(recentBookCount);

  // Re-render only the affected slots rather than rebuilding the entire cache.
  if (isCarouselTheme) {
    bool anyUpdated = false;
    for (int i = 0; i < static_cast<int>(recentBooks.size()); ++i) {
      if (static_cast<size_t>(i) >= deferredCoverUpdated.size() || !deferredCoverUpdated[i]) continue;
      anyUpdated = true;
      if (carouselFramesReady) {
        // Only re-render the slot holding this book; books outside the window
        // will be picked up by updateSlidingWindowCache on next navigation.
        const int slot = gCarouselCache.findFrameSlot(i);
        if (slot >= 0) renderCarouselFrame(i, slot);
      }
    }
    if (anyUpdated) {
      if (!carouselFramesReady) {
        // Cover assets changed before the carousel cache was initialised, so
        // any existing SD snapshot may still contain placeholder frames.
        // Force a rebuild from the fresh thumbs instead of reusing stale
        // `home_carousel_cache.bin` content keyed only by book order/layout.
        if (Storage.exists(CAROUSEL_CACHE_PATH)) {
          Storage.remove(CAROUSEL_CACHE_PATH);
        }
        if (Storage.exists(CAROUSEL_CACHE_TMP_PATH)) {
          Storage.remove(CAROUSEL_CACHE_TMP_PATH);
        }
        // Home defers the expensive snapshot build until after its next idle
        // window. Returning from a book should never wait on five cover frames.
        carouselWarmupPending = true;
      } else {
        // The live carousel frames are already updated above. Keep Home
        // responsive by invalidating any stale SD snapshot instead of
        // rewriting all 5 frames synchronously on this return-to-Home path.
        if (Storage.exists(CAROUSEL_CACHE_PATH)) {
          Storage.remove(CAROUSEL_CACHE_PATH);
        }
        if (Storage.exists(CAROUSEL_CACHE_TMP_PATH)) {
          Storage.remove(CAROUSEL_CACHE_TMP_PATH);
        }
      }
      coverRepaintPending = true;
    }
  }

  // Each cover decode used to request its own whole-panel repaint; on e-ink
  // that reads as a burst of flashes at boot/Home entry (worst on the X4,
  // whose driver cannot coalesce them like the X3's resync machinery). One
  // repaint after all covers settle shows the same final frame with a single
  // refresh.
  if (coverRepaintPending) {
    coverRepaintPending = false;
    requestUpdate();
  }
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  // Entering Home replaces whatever full screen was showing (settings, reader,
  // launcher); the first frame needs the clean swap refresh.
  cleanRefreshPending = true;
  timingOnEnterMs = millis();
  timingFirstPaintMs = 0;
  timingWritten = false;
  selectorIndex = 0;
  lastCarouselBookIndex = 0;
  firstRenderDone.store(false, std::memory_order_release);
  deferredHomeWork = DeferredHomeWork::Metadata;
  deferredBookStatsIdx = 0;
  deferredCoverIdx = 0;
  deferredCoverUpdated.fill(0);
  deferredHomeWorkNotBefore.store(0UL, std::memory_order_release);
  initialMenuSelectionPending = initialMenuItem != HomeMenuItem::NONE;
  recentsLoading = false;
  recentsLoaded = false;
  bookStatsCached = false;
  hasOpdsServers = false;
  hasBookmarks = false;
  hasClippings = false;
  minimalMenuOpen = false;
  minimalSuppressInitialFrontRelease = usesMinimalHomeInteraction();
  minimalMenuIndex = 0;
  minimalHomeNavIndex = -1;
  readingHomeFrameReady = false;
  readingHomeSelectionOnlyPending = false;
  carouselFramesReady = false;
  carouselWarmupPending = false;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int recentBooksToLoad =
      std::min(kMaxCachedBooks, std::max(metrics.homeRecentBooksCount, HOME_MIN_CACHED_RECENT_COUNT));

  std::string activeBookPath;
  CurrentBookStats::loadLastActivePath(activeBookPath);
  {
    std::lock_guard<std::mutex> lock(APP_STATE.getMutex());
    if (activeBookPath.empty()) activeBookPath = APP_STATE.openEpubPath;
  }
  loadRecentBooks(recentBooksToLoad, activeBookPath);
  if (!activeBookPath.empty()) {
    for (int i = 0; i < static_cast<int>(recentBooks.size()); ++i) {
      if (recentBooks[i].path == activeBookPath) {
        if ((metrics.homeRecentBooksCount == 1 || isReadingHomeTheme()) && i > 0) {
          std::rotate(recentBooks.begin(), recentBooks.begin() + i, recentBooks.end());
          selectorIndex = 0;
          lastCarouselBookIndex = 0;
        } else {
          selectorIndex = i;
          lastCarouselBookIndex = i;
        }
        break;
      }
    }
  }

  // Global stats are a tiny fixed-size read and keep the initial Home frame
  // truthful; heavier book, journal, and catalogue work follows first paint.
  globalStats = GlobalReadingStats::load();
  showAllDevicesStats = GlobalReadingStats::hasSyncedStats();
  allDevicesGlobalStats = globalStats;
  // Keep current-book stats in the first Home frame without constructing an
  // EPUB before the reader opens it. The eager EPUB metadata load fragmented
  // X3 heap badly enough that opening the selected book could fail until reset.
  updateHighlightedBookStatsOnly();
  readingHomeTodaySeconds = 0;
  readingHomeCurrentStreak = 0;
  if (isReadingHomeTheme()) {
    ReadingStatsDateTime now;
    if (getCurrentLocalReadingStatsDateTime(now)) {
      readingHomeCurrentStreak = globalStats.currentReadingStreak(&now.date);
    }
  }

  requestUpdate();
}

bool HomeActivity::hasActiveHomeInput() const {
  return mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased() ||
         mappedInput.isPressed(MappedInputManager::Button::Back) ||
         mappedInput.isPressed(MappedInputManager::Button::Confirm) ||
         mappedInput.isPressed(MappedInputManager::Button::Left) ||
         mappedInput.isPressed(MappedInputManager::Button::Right) ||
         mappedInput.isPressed(MappedInputManager::Button::Up) ||
         mappedInput.isPressed(MappedInputManager::Button::Down) ||
         mappedInput.isPressed(MappedInputManager::Button::Power) || isAnyFrontButtonPressed(mappedInput);
}

void HomeActivity::deferHomeWorkAfterInput(const unsigned long idleMs) {
  const unsigned long delay = idleMs == 0 ? HOME_DEFERRED_AFTER_INPUT_IDLE_MS : idleMs;
  deferredHomeWorkNotBefore.store(millis() + delay, std::memory_order_release);
}

void HomeActivity::resetHighlightedBookContextToProgressOnly() {
  currentBookStats = BookReadingStats{};
  currentBookProgressPercent = -1.0f;
  currentBookWordCount = 0;
  currentBookChapterTitle.clear();

  const int idx = getHighlightedBookIndex();
  if (idx >= 0) {
    currentBookProgressPercent = RecentBookProgress::loadPercent(recentBooks[idx]);
  }

  hasReadingStats = hasAnyBookStats(currentBookStats) || hasAnyGlobalStats(globalStats) ||
                    (showAllDevicesStats && hasAnyGlobalStats(allDevicesGlobalStats));
}

void HomeActivity::scheduleHighlightedBookContextRefresh(const unsigned long idleMs) {
  deferredHomeWork = DeferredHomeWork::BookContext;
  deferredBookStatsIdx = 0;
  bookStatsCached = false;
  deferredHomeWorkNotBefore.store(millis() + (idleMs == 0 ? HOME_DEFERRED_BOOK_CONTEXT_IDLE_MS : idleMs),
                                  std::memory_order_release);
}

void HomeActivity::applyInitialMenuSelection() {
  if (!initialMenuSelectionPending) return;
  initialMenuSelectionPending = false;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const bool includeContinueReading = metrics.homeContinueReadingInMenu && !recentBooks.empty();
  const auto menuItems =
      buildSelectableHomeMenuItems(hasOpdsServers, hasReadingStats, hasBookmarks, hasClippings, includeContinueReading);
  const int menuIndex = findMenuItemIndex(menuItems, initialMenuItem);
  if (menuIndex >= 0) {
    selectorIndex = getHomeMenuSelectionOffset(recentBooks) + menuIndex;
  }
}

void HomeActivity::markFirstRenderDone() {
  bool expected = false;
  if (!firstRenderDone.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return;
  timingFirstPaintMs = millis();
  deferredHomeWorkNotBefore.store(millis() + HOME_DEFERRED_METADATA_IDLE_MS, std::memory_order_release);
}

void HomeActivity::runDeferredHomeWork() {
  if (!firstRenderDone.load(std::memory_order_acquire) || deferredHomeWork == DeferredHomeWork::Complete) return;

  const unsigned long now = millis();
  if (hasActiveHomeInput()) {
    deferredHomeWorkNotBefore.store(now + HOME_DEFERRED_METADATA_IDLE_MS, std::memory_order_release);
    return;
  }
  if (!homeWorkDeadlineReached(now, deferredHomeWorkNotBefore.load(std::memory_order_acquire))) return;

  const bool isCarouselTheme =
      static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_CAROUSEL;

  switch (deferredHomeWork) {
    case DeferredHomeWork::Metadata:
      if (!timingWritten && timingFirstPaintMs != 0) {
        timingWritten = true;
        FsFile timingFile;
        if (Storage.openFileForWrite("HOME", DUET_STATE_ROOT_PATH "/home_timing.txt", timingFile)) {
          char buf[96];
          const int n = snprintf(buf, sizeof(buf), "entryToPaint=%lums\n", timingFirstPaintMs - timingOnEnterMs);
          if (n > 0) timingFile.write(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(n));
          timingFile.close();
        }
      }
      hasOpdsServers = OPDS_STORE.hasServers();
      hasBookmarks = BookmarkStore::hasAnyBookmarks();
      hasClippings = ClippingStore::hasAnyClippings();
      showAllDevicesStats = GlobalReadingStats::hasSyncedStats();
      allDevicesGlobalStats = showAllDevicesStats ? GlobalReadingStats::loadAggregated(globalStats) : globalStats;
      if (isReadingHomeTheme()) refreshReadingHomeToday();
      applyInitialMenuSelection();
      deferredHomeWork = DeferredHomeWork::BookContext;
      deferredHomeWorkNotBefore.store(now + HOME_DEFERRED_BOOK_CONTEXT_IDLE_MS, std::memory_order_release);
      if (!usesMinimalHomeInteraction() && !isReadingHomeTheme()) requestUpdate();
      return;

    case DeferredHomeWork::BookContext:
      // Any multi-cover theme re-resolves the highlighted book's context on
      // every cursor move; cache all book stats up front. Lyra 3 Covers
      // previously missed this gate and reloaded EPUB metadata + stats from
      // SD on each move. One book per pass: each load opens files inside the
      // book's durable cache dir, and resolving that dir is a linear FAT
      // scan over the whole cache root — seconds per book. A monolithic loop
      // blocked input for its full duration and swallowed taps; slicing gives
      // the input path a beat between every book.
      if (isCarouselTheme || isReadingHomeTheme() || UITheme::getInstance().getMetrics().homeRecentBooksCount > 1) {
        const int count = std::min(static_cast<int>(recentBooks.size()), kMaxCachedBooks);
        if (deferredBookStatsIdx < count) {
          if (isReadingHomeTheme()) {
            // Reading Home always keeps book zero as the active book. Reuse its
            // already-loaded stats and only read the tiny progress sidecars
            // needed by the three recent-cover bars.
            cachedBookStats[deferredBookStatsIdx] = deferredBookStatsIdx == 0 ? currentBookStats : BookReadingStats{};
            cachedBookProgress[deferredBookStatsIdx] =
                deferredBookStatsIdx == 0 ? currentBookProgressPercent
                                          : RecentBookProgress::loadPercent(recentBooks[deferredBookStatsIdx]);
            cachedBookWordCounts[deferredBookStatsIdx] =
                deferredBookStatsIdx == 0 ? currentBookWordCount : cachedBookStats[deferredBookStatsIdx].totalWordCount;
            if (deferredBookStatsIdx == 0) {
              loadEpubHighlightedContext(recentBooks[0], false, true, nullptr, &currentBookChapterTitle,
                                         &currentBookWordCount);
              cachedBookWordCounts[0] = currentBookWordCount;
            }
          } else {
            cachedBookStats[deferredBookStatsIdx] = loadRecentBookStats(recentBooks[deferredBookStatsIdx]);
            cachedBookProgress[deferredBookStatsIdx] =
                RecentBookProgress::loadPercent(recentBooks[deferredBookStatsIdx]);
            uint32_t wordCount = cachedBookStats[deferredBookStatsIdx].totalWordCount;
            loadEpubHighlightedContext(recentBooks[deferredBookStatsIdx], false, false, nullptr, nullptr, &wordCount);
            cachedBookWordCounts[deferredBookStatsIdx] = wordCount;
          }
          ++deferredBookStatsIdx;
          if (deferredBookStatsIdx < count) {
            deferredHomeWorkNotBefore.store(millis() + HOME_DEFERRED_BOOK_STATS_STEP_MS, std::memory_order_release);
            return;
          }
        }
        bookStatsCached = true;
      }
      deferredHomeWork = DeferredHomeWork::Covers;
      deferredHomeWorkNotBefore.store(now + HOME_DEFERRED_COVER_IDLE_MS, std::memory_order_release);
      return;

    case DeferredHomeWork::Covers:
      if (!recentsLoaded && !recentsLoading) {
        RenderLock lock(*this);
        loadRecentCovers(UITheme::getInstance().getMetrics().homeCoverHeight);
      }
      if (!recentsLoaded) {
        deferredHomeWorkNotBefore.store(millis() + HOME_DEFERRED_BOOK_STATS_STEP_MS, std::memory_order_release);
        return;
      }
      if (isCarouselTheme && !carouselFramesReady) carouselWarmupPending = true;
      deferredHomeWork = DeferredHomeWork::Carousel;
      deferredHomeWorkNotBefore.store(now + HOME_DEFERRED_CAROUSEL_IDLE_MS, std::memory_order_release);
      return;

    case DeferredHomeWork::Carousel:
      if (isCarouselTheme && carouselWarmupPending && !carouselFramesReady) {
        RenderLock lock(*this);
        carouselWarmupPending = false;
        preRenderCarouselFrames(false);
      }
      deferredHomeWork = DeferredHomeWork::Achievements;
      deferredHomeWorkNotBefore.store(now + HOME_DEFERRED_ACHIEVEMENT_IDLE_MS, std::memory_order_release);
      return;

    case DeferredHomeWork::Achievements:
      // Keep Home responsive. Achievement popups are reconciled when the user
      // opens Achievements; background Home work should not steal an input beat.
      ACHIEVEMENT_STORE.refreshLightweight(false);
      deferredHomeWork = DeferredHomeWork::Complete;
      return;

    case DeferredHomeWork::Complete:
      return;
  }
}

int HomeActivity::getHighlightedBookIndex() const {
  if (recentBooks.empty()) {
    return -1;
  }

  if (isReadingHomeTheme()) {
    return 0;
  }

  const int visibleBookCount = getVisibleRecentBookCount();
  const int highlightedBookIdx = (selectorIndex < visibleBookCount) ? selectorIndex : lastCarouselBookIndex;
  return std::clamp(highlightedBookIdx, 0, visibleBookCount - 1);
}

int HomeActivity::getVisibleRecentBookCount() const { return ::getVisibleRecentBookCount(recentBooks); }

std::string HomeActivity::getCurrentBookPath() const {
  const int idx = getHighlightedBookIndex();
  return idx >= 0 ? recentBooks[idx].path : std::string{};
}

void HomeActivity::updateHighlightedBookStatsOnly() {
  currentBookStats = BookReadingStats{};
  currentBookProgressPercent = -1.0f;
  currentBookWordCount = 0;
  currentBookChapterTitle.clear();

  const int idx = getHighlightedBookIndex();
  if (idx >= 0) {
    currentBookStats = loadRecentBookStats(recentBooks[idx]);
    currentBookWordCount = currentBookStats.totalWordCount;
    // This reads the tiny persisted percentage sidecar and does not open or
    // parse the EPUB. Keeping it in the first frame prevents Home from showing
    // an unknown progress value for a book whose reader stats are accurate.
    currentBookProgressPercent = RecentBookProgress::loadPercent(recentBooks[idx]);
  }

  hasReadingStats = hasAnyBookStats(currentBookStats) || hasAnyGlobalStats(globalStats) ||
                    (showAllDevicesStats && hasAnyGlobalStats(allDevicesGlobalStats));
}

void HomeActivity::updateHighlightedBookContext() {
  const auto start = millis();
  currentBookStats = BookReadingStats{};
  currentBookProgressPercent = -1.0f;
  currentBookWordCount = 0;
  currentBookChapterTitle.clear();

  const int idx = getHighlightedBookIndex();
  const bool useCachedStats = idx >= 0 && bookStatsCached && idx < kMaxCachedBooks;
  if (idx >= 0) {
    const RecentBook& book = recentBooks[idx];
    const bool isEpub = FsHelpers::hasEpubExtension(book.path);
    const bool loadChapterTitle = isDashboardTheme() || isReadingHomeTheme();
    if (useCachedStats) {
      currentBookStats = cachedBookStats[idx];
      currentBookProgressPercent = cachedBookProgress[idx];
      currentBookWordCount = cachedBookWordCounts[idx];
      if (loadChapterTitle && isEpub) {
        loadEpubHighlightedContext(book, false, true, nullptr, &currentBookChapterTitle, &currentBookWordCount);
      }
    } else {
      currentBookStats = loadRecentBookStats(book);
      currentBookWordCount = currentBookStats.totalWordCount;
      if (isEpub) {
        loadEpubHighlightedContext(book, true, loadChapterTitle, &currentBookProgressPercent, &currentBookChapterTitle,
                                   &currentBookWordCount);
      } else {
        currentBookProgressPercent = RecentBookProgress::loadPercent(book);
      }
      if (loadChapterTitle && !isEpub) {
        currentBookChapterTitle.clear();
      }
    }
  }

  hasReadingStats = hasAnyBookStats(currentBookStats) || hasAnyGlobalStats(globalStats) ||
                    (showAllDevicesStats && hasAnyGlobalStats(allDevicesGlobalStats));
  LOG_DBG("HOME", "updateHighlightedBookContext idx=%d cached=%s took %lums", idx, useCachedStats ? "yes" : "no",
          millis() - start);
}

void HomeActivity::refreshReadingHomeToday() {
  readingHomeTodaySeconds = 0;
  readingHomeCurrentStreak = 0;
  if (!isReadingHomeTheme()) return;

  ReadingStatsDateTime now;
  if (!getCurrentLocalReadingStatsDateTime(now)) return;
  const GlobalReadingStats& displayedStats = showAllDevicesStats ? allDevicesGlobalStats : globalStats;
  readingHomeCurrentStreak = displayedStats.currentReadingStreak(&now.date);
  const auto heap = MemoryBudget::snapshot();
  if (!MemoryBudget::hasHeap(heap, HOME_JOURNAL_LOAD_MIN_FREE, HOME_JOURNAL_LOAD_MIN_MAX_ALLOC)) {
    LOG_DBG("HOME", "Skipping Home journal load: low heap (free=%u maxAlloc=%u)", heap.freeHeap, heap.maxAllocHeap);
    return;
  }
  const auto journal = ReadingJournal::loadAggregated();
  if (journal) {
    readingHomeTodaySeconds = journal->secondsOnDay(readingStatsDayIndex(now.date));
  }
}

bool HomeActivity::loopReadingHome() {
  const int recentCount =
      std::min(ReadingHomeTheme::kRecentCoverCount, std::max(0, static_cast<int>(recentBooks.size()) - 1));
  const int navigationStart = 1 + recentCount;
  const int itemCount = navigationStart + 4;
  bool moved = false;

  buttonNavigator.onNext([this, itemCount, &moved] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, itemCount);
    readingHomeSelectionOnlyPending = readingHomeFrameReady;
    moved = true;
    requestUpdate(true);
  });
  buttonNavigator.onPrevious([this, itemCount, &moved] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, itemCount);
    readingHomeSelectionOnlyPending = readingHomeFrameReady;
    moved = true;
    requestUpdate(true);
  });

  if (moved) {
    deferredHomeWorkNotBefore.store(millis() + HOME_DEFERRED_COVER_IDLE_MS, std::memory_order_release);
    return true;
  }

  if (!mappedInput.wasReleased(MappedInputManager::Button::Confirm)) return false;
  if (selectorIndex == 0) {
    if (!recentBooks.empty()) onSelectBook(recentBooks[0].path);
    return true;
  }
  if (selectorIndex <= recentCount) {
    onSelectBook(recentBooks[selectorIndex].path);
    return true;
  }

  switch (selectorIndex - navigationStart) {
    case 0:
      onUtilitiesOpen();
      break;
    case 1:
      onRecentsOpen();
      break;
    case 2:
      onFileBrowserOpen();
      break;
    case 3:
      onSettingsOpen();
      break;
  }
  return true;
}

void HomeActivity::renderReadingHome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  if (readingHomeFrameReady && readingHomeSelectionOnlyPending) {
    const int recentCount =
        std::min(ReadingHomeTheme::kRecentCoverCount, std::max(0, static_cast<int>(recentBooks.size()) - 1));
    static_cast<const ReadingHomeTheme&>(GUI).drawReadingHomeSelection(renderer, selectorIndex, recentCount);
    const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(nextPaintRefreshMode());
    cleanRefreshPending = false;
    readingHomeSelectionOnlyPending = false;
    return;
  }

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, 0, pageWidth, metrics.homeTopPadding}, nullptr);
  static_cast<const ReadingHomeTheme&>(GUI).drawReadingHome(
      renderer, recentBooks, selectorIndex, hasAnyBookStats(currentBookStats) ? &currentBookStats : nullptr,
      currentBookProgressPercent, currentBookWordCount, currentBookChapterTitle.c_str(), cachedBookProgress,
      globalStats, showAllDevicesStats ? allDevicesGlobalStats : globalStats, readingHomeTodaySeconds,
      readingHomeCurrentStreak);

  const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(nextPaintRefreshMode());
  cleanRefreshPending = false;
  readingHomeFrameReady = true;
  readingHomeSelectionOnlyPending = false;

  if (!firstRenderDone.load(std::memory_order_acquire)) {
    markFirstRenderDone();
    return;
  }
}

void HomeActivity::onExit() {
  Activity::onExit();

  readingHomeFrameReady = false;
  readingHomeSelectionOnlyPending = false;
  freeCoverBuffer();
  gCarouselCache.invalidate();
  freeCarouselFrames();
  carouselWarmupPending = false;
}

bool HomeActivity::storeCoverBuffer() {
  // render() must have already set the cover rect; without it we'd be back to
  // cloning the whole framebuffer.
  if (coverRectW <= 0 || coverRectH <= 0) return false;
  freeCoverBuffer();
  const size_t needed = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (needed == 0) return false;
  coverBuffer = static_cast<uint8_t*>(malloc(needed));
  if (!coverBuffer) {
    LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", (unsigned)needed);
    return false;
  }
  coverBufferSize = needed;
  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize)) {
    free(coverBuffer);
    coverBuffer = nullptr;
    coverBufferSize = 0;
    return false;
  }
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer || coverRectW <= 0 || coverRectH <= 0) return false;
  return renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize);
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferSize = 0;
  coverBufferStored = false;
}

void HomeActivity::invalidateCoverCache() {
  coverRendered = false;
  freeCoverBuffer();
}

void HomeActivity::freeCarouselFrames() {
  // Instance pointers are aliases into the static cache — do not free here.
  for (int i = 0; i < kCarouselFrameCount; ++i) carouselFrames[i] = nullptr;
  carouselFramesReady = false;
}

bool HomeActivity::allocateCarouselFrameSlots(int targetFrameCount) {
  const size_t bufferSize = renderer.getBufferSize();
  int frameCount = 0;
  for (int attemptFrameCount = targetFrameCount; attemptFrameCount >= 1; --attemptFrameCount) {
    bool allocFailed = false;
    for (int i = 0; i < attemptFrameCount; ++i) {
      gCarouselCache.frames[i] = static_cast<uint8_t*>(malloc(bufferSize));
      if (!gCarouselCache.frames[i]) {
        LOG_ERR("HOME", "preRenderCarouselFrames: malloc failed for frame %d while allocating %d frame(s)", i,
                attemptFrameCount);
        allocFailed = true;
        break;
      }
      if (!hasHeapForCarouselFrameCache()) {
        LOG_INF("HOME", "carousel: low heap after frame cache alloc (%u free, %u maxAlloc); skipping cache",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        free(gCarouselCache.frames[i]);
        gCarouselCache.frames[i] = nullptr;
        allocFailed = true;
        break;
      }
      gCarouselCache.frameBookIdx[i] = -1;
    }

    if (!allocFailed) {
      frameCount = attemptFrameCount;
      break;
    }

    for (int i = 0; i < attemptFrameCount; ++i) {
      if (gCarouselCache.frames[i]) {
        free(gCarouselCache.frames[i]);
        gCarouselCache.frames[i] = nullptr;
      }
      gCarouselCache.frameBookIdx[i] = -1;
    }
  }

  if (frameCount == 0) {
    gCarouselCache.invalidate();
    return false;
  }

  gCarouselCache.frameCount = frameCount;
  LOG_INF("HOME", "carousel: frame cache capacity %d/%d", frameCount, targetFrameCount);
  return true;
}

void HomeActivity::renderCarouselFrameToCurrentBuffer(int bookIdx, BookReadingStats* outStats,
                                                      float* outProgressPercent, bool* outUsedCachedStats) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int bookCount = static_cast<int>(recentBooks.size());
  bool dummy1 = false, dummy2 = false, dummy3 = false;
  BookReadingStats frameStats;
  const BookReadingStats* frameStatsPtr = nullptr;
  float frameProgressPercent = -1.0f;
  bool usedCachedStats = false;

  if (bookIdx >= 0 && bookIdx < bookCount) {
    if (bookStatsCached && bookIdx < kMaxCachedBooks) {
      usedCachedStats = true;
      frameStats = cachedBookStats[bookIdx];
      frameProgressPercent = cachedBookProgress[bookIdx];
    } else {
      frameStats = loadRecentBookStats(recentBooks[bookIdx]);
      frameProgressPercent = RecentBookProgress::loadPercent(recentBooks[bookIdx]);
    }
    if (hasAnyBookStats(frameStats)) frameStatsPtr = &frameStats;
  }

  LyraCarouselTheme::setPreRenderIndex(bookIdx);
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr);
  GUI.drawRecentBookCover(
      renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight}, recentBooks, bookCount, dummy1,
      dummy2, dummy3, []() { return true; }, frameStatsPtr, frameProgressPercent);

  const bool frameHasReadingStats = hasAnyBookStats(frameStats) || hasAnyGlobalStats(globalStats) ||
                                    (showAllDevicesStats && hasAnyGlobalStats(allDevicesGlobalStats));
  const auto menuItems = buildHomeMenuItems(hasOpdsServers, frameHasReadingStats, hasBookmarks, hasClippings);
  GUI.drawButtonMenu(
      renderer,
      Rect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing, pageWidth,
           pageHeight - (metrics.headerHeight + metrics.homeTopPadding + metrics.verticalSpacing * 2 +
                         metrics.buttonHintsHeight)},
      static_cast<int>(menuItems.size()), -1, [&menuItems](int index) { return menuItems[index].label; },
      [&menuItems](int index) { return menuItems[index].icon; });

  const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (outStats) *outStats = frameStats;
  if (outProgressPercent) *outProgressPercent = frameProgressPercent;
  if (outUsedCachedStats) *outUsedCachedStats = usedCachedStats;
}

bool HomeActivity::buildCarouselCacheFile(const std::string& cacheKey, uint64_t cacheKeyHash, int bookCount,
                                          bool showProgressPopup) {
  (void)cacheKey;
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer || bookCount <= 0) return false;

  Storage.mkdir(DUET_STATE_ROOT_PATH "");
  if (Storage.exists(CAROUSEL_CACHE_TMP_PATH)) {
    Storage.remove(CAROUSEL_CACHE_TMP_PATH);
  }

  FsFile file;
  if (!Storage.openFileForWrite("HOME", CAROUSEL_CACHE_TMP_PATH, file)) {
    return false;
  }

  const CarouselCacheHeader header = {
      CAROUSEL_CACHE_MAGIC,
      CAROUSEL_CACHE_VERSION,
      static_cast<uint16_t>(bookCount),
      static_cast<uint32_t>(renderer.getBufferSize()),
      cacheKeyHash,
      static_cast<uint16_t>(renderer.getScreenWidth()),
      static_cast<uint16_t>(renderer.getScreenHeight()),
      static_cast<uint16_t>(LyraCarouselTheme::kCenterThumbW),
      static_cast<uint16_t>(LyraCarouselTheme::kCenterThumbH),
      static_cast<uint16_t>(LyraCarouselTheme::kSideCoverW),
      static_cast<uint16_t>(LyraCarouselTheme::kSideCoverH),
  };
  if (!serialization::tryWritePod(file, header)) {
    file.close();
    Storage.remove(CAROUSEL_CACHE_TMP_PATH);
    LOG_ERR("HOME", "carousel: failed to write SD cache header");
    return false;
  }

  const auto start = millis();
  Rect popupRect{};
  uint8_t* progressFrameBuffer = nullptr;
  const size_t bufferSize = renderer.getBufferSize();
  if (showProgressPopup) {
    progressFrameBuffer = static_cast<uint8_t*>(malloc(bufferSize));
    if (!progressFrameBuffer) {
      LOG_ERR("HOME", "carousel: failed to allocate progress overlay buffer");
      showProgressPopup = false;
    } else {
      popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
      GUI.fillPopupProgress(renderer, popupRect, 0);
      memcpy(progressFrameBuffer, frameBuffer, bufferSize);
    }
  }
  bool writeFailed = false;
  for (int i = 0; i < bookCount; ++i) {
    const int cachedSlot = gCarouselCache.findFrameSlot(i);
    if (cachedSlot >= 0 && carouselFrames[cachedSlot]) {
      memcpy(frameBuffer, carouselFrames[cachedSlot], renderer.getBufferSize());
    } else {
      renderCarouselFrameToCurrentBuffer(i, nullptr, nullptr, nullptr);
    }
    if (file.write(frameBuffer, renderer.getBufferSize()) != renderer.getBufferSize()) {
      writeFailed = true;
      break;
    }
    if (showProgressPopup) {
      memcpy(frameBuffer, progressFrameBuffer, bufferSize);
      GUI.fillPopupProgress(renderer, popupRect, ((i + 1) * 100) / bookCount);
    }
  }

  const bool syncOk = file.sync();
  file.close();

  if (writeFailed || !syncOk) {
    free(progressFrameBuffer);
    Storage.remove(CAROUSEL_CACHE_TMP_PATH);
    LOG_ERR("HOME", "carousel: failed to write SD cache snapshot");
    return false;
  }

  if (Storage.exists(CAROUSEL_CACHE_PATH)) {
    Storage.remove(CAROUSEL_CACHE_PATH);
  }
  if (!Storage.rename(CAROUSEL_CACHE_TMP_PATH, CAROUSEL_CACHE_PATH)) {
    free(progressFrameBuffer);
    Storage.remove(CAROUSEL_CACHE_TMP_PATH);
    LOG_ERR("HOME", "carousel: failed to promote SD cache snapshot");
    return false;
  }

  free(progressFrameBuffer);
  LOG_DBG("HOME", "carousel: built SD cache for %d book(s) in %lums", bookCount, millis() - start);
  return true;
}

bool HomeActivity::loadCarouselFrameFromDisk(uint64_t cacheKeyHash, int bookCount, int bookIdx, int slotIdx) {
  if (slotIdx < 0 || slotIdx >= kCarouselFrameCount || !gCarouselCache.frames[slotIdx] || bookIdx < 0 ||
      bookIdx >= bookCount) {
    return false;
  }

  FsFile file;
  if (!Storage.openFileForRead("HOME", CAROUSEL_CACHE_PATH, file)) {
    return false;
  }

  CarouselCacheHeader header{};
  if (!readCarouselCacheHeader(file, header) ||
      !isCarouselCacheHeaderValid(header, cacheKeyHash, bookCount, renderer)) {
    file.close();
    return false;
  }

  const size_t frameOffset = sizeof(CarouselCacheHeader) + static_cast<size_t>(bookIdx) * renderer.getBufferSize();
  if (!file.seek(frameOffset)) {
    file.close();
    return false;
  }
  const size_t expectedBytes = renderer.getBufferSize();
  size_t totalBytesRead = 0;
  while (totalBytesRead < expectedBytes) {
    const int bytesRead = file.read(gCarouselCache.frames[slotIdx] + totalBytesRead, expectedBytes - totalBytesRead);
    if (bytesRead <= 0) {
      break;
    }
    totalBytesRead += static_cast<size_t>(bytesRead);
  }
  file.close();
  if (totalBytesRead != expectedBytes) {
    LOG_ERR("HOME", "carousel: short read for slot %d (%zu/%zu bytes)", slotIdx, totalBytesRead, expectedBytes);
    return false;
  }

  gCarouselCache.frameBookIdx[slotIdx] = bookIdx;
  carouselFrames[slotIdx] = gCarouselCache.frames[slotIdx];
  return true;
}

int HomeActivity::chooseCarouselEvictionSlot(int centerIdx, int bookCount, std::optional<int> protectedBookIdx) const {
  for (int i = 0; i < kCarouselFrameCount; ++i) {
    if (gCarouselCache.frames[i] && gCarouselCache.frameBookIdx[i] < 0) {
      return i;
    }
  }

  int evictSlot = -1;
  int maxDist = -1;
  for (int i = 0; i < kCarouselFrameCount; ++i) {
    if (!gCarouselCache.frames[i]) continue;
    const int cachedBookIdx = gCarouselCache.frameBookIdx[i];
    if (protectedBookIdx.has_value() && cachedBookIdx == protectedBookIdx.value()) continue;
    const int diff = std::abs(cachedBookIdx - centerIdx);
    const int dist = std::min(diff, bookCount - diff);
    if (dist > maxDist) {
      maxDist = dist;
      evictSlot = i;
    }
  }
  return evictSlot;
}

bool HomeActivity::preRenderCarouselFrames(bool showProgressPopup) {
  const int bookCount = static_cast<int>(recentBooks.size());
  if (bookCount == 0) return false;
  bool showedProgressPopup = false;

  // Build cache key from book paths plus thumb-asset availability so we don't
  // reuse a stale snapshot built before carousel-sized thumbs existed.
  std::string newKey;
  uint64_t newKeyHash = 0;
  buildCarouselCacheKey(recentBooks, hasOpdsServers, hasReadingStats, hasBookmarks, hasClippings, newKey, newKeyHash);

  // Cache hit: same books in same order — reuse without any SD reads
  if (newKey == gCarouselCache.key && gCarouselCache.frameCount > 0) {
    for (int i = 0; i < gCarouselCache.frameCount; ++i) carouselFrames[i] = gCarouselCache.frames[i];
    carouselFramesReady = true;
    coverRendered = false;
    coverBufferStored = false;
    return false;
  }

  // Cache miss: free old cache and re-render
  if (!renderer.getFrameBuffer()) return false;
  freeCoverBuffer();  // reclaim 48KB before allocating frames
  gCarouselCache.invalidate();

  const int targetFrameCount = std::min(bookCount, kCarouselFrameCount);
  bool diskCacheValid = false;
  FsFile cacheFile;
  if (Storage.openFileForRead("HOME", CAROUSEL_CACHE_PATH, cacheFile)) {
    CarouselCacheHeader header{};
    const bool readOk = readCarouselCacheHeader(cacheFile, header);
    cacheFile.close();
    diskCacheValid = readOk && isCarouselCacheHeaderValid(header, newKeyHash, bookCount, renderer);
  }

  if (!allocateCarouselFrameSlots(targetFrameCount)) {
    return showedProgressPopup;
  }

  // Keep only the current frame in RAM; adjacent frames come from the SD
  // snapshot on demand instead of occupying another framebuffer-sized slot.
  const int selectedBookIdx = (selectorIndex < bookCount) ? selectorIndex : lastCarouselBookIndex;
  const int initialBookIdx = (selectedBookIdx >= 0 && selectedBookIdx < bookCount) ? selectedBookIdx : 0;
  auto loadOrRender = [&](int bookIdx, int slot) {
    if (!diskCacheValid || !loadCarouselFrameFromDisk(newKeyHash, bookCount, bookIdx, slot)) {
      renderCarouselFrame(bookIdx, slot);
    }
  };
  loadOrRender(initialBookIdx, 0);
  gCarouselCache.lastCenterIdx = initialBookIdx;

  if (gCarouselCache.frameCount >= 2 && bookCount >= 2) {
    const int nextIdx = (initialBookIdx + 1) % bookCount;
    loadOrRender(nextIdx, 1);
  }

  if (gCarouselCache.frameCount >= 3 && bookCount >= 3) {
    const int prevIdx = (initialBookIdx + bookCount - 1) % bookCount;
    loadOrRender(prevIdx, 2);
  }

  const bool hasFullFrameCache = gCarouselCache.frameCount >= targetFrameCount;
  gCarouselCache.key = newKey;
  gCarouselCache.keyHash = diskCacheValid ? newKeyHash : 0;
  carouselFramesReady = true;
  coverRendered = false;
  coverBufferStored = false;

  // Persist the freshly-rendered carousel snapshot back to SD after Home is
  // already visible so later reader->Home returns and carousel navigation can
  // bootstrap from disk instead of live-rendering covers again.
  if (!diskCacheValid && gCarouselCache.frameCount > 0) {
    if (hasFullFrameCache) {
      const bool cacheBuilt = buildCarouselCacheFile(newKey, newKeyHash, bookCount, showProgressPopup);
      if (cacheBuilt) {
        gCarouselCache.keyHash = newKeyHash;
        showedProgressPopup = true;
      }
    } else {
      LOG_INF("HOME", "carousel: skipping SD cache build in degraded frame cache mode");
    }
  }
  return showedProgressPopup;
}

void HomeActivity::loop() {
  const bool inputActive = hasActiveHomeInput();
  if (inputActive) {
    deferHomeWorkAfterInput();
  }

  if (isReadingHomeTheme()) {
    if (!loopReadingHome() && !inputActive) runDeferredHomeWork();
    return;
  }

  if (usesMinimalHomeInteraction()) {
    const int pressedFrontButton = mappedInput.getPressedFrontButton();
    const int releasedFrontButton = mappedInput.getReleasedFrontButton();

    if (minimalSuppressInitialFrontRelease) {
      if (releasedFrontButton >= 0) {
        minimalSuppressInitialFrontRelease = false;
        return;
      }
      if (!isAnyFrontButtonPressed(mappedInput)) {
        minimalSuppressInitialFrontRelease = false;
      }
    }

    if (minimalMenuOpen) {
      const auto menuItems = buildMinimalMenuItems(hasOpdsServers, hasReadingStats, hasBookmarks, hasClippings);
      const int menuCount = static_cast<int>(menuItems.size());
      if (menuCount <= 0) {
        minimalMenuOpen = false;
        minimalHomeNavIndex = -1;
        cleanRefreshPending = true;
        requestUpdate();
        return;
      }

      if (minimalMenuIndex >= menuCount) {
        minimalMenuIndex = menuCount - 1;
      }

      buttonNavigator.onPreviousPress([this, menuCount] {
        minimalMenuIndex = ButtonNavigator::previousIndex(minimalMenuIndex, menuCount);
        requestUpdate();
      });
      buttonNavigator.onNextPress([this, menuCount] {
        minimalMenuIndex = ButtonNavigator::nextIndex(minimalMenuIndex, menuCount);
        requestUpdate();
      });
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        minimalMenuOpen = false;
        minimalHomeNavIndex = -1;
        cleanRefreshPending = true;
        requestUpdate();
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        const HomeMenuEntry& entry = menuItems[minimalMenuIndex];
        if (entry.continueReading) {
          onContinueReading();
        } else {
          onLauncherItemOpen(entry.item);
        }
      }
      return;
    }

    const int homeNavCount = minimalHomeNavCount(!recentBooks.empty());
    if (minimalHomeNavIndex >= homeNavCount) {
      minimalHomeNavIndex = homeNavCount - 1;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      minimalHomeNavIndex = minimalHomeNavIndex < 0 ? homeNavCount - 1
                                                    : ButtonNavigator::previousIndex(minimalHomeNavIndex, homeNavCount);
      requestUpdate();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      minimalHomeNavIndex = minimalHomeNavIndex < 0 ? 0 : ButtonNavigator::nextIndex(minimalHomeNavIndex, homeNavCount);
      requestUpdate();
      return;
    }

    auto activateMinimalHomeNav = [this](int index) {
      switch (index) {
        case 0:
          minimalMenuOpen = true;
          minimalMenuIndex = 0;
          requestUpdate();
          break;
        case 1:
          onFileBrowserOpen();
          break;
        case 2:
          onSettingsOpen();
          break;
        case 3:
          onContinueReading();
          break;
      }
    };

    if (releasedFrontButton == HalGPIO::BTN_BACK) {
      minimalHomeNavIndex = 0;
      activateMinimalHomeNav(minimalHomeNavIndex);
      return;
    }
    if (releasedFrontButton == HalGPIO::BTN_CONFIRM) {
      minimalHomeNavIndex = 1;
      activateMinimalHomeNav(minimalHomeNavIndex);
      return;
    }
    if (releasedFrontButton == HalGPIO::BTN_LEFT) {
      minimalHomeNavIndex = 2;
      activateMinimalHomeNav(minimalHomeNavIndex);
      return;
    }
    if (releasedFrontButton == HalGPIO::BTN_RIGHT) {
      if (!recentBooks.empty()) {
        minimalHomeNavIndex = 3;
        activateMinimalHomeNav(minimalHomeNavIndex);
      }
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (minimalHomeNavIndex >= 0) {
        activateMinimalHomeNav(minimalHomeNavIndex);
      }
      return;
    }
    if (!inputActive) runDeferredHomeWork();
    return;
  }

  const bool isCarousel =
      static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_CAROUSEL;
  const int previousHighlightedBookIdx = getHighlightedBookIndex();
  const int visibleBookCount = getVisibleRecentBookCount();
  bool selectionChanged = false;

  if (isCarousel) {
    const int bookCount = visibleBookCount;
    const int menuItemCount =
        static_cast<int>(buildHomeMenuItems(hasOpdsServers, hasReadingStats, hasBookmarks, hasClippings).size());
    const bool inCarouselRow = (selectorIndex < bookCount);
    const int menuIdx = inCarouselRow ? 0 : (selectorIndex - bookCount);

    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (inCarouselRow && bookCount > 0) {
        selectorIndex = (selectorIndex + 1) % bookCount;
        selectionChanged = true;
      } else if (!inCarouselRow) {
        selectorIndex = bookCount + (menuIdx + 1) % menuItemCount;
        selectionChanged = true;
      }
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (inCarouselRow && bookCount > 0) {
        selectorIndex = (selectorIndex + bookCount - 1) % bookCount;
        selectionChanged = true;
      } else if (!inCarouselRow) {
        selectorIndex = bookCount + (menuIdx + menuItemCount - 1) % menuItemCount;
        selectionChanged = true;
      }
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      if (inCarouselRow) {
        lastCarouselBookIndex = selectorIndex;
        selectorIndex = bookCount;
        invalidateCoverCache();
      } else {
        selectorIndex = lastCarouselBookIndex;
        invalidateCoverCache();
      }
      selectionChanged = true;
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      if (inCarouselRow) {
        lastCarouselBookIndex = selectorIndex;
        selectorIndex = bookCount;
        invalidateCoverCache();
      } else {
        selectorIndex = lastCarouselBookIndex;
        invalidateCoverCache();
      }
      selectionChanged = true;
      requestUpdate();
    }
  } else {
    const int menuCount = getMenuItemCount();
    buttonNavigator.onNext([this, menuCount, &selectionChanged] {
      selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
      selectionChanged = true;
      requestUpdate();
    });
    buttonNavigator.onPrevious([this, menuCount, &selectionChanged] {
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
      selectionChanged = true;
      requestUpdate();
    });
  }

  if (selectionChanged && getHighlightedBookIndex() != previousHighlightedBookIdx) {
    deferHomeWorkAfterInput();
  }

  if (getHighlightedBookIndex() != previousHighlightedBookIdx) {
    if (!bookStatsCached) {
      resetHighlightedBookContextToProgressOnly();
      scheduleHighlightedBookContextRefresh(HOME_DEFERRED_AFTER_INPUT_IDLE_MS);
    } else {
      updateHighlightedBookContext();
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    if (!metrics.homeContinueReadingInMenu && selectorIndex < visibleBookCount) {
      onSelectBook(recentBooks[selectorIndex].path);
      return;
    }

    auto menuItems = buildSelectableHomeMenuItems(hasOpdsServers, hasReadingStats, hasBookmarks, hasClippings,
                                                  metrics.homeContinueReadingInMenu && !recentBooks.empty());
    const int menuSelectedIndex = selectorIndex - getHomeMenuSelectionOffset(recentBooks);
    if (menuSelectedIndex < 0 || menuSelectedIndex >= static_cast<int>(menuItems.size())) {
      return;
    }

    const HomeMenuEntry& entry = menuItems[menuSelectedIndex];
    if (entry.continueReading) {
      onContinueReading();
    } else {
      onLauncherItemOpen(entry.item);
    }
    return;
  }

  if (!inputActive) runDeferredHomeWork();
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (isReadingHomeTheme()) {
    renderReadingHome();
    return;
  }

  if (usesMinimalHomeInteraction()) {
    renderer.clearScreen();

    if (minimalMenuOpen) {
      GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr);
      const auto menuItems = buildMinimalMenuItems(hasOpdsServers, hasReadingStats, hasBookmarks, hasClippings);
      GUI.drawButtonMenu(
          renderer, Rect{0, metrics.homeTopPadding, pageWidth, pageHeight - metrics.homeTopPadding},
          static_cast<int>(menuItems.size()), minimalMenuIndex,
          [&menuItems](int index) { return menuItems[index].label; },
          [&menuItems](int index) { return menuItems[index].icon; });
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      renderer.displayBuffer(nextPaintRefreshMode());
      cleanRefreshPending = false;
      return;
    }

    bool bufferRestored = coverBufferStored && restoreCoverBuffer();
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr);

    coverRectX = 0;
    coverRectY = metrics.homeTopPadding;
    coverRectW = pageWidth;
    coverRectH = metrics.homeCoverTileHeight;

    GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                            recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                            std::bind(&HomeActivity::storeCoverBuffer, this),
                            hasAnyBookStats(currentBookStats) ? &currentBookStats : nullptr, currentBookProgressPercent,
                            &globalStats, currentBookChapterTitle.c_str(), currentBookWordCount);

    const int homeNavCount = minimalHomeNavCount(!recentBooks.empty());
    if (minimalHomeNavIndex >= homeNavCount) {
      minimalHomeNavIndex = homeNavCount - 1;
    }
    MinimalTheme::setHomeButtonHintSelection(minimalHomeNavIndex);
    GUI.drawButtonHints(renderer, tr(STR_MENU), tr(STR_BROWSE), tr(STR_SETTINGS_SHORT),
                        recentBooks.empty() ? "" : tr(STR_READ));

    renderer.displayBuffer(nextPaintRefreshMode());
    cleanRefreshPending = false;

    if (!firstRenderDone.load(std::memory_order_acquire)) {
      markFirstRenderDone();
      return;
    }
    return;
  }

  // Fast path: pre-rendered frames ready — memcpy + border overlay
  if (carouselFramesReady) {
    uint8_t* frameBuffer = renderer.getFrameBuffer();
    const int bookCount = static_cast<int>(recentBooks.size());
    const bool inCarouselRow = (selectorIndex < bookCount);
    const int centerIdx = inCarouselRow ? selectorIndex : lastCarouselBookIndex;
    int slotIdx = gCarouselCache.findFrameSlot(centerIdx);

    if (frameBuffer && slotIdx < 0 && gCarouselCache.keyHash != 0 && bookCount > 0) {
      const int evictSlot = chooseCarouselEvictionSlot(centerIdx, bookCount);
      if (evictSlot >= 0 && loadCarouselFrameFromDisk(gCarouselCache.keyHash, bookCount, centerIdx, evictSlot)) {
        slotIdx = evictSlot;
      }
    }

    if (frameBuffer && slotIdx >= 0 && carouselFrames[slotIdx]) {
      memcpy(frameBuffer, carouselFrames[slotIdx], renderer.getBufferSize());
      LyraCarouselTheme::setPreRenderIndex(centerIdx);

      // Cached carousel frames include the header; redraw it so dynamic values
      // like battery percentage and clock are current for every restored frame.
      GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr);
      GUI.drawCarouselBorder(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                             recentBooks, centerIdx, inCarouselRow);
      if (!inCarouselRow) {
        const auto menuItems = buildHomeMenuItems(hasOpdsServers, hasReadingStats, hasBookmarks, hasClippings);
        if (static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) ==
            CrossPointSettings::UI_THEME::LYRA_CAROUSEL) {
          static_cast<const LyraCarouselTheme&>(GUI).drawButtonMenuSelectionOverlay(
              renderer, static_cast<int>(menuItems.size()), selectorIndex - recentBooks.size(),
              [&menuItems](int index) { return menuItems[index].label; },
              [&menuItems](int index) { return menuItems[index].icon; });
        }
      }

      renderer.displayBuffer(nextPaintRefreshMode());
      cleanRefreshPending = false;
      // E-ink refresh complete — pre-render the missing adjacent frame while idle.
      updateSlidingWindowCache(centerIdx, bookCount);
      if (!firstRenderDone.load(std::memory_order_acquire)) {
        markFirstRenderDone();
      }
      return;
    }
  }

  renderer.clearScreen();
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding},
                 metrics.homeContinueReadingInMenu && !recentBooks.empty() ? recentBooks[0].title.c_str() : nullptr);

  // Record the tile rect so storeCoverBuffer (called from the theme) knows
  // which sub-region of the framebuffer to snapshot. ~16 KB in Portrait
  // instead of the 48 KB full framebuffer the previous bind captured.
  coverRectX = 0;
  coverRectY = metrics.homeTopPadding;
  coverRectW = pageWidth;
  coverRectH = metrics.homeCoverTileHeight;

  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this),
                          hasAnyBookStats(currentBookStats) ? &currentBookStats : nullptr, currentBookProgressPercent);

  auto menuItems = buildSelectableHomeMenuItems(hasOpdsServers, hasReadingStats, hasBookmarks, hasClippings,
                                                metrics.homeContinueReadingInMenu && !recentBooks.empty());

  const int menuStartY = metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset;
  const int menuEndY = pageHeight - metrics.buttonHintsHeight;
  const int menuHeight = std::max(0, menuEndY - menuStartY);

  GUI.drawButtonMenu(
      renderer, Rect{0, menuStartY, pageWidth, menuHeight}, static_cast<int>(menuItems.size()),
      selectorIndex - getHomeMenuSelectionOffset(recentBooks),
      [&menuItems](int index) { return menuItems[index].label; },
      [&menuItems](int index) { return menuItems[index].icon; });

  const bool isCarouselTheme =
      static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_CAROUSEL;
  const auto labels = isCarouselTheme ? mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT))
                                      : mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(nextPaintRefreshMode());
  cleanRefreshPending = false;

  if (!firstRenderDone.load(std::memory_order_acquire)) {
    markFirstRenderDone();
    return;
  }
}

void HomeActivity::renderCarouselFrame(int bookIdx, int slotIdx) {
  const auto start = millis();
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer || !gCarouselCache.frames[slotIdx]) return;
  BookReadingStats frameStats;
  float frameProgressPercent = -1.0f;
  bool usedCachedStats = false;
  renderCarouselFrameToCurrentBuffer(bookIdx, &frameStats, &frameProgressPercent, &usedCachedStats);

  memcpy(gCarouselCache.frames[slotIdx], frameBuffer, renderer.getBufferSize());
  gCarouselCache.frameBookIdx[slotIdx] = bookIdx;
  carouselFrames[slotIdx] = gCarouselCache.frames[slotIdx];
  LOG_DBG("HOME", "carousel: renderCarouselFrame book=%d slot=%d cached=%s took %lums", bookIdx, slotIdx,
          usedCachedStats ? "yes" : "no", millis() - start);
}

void HomeActivity::updateSlidingWindowCache(int centerIdx, int bookCount) {
  (void)centerIdx;
  (void)bookCount;
  // The current carousel cache keeps one frame in RAM; other frames are paged
  // from the SD snapshot cache on demand in render().
}

void HomeActivity::onSelectBook(const std::string& path) {
  gCarouselCache.invalidate();
  freeCarouselFrames();
  activityManager.goToReader(path);
}

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onLibrarySearchOpen() {
  startActivityForResult(std::make_unique<LibrarySearchActivity>(renderer, mappedInput),
                         [this](const ActivityResult&) { requestUpdate(); });
}

void HomeActivity::onContinueReading() {
  if (!recentBooks.empty()) {
    onSelectBook(recentBooks[0].path);
  }
}

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }

void HomeActivity::onReadingStatsOpen(const BookStatsActivity::InitialPage initialPage) {
  CurrentBookStatsTarget lastActiveBook;
  const bool hasLastActiveBook = CurrentBookStats::loadLastActive(lastActiveBook);
  const int highlightedBookIdx = getHighlightedBookIndex();
  const std::string fallbackTitle =
      highlightedBookIdx >= 0 ? recentBooks[highlightedBookIdx].title : std::string(tr(STR_READING_STATS));
  const std::string bookPath = getCurrentBookPath();
  const std::string fallbackCachePath = FsHelpers::hasEpubExtension(bookPath)
                                            ? Epub::cachePathForFilePath(bookPath, DUET_BOOKS_ROOT_PATH "")
                                            : std::string{};
  const std::string& bookTitle = hasLastActiveBook ? lastActiveBook.title : fallbackTitle;
  const std::string& cachePath = hasLastActiveBook ? lastActiveBook.cachePath : fallbackCachePath;
  const BookReadingStats& bookStats = hasLastActiveBook ? lastActiveBook.stats : currentBookStats;
  const float bookProgressPercent = hasLastActiveBook ? lastActiveBook.progressPercent : currentBookProgressPercent;
  const uint32_t bookWordCount = hasLastActiveBook ? lastActiveBook.wordCount : currentBookWordCount;
  if (showAllDevicesStats) {
    startActivityForResult(
        std::make_unique<BookStatsActivity>(renderer, mappedInput, bookTitle, cachePath, bookStats, bookProgressPercent,
                                            false, 0, globalStats, allDevicesGlobalStats, true,
                                            ReadingSessionSnapshot{}, bookWordCount, initialPage),
        [this](const ActivityResult& result) {
          mappedInput.suppressNextConfirmRelease();
          const auto* statsResult = std::get_if<ReadingStatsResult>(&result.data);
          if (statsResult && statsResult->changed) {
            globalStats = GlobalReadingStats::load();
            showAllDevicesStats = GlobalReadingStats::hasSyncedStats();
            allDevicesGlobalStats = showAllDevicesStats ? GlobalReadingStats::loadAggregated(globalStats) : globalStats;
            bookStatsCached = false;
            updateHighlightedBookContext();
          }
          requestUpdate();
        });
  } else {
    startActivityForResult(std::make_unique<BookStatsActivity>(renderer, mappedInput, bookTitle, cachePath, bookStats,
                                                               bookProgressPercent, false, 0, globalStats, true,
                                                               ReadingSessionSnapshot{}, bookWordCount, initialPage),
                           [this](const ActivityResult& result) {
                             mappedInput.suppressNextConfirmRelease();
                             const auto* statsResult = std::get_if<ReadingStatsResult>(&result.data);
                             if (statsResult && statsResult->changed) {
                               globalStats = GlobalReadingStats::load();
                               showAllDevicesStats = GlobalReadingStats::hasSyncedStats();
                               allDevicesGlobalStats =
                                   showAllDevicesStats ? GlobalReadingStats::loadAggregated(globalStats) : globalStats;
                               bookStatsCached = false;
                               updateHighlightedBookContext();
                             }
                             requestUpdate();
                           });
  }
}

void HomeActivity::onSavedItemsOpen() {
  startActivityForResult(std::make_unique<SavedItemsHomeActivity>(renderer, mappedInput),
                         [this](const ActivityResult&) { requestUpdate(); });
}

void HomeActivity::onUtilitiesOpen() {
  startActivityForResult(std::make_unique<UtilitiesActivity>(renderer, mappedInput),
                         [this](const ActivityResult&) { requestUpdate(); });
}

void HomeActivity::onLauncherItemOpen(const LauncherItem item) {
  switch (item) {
    case LauncherItem::BrowseFiles:
      onFileBrowserOpen();
      break;
    case LauncherItem::SearchLibrary:
      onLibrarySearchOpen();
      break;
    case LauncherItem::RecentBooks:
      onRecentsOpen();
      break;
    case LauncherItem::ReadingStats:
      onReadingStatsOpen(BookStatsActivity::InitialPage::CurrentBook);
      break;
    case LauncherItem::ReadingHeatmap:
      onReadingStatsOpen(BookStatsActivity::InitialPage::Heatmap);
      break;
    case LauncherItem::ReadingProfile:
      onReadingStatsOpen(BookStatsActivity::InitialPage::ReadingProfile);
      break;
    case LauncherItem::SavedItems:
      onSavedItemsOpen();
      break;
    case LauncherItem::Favorites:
      startActivityForResult(std::make_unique<FavoritesActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;
    case LauncherItem::Achievements:
      startActivityForResult(std::make_unique<AchievementsActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;
    case LauncherItem::Dictionary:
      startActivityForResult(std::make_unique<DictionaryActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;
    case LauncherItem::Tetris:
      startActivityForResult(std::make_unique<TetrisActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;
    case LauncherItem::IfFound:
      startActivityForResult(std::make_unique<IfFoundActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;
    case LauncherItem::ScreenClean:
      startActivityForResult(std::make_unique<ScreenCleanActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;
    case LauncherItem::NearbyStatsSync:
      activityManager.goToNearbyStatsSync();
      break;
    case LauncherItem::FileTransfer:
      onFileTransferOpen();
      break;
    case LauncherItem::OpdsBrowser:
      onOpdsBrowserOpen();
      break;
    case LauncherItem::KOReaderSync:
      startActivityForResult(std::make_unique<KOReaderSettingsActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;
    case LauncherItem::Sleep:
      enterDeepSleep();
      break;
    case LauncherItem::ReadMe:
      startActivityForResult(std::make_unique<ReadMeActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;
    case LauncherItem::Apps:
      onUtilitiesOpen();
      break;
    case LauncherItem::Settings:
      onSettingsOpen();
      break;
    case LauncherItem::CustomizeHomeApps:
    case LauncherItem::Count:
      break;
  }
}
