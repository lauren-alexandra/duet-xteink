#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "./FileBrowserActivity.h"
#include "LauncherLayoutStore.h"
#include "activities/Activity.h"
#include "activities/reader/BookReadingStats.h"
#include "activities/reader/BookStatsActivity.h"
#include "activities/reader/GlobalReadingStats.h"
#include "util/ButtonNavigator.h"

struct RecentBook;
struct Rect;

class HomeActivity final : public Activity {
 public:
  // Keep one rendered carousel frame in RAM. Additional frames remain available
  // through the SD snapshot cache and are paged in on demand.
  static constexpr int kCarouselFrameCount = 1;
  // Must cover the carousel and Reading Home (current book + three recents).
  static constexpr int kMaxCachedBooks = 4;

 private:
  enum class DeferredHomeWork : uint8_t { Metadata, BookContext, Covers, Carousel, Achievements, Complete };

  ButtonNavigator buttonNavigator;
  // Set by loadRecentCovers in place of per-cover repaints; flushed once when
  // cover loading completes.
  bool coverRepaintPending = false;
  // Next Home frame replaces arbitrary full-screen content (activity return,
  // menu close). Present it with HALF_REFRESH — the X3 driver promotes that to
  // its clean sync, preventing the dismissed screen from ghosting through a
  // single differential pass. Normal navigation stays on FAST.
  bool cleanRefreshPending = false;
  HalDisplay::RefreshMode nextPaintRefreshMode();
  // Repair15 phase-1 instrumentation (measurement only).
  unsigned long timingOnEnterMs = 0;
  unsigned long timingFirstPaintMs = 0;
  bool timingWritten = false;
  int selectorIndex = 0;
  int lastCarouselBookIndex = 0;  // remembered position when leaving carousel row
  bool recentsLoading = false;
  bool recentsLoaded = false;
  std::atomic<bool> firstRenderDone{false};
  DeferredHomeWork deferredHomeWork = DeferredHomeWork::Complete;
  int deferredBookStatsIdx = 0;  // next recent-book slot for the sliced BookContext step
  int deferredCoverIdx = 0;      // next recent-book slot for sliced Reading Home cover hydration
  std::array<char, kMaxCachedBooks> deferredCoverUpdated{};
  std::atomic<unsigned long> deferredHomeWorkNotBefore{0UL};
  bool initialMenuSelectionPending = false;
  bool hasReadingStats = false;
  bool hasBookmarks = false;
  bool hasClippings = false;
  bool hasOpdsServers = false;
  bool minimalMenuOpen = false;
  bool minimalSuppressInitialFrontRelease = false;
  int minimalMenuIndex = 0;
  int minimalHomeNavIndex = -1;
  bool coverRendered = false;      // Track if cover has been rendered once
  bool coverBufferStored = false;  // Track if cover buffer is stored
  uint8_t* coverBuffer = nullptr;  // HomeActivity's own buffer for cover image
  size_t coverBufferSize = 0;      // Bytes allocated to coverBuffer
  // Logical rect last passed to drawRecentBookCover. The cover snapshot only
  // needs to cover this region, not the entire framebuffer, so we cache the
  // tile instead of all 48 KB. Set in render() before the call.
  int coverRectX = 0;
  int coverRectY = 0;
  int coverRectW = 0;
  int coverRectH = 0;
  float currentBookProgressPercent = -1.0f;
  uint32_t currentBookWordCount = 0;
  std::string currentBookChapterTitle;
  BookReadingStats currentBookStats;
  GlobalReadingStats globalStats;
  GlobalReadingStats allDevicesGlobalStats;
  bool showAllDevicesStats = false;
  uint32_t readingHomeTodaySeconds = 0;
  uint16_t readingHomeCurrentStreak = 0;
  bool readingHomeFrameReady = false;
  bool readingHomeSelectionOnlyPending = false;

  // Per-book stats and progress cached at onEnter() to avoid SD reads during navigation.
  std::array<BookReadingStats, kMaxCachedBooks> cachedBookStats{};
  std::array<float, kMaxCachedBooks> cachedBookProgress{};
  std::array<uint32_t, kMaxCachedBooks> cachedBookWordCounts{};
  bool bookStatsCached = false;

  uint8_t* carouselFrames[kCarouselFrameCount] = {};
  bool carouselFramesReady = false;
  bool carouselWarmupPending = false;

  std::vector<RecentBook> recentBooks;
  const HomeMenuItem initialMenuItem;

  void onSelectBook(const std::string& path);
  void onFileBrowserOpen();
  void onLibrarySearchOpen();
  void onContinueReading();
  void onRecentsOpen();
  void onSettingsOpen();
  void onFileTransferOpen();
  void onOpdsBrowserOpen();
  void onReadingStatsOpen(BookStatsActivity::InitialPage initialPage = BookStatsActivity::InitialPage::CurrentBook);
  void onSavedItemsOpen();
  void onUtilitiesOpen();
  void onLauncherItemOpen(LauncherItem item);

  int getMenuItemCount() const;
  bool storeCoverBuffer();    // Store frame buffer for cover image
  bool restoreCoverBuffer();  // Restore frame buffer from stored cover
  void freeCoverBuffer();     // Free the stored cover buffer
  void invalidateCoverCache();
  bool preRenderCarouselFrames(bool showProgressPopup = false);
  void freeCarouselFrames();
  bool allocateCarouselFrameSlots(int targetFrameCount);
  bool buildCarouselCacheFile(const std::string& cacheKey, uint64_t cacheKeyHash, int bookCount,
                              bool showProgressPopup = false);
  bool loadCarouselFrameFromDisk(uint64_t cacheKeyHash, int bookCount, int bookIdx, int slotIdx);
  int chooseCarouselEvictionSlot(int centerIdx, int bookCount,
                                 std::optional<int> protectedBookIdx = std::nullopt) const;
  void renderCarouselFrameToCurrentBuffer(int bookIdx, BookReadingStats* outStats, float* outProgressPercent,
                                          bool* outUsedCachedStats);
  void renderCarouselFrame(int bookIdx, int slotIdx);
  void updateSlidingWindowCache(int centerIdx, int bookCount);
  int getHighlightedBookIndex() const;
  int getVisibleRecentBookCount() const;
  void updateHighlightedBookStatsOnly();
  void updateHighlightedBookContext();
  void refreshReadingHomeToday();
  bool loopReadingHome();
  void renderReadingHome();
  void markFirstRenderDone();
  void runDeferredHomeWork();
  bool hasActiveHomeInput() const;
  void applyInitialMenuSelection();
  void loadRecentBooks(int maxBooks, const std::string& activeBookPath);
  void loadRecentCovers(int coverHeight);

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                        HomeMenuItem initialMenuItemValue = HomeMenuItem::NONE)
      : Activity("Home", renderer, mappedInput), initialMenuItem(initialMenuItemValue) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  std::string getCurrentBookPath() const override;
#ifdef SIMULATOR
  int simulatorSelectorIndex() const { return selectorIndex; }
  bool simulatorReadingHomeSelectionPending() const { return readingHomeSelectionOnlyPending; }
  void simulatorOverrideBookWordCount(uint32_t wordCount) { currentBookWordCount = wordCount; }
#endif
};
