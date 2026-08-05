#pragma once
#include <Epub.h>
#include <Epub/FootnoteEntry.h>
#include <Epub/Section.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "BookReadingStats.h"
#include "BookmarkStore.h"
#include "EndOfBookOptions.h"
#include "EpubReaderMenuActivity.h"
#include "GlobalReadingStats.h"
#include "activities/Activity.h"

class EpubReaderActivity final : public Activity {
 public:
  struct ReaderSettingsSnapshot {
    uint8_t fontFamily = 0;
    uint8_t fontSize = 0;
    uint8_t lineHeightPercent = 100;
    uint8_t orientation = 0;
    uint8_t screenMargin = 5;
    uint8_t publisherPageNumbers = 0;
    uint8_t paragraphAlignment = 0;
    uint8_t embeddedStyle = 1;
    uint8_t hyphenationEnabled = 0;
    uint8_t textAntiAliasing = 1;
    uint8_t textDarkness = 0;
    uint8_t readerRefreshMode = 0;
    uint8_t readerDarkMode = 0;
    uint8_t imageRendering = 0;
    uint8_t extraParagraphSpacing = 1;
    uint8_t forceParagraphIndents = 0;
    uint8_t bionicReadingEnabled = 0;
    uint8_t guideReadingEnabled = 0;
    uint8_t epubRenderMode = 0;
    char sdFontFamilyName[64] = "";
    uint8_t sdFontPointSize = 0;
  };

 private:
  std::shared_ptr<Epub> epub;
  std::unique_ptr<Section> section = nullptr;
  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  int activeSectionFontId = 0;
  std::optional<uint16_t> pendingPageJump;
  // Set when navigating to a footnote href with a fragment (e.g. #note1).
  // Cleared on the next render after the new section loads and resolves it to a page.
  std::string pendingAnchor;
  std::string pendingFootnotePreviewAnchor;
  bool activeFootnotePreview = false;
  int pagesUntilFullRefresh = 0;
  int cachedSpineIndex = 0;
  int cachedChapterPageNumber = 0;
  int cachedChapterTotalPageCount = 0;
  bool pendingRelayoutReposition = false;
  bool cachedRelayoutAtChapterStart = false;
  uint16_t cachedPageParagraphIndex = UINT16_MAX;
  uint16_t cachedPageParagraphOffset = 0;
  uint16_t cachedPageParagraphSpan = 0;
  bool pendingRelayoutPreview = false;
  bool pendingRelayoutPreviewFromBack = false;
  bool relayoutPreviewUserMoved = false;
  bool activeRelayoutPreview = false;
  bool activeChapterPreview = false;
  std::atomic<bool> relayoutBuildWorkRequested{false};
  std::atomic<bool> relayoutBuildCancelRequested{false};
  std::atomic<bool> relayoutBuildActive{false};
  std::atomic<bool> relayoutBuildLowHeapAbort{false};
  std::atomic<bool> relayoutBuildRetired{false};
  std::atomic<bool> relayoutBuildSafeModePending{false};
  std::atomic<uint32_t> relayoutBuildEligibleAfterMs{0};
  std::atomic<uint8_t> relayoutBuildAttempts{0};
  uint32_t relayoutBuildLastHeapCheckMs = 0;
  int8_t deferredPageTurnDirection = 0;
  unsigned long lastPageTurnTime = 0UL;
  unsigned long pageTurnDuration = 0UL;
  unsigned long pageShownAtMs = 0UL;
  bool paceSampleWarmupPending = true;
  uint32_t sessionPaceSampleSeconds = 0;
  uint16_t sessionPaceSampleCount = 0;
  uint32_t sessionReadingSeconds = 0;
  uint16_t sessionScreenPages = 0;
  bool readingStatsCommitted = false;
  bool fastHomeExitRequested = false;
  std::atomic<bool> firstRenderCompleted{false};
  bool deferredOnEnterPending = false;
  std::atomic<bool> nextChapterPreindexWorkRequested{false};
  std::atomic<bool> nextChapterPreindexCancelRequested{false};
  std::atomic<bool> nextChapterPreindexActive{false};
  std::atomic<bool> nextChapterPreindexLowHeapAbort{false};
  std::atomic<int> nextChapterPreindexCandidateSpine{-1};
  std::atomic<uint32_t> nextChapterPreindexCandidateKey{0};
  std::atomic<uint32_t> nextChapterPreindexEligibleAfterMs{0};
  std::atomic<uint8_t> nextChapterPreindexAttempts{0};
  std::atomic<int> nextChapterPreindexFinishedSpine{-1};
  std::atomic<uint32_t> nextChapterPreindexFinishedKey{0};
  uint32_t nextChapterPreindexLastHeapCheckMs = 0;
  uint16_t lastAutoPageTurnIntervalSeconds = 0;
  bool bookHasCustomReaderSettings = false;
  bool bookHasAutoPageTurnInterval = false;
  bool bookHasRenderModeOverride = false;
  bool committedReaderSettingsValid = false;
  bool relayoutSettingsTransactionActive = false;
  bool committedBookHasCustomReaderSettings = false;
  bool committedBookHasRenderModeOverride = false;
  ReaderSettingsSnapshot committedReaderSettings;
  bool restoreGlobalReaderSettingsOnExit = false;
  ReaderSettingsSnapshot globalReaderSettingsBeforeBook;
  bool bookReaderSettingsSuspendedForGlobalEdit = false;
  ReaderSettingsSnapshot suspendedBookReaderSettings;
  BookReadingStats stats;
  GlobalReadingStats globalStats;
  ReadingStatsDateTime sessionStartLocalDateTime;
  bool hasSessionStartLocalDateTime = false;
  // Signals that the next render should reposition within the newly loaded section
  // based on a cross-book percentage jump.
  bool pendingPercentJump = false;
  // Normalized 0.0-1.0 progress within the target spine item, computed from book percentage.
  float pendingSpineProgress = 0.0f;
  uint16_t pendingParagraphIndex = UINT16_MAX;
  uint16_t pendingClippingIndex = UINT16_MAX;
  bool pendingScreenshot = false;
  bool pendingSyncSaveError = false;
  // Serializes the debounce state and progress.bin write sequences between the
  // render task (saveProgressDebounced in render()) and the main task
  // (flushPendingProgress from menus/exit).
  std::mutex progressPersistMutex_;
  // Progress-persistence debounce state. lastSaved* mirror what progress.bin
  // holds; pending* is the newest in-RAM position awaiting a flush.
  bool hasSavedProgress = false;
  bool progressDirty = false;
  int lastSavedProgressSpineIndex = -1;
  int lastSavedProgressPageNumber = -1;
  int lastSavedProgressPageCount = -1;
  int pendingProgressSpineIndex = 0;
  int pendingProgressPageNumber = 0;
  int pendingProgressPageCount = 0;
  uint32_t lastProgressWriteMs = 0;
  uint8_t pageTurnsSinceProgressWrite = 0;
  static constexpr uint8_t kProgressWriteMaxTurns = 10;
  static constexpr uint32_t kProgressWriteMaxMs = 60000;
  // Chapter title shown in the status bar; resolved from book.bin only when the
  // chapter changes instead of on every page paint.
  mutable int cachedTocTitleSpineIndex = -1;
  mutable std::string cachedTocTitle;
  // Repair15 phase-1 instrumentation: transition timings written to
  // Duet's reader timing log at exit. Measurement only.
  unsigned long timingOnEnterMs = 0;
  unsigned long timingFirstPaintMs = 0;
  uint16_t timingRenderCount = 0;
  uint16_t timingEarlyRenderMs[6] = {};
  uint32_t timingLaterRenderTotalMs = 0;
  uint16_t timingLaterRenderCount = 0;
  uint32_t timingLastSectionBuildMs = 0;
  uint32_t timingMaxSectionBuildMs = 0;
  uint16_t timingSectionBuildCount = 0;
  uint32_t timingLastPreindexMs = 0;
  uint32_t timingMaxPreindexMs = 0;
  uint16_t timingPreindexCompleteCount = 0;
  uint16_t timingPreindexCancelCount = 0;
  uint16_t timingPreindexSkipHeapCount = 0;
  uint16_t timingPreindexFailCount = 0;
  uint32_t timingLastRelayoutPreviewMs = 0;
  uint32_t timingMaxRelayoutPreviewMs = 0;
  uint16_t timingRelayoutPreviewCount = 0;
  uint32_t timingLastRelayoutCompletionMs = 0;
  uint32_t timingMaxRelayoutCompletionMs = 0;
  uint16_t timingRelayoutCompletionCount = 0;
  uint16_t timingRelayoutCompletionCancelCount = 0;
  uint16_t timingRelayoutCacheHitCount = 0;
  uint32_t timingLastChapterPreviewMs = 0;
  uint32_t timingMaxChapterPreviewMs = 0;
  uint16_t timingChapterPreviewCount = 0;
  uint32_t timingLastChapterCompletionMs = 0;
  uint32_t timingMaxChapterCompletionMs = 0;
  uint16_t timingChapterCompletionCount = 0;
  uint16_t timingChapterCompletionCancelCount = 0;
  uint16_t timingChapterCacheHitCount = 0;
  bool skipNextButtonCheck = false;  // Skip button processing for one frame after subactivity exit
  bool automaticPageTurnActive = false;
  bool longPressMenuHandled = false;
  bool longPressBackHandled = false;
  bool longPowerButtonHandled = false;
  bool sideButtonLongPressHandled = false;
  bool frontButtonLongPressHandled = false;
  int pageLoadRetryCount = 0;
  enum class BookmarkFeedbackType : uint8_t {
    Added,
    Removed,
    LimitReached,
  };
  bool pendingBookmarkFeedback = false;
  BookmarkFeedbackType bookmarkFeedbackType = BookmarkFeedbackType::Added;
  unsigned long bookmarkFeedbackShowTime = 0UL;
  bool pendingCompletedFeedback = false;
  bool completedFeedbackIsFinished = false;
  unsigned long completedFeedbackShowTime = 0UL;
  bool pendingTiltPageTurnFeedback = false;
  bool tiltPageTurnFeedbackEnabled = false;
  unsigned long tiltPageTurnFeedbackShowTime = 0UL;
  bool pendingBionicFeedback = false;
  uint8_t bionicFeedbackMode = 0;
  unsigned long bionicFeedbackShowTime = 0UL;
  bool pendingRenderModeToast = false;
  bool renderModeToastShown = false;
  bool pendingSafeModeToast = false;
  bool safeModeToastShown = false;
  uint8_t renderModeToastMode = 0;
  unsigned long renderModeToastShowTime = 0UL;
  std::unique_ptr<uint8_t[]> renderModeToastRegionBuffer;
  size_t renderModeToastRegionBufferSize = 0;
  int renderModeToastRegionX = 0;
  int renderModeToastRegionY = 0;
  int renderModeToastRegionW = 0;
  int renderModeToastRegionH = 0;
  bool renderModeToastRegionSaved = false;
  int completionTriggerSpineIndex = -1;
  float completionTriggerSpineProgress = 1.0f;
  bool completionPromptQueued = false;
  bool completionPromptShown = false;
  bool completionTriggerSeenBelow = false;
  bool completionTriggerCrossed = false;
  bool lastAtOrPastCompletionTrigger = false;

  // Tracks whether this book is currently removed from Recent Books by the
  // removeReadBooksFromRecents feature (set at End-of-Book, cleared if paged back in).
  bool recentsEntryRemoved = false;
  // Set when the reader is left at end-of-book and SETTINGS.moveFinishedToReadFolder is on.
  // Consumed in onExit() to relocate the finished book into /Read/.
  bool pendingReadFolderMove = false;
  bool savedItemsLoaded = false;
  // Next-book suggestion menu for the End-of-Book screen
  EndOfBookOptions endOfBookOptions;

  // Footnote support
  std::vector<FootnoteEntry> currentPageFootnotes;
  struct SavedPosition {
    int spineIndex;
    int pageNumber;
  };
  static constexpr int MAX_FOOTNOTE_DEPTH = 3;
  SavedPosition savedPositions[MAX_FOOTNOTE_DEPTH] = {};
  int footnoteDepth = 0;

  void renderContents(std::unique_ptr<Page> page, int fontId, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);
  void drawClippingHighlights(const Page& page, int fontId, int orientedMarginTop, int orientedMarginLeft) const;
  void renderStatusBar() const;
  bool shouldUseFootnotePreview(int targetSpineIndex, const std::string& anchor) const;
  std::string footnotePreviewCacheSuffix(EpubRenderMode renderMode, int fontId, const std::string& anchor) const;
  std::string relayoutPreviewCacheSuffix(EpubRenderMode renderMode, int fontId, uint16_t paragraphIndex) const;
  std::string chapterPreviewCacheSuffix(EpubRenderMode renderMode, int fontId) const;
  void clearFootnotePreviewState();
  // Remap the cached relative reading position once the section's real page count is known
  // (used after a settings change re-paginates a chapter). Returns true if currentPage moved.
  // No-op when pagination is unchanged (plain resume).
  bool applyDeferredReposition();
  bool saveProgress(int spineIndex, int currentPage, int pageCount);
  // Render-path variant (repo rule 8: no persist per page turn). Tracks the
  // position in RAM and writes through only on chapter change or when the
  // turn/time budget is exceeded; flushPendingProgress() covers exits.
  void saveProgressDebounced(int spineIndex, int currentPage, int pageCount);
  bool flushPendingProgress();
  // Body shared by the three entry points above; caller must hold
  // progressPersistMutex_ (render task saves vs main-task flushes).
  bool saveProgressLocked(int spineIndex, int currentPage, int pageCount);
  void cacheCurrentSectionPosition();
  void pauseReadingPaceTimer(const char* reason = "unknown");
  void resumeReadingPaceTimer(const char* reason = "unknown");
  void armReadingPaceWarmup(const char* reason = "unknown");
  bool forwardPageReadElapsed(uint32_t& seconds, const char* source) const;
  bool currentPageReadingSecondsForStats(uint32_t& seconds, const char* source) const;
  void recordCurrentPageReadingTime(const char* source = "unknown");
  void recordForwardPagePaceSample(uint32_t seconds, const char* source);
  bool getSessionAveragePaceSeconds(uint16_t& avgSeconds) const;
  void recoverStoredPaceFromSession(const char* reason = "unknown");
  bool getTimeLeftPaceSeconds(uint16_t& avgSeconds, const char*& source, uint16_t& sampleCount) const;
  bool estimateRemainingTimeLeftPages(bool bookEstimate, float& remainingPages) const;
  bool estimateProgressTimeLeftSeconds(uint32_t& seconds) const;
  bool estimateTimeLeftSeconds(bool bookEstimate, uint32_t& seconds) const;
  bool formatTimeLeftLabel(char* buf, size_t len) const;
  void refreshCachedTimeLeftEstimate();
  void applyBookStatsEditsFromDisk();
  void handleBookStatsReturn();
  void resetCurrentBookStatsAfterDelete();
  void openFileTransfer();
  void openAutoPageTurnIntervalPicker(bool ignoreInitialConfirmRelease = false);
  void openDictionaryLookup(bool history);
  void startClipSelection();
  void resetReadingPaceData();
  void captureGlobalReaderSettings();
  void restoreGlobalReaderSettings();
  void loadBookReaderSettings();
  void rememberCommittedReaderSettings();
  bool persistCommittedReaderSettings();
  bool beginReaderSettingsRelayoutTransaction();
  void commitReaderSettingsRelayout();
  void rollbackReaderSettingsRelayout(const char* reason);
  void saveCurrentBookReaderSettings();
  void saveGlobalSettingsPreservingBookOverrides();
  void beginGlobalSettingsEdit();
  void endGlobalSettingsEdit();
  static void saveReaderOptionsForBook(void* ctx);
  static void saveGlobalSettingsForBookReader(void* ctx);
  static void beginGlobalSettingsEditForBookReader(void* ctx);
  static void endGlobalSettingsEditForBookReader(void* ctx);
  // Jump to a percentage of the book (0-100), mapping it to spine and page.
  void jumpToPercent(int percent);
  void reindexCurrentSection();
  void beginInteractiveRelayout(bool allowPreview = true);
  void cacheRelayoutPreviewPosition();
  void positionRelayoutPreview();
  void cancelRelayoutBuildForInput();
  bool maybeScheduleRelayoutBuild();
  void runGuardedRelayoutBuild();
  static bool shouldCancelRelayoutBuild(void* context);
  void executeReaderQuickAction(CrossPointSettings::LONG_PRESS_MENU_ACTION action);
  bool quickActionUsesConfirmRelease(CrossPointSettings::LONG_PRESS_MENU_ACTION action) const;
  bool quickActionUsesPowerRelease(CrossPointSettings::LONG_PRESS_MENU_ACTION action) const;
  void suppressConfirmShortcutRelease(CrossPointSettings::LONG_PRESS_MENU_ACTION action);
  void executeFootnoteQuickAction(bool suppressInitialPowerRelease = false);
  void suppressPowerShortcutRelease();
  bool consumeLongPowerButtonRelease();
  bool consumeLongPowerButtonHold();
  bool executeShortPowerButtonAction();
  bool executeLongPowerButtonAction();
  void handleClippingJump(const ClippingJumpResult& clipping);
  void onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action);
  // Short-press Confirm opens the overlay; its More row opens the complete menu.
  void openReaderQuickMenu();
  void openReaderMenu(bool initialSettingsChanged = false);
  void applyOrientation(uint8_t orientation);
  bool pageTurn(bool isForwardTurn, const char* source = "unknown");
  bool consumeDeferredPageTurn();
  void deferPageTurn(bool isForwardTurn);
  bool anyReaderButtonHeld() const;
  void cancelNextChapterPreindexForInput();
  uint32_t nextChapterPreindexSettingsKey(uint16_t viewportWidth, uint16_t viewportHeight) const;
  void armNextChapterPreindex(uint16_t viewportWidth, uint16_t viewportHeight);
  bool maybeScheduleNextChapterPreindex();
  void runGuardedNextChapterPreindex();
  static bool shouldCancelNextChapterPreindex(void* context);
  void runDeferredOnEnter(bool loadSavedItems = true);
  void commitReadingStats();
  void goHomeAfterReading();
  float getCurrentBookProgressPercent() const;
  void initializeCompletionPromptTrigger();
  bool isAtOrPastCompletionTrigger() const;
  bool shouldQueueCompletionPromptOnChapterExit() const;
  void queueCompletionPromptIfNeeded();
  void setBookCompleted(bool isCompleted);
  void showCompletedFeedback(bool isCompleted);
  void showTiltPageTurnFeedback(bool enabled);
  void showBionicFeedback(uint8_t mode);
  void showRenderModeToast(uint8_t renderMode);
  void showSafeModeToast();
  bool storeRenderModeToastRegion(const char* msg);
  void drawRenderModeToastBuffer(const char* msg);
  bool restoreRenderModeToastRegion();

  // Footnote navigation
  void navigateToHref(const std::string& href, bool savePosition = false);
  void restoreSavedPosition();

 public:
  explicit EpubReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Epub> epub)
      : Activity("EpubReader", renderer, mappedInput), epub(std::move(epub)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  bool preventAutoSleep() override { return automaticPageTurnActive; }
  bool isReaderActivity() const override { return true; }
  bool canSnapshotForSleepOverlay() const override { return true; }
  std::string getCurrentBookPath() const override { return epub ? epub->getPath() : std::string{}; }
  void setAutoPageTurnIntervalSeconds(uint16_t seconds);
  uint16_t getAutoPageTurnIntervalSeconds() const;

  // Renders the last saved page to the frame buffer without flushing to display.
  // Used by SleepActivity to prepare the background for the overlay sleep mode.
  // Returns false if the page cannot be loaded (missing cache / file error).
  static bool drawCurrentPageToBuffer(const std::string& filePath, GfxRenderer& renderer);
  static uint8_t loadBookRenderMode(const std::string& filePath);
  static bool saveBookRenderMode(const std::string& filePath, uint8_t renderMode);
  static bool resetBookReaderSettings(const std::string& filePath);
  ScreenshotInfo getScreenshotInfo() const override;
};
