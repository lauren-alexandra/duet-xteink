#pragma once

#include <FileIndex.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class FileBrowserActivity final : public Activity {
 public:
  // Books = standard reader browser; PickFirmware = filter to .bin only and return path via ActivityResult.
  enum class Mode { Books, PickFirmware };

 private:
  // Picker instrumentation (measurement only): cumulative costs of the shelf
  // build and the background cover workers, dumped to a card breadcrumb on
  // exit so grid/carousel slowness can be attributed before optimizing.
  unsigned long pickerEnterMs = 0;
  unsigned long pickerFirstRenderMs = 0;
  unsigned long pickerLoadFilesMs = 0;
  unsigned long pickerSignalTotalMs = 0;
  unsigned long pickerSignalMaxMs = 0;
  unsigned long pickerGenTotalMs = 0;
  unsigned long pickerGenMaxMs = 0;
  uint16_t pickerSignalCount = 0;
  uint16_t pickerGenCount = 0;
  // Last observed user input in the cover views; background work waits for a
  // real idle window past this before touching the SD card.
  unsigned long lastPickerInputMs = 0;

  enum class BookStatus : uint8_t { Unread = 1, Reading = 2, Finished = 3 };
  enum class BookStatusFilter : uint8_t { All = 0, Reading = 1, Unread = 2, Finished = 3 };
  enum class CoverRefresh : uint8_t {
    None = 0,
    Full = 1,
    GridSelection = 2,
    GridFocus = 3,
    GridTitle = 4,
    CarouselTitle = 5
  };
  struct CoverBookSignal {
    std::string entry;
    BookStatus status = BookStatus::Unread;
    int progressPercent = -1;
  };
  struct CachedCoverBitmap {
    std::string entry;
    std::unique_ptr<uint8_t[]> rows;
    size_t rowDataSize = 0;
    int requestedWidth = 0;
    int requestedHeight = 0;
    int width = 0;
    int height = 0;
    int rowBytes = 0;
    bool topDown = false;
    uint8_t blackPaletteIndex = 0;
    // A lower-resolution cached thumbnail is still useful for first paint;
    // this distinguishes it from the exact target tile that the idle worker
    // should generate for the next refresh.
    bool exactSize = false;

    bool isReady() const { return rows != nullptr && width > 0 && height > 0 && rowBytes > 0; }
    bool matches(const std::string& candidateEntry, int candidateWidth, int candidateHeight) const {
      return entry == candidateEntry && requestedWidth == candidateWidth && requestedHeight == candidateHeight;
    }
  };

  // Deletion
  void promptDeleteFile(const std::string& fullPath, const std::string& entry);
  void promptDeleteDirectory(const std::string& fullPath, const std::string& entry,
                             bool ignoreInitialConfirmRelease = false);
  void showDirectoryActionMenu(const std::string& entry, bool ignoreInitialConfirmRelease = false);
  void pinSleepFavorite(const std::string& fullPath);
  void unpinSleepFavorite();
  bool isPinnedSleepFavorite(const std::string& fullPath) const;
  void setPreferredSleepFolder(const std::string& fullPath);
  void clearPreferredSleepFolder();
  bool isPreferredSleepFolder(const std::string& fullPath) const;
  bool isSleepFavoriteFolder(const std::string& fullPath) const;
  void showFileActionMenu(const std::string& entry, bool ignoreInitialConfirmRelease = false);
  void showLibrarySearch();
  void showBookSortMenu();
  void navigateBack();
  void navigateIntoDirectory(const std::string& entry);
  bool processPendingFolderNavigation();
  void cancelCoverRenderForExit();

  enum class PendingFolderNavigation : uint8_t { None = 0, Back = 1, Into = 2 };
  PendingFolderNavigation pendingFolderNavigation = PendingFolderNavigation::None;
  std::string pendingDirectoryEntry;

  ButtonNavigator buttonNavigator;

  size_t selectorIndex = 0;

  bool lockLongPressBack = false;
  bool longPressBackHandled = false;
  std::atomic<bool> folderTransitionInProgress{false};
  // When the transition flag was last raised; cancelCoverRenderForExit() sets
  // it with no matching clear, so loop() self-heals a suppression that
  // outlives its exit instead of never painting again.
  unsigned long folderTransitionSetMs = 0UL;
  bool longPressConfirmHandled = false;
  bool pendingCompletedFeedback = false;
  bool completedFeedbackIsFinished = false;
  unsigned long completedFeedbackShowTime = 0UL;
  // True when this activity was entered while Confirm was already held; we must swallow the next
  // release so we don't immediately auto-open the first entry.
  bool lockNextConfirmRelease = false;

  Mode mode = Mode::Books;

  // Files state
  static constexpr size_t INDEX_ROW_CACHE_SIZE = 32;
  std::string basepath = "/";
  std::vector<std::string> files;
  std::unique_ptr<char[]> fileNameBuffer;
  std::unique_ptr<FileIndex> fileIndex;
  std::unique_ptr<FileIndex::Entry> indexEntry;
  std::array<std::string, INDEX_ROW_CACHE_SIZE> indexCachedNames;
  std::array<size_t, INDEX_ROW_CACHE_SIZE> indexCachedRows{};
  bool usingIndex = false;
  bool fileListMemoryLimited = false;
  bool coverGridAvailable = false;
  static constexpr int NO_COVER_PAGE_LOADED = -1;
  int loadedCoverPageStart = NO_COVER_PAGE_LOADED;
  int loadedCoverItemsPerPage = 0;
  int loadedCoverWidth = 0;
  int loadedCoverHeight = 0;
  int loadedCarouselCenterIndex = NO_COVER_PAGE_LOADED;
  int loadedCarouselSideWidth = 0;
  int loadedCarouselSideHeight = 0;
  // CrumBLE's densest bookshelf page is 4x4, so every visible book can
  // carry its progress signal without a second metadata pass during render.
  static constexpr size_t COVER_SIGNAL_COUNT = 16;
  std::array<CoverBookSignal, COVER_SIGNAL_COUNT> loadedCoverSignals{};
  // Status/progress lookups can fall back to small per-book cache files when
  // the compact library index is cold. Queue them just like covers so a new
  // grid page can paint and accept navigation before any fallback disk work.
  std::array<int, COVER_SIGNAL_COUNT> pendingCoverSignalIndices{};
  std::array<size_t, COVER_SIGNAL_COUNT> pendingCoverSignalSlots{};
  size_t pendingCoverSignalCount = 0;
  size_t pendingCoverSignalNext = 0;
  unsigned long pendingCoverSignalNextAt = 0UL;
  // Address space for the visible CrumBLE-style page plus the page on either
  // side. The idle loader applies a heap floor while filling the two off-page
  // windows, so low-memory devices may retain only part of each neighbor.
  static constexpr size_t COVER_BITMAP_CACHE_COUNT = COVER_SIGNAL_COUNT * 3;
  std::array<CachedCoverBitmap, COVER_BITMAP_CACHE_COUNT> loadedCoverBitmaps{};
  // Five visible covers plus one hidden look-ahead cover in each direction.
  // Once the initial window hydrates, an ordinary next/previous move already
  // has the newly entering outside cover in RAM.
  static constexpr size_t CAROUSEL_DETAIL_CACHE_COUNT = 7;
  std::array<CachedCoverBitmap, CAROUSEL_DETAIL_CACHE_COUNT> loadedCarouselDetailBitmaps{};
  std::array<int, CAROUSEL_DETAIL_CACHE_COUNT> pendingCarouselDetailIndices{};
  std::array<size_t, CAROUSEL_DETAIL_CACHE_COUNT> pendingCarouselDetailSlots{};
  size_t pendingCarouselDetailCount = 0;
  size_t pendingCarouselDetailNext = 0;
  size_t pendingVisibleCarouselDetailCount = 0;
  unsigned long pendingCarouselDetailNextAt = 0UL;
  static constexpr size_t CAROUSEL_VISIBLE_COUNT = 5;
  std::array<int, CAROUSEL_VISIBLE_COUNT> pendingCarouselSignalIndices{};
  std::array<size_t, CAROUSEL_VISIBLE_COUNT> pendingCarouselSignalSlots{};
  size_t pendingCarouselSignalCount = 0;
  size_t pendingCarouselSignalNext = 0;
  unsigned long pendingCarouselSignalNextAt = 0UL;
  std::array<int, COVER_BITMAP_CACHE_COUNT> pendingCoverPrefetchIndices{};
  std::array<size_t, COVER_BITMAP_CACHE_COUNT> pendingCoverPrefetchSlots{};
  std::array<uint16_t, COVER_BITMAP_CACHE_COUNT> pendingCoverPrefetchWidths{};
  std::array<uint16_t, COVER_BITMAP_CACHE_COUNT> pendingCoverPrefetchHeights{};
  size_t pendingCoverPrefetchCount = 0;
  size_t pendingCoverPrefetchNext = 0;
  size_t pendingVisibleCoverPrefetchCount = 0;
  unsigned long pendingCoverPrefetchNextAt = 0UL;
  std::array<int, COVER_SIGNAL_COUNT> pendingCoverGenerationIndices{};
  size_t pendingCoverGenerationCount = 0;
  size_t pendingCoverGenerationNext = 0;
  unsigned long pendingCoverGenerationNextAt = 0UL;
  bool pendingCoverGenerationCancelled = false;
  // Disk-backed thumbnails are useful, but must never make directional input
  // feel sticky. Delay them until the shelf has been quiet, then coalesce all
  // resulting artwork into one repaint.
  unsigned long coverBackgroundWorkNotBefore = 0UL;
  unsigned long coverBackgroundRefreshNotBefore = 0UL;
  bool coverBackgroundRefreshPending = false;
  // Navigation gets an immediate tiny paint, then the expensive artwork frame
  // waits for the user to pause so repeated button presses never build a queue.
  CoverRefresh deferredCoverNavigationRefresh = CoverRefresh::None;
  unsigned long deferredCoverNavigationRefreshNotBefore = 0UL;
  bool suppressNextCoverPagePaint = false;
  std::unique_ptr<uint8_t[]> coverGridSelectionBackground;
  size_t coverGridSelectionBackgroundCapacity = 0;
  size_t coverGridSelectionBackgroundSize = 0;
  size_t coverGridSelectionBackgroundIndex = static_cast<size_t>(-1);
  int coverGridSelectionBackgroundPageStart = NO_COVER_PAGE_LOADED;
  int coverGridSelectionBackgroundX = 0;
  int coverGridSelectionBackgroundY = 0;
  int coverGridSelectionBackgroundWidth = 0;
  int coverGridSelectionBackgroundHeight = 0;
  std::unique_ptr<uint8_t[]> coverGridFooterBackground;
  size_t coverGridFooterBackgroundCapacity = 0;
  size_t coverGridFooterBackgroundSize = 0;
  int coverGridFooterBackgroundPageStart = NO_COVER_PAGE_LOADED;
  int coverGridFooterBackgroundX = 0;
  int coverGridFooterBackgroundY = 0;
  int coverGridFooterBackgroundWidth = 0;
  int coverGridFooterBackgroundHeight = 0;
  BookStatusFilter bookStatusFilter = BookStatusFilter::All;

  // Render publishes the measured selection and main loop advances only a
  // character counter, so the marquee adds no task and no per-tick buffer.
  std::atomic<size_t> titleMarqueeStep{0};
  std::atomic<size_t> titleMarqueeEndStep{0};
  std::atomic<uint32_t> titleMarqueeEntryHash{0};
  std::atomic<unsigned long> titleMarqueeNextStepAt{0UL};
  // Completed wrap-arounds for the current entry. In plain-list mode every
  // marquee step is a full repaint + e-ink refresh, so scrolling stops after
  // TITLE_MARQUEE_MAX_LOOPS instead of refreshing the panel forever.
  std::atomic<uint8_t> titleMarqueeLoops{0};
  // Full shelf renders are comparatively expensive on the X3. Stamp every
  // request so a full repaint queued before a newer cursor move can be safely
  // discarded instead of making navigation wait behind stale cover work.
  std::atomic<uint32_t> coverNavigationEpoch{0};
  std::atomic<uint32_t> pendingCoverRefreshEpoch{0};
  std::atomic<uint8_t> pendingCoverRefresh{static_cast<uint8_t>(CoverRefresh::None)};
  // These describe what is actually visible on the panel, rather than the
  // latest logical selection. They let the focus ring move even when a cover
  // refresh invalidated the optional framebuffer snapshot.
  std::atomic<size_t> paintedCoverGridSelection{static_cast<size_t>(-1)};
  std::atomic<int> paintedCoverGridPageStart{NO_COVER_PAGE_LOADED};
  std::atomic<int> paintedCarouselCenterIndex{NO_COVER_PAGE_LOADED};

  // Data loading
  void clearIndexNameCache();
  void loadFiles(bool forceRescan = false);
  bool loadFilesIntoVector(size_t cap, bool& overflow);
  size_t entryCount() const;
  const char* entryNameAt(size_t row);
  void toggleHiddenFiles();
  size_t findEntry(const std::string& name);
  void resetTitleMarquee();
  void updateTitleMarquee(const std::string& entry);
  void measureTitleMarquee(const std::string& entry, int maxWidth = 0, int maxLines = 1);
  void requestFastCoverUpdate(CoverRefresh refresh);
  std::string rowValueForEntry(const std::string& entry) const;
  void resetCoverPage();
  void deferCoverBackgroundWork();
  bool canRunCoverBackgroundWork() const;
  void queueCoverNavigationRefresh(CoverRefresh refresh);
  bool refreshCoverNavigationWhenIdle(bool inputHeld);
  void queueCoverBackgroundRefresh();
  bool claimCoverBackgroundRefresh();
  bool refreshCoverBrowserWhenIdle(bool inputHeld);
  static bool isFocusCoverRefresh(CoverRefresh refresh);
  void invalidateCoverGridSelectionBackground();
  bool captureCoverGridSelectionBackground(int x, int y, int width, int height, size_t index, int pageStart);
  bool restoreCoverGridSelectionBackground(size_t expectedIndex, int expectedPageStart);
  bool captureCoverGridFooterBackground(int x, int y, int width, int height, int pageStart);
  bool restoreCoverGridFooterBackground(int expectedPageStart);
  bool moveCoverGridSelectionFromSnapshot();
  bool refreshCoverGridFocus();
  bool refreshCoverGridSelectionFromSnapshot();
  bool refreshCoverCarouselTitle();
  void refreshCoverGridAvailability();
  void applyBookStatusFilter();
  void showBookStatusFilterMenu();
  CoverBookSignal loadBookSignal(const std::string& entry, bool includeProgress) const;
  CoverBookSignal loadBookSignalUncached(const std::string& entry, bool includeProgress) const;
  // Per-book signal results survive page turns (grids re-request the same
  // entries on every pass). 16 bytes per book; keyed by full-path hash.
  struct CoverSignalMemoEntry {
    uint64_t key = 0;
    int8_t progressPercent = -1;
    uint8_t status = 0;
    bool progressComputed = false;
  };
  mutable std::vector<CoverSignalMemoEntry> coverSignalMemo;
  void writePickerHeartbeat(const char* phase, int detail) const;
  void releaseNonVisibleCoverBitmapsForHeap();
  void releaseNonVisibleCoverBitmapsForHeapLocked();
  uint16_t pickerGenSkipHeap = 0;
  uint16_t pickerGenQueuedPeak = 0;
  unsigned long lastGenSkipHeartbeatMs = 0UL;
  const CoverBookSignal* coverSignalForEntry(const std::string& entry) const;
  bool loadCoverBitmap(const std::string& entry, int width, int height, CachedCoverBitmap& cachedCover) const;
  const CachedCoverBitmap* coverBitmapForEntry(const std::string& entry, int width, int height) const;
  const CachedCoverBitmap* carouselDetailBitmapForEntry(const std::string& entry) const;
  bool matchesBookStatusFilter(BookStatus status) const;
  bool isCoverGridActive() const;
  bool isCoverCarouselActive() const;
  std::string coverThumbPathForEntry(const std::string& entry, int width, int height) const;
  void loadCoverPage(int pageStart, int itemsPerPage, int width, int height);
  void prefetchNextQueuedCoverSignal();
  void prefetchNextQueuedCover();
  void prefetchNextCarouselCover();
  void prefetchNextCarouselDetail();
  void prefetchNextCarouselSignal();
  static bool shouldCancelQueuedCoverGeneration(void* context);
  void generateNextQueuedCover();
  void loadCarouselCovers(int centerIndex, int centerWidth, int centerHeight, int sideWidth, int sideHeight);
  bool drawCoverArtwork(const std::string& entry, int x, int y, int width, int height, int cornerRadius,
                        int leftHeight = -1, int rightHeight = -1);
  void drawCoverReadingSignal(const std::string& entry, int x, int y, int width, int height);

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/",
                               Mode mode = Mode::Books)
      : Activity("FileBrowser", renderer, mappedInput),
        mode(mode),
        basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  void requestUpdate(bool immediate = false) override;
#ifdef SIMULATOR
  bool simulatorVerifyCoverBackgroundRefreshCoalescing();
#endif
};
