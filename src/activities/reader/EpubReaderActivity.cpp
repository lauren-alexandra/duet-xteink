#include "EpubReaderActivity.h"

#include <Arduino.h>
#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <MemoryBudget.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <new>

#include "../settings/KOReaderSettingsActivity.h"
#include "BookStatsActivity.h"
#include "ClipSelectionActivity.h"
#include "ClippingStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "DictionaryHistoryActivity.h"
#include "DictionaryWordSelectActivity.h"
#include "EpubReaderBookmarkListActivity.h"
#include "EpubReaderChapterSelectionActivity.h"
#include "EpubReaderClippingListActivity.h"
#include "EpubReaderFootnotesActivity.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "EpubReaderQuickMenuActivity.h"
#include "EpubReaderUtils.h"
#include "GlobalActions.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncActivity.h"
#include "MappedInputManager.h"
#include "NearbyBookPositionSyncActivity.h"
#include "ProgressMapper.h"
#include "QrDisplayActivity.h"
#include "ReaderUtils.h"
#include "ReadingJournal.h"
#include "ReadingLedger.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "SessionLog.h"
#include "WordRef.h"
#include "activities/home/BookInfoActivity.h"
#include "activities/home/CurrentBookStats.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "clippings/ClippingsManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"
#include "util/BookMoveUtils.h"
#include "util/ScreenshotUtil.h"

namespace {
// pagesPerRefresh now comes from SETTINGS.getRefreshFrequency()
constexpr unsigned long longPressMenuMs = 600;
constexpr uint16_t DEFAULT_AUTO_PAGE_TURN_INTERVAL_S = 30;
constexpr uint16_t MIN_AUTO_PAGE_TURN_INTERVAL_S = 5;
constexpr uint16_t MAX_AUTO_PAGE_TURN_INTERVAL_S = 120;
constexpr int MAX_PAGE_LOAD_RETRIES = 3;
constexpr uint8_t LEGACY_READER_SETTINGS_FILE_VERSION = 1;
constexpr uint8_t INDEXED_READER_SETTINGS_FILE_VERSION = 2;
constexpr uint8_t STABLE_POINT_SIZE_READER_SETTINGS_FILE_VERSION = 3;
constexpr uint8_t READER_SETTINGS_FILE_VERSION = 4;
constexpr uint8_t READER_SETTINGS_FLAG_CUSTOM = 1 << 0;
constexpr uint8_t READER_SETTINGS_FLAG_AUTO_PAGE_TURN = 1 << 1;
constexpr uint8_t READER_SETTINGS_FLAG_RENDER_MODE = 1 << 2;
constexpr char READER_SETTINGS_FILE_NAME[] = "/reader_settings.bin";
constexpr char BALANCED_SECTION_CACHE_SUFFIX[] = "_balanced";
constexpr char LIGHT_SECTION_CACHE_SUFFIX[] = "_light";
constexpr unsigned long RENDER_MODE_TOAST_MS = 1500UL;
constexpr unsigned long MIN_READING_STATS_PAGE_MS = 2000UL;
constexpr uint32_t MIN_READING_PACE_SAMPLE_SECONDS = 2;
constexpr uint16_t MIN_STORED_TIME_LEFT_PACE_SAMPLE_COUNT = 3;
constexpr uint16_t MIN_SESSION_TIME_LEFT_PACE_SAMPLE_COUNT = 10;
constexpr uint16_t MIN_STORED_PACE_SLOWER_RECOVERY_SESSION_SAMPLES = 10;
constexpr uint8_t STORED_PACE_SLOWER_RECOVERY_PERCENT = 110;
constexpr uint16_t MIN_STORED_PACE_FASTER_RECOVERY_SESSION_SAMPLES = 15;
constexpr uint8_t STORED_PACE_FASTER_RECOVERY_PERCENT = 90;
constexpr uint8_t BOOK_PROGRESS_ESTIMATE_FLOOR_PERCENT = 90;
constexpr uint16_t FOOTNOTE_PREVIEW_MAX_PAGES = 3;
constexpr uint8_t PUBLISHER_PAGE_NUMBER_LEFT_MARGIN_MIN = 15;
constexpr int PUBLISHER_PAGE_NUMBER_X = 5;
constexpr uint32_t NEXT_CHAPTER_PREINDEX_IDLE_MS = 9000UL;
constexpr uint32_t NEXT_CHAPTER_PREINDEX_RETRY_IDLE_MS = 12000UL;
constexpr uint32_t NEXT_CHAPTER_PREINDEX_LOW_HEAP_RETRY_MS = 5000UL;
constexpr uint32_t NEXT_CHAPTER_PREINDEX_HEAP_CHECK_MS = 50UL;
constexpr uint8_t NEXT_CHAPTER_PREINDEX_MAX_ATTEMPTS = 8;
constexpr uint8_t NEXT_CHAPTER_PREINDEX_MAX_LOOKAHEAD_PAGES = 6;
constexpr uint32_t RELAYOUT_BUILD_IDLE_MS = 12000UL;
constexpr uint32_t CHAPTER_COMPLETION_IDLE_MS = 1500UL;
constexpr uint32_t RELAYOUT_BUILD_RETRY_IDLE_MS = 15000UL;
constexpr uint32_t RELAYOUT_BUILD_LOW_HEAP_RETRY_MS = 5000UL;
constexpr uint32_t RELAYOUT_BUILD_HEAP_CHECK_MS = 50UL;
constexpr uint8_t RELAYOUT_BUILD_MAX_ATTEMPTS = 3;
constexpr uint16_t RELAYOUT_PREVIEW_MAX_PAGES = 6;
constexpr uint16_t CHAPTER_PREVIEW_MAX_PAGES = 2;

struct ToastRect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

uint32_t pagesCentipages(const float pages) {
  if (pages <= 0.0f) {
    return 0;
  }
  if (pages >= static_cast<float>(UINT32_MAX) / 100.0f) {
    return UINT32_MAX;
  }
  return static_cast<uint32_t>(pages * 100.0f + 0.5f);
}

bool hasEnoughPaceSamplesForTimeLeft(const BookReadingStats& stats) {
  return stats.avgSecondsPerForwardPage > 0 && stats.paceSampleCount >= MIN_STORED_TIME_LEFT_PACE_SAMPLE_COUNT;
}

std::string confirmationHeading(const StrId actionLabelId) {
  return std::string(tr(STR_CONFIRM)) + ": " + std::string(I18N.get(actionLabelId));
}

EpubRenderMode normalizeRenderMode(const uint8_t rawMode) {
  return isValidEpubRenderMode(rawMode) ? static_cast<EpubRenderMode>(rawMode) : EpubRenderMode::CrossInkDefault;
}

uint8_t normalizeRenderModeRaw(const uint8_t rawMode) { return static_cast<uint8_t>(normalizeRenderMode(rawMode)); }

const char* sectionCacheSuffixForRenderMode(const EpubRenderMode renderMode) {
  switch (renderMode) {
    case EpubRenderMode::Balanced:
      return BALANCED_SECTION_CACHE_SUFFIX;
    case EpubRenderMode::Light:
      return LIGHT_SECTION_CACHE_SUFFIX;
    case EpubRenderMode::CrossInkDefault:
    default:
      return "";
  }
}

std::string sectionCacheSuffixForLayout(const EpubRenderMode renderMode, const int fontId) {
  char fontSuffix[16] = {};
  snprintf(fontSuffix, sizeof(fontSuffix), "_f%08lx", static_cast<unsigned long>(static_cast<uint32_t>(fontId)));
  return std::string(sectionCacheSuffixForRenderMode(renderMode)) + fontSuffix;
}

void getSyncPageAnchors(const Section& section, const int page, std::optional<uint16_t>& paragraphIndex,
                        std::optional<uint16_t>& listItemIndex) {
  if (page < 0 || page >= section.pageCount) {
    return;
  }

  const uint16_t sourcePage = static_cast<uint16_t>(page);
  if (const auto pIdx = section.getParagraphIndexForPage(sourcePage)) {
    paragraphIndex = *pIdx;
  }
  if (const auto liIdx = section.getListItemIndexForPage(sourcePage); liIdx.has_value() && *liIdx > 0) {
    listItemIndex = *liIdx;
  }
}

uint64_t hashFootnotePreviewAnchor(const std::string& anchor) {
  uint64_t hash = 1469598103934665603ULL;
  for (const char c : anchor) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 1099511628211ULL;
  }
  return hash;
}

const char* labelForRenderModeToast(const EpubRenderMode renderMode) {
  switch (renderMode) {
    case EpubRenderMode::Balanced:
      return tr(STR_BALANCED_MODE);
    case EpubRenderMode::Light:
      return tr(STR_LIGHT_MODE);
    case EpubRenderMode::CrossInkDefault:
    default:
      return "";
  }
}

std::array<EpubRenderMode, 3> fallbackModesForSelection(const EpubRenderMode selectedMode, uint8_t& count) {
  switch (selectedMode) {
    case EpubRenderMode::Balanced:
      count = 2;
      return {EpubRenderMode::Balanced, EpubRenderMode::Light, EpubRenderMode::Light};
    case EpubRenderMode::Light:
      count = 1;
      return {EpubRenderMode::Light, EpubRenderMode::Light, EpubRenderMode::Light};
    case EpubRenderMode::CrossInkDefault:
    default:
      count = 3;
      return {EpubRenderMode::CrossInkDefault, EpubRenderMode::Balanced, EpubRenderMode::Light};
  }
}

struct SectionBuildProfile {
  EpubRenderMode renderMode;
  bool embeddedStyle;
  uint8_t bionicReadingEnabled;
  bool guideReadingEnabled;
  const char* label;
  bool safeMode;
};

const char* sectionBuildLabelForRenderMode(const EpubRenderMode renderMode) {
  switch (renderMode) {
    case EpubRenderMode::Balanced:
      return "balanced";
    case EpubRenderMode::Light:
      return "light";
    case EpubRenderMode::CrossInkDefault:
    default:
      return "primary";
  }
}

SectionBuildProfile buildProfileForRenderMode(const EpubRenderMode renderMode) {
  return SectionBuildProfile{renderMode,
                             SETTINGS.embeddedStyle != 0,
                             BionicReadingMode::normalize(SETTINGS.bionicReadingEnabled),
                             SETTINGS.guideReadingEnabled != 0,
                             sectionBuildLabelForRenderMode(renderMode),
                             false};
}

bool shouldAttemptSafeModeFallback() {
  return SETTINGS.embeddedStyle != 0 || SETTINGS.bionicReadingEnabled != 0 || SETTINGS.guideReadingEnabled != 0;
}

SectionBuildProfile safeModeBuildProfile() {
  return SectionBuildProfile{EpubRenderMode::Light, false, false, false, "safe", true};
}

void applySafeModeReaderSettings() {
  SETTINGS.epubRenderMode = static_cast<uint8_t>(EpubRenderMode::Light);
  SETTINGS.embeddedStyle = 0;
  SETTINGS.bionicReadingEnabled = BionicReadingMode::Off;
  SETTINGS.guideReadingEnabled = 0;
}

bool hasEmSpacePrefix(const std::string& text) {
  return text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xE2 &&
         static_cast<unsigned char>(text[1]) == 0x80 && static_cast<unsigned char>(text[2]) == 0x83;
}

bool hasVisibleWordText(const std::string& text) {
  const char* cursor = text.c_str() + (hasEmSpacePrefix(text) ? 3 : 0);
  while (*cursor) {
    if (*cursor != ' ' && *cursor != '\t' && *cursor != '\r' && *cursor != '\n') return true;
    cursor++;
  }
  return false;
}

uint8_t largestBlockPercent(const MemoryBudget::HeapSnapshot& heap) {
  if (heap.freeHeap == 0) {
    return 0;
  }
  return static_cast<uint8_t>(std::min<uint32_t>(100, (heap.maxAllocHeap * 100U) / heap.freeHeap));
}

struct TiledGrayscaleTimings {
  unsigned long grayLsb = 0;
  unsigned long grayMsb = 0;
  unsigned long grayDisplay = 0;
  unsigned long cleanup = 0;
};

struct ClippingPageMatch {
  uint16_t startWord = 0;
  uint16_t endWord = 0;
};

bool isUtf8SpaceAt(const char* cursor, size_t& advance) {
  const auto c = static_cast<unsigned char>(cursor[0]);
  if (c == 0xC2 && cursor[1] != '\0' && static_cast<unsigned char>(cursor[1]) == 0xA0) {
    advance = 2;
    return true;
  }
  if (c == 0xE2 && cursor[1] != '\0' && cursor[2] != '\0' && static_cast<unsigned char>(cursor[1]) == 0x80) {
    const auto c2 = static_cast<unsigned char>(cursor[2]);
    if (c2 == 0x83 || c2 == 0xAF) {
      advance = 3;
      return true;
    }
  }
  return false;
}

bool nextClipToken(const char*& cursor, const char*& tokenStart, size_t& tokenLen) {
  while (*cursor != '\0') {
    size_t advance = 0;
    if (std::isspace(static_cast<unsigned char>(*cursor)) || isUtf8SpaceAt(cursor, advance)) {
      cursor += advance > 0 ? advance : 1;
      continue;
    }
    break;
  }
  if (*cursor == '\0') {
    tokenStart = nullptr;
    tokenLen = 0;
    return false;
  }

  tokenStart = cursor;
  while (*cursor != '\0') {
    size_t advance = 0;
    if (std::isspace(static_cast<unsigned char>(*cursor)) || isUtf8SpaceAt(cursor, advance)) {
      break;
    }
    cursor++;
  }
  tokenLen = static_cast<size_t>(cursor - tokenStart);
  return true;
}

uint16_t countClipTokens(const std::string& text) {
  uint16_t count = 0;
  const char* cursor = text.c_str();
  const char* token = nullptr;
  size_t len = 0;
  while (nextClipToken(cursor, token, len) && count < UINT16_MAX) {
    count++;
  }
  return count;
}

bool advanceClipCursorToToken(const std::string& text, const uint16_t targetIndex, const char*& cursor,
                              const char*& tokenStart, size_t& tokenLen) {
  cursor = text.c_str();
  uint16_t index = 0;
  while (nextClipToken(cursor, tokenStart, tokenLen)) {
    if (index == targetIndex) {
      return true;
    }
    index++;
  }
  tokenStart = nullptr;
  tokenLen = 0;
  return false;
}

bool wordMatchesToken(const std::string& word, const char* token, const size_t tokenLen) {
  if (!token || tokenLen == 0) return false;
  const char* visibleWord = word.c_str() + (hasEmSpacePrefix(word) ? 3 : 0);
  return std::strlen(visibleWord) == tokenLen && std::strncmp(visibleWord, token, tokenLen) == 0;
}

template <typename Callback>
bool forEachVisiblePageWord(const Page& page, Callback&& callback) {
  uint16_t wordIndex = 0;
  for (const auto& element : page.elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto& line = static_cast<const PageLine&>(*element);
    if (!line.getBlock()) continue;

    const auto& block = *line.getBlock();
    const auto& wordList = block.getWords();
    const auto& xpos = block.getWordXpos();
    const auto& styles = block.getWordStyles();
    const size_t count = std::min({wordList.size(), xpos.size(), styles.size()});
    for (size_t i = 0; i < count; ++i) {
      const std::string& word = wordList[i];
      const char* visibleWord = word.c_str() + (hasEmSpacePrefix(word) ? 3 : 0);
      bool hasVisibleText = false;
      for (const char* p = visibleWord; *p != '\0'; ++p) {
        if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
          hasVisibleText = true;
          break;
        }
      }
      if (!hasVisibleText) continue;

      if (!callback(wordIndex, line, block, i)) {
        return false;
      }
      wordIndex++;
    }
  }
  return true;
}

bool matchClipRunFromPageWord(const Page& page, const Clipping& clipping, const uint16_t startPageWord,
                              const uint16_t startClipToken, const uint16_t minPartialMatch, ClippingPageMatch& match) {
  const char* cursor = nullptr;
  const char* token = nullptr;
  size_t tokenLen = 0;
  if (!advanceClipCursorToToken(clipping.text, startClipToken, cursor, token, tokenLen)) {
    return false;
  }

  uint16_t matchedTokens = 0;
  uint16_t lastWord = startPageWord;
  bool reachedClipEnd = false;
  bool stoppedByMismatch = false;

  forEachVisiblePageWord(page, [&](const uint16_t wordIndex, const PageLine&, const TextBlock& block, const size_t i) {
    if (wordIndex < startPageWord) {
      return true;
    }

    const std::string& word = block.getWords()[i];
    if (!wordMatchesToken(word, token, tokenLen)) {
      stoppedByMismatch = true;
      return false;
    }

    matchedTokens++;
    lastWord = wordIndex;
    if (!nextClipToken(cursor, token, tokenLen)) {
      reachedClipEnd = true;
      return false;
    }
    return true;
  });

  if (matchedTokens == 0) {
    return false;
  }

  if (stoppedByMismatch) {
    return false;
  }

  // A relayout can split a saved clipping so this page starts mid-clipping.
  // Accept complete runs or page-boundary partial runs.
  if (!reachedClipEnd && matchedTokens < minPartialMatch) {
    const bool startsAtClipBoundary = startClipToken == 0;
    const bool startsAtPageBoundary = startPageWord == 0;
    if (!startsAtClipBoundary && !startsAtPageBoundary) {
      return false;
    }
  }

  match.startWord = startPageWord;
  match.endWord = lastWord;
  return true;
}

bool findClippingTextOnPage(const Page& page, const Clipping& clipping, ClippingPageMatch& match) {
  if (clipping.text.empty()) return false;

  const uint16_t tokenCount = countClipTokens(clipping.text);
  if (tokenCount == 0) return false;
  const uint16_t minPartialMatch = std::min<uint16_t>(tokenCount, 3);

  bool found = false;

  forEachVisiblePageWord(page, [&](const uint16_t wordIndex, const PageLine&, const TextBlock& block, const size_t i) {
    const std::string& word = block.getWords()[i];
    const char* cursor = clipping.text.c_str();
    const char* token = nullptr;
    size_t tokenLen = 0;
    uint16_t tokenIndex = 0;
    while (nextClipToken(cursor, token, tokenLen)) {
      if (tokenIndex >= tokenCount) {
        break;
      }
      if (wordMatchesToken(word, token, tokenLen) &&
          matchClipRunFromPageWord(page, clipping, wordIndex, tokenIndex, minPartialMatch, match)) {
        found = true;
        return false;
      }
      tokenIndex++;
    }
    return true;
  });

  return found;
}

uint16_t countVisiblePageWords(const Page& page) {
  uint16_t count = 0;
  forEachVisiblePageWord(page, [&](const uint16_t, const PageLine&, const TextBlock&, const size_t) {
    if (count == UINT16_MAX) return false;
    count++;
    return true;
  });
  return count;
}

bool findClippingStoredRangeOnPage(const Page& page, const Clipping& clipping, const uint16_t currentPage,
                                   const uint16_t currentPageCount, ClippingPageMatch& match) {
  if (clipping.wordCount == 0 || currentPageCount == 0 || clipping.pageCount != currentPageCount) {
    return false;
  }
  if (clipping.startPage > clipping.endPage || currentPage < clipping.startPage || currentPage > clipping.endPage) {
    return false;
  }

  const uint16_t pageWordCount = countVisiblePageWords(page);
  if (pageWordCount == 0) return false;

  uint16_t startWord = 0;
  uint16_t endWord = static_cast<uint16_t>(pageWordCount - 1);
  if (currentPage == clipping.startPage) {
    if (clipping.startWordIndex >= pageWordCount) return false;
    startWord = clipping.startWordIndex;
  }
  if (currentPage == clipping.endPage) {
    if (clipping.endWordIndex >= pageWordCount) return false;
    endWord = clipping.endWordIndex;
  }
  if (startWord > endWord) return false;

  match.startWord = startWord;
  match.endWord = endWord;
  return true;
}

uint16_t clampSectionPage(const uint32_t page, const uint16_t pageCount) {
  if (pageCount == 0) return 0;
  return static_cast<uint16_t>(std::min<uint32_t>(page, pageCount - 1));
}

uint16_t approximateRelayoutPage(const Clipping& clipping, const uint16_t currentPageCount) {
  if (currentPageCount == 0) return 0;
  if (clipping.pageCount <= 1) return 0;

  const uint32_t oldLastPage = static_cast<uint32_t>(clipping.pageCount - 1);
  const uint32_t newLastPage = static_cast<uint32_t>(currentPageCount - 1);
  const uint32_t scaledPage =
      (static_cast<uint32_t>(clipping.startPage) * newLastPage + oldLastPage / 2U) / oldLastPage;
  return clampSectionPage(scaledPage, currentPageCount);
}

bool pageContainsClippingText(Section& section, const Clipping& clipping, const uint16_t page) {
  section.currentPage = page;
  auto loadedPage = section.loadPageFromSectionFile();
  if (!loadedPage) return false;

  ClippingPageMatch match;
  return findClippingTextOnPage(*loadedPage, clipping, match);
}

bool findClippingPageNear(Section& section, const Clipping& clipping, const uint16_t center, const uint16_t radius,
                          uint16_t& outPage) {
  if (section.pageCount == 0) return false;

  const uint16_t pageCount = static_cast<uint16_t>(section.pageCount);
  const uint16_t clampedCenter = clampSectionPage(center, pageCount);
  if (pageContainsClippingText(section, clipping, clampedCenter)) {
    outPage = clampedCenter;
    return true;
  }

  for (uint16_t distance = 1; distance <= radius; ++distance) {
    if (clampedCenter >= distance) {
      const uint16_t before = static_cast<uint16_t>(clampedCenter - distance);
      if (pageContainsClippingText(section, clipping, before)) {
        outPage = before;
        return true;
      }
    }
    const uint32_t after = static_cast<uint32_t>(clampedCenter) + distance;
    if (after < pageCount && pageContainsClippingText(section, clipping, static_cast<uint16_t>(after))) {
      outPage = static_cast<uint16_t>(after);
      return true;
    }
  }
  return false;
}

uint16_t resolveClippingJumpPage(Section& section, const Clipping& clipping, const uint16_t fallbackPage) {
  constexpr uint16_t SEARCH_RADIUS = 8;
  if (section.pageCount == 0) return fallbackPage;

  const uint16_t pageCount = static_cast<uint16_t>(section.pageCount);
  uint16_t resolvedPage = clampSectionPage(fallbackPage, pageCount);
  const uint16_t approximatePage = approximateRelayoutPage(clipping, pageCount);
  if (findClippingPageNear(section, clipping, approximatePage, SEARCH_RADIUS, resolvedPage)) {
    return resolvedPage;
  }

  if (clipping.paragraphIndex != UINT16_MAX) {
    const auto paragraphPage = section.getPageForParagraphIndex(clipping.paragraphIndex);
    if (paragraphPage.has_value() &&
        findClippingPageNear(section, clipping, clampSectionPage(*paragraphPage, pageCount), SEARCH_RADIUS,
                             resolvedPage)) {
      return resolvedPage;
    }
  }

  findClippingPageNear(section, clipping, resolvedPage, SEARCH_RADIUS, resolvedPage);
  return resolvedPage;
}

bool runTiledGrayscalePass(GfxRenderer& renderer, const Page& page, const int fontId, const int marginLeft,
                           const int marginTop, const bool foregroundBlack, const bool needsTextGrayscale,
                           const bool needsImageGrayscale, TiledGrayscaleTimings& timings) {
  if ((!needsTextGrayscale && !needsImageGrayscale) || !renderer.supportsStripGrayscale()) {
    return false;
  }

  constexpr int STRIP_ROWS = 80;
  const int displayHeight = renderer.getDisplayHeight();
  const int displayWidthBytes = renderer.getDisplayWidthBytes();
  auto scratch =
      std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[static_cast<size_t>(displayWidthBytes) * STRIP_ROWS]);
  if (!scratch) {
    LOG_ERR("ERS", "OOM: grayscale strip scratch (%d bytes); falling back to BW snapshot",
            displayWidthBytes * STRIP_ROWS);
    return false;
  }

  // Keep the live BW framebuffer intact, stream grayscale planes by row-band,
  // then re-sync the controller BW state from the framebuffer.
  const auto renderPlane = [&](const GfxRenderer::RenderMode mode, const bool lsbPlane) {
    renderer.setRenderMode(mode);
    for (int y = 0; y < displayHeight; y += STRIP_ROWS) {
      const int rows = std::min(STRIP_ROWS, displayHeight - y);
      renderer.beginStripTarget(scratch.get(), y, rows);
      renderer.clearScreen(0x00);
      if (needsTextGrayscale) {
        page.render(renderer, fontId, marginLeft, marginTop, foregroundBlack);
      } else {
        page.renderImages(renderer, fontId, marginLeft, marginTop);
      }
      renderer.endStripTarget();
      renderer.writeGrayscalePlaneStrip(lsbPlane, scratch.get(), y, rows);
    }
  };

  renderPlane(GfxRenderer::GRAYSCALE_LSB, true);
  timings.grayLsb = millis();

  renderPlane(GfxRenderer::GRAYSCALE_MSB, false);
  timings.grayMsb = millis();

  renderer.setRenderMode(GfxRenderer::BW);
  renderer.displayGrayBuffer();
  timings.grayDisplay = millis();
  renderer.cleanupGrayscaleWithFrameBuffer();
  timings.cleanup = millis();
  return true;
}

ToastRect computeToastRect(const GfxRenderer& renderer, const char* msg) {
  constexpr int toastPadX = 20;
  constexpr int toastPadY = 12;
  const int msgW = renderer.getTextWidth(UI_10_FONT_ID, msg);
  const int msgH = renderer.getLineHeight(UI_10_FONT_ID);
  const int toastW = msgW + toastPadX * 2;
  const int toastH = msgH + toastPadY * 2;
  const int toastX = (renderer.getScreenWidth() - toastW) / 2;
  const int toastY = (renderer.getScreenHeight() - toastH) / 2;
  return {toastX, toastY, toastW, toastH};
}

void drawToastBuffer(const GfxRenderer& renderer, const char* msg) {
  constexpr int toastPadX = 20;
  constexpr int toastPadY = 12;
  const bool toastBackgroundBlack = ReaderUtils::readerForegroundBlack();
  const ToastRect toast = computeToastRect(renderer, msg);
  renderer.fillRect(toast.x, toast.y, toast.w, toast.h, toastBackgroundBlack);
  renderer.drawRect(toast.x, toast.y, toast.w, toast.h, !toastBackgroundBlack);
  renderer.drawText(UI_10_FONT_ID, toast.x + toastPadX, toast.y + toastPadY, msg, !toastBackgroundBlack);
}

void drawToast(const GfxRenderer& renderer, const char* msg) {
  drawToastBuffer(renderer, msg);
  renderer.displayBuffer();
}

void drawPublisherPageMarkers(const GfxRenderer& renderer, const Page& page, const int contentTop,
                              const int contentBottom, const bool foregroundBlack = true) {
  if (!SETTINGS.publisherPageNumbers || page.publisherPageMarkers.empty()) {
    return;
  }

  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int lineStep = std::max(1, lineHeight - 2);
  const int availableHeight = contentBottom - contentTop;
  if (availableHeight <= lineHeight) {
    return;
  }

  for (const auto& marker : page.publisherPageMarkers) {
    const char* label = marker.label;
    if (!label || label[0] == '\0') {
      continue;
    }

    bool hasNonAscii = false;
    int labelLen = 0;
    int maxCharWidth = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(label); *p != '\0'; p++) {
      if (*p >= 0x80) {
        hasNonAscii = true;
        break;
      }
      if (*p <= ' ') {
        continue;
      }
      const char ch[2] = {static_cast<char>(*p), '\0'};
      maxCharWidth = std::max(maxCharWidth, renderer.getTextWidth(SMALL_FONT_ID, ch));
      labelLen++;
    }

    if (labelLen == 0) {
      continue;
    }

    const int x = PUBLISHER_PAGE_NUMBER_X;
    if (hasNonAscii) {
      const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, label);
      const int maxY = contentBottom - lineHeight;
      const int y = maxY < contentTop ? contentTop : std::min(std::max(contentTop + marker.yPos, contentTop), maxY);
      renderer.drawTextRotated90CW(SMALL_FONT_ID, x, y + textWidth, label, foregroundBlack);
      continue;
    }

    const int markerHeight = lineHeight + (labelLen - 1) * lineStep;
    int y = contentTop + marker.yPos - lineHeight / 2;
    const int maxY = contentBottom - markerHeight;
    y = maxY < contentTop ? contentTop : std::min(std::max(y, contentTop), maxY);

    int row = 0;
    for (const char* p = label; *p != '\0'; p++) {
      if (static_cast<unsigned char>(*p) <= ' ') {
        continue;
      }
      const char ch[2] = {*p, '\0'};
      const int charWidth = renderer.getTextWidth(SMALL_FONT_ID, ch);
      renderer.drawText(SMALL_FONT_ID, x + (maxCharWidth - charWidth) / 2, y + row * lineStep, ch, foregroundBlack);
      row++;
    }
  }
}

uint8_t effectiveReaderLeftMargin() {
  return SETTINGS.publisherPageNumbers ? std::max<uint8_t>(SETTINGS.screenMargin, PUBLISHER_PAGE_NUMBER_LEFT_MARGIN_MIN)
                                       : SETTINGS.screenMargin;
}

struct ReaderViewportLayout {
  int marginTop;
  int marginRight;
  int marginBottom;
  int marginLeft;
  uint16_t viewportWidth;
  uint16_t viewportHeight;
};

ReaderViewportLayout computeReaderViewportLayout(GfxRenderer& renderer, const bool automaticPageTurnActive) {
  ReaderViewportLayout layout{};
  renderer.getOrientedViewableTRBL(&layout.marginTop, &layout.marginRight, &layout.marginBottom, &layout.marginLeft);
  layout.marginLeft += effectiveReaderLeftMargin();
  layout.marginRight += SETTINGS.screenMargin;

  const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  const int topStatusBarReservedHeight = ReaderUtils::getTopClockStatusBarReservedHeight();
  if (topStatusBarReservedHeight > 0) {
    layout.marginTop += std::max(static_cast<int>(SETTINGS.screenMargin),
                                 topStatusBarReservedHeight + ReaderUtils::TOP_CLOCK_TEXT_PADDING);
  } else {
    layout.marginTop += SETTINGS.screenMargin;
  }

  if (automaticPageTurnActive &&
      (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight())) {
    layout.marginBottom +=
        std::max(SETTINGS.screenMargin,
                 static_cast<uint8_t>(statusBarHeight + UITheme::getInstance().getMetrics().statusBarVerticalMargin +
                                      ReaderUtils::STATUS_BAR_TEXT_PADDING));
  } else {
    layout.marginBottom +=
        std::max(SETTINGS.screenMargin, static_cast<uint8_t>(statusBarHeight + ReaderUtils::STATUS_BAR_TEXT_PADDING));
  }

  layout.viewportWidth = renderer.getScreenWidth() - layout.marginLeft - layout.marginRight;
  layout.viewportHeight = renderer.getScreenHeight() - layout.marginTop - layout.marginBottom;
  return layout;
}

bool releaseReaderSdFontCachesForLowMemory(const GfxRenderer& renderer, const char* tag, const char* reason) {
  const int fontId = SETTINGS.getReaderFontId();
  if (!renderer.isSdCardFont(fontId)) {
    return false;
  }

#if defined(ENABLE_SERIAL_LOG) && LOG_LEVEL >= 2
  const auto before = MemoryBudget::snapshot();
#endif
  if (!renderer.releaseSdCardFontForLowMemory(fontId)) {
    return false;
  }
#if defined(ENABLE_SERIAL_LOG) && LOG_LEVEL >= 2
  const auto after = MemoryBudget::snapshot();
  LOG_DBG(tag, "Released SD font caches after %s: free=%u->%u maxAlloc=%u->%u", reason, before.freeHeap, after.freeHeap,
          before.maxAllocHeap, after.maxAllocHeap);
#endif
  return true;
}

int clampPercent(int percent) {
  if (percent < 0) {
    return 0;
  }
  if (percent > 100) {
    return 100;
  }
  return percent;
}

bool isSnippetWhitespace(const std::string& word) {
  if (word.empty()) return true;
  return std::all_of(word.begin(), word.end(),
                     [](const char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; });
}

void buildBookmarkSnippet(const Page& page, char* out, const size_t outSize) {
  if (!out || outSize == 0) return;
  out[0] = '\0';
  size_t len = 0;

  for (const auto& el : page.elements) {
    if (el->getTag() != TAG_PageLine) continue;
    const auto& line = static_cast<const PageLine&>(*el);
    if (!line.getBlock()) continue;
    const auto& words = line.getBlock()->getWords();
    for (const auto& word : words) {
      if (isSnippetWhitespace(word)) continue;
      const size_t separatorLen = len > 0 ? 1 : 0;
      const size_t wordLen = word.size();
      if (len + separatorLen + wordLen >= outSize) return;
      if (separatorLen > 0) out[len++] = ' ';
      memcpy(out + len, word.c_str(), wordLen);
      len += wordLen;
      out[len] = '\0';
    }
  }
}

uint16_t clampAutoPageTurnIntervalSeconds(const uint16_t seconds) {
  return std::clamp(seconds, MIN_AUTO_PAGE_TURN_INTERVAL_S, MAX_AUTO_PAGE_TURN_INTERVAL_S);
}

bool readExact(FsFile& file, void* data, const size_t size) { return file.read(data, size) == static_cast<int>(size); }

bool writeExact(FsFile& file, const void* data, const size_t size) { return file.write(data, size) == size; }

bool readU8(FsFile& file, uint8_t& value) { return readExact(file, &value, sizeof(value)); }

bool writeU8(FsFile& file, const uint8_t value) { return writeExact(file, &value, sizeof(value)); }

bool readU16(FsFile& file, uint16_t& value) {
  uint8_t data[2] = {};
  if (!readExact(file, data, sizeof(data))) return false;
  value = static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
  return true;
}

bool writeU16(FsFile& file, const uint16_t value) {
  const uint8_t data[2] = {static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>((value >> 8) & 0xFF)};
  return writeExact(file, data, sizeof(data));
}

void captureReaderSettings(EpubReaderActivity::ReaderSettingsSnapshot& out) {
  out.fontFamily = SETTINGS.fontFamily;
  out.fontSize = SETTINGS.fontSize;
  out.lineHeightPercent = SETTINGS.lineHeightPercent;
  out.orientation = SETTINGS.orientation;
  out.screenMargin = SETTINGS.screenMargin;
  out.publisherPageNumbers = SETTINGS.publisherPageNumbers;
  out.paragraphAlignment = SETTINGS.paragraphAlignment;
  out.embeddedStyle = SETTINGS.embeddedStyle;
  out.hyphenationEnabled = SETTINGS.hyphenationEnabled;
  out.textAntiAliasing = SETTINGS.textAntiAliasing;
  out.textDarkness = SETTINGS.textDarkness;
  out.readerRefreshMode = SETTINGS.readerRefreshMode;
  out.readerDarkMode = SETTINGS.readerDarkMode;
  out.imageRendering = SETTINGS.imageRendering;
  out.extraParagraphSpacing = SETTINGS.extraParagraphSpacing;
  out.forceParagraphIndents = SETTINGS.forceParagraphIndents;
  out.bionicReadingEnabled = SETTINGS.bionicReadingEnabled;
  out.guideReadingEnabled = SETTINGS.guideReadingEnabled;
  out.epubRenderMode = normalizeRenderModeRaw(SETTINGS.epubRenderMode);
  out.sdFontPointSize = SETTINGS.sdFontPointSize;
  std::strncpy(out.sdFontFamilyName, SETTINGS.sdFontFamilyName, sizeof(out.sdFontFamilyName) - 1);
  out.sdFontFamilyName[sizeof(out.sdFontFamilyName) - 1] = '\0';
}

bool readerSettingsChanged(const EpubReaderActivity::ReaderSettingsSnapshot& before,
                           const EpubReaderActivity::ReaderSettingsSnapshot& after) {
  return before.fontFamily != after.fontFamily || before.fontSize != after.fontSize ||
         before.lineHeightPercent != after.lineHeightPercent || before.orientation != after.orientation ||
         before.screenMargin != after.screenMargin || before.publisherPageNumbers != after.publisherPageNumbers ||
         before.paragraphAlignment != after.paragraphAlignment || before.embeddedStyle != after.embeddedStyle ||
         before.hyphenationEnabled != after.hyphenationEnabled || before.textAntiAliasing != after.textAntiAliasing ||
         before.textDarkness != after.textDarkness || before.readerRefreshMode != after.readerRefreshMode ||
         before.readerDarkMode != after.readerDarkMode || before.imageRendering != after.imageRendering ||
         before.extraParagraphSpacing != after.extraParagraphSpacing ||
         before.forceParagraphIndents != after.forceParagraphIndents ||
         before.bionicReadingEnabled != after.bionicReadingEnabled ||
         before.guideReadingEnabled != after.guideReadingEnabled || before.epubRenderMode != after.epubRenderMode ||
         before.sdFontPointSize != after.sdFontPointSize ||
         std::strncmp(before.sdFontFamilyName, after.sdFontFamilyName, sizeof(before.sdFontFamilyName)) != 0;
}

uint8_t clampedStoredReaderFontSize(const EpubReaderActivity::ReaderSettingsSnapshot& in) {
  if (in.sdFontFamilyName[0] != '\0') {
    return std::min<uint8_t>(in.fontSize, CrossPointSettings::SD_FONT_MAX_SIZE_STEPS - 1);
  }
  const uint8_t builtinSizeCount = CrossPointSettings::getActiveReaderFontSizeCount();
  return in.fontSize < builtinSizeCount ? in.fontSize : SETTINGS.fontSize;
}

void applyReaderSettings(const EpubReaderActivity::ReaderSettingsSnapshot& in) {
  SETTINGS.fontFamily = in.fontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? in.fontFamily : SETTINGS.fontFamily;
  std::strncpy(SETTINGS.sdFontFamilyName, in.sdFontFamilyName, sizeof(SETTINGS.sdFontFamilyName) - 1);
  SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
  SETTINGS.sdFontPointSize = SETTINGS.sdFontFamilyName[0] != '\0' ? in.sdFontPointSize : 0;
  SETTINGS.fontSize = clampedStoredReaderFontSize(in);
  SETTINGS.lineHeightPercent = CrossPointSettings::clampedLineHeightPercent(in.lineHeightPercent);
  SETTINGS.orientation = in.orientation < CrossPointSettings::ORIENTATION_COUNT ? in.orientation : SETTINGS.orientation;
  SETTINGS.screenMargin = std::clamp<uint8_t>(in.screenMargin, 5, 40);
  SETTINGS.publisherPageNumbers = in.publisherPageNumbers ? 1 : 0;
  SETTINGS.paragraphAlignment = in.paragraphAlignment < CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT
                                    ? in.paragraphAlignment
                                    : SETTINGS.paragraphAlignment;
  SETTINGS.embeddedStyle = in.embeddedStyle ? 1 : 0;
  SETTINGS.hyphenationEnabled = in.hyphenationEnabled ? 1 : 0;
  SETTINGS.textAntiAliasing = in.textAntiAliasing ? 1 : 0;
  SETTINGS.textDarkness = in.textDarkness < CrossPointSettings::TEXT_DARKNESS_COUNT
                              ? in.textDarkness
                              : CrossPointSettings::TEXT_DARKNESS_NORMAL;
  SETTINGS.readerRefreshMode = in.readerRefreshMode < CrossPointSettings::READER_REFRESH_MODE_COUNT
                                   ? in.readerRefreshMode
                                   : CrossPointSettings::READER_REFRESH_AUTO;
  SETTINGS.readerDarkMode = in.readerDarkMode ? 1 : 0;
  SETTINGS.imageRendering =
      in.imageRendering < CrossPointSettings::IMAGE_RENDERING_COUNT ? in.imageRendering : SETTINGS.imageRendering;
  SETTINGS.extraParagraphSpacing = in.extraParagraphSpacing ? 1 : 0;
  SETTINGS.forceParagraphIndents = in.forceParagraphIndents ? 1 : 0;
  SETTINGS.bionicReadingEnabled = BionicReadingMode::normalize(in.bionicReadingEnabled);
  SETTINGS.guideReadingEnabled = in.guideReadingEnabled ? 1 : 0;
  SETTINGS.epubRenderMode = normalizeRenderModeRaw(in.epubRenderMode);
}

struct BookReaderSettingsData {
  bool hasAutoPageTurnInterval = false;
  uint16_t autoPageTurnSeconds = DEFAULT_AUTO_PAGE_TURN_INTERVAL_S;
  bool hasCustomReaderSettings = false;
  bool hasRenderModeOverride = false;
  uint8_t renderMode = static_cast<uint8_t>(EpubRenderMode::CrossInkDefault);
  EpubReaderActivity::ReaderSettingsSnapshot readerSettings;
};

bool readReaderSettingsSnapshot(FsFile& file, EpubReaderActivity::ReaderSettingsSnapshot& out, const uint8_t version) {
  if (!(readU8(file, out.fontFamily) && readU8(file, out.fontSize) && readU8(file, out.lineHeightPercent) &&
        readU8(file, out.orientation) && readU8(file, out.screenMargin) && readU8(file, out.publisherPageNumbers) &&
        readU8(file, out.paragraphAlignment) && readU8(file, out.embeddedStyle) &&
        readU8(file, out.hyphenationEnabled) && readU8(file, out.textAntiAliasing) &&
        readU8(file, out.readerDarkMode) && readU8(file, out.imageRendering) &&
        readU8(file, out.extraParagraphSpacing) && readU8(file, out.forceParagraphIndents) &&
        readU8(file, out.bionicReadingEnabled) && readU8(file, out.guideReadingEnabled))) {
    return false;
  }
  if (!readU8(file, out.epubRenderMode)) {
    return false;
  }
  out.epubRenderMode = normalizeRenderModeRaw(out.epubRenderMode);
  if (!readExact(file, out.sdFontFamilyName, sizeof(out.sdFontFamilyName))) return false;
  if (version >= STABLE_POINT_SIZE_READER_SETTINGS_FILE_VERSION && !readU8(file, out.sdFontPointSize)) {
    return false;
  }
  if (version >= READER_SETTINGS_FILE_VERSION) {
    return readU8(file, out.textDarkness) && readU8(file, out.readerRefreshMode);
  }
  return true;
}

bool writeReaderSettingsSnapshot(FsFile& file, const EpubReaderActivity::ReaderSettingsSnapshot& in) {
  return writeU8(file, in.fontFamily) && writeU8(file, in.fontSize) && writeU8(file, in.lineHeightPercent) &&
         writeU8(file, in.orientation) && writeU8(file, in.screenMargin) && writeU8(file, in.publisherPageNumbers) &&
         writeU8(file, in.paragraphAlignment) && writeU8(file, in.embeddedStyle) &&
         writeU8(file, in.hyphenationEnabled) && writeU8(file, in.textAntiAliasing) &&
         writeU8(file, in.readerDarkMode) && writeU8(file, in.imageRendering) &&
         writeU8(file, in.extraParagraphSpacing) && writeU8(file, in.forceParagraphIndents) &&
         writeU8(file, in.bionicReadingEnabled) && writeU8(file, in.guideReadingEnabled) &&
         writeU8(file, normalizeRenderModeRaw(in.epubRenderMode)) &&
         writeExact(file, in.sdFontFamilyName, sizeof(in.sdFontFamilyName)) && writeU8(file, in.sdFontPointSize) &&
         writeU8(file, in.textDarkness) && writeU8(file, in.readerRefreshMode);
}

BookReaderSettingsData loadBookReaderSettingsFile(const std::string& cachePath) {
  BookReaderSettingsData data;
  captureReaderSettings(data.readerSettings);

  FsFile file;
  if (!Storage.openFileForRead("ERS", cachePath + READER_SETTINGS_FILE_NAME, file)) {
    return data;
  }

  uint8_t version = 0;
  if (!readU8(file, version)) {
    file.close();
    LOG_DBG("ERS", "Reader settings missing version, using defaults");
    return data;
  }

  if (version == LEGACY_READER_SETTINGS_FILE_VERSION) {
    uint16_t seconds = 0;
    if (readU16(file, seconds) && seconds != 0) {
      data.hasAutoPageTurnInterval = true;
      data.autoPageTurnSeconds = clampAutoPageTurnIntervalSeconds(seconds);
    }
    file.close();
    return data;
  }

  if (version != INDEXED_READER_SETTINGS_FILE_VERSION && version != STABLE_POINT_SIZE_READER_SETTINGS_FILE_VERSION &&
      version != READER_SETTINGS_FILE_VERSION) {
    file.close();
    LOG_DBG("ERS", "Reader settings version mismatch, using defaults");
    return data;
  }

  uint8_t flags = 0;
  uint16_t seconds = 0;
  uint8_t renderMode = static_cast<uint8_t>(EpubRenderMode::CrossInkDefault);
  EpubReaderActivity::ReaderSettingsSnapshot snapshot = data.readerSettings;
  bool ok = readU8(file, flags) && readU16(file, seconds);
  if (ok) {
    ok = readU8(file, renderMode);
  }
  if (ok) {
    ok = readReaderSettingsSnapshot(file, snapshot, version);
  }
  file.close();
  if (!ok) {
    LOG_ERR("ERS", "Reader settings file is truncated, using defaults");
    return data;
  }

  if ((flags & READER_SETTINGS_FLAG_AUTO_PAGE_TURN) && seconds != 0) {
    data.hasAutoPageTurnInterval = true;
    data.autoPageTurnSeconds = clampAutoPageTurnIntervalSeconds(seconds);
  }
  if (flags & READER_SETTINGS_FLAG_CUSTOM) {
    data.hasCustomReaderSettings = true;
    data.readerSettings = snapshot;
  }
  if (flags & READER_SETTINGS_FLAG_RENDER_MODE) {
    data.hasRenderModeOverride = true;
    data.renderMode = normalizeRenderModeRaw(renderMode);
  }
  return data;
}

bool saveBookReaderSettingsFile(const std::string& cachePath, const bool hasAutoPageTurnInterval,
                                const uint16_t autoPageTurnSeconds, const bool hasCustomReaderSettings,
                                const bool hasRenderModeOverride, const uint8_t renderMode,
                                const EpubReaderActivity::ReaderSettingsSnapshot& readerSettings) {
  FsFile file;
  if (!Storage.openFileForWrite("ERS", cachePath + READER_SETTINGS_FILE_NAME, file)) {
    LOG_ERR("ERS", "Could not open reader settings file for write");
    return false;
  }

  uint8_t flags = 0;
  if (hasCustomReaderSettings) flags |= READER_SETTINGS_FLAG_CUSTOM;
  if (hasAutoPageTurnInterval) flags |= READER_SETTINGS_FLAG_AUTO_PAGE_TURN;
  if (hasRenderModeOverride) flags |= READER_SETTINGS_FLAG_RENDER_MODE;
  const uint16_t clampedSeconds = clampAutoPageTurnIntervalSeconds(autoPageTurnSeconds);
  EpubReaderActivity::ReaderSettingsSnapshot normalizedReaderSettings = readerSettings;
  normalizedReaderSettings.epubRenderMode = normalizeRenderModeRaw(renderMode);
  const bool ok = writeU8(file, READER_SETTINGS_FILE_VERSION) && writeU8(file, flags) &&
                  writeU16(file, clampedSeconds) && writeU8(file, normalizeRenderModeRaw(renderMode)) &&
                  writeReaderSettingsSnapshot(file, normalizedReaderSettings);
  file.close();
  if (!ok) {
    LOG_ERR("ERS", "Short write saving reader settings");
  }
  return ok;
}

bool saveBookRenderModeForCache(const std::string& cachePath, const uint8_t renderMode) {
  BookReaderSettingsData data = loadBookReaderSettingsFile(cachePath);
  data.hasRenderModeOverride = true;
  data.renderMode = normalizeRenderModeRaw(renderMode);
  data.readerSettings.epubRenderMode = data.renderMode;
  return saveBookReaderSettingsFile(cachePath, data.hasAutoPageTurnInterval, data.autoPageTurnSeconds,
                                    data.hasCustomReaderSettings, data.hasRenderModeOverride, data.renderMode,
                                    data.readerSettings);
}

bool saveRuntimeReaderSettingsForCache(const std::string& cachePath) {
  BookReaderSettingsData data = loadBookReaderSettingsFile(cachePath);
  EpubReaderActivity::ReaderSettingsSnapshot snapshot;
  captureReaderSettings(snapshot);
  data.hasCustomReaderSettings = true;
  data.hasRenderModeOverride = true;
  data.renderMode = normalizeRenderModeRaw(SETTINGS.epubRenderMode);
  return saveBookReaderSettingsFile(cachePath, data.hasAutoPageTurnInterval, data.autoPageTurnSeconds,
                                    data.hasCustomReaderSettings, data.hasRenderModeOverride, data.renderMode,
                                    snapshot);
}

class ScopedReaderSettingsRestore {
 public:
  ScopedReaderSettingsRestore() { captureReaderSettings(snapshot); }
  ~ScopedReaderSettingsRestore() { applyReaderSettings(snapshot); }

 private:
  EpubReaderActivity::ReaderSettingsSnapshot snapshot;
};

void formatCompactTimeLeft(const uint32_t seconds, char* out, const size_t outSize) {
  if (!out || outSize == 0) return;
  if (seconds < 60) {
    snprintf(out, outSize, "<1m");
    return;
  }

  const uint32_t minutes = (seconds + 30U) / 60U;
  if (minutes < 60) {
    snprintf(out, outSize, "%lum", static_cast<unsigned long>(minutes));
    return;
  }

  const uint32_t hours = minutes / 60U;
  const uint32_t remainingMinutes = minutes % 60U;
  if (remainingMinutes == 0) {
    snprintf(out, outSize, "%luh", static_cast<unsigned long>(hours));
  } else {
    snprintf(out, outSize, "%luh %lum", static_cast<unsigned long>(hours),
             static_cast<unsigned long>(remainingMinutes));
  }
}

// SD card folder finished books are moved into. Single source of truth for the path.
constexpr char READ_FOLDER[] = "/Read";

// True if path is inside READ_FOLDER (starts with "<READ_FOLDER>/"). Non-allocating so
// it is cheap to call from loop(), and avoids reintroducing a separate "/Read/" literal.
bool isInReadFolder(const std::string& path) {
  constexpr size_t n = sizeof(READ_FOLDER) - 1;  // excludes NUL
  return path.size() > n && path.compare(0, n, READ_FOLDER) == 0 && path[n] == '/';
}

// Relocate a finished book into /Read/, then migrate path-keyed state such as
// cache files, bookmarks, recents, and resume path.
void moveFinishedBookToReadFolder(const std::string& srcPath, const std::string& dstPath,
                                  const std::string& oldCachePath, const std::string& title,
                                  const std::string& author) {
  LOG_INF("ERS", "Moving finished epub: %s -> %s", srcPath.c_str(), dstPath.c_str());
  if (!Storage.rename(srcPath.c_str(), dstPath.c_str())) {
    LOG_ERR("ERS", "Failed to move finished book to '/Read' folder");
    snprintf(APP_STATE.pendingAlertTitle, sizeof(APP_STATE.pendingAlertTitle), "%s", tr(STR_MOVE_TO_READ_FAILED_TITLE));
    snprintf(APP_STATE.pendingAlertBody, sizeof(APP_STATE.pendingAlertBody), tr(STR_MOVE_TO_READ_FAILED_BODY),
             title.c_str());
    APP_STATE.pendingAlertGoHomeOnBack.store(false, std::memory_order_relaxed);
    APP_STATE.hasPendingAlert.store(true, std::memory_order_release);
    return;
  }

  BookMoveUtils::migrateMovedEpubState(srcPath, dstPath, oldCachePath, title, author,
                                       !SETTINGS.removeReadBooksFromRecents);
}

}  // namespace

uint8_t EpubReaderActivity::loadBookRenderMode(const std::string& filePath) {
  Epub epub(filePath, DUET_BOOKS_ROOT_PATH "");
  epub.setupCacheDir();
  const BookReaderSettingsData data = loadBookReaderSettingsFile(epub.getCachePath());
  return data.hasRenderModeOverride ? normalizeRenderModeRaw(data.renderMode)
                                    : static_cast<uint8_t>(EpubRenderMode::CrossInkDefault);
}

bool EpubReaderActivity::saveBookRenderMode(const std::string& filePath, const uint8_t renderMode) {
  Epub epub(filePath, DUET_BOOKS_ROOT_PATH "");
  epub.setupCacheDir();
  return saveBookRenderModeForCache(epub.getCachePath(), renderMode);
}

bool EpubReaderActivity::resetBookReaderSettings(const std::string& filePath) {
  Epub epub(filePath, DUET_BOOKS_ROOT_PATH "");
  const std::string settingsPath = epub.getCachePath() + READER_SETTINGS_FILE_NAME;
  if (!Storage.exists(settingsPath.c_str())) {
    return true;
  }
  if (!Storage.remove(settingsPath.c_str())) {
    LOG_ERR("ERS", "Failed to reset reader settings: %s", settingsPath.c_str());
    return false;
  }
  LOG_INF("ERS", "Reset reader settings: %s", settingsPath.c_str());
  return true;
}

float EpubReaderActivity::getCurrentBookProgressPercent() const {
  const int totalPages = section ? section->pageCount : 0;
  if (activeFootnotePreview || !epub || !section || totalPages == 0 || epub->getBookSize() == 0) {
    return 0.0f;
  }

  const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(totalPages);
  return epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f;
}

void EpubReaderActivity::pauseReadingPaceTimer(const char* reason) {
  if (!activeFootnotePreview) {
    recordCurrentPageReadingTime(reason);
  }
  // Every pause is a user-initiated transition (menu, dialog, jump) — a natural
  // checkpoint to persist any debounced page position.
  flushPendingProgress();
  pageShownAtMs = 0UL;
  paceSampleWarmupPending = true;
}

void EpubReaderActivity::resumeReadingPaceTimer(const char*) {
  if (activeFootnotePreview) {
    pageShownAtMs = 0UL;
    return;
  }
  if (section && section->pageCount > 0 && section->currentPage >= 0 && section->currentPage < section->pageCount) {
    pageShownAtMs = millis();
  } else {
    pageShownAtMs = 0UL;
  }
}

void EpubReaderActivity::armReadingPaceWarmup(const char*) { paceSampleWarmupPending = true; }

bool EpubReaderActivity::forwardPageReadElapsed(uint32_t& seconds, const char*) const {
  seconds = 0;
  if (activeFootnotePreview || !epub || !shouldTrackReadingStatsForPath(epub->getPath()) || pageShownAtMs == 0UL) {
    return false;
  }

  const unsigned long elapsedMs = millis() - pageShownAtMs;
  if (elapsedMs < MIN_READING_STATS_PAGE_MS) {
    return false;
  }

  seconds = static_cast<uint32_t>(elapsedMs / 1000UL);
  return true;
}

bool EpubReaderActivity::currentPageReadingSecondsForStats(uint32_t& seconds, const char* source) const {
  seconds = 0;
  if (activeFootnotePreview || !epub || !shouldTrackReadingStatsForPath(epub->getPath()) || pageShownAtMs == 0UL) {
    return false;
  }

  const unsigned long elapsedMs = millis() - pageShownAtMs;
  const uint32_t elapsedSeconds = static_cast<uint32_t>(elapsedMs / 1000UL);
  if (elapsedSeconds == 0) {
    return false;
  }

  const uint32_t thresholdSeconds = SETTINGS.getReadingIdleTimeThresholdSeconds();
  if (elapsedSeconds > thresholdSeconds) {
    LOG_DBG("ERS", "Reading time interval rejected as idle: source=%s seconds=%lu threshold=%lu",
            source ? source : "unknown", static_cast<unsigned long>(elapsedSeconds),
            static_cast<unsigned long>(thresholdSeconds));
    return false;
  }

  seconds = elapsedSeconds;
  return true;
}

void EpubReaderActivity::recordCurrentPageReadingTime(const char* source) {
  if (activeFootnotePreview) {
    pageShownAtMs = 0UL;
    return;
  }
  uint32_t seconds = 0;
  if (currentPageReadingSecondsForStats(seconds, source)) {
    sessionReadingSeconds = sessionReadingSeconds > UINT32_MAX - seconds ? UINT32_MAX : sessionReadingSeconds + seconds;
  }
  pageShownAtMs = 0UL;
}

void EpubReaderActivity::recordForwardPagePaceSample(uint32_t seconds, const char* source) {
  if (paceSampleWarmupPending) {
    paceSampleWarmupPending = false;
    return;
  }

  if (seconds < MIN_READING_PACE_SAMPLE_SECONDS) {
    LOG_DBG("ERS", "Time-left pace sample rejected: source=%s seconds=%lu minSeconds=%lu", source ? source : "unknown",
            static_cast<unsigned long>(seconds), static_cast<unsigned long>(MIN_READING_PACE_SAMPLE_SECONDS));
    return;
  }

  const uint32_t maxReadingPaceSampleSeconds = SETTINGS.getReadingIdleTimeThresholdSeconds();
  if (seconds > maxReadingPaceSampleSeconds) {
    LOG_DBG("ERS", "Time-left pace sample rejected: source=%s seconds=%lu maxSeconds=%lu", source ? source : "unknown",
            static_cast<unsigned long>(seconds), static_cast<unsigned long>(maxReadingPaceSampleSeconds));
    return;
  }

  if (sessionPaceSampleCount < UINT16_MAX && sessionPaceSampleSeconds <= UINT32_MAX - static_cast<uint32_t>(seconds)) {
    sessionPaceSampleSeconds += seconds;
    sessionPaceSampleCount++;
  }

  stats.recordForwardPageRead(seconds);
  recoverStoredPaceFromSession("pace_sample");
}

bool EpubReaderActivity::getSessionAveragePaceSeconds(uint16_t& avgSeconds) const {
  avgSeconds = 0;
  if (sessionPaceSampleCount < MIN_SESSION_TIME_LEFT_PACE_SAMPLE_COUNT || sessionPaceSampleSeconds == 0) {
    return false;
  }
  const uint32_t roundedAvg =
      (sessionPaceSampleSeconds + static_cast<uint32_t>(sessionPaceSampleCount / 2)) / sessionPaceSampleCount;
  avgSeconds = static_cast<uint16_t>(std::min<uint32_t>(roundedAvg, UINT16_MAX));
  return avgSeconds > 0;
}

void EpubReaderActivity::recoverStoredPaceFromSession(const char* reason) {
  if (stats.avgSecondsPerForwardPage == 0) {
    return;
  }

  uint16_t sessionAvg = 0;
  if (!getSessionAveragePaceSeconds(sessionAvg)) {
    return;
  }

  const uint32_t slowerRecoveryThreshold =
      (static_cast<uint32_t>(stats.avgSecondsPerForwardPage) * STORED_PACE_SLOWER_RECOVERY_PERCENT + 99U) / 100U;
  if (sessionPaceSampleCount >= MIN_STORED_PACE_SLOWER_RECOVERY_SESSION_SAMPLES &&
      sessionAvg >= slowerRecoveryThreshold) {
    LOG_DBG("ERS",
            "Time-left stored pace recovered: reason=%s direction=slower avg=%u->%u samples=%u sessionSamples=%u "
            "threshold=%lu",
            reason ? reason : "unknown", stats.avgSecondsPerForwardPage, sessionAvg, stats.paceSampleCount,
            sessionPaceSampleCount, static_cast<unsigned long>(slowerRecoveryThreshold));
    stats.avgSecondsPerForwardPage = sessionAvg;
    return;
  }

  const uint32_t fasterRecoveryThreshold =
      (static_cast<uint32_t>(stats.avgSecondsPerForwardPage) * STORED_PACE_FASTER_RECOVERY_PERCENT) / 100U;
  if (sessionPaceSampleCount >= MIN_STORED_PACE_FASTER_RECOVERY_SESSION_SAMPLES &&
      sessionAvg <= fasterRecoveryThreshold) {
    LOG_DBG("ERS",
            "Time-left stored pace recovered: reason=%s direction=faster avg=%u->%u samples=%u sessionSamples=%u "
            "threshold=%lu",
            reason ? reason : "unknown", stats.avgSecondsPerForwardPage, sessionAvg, stats.paceSampleCount,
            sessionPaceSampleCount, static_cast<unsigned long>(fasterRecoveryThreshold));
    stats.avgSecondsPerForwardPage = sessionAvg;
  }
}

bool EpubReaderActivity::getTimeLeftPaceSeconds(uint16_t& avgSeconds, const char*& source,
                                                uint16_t& sampleCount) const {
  if (getSessionAveragePaceSeconds(avgSeconds)) {
    source = "session_pace";
    sampleCount = sessionPaceSampleCount;
    return true;
  }
  if (hasEnoughPaceSamplesForTimeLeft(stats)) {
    avgSeconds = stats.avgSecondsPerForwardPage;
    source = "stored_pace";
    sampleCount = stats.paceSampleCount;
    return true;
  }
  avgSeconds = 0;
  source = "none";
  sampleCount = 0;
  return false;
}

bool EpubReaderActivity::estimateRemainingTimeLeftPages(const bool bookEstimate, float& remainingPages) const {
  remainingPages = 0.0f;
  const int totalPages = section ? section->pageCount : 0;
  if (!epub || !section || totalPages == 0) {
    return false;
  }

  if (!bookEstimate) {
    const int remainingChapterPages = totalPages - section->currentPage - 1;
    if (remainingChapterPages <= 0) {
      return false;
    }
    remainingPages = static_cast<float>(remainingChapterPages);
  } else {
    const size_t bookSize = epub->getBookSize();
    if (bookSize == 0) {
      return false;
    }

    const size_t prevChapterSize = currentSpineIndex >= 1 ? epub->getCumulativeSpineItemSize(currentSpineIndex - 1) : 0;
    const size_t cumulativeSize = epub->getCumulativeSpineItemSize(currentSpineIndex);
    if (cumulativeSize <= prevChapterSize) {
      return false;
    }

    const float chapterSize = static_cast<float>(cumulativeSize - prevChapterSize);
    const float completedCurrentChapter =
        (static_cast<float>(section->currentPage) / static_cast<float>(totalPages)) * chapterSize;
    const float completedBookSize = static_cast<float>(prevChapterSize) + completedCurrentChapter;
    if (completedBookSize >= static_cast<float>(bookSize)) {
      return false;
    }

    const float bytesPerPage = chapterSize / static_cast<float>(totalPages);
    if (bytesPerPage <= 0.0f) {
      return false;
    }
    remainingPages = (static_cast<float>(bookSize) - completedBookSize) / bytesPerPage;
  }

  return remainingPages > 0.0f;
}

bool EpubReaderActivity::estimateProgressTimeLeftSeconds(uint32_t& seconds) const {
  seconds = 0;
  const int totalPages = section ? section->pageCount : 0;
  if (!epub || !section || totalPages == 0 || epub->getBookSize() == 0) {
    return false;
  }
  const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(totalPages);
  const float progressPercent = epub->calculateSizeProgress(currentSpineIndex, chapterProgress) * 100.0f;
  uint32_t currentPageSeconds = 0;
  uint32_t sessionSeconds = sessionReadingSeconds;
  if (epub && shouldTrackReadingStatsForPath(epub->getPath()) &&
      currentPageReadingSecondsForStats(currentPageSeconds, "time_left_preview")) {
    sessionSeconds =
        sessionSeconds > UINT32_MAX - currentPageSeconds ? UINT32_MAX : sessionSeconds + currentPageSeconds;
  }
  const uint32_t elapsedReadingSeconds =
      stats.totalReadingSeconds > UINT32_MAX - sessionSeconds ? UINT32_MAX : stats.totalReadingSeconds + sessionSeconds;

  if (progressPercent <= 0.0f || progressPercent >= 100.0f || elapsedReadingSeconds < 120) {
    return false;
  }

  const float progress = progressPercent / 100.0f;
  const float estimate = (static_cast<float>(elapsedReadingSeconds) * (1.0f - progress)) / progress;
  if (estimate <= 0.0f) {
    return false;
  }

  seconds = static_cast<uint32_t>(std::min(estimate + 0.5f, static_cast<float>(UINT32_MAX)));
  return seconds > 0;
}

bool EpubReaderActivity::estimateTimeLeftSeconds(const bool bookEstimate, uint32_t& seconds) const {
  seconds = 0;
  uint32_t currentPageSeconds = 0;
  uint32_t elapsedReadingSeconds = stats.totalReadingSeconds > UINT32_MAX - sessionReadingSeconds
                                       ? UINT32_MAX
                                       : stats.totalReadingSeconds + sessionReadingSeconds;
  if (epub && shouldTrackReadingStatsForPath(epub->getPath()) &&
      currentPageReadingSecondsForStats(currentPageSeconds, "time_left_reliability")) {
    elapsedReadingSeconds = elapsedReadingSeconds > UINT32_MAX - currentPageSeconds
                                ? UINT32_MAX
                                : elapsedReadingSeconds + currentPageSeconds;
  }
  if (elapsedReadingSeconds < BookReadingStats::MIN_RELIABLE_TIME_LEFT_READING_SECONDS) {
    return false;
  }

  uint16_t paceSeconds = 0;
  const char* paceSource = "none";
  uint16_t paceSampleCount = 0;
  const bool hasPace = getTimeLeftPaceSeconds(paceSeconds, paceSource, paceSampleCount);

  uint32_t paceEstimateSeconds = 0;
  bool hasPaceEstimate = false;
  float remainingPages = 0.0f;
  if (hasPace && estimateRemainingTimeLeftPages(bookEstimate, remainingPages)) {
    const float estimatedSeconds = remainingPages * static_cast<float>(paceSeconds);
    if (estimatedSeconds > 0.0f) {
      paceEstimateSeconds = static_cast<uint32_t>(std::min(estimatedSeconds + 0.5f, static_cast<float>(UINT32_MAX)));
      hasPaceEstimate = paceEstimateSeconds > 0;
    }
  }

  uint32_t progressEstimateSeconds = 0;
  bool hasProgressEstimate = false;
  if (bookEstimate && hasPace) {
    hasProgressEstimate = estimateProgressTimeLeftSeconds(progressEstimateSeconds);
  }
  if (!hasPaceEstimate && !hasProgressEstimate) {
    return false;
  }

  if (!hasPaceEstimate) {
    seconds = progressEstimateSeconds;
  } else if (hasProgressEstimate) {
    const uint32_t progressFloorSeconds = static_cast<uint32_t>(std::min<uint64_t>(
        (static_cast<uint64_t>(progressEstimateSeconds) * BOOK_PROGRESS_ESTIMATE_FLOOR_PERCENT + 99ULL) / 100ULL,
        UINT32_MAX));
    if (paceEstimateSeconds < progressFloorSeconds) {
      seconds = progressFloorSeconds;
    } else {
      seconds = paceEstimateSeconds;
    }
  } else {
    seconds = paceEstimateSeconds;
  }
  return seconds > 0;
}

bool EpubReaderActivity::formatTimeLeftLabel(char* buf, const size_t len) const {
  if (!buf || len == 0 || SETTINGS.statusBarTimeLeft == CrossPointSettings::STATUS_BAR_TIME_LEFT::TIME_LEFT_HIDE) {
    return false;
  }

  const bool bookEstimate = SETTINGS.statusBarTimeLeft == CrossPointSettings::STATUS_BAR_TIME_LEFT::TIME_LEFT_BOOK;
  uint32_t seconds = 0;
  if (estimateTimeLeftSeconds(bookEstimate, seconds)) {
    formatCompactTimeLeft(seconds, buf, len);
    return true;
  }

  uint16_t paceSeconds = 0;
  const char* paceSource = "none";
  uint16_t paceSampleCount = 0;
  if (!getTimeLeftPaceSeconds(paceSeconds, paceSource, paceSampleCount)) {
    float remainingPages = 0.0f;
    if (!estimateRemainingTimeLeftPages(bookEstimate, remainingPages)) {
      return false;
    }
    snprintf(buf, len, "%s", tr(STR_TIME_LEFT_CALCULATING));
    return true;
  }

  return false;
}

void EpubReaderActivity::refreshCachedTimeLeftEstimate() {
  uint32_t seconds = 0;
  stats.estimatedTimeLeftSeconds = (!stats.isCompleted && estimateTimeLeftSeconds(true, seconds)) ? seconds : 0;
}

// The stats screen can edit dates/completion using a preview copy that includes
// current-session time, so keep live counters in memory and import only edits.
void EpubReaderActivity::applyBookStatsEditsFromDisk() {
  if (epub) {
    const BookReadingStats diskStats = BookReadingStats::load(epub->getCachePath());
    stats.isCompleted = diskStats.isCompleted;
    stats.startDateManual = diskStats.startDateManual;
    stats.finishedDateManual = diskStats.finishedDateManual;
    stats.startDate = diskStats.startDate;
    stats.finishedDate = diskStats.finishedDate;
  }

  const GlobalReadingStats diskGlobalStats = GlobalReadingStats::load();
  globalStats.completedBooks = diskGlobalStats.completedBooks;
}

void EpubReaderActivity::handleBookStatsReturn() {
  applyBookStatsEditsFromDisk();
  completionPromptShown = stats.isCompleted;
  if (stats.isCompleted && SETTINGS.moveFinishedToReadFolder && epub && !isInReadFolder(epub->getPath())) {
    pendingReadFolderMove = true;
  } else if (!stats.isCompleted) {
    pendingReadFolderMove = false;
  }
  resumeReadingPaceTimer("book_stats_return");
  requestUpdate();
}

void EpubReaderActivity::initializeCompletionPromptTrigger() {
  completionTriggerSpineIndex = -1;
  completionTriggerSpineProgress = 1.0f;
  completionPromptQueued = false;
  completionPromptShown = stats.isCompleted;
  completionTriggerSeenBelow = false;
  completionTriggerCrossed = false;
  lastAtOrPastCompletionTrigger = false;

  if (!epub) {
    return;
  }

  const size_t bookSize = epub->getBookSize();
  const int spineCount = epub->getSpineItemsCount();
  if (bookSize == 0 || spineCount <= 0) {
    return;
  }

  int locationSpineIndex = 0;
  float locationSpineProgress = 0.0f;
  if (epub->resolveLocationPercentToSpineProgress(99, locationSpineIndex, locationSpineProgress)) {
    completionTriggerSpineIndex = locationSpineIndex;
    completionTriggerSpineProgress = locationSpineProgress;
    return;
  }

  size_t targetSize = (bookSize / 100) * 99 + (bookSize % 100) * 99 / 100;
  if (targetSize >= bookSize) {
    targetSize = bookSize - 1;
  }

  int targetSpineIndex = spineCount - 1;
  size_t prevCumulative = 0;

  for (int i = 0; i < spineCount; i++) {
    const size_t cumulative = epub->getCumulativeSpineItemSize(i);
    if (targetSize <= cumulative) {
      targetSpineIndex = i;
      prevCumulative = (i > 0) ? epub->getCumulativeSpineItemSize(i - 1) : 0;
      break;
    }
  }

  const size_t cumulative = epub->getCumulativeSpineItemSize(targetSpineIndex);
  const size_t spineSize = (cumulative > prevCumulative) ? (cumulative - prevCumulative) : 0;

  completionTriggerSpineIndex = targetSpineIndex;
  completionTriggerSpineProgress =
      (spineSize == 0) ? 0.0f : static_cast<float>(targetSize - prevCumulative) / static_cast<float>(spineSize);

  if (completionTriggerSpineProgress < 0.0f) {
    completionTriggerSpineProgress = 0.0f;
  } else if (completionTriggerSpineProgress > 1.0f) {
    completionTriggerSpineProgress = 1.0f;
  }
}

bool EpubReaderActivity::isAtOrPastCompletionTrigger() const {
  const int totalPages = section ? section->pageCount : 0;
  if (!epub || !section || totalPages == 0 || completionTriggerSpineIndex < 0) {
    return false;
  }

  if (currentSpineIndex > completionTriggerSpineIndex) {
    return true;
  }
  if (currentSpineIndex < completionTriggerSpineIndex) {
    return false;
  }

  const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(totalPages);
  return chapterProgress >= completionTriggerSpineProgress;
}

bool EpubReaderActivity::shouldQueueCompletionPromptOnChapterExit() const {
  if (completionPromptShown || completionPromptQueued || stats.isCompleted || footnoteDepth > 0 ||
      !completionTriggerCrossed || !epub || !section || section->pageCount == 0 || completionTriggerSpineIndex < 0 ||
      section->currentPage < 0) {
    return false;
  }

  if (currentSpineIndex != completionTriggerSpineIndex) {
    return false;
  }

  return section->currentPage >= section->pageCount - 1;
}

void EpubReaderActivity::queueCompletionPromptIfNeeded() {
  if (completionPromptShown || completionPromptQueued || stats.isCompleted || footnoteDepth > 0) {
    return;
  }

  const bool atOrPastTrigger = isAtOrPastCompletionTrigger();

  if (!atOrPastTrigger) {
    completionTriggerSeenBelow = true;
  }

  if (completionTriggerSeenBelow && !lastAtOrPastCompletionTrigger && atOrPastTrigger) {
    completionTriggerCrossed = true;
  }

  lastAtOrPastCompletionTrigger = atOrPastTrigger;
}

void EpubReaderActivity::captureGlobalReaderSettings() {
  captureReaderSettings(globalReaderSettingsBeforeBook);
  restoreGlobalReaderSettingsOnExit = true;
}

void EpubReaderActivity::restoreGlobalReaderSettings() {
  if (!restoreGlobalReaderSettingsOnExit) {
    return;
  }
  applyReaderSettings(globalReaderSettingsBeforeBook);
  restoreGlobalReaderSettingsOnExit = false;
}

void EpubReaderActivity::loadBookReaderSettings() {
  bookHasCustomReaderSettings = false;
  bookHasAutoPageTurnInterval = false;
  lastAutoPageTurnIntervalSeconds = DEFAULT_AUTO_PAGE_TURN_INTERVAL_S;

  if (!epub) {
    return;
  }

  const auto data = loadBookReaderSettingsFile(epub->getCachePath());
  bookHasCustomReaderSettings = data.hasCustomReaderSettings;
  bookHasAutoPageTurnInterval = data.hasAutoPageTurnInterval;
  bookHasRenderModeOverride = data.hasRenderModeOverride;
  lastAutoPageTurnIntervalSeconds =
      data.hasAutoPageTurnInterval ? data.autoPageTurnSeconds : DEFAULT_AUTO_PAGE_TURN_INTERVAL_S;
  if (data.hasCustomReaderSettings) {
    applyReaderSettings(data.readerSettings);
  }
  SETTINGS.epubRenderMode = data.hasRenderModeOverride ? normalizeRenderModeRaw(data.renderMode)
                                                       : static_cast<uint8_t>(EpubRenderMode::CrossInkDefault);
  sdFontSystem.ensureLoaded(renderer);
}

void EpubReaderActivity::saveCurrentBookReaderSettings() {
  if (!epub) {
    return;
  }

  ReaderSettingsSnapshot snapshot;
  captureReaderSettings(snapshot);
  bookHasCustomReaderSettings = true;
  bookHasRenderModeOverride = true;
  saveBookReaderSettingsFile(epub->getCachePath(), bookHasAutoPageTurnInterval, lastAutoPageTurnIntervalSeconds,
                             bookHasCustomReaderSettings, bookHasRenderModeOverride, SETTINGS.epubRenderMode, snapshot);
}

void EpubReaderActivity::saveGlobalSettingsPreservingBookOverrides() {
  if (!restoreGlobalReaderSettingsOnExit) {
    SETTINGS.saveToFile();
    return;
  }

  ReaderSettingsSnapshot activeReaderSettings;
  captureReaderSettings(activeReaderSettings);
  applyReaderSettings(globalReaderSettingsBeforeBook);
  SETTINGS.saveToFile();
  applyReaderSettings(activeReaderSettings);
}

void EpubReaderActivity::beginGlobalSettingsEdit() {
  if (bookReaderSettingsSuspendedForGlobalEdit || !restoreGlobalReaderSettingsOnExit) {
    return;
  }
  captureReaderSettings(suspendedBookReaderSettings);
  applyReaderSettings(globalReaderSettingsBeforeBook);
  bookReaderSettingsSuspendedForGlobalEdit = true;
}

void EpubReaderActivity::endGlobalSettingsEdit() {
  if (!bookReaderSettingsSuspendedForGlobalEdit) {
    return;
  }
  applyReaderSettings(suspendedBookReaderSettings);
  bookReaderSettingsSuspendedForGlobalEdit = false;
}

void EpubReaderActivity::saveReaderOptionsForBook(void* ctx) {
  if (!ctx) {
    return;
  }
  static_cast<EpubReaderActivity*>(ctx)->saveCurrentBookReaderSettings();
}

void EpubReaderActivity::saveGlobalSettingsForBookReader(void* ctx) {
  if (!ctx) {
    return;
  }
  static_cast<EpubReaderActivity*>(ctx)->saveGlobalSettingsPreservingBookOverrides();
}

void EpubReaderActivity::beginGlobalSettingsEditForBookReader(void* ctx) {
  if (!ctx) {
    return;
  }
  static_cast<EpubReaderActivity*>(ctx)->beginGlobalSettingsEdit();
}

void EpubReaderActivity::endGlobalSettingsEditForBookReader(void* ctx) {
  if (!ctx) {
    return;
  }
  static_cast<EpubReaderActivity*>(ctx)->endGlobalSettingsEdit();
}

void EpubReaderActivity::onEnter() {
  Activity::onEnter();
  timingOnEnterMs = millis();
  pageLoadRetryCount = 0;
  firstRenderCompleted.store(false, std::memory_order_relaxed);
  deferredOnEnterPending = false;
  pendingRelayoutPreview = false;
  pendingRelayoutPreviewFromBack = false;
  relayoutPreviewUserMoved = false;
  activeRelayoutPreview = false;
  activeChapterPreview = false;
  relayoutBuildWorkRequested.store(false, std::memory_order_relaxed);
  relayoutBuildCancelRequested.store(false, std::memory_order_relaxed);
  relayoutBuildActive.store(false, std::memory_order_relaxed);
  relayoutBuildLowHeapAbort.store(false, std::memory_order_relaxed);
  relayoutBuildRetired.store(false, std::memory_order_relaxed);
  relayoutBuildSafeModePending.store(false, std::memory_order_relaxed);
  relayoutBuildEligibleAfterMs.store(0, std::memory_order_relaxed);
  relayoutBuildAttempts.store(0, std::memory_order_relaxed);
  deferredPageTurnDirection = 0;
  nextChapterPreindexWorkRequested.store(false, std::memory_order_relaxed);
  nextChapterPreindexCancelRequested.store(false, std::memory_order_relaxed);
  nextChapterPreindexActive.store(false, std::memory_order_relaxed);
  nextChapterPreindexLowHeapAbort.store(false, std::memory_order_relaxed);
  nextChapterPreindexCandidateSpine.store(-1, std::memory_order_relaxed);
  nextChapterPreindexCandidateKey.store(0, std::memory_order_relaxed);
  nextChapterPreindexEligibleAfterMs.store(0, std::memory_order_relaxed);
  nextChapterPreindexAttempts.store(0, std::memory_order_relaxed);
  nextChapterPreindexFinishedSpine.store(-1, std::memory_order_relaxed);
  nextChapterPreindexFinishedKey.store(0, std::memory_order_relaxed);
  savedItemsLoaded = false;

  if (!epub) {
    return;
  }

  captureGlobalReaderSettings();
  epub->setupCacheDir();
  loadBookReaderSettings();
  sdFontSystem.ensureLoaded(renderer);

  // Configure screen orientation based on settings
  // NOTE: This affects layout math and must be applied before any render calls.
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

  // Activate reader-specific front button mapping (if configured).
  mappedInput.setReaderMode(true);

  if (APP_STATE.pendingClippingIndex != UINT16_MAX) {
    BOOKMARKS.loadForBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), "epub");
    CLIPPINGS.loadForBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), "epub");
    savedItemsLoaded = true;
  }

  if (APP_STATE.pendingBookmarkSpine != UINT16_MAX && APP_STATE.pendingBookmarkProgress >= 0.0f) {
    // Resume from a bookmark selected on the Home screen
    currentSpineIndex = APP_STATE.pendingBookmarkSpine;
    pendingSpineProgress = APP_STATE.pendingBookmarkProgress;
    pendingParagraphIndex = APP_STATE.pendingBookmarkParagraphIndex;
    pendingClippingIndex = APP_STATE.pendingClippingIndex;
    pendingPercentJump = true;
    cachedSpineIndex = currentSpineIndex;

    // Clear the pending jump
    APP_STATE.pendingBookmarkSpine = UINT16_MAX;
    APP_STATE.pendingBookmarkProgress = -1.0f;
    APP_STATE.pendingBookmarkParagraphIndex = UINT16_MAX;
    APP_STATE.pendingClippingIndex = UINT16_MAX;
    APP_STATE.saveToFile();
  } else {
    EpubReaderUtils::Progress progress;
    if (EpubReaderUtils::loadProgress(*epub, progress)) {
      currentSpineIndex = progress.spineIndex;
      nextPageNumber = progress.pageNumber;
      cachedSpineIndex = currentSpineIndex;
      if (progress.hasPageCount) {
        cachedChapterPageNumber = progress.pageNumber;
        cachedChapterTotalPageCount = progress.pageCount;
      }
      LOG_DBG("ERS", "Loaded cache: %d, %d", currentSpineIndex, nextPageNumber);
    }
  }
  // We may want a better condition to detect if we are opening for the first time.
  // This will trigger if the book is re-opened at Chapter 0.
  if (currentSpineIndex == 0 && !pendingPercentJump) {
    int textSpineIndex = epub->getSpineIndexForTextReference();
    if (textSpineIndex != 0) {
      currentSpineIndex = textSpineIndex;
      LOG_DBG("ERS", "Opened for first time, navigating to text reference at index %d", textSpineIndex);
    }
  }

  // Start timing immediately, but defer non-render-critical SD reads and writes
  // until the first page is visible.
  armReadingPaceWarmup("reader_open");
  sessionReadingSeconds = 0;
  sessionScreenPages = 0;
  readingStatsCommitted = false;
  fastHomeExitRequested = false;
  hasSessionStartLocalDateTime = getCurrentLocalReadingStatsDateTime(sessionStartLocalDateTime);
  deferredOnEnterPending = true;

  // Trigger first update
  requestUpdate();
}

void EpubReaderActivity::runDeferredOnEnter(const bool loadSavedItems) {
  if (!deferredOnEnterPending) return;
  deferredOnEnterPending = false;
  if (!epub) return;

  const unsigned long startedAt = millis();
  stats = BookReadingStats::load(epub->getCachePath());
  if (const uint32_t words = epub->getTotalWords(); words > 0) {
    stats.totalWordCount = words;
  }
#if defined(ENABLE_SERIAL_LOG) && LOG_LEVEL >= 2
  const uint32_t cumulativeAvgSeconds =
      stats.totalPagesTurned > 0 ? stats.totalReadingSeconds / stats.totalPagesTurned : 0;
  LOG_DBG("ERS",
          "Reading stats loaded after first paint: totalReadingSeconds=%lu totalPagesTurned=%lu avg=%u samples=%u "
          "cumulativeAvg=%lu",
          static_cast<unsigned long>(stats.totalReadingSeconds), static_cast<unsigned long>(stats.totalPagesTurned),
          stats.avgSecondsPerForwardPage, stats.paceSampleCount, static_cast<unsigned long>(cumulativeAvgSeconds));
#endif
  globalStats = GlobalReadingStats::load();
  initializeCompletionPromptTrigger();

  if (loadSavedItems && !savedItemsLoaded) {
    BOOKMARKS.loadForBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), "epub");
    CLIPPINGS.loadForBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), "epub");
    savedItemsLoaded = true;
  }

  // The reader that actually reached first paint is the user's active book.
  // Refresh APP_STATE here so Home cannot cling to an older recent. Skip the
  // file rewrite only when DISK already names this book — goToReader has
  // already set the RAM value, so comparing openEpubPath itself would elide
  // the deliberately deferred persist on every open.
  // Promotion is gated on first paint: an exit or sleep before the page ever
  // rendered (accidental or wrong-book open) must not crown this book in
  // state.json/last_active/recents — that write-through is how a one-second
  // misopen used to become the persistent stale "current book".
  if (firstRenderCompleted.load(std::memory_order_acquire)) {
    const std::string epubPath = epub->getPath();
    bool appStateChanged = false;
    {
      std::lock_guard<std::mutex> lock(APP_STATE.getMutex());
      APP_STATE.openEpubPath = epubPath;
      appStateChanged = APP_STATE.persistedOpenEpubPath != epubPath;
    }
    if (appStateChanged) {
      APP_STATE.saveToFile();
    }
    CurrentBookStats::markLastActive(epubPath);
    RECENT_BOOKS.addOrUpdateBook(epubPath, epub->getTitle(), epub->getAuthor(), epub->getThumbBmpPath());
  } else {
    // Restore the RAM value goToReader set optimistically; otherwise the next
    // unrelated APP_STATE save (sleep, onExit) would persist the unread book.
    std::lock_guard<std::mutex> lock(APP_STATE.getMutex());
    APP_STATE.openEpubPath = APP_STATE.persistedOpenEpubPath;
    LOG_DBG("ERS", "Skipping active-book promotion: first paint never completed");
  }
  LOG_DBG("ERS", "Deferred reader startup completed in %lums", millis() - startedAt);
}

void EpubReaderActivity::onExit() {
  relayoutBuildCancelRequested.store(true, std::memory_order_release);
  relayoutBuildWorkRequested.store(false, std::memory_order_release);
  relayoutBuildSafeModePending.store(false, std::memory_order_release);
  deferredPageTurnDirection = 0;
  nextChapterPreindexCancelRequested.store(true, std::memory_order_release);
  nextChapterPreindexWorkRequested.store(false, std::memory_order_release);
  Activity::onExit();

  // A timeout or immediate sleep can race the first render. Load the persisted
  // counters before committing so an early exit can never overwrite history.
  runDeferredOnEnter(false);

  // Deactivate reader-specific front button mapping.
  mappedInput.setReaderMode(false);

  // Reset orientation back to portrait for the rest of the UI
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  if (APP_STATE.readerActivityLoadCount != 0) {
    APP_STATE.readerActivityLoadCount = 0;
    APP_STATE.saveToFile();
  }

  const unsigned long timingCommitStartMs = millis();
  commitReadingStats();
  const unsigned long timingCommitMs = millis() - timingCommitStartMs;
  if (!fastHomeExitRequested) {
    // Repair15 phase-1: transition breadcrumbs (measurement only).
    FsFile timingFile;
    if (Storage.openFileForWrite("ERS", DUET_STATE_ROOT_PATH "/reader_timing.txt", timingFile)) {
      char buf[700];
      const uint32_t laterAvg = timingLaterRenderCount ? timingLaterRenderTotalMs / timingLaterRenderCount : 0;
      const int n = snprintf(
          buf, sizeof(buf),
          "openToPaint=%lums renders=%u early=%u,%u,%u,%u,%u,%ums laterAvg=%lums "
          "statsCommit=%lums sectionBuilds=%u lastBuild=%lums maxBuild=%lums "
          "preindexDone=%u preindexCancel=%u preindexSkipHeap=%u preindexFail=%u "
          "lastPreindex=%lums maxPreindex=%lums "
          "relayoutPreview=%u lastPreview=%lums maxPreview=%lums "
          "relayoutFull=%u relayoutCancel=%u relayoutHit=%u lastRelayout=%lums maxRelayout=%lums "
          "chapterPreview=%u lastChapterPreview=%lums maxChapterPreview=%lums "
          "chapterFull=%u chapterCancel=%u chapterHit=%u lastChapterFull=%lums maxChapterFull=%lums\n",
          timingFirstPaintMs > timingOnEnterMs ? timingFirstPaintMs - timingOnEnterMs : 0, timingRenderCount,
          timingEarlyRenderMs[0], timingEarlyRenderMs[1], timingEarlyRenderMs[2], timingEarlyRenderMs[3],
          timingEarlyRenderMs[4], timingEarlyRenderMs[5], static_cast<unsigned long>(laterAvg), timingCommitMs,
          timingSectionBuildCount, static_cast<unsigned long>(timingLastSectionBuildMs),
          static_cast<unsigned long>(timingMaxSectionBuildMs), timingPreindexCompleteCount, timingPreindexCancelCount,
          timingPreindexSkipHeapCount, timingPreindexFailCount, static_cast<unsigned long>(timingLastPreindexMs),
          static_cast<unsigned long>(timingMaxPreindexMs), timingRelayoutPreviewCount,
          static_cast<unsigned long>(timingLastRelayoutPreviewMs),
          static_cast<unsigned long>(timingMaxRelayoutPreviewMs), timingRelayoutCompletionCount,
          timingRelayoutCompletionCancelCount, timingRelayoutCacheHitCount,
          static_cast<unsigned long>(timingLastRelayoutCompletionMs),
          static_cast<unsigned long>(timingMaxRelayoutCompletionMs), timingChapterPreviewCount,
          static_cast<unsigned long>(timingLastChapterPreviewMs), static_cast<unsigned long>(timingMaxChapterPreviewMs),
          timingChapterCompletionCount, timingChapterCompletionCancelCount, timingChapterCacheHitCount,
          static_cast<unsigned long>(timingLastChapterCompletionMs),
          static_cast<unsigned long>(timingMaxChapterCompletionMs));
      if (n > 0) timingFile.write(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(n));
      timingFile.close();
    }
  }

  // The activity is tearing down: the preview guard exists only to keep
  // preview page renders from clobbering progress, and would otherwise
  // swallow the exit-time writes below (losing debounced turns on sleep).
  if (activeRelayoutPreview && section) {
    section->clearCache();
  }
  activeFootnotePreview = false;
  activeRelayoutPreview = false;
  activeChapterPreview = false;
  pendingRelayoutPreviewFromBack = false;
  relayoutPreviewUserMoved = false;

  // Leaving mid-footnote loses the in-RAM return stack on deep sleep; persist the
  // pre-footnote position so the book reopens at the link origin, not the footnote.
  // Otherwise flush any debounced position so exit/sleep never loses page turns.
  if (footnoteDepth > 0 && epub) {
    const SavedPosition& origin = savedPositions[0];
    saveProgress(origin.spineIndex, origin.pageNumber, 0);
  } else {
    flushPendingProgress();
  }

  if (savedItemsLoaded) {
    BOOKMARKS.unload();
    CLIPPINGS.unload();
    savedItemsLoaded = false;
  }
  section.reset();

  if (pendingReadFolderMove && epub) {
    const std::string srcPath = epub->getPath();
    const std::string oldCachePath = epub->getCachePath();
    const std::string title = epub->getTitle();
    const std::string author = epub->getAuthor();
    const std::string dstPath = BookMoveUtils::buildReadFolderDestination(srcPath);
    epub.reset();  // release the Epub (and any open handles) before renaming on the SD card
    moveFinishedBookToReadFolder(srcPath, dstPath, oldCachePath, title, author);
  } else {
    epub.reset();
  }

  restoreGlobalReaderSettings();
}

void EpubReaderActivity::commitReadingStats() {
  if (readingStatsCommitted || !epub || !shouldTrackReadingStatsForPath(epub->getPath())) return;
  readingStatsCommitted = true;

  recordCurrentPageReadingTime("reader_exit");
  const uint32_t elapsedSecs = sessionReadingSeconds;
  const bool qualifiesAsSession = sessionScreenPages > 0;
  if (qualifiesAsSession) {
    if (stats.sessionCount < UINT16_MAX) stats.sessionCount++;
    if (globalStats.totalSessions < UINT32_MAX) globalStats.totalSessions++;
    stats.countedSessionSeconds =
        stats.countedSessionSeconds > UINT32_MAX - elapsedSecs ? UINT32_MAX : stats.countedSessionSeconds + elapsedSecs;
    globalStats.countedSessionSeconds = globalStats.countedSessionSeconds > UINT32_MAX - elapsedSecs
                                            ? UINT32_MAX
                                            : globalStats.countedSessionSeconds + elapsedSecs;
    stats.latestSessionReadingSeconds = elapsedSecs;
    stats.latestSessionScreenPages = sessionScreenPages;
    if (hasSessionStartLocalDateTime) {
      stats.latestSessionDayIndex = readingStatsDayIndex(sessionStartLocalDateTime.date);
      stats.latestSessionStartMinute =
          static_cast<uint16_t>(sessionStartLocalDateTime.hour * 60u + sessionStartLocalDateTime.minute);
    }
  }
  if (elapsedSecs > 0) {
    stats.totalReadingSeconds =
        stats.totalReadingSeconds > UINT32_MAX - elapsedSecs ? UINT32_MAX : stats.totalReadingSeconds + elapsedSecs;
    globalStats.totalReadingSeconds = globalStats.totalReadingSeconds > UINT32_MAX - elapsedSecs
                                          ? UINT32_MAX
                                          : globalStats.totalReadingSeconds + elapsedSecs;
    if (hasSessionStartLocalDateTime) {
      stats.recordReadingSpan(sessionStartLocalDateTime, elapsedSecs);
      globalStats.recordReadingSpan(sessionStartLocalDateTime, elapsedSecs);
    }
    if (elapsedSecs >= 120 && !stats.startDateManual && !stats.startDate.isValid() && hasSessionStartLocalDateTime) {
      stats.startDate = sessionStartLocalDateTime.date;
    }
  }
  if (epub) {
    recoverStoredPaceFromSession("reader_exit");
    refreshCachedTimeLeftEstimate();
    stats.save(epub->getCachePath());
  }
  globalStats.save();
  if (hasSessionStartLocalDateTime && (elapsedSecs > 0 || qualifiesAsSession)) {
    ReadingJournal::recordSession(sessionStartLocalDateTime, elapsedSecs, sessionScreenPages);
    if (epub) {
      ReadingLedger::recordReadingSpan(sessionStartLocalDateTime, elapsedSecs, sessionScreenPages, epub->getCachePath(),
                                       epub->getTitle());
      SessionLog::append(sessionStartLocalDateTime, elapsedSecs, sessionScreenPages, epub->getCachePath(),
                         epub->getTitle());
    }
  }
}

void EpubReaderActivity::goHomeAfterReading() {
  if (SETTINGS.showStatsAfterReading == 0 || !epub || !shouldTrackReadingStatsForPath(epub->getPath())) {
    fastHomeExitRequested = true;
    onGoHome();
    return;
  }

  runDeferredOnEnter();
  commitReadingStats();
  if (!epub || (sessionReadingSeconds == 0 && sessionScreenPages == 0)) {
    fastHomeExitRequested = true;
    onGoHome();
    return;
  }

  uint32_t timeLeftSeconds = 0;
  const bool hasTimeLeft = estimateTimeLeftSeconds(true, timeLeftSeconds);
  const bool hasSyncedStats = GlobalReadingStats::hasSyncedStats();
  pauseReadingPaceTimer("post_reading_stats");
  if (hasSyncedStats) {
    startActivityForResult(
        std::make_unique<BookStatsActivity>(
            renderer, mappedInput, epub->getTitle(), epub->getCachePath(), stats, getCurrentBookProgressPercent(),
            hasTimeLeft, timeLeftSeconds, globalStats, GlobalReadingStats::loadAggregated(globalStats), true,
            ReadingSessionSnapshot{}, std::max(epub->getTotalWords(), stats.totalWordCount)),
        [this](const ActivityResult&) {
          fastHomeExitRequested = true;
          onGoHome();
        });
  } else {
    startActivityForResult(std::make_unique<BookStatsActivity>(
                               renderer, mappedInput, epub->getTitle(), epub->getCachePath(), stats,
                               getCurrentBookProgressPercent(), hasTimeLeft, timeLeftSeconds, globalStats, true,
                               ReadingSessionSnapshot{}, std::max(epub->getTotalWords(), stats.totalWordCount)),
                           [this](const ActivityResult&) {
                             fastHomeExitRequested = true;
                             onGoHome();
                           });
  }
}

void EpubReaderActivity::openReaderQuickMenu() {
  int currentPage = 0;
  int totalPages = 0;
  float bookProgress = 0.0f;
  {
    RenderLock lock(*this);
    currentPage = section && !activeRelayoutPreview ? section->currentPage + 1 : 0;
    totalPages = section && !activeRelayoutPreview ? section->pageCount : 0;
    bookProgress = getCurrentBookProgressPercent();
  }
  const int bookProgressPercent = clampPercent(static_cast<int>(bookProgress + 0.5f));

  pauseReadingPaceTimer("reader_quick_menu");
  startActivityForResult(std::make_unique<EpubReaderQuickMenuActivity>(
                             renderer, mappedInput, epub->getTitle(), currentPage, totalPages, bookProgressPercent,
                             automaticPageTurnActive, getAutoPageTurnIntervalSeconds()),
                         [this](const ActivityResult& result) {
                           const auto* menu = std::get_if<MenuResult>(&result.data);
                           if (menu == nullptr) {
                             resumeReadingPaceTimer("reader_quick_menu_return");
                             requestUpdate();
                             return;
                           }

                           applyOrientation(menu->orientation);
                           const auto action = menu->action >= 0
                                                   ? static_cast<EpubReaderMenuActivity::MenuAction>(menu->action)
                                                   : EpubReaderMenuActivity::MenuAction::OPEN_FULL_MENU;
                           if (!result.isCancelled && action == EpubReaderMenuActivity::MenuAction::OPEN_FULL_MENU) {
                             openReaderMenu(menu->settingsChanged);
                             return;
                           }

                           if (menu->settingsChanged) {
                             beginInteractiveRelayout();
                           }

                           resumeReadingPaceTimer("reader_quick_menu_return");
                           if (!result.isCancelled && menu->action >= 0) {
                             onReaderMenuConfirm(action);
                           } else {
                             requestUpdate();
                           }
                         });
}

void EpubReaderActivity::openReaderMenu(const bool initialSettingsChanged) {
  const uint8_t bionicBefore = BionicReadingMode::normalize(SETTINGS.bionicReadingEnabled);
  int currentPage = 0;
  int totalPages = 0;
  float bookProgress = 0.0f;
  uint16_t bmSpine;
  float bmProgress = 0.0f;
  int bookmarkPageCount = 1;
  bool isBookCompleted;
  bool previewActive = false;
  {
    // Serialize EPUB metadata/file access with the render task.
    RenderLock lock(*this);
    previewActive = activeFootnotePreview;
    currentPage = section ? section->currentPage + 1 : 0;
    totalPages = section ? section->pageCount : 0;
    bmSpine = static_cast<uint16_t>(currentSpineIndex);
    bmProgress = (section && totalPages > 0) ? static_cast<float>(section->currentPage) / totalPages : 0.0f;
    bookmarkPageCount = totalPages > 0 ? totalPages : 1;
    isBookCompleted = stats.isCompleted;
    bookProgress = getCurrentBookProgressPercent();
  }
  const int bookProgressPercent = clampPercent(static_cast<int>(bookProgress + 0.5f));

  pauseReadingPaceTimer("reader_menu");
  startActivityForResult(
      std::make_unique<EpubReaderMenuActivity>(
          renderer, mappedInput, epub->getTitle(), currentPage, totalPages, bookProgressPercent, SETTINGS.orientation,
          !previewActive && !currentPageFootnotes.empty(), !BOOKMARKS.getBookmarks().empty(), CLIPPINGS.hasClippings(),
          !previewActive && BOOKMARKS.hasBookmarkForPage(bmSpine, bmProgress, bookmarkPageCount), isBookCompleted,
          automaticPageTurnActive, getAutoPageTurnIntervalSeconds(),
          SETTINGS.statusBarTimeLeft != CrossPointSettings::STATUS_BAR_TIME_LEFT::TIME_LEFT_HIDE,
          saveReaderOptionsForBook, this, saveGlobalSettingsForBookReader, this, beginGlobalSettingsEditForBookReader,
          this, !previewActive && epub && epub->hasStablePageNumbers(), endGlobalSettingsEditForBookReader, this,
          initialSettingsChanged),
      [this, bionicBefore](const ActivityResult& result) {
        if (bionicBefore != BionicReadingMode::normalize(SETTINGS.bionicReadingEnabled)) {
          showBionicFeedback(SETTINGS.bionicReadingEnabled);
        }
        if (const auto* clipping = std::get_if<ClippingJumpResult>(&result.data)) {
          applyOrientation(clipping->orientation);
          if (clipping->settingsChanged) {
            beginInteractiveRelayout(false);
          }
          handleClippingJump(*clipping);
          requestUpdate();
          return;
        }

        // Always apply orientation change even if the menu was cancelled
        const auto* menu = std::get_if<MenuResult>(&result.data);
        if (menu == nullptr) {
          resumeReadingPaceTimer("reader_menu_return");
          requestUpdate();
          return;
        }
        applyOrientation(menu->orientation);
        if (menu->settingsChanged) {
          beginInteractiveRelayout();
        }
        resumeReadingPaceTimer("reader_menu_return");
        if (!result.isCancelled) {
          onReaderMenuConfirm(static_cast<EpubReaderMenuActivity::MenuAction>(menu->action));
        }
      });
}

void EpubReaderActivity::loop() {
  if (!epub) {
    // Should never happen
    finish();
    return;
  }

  const bool readerInputThisFrame = mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased();
  if (readerInputThisFrame) {
    cancelRelayoutBuildForInput();
    cancelNextChapterPreindexForInput();
  }

  if (deferredOnEnterPending && firstRenderCompleted.load(std::memory_order_acquire)) {
    runDeferredOnEnter();
  }

  // Font-selection screens persist the chosen family before returning to the
  // reader. Detect the resulting ID change here as a final guard so every
  // family change uses the same visible-page-first relayout path, even if an
  // intermediate menu does not propagate its settingsChanged flag.
  if (!readerInputThisFrame && section && !activeFootnotePreview && activeSectionFontId != 0 &&
      activeSectionFontId != SETTINGS.getReaderFontId()) {
    beginInteractiveRelayout();
    return;
  }

  if (!readerInputThisFrame && maybeScheduleRelayoutBuild()) {
    return;
  }

  if (!activeRelayoutPreview && !readerInputThisFrame && maybeScheduleNextChapterPreindex()) {
    return;
  }

  if (completionPromptQueued) {
    completionPromptQueued = false;
    completionPromptShown = true;
    pauseReadingPaceTimer("completion_prompt");
    startActivityForResult(
        std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_MARK_FINISHED_PROMPT_TITLE),
                                               tr(STR_MARK_FINISHED_PROMPT_BODY)),
        [this](const ActivityResult& result) {
          resumeReadingPaceTimer("completion_prompt_return");
          if (!result.isCancelled) {
            setBookCompleted(true);
            showCompletedFeedback(true);
          }
          requestUpdate();
        });
    return;
  }

  if (pendingBookmarkFeedback) {
    const bool timedOut = (millis() - bookmarkFeedbackShowTime) >= 1000UL;
    const bool navPressed = mappedInput.wasReleased(MappedInputManager::Button::Left) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Right) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Down);
    if (timedOut || navPressed) {
      pendingBookmarkFeedback = false;
      requestUpdate();
      return;
    }
  }

  if (pendingCompletedFeedback) {
    const bool timedOut = (millis() - completedFeedbackShowTime) >= 1000UL;
    const bool navPressed = mappedInput.wasReleased(MappedInputManager::Button::Left) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Right) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Down);
    if (timedOut || navPressed) {
      pendingCompletedFeedback = false;
      requestUpdate();
      return;
    }
  }
  if (pendingTiltPageTurnFeedback) {
    const bool timedOut = (millis() - tiltPageTurnFeedbackShowTime) >= 1000UL;
    const bool navPressed = mappedInput.wasReleased(MappedInputManager::Button::Left) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Right) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Down);
    if (timedOut || navPressed) {
      pendingTiltPageTurnFeedback = false;
      requestUpdate();
      return;
    }
  }
  if (pendingBionicFeedback) {
    const bool timedOut = (millis() - bionicFeedbackShowTime) >= 1000UL;
    const bool navPressed = mappedInput.wasReleased(MappedInputManager::Button::Left) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Right) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Down);
    if (timedOut || navPressed) {
      pendingBionicFeedback = false;
      requestUpdate();
      return;
    }
  }
  if ((pendingRenderModeToast || pendingSafeModeToast) &&
      (millis() - renderModeToastShowTime) >= RENDER_MODE_TOAST_MS) {
    if (renderModeToastRegionSaved) {
      if (RenderLock::peek()) {
        return;
      }
      RenderLock lock(*this);
      restoreRenderModeToastRegion();
    }
    pendingRenderModeToast = false;
    pendingSafeModeToast = false;
    return;
  }

  // End-of-Book screen reached (currentSpineIndex == spine count) means the book is
  // finished. Two independent finished-book features key off this same condition.
  const bool atEndOfBook = currentSpineIndex > 0 && currentSpineIndex >= epub->getSpineItemsCount();

  // Drop this book from the Recent Books list; if the reader then pages back into the book,
  // re-add it. So removal only sticks if the reader leaves while still on the End-of-Book
  // screen. Acts only on the transition (guarded by recentsEntryRemoved) — no per-frame writes.
  if (SETTINGS.removeReadBooksFromRecents) {
    if (atEndOfBook && !recentsEntryRemoved) {
      recentsEntryRemoved = RECENT_BOOKS.removeByPath(epub->getPath());
    } else if (!atEndOfBook && recentsEntryRemoved) {
      RECENT_BOOKS.addOrUpdateBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), epub->getThumbBmpPath());
      recentsEntryRemoved = false;
    }
  }

  // Arm the move here so any exit path relocates the book into /Read/.
  // setBookCompleted() also arms this when the user marks a book finished before
  // the End-of-Book screen.
  if (atEndOfBook) {
    pendingReadFolderMove = SETTINGS.moveFinishedToReadFolder && !isInReadFolder(epub->getPath());
  } else if (!stats.isCompleted) {
    pendingReadFolderMove = false;
  }

  if (automaticPageTurnActive) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      automaticPageTurnActive = false;
      // updates chapter title space to indicate page turn disabled
      requestUpdate();
      return;
    }

    if (!section) {
      requestUpdate();
      return;
    }

    // Skips page turn if renderingMutex is busy
    if (RenderLock::peek()) {
      lastPageTurnTime = millis();
      return;
    }

    if ((millis() - lastPageTurnTime) >= pageTurnDuration) {
      pageTurn(true, "auto");
      return;
    }
  }

  // Long-press Confirm: execute the configured reader action without opening the menu
  if (longPressMenuHandled) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        !mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      longPressMenuHandled = false;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (SETTINGS.longPressMenuAction != CrossPointSettings::LONG_MENU_OFF &&
        mappedInput.getHeldTime() >= longPressMenuMs) {
      const auto action = static_cast<CrossPointSettings::LONG_PRESS_MENU_ACTION>(SETTINGS.longPressMenuAction);
      suppressConfirmShortcutRelease(action);
      executeReaderQuickAction(action);
      return;
    }
  }

  // While the end screen suggestion menu is showing it owns Confirm/Back/navigation
  // input. Anything it doesn't handle (e.g. long-press Back) falls through to the
  // regular handlers below; page turns are absorbed by the end-of-book block.
  if (atEndOfBook && endOfBookOptions.menuActive()) {
    std::string openPath;
    switch (endOfBookOptions.handleMenuInput(mappedInput, &openPath)) {
      case EndOfBookOptions::Action::OpenBook:
        activityManager.goToReader(openPath);
        return;
      case EndOfBookOptions::Action::GoHome:
        goHomeAfterReading();
        return;
      case EndOfBookOptions::Action::LastPage:
        currentSpineIndex = std::max(epub->getSpineItemsCount() - 1, 0);
        nextPageNumber = 0;
        pendingPageJump = std::numeric_limits<uint16_t>::max();
        requestUpdate();
        return;
      case EndOfBookOptions::Action::Redraw:
        requestUpdate();
        return;
      case EndOfBookOptions::Action::None:
        break;
    }
  }
  if (SETTINGS.longPressMenuAction != CrossPointSettings::LONG_MENU_OFF &&
      mappedInput.isPressed(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() >= longPressMenuMs) {
    longPressMenuHandled = true;
    const auto action = static_cast<CrossPointSettings::LONG_PRESS_MENU_ACTION>(SETTINGS.longPressMenuAction);
    suppressConfirmShortcutRelease(action);
    executeReaderQuickAction(action);
    return;
  }

  // Enter the quick reader overlay; its More row opens the complete menu.
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openReaderQuickMenu();
  }

  if (longPressBackHandled) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        !mappedInput.isPressed(MappedInputManager::Button::Back)) {
      longPressBackHandled = false;
    }
    return;
  }

  if (!longPressBackHandled && mappedInput.isPressed(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() >= ReaderUtils::GO_HOME_MS) {
    longPressBackHandled = true;
    mappedInput.suppressNextBackRelease();
    executeReaderQuickAction(static_cast<CrossPointSettings::LONG_PRESS_MENU_ACTION>(SETTINGS.longPressBackAction));
    return;
  }

  // Short press BACK goes directly to home (or restores position if viewing footnote)
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() < ReaderUtils::GO_HOME_MS) {
    if (footnoteDepth > 0) {
      restoreSavedPosition();
      return;
    }
    goHomeAfterReading();
    return;
  }

  // Side button long-press actions use raw Up/Down so the direction stays
  // physical regardless of the Prev/Next side layout setting.
  const bool sideLongPressChangesFont =
      SETTINGS.sideButtonLongPress == CrossPointSettings::SIDE_LONG_PRESS::SIDE_LONG_FONT_SIZE;
  const bool sideLongPressChangesOrientation =
      SETTINGS.sideButtonLongPress == CrossPointSettings::SIDE_LONG_PRESS::SIDE_LONG_ORIENTATION_CHANGE;
  if (sideLongPressChangesFont || sideLongPressChangesOrientation) {
    const bool topReleased = mappedInput.wasReleased(MappedInputManager::Button::Up);
    const bool bottomReleased = mappedInput.wasReleased(MappedInputManager::Button::Down);
    if (sideButtonLongPressHandled && (topReleased || bottomReleased)) {
      sideButtonLongPressHandled = false;
      return;
    }

    const bool longPressReady = mappedInput.getHeldTime() > ReaderUtils::SKIP_HOLD_MS;
    const bool topLongPressed =
        longPressReady && (mappedInput.isPressed(MappedInputManager::Button::Up) || topReleased);
    const bool bottomLongPressed =
        longPressReady && (mappedInput.isPressed(MappedInputManager::Button::Down) || bottomReleased);

    if (!sideButtonLongPressHandled && topLongPressed) {
      sideButtonLongPressHandled = !topReleased;
      if (sideLongPressChangesFont) {
        // On X3 the side buttons are physically left/right; make right larger
        // and left smaller even though the raw hardware names are Up/Down.
        if (sdFontSystem.changeReaderFontSize(/*larger=*/false)) {
          reindexCurrentSection();
        }
      } else {
        applyOrientation(ReaderUtils::rotatedOrientation(SETTINGS.orientation, /*clockwise=*/false));
        requestUpdate();
      }
      return;
    }
    if (!sideButtonLongPressHandled && bottomLongPressed) {
      sideButtonLongPressHandled = !bottomReleased;
      if (sideLongPressChangesFont) {
        if (sdFontSystem.changeReaderFontSize(/*larger=*/true)) {
          reindexCurrentSection();
        }
      } else {
        applyOrientation(ReaderUtils::rotatedOrientation(SETTINGS.orientation, /*clockwise=*/true));
        requestUpdate();
      }
      return;
    }
  }

  if (consumeLongPowerButtonRelease()) {
    return;
  }
  if (executeShortPowerButtonAction()) {
    return;
  }
  if (executeLongPowerButtonAction()) {
    return;
  }

  const bool frontLongPressChangesFont = SETTINGS.longPressButtonBehavior == CrossPointSettings::FONT_SIZE_CHANGE;
  const bool frontLongPressAction = SETTINGS.longPressButtonBehavior == CrossPointSettings::CHAPTER_SKIP ||
                                    SETTINGS.longPressButtonBehavior == CrossPointSettings::ORIENTATION_CHANGE ||
                                    frontLongPressChangesFont;
  if (frontLongPressAction) {
    const bool leftReleased = mappedInput.wasReleased(MappedInputManager::Button::Left);
    const bool rightReleased = mappedInput.wasReleased(MappedInputManager::Button::Right);
    if (frontButtonLongPressHandled && (leftReleased || rightReleased)) {
      frontButtonLongPressHandled = false;
      return;
    }

    const bool longPressReady = mappedInput.getHeldTime() > ReaderUtils::SKIP_HOLD_MS;
    const bool prevLongPressed = longPressReady && mappedInput.isPressed(MappedInputManager::Button::Left);
    const bool nextLongPressed = longPressReady && mappedInput.isPressed(MappedInputManager::Button::Right);
    if (!frontButtonLongPressHandled && (prevLongPressed || nextLongPressed)) {
      frontButtonLongPressHandled = true;
      if (SETTINGS.longPressButtonBehavior == CrossPointSettings::CHAPTER_SKIP) {
        if (currentSpineIndex > 0 && currentSpineIndex >= epub->getSpineItemsCount()) {
          if (nextLongPressed) {
            goHomeAfterReading();
          } else {
            currentSpineIndex = epub->getSpineItemsCount() - 1;
            nextPageNumber = 0;
            pendingPageJump = std::numeric_limits<uint16_t>::max();
            requestUpdate();
          }
          return;
        }

        {
          RenderLock lock(*this);
          nextPageNumber = 0;
          currentSpineIndex = nextLongPressed ? currentSpineIndex + 1 : currentSpineIndex - 1;
          section.reset();
        }
        requestUpdate();
        return;
      }

      if (frontLongPressChangesFont) {
        if (sdFontSystem.changeReaderFontSize(/*larger=*/nextLongPressed)) {
          reindexCurrentSection();
        }
        return;
      }

      const uint8_t newOrientation = nextLongPressed
                                         ? ReaderUtils::rotatedOrientation(SETTINGS.orientation, /*clockwise=*/false)
                                         : ReaderUtils::rotatedOrientation(SETTINGS.orientation, /*clockwise=*/true);
      applyOrientation(newOrientation);
      requestUpdate();
      return;
    }
  }

  if (!readerInputThisFrame && consumeDeferredPageTurn()) {
    return;
  }

  auto [prevTriggered, nextTriggered, fromSideBtn, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);
  const bool powerReleased = mappedInput.wasReleased(MappedInputManager::Button::Power);
  const bool shortPowerTurn = mappedInput.wasShortPowerActionResolved() &&
                              mappedInput.getResolvedShortPowerAction() == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN;
  const bool releasedLongPowerTurn = SETTINGS.longPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN &&
                                     powerReleased &&
                                     mappedInput.getHeldTime() >= SETTINGS.getPowerButtonLongPressDuration();
  bool heldLongPowerTurn = false;
  if (SETTINGS.longPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN && consumeLongPowerButtonHold()) {
    nextTriggered = true;
    fromSideBtn = false;
    fromTilt = false;
    heldLongPowerTurn = true;
  }
  if (!prevTriggered && !nextTriggered) {
    return;
  }

  if (RenderLock::peek()) {
    deferPageTurn(!prevTriggered);
    return;
  }

  // At end of the book with no suggestion menu, forward button goes home and back
  // button returns to last page
  if (currentSpineIndex > 0 && currentSpineIndex >= epub->getSpineItemsCount()) {
    if (endOfBookOptions.menuActive()) {
      // Selection movement was handled above; absorb leftover page-turn triggers so
      // e.g. "previous" at the top of the list doesn't jump back into the book
      return;
    }
    if (nextTriggered) {
      goHomeAfterReading();
    } else {
      currentSpineIndex = epub->getSpineItemsCount() - 1;
      nextPageNumber = 0;
      pendingPageJump = std::numeric_limits<uint16_t>::max();
      requestUpdate();
    }
    return;
  }

  const bool longPress = !fromTilt && mappedInput.getHeldTime() > ReaderUtils::SKIP_HOLD_MS;
  const bool skipChapter =
      longPress &&
      (fromSideBtn ? SETTINGS.sideButtonLongPress == CrossPointSettings::SIDE_LONG_PRESS::SIDE_LONG_CHAPTER_SKIP
                   : SETTINGS.longPressButtonBehavior == CrossPointSettings::CHAPTER_SKIP);

  // Don't skip chapter after screenshot
  if (gpio.wasReleased(HalGPIO::BTN_POWER) && gpio.wasReleased(HalGPIO::BTN_DOWN)) {
    return;
  }

  if (skipChapter) {
    if (!nextTriggered && section && section->currentPage > 0) {
      section->currentPage = 0;
      requestUpdate();
      return;
    }

    // We don't want to delete the section mid-render, so grab the semaphore
    {
      RenderLock lock(*this);
      nextPageNumber = 0;
      if (nextTriggered) {
        currentSpineIndex++;
      } else if (currentSpineIndex > 0) {
        currentSpineIndex--;
      }
      section.reset();
    }
    requestUpdate();
    return;
  }

  if (longPress && !fromSideBtn && SETTINGS.longPressButtonBehavior == CrossPointSettings::ORIENTATION_CHANGE) {
    const uint8_t newOrientation =
        nextTriggered ? (SETTINGS.orientation - 1 + SETTINGS.ORIENTATION_COUNT) % SETTINGS.ORIENTATION_COUNT
                      : (SETTINGS.orientation + 1) % SETTINGS.ORIENTATION_COUNT;
    applyOrientation(newOrientation);
    requestUpdate();
    return;
  }

  // No current section, attempt to rerender the book
  if (!section) {
    requestUpdate();
    return;
  }

  const char* pageTurnSource = fromTilt ? "tilt" : (fromSideBtn ? "side" : "front");
  if (shortPowerTurn || releasedLongPowerTurn || heldLongPowerTurn) {
    pageTurnSource = "power";
  }
  if (prevTriggered) {
    if (!pageTurn(false, pageTurnSource)) deferPageTurn(false);
  } else {
    if (!pageTurn(true, pageTurnSource)) deferPageTurn(true);
  }
}

// Translate an absolute percent into a spine index plus a normalized position
// within that spine so we can jump after the section is loaded.
void EpubReaderActivity::jumpToPercent(int percent) {
  pageLoadRetryCount = 0;
  if (!epub) {
    return;
  }

  // BookMetadataCache uses a shared seek-based FsFile for spine metadata lookups.
  // Hold the render/file mutex for the full jump calculation so menu-driven jumps
  // cannot race render/status-bar reads of the same cache file.
  RenderLock lock(*this);

  const size_t bookSize = epub->getBookSize();
  if (bookSize == 0) {
    return;
  }

  // Normalize input to 0-100 to avoid invalid jumps.
  percent = clampPercent(percent);

  int locationSpineIndex = 0;
  float locationSpineProgress = 0.0f;
  if (epub->resolveLocationPercentToSpineProgress(percent, locationSpineIndex, locationSpineProgress)) {
    clearFootnotePreviewState();
    currentSpineIndex = locationSpineIndex;
    pendingSpineProgress = locationSpineProgress;
    nextPageNumber = 0;
    pendingPercentJump = true;
    section.reset();
    armReadingPaceWarmup("percent_jump");
    return;
  }

  // Convert percent into a byte-like absolute position across the spine sizes.
  // Use an overflow-safe computation: (bookSize / 100) * percent + (bookSize % 100) * percent / 100
  size_t targetSize =
      (bookSize / 100) * static_cast<size_t>(percent) + (bookSize % 100) * static_cast<size_t>(percent) / 100;
  if (percent >= 100) {
    // Ensure the final percent lands inside the last spine item.
    targetSize = bookSize - 1;
  }

  const int spineCount = epub->getSpineItemsCount();
  if (spineCount == 0) {
    return;
  }

  int targetSpineIndex = spineCount - 1;
  size_t prevCumulative = 0;

  for (int i = 0; i < spineCount; i++) {
    const size_t cumulative = epub->getCumulativeSpineItemSize(i);
    if (targetSize <= cumulative) {
      // Found the spine item containing the absolute position.
      targetSpineIndex = i;
      prevCumulative = (i > 0) ? epub->getCumulativeSpineItemSize(i - 1) : 0;
      break;
    }
  }

  const size_t cumulative = epub->getCumulativeSpineItemSize(targetSpineIndex);
  const size_t spineSize = (cumulative > prevCumulative) ? (cumulative - prevCumulative) : 0;
  // Store a normalized position within the spine so it can be applied once loaded.
  pendingSpineProgress =
      (spineSize == 0) ? 0.0f : static_cast<float>(targetSize - prevCumulative) / static_cast<float>(spineSize);
  if (pendingSpineProgress < 0.0f) {
    pendingSpineProgress = 0.0f;
  } else if (pendingSpineProgress > 1.0f) {
    pendingSpineProgress = 1.0f;
  }

  // Reset state so render() reloads and repositions on the target spine.
  clearFootnotePreviewState();
  currentSpineIndex = targetSpineIndex;
  nextPageNumber = 0;
  pendingPercentJump = true;
  section.reset();
  armReadingPaceWarmup("percent_jump");
}

void EpubReaderActivity::handleClippingJump(const ClippingJumpResult& clipping) {
  RenderLock lock(*this);
  clearFootnotePreviewState();
  currentSpineIndex = clipping.spineIndex;
  pendingPageJump = clipping.page;
  pendingParagraphIndex = clipping.paragraphIndex;
  pendingClippingIndex = clipping.clippingIndex;
  section.reset();
  armReadingPaceWarmup("clipping_jump");
  pauseReadingPaceTimer("clipping_jump");
}

void EpubReaderActivity::onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action) {
  switch (action) {
    case EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER: {
      const int spineIdx = currentSpineIndex;
      const std::string path = epub->getPath();
      pauseReadingPaceTimer("chapter_selection");
      startActivityForResult(
          std::make_unique<EpubReaderChapterSelectionActivity>(renderer, mappedInput, epub, path, spineIdx),
          [this](const ActivityResult& result) {
            if (!result.isCancelled) {
              const auto& chapterResult = std::get<ChapterResult>(result.data);
              RenderLock lock(*this);

              clearFootnotePreviewState();
              currentSpineIndex = chapterResult.spineIndex;
              pendingPageJump.reset();
              pendingPercentJump = false;
              pendingSpineProgress = 0.0f;
              pendingParagraphIndex = UINT16_MAX;
              pendingClippingIndex = UINT16_MAX;
              pendingRelayoutReposition = false;
              cachedChapterTotalPageCount = 0;
              cachedRelayoutAtChapterStart = false;

              // If anchor is not empty, it will be used later to calculate the page number.
              pendingAnchor = chapterResult.anchor;

              // Otherwise page 0 will be used.
              nextPageNumber = 0;

              section.reset();
              armReadingPaceWarmup("chapter_jump");
              pauseReadingPaceTimer("chapter_jump");
            } else {
              resumeReadingPaceTimer("chapter_selection_cancel");
            }
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::LOOK_UP_WORD:
      openDictionaryLookup(false);
      break;
    case EpubReaderMenuActivity::MenuAction::LOOKUP_HISTORY:
      openDictionaryLookup(true);
      break;
    case EpubReaderMenuActivity::MenuAction::FOOTNOTES: {
      pauseReadingPaceTimer("footnotes");
      startActivityForResult(std::make_unique<EpubReaderFootnotesActivity>(renderer, mappedInput, currentPageFootnotes),
                             [this](const ActivityResult& result) {
                               if (!result.isCancelled) {
                                 const auto& footnoteResult = std::get<FootnoteResult>(result.data);
                                 navigateToHref(footnoteResult.href, true);
                               } else {
                                 resumeReadingPaceTimer("footnotes_cancel");
                               }
                               requestUpdate();
                             });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::GO_TO_PERCENT: {
      float bookProgress = 0.0f;
      {
        // Serialize EPUB metadata/file access with the render task.
        RenderLock lock(*this);
        bookProgress = getCurrentBookProgressPercent();
      }
      const int initialPercent = clampPercent(static_cast<int>(bookProgress + 0.5f));
      pauseReadingPaceTimer("percent_selection");
      startActivityForResult(
          std::make_unique<EpubReaderPercentSelectionActivity>(renderer, mappedInput, initialPercent),
          [this](const ActivityResult& result) {
            if (!result.isCancelled) {
              jumpToPercent(std::get<PercentResult>(result.data).percent);
            } else {
              resumeReadingPaceTimer("percent_selection_cancel");
            }
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DISPLAY_QR: {
      if (section && section->currentPage >= 0 && section->currentPage < section->pageCount) {
        auto p = section->loadPageFromSectionFile();
        if (p) {
          std::string fullText;
          for (const auto& el : p->elements) {
            if (el->getTag() == TAG_PageLine) {
              const auto& line = static_cast<const PageLine&>(*el);
              if (line.getBlock()) {
                const auto& words = line.getBlock()->getWords();
                for (const auto& w : words) {
                  if (!fullText.empty()) fullText += " ";
                  fullText += w;
                }
              }
            }
          }
          if (!fullText.empty()) {
            pauseReadingPaceTimer("qr_display");
            startActivityForResult(
                std::make_unique<QrDisplayActivity>(renderer, mappedInput, fullText),
                [this](const ActivityResult& result) { resumeReadingPaceTimer("qr_display_return"); });
            break;
          }
        }
      }
      // If no text or page loading failed, just close menu
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SAVE_CLIPPING: {
      startClipSelection();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::GO_HOME: {
      goHomeAfterReading();
      return;
    }
    case EpubReaderMenuActivity::MenuAction::DELETE_STATS: {
      pauseReadingPaceTimer("delete_stats_confirm");
      startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput,
                                                                    confirmationHeading(StrId::STR_DELETE_BOOK_STATS),
                                                                    epub ? epub->getTitle() : std::string{}),
                             [this](const ActivityResult& result) {
                               bool statsDeleted = false;
                               if (!result.isCancelled) {
                                 {
                                   RenderLock lock(*this);
                                   if (epub) {
                                     statsDeleted = BookReadingStats::remove(epub->getCachePath());
                                     if (statsDeleted) {
                                       resetCurrentBookStatsAfterDelete();
                                     }
                                   }
                                 }
                                 if (statsDeleted) {
                                   drawToast(renderer, tr(STR_BOOK_STATS_DELETED));
                                   delay(1000);
                                 } else {
                                   LOG_ERR("ERS", "Failed to delete book stats");
                                 }
                               }
                               resumeReadingPaceTimer("delete_stats_return");
                               requestUpdate();
                             });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DELETE_CACHE: {
      pauseReadingPaceTimer("delete_cache_confirm");
      startActivityForResult(
          std::make_unique<ConfirmationActivity>(renderer, mappedInput, confirmationHeading(StrId::STR_DELETE_CACHE),
                                                 epub ? epub->getTitle() : std::string{}),
          [this](const ActivityResult& result) {
            if (result.isCancelled) {
              resumeReadingPaceTimer("delete_cache_cancel");
              requestUpdate();
              return;
            }

            bool cacheDeleted = false;
            {
              RenderLock lock(*this);
              if (epub && section) {
                uint16_t backupSpine = currentSpineIndex;
                uint16_t backupPage = section->currentPage;
                const int backupPageCount = section->pageCount;
                if (!saveProgress(backupSpine, backupPage, backupPageCount)) {
                  LOG_ERR("ERS", "Failed to save progress before cache clear");
                }
                stats.save(epub->getCachePath());
                section.reset();
                cacheDeleted = clearBookCachePreservingUserState(epub->getPath());
                epub->setupCacheDir();
                if (cacheDeleted) {
                  drawToast(renderer, tr(STR_BOOK_CACHE_DELETED));
                }
              }
            }
            if (cacheDeleted) {
              delay(1000);
            }
            goHomeAfterReading();
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::RESET_READING_PACE: {
      resetReadingPaceData();
      drawToast(renderer, tr(STR_READING_PACE_RESET));
      delay(1000);
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SCREENSHOT: {
      {
        RenderLock lock(*this);
        pendingScreenshot = true;
      }
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::READING_STATS: {
      // Include elapsed time from the current session in the display stats.
      BookReadingStats displayStats = stats;
      ReadingSessionSnapshot sessionSnapshot;
      if (epub && shouldTrackReadingStatsForPath(epub->getPath())) {
        uint32_t currentPageSeconds = 0;
        sessionSnapshot.readingSeconds = sessionReadingSeconds;
        sessionSnapshot.screenPages = sessionScreenPages;
        sessionSnapshot.startedAt = sessionStartLocalDateTime;
        sessionSnapshot.hasStartedAt = hasSessionStartLocalDateTime;
        displayStats.totalReadingSeconds = displayStats.totalReadingSeconds > UINT32_MAX - sessionReadingSeconds
                                               ? UINT32_MAX
                                               : displayStats.totalReadingSeconds + sessionReadingSeconds;
        if (currentPageReadingSecondsForStats(currentPageSeconds, "book_stats_preview")) {
          sessionSnapshot.readingSeconds = sessionSnapshot.readingSeconds > UINT32_MAX - currentPageSeconds
                                               ? UINT32_MAX
                                               : sessionSnapshot.readingSeconds + currentPageSeconds;
          displayStats.totalReadingSeconds = displayStats.totalReadingSeconds > UINT32_MAX - currentPageSeconds
                                                 ? UINT32_MAX
                                                 : displayStats.totalReadingSeconds + currentPageSeconds;
        }
        if (sessionSnapshot.screenPages > 0 && displayStats.sessionCount < UINT16_MAX) {
          displayStats.sessionCount++;
          displayStats.countedSessionSeconds += sessionSnapshot.readingSeconds;
        }
      }
      uint32_t estimatedTimeLeftSeconds = 0;
      const bool hasEstimatedTimeLeft = estimateTimeLeftSeconds(true, estimatedTimeLeftSeconds);
      displayStats.estimatedTimeLeftSeconds = hasEstimatedTimeLeft ? estimatedTimeLeftSeconds : 0;
      const bool hasSyncedStats = GlobalReadingStats::hasSyncedStats();
      const GlobalReadingStats displayAllDevicesStats =
          hasSyncedStats ? GlobalReadingStats::loadAggregated(globalStats) : GlobalReadingStats{};
      displayStats.totalWordCount = std::max(epub->getTotalWords(), displayStats.totalWordCount);
      pauseReadingPaceTimer("book_stats");
      if (hasSyncedStats) {
        startActivityForResult(
            std::make_unique<BookStatsActivity>(renderer, mappedInput, epub->getTitle(), epub->getCachePath(),
                                                displayStats, getCurrentBookProgressPercent(), hasEstimatedTimeLeft,
                                                estimatedTimeLeftSeconds, globalStats, displayAllDevicesStats, false,
                                                sessionSnapshot, displayStats.totalWordCount),
            [this](const ActivityResult&) { handleBookStatsReturn(); });
      } else {
        startActivityForResult(std::make_unique<BookStatsActivity>(
                                   renderer, mappedInput, epub->getTitle(), epub->getCachePath(), displayStats,
                                   getCurrentBookProgressPercent(), hasEstimatedTimeLeft, estimatedTimeLeftSeconds,
                                   globalStats, false, sessionSnapshot, displayStats.totalWordCount),
                               [this](const ActivityResult&) { handleBookStatsReturn(); });
      }
      break;
    }
    case EpubReaderMenuActivity::MenuAction::BOOK_INFO: {
      if (!epub) break;
      pauseReadingPaceTimer("book_info");
      startActivityForResult(std::make_unique<BookInfoActivity>(renderer, mappedInput, epub->getPath(),
                                                                epub->getTitle(), epub->getThumbBmpPath()),
                             [this](const ActivityResult&) {
                               resumeReadingPaceTimer("book_info_return");
                               requestUpdate();
                             });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::TOGGLE_COMPLETED: {
      const bool markCompleted = !stats.isCompleted;
      setBookCompleted(markCompleted);
      showCompletedFeedback(markCompleted);
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::READER_OPTIONS: {
      ReaderSettingsSnapshot before;
      captureReaderSettings(before);
      const uint8_t bionicBefore = BionicReadingMode::normalize(SETTINGS.bionicReadingEnabled);
      pauseReadingPaceTimer("reader_options");
      startActivityForResult(std::make_unique<ReaderOptionsActivity>(
                                 renderer, mappedInput, saveReaderOptionsForBook, this, saveGlobalSettingsForBookReader,
                                 this, beginGlobalSettingsEditForBookReader, this, endGlobalSettingsEditForBookReader,
                                 this, epub && epub->hasStablePageNumbers()),
                             [this, before, bionicBefore](const ActivityResult&) {
                               ReaderSettingsSnapshot after;
                               captureReaderSettings(after);
                               applyOrientation(SETTINGS.orientation);
                               if (readerSettingsChanged(before, after)) {
                                 if (bionicBefore != BionicReadingMode::normalize(SETTINGS.bionicReadingEnabled)) {
                                   showBionicFeedback(SETTINGS.bionicReadingEnabled);
                                 }
                                 beginInteractiveRelayout();
                               }
                               resumeReadingPaceTimer("reader_options_return");
                               requestUpdate();
                             });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SYNC: {
      if (activeFootnotePreview) {
        requestUpdate();
        break;
      }
      if (KOREADER_STORE.hasCredentials()) {
        const int currentPage = section ? section->currentPage : nextPageNumber;
        const int totalPages = section ? section->pageCount : cachedChapterTotalPageCount;
        std::optional<uint16_t> paragraphIndex;
        std::optional<uint16_t> listItemIndex;
        if (section) {
          getSyncPageAnchors(*section, currentPage, paragraphIndex, listItemIndex);
        }

        // Pre-compute local KO position and chapter name while Epub is still in RAM.
        CrossPointPosition localPos = {currentSpineIndex, currentPage, totalPages};
        if (paragraphIndex.has_value()) {
          localPos.paragraphIndex = *paragraphIndex;
          localPos.hasParagraphIndex = true;
        }
        if (listItemIndex.has_value()) {
          localPos.liIndex = *listItemIndex;
          localPos.hasLiIndex = true;
        }
        KOReaderPosition localKoPos = ProgressMapper::toKOReader(epub, localPos);
        const int tocIdx = epub->getTocIndexForSpineIndex(currentSpineIndex);
        std::string localChapterName = (tocIdx >= 0) ? epub->getTocItem(tocIdx).title : "";
        const std::string savedEpubPath = epub->getPath();

        // Persist current position so the reader resumes at the right page on return.
        // goToReader() depends on this file, so abort the sync if the write fails.
        if (!saveProgress(currentSpineIndex, currentPage, totalPages)) {
          LOG_ERR("KOSync", "Aborting sync because current progress could not be saved");
          pendingSyncSaveError = true;
          requestUpdate();
          return;
        }

        // Release the heavy Section now. Keep Epub alive until onExit(), which still
        // needs it for stats/cache cleanup before the sync activity starts.
        LOG_DBG("KOSync", "Releasing section for sync (heap before: %u)", (unsigned)ESP.getFreeHeap());
        {
          RenderLock lock(*this);
          if (section) {
            nextPageNumber = section->currentPage;
          }
          section.reset();
        }
        LOG_DBG("KOSync", "Section released for sync (heap after: %u)", (unsigned)ESP.getFreeHeap());

        pauseReadingPaceTimer("sync_progress");
        activityManager.replaceActivity(std::make_unique<KOReaderSyncActivity>(
            renderer, mappedInput, savedEpubPath, currentSpineIndex, currentPage, totalPages, std::move(localKoPos),
            std::move(localChapterName), paragraphIndex));
      }
      break;
    }
    case EpubReaderMenuActivity::MenuAction::NEARBY_POSITION_SYNC: {
      const int currentPage = section ? section->currentPage : nextPageNumber;
      const int totalPages = section ? section->pageCount : std::max(1, cachedChapterTotalPageCount);
      std::optional<uint16_t> paragraphIndex;
      std::optional<uint16_t> listItemIndex;
      if (section) {
        getSyncPageAnchors(*section, currentPage, paragraphIndex, listItemIndex);
      }

      CrossPointPosition localPos = {currentSpineIndex, currentPage, totalPages};
      if (paragraphIndex.has_value()) {
        localPos.paragraphIndex = *paragraphIndex;
        localPos.hasParagraphIndex = true;
      }
      if (listItemIndex.has_value()) {
        localPos.liIndex = *listItemIndex;
        localPos.hasLiIndex = true;
      }
      KOReaderPosition localKoPos = ProgressMapper::toKOReader(epub, localPos);
      const int tocIdx = epub->getTocIndexForSpineIndex(currentSpineIndex);
      std::string localChapterName = (tocIdx >= 0) ? epub->getTocItem(tocIdx).title : "";
      const std::string savedEpubPath = epub->getPath();

      if (!saveProgress(currentSpineIndex, currentPage, totalPages)) {
        LOG_ERR("NBPS", "Aborting nearby position sync because current progress could not be saved");
        pendingSyncSaveError = true;
        requestUpdate();
        return;
      }

      LOG_DBG("NBPS", "Releasing section for nearby position sync (heap before: %u)", (unsigned)ESP.getFreeHeap());
      {
        RenderLock lock(*this);
        if (section) {
          nextPageNumber = section->currentPage;
        }
        section.reset();
      }
      LOG_DBG("NBPS", "Section released for nearby position sync (heap after: %u)", (unsigned)ESP.getFreeHeap());

      pauseReadingPaceTimer("nearby_position_sync");
      activityManager.replaceActivity(std::make_unique<NearbyBookPositionSyncActivity>(
          renderer, mappedInput, epub, savedEpubPath, currentSpineIndex, currentPage, totalPages, std::move(localKoPos),
          std::move(localChapterName), paragraphIndex, listItemIndex));
      break;
    }
    case EpubReaderMenuActivity::MenuAction::BOOKMARK_TOGGLE: {
      const int bookmarkPageCount = section ? section->pageCount : 0;
      if (activeFootnotePreview || !section || bookmarkPageCount == 0 || section->pageCount == 0) break;
      const uint16_t spine = static_cast<uint16_t>(currentSpineIndex);
      const float progress = static_cast<float>(section->currentPage) / static_cast<float>(bookmarkPageCount);

      if (BOOKMARKS.hasBookmarkForPage(spine, progress, bookmarkPageCount)) {
        BOOKMARKS.removeBookmarkForPage(spine, progress, bookmarkPageCount);
        bookmarkFeedbackType = BookmarkFeedbackType::Removed;
      } else {
        const char* chapterTitle = nullptr;
        std::string titleStr;
        const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
        if (tocIndex != -1) {
          titleStr = epub->getTocItem(tocIndex).title;
          chapterTitle = titleStr.c_str();
        }
        uint16_t paragraphIndex = UINT16_MAX;
        if (const auto pIdx = section->getParagraphIndexForPage(static_cast<uint16_t>(section->currentPage))) {
          paragraphIndex = *pIdx;
        }
        char snippet[BOOKMARK_SNIPPET_MAX] = {};
        if (auto page = section->loadPageFromSectionFile()) {
          buildBookmarkSnippet(*page, snippet, sizeof(snippet));
        }
        const auto addResult =
            BOOKMARKS.addBookmark(spine, progress, bookmarkPageCount, chapterTitle, paragraphIndex, snippet);
        bookmarkFeedbackType = (addResult == BookmarkStore::AddResult::Added) ? BookmarkFeedbackType::Added
                                                                              : BookmarkFeedbackType::LimitReached;
      }
      pendingBookmarkFeedback = true;
      bookmarkFeedbackShowTime = millis();
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::VIEW_BOOKMARKS: {
      pauseReadingPaceTimer("bookmark_list");
      startActivityForResult(
          std::make_unique<EpubReaderBookmarkListActivity>(renderer, mappedInput, BOOKMARKS.getBookmarks()),
          [this](const ActivityResult& result) {
            if (!result.isCancelled) {
              const auto& bm = std::get<BookmarkResult>(result.data);
              RenderLock lock(*this);
              clearFootnotePreviewState();
              if (section && currentSpineIndex == bm.spineIndex) {
                bool resolved = false;
                if (bm.paragraphIndex != UINT16_MAX) {
                  if (const auto paragraphPage = section->getPageForParagraphIndex(bm.paragraphIndex)) {
                    section->currentPage = *paragraphPage;
                    resolved = true;
                    LOG_DBG("ERS", "Resolved bookmark paragraph %u to page %u", bm.paragraphIndex, *paragraphPage);
                  }
                }
                if (!resolved) {
                  const int totalPages = section->pageCount;
                  int targetPage = static_cast<int>(bm.progress * static_cast<float>(totalPages));
                  if (totalPages > 0 && targetPage >= totalPages) {
                    targetPage = totalPages - 1;
                  }
                  section->currentPage = std::max(0, targetPage);
                }
                nextPageNumber = section->currentPage;
                pendingPercentJump = false;
                pendingParagraphIndex = UINT16_MAX;
              } else {
                currentSpineIndex = bm.spineIndex;
                pendingSpineProgress = bm.progress;
                pendingParagraphIndex = bm.paragraphIndex;
                pendingPercentJump = true;
                section.reset();
              }
              armReadingPaceWarmup("bookmark_jump");
              pauseReadingPaceTimer("bookmark_jump");
            } else {
              resumeReadingPaceTimer("bookmark_list_cancel");
            }
            requestUpdate();
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::VIEW_CLIPPINGS: {
      pauseReadingPaceTimer("clipping_list");
      startActivityForResult(
          std::make_unique<EpubReaderClippingListActivity>(renderer, mappedInput, CLIPPINGS.getClippings()),
          [this](const ActivityResult& result) {
            if (!result.isCancelled) {
              const auto& clipping = std::get<ClippingJumpResult>(result.data);
              handleClippingJump(clipping);
            } else {
              resumeReadingPaceTimer("clipping_list_cancel");
            }
            requestUpdate();
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DELETE_BOOKMARKS: {
      pauseReadingPaceTimer("delete_bookmarks_confirm");
      startActivityForResult(
          std::make_unique<ConfirmationActivity>(renderer, mappedInput,
                                                 confirmationHeading(StrId::STR_DELETE_BOOKMARKS),
                                                 epub ? epub->getTitle() : std::string{}),
          [this](const ActivityResult& result) {
            if (!result.isCancelled) {
              BOOKMARKS.clearAll();
            }
            resumeReadingPaceTimer(result.isCancelled ? "delete_bookmarks_cancel" : "delete_bookmarks_return");
            requestUpdate();
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::AUTO_PAGE_TURN:
      openAutoPageTurnIntervalPicker();
      break;
    case EpubReaderMenuActivity::MenuAction::ROTATE_SCREEN:
    case EpubReaderMenuActivity::MenuAction::CONTROLS_OPTIONS:
    case EpubReaderMenuActivity::MenuAction::OPEN_FULL_MENU:
      break;
  }
}

void EpubReaderActivity::reindexCurrentSection() {
  saveCurrentBookReaderSettings();
  if (activeFootnotePreview) {
    restoreSavedPosition();
    return;
  }
  beginInteractiveRelayout();
}

void EpubReaderActivity::beginInteractiveRelayout(const bool allowPreview) {
  relayoutBuildCancelRequested.store(true, std::memory_order_release);
  relayoutBuildRetired.store(false, std::memory_order_release);
  relayoutBuildSafeModePending.store(false, std::memory_order_release);
  {
    RenderLock lock(*this);
    if (section) {
      if (activeRelayoutPreview) {
        cacheRelayoutPreviewPosition();
        section->clearCache();
      } else {
        cacheCurrentSectionPosition();
      }
    }
    section.reset();
    activeRelayoutPreview = false;
    activeChapterPreview = false;
    pendingRelayoutPreviewFromBack = false;
    relayoutPreviewUserMoved = false;
    pendingRelayoutPreview = allowPreview && pendingRelayoutReposition && cachedPageParagraphIndex != UINT16_MAX;
  }
  sdFontSystem.ensureLoaded(renderer);
  relayoutBuildCancelRequested.store(false, std::memory_order_release);
  relayoutBuildEligibleAfterMs.store(millis() + RELAYOUT_BUILD_IDLE_MS, std::memory_order_release);
  requestUpdate();
}

void EpubReaderActivity::cacheRelayoutPreviewPosition() {
  if (!activeRelayoutPreview || !section || section->pageCount == 0 || section->currentPage < 0 ||
      section->currentPage >= section->pageCount) {
    return;
  }

  const uint16_t currentPage = static_cast<uint16_t>(section->currentPage);
  const auto paragraphIndex = section->getParagraphIndexForPage(currentPage);
  if (!paragraphIndex) return;

  cachedPageParagraphIndex = *paragraphIndex;
  uint16_t paragraphStartPage = currentPage;
  while (paragraphStartPage > 0) {
    const auto previous = section->getParagraphIndexForPage(paragraphStartPage - 1);
    if (!previous || *previous != *paragraphIndex) break;
    paragraphStartPage--;
  }

  uint16_t paragraphEndPage = currentPage + 1;
  while (paragraphEndPage < section->pageCount) {
    const auto next = section->getParagraphIndexForPage(paragraphEndPage);
    if (!next || *next != *paragraphIndex) break;
    paragraphEndPage++;
  }

  cachedPageParagraphOffset = currentPage - paragraphStartPage;
  cachedPageParagraphSpan = std::max<uint16_t>(1, paragraphEndPage - paragraphStartPage);
  cachedRelayoutAtChapterStart = currentPage == 0;
  pendingRelayoutReposition = true;
}

void EpubReaderActivity::positionRelayoutPreview() {
  if (!activeRelayoutPreview || !section || section->pageCount == 0 || cachedPageParagraphIndex == UINT16_MAX) {
    return;
  }
  if (cachedRelayoutAtChapterStart) {
    section->currentPage = 0;
    return;
  }

  const auto paragraphPage = section->getPageForParagraphIndex(cachedPageParagraphIndex);
  if (!paragraphPage) {
    section->currentPage = 0;
    return;
  }

  uint16_t newStartPage = std::min<uint16_t>(*paragraphPage, section->pageCount - 1);
  uint16_t newEndPage = newStartPage + 1;
  while (newEndPage < section->pageCount) {
    const auto paragraph = section->getParagraphIndexForPage(newEndPage);
    if (!paragraph || *paragraph != cachedPageParagraphIndex) break;
    newEndPage++;
  }

  const uint16_t newSpan = std::max<uint16_t>(1, newEndPage - newStartPage);
  const uint16_t oldSpan = std::max<uint16_t>(1, cachedPageParagraphSpan);
  const uint16_t oldOffset = std::min<uint16_t>(cachedPageParagraphOffset, oldSpan - 1);
  uint16_t newOffset = 0;
  if (oldSpan > 1 && newSpan > 1) {
    newOffset =
        static_cast<uint16_t>((static_cast<uint32_t>(oldOffset) * (newSpan - 1) + (oldSpan - 1) / 2) / (oldSpan - 1));
  }
  section->currentPage = newStartPage + newOffset;
}

void EpubReaderActivity::openFileTransfer() {
  pauseReadingPaceTimer("file_transfer");
  if (epub && section) {
    saveProgress(currentSpineIndex, section->currentPage, section->pageCount);
  }

  activityManager.goToFileTransfer(epub ? epub->getPath() : std::string{});
}

void EpubReaderActivity::openAutoPageTurnIntervalPicker(const bool ignoreInitialConfirmRelease) {
  pauseReadingPaceTimer("auto_turn_interval");
  startActivityForResult(
      std::make_unique<IntervalSelectionActivity>(
          renderer, mappedInput, "EpubReaderAutoPageTurnInterval", StrId::STR_AUTO_TURN_INTERVAL_SECONDS,
          getAutoPageTurnIntervalSeconds(), MIN_AUTO_PAGE_TURN_INTERVAL_S, MAX_AUTO_PAGE_TURN_INTERVAL_S, 1, 5,
          StrId::STR_NONE_OPT, /*readerActivity=*/true,
          /*allowPowerAsConfirm=*/true, ignoreInitialConfirmRelease),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          setAutoPageTurnIntervalSeconds(static_cast<uint16_t>(std::get<IntervalResult>(result.data).value));
        } else {
          resumeReadingPaceTimer("auto_turn_interval_cancel");
        }
        requestUpdate();
      });
}

void EpubReaderActivity::openDictionaryLookup(const bool history) {
  if (!section) {
    requestUpdate();
    return;
  }

  std::shared_ptr<Page> page;
  ReaderViewportLayout layout{};
  int readerFontId = SETTINGS.getReaderFontId();
  {
    RenderLock lock(*this);
    if (!section) {
      requestUpdate();
      return;
    }
    layout = computeReaderViewportLayout(renderer, automaticPageTurnActive);
    readerFontId = activeSectionFontId > 0 ? activeSectionFontId : SETTINGS.getReaderFontId();
    page = section->loadPageFromSectionFile();
  }

  if (!page) {
    requestUpdate();
    return;
  }

  pauseReadingPaceTimer(history ? "dictionary_history" : "dictionary_lookup");
  auto onReturn = [this](const ActivityResult&) {
    resumeReadingPaceTimer("dictionary_return");
    requestUpdate();
  };
  if (history) {
    startActivityForResult(std::make_unique<DictionaryHistoryActivity>(renderer, mappedInput, page, readerFontId,
                                                                       layout.marginLeft, layout.marginTop),
                           std::move(onReturn));
  } else {
    startActivityForResult(std::make_unique<DictionaryWordSelectActivity>(renderer, mappedInput, page, readerFontId,
                                                                          layout.marginLeft, layout.marginTop),
                           std::move(onReturn));
  }
}

void EpubReaderActivity::startClipSelection() {
  if (!section || !epub) {
    requestUpdate();
    return;
  }

  ReaderViewportLayout layout{};
  std::vector<WordRef> words;
  int readerFontId = 0;
  int startPage = 0;
  std::string bookTitle;
  std::string author;
  std::string chapterTitle;

  {
    RenderLock lock(*this);
    if (!section || !epub) {
      requestUpdate();
      return;
    }

    layout = computeReaderViewportLayout(renderer, automaticPageTurnActive);
    readerFontId = activeSectionFontId > 0 ? activeSectionFontId : SETTINGS.getReaderFontId();
    const int lineHeight = renderer.getLineHeight(readerFontId);
    startPage = section->currentPage;
    const int pagesToLoad = std::min(3, section->pageCount - startPage);
    std::array<uint16_t, 3> pageWordCounts{};
    static constexpr size_t CLIP_SELECTION_WORDS_PER_PAGE = 80;
    static constexpr size_t MAX_CLIP_SELECTION_WORDS = 240;
    static constexpr uint32_t CLIP_SELECTION_WORD_RESERVE_HEADROOM = 16U * 1024U;
    const size_t maxSelectableWords =
        std::min(MAX_CLIP_SELECTION_WORDS, static_cast<size_t>(pagesToLoad) * CLIP_SELECTION_WORDS_PER_PAGE);
    const uint32_t wordReserveBytes = static_cast<uint32_t>(maxSelectableWords * sizeof(WordRef));
    const auto heapBeforeWords = MemoryBudget::snapshot();
    if (heapBeforeWords.maxAllocHeap < wordReserveBytes + CLIP_SELECTION_WORD_RESERVE_HEADROOM) {
      LOG_ERR("CLIP", "Low heap for clipping selection (%u free, %u max alloc, need block %u); skipping",
              heapBeforeWords.freeHeap, heapBeforeWords.maxAllocHeap,
              wordReserveBytes + CLIP_SELECTION_WORD_RESERVE_HEADROOM);
      section->currentPage = startPage;
      requestUpdate();
      return;
    }
    words.reserve(maxSelectableWords);
    bool wordLimitLogged = false;

    for (int pageIdx = 0; pageIdx < pagesToLoad; ++pageIdx) {
      section->currentPage = startPage + pageIdx;
      auto page = section->loadPageFromSectionFile();
      if (!page) break;

      for (const auto& element : page->elements) {
        if (element->getTag() != TAG_PageLine) continue;
        const auto& line = static_cast<const PageLine&>(*element);
        if (!line.getBlock()) continue;

        const auto& block = *line.getBlock();
        const auto& xpos = block.getWordXpos();
        const auto& wordList = block.getWords();
        const auto& styles = block.getWordStyles();
        const size_t count = std::min({wordList.size(), xpos.size(), styles.size()});
        if (renderer.isSdCardFont(readerFontId) && count > 0) {
          uint8_t styleMask = 0;
          for (size_t i = 0; i < count; ++i) {
            styleMask |= static_cast<uint8_t>(1u << (static_cast<uint8_t>(styles[i]) & 0x03));
          }
          renderer.ensureSdCardFontReady(readerFontId, wordList, /*includeHyphen=*/false, styleMask);
        }
        for (size_t i = 0; i < count; ++i) {
          if (!hasVisibleWordText(wordList[i])) continue;
          if (words.size() >= maxSelectableWords) {
            if (!wordLimitLogged) {
              LOG_ERR("CLIP", "Selectable word cap hit (%u words); clipping range truncated",
                      static_cast<unsigned>(maxSelectableWords));
              wordLimitLogged = true;
            }
            break;
          }

          const auto textStyle = static_cast<EpdFontFamily::Style>(styles[i] & ~EpdFontFamily::UNDERLINE);
          int wordWidth = renderer.getTextAdvanceX(readerFontId, wordList[i].c_str(), textStyle);
          if (wordWidth <= 0) continue;

          WordRef word;
          word.x = layout.marginLeft + line.xPos + xpos[i];
          word.y = layout.marginTop + line.yPos;
          if (i + 1 < count && xpos[i + 1] > xpos[i]) {
            wordWidth = std::min(wordWidth, static_cast<int>(xpos[i + 1] - xpos[i]));
          }
          word.w = wordWidth;
          word.h = lineHeight;
          word.pageIdx = pageIdx;
          word.pageWordIndex = pageWordCounts[pageIdx]++;
          word.text = wordList[i];
          word.style = textStyle;
          word.endsWithInsertedHyphen = block.wordEndsWithInsertedHyphen(i);
          word.lineIsRtl = block.getBlockStyle().isRtl;
          words.push_back(std::move(word));
        }
      }
    }

    section->currentPage = startPage;

    auto endsWithHyphen = [](const std::string& word) { return !word.empty() && word.back() == '-'; };
    const int indentThreshold = renderer.getLineHeight(readerFontId) / 2;
    int previousLineFirstIdx = -1;
    for (int i = 0; i < static_cast<int>(words.size()); ++i) {
      const bool newLine = i == 0 || words[i].pageIdx != words[i - 1].pageIdx || words[i].y != words[i - 1].y;
      if (!newLine) continue;

      const bool byEmSpace = hasEmSpacePrefix(words[i].text);
      const bool byIndent = !byEmSpace && previousLineFirstIdx >= 0 &&
                            words[i].x > words[previousLineFirstIdx].x + indentThreshold &&
                            !endsWithHyphen(words[i - 1].text);
      if (byEmSpace || byIndent) {
        words[i].paragraphStart = true;
      }
      previousLineFirstIdx = i;
    }

    const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
    if (tocIndex >= 0) {
      chapterTitle = epub->getTocItem(tocIndex).title;
    }
    bookTitle = epub->getTitle();
    author = epub->getAuthor();
  }

  if (words.empty()) {
    LOG_ERR("CLIP", "No selectable words on current EPUB page");
    requestUpdate();
    return;
  }

  pauseReadingPaceTimer("clip_selection");
  startActivityForResult(
      std::make_unique<ClipSelectionActivity>(renderer, mappedInput, std::move(words), readerFontId, *section,
                                              startPage, layout.marginTop, layout.marginLeft),
      [this, bookTitle = std::move(bookTitle), author = std::move(author),
       chapterTitle = std::move(chapterTitle)](const ActivityResult& result) {
        if (!result.isCancelled) {
          const auto& clip = std::get<ClippingResult>(result.data);
          if (!clip.text.empty()) {
            const size_t clippingIndex = CLIPPINGS.getClippings().size();
            const auto addResult =
                CLIPPINGS.addClipping(static_cast<uint16_t>(currentSpineIndex), clip.sectionPage, clip.endSectionPage,
                                      clip.sectionPageCount, clip.startPageWordIndex, clip.endPageWordIndex,
                                      clip.wordCount, chapterTitle.c_str(), clip.paragraphIndex, clip.text);
            bool exported = false;
            if (addResult == ClippingStore::AddResult::Added) {
              exported = ClippingsManager::saveClipping(bookTitle, author, chapterTitle,
                                                        static_cast<int>(clip.sectionPage) + 1, clip.text);
              if (!exported && !CLIPPINGS.removeClippingAt(clippingIndex)) {
                LOG_ERR("CLIP", "Failed to roll back clipping after export failure");
              }
            }
            const bool saved = addResult == ClippingStore::AddResult::Added && exported;
            drawToast(renderer, addResult == ClippingStore::AddResult::LimitReached ? tr(STR_CLIPPING_LIMIT_REACHED)
                                : saved                                             ? tr(STR_CLIPPING_SAVED)
                                                                                    : tr(STR_CLIPPING_FAILED));
            delay(1000);
          }
        }
        resumeReadingPaceTimer("clip_selection_return");
        requestUpdate();
      });
}

void EpubReaderActivity::resetReadingPaceData() {
#if defined(ENABLE_SERIAL_LOG) && LOG_LEVEL >= 2
  const uint16_t oldAvg = stats.avgSecondsPerForwardPage;
  const uint16_t oldCount = stats.paceSampleCount;
#endif
  stats.avgSecondsPerForwardPage = 0;
  stats.paceSampleCount = 0;
  stats.estimatedTimeLeftSeconds = 0;
  sessionPaceSampleSeconds = 0;
  sessionPaceSampleCount = 0;
  armReadingPaceWarmup("reading_pace_reset");
  if (epub) {
    epub->setupCacheDir();
    stats.save(epub->getCachePath());
  }
#if defined(ENABLE_SERIAL_LOG) && LOG_LEVEL >= 2
  LOG_DBG("ERS", "Reading pace reset: avg=%u->%u samples=%u->%u totalReadingSeconds=%lu totalPagesTurned=%lu", oldAvg,
          stats.avgSecondsPerForwardPage, oldCount, stats.paceSampleCount,
          static_cast<unsigned long>(stats.totalReadingSeconds), static_cast<unsigned long>(stats.totalPagesTurned));
#endif
}

void EpubReaderActivity::resetCurrentBookStatsAfterDelete() {
  stats = BookReadingStats{};
  sessionReadingSeconds = 0;
  sessionScreenPages = 0;
  readingStatsCommitted = false;
  sessionPaceSampleSeconds = 0;
  sessionPaceSampleCount = 0;
  pendingReadFolderMove = false;
  hasSessionStartLocalDateTime = getCurrentLocalReadingStatsDateTime(sessionStartLocalDateTime);
  armReadingPaceWarmup("book_stats_delete");
  initializeCompletionPromptTrigger();
}

void EpubReaderActivity::executeReaderQuickAction(CrossPointSettings::LONG_PRESS_MENU_ACTION action) {
  switch (action) {
    case CrossPointSettings::LONG_MENU_SLEEP:
      enterDeepSleep();
      break;
    case CrossPointSettings::LONG_MENU_CHANGE_FONT:
      SETTINGS.fontFamily = (SETTINGS.fontFamily + 1) % CrossPointSettings::FONT_FAMILY_COUNT;
      reindexCurrentSection();
      break;
    case CrossPointSettings::LONG_MENU_TOGGLE_GUIDE_DOTS:
      SETTINGS.guideReadingEnabled = !SETTINGS.guideReadingEnabled;
      reindexCurrentSection();
      break;
    case CrossPointSettings::LONG_MENU_TOGGLE_BIONIC:
      SETTINGS.bionicReadingEnabled = BionicReadingMode::next(SETTINGS.bionicReadingEnabled);
      showBionicFeedback(SETTINGS.bionicReadingEnabled);
      reindexCurrentSection();
      break;
    case CrossPointSettings::LONG_MENU_TOGGLE_BOOKMARK:
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::BOOKMARK_TOGGLE);
      break;
    case CrossPointSettings::LONG_MENU_REFRESH_SCREEN:
      pagesUntilFullRefresh = 1;  // Forces HALF_REFRESH on next render
      requestUpdate();
      break;
    case CrossPointSettings::LONG_MENU_SYNC_PROGRESS:
      if (KOREADER_STORE.hasCredentials()) {
        onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::SYNC);
      } else {
        pauseReadingPaceTimer("koreader_settings");
        startActivityForResult(std::make_unique<KOReaderSettingsActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) {
                                 resumeReadingPaceTimer("koreader_settings_return");
                                 saveGlobalSettingsPreservingBookOverrides();
                               });
      }
      break;
    case CrossPointSettings::LONG_MENU_MARK_FINISHED: {
      const bool newCompleted = !stats.isCompleted;
      setBookCompleted(newCompleted);
      showCompletedFeedback(newCompleted);
    }
      requestUpdate();
      break;
    case CrossPointSettings::LONG_MENU_READING_STATS:
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::READING_STATS);
      break;
    case CrossPointSettings::LONG_MENU_SCREENSHOT:
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::SCREENSHOT);
      break;
    case CrossPointSettings::LONG_MENU_CYCLE_PAGE_TURN:
      openAutoPageTurnIntervalPicker(/*ignoreInitialConfirmRelease=*/true);
      break;
    case CrossPointSettings::LONG_MENU_FILE_TRANSFER:
      openFileTransfer();
      break;
    case CrossPointSettings::LONG_MENU_CALIBRE_WIRELESS:
      activityManager.goToCalibreWireless(epub ? epub->getPath() : "");
      break;
    case CrossPointSettings::LONG_MENU_JOIN_NETWORK:
      activityManager.goToJoinNetworkFileTransfer(epub ? epub->getPath() : "");
      break;
    case CrossPointSettings::LONG_MENU_CREATE_HOTSPOT:
      activityManager.goToHotspotFileTransfer(epub ? epub->getPath() : "");
      break;
    case CrossPointSettings::LONG_MENU_TOGGLE_TILT_PAGE_TURN:
      if (halTiltSensor.isAvailable()) {
        SETTINGS.tiltPageTurn = SETTINGS.tiltPageTurn == CrossPointSettings::TILT_OFF ? CrossPointSettings::TILT_ON
                                                                                      : CrossPointSettings::TILT_OFF;
        saveGlobalSettingsPreservingBookOverrides();
        halTiltSensor.clearPendingEvents();
        showTiltPageTurnFeedback(SETTINGS.tiltPageTurn != CrossPointSettings::TILT_OFF);
        requestUpdate();
      }
      break;
    case CrossPointSettings::LONG_MENU_TOGGLE_DARK_MODE:
      SETTINGS.readerDarkMode = !SETTINGS.readerDarkMode;
      saveCurrentBookReaderSettings();
      requestUpdate();
      break;
    case CrossPointSettings::LONG_MENU_FOOTNOTES:
      executeFootnoteQuickAction();
      break;
    case CrossPointSettings::LONG_MENU_FILE_BROWSER:
      activityManager.goToFileBrowser(epub ? epub->getPath() : "");
      break;
    case CrossPointSettings::LONG_MENU_CREATE_CLIPPING:
      startClipSelection();
      break;
    case CrossPointSettings::LONG_MENU_OFF:
    default:
      break;
  }
}

bool EpubReaderActivity::quickActionUsesConfirmRelease(const CrossPointSettings::LONG_PRESS_MENU_ACTION action) const {
  switch (action) {
    case CrossPointSettings::LONG_MENU_READING_STATS:
    case CrossPointSettings::LONG_MENU_CYCLE_PAGE_TURN:
    case CrossPointSettings::LONG_MENU_CREATE_CLIPPING:
      return true;
    case CrossPointSettings::LONG_MENU_FOOTNOTES:
      return currentPageFootnotes.size() > 1;
    default:
      return false;
  }
}

bool EpubReaderActivity::quickActionUsesPowerRelease(const CrossPointSettings::LONG_PRESS_MENU_ACTION action) const {
  return action == CrossPointSettings::LONG_MENU_FOOTNOTES && currentPageFootnotes.size() > 1;
}

void EpubReaderActivity::suppressConfirmShortcutRelease(const CrossPointSettings::LONG_PRESS_MENU_ACTION action) {
  if (quickActionUsesConfirmRelease(action)) {
    mappedInput.suppressNextConfirmRelease();
  }

  if (mappedInput.isPressed(MappedInputManager::Button::Power)) {
    if (quickActionUsesConfirmRelease(action)) {
      mappedInput.suppressNextPowerConfirmRelease();
    }
    if (quickActionUsesPowerRelease(action)) {
      mappedInput.suppressNextPowerRelease();
    }
  }
}

void EpubReaderActivity::executeFootnoteQuickAction(const bool suppressInitialPowerRelease) {
  if (footnoteDepth > 0 && SETTINGS.pwrBtnFootnoteBack) {
    restoreSavedPosition();
    return;
  }

  if (currentPageFootnotes.size() == 1) {
    navigateToHref(currentPageFootnotes[0].href, true);
    return;
  }

  if (currentPageFootnotes.size() > 1) {
    if (suppressInitialPowerRelease) {
      suppressPowerShortcutRelease();
    }
    pauseReadingPaceTimer("footnotes");
    startActivityForResult(std::make_unique<EpubReaderFootnotesActivity>(renderer, mappedInput, currentPageFootnotes),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& footnoteResult = std::get<FootnoteResult>(result.data);
                               navigateToHref(footnoteResult.href, true);
                             } else {
                               resumeReadingPaceTimer("footnotes_cancel");
                             }
                             requestUpdate();
                           });
  }
}

bool EpubReaderActivity::executeShortPowerButtonAction() {
  if (!mappedInput.wasShortPowerActionResolved()) {
    return false;
  }

  switch (static_cast<CrossPointSettings::SHORT_PWRBTN>(mappedInput.getResolvedShortPowerAction())) {
    case CrossPointSettings::SHORT_PWRBTN::TOGGLE_FONT:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_CHANGE_FONT);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::TOGGLE_GUIDE_DOTS:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_TOGGLE_GUIDE_DOTS);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::TOGGLE_BIONIC_READING:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_TOGGLE_BIONIC);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::TOGGLE_BOOKMARK:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_TOGGLE_BOOKMARK);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::SYNC_PROGRESS:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_SYNC_PROGRESS);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::MARK_FINISHED:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_MARK_FINISHED);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::READING_STATS:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_READING_STATS);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::SCREENSHOT:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_SCREENSHOT);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::CYCLE_PAGE_TURN:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_CYCLE_PAGE_TURN);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::FILE_TRANSFER:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_FILE_TRANSFER);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::CALIBRE_WIRELESS:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_CALIBRE_WIRELESS);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::JOIN_NETWORK:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_JOIN_NETWORK);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::CREATE_HOTSPOT:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_CREATE_HOTSPOT);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::TOGGLE_TILT_PAGE_TURN:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_TOGGLE_TILT_PAGE_TURN);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::TOGGLE_DARK_MODE:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_TOGGLE_DARK_MODE);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::FOOTNOTES:
      executeFootnoteQuickAction();
      return true;
    case CrossPointSettings::SHORT_PWRBTN::FILE_BROWSER:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_FILE_BROWSER);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::CREATE_CLIPPING:
      mappedInput.suppressNextPowerConfirmRelease();
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_CREATE_CLIPPING);
      return true;
    default:
      return false;
  }
}

bool EpubReaderActivity::consumeLongPowerButtonRelease() {
  if (!longPowerButtonHandled) {
    return false;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Power) ||
      !mappedInput.isPressed(MappedInputManager::Button::Power)) {
    longPowerButtonHandled = false;
    return true;
  }

  return false;
}

bool EpubReaderActivity::consumeLongPowerButtonHold() {
  if (longPowerButtonHandled || !mappedInput.isPressed(MappedInputManager::Button::Power) ||
      mappedInput.getHeldTime() < SETTINGS.getPowerButtonLongPressDuration()) {
    return false;
  }

  longPowerButtonHandled = true;
  return true;
}

bool EpubReaderActivity::executeLongPowerButtonAction() {
  if (SETTINGS.longPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN || !consumeLongPowerButtonHold()) {
    return false;
  }

  switch (SETTINGS.longPwrBtn) {
    case CrossPointSettings::SHORT_PWRBTN::TOGGLE_FONT:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_CHANGE_FONT);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::TOGGLE_GUIDE_DOTS:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_TOGGLE_GUIDE_DOTS);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::TOGGLE_BIONIC_READING:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_TOGGLE_BIONIC);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::TOGGLE_BOOKMARK:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_TOGGLE_BOOKMARK);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::SYNC_PROGRESS:
      mappedInput.suppressNextPowerConfirmRelease();
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_SYNC_PROGRESS);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::MARK_FINISHED:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_MARK_FINISHED);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::READING_STATS:
      mappedInput.suppressNextPowerConfirmRelease();
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_READING_STATS);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::SCREENSHOT:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_SCREENSHOT);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::CYCLE_PAGE_TURN:
      mappedInput.suppressNextPowerConfirmRelease();
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_CYCLE_PAGE_TURN);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::FILE_TRANSFER:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_FILE_TRANSFER);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::CALIBRE_WIRELESS:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_CALIBRE_WIRELESS);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::JOIN_NETWORK:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_JOIN_NETWORK);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::CREATE_HOTSPOT:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_CREATE_HOTSPOT);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::TOGGLE_TILT_PAGE_TURN:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_TOGGLE_TILT_PAGE_TURN);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::TOGGLE_DARK_MODE:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_TOGGLE_DARK_MODE);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::FOOTNOTES:
      executeFootnoteQuickAction(/*suppressInitialPowerRelease=*/true);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::FILE_BROWSER:
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_FILE_BROWSER);
      return true;
    case CrossPointSettings::SHORT_PWRBTN::CREATE_CLIPPING:
      mappedInput.suppressNextPowerConfirmRelease();
      executeReaderQuickAction(CrossPointSettings::LONG_MENU_CREATE_CLIPPING);
      return true;
    default:
      return false;
  }
}

void EpubReaderActivity::suppressPowerShortcutRelease() {
  mappedInput.suppressNextPowerRelease();
  mappedInput.suppressNextPowerConfirmRelease();
}

void EpubReaderActivity::setBookCompleted(bool isCompleted) {
  if (stats.isCompleted == isCompleted) {
    return;
  }

  ReadingStatsDate completionDate = stats.finishedDate;
  stats.isCompleted = isCompleted;
  if (isCompleted && !stats.finishedDateManual) {
    ReadingStatsDateTime now;
    if (getCurrentLocalReadingStatsDateTime(now)) {
      stats.finishedDate = now.date;
      completionDate = now.date;
    }
  }
  if (isCompleted) {
    completionPromptShown = true;
    if (SETTINGS.removeReadBooksFromRecents) {
      RECENT_BOOKS.removeByPath(epub->getPath());
    }
    if (SETTINGS.moveFinishedToReadFolder && !isInReadFolder(epub->getPath())) {
      pendingReadFolderMove = true;
    }
  } else {
    if (SETTINGS.removeReadBooksFromRecents) {
      RECENT_BOOKS.addOrUpdateBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), epub->getThumbBmpPath());
    }
    recentsEntryRemoved = false;
    pendingReadFolderMove = false;
  }
  const bool includeInAggregateStats = shouldTrackReadingStatsForPath(epub->getPath());
  if (includeInAggregateStats) {
    if (isCompleted) {
      globalStats.completedBooks++;
    } else if (globalStats.completedBooks > 0) {
      globalStats.completedBooks--;
    }
  }

  refreshCachedTimeLeftEstimate();
  stats.save(epub->getCachePath());
  if (includeInAggregateStats) globalStats.save();
  if (includeInAggregateStats && completionDate.isValid()) {
    ReadingJournal::adjustCompletion(completionDate, isCompleted ? 1 : -1);
  }
}

void EpubReaderActivity::showCompletedFeedback(bool isCompleted) {
  completedFeedbackIsFinished = isCompleted;
  pendingCompletedFeedback = true;
  completedFeedbackShowTime = millis();
}

void EpubReaderActivity::showTiltPageTurnFeedback(bool enabled) {
  tiltPageTurnFeedbackEnabled = enabled;
  pendingTiltPageTurnFeedback = true;
  tiltPageTurnFeedbackShowTime = millis();
}

void EpubReaderActivity::showBionicFeedback(const uint8_t mode) {
  bionicFeedbackMode = BionicReadingMode::normalize(mode);
  pendingBionicFeedback = true;
  bionicFeedbackShowTime = millis();
}

void EpubReaderActivity::showRenderModeToast(const uint8_t renderMode) {
  if (normalizeRenderMode(renderMode) == EpubRenderMode::CrossInkDefault) {
    return;
  }
  renderModeToastMode = normalizeRenderModeRaw(renderMode);
  pendingRenderModeToast = true;
  pendingSafeModeToast = false;
  renderModeToastShown = true;
  renderModeToastShowTime = millis();
}

void EpubReaderActivity::showSafeModeToast() {
  pendingSafeModeToast = true;
  pendingRenderModeToast = false;
  safeModeToastShown = true;
  renderModeToastShown = true;
  renderModeToastShowTime = millis();
}

bool EpubReaderActivity::storeRenderModeToastRegion(const char* msg) {
  renderModeToastRegionSaved = false;
  const ToastRect toast = computeToastRect(renderer, msg);
  const size_t needed = renderer.getRegionByteSize(toast.x, toast.y, toast.w, toast.h);
  if (needed == 0) {
    return false;
  }
  if (!renderModeToastRegionBuffer || renderModeToastRegionBufferSize < needed) {
    renderModeToastRegionBuffer = makeUniqueNoThrow<uint8_t[]>(needed);
    if (!renderModeToastRegionBuffer) {
      renderModeToastRegionBufferSize = 0;
      LOG_ERR("ERS", "OOM: render-mode toast region backup (%u bytes)", static_cast<unsigned>(needed));
      return false;
    }
    renderModeToastRegionBufferSize = needed;
  }
  if (!renderer.copyRegionToBuffer(toast.x, toast.y, toast.w, toast.h, renderModeToastRegionBuffer.get(),
                                   renderModeToastRegionBufferSize)) {
    return false;
  }
  renderModeToastRegionX = toast.x;
  renderModeToastRegionY = toast.y;
  renderModeToastRegionW = toast.w;
  renderModeToastRegionH = toast.h;
  renderModeToastRegionSaved = true;
  return true;
}

void EpubReaderActivity::drawRenderModeToastBuffer(const char* msg) {
  storeRenderModeToastRegion(msg);
  drawToastBuffer(renderer, msg);
}

bool EpubReaderActivity::restoreRenderModeToastRegion() {
  if (!renderModeToastRegionSaved || !renderModeToastRegionBuffer) {
    return false;
  }
  const bool restored = renderer.copyBufferToRegion(renderModeToastRegionX, renderModeToastRegionY,
                                                    renderModeToastRegionW, renderModeToastRegionH,
                                                    renderModeToastRegionBuffer.get(), renderModeToastRegionBufferSize);
  renderModeToastRegionSaved = false;
  renderModeToastRegionBuffer.reset();
  renderModeToastRegionBufferSize = 0;
  if (!restored) {
    return false;
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  return true;
}

void EpubReaderActivity::applyOrientation(const uint8_t orientation) {
  const auto targetOrientation = ReaderUtils::toRendererOrientation(orientation);
  const bool settingsChanged = SETTINGS.orientation != orientation;
  const bool rendererChanged = renderer.getOrientation() != targetOrientation;

  // No-op only when both the persisted setting and the live renderer already match.
  if (!settingsChanged && !rendererChanged) {
    return;
  }

  {
    RenderLock lock(*this);

    // Preserve current reading position only when we need a live re-layout.
    if (rendererChanged && section) {
      if (activeRelayoutPreview) {
        cacheRelayoutPreviewPosition();
      } else {
        cacheCurrentSectionPosition();
      }
    }

    if (settingsChanged) {
      // Persist the selection for this book without changing global reader defaults.
      SETTINGS.orientation = orientation;
      saveCurrentBookReaderSettings();
    }

    if (rendererChanged) {
      // Update renderer orientation to match the new logical coordinate system.
      renderer.setOrientation(targetOrientation);

      // Reset section to force re-layout in the new orientation.
      section.reset();
    }
  }
}

uint16_t EpubReaderActivity::getAutoPageTurnIntervalSeconds() const {
  if (lastAutoPageTurnIntervalSeconds == 0) {
    return DEFAULT_AUTO_PAGE_TURN_INTERVAL_S;
  }
  return clampAutoPageTurnIntervalSeconds(lastAutoPageTurnIntervalSeconds);
}

void EpubReaderActivity::setAutoPageTurnIntervalSeconds(uint16_t seconds) {
  if (seconds == 0) {
    automaticPageTurnActive = false;
    return;
  }

  seconds = clampAutoPageTurnIntervalSeconds(seconds);
  lastAutoPageTurnIntervalSeconds = seconds;
  bookHasAutoPageTurnInterval = true;
  if (epub) {
    ReaderSettingsSnapshot snapshot;
    captureReaderSettings(snapshot);
    saveBookReaderSettingsFile(epub->getCachePath(), bookHasAutoPageTurnInterval, seconds, bookHasCustomReaderSettings,
                               bookHasRenderModeOverride, SETTINGS.epubRenderMode, snapshot);
  }
  lastPageTurnTime = millis();
  pageTurnDuration = static_cast<unsigned long>(seconds) * 1000UL;
  automaticPageTurnActive = true;

  const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  // resets cached section so that space is reserved for auto page turn indicator when None or progress bar only
  if (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight()) {
    // Preserve current reading position so we can restore after reflow.
    RenderLock lock(*this);
    if (section) {
      cacheCurrentSectionPosition();
    }
    section.reset();
  }
}

bool EpubReaderActivity::pageTurn(bool isForwardTurn, const char* source) {
  RenderLock lock(RenderLock::AcquireMode::Try);
  if (!lock.ownsLock() || !section) {
    return false;
  }

  pageLoadRetryCount = 0;
  if (activeFootnotePreview) {
    if (isForwardTurn) {
      if (section && section->currentPage < section->pageCount - 1) {
        section->currentPage++;
      }
    } else {
      armReadingPaceWarmup("preview_back_page");
      if (section && section->currentPage > 0) {
        section->currentPage--;
      }
    }
    lastPageTurnTime = millis();
    requestUpdate();
    return true;
  }
  if (activeRelayoutPreview) {
    recordCurrentPageReadingTime(source);
    bool moved = false;
    if (isForwardTurn && section && section->currentPage < section->pageCount - 1) {
      section->currentPage++;
      moved = true;
      relayoutPreviewUserMoved = true;
      if (sessionScreenPages < UINT16_MAX) sessionScreenPages++;
    } else if (!isForwardTurn && section && section->currentPage > 0) {
      section->currentPage--;
      moved = true;
      relayoutPreviewUserMoved = true;
      armReadingPaceWarmup("relayout_preview_back");
    } else if (!isForwardTurn && section && section->currentPage <= 0 && cachedPageParagraphIndex > 0) {
      const uint16_t stepBack = std::min<uint16_t>(cachedPageParagraphIndex, RELAYOUT_PREVIEW_MAX_PAGES);
      cachedPageParagraphIndex -= stepBack;
      cachedPageParagraphOffset = RELAYOUT_PREVIEW_MAX_PAGES - 1;
      cachedPageParagraphSpan = RELAYOUT_PREVIEW_MAX_PAGES;
      cachedRelayoutAtChapterStart = cachedPageParagraphIndex == 0;
      pendingRelayoutReposition = true;
      pendingRelayoutPreview = true;
      pendingRelayoutPreviewFromBack = true;
      relayoutPreviewUserMoved = true;
      activeRelayoutPreview = false;
      activeChapterPreview = false;
      relayoutBuildCancelRequested.store(true, std::memory_order_release);
      relayoutBuildRetired.store(false, std::memory_order_release);
      relayoutBuildEligibleAfterMs.store(millis() + RELAYOUT_BUILD_IDLE_MS, std::memory_order_release);
      section->clearCache();
      section.reset();
      moved = true;
      armReadingPaceWarmup("relayout_preview_back_window");
    } else if (!isForwardTurn && section && section->currentPage <= 0 && currentSpineIndex > 0) {
      relayoutBuildCancelRequested.store(true, std::memory_order_release);
      relayoutBuildRetired.store(true, std::memory_order_release);
      activeRelayoutPreview = false;
      activeChapterPreview = false;
      pendingRelayoutPreview = false;
      pendingRelayoutPreviewFromBack = false;
      relayoutPreviewUserMoved = false;
      pendingRelayoutReposition = false;
      cachedRelayoutAtChapterStart = false;
      cachedChapterTotalPageCount = 0;
      cachedPageParagraphIndex = UINT16_MAX;
      currentSpineIndex--;
      nextPageNumber = 0;
      pendingPageJump = std::numeric_limits<uint16_t>::max();
      section.reset();
      armReadingPaceWarmup("relayout_preview_prev_chapter");
      lastPageTurnTime = millis();
      requestUpdate();
      return true;
    }
    if (moved) {
      cacheRelayoutPreviewPosition();
    } else {
      relayoutBuildEligibleAfterMs.store(millis(), std::memory_order_release);
      if (isForwardTurn && activeChapterPreview && !relayoutBuildActive.load(std::memory_order_acquire)) {
        relayoutBuildCancelRequested.store(false, std::memory_order_release);
        relayoutBuildLowHeapAbort.store(false, std::memory_order_release);
        relayoutBuildWorkRequested.store(true, std::memory_order_release);
      }
    }
    lastPageTurnTime = millis();
    requestUpdate();
    return true;
  }
  if (isForwardTurn) {
    uint32_t forwardReadSeconds = 0;
    const bool shouldRecordForwardRead = forwardPageReadElapsed(forwardReadSeconds, source);
    recordCurrentPageReadingTime(source);
    const bool exitingChapter = section && section->pageCount > 0 && section->currentPage >= section->pageCount - 1;
    bool advancedForward = false;
    if (section->currentPage < section->pageCount - 1) {
      section->currentPage++;
      advancedForward = true;
    } else {
      if (shouldQueueCompletionPromptOnChapterExit()) {
        completionPromptQueued = true;
        requestUpdate();
        return true;
      }

      nextPageNumber = 0;
      currentSpineIndex++;
      section.reset();
      advancedForward = true;
    }
    if (advancedForward && sessionScreenPages < UINT16_MAX) {
      sessionScreenPages++;
    }
    if (shouldRecordForwardRead) {
      if (!exitingChapter) {
        recordForwardPagePaceSample(forwardReadSeconds, source);
      }
      if (stats.totalPagesTurned < UINT32_MAX) stats.totalPagesTurned++;
      if (globalStats.totalPagesTurned < UINT32_MAX) globalStats.totalPagesTurned++;
    }
  } else {
    recordCurrentPageReadingTime(source);
    armReadingPaceWarmup("back_page");
    if (section->currentPage > 0) {
      section->currentPage--;
    } else if (currentSpineIndex > 0) {
      nextPageNumber = 0;
      pendingPageJump = std::numeric_limits<uint16_t>::max();
      currentSpineIndex--;
      section.reset();
    }
  }
  lastPageTurnTime = millis();
  requestUpdate();
  return true;
}

void EpubReaderActivity::deferPageTurn(const bool isForwardTurn) {
  if (deferredPageTurnDirection != 0) return;
  deferredPageTurnDirection = isForwardTurn ? 1 : -1;
  LOG_DBG("ERS", "Deferred one %s page turn while layout/render lock was busy", isForwardTurn ? "forward" : "back");
}

bool EpubReaderActivity::consumeDeferredPageTurn() {
  if (deferredPageTurnDirection == 0 || anyReaderButtonHeld() || RenderLock::peek() || !section) {
    return false;
  }

  const int8_t direction = deferredPageTurnDirection;
  if (!pageTurn(direction > 0, "deferred")) {
    return false;
  }
  deferredPageTurnDirection = 0;
  LOG_DBG("ERS", "Applied deferred %s page turn", direction > 0 ? "forward" : "back");
  return true;
}

bool EpubReaderActivity::anyReaderButtonHeld() const {
  for (size_t i = 0; i < MappedInputManager::BUTTON_COUNT; ++i) {
    if (mappedInput.isPressed(static_cast<MappedInputManager::Button>(i))) return true;
  }
  return false;
}

void EpubReaderActivity::cancelRelayoutBuildForInput() {
  if (activeRelayoutPreview) {
    relayoutBuildEligibleAfterMs.store(millis() + RELAYOUT_BUILD_RETRY_IDLE_MS, std::memory_order_release);
  }
  if (!relayoutBuildActive.load(std::memory_order_acquire) &&
      !relayoutBuildWorkRequested.load(std::memory_order_acquire)) {
    return;
  }
  relayoutBuildCancelRequested.store(true, std::memory_order_release);
}

bool EpubReaderActivity::maybeScheduleRelayoutBuild() {
  if (!activeRelayoutPreview || !epub || !section || relayoutBuildActive.load(std::memory_order_acquire) ||
      relayoutBuildWorkRequested.load(std::memory_order_acquire) ||
      relayoutBuildRetired.load(std::memory_order_acquire) || anyReaderButtonHeld()) {
    return false;
  }

  const uint32_t now = millis();
  const uint32_t eligibleAfter = relayoutBuildEligibleAfterMs.load(std::memory_order_acquire);
  if (static_cast<int32_t>(now - eligibleAfter) < 0) return false;

  const char* completionLabel = activeChapterPreview ? "new chapter completion" : "font relayout completion";
  if (!MemoryBudget::hasHeapForOptionalEpubRebuild("ERS", completionLabel, currentSpineIndex)) {
    relayoutBuildEligibleAfterMs.store(now + RELAYOUT_BUILD_LOW_HEAP_RETRY_MS, std::memory_order_release);
    return false;
  }

  relayoutBuildCancelRequested.store(false, std::memory_order_release);
  relayoutBuildLowHeapAbort.store(false, std::memory_order_release);
  relayoutBuildWorkRequested.store(true, std::memory_order_release);
  requestUpdate();
  return true;
}

bool EpubReaderActivity::shouldCancelRelayoutBuild(void* context) {
  auto* self = static_cast<EpubReaderActivity*>(context);
  if (!self) return true;
  if (self->relayoutBuildCancelRequested.load(std::memory_order_acquire)) return true;

#ifdef SIMULATOR
  if (self->activeChapterPreview && !self->relayoutBuildSafeModePending.load(std::memory_order_acquire) &&
      std::getenv("CROSSINK_SIMULATOR_FORCE_CHAPTER_COMPLETION_LOW_MEMORY") != nullptr) {
    self->relayoutBuildLowHeapAbort.store(true, std::memory_order_release);
    LOG_DBG("ERS", "Simulator forced low-memory chapter completion cancellation");
    return true;
  }
#endif

  const uint32_t now = millis();
  if (static_cast<uint32_t>(now - self->relayoutBuildLastHeapCheckMs) < RELAYOUT_BUILD_HEAP_CHECK_MS) {
    return false;
  }
  self->relayoutBuildLastHeapCheckMs = now;
  const auto heap = MemoryBudget::snapshot();
  if (MemoryBudget::hasHeap(heap, MemoryBudget::BACKGROUND_EPUB_BUILD_ABORT_MIN_FREE,
                            MemoryBudget::BACKGROUND_EPUB_BUILD_ABORT_MIN_MAX_ALLOC)) {
    return false;
  }

  self->relayoutBuildLowHeapAbort.store(true, std::memory_order_release);
  LOG_DBG("ERS", "Cancelling section completion for heap guard: free=%u maxAlloc=%u", heap.freeHeap, heap.maxAllocHeap);
  return true;
}

void EpubReaderActivity::runGuardedRelayoutBuild() {
  relayoutBuildActive.store(true, std::memory_order_release);
  struct ActiveGuard {
    std::atomic<bool>& active;
    ~ActiveGuard() { active.store(false, std::memory_order_release); }
  } activeGuard{relayoutBuildActive};

  if (!activeRelayoutPreview || !epub || !section) return;

  const bool completingChapterPreview = activeChapterPreview;
  const int targetSpine = currentSpineIndex;
  const ReaderViewportLayout layout = computeReaderViewportLayout(renderer, automaticPageTurnActive);
  const EpubRenderMode selectedRenderMode = normalizeRenderMode(SETTINGS.epubRenderMode);
  const int fontId = SETTINGS.getReaderFontId();
  const bool safeModeCompletion = relayoutBuildSafeModePending.load(std::memory_order_acquire);
  const SectionBuildProfile profile =
      safeModeCompletion ? safeModeBuildProfile() : buildProfileForRenderMode(selectedRenderMode);
  const bool safeModeWouldChangeSettings =
      selectedRenderMode != EpubRenderMode::Light || shouldAttemptSafeModeFallback();
  const std::string cacheSuffix = sectionCacheSuffixForLayout(profile.renderMode, fontId);
  auto completedSection = makeUniqueNoThrow<Section>(epub, targetSpine, renderer, cacheSuffix.c_str());
  if (!completedSection) {
    relayoutBuildEligibleAfterMs.store(millis() + RELAYOUT_BUILD_LOW_HEAP_RETRY_MS, std::memory_order_release);
    LOG_ERR("ERS", "Failed to allocate %s completion section for spine %d", profile.label, targetSpine);
    return;
  }

  const auto finishRelayout = [&]() {
    if (profile.safeMode) {
      applySafeModeReaderSettings();
      bookHasCustomReaderSettings = true;
      bookHasRenderModeOverride = true;
      if (!saveRuntimeReaderSettingsForCache(epub->getCachePath())) {
        LOG_ERR("ERS", "Failed to save Safe Mode reader settings after background completion");
      }
      if (!safeModeToastShown) {
        showSafeModeToast();
      }
    }
    if (section && activeRelayoutPreview && (completingChapterPreview || relayoutPreviewUserMoved)) {
      cacheRelayoutPreviewPosition();
      section->clearCache();
    } else if (section && activeRelayoutPreview) {
      section->clearCache();
    }
    section.reset();
    activeRelayoutPreview = false;
    activeChapterPreview = false;
    pendingRelayoutPreview = false;
    pendingRelayoutPreviewFromBack = false;
    relayoutPreviewUserMoved = false;
    relayoutBuildAttempts.store(0, std::memory_order_release);
    relayoutBuildRetired.store(false, std::memory_order_release);
    relayoutBuildSafeModePending.store(false, std::memory_order_release);
    relayoutBuildEligibleAfterMs.store(0, std::memory_order_release);
    requestUpdate();
  };

  if (shouldCancelRelayoutBuild(this)) {
    if (relayoutBuildLowHeapAbort.load(std::memory_order_acquire) && !profile.safeMode && safeModeWouldChangeSettings) {
      relayoutBuildSafeModePending.store(true, std::memory_order_release);
      relayoutBuildAttempts.store(0, std::memory_order_release);
      LOG_DBG("ERS", "%s completion will start in Safe Mode after the pre-build heap guard",
              completingChapterPreview ? "Chapter layout" : "Font relayout");
    }
    relayoutBuildEligibleAfterMs.store(millis() + RELAYOUT_BUILD_RETRY_IDLE_MS, std::memory_order_release);
    return;
  }

  if (completedSection->loadSectionFile(fontId, SETTINGS.getReaderLineCompression(), SETTINGS.extraParagraphSpacing,
                                        SETTINGS.forceParagraphIndents, SETTINGS.paragraphAlignment,
                                        layout.viewportWidth, layout.viewportHeight, SETTINGS.hyphenationEnabled,
                                        profile.embeddedStyle, SETTINGS.imageRendering, profile.bionicReadingEnabled,
                                        profile.guideReadingEnabled, profile.renderMode)) {
    if (completingChapterPreview) {
      if (timingChapterCacheHitCount < UINT16_MAX) timingChapterCacheHitCount++;
      LOG_DBG("ERS", "Chapter layout cache ready: spine=%d pages=%u mode=%s", targetSpine, completedSection->pageCount,
              profile.label);
    } else {
      if (timingRelayoutCacheHitCount < UINT16_MAX) timingRelayoutCacheHitCount++;
      LOG_DBG("ERS", "Font relayout cache ready: spine=%d pages=%u mode=%s", targetSpine, completedSection->pageCount,
              profile.label);
    }
    finishRelayout();
    return;
  }

  relayoutBuildAttempts.fetch_add(1, std::memory_order_acq_rel);
  relayoutBuildLastHeapCheckMs = 0;
  bool imagesWereSuppressed = false;
  bool layoutAbortedForLowMemory = false;
  bool cancelledByCaller = false;
  SectionBuildOptions buildOptions;
  buildOptions.shouldCancel = &EpubReaderActivity::shouldCancelRelayoutBuild;
  buildOptions.cancelContext = this;
  buildOptions.cancelledByCaller = &cancelledByCaller;
  // The visible preview already warmed the glyphs needed for the current page.
  // Whole-chapter prewarming loads every style before layout and can exhaust the
  // smaller readers before the cancellable background pass makes useful progress.
  buildOptions.skipFontPrewarm = true;
  const uint32_t buildStart = millis();
  LOG_DBG("ERS", "%s completion start: spine=%d font=%d mode=%s free=%u maxAlloc=%u",
          completingChapterPreview ? "Chapter layout" : "Font relayout", targetSpine, fontId, profile.label,
          ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  const bool built = completedSection->createSectionFile(
      fontId, SETTINGS.getReaderLineCompression(), SETTINGS.extraParagraphSpacing, SETTINGS.forceParagraphIndents,
      SETTINGS.paragraphAlignment, layout.viewportWidth, layout.viewportHeight, SETTINGS.hyphenationEnabled,
      profile.embeddedStyle, SETTINGS.imageRendering, profile.bionicReadingEnabled, profile.guideReadingEnabled,
      nullptr, &imagesWereSuppressed, &layoutAbortedForLowMemory, profile.renderMode, buildOptions);

  if (cancelledByCaller || relayoutBuildCancelRequested.load(std::memory_order_acquire)) {
    const uint32_t elapsed = millis() - buildStart;
    if (completingChapterPreview) {
      timingLastChapterCompletionMs = elapsed;
      timingMaxChapterCompletionMs = std::max(timingMaxChapterCompletionMs, elapsed);
      if (timingChapterCompletionCancelCount < UINT16_MAX) timingChapterCompletionCancelCount++;
    } else {
      timingLastRelayoutCompletionMs = elapsed;
      timingMaxRelayoutCompletionMs = std::max(timingMaxRelayoutCompletionMs, elapsed);
      if (timingRelayoutCompletionCancelCount < UINT16_MAX) timingRelayoutCompletionCancelCount++;
    }
    const uint32_t retryMs = relayoutBuildLowHeapAbort.load(std::memory_order_acquire)
                                 ? RELAYOUT_BUILD_LOW_HEAP_RETRY_MS
                                 : RELAYOUT_BUILD_RETRY_IDLE_MS;
    if (relayoutBuildLowHeapAbort.load(std::memory_order_acquire) && !profile.safeMode && safeModeWouldChangeSettings) {
      relayoutBuildSafeModePending.store(true, std::memory_order_release);
      relayoutBuildAttempts.store(0, std::memory_order_release);
      LOG_DBG("ERS", "%s completion will retry with Safe Mode after low-heap cancellation",
              completingChapterPreview ? "Chapter layout" : "Font relayout");
    }
    relayoutBuildEligibleAfterMs.store(millis() + retryMs, std::memory_order_release);
    LOG_DBG("ERS", "%s completion interrupted after %lu ms",
            completingChapterPreview ? "Chapter layout" : "Font relayout", millis() - buildStart);
    return;
  }

  if (!built) {
    const uint32_t elapsed = millis() - buildStart;
    if (completingChapterPreview) {
      timingLastChapterCompletionMs = elapsed;
      timingMaxChapterCompletionMs = std::max(timingMaxChapterCompletionMs, elapsed);
    } else {
      timingLastRelayoutCompletionMs = elapsed;
      timingMaxRelayoutCompletionMs = std::max(timingMaxRelayoutCompletionMs, elapsed);
    }
    const uint8_t attempts = relayoutBuildAttempts.load(std::memory_order_acquire);
    LOG_DBG("ERS", "%s completion failed: spine=%d attempt=%u lowMemory=%u",
            completingChapterPreview ? "Chapter layout" : "Font relayout", targetSpine, attempts,
            layoutAbortedForLowMemory ? 1U : 0U);
    if (layoutAbortedForLowMemory && !profile.safeMode && safeModeWouldChangeSettings) {
      completedSection->clearCache();
      releaseReaderSdFontCachesForLowMemory(renderer, "ERS", "safe mode background completion");
      relayoutBuildSafeModePending.store(true, std::memory_order_release);
      relayoutBuildAttempts.store(0, std::memory_order_release);
      relayoutBuildEligibleAfterMs.store(millis() + RELAYOUT_BUILD_LOW_HEAP_RETRY_MS, std::memory_order_release);
      LOG_ERR("ERS", "%s completion exhausted the selected profile; retrying once memory recovers with Safe Mode",
              completingChapterPreview ? "Chapter layout" : "Font relayout");
      requestUpdate();
      return;
    }
    if (attempts >= RELAYOUT_BUILD_MAX_ATTEMPTS) {
      completedSection->clearCache();
      relayoutBuildRetired.store(true, std::memory_order_release);
      relayoutBuildEligibleAfterMs.store(0, std::memory_order_release);
      LOG_ERR("ERS", "%s %s completion retired after %u attempts; keeping readable preview",
              completingChapterPreview ? "Chapter layout" : "Font relayout", profile.label, attempts);
      requestUpdate();
      return;
    }
    relayoutBuildEligibleAfterMs.store(
        millis() + (layoutAbortedForLowMemory ? RELAYOUT_BUILD_LOW_HEAP_RETRY_MS : RELAYOUT_BUILD_RETRY_IDLE_MS),
        std::memory_order_release);
    return;
  }

  const uint32_t elapsed = millis() - buildStart;
  if (completingChapterPreview) {
    timingLastChapterCompletionMs = elapsed;
    timingMaxChapterCompletionMs = std::max(timingMaxChapterCompletionMs, elapsed);
    if (timingChapterCompletionCount < UINT16_MAX) timingChapterCompletionCount++;
  } else {
    timingLastRelayoutCompletionMs = elapsed;
    timingMaxRelayoutCompletionMs = std::max(timingMaxRelayoutCompletionMs, elapsed);
    if (timingRelayoutCompletionCount < UINT16_MAX) timingRelayoutCompletionCount++;
  }
  LOG_DBG("ERS", "%s completion ready: spine=%d pages=%u time=%lu ms free=%u maxAlloc=%u",
          completingChapterPreview ? "Chapter layout" : "Font relayout", targetSpine, completedSection->pageCount,
          static_cast<unsigned long>(elapsed), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  finishRelayout();
}

void EpubReaderActivity::cancelNextChapterPreindexForInput() {
  if (!nextChapterPreindexActive.load(std::memory_order_acquire) &&
      !nextChapterPreindexWorkRequested.load(std::memory_order_acquire)) {
    return;
  }
  nextChapterPreindexCancelRequested.store(true, std::memory_order_release);
  nextChapterPreindexEligibleAfterMs.store(millis() + NEXT_CHAPTER_PREINDEX_RETRY_IDLE_MS, std::memory_order_release);
}

uint32_t EpubReaderActivity::nextChapterPreindexSettingsKey(const uint16_t viewportWidth,
                                                            const uint16_t viewportHeight) const {
  uint32_t hash = 2166136261UL;
  const auto mix = [&hash](const void* data, const size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
      hash ^= bytes[i];
      hash *= 16777619UL;
    }
  };
  const int fontId = SETTINGS.getReaderFontId();
  const float lineCompression = SETTINGS.getReaderLineCompression();
  const EpubRenderMode renderMode = normalizeRenderMode(SETTINGS.epubRenderMode);
  mix(&fontId, sizeof(fontId));
  mix(&lineCompression, sizeof(lineCompression));
  mix(&viewportWidth, sizeof(viewportWidth));
  mix(&viewportHeight, sizeof(viewportHeight));
  mix(&SETTINGS.extraParagraphSpacing, sizeof(SETTINGS.extraParagraphSpacing));
  mix(&SETTINGS.forceParagraphIndents, sizeof(SETTINGS.forceParagraphIndents));
  mix(&SETTINGS.paragraphAlignment, sizeof(SETTINGS.paragraphAlignment));
  mix(&SETTINGS.hyphenationEnabled, sizeof(SETTINGS.hyphenationEnabled));
  mix(&SETTINGS.embeddedStyle, sizeof(SETTINGS.embeddedStyle));
  mix(&SETTINGS.imageRendering, sizeof(SETTINGS.imageRendering));
  mix(&SETTINGS.bionicReadingEnabled, sizeof(SETTINGS.bionicReadingEnabled));
  mix(&SETTINGS.guideReadingEnabled, sizeof(SETTINGS.guideReadingEnabled));
  mix(&renderMode, sizeof(renderMode));
  return hash;
}

void EpubReaderActivity::armNextChapterPreindex(const uint16_t viewportWidth, const uint16_t viewportHeight) {
  const int pagesRemaining =
      section && section->pageCount > 0 ? static_cast<int>(section->pageCount) - section->currentPage - 1 : INT_MAX;
  const int lookaheadPages = section ? std::min<int>(NEXT_CHAPTER_PREINDEX_MAX_LOOKAHEAD_PAGES,
                                                     std::max<int>(2, static_cast<int>(section->pageCount) / 4))
                                     : 0;
  if (!epub || !section || activeFootnotePreview || automaticPageTurnActive || section->pageCount < 2 ||
      pagesRemaining > lookaheadPages) {
    if (!nextChapterPreindexActive.load(std::memory_order_acquire)) {
      nextChapterPreindexCandidateSpine.store(-1, std::memory_order_release);
    }
    return;
  }

  const int targetSpine = currentSpineIndex + 1;
  if (targetSpine < 0 || targetSpine >= epub->getSpineItemsCount()) {
    nextChapterPreindexCandidateSpine.store(-1, std::memory_order_release);
    return;
  }

  const uint32_t settingsKey = nextChapterPreindexSettingsKey(viewportWidth, viewportHeight);
  if (nextChapterPreindexFinishedSpine.load(std::memory_order_acquire) == targetSpine &&
      nextChapterPreindexFinishedKey.load(std::memory_order_acquire) == settingsKey) {
    nextChapterPreindexCandidateSpine.store(-1, std::memory_order_release);
    return;
  }

  const int existingTarget = nextChapterPreindexCandidateSpine.load(std::memory_order_acquire);
  const uint32_t existingKey = nextChapterPreindexCandidateKey.load(std::memory_order_acquire);
  if (existingTarget == targetSpine && existingKey == settingsKey) return;

  nextChapterPreindexAttempts.store(0, std::memory_order_relaxed);
  nextChapterPreindexCandidateKey.store(settingsKey, std::memory_order_relaxed);
  nextChapterPreindexEligibleAfterMs.store(millis() + NEXT_CHAPTER_PREINDEX_IDLE_MS, std::memory_order_relaxed);
  nextChapterPreindexCandidateSpine.store(targetSpine, std::memory_order_release);
  LOG_DBG("ERS", "Armed guarded next-chapter preindex: spine=%d", targetSpine);
}

bool EpubReaderActivity::maybeScheduleNextChapterPreindex() {
  const int targetSpine = nextChapterPreindexCandidateSpine.load(std::memory_order_acquire);
  if (targetSpine < 0 || targetSpine != currentSpineIndex + 1 || !epub || !section || activeFootnotePreview ||
      automaticPageTurnActive || deferredOnEnterPending || completionPromptQueued || pendingScreenshot ||
      nextChapterPreindexActive.load(std::memory_order_acquire) ||
      nextChapterPreindexWorkRequested.load(std::memory_order_acquire) || anyReaderButtonHeld() || RenderLock::peek()) {
    return false;
  }

  const uint32_t now = millis();
  const uint32_t eligibleAfter = nextChapterPreindexEligibleAfterMs.load(std::memory_order_acquire);
  if (static_cast<int32_t>(now - eligibleAfter) < 0) return false;

  if (!MemoryBudget::hasHeapForOptionalEpubRebuild("ERS", "guarded next-chapter preindex", targetSpine)) {
    if (timingPreindexSkipHeapCount < UINT16_MAX) timingPreindexSkipHeapCount++;
    nextChapterPreindexEligibleAfterMs.store(now + NEXT_CHAPTER_PREINDEX_LOW_HEAP_RETRY_MS, std::memory_order_release);
    return false;
  }

  nextChapterPreindexCancelRequested.store(false, std::memory_order_release);
  nextChapterPreindexLowHeapAbort.store(false, std::memory_order_release);
  nextChapterPreindexWorkRequested.store(true, std::memory_order_release);
  requestUpdate();
  return true;
}

bool EpubReaderActivity::shouldCancelNextChapterPreindex(void* context) {
  auto* self = static_cast<EpubReaderActivity*>(context);
  if (!self) return true;
  if (self->nextChapterPreindexCancelRequested.load(std::memory_order_acquire)) return true;

  const uint32_t now = millis();
  if (static_cast<uint32_t>(now - self->nextChapterPreindexLastHeapCheckMs) < NEXT_CHAPTER_PREINDEX_HEAP_CHECK_MS) {
    return false;
  }
  self->nextChapterPreindexLastHeapCheckMs = now;
  const auto heap = MemoryBudget::snapshot();
  if (MemoryBudget::hasHeap(heap, MemoryBudget::BACKGROUND_EPUB_BUILD_ABORT_MIN_FREE,
                            MemoryBudget::BACKGROUND_EPUB_BUILD_ABORT_MIN_MAX_ALLOC)) {
    return false;
  }

  self->nextChapterPreindexLowHeapAbort.store(true, std::memory_order_release);
  LOG_DBG("ERS", "Cancelling next-chapter preindex for heap guard: free=%u maxAlloc=%u", heap.freeHeap,
          heap.maxAllocHeap);
  return true;
}

void EpubReaderActivity::runGuardedNextChapterPreindex() {
  const int targetSpine = nextChapterPreindexCandidateSpine.load(std::memory_order_acquire);
  const uint32_t settingsKey = nextChapterPreindexCandidateKey.load(std::memory_order_acquire);
  const uint32_t preindexStartMs = millis();
  enum class PreindexOutcome : uint8_t { Complete, Cancelled, LowHeap, Failed };
  bool preindexTimingRecorded = false;
  const auto recordPreindexOutcome = [&](const PreindexOutcome outcome) {
    if (preindexTimingRecorded) return;
    preindexTimingRecorded = true;
    timingLastPreindexMs = millis() - preindexStartMs;
    timingMaxPreindexMs = std::max(timingMaxPreindexMs, timingLastPreindexMs);
    switch (outcome) {
      case PreindexOutcome::Complete:
        if (timingPreindexCompleteCount < UINT16_MAX) timingPreindexCompleteCount++;
        break;
      case PreindexOutcome::Cancelled:
        if (timingPreindexCancelCount < UINT16_MAX) timingPreindexCancelCount++;
        break;
      case PreindexOutcome::LowHeap:
        if (timingPreindexSkipHeapCount < UINT16_MAX) timingPreindexSkipHeapCount++;
        break;
      case PreindexOutcome::Failed:
        if (timingPreindexFailCount < UINT16_MAX) timingPreindexFailCount++;
        break;
    }
  };
  nextChapterPreindexActive.store(true, std::memory_order_release);
  struct ActiveGuard {
    std::atomic<bool>& active;
    ~ActiveGuard() { active.store(false, std::memory_order_release); }
  } activeGuard{nextChapterPreindexActive};

  const auto retireAttempt = [&]() {
    nextChapterPreindexFinishedKey.store(settingsKey, std::memory_order_relaxed);
    nextChapterPreindexFinishedSpine.store(targetSpine, std::memory_order_release);
    nextChapterPreindexCandidateSpine.store(-1, std::memory_order_release);
  };
  const auto retryOrRetireAttempt = [&](const bool lowHeap) {
    const uint8_t attempts = nextChapterPreindexAttempts.load(std::memory_order_acquire);
    if (attempts < NEXT_CHAPTER_PREINDEX_MAX_ATTEMPTS) {
      nextChapterPreindexEligibleAfterMs.store(
          millis() + (lowHeap ? NEXT_CHAPTER_PREINDEX_LOW_HEAP_RETRY_MS : NEXT_CHAPTER_PREINDEX_RETRY_IDLE_MS),
          std::memory_order_release);
      LOG_DBG("ERS", "Next-chapter preindex retry armed: spine=%d attempt=%u lowHeap=%u", targetSpine, attempts,
              lowHeap ? 1U : 0U);
    } else {
      retireAttempt();
    }
  };

  if (!epub || !section || targetSpine < 0 || targetSpine != currentSpineIndex + 1 ||
      targetSpine >= epub->getSpineItemsCount()) {
    nextChapterPreindexCandidateSpine.store(-1, std::memory_order_release);
    return;
  }

  const ReaderViewportLayout layout = computeReaderViewportLayout(renderer, automaticPageTurnActive);
  if (settingsKey != nextChapterPreindexSettingsKey(layout.viewportWidth, layout.viewportHeight) ||
      shouldCancelNextChapterPreindex(this)) {
    const bool lowHeap = nextChapterPreindexLowHeapAbort.load(std::memory_order_acquire);
    recordPreindexOutcome(lowHeap ? PreindexOutcome::LowHeap : PreindexOutcome::Cancelled);
    retryOrRetireAttempt(lowHeap);
    return;
  }

  nextChapterPreindexAttempts.fetch_add(1, std::memory_order_acq_rel);
  nextChapterPreindexLastHeapCheckMs = 0;
  const EpubRenderMode renderMode = normalizeRenderMode(SETTINGS.epubRenderMode);
  const int fontId = SETTINGS.getReaderFontId();
  const std::string cacheSuffix = sectionCacheSuffixForLayout(renderMode, fontId);
  Section nextSection(epub, targetSpine, renderer, cacheSuffix.c_str());
  if (nextSection.loadSectionFile(fontId, SETTINGS.getReaderLineCompression(), SETTINGS.extraParagraphSpacing,
                                  SETTINGS.forceParagraphIndents, SETTINGS.paragraphAlignment, layout.viewportWidth,
                                  layout.viewportHeight, SETTINGS.hyphenationEnabled, SETTINGS.embeddedStyle,
                                  SETTINGS.imageRendering, SETTINGS.bionicReadingEnabled, SETTINGS.guideReadingEnabled,
                                  renderMode)) {
    LOG_DBG("ERS", "Next-chapter cache already ready: spine=%d pages=%u", targetSpine, nextSection.pageCount);
    recordPreindexOutcome(PreindexOutcome::Complete);
    retireAttempt();
    return;
  }

  if (!MemoryBudget::hasHeapForOptionalEpubRebuild("ERS", "guarded next-chapter preindex", targetSpine)) {
    recordPreindexOutcome(PreindexOutcome::LowHeap);
    retryOrRetireAttempt(true);
    return;
  }

  bool imagesWereSuppressed = false;
  bool layoutAbortedForLowMemory = false;
  bool cancelledByCaller = false;
  SectionBuildOptions buildOptions;
  buildOptions.shouldCancel = &EpubReaderActivity::shouldCancelNextChapterPreindex;
  buildOptions.cancelContext = this;
  buildOptions.cancelledByCaller = &cancelledByCaller;
  // Pre-indexing should prepare the disk cache, not populate every glyph for a
  // chapter the reader has not reached. The visible page warms its own glyphs.
  buildOptions.skipFontPrewarm = true;
  const SectionBuildProfile profile = buildProfileForRenderMode(renderMode);
  LOG_DBG("ERS", "Guarded next-chapter preindex start: spine=%d free=%u maxAlloc=%u", targetSpine, ESP.getFreeHeap(),
          ESP.getMaxAllocHeap());
  const bool built = nextSection.createSectionFile(
      fontId, SETTINGS.getReaderLineCompression(), SETTINGS.extraParagraphSpacing, SETTINGS.forceParagraphIndents,
      SETTINGS.paragraphAlignment, layout.viewportWidth, layout.viewportHeight, SETTINGS.hyphenationEnabled,
      profile.embeddedStyle, SETTINGS.imageRendering, profile.bionicReadingEnabled, profile.guideReadingEnabled,
      nullptr, &imagesWereSuppressed, &layoutAbortedForLowMemory, profile.renderMode, buildOptions);

  if (cancelledByCaller || nextChapterPreindexCancelRequested.load(std::memory_order_acquire)) {
    const bool lowHeap = nextChapterPreindexLowHeapAbort.load(std::memory_order_acquire);
    recordPreindexOutcome(lowHeap ? PreindexOutcome::LowHeap : PreindexOutcome::Cancelled);
    retryOrRetireAttempt(lowHeap);
    return;
  }
  if (!built) {
    LOG_DBG("ERS", "Guarded next-chapter preindex stopped: spine=%d lowMemory=%u", targetSpine,
            layoutAbortedForLowMemory ? 1U : 0U);
    recordPreindexOutcome(layoutAbortedForLowMemory ? PreindexOutcome::LowHeap : PreindexOutcome::Failed);
    if (layoutAbortedForLowMemory) {
      retryOrRetireAttempt(true);
    } else {
      retireAttempt();
    }
    return;
  }

  LOG_DBG("ERS", "Guarded next-chapter preindex complete: spine=%d pages=%u free=%u maxAlloc=%u", targetSpine,
          nextSection.pageCount, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  recordPreindexOutcome(PreindexOutcome::Complete);
  retireAttempt();
}

// TODO: Failure handling
void EpubReaderActivity::render(RenderLock&& lock) {
  if (!epub) {
    return;
  }

  if (relayoutBuildWorkRequested.exchange(false, std::memory_order_acq_rel)) {
    runGuardedRelayoutBuild();
    return;
  }

  if (nextChapterPreindexWorkRequested.exchange(false, std::memory_order_acq_rel)) {
    runGuardedNextChapterPreindex();
    return;
  }

  struct FirstRenderSignal {
    std::atomic<bool>& completed;
    ~FirstRenderSignal() { completed.store(true, std::memory_order_release); }
  } firstRenderSignal{firstRenderCompleted};

  const auto showPendingSyncSaveError = [this]() {
    if (!pendingSyncSaveError) return;
    pendingSyncSaveError = false;
    GUI.drawPopup(renderer, tr(STR_SAVE_PROGRESS_FAILED));
  };

  const auto showLowMemoryLayoutError = [this]() {
    snprintf(APP_STATE.pendingAlertTitle, sizeof(APP_STATE.pendingAlertTitle), "%s", tr(STR_EPUB_LAYOUT_MEMORY_TITLE));
    snprintf(APP_STATE.pendingAlertBody, sizeof(APP_STATE.pendingAlertBody), "%s", tr(STR_EPUB_LAYOUT_MEMORY_BODY));
    APP_STATE.pendingAlertGoHomeOnBack.store(true, std::memory_order_relaxed);
    APP_STATE.hasPendingAlert.store(true, std::memory_order_release);
    GUI.drawPopup(renderer, tr(STR_EPUB_LAYOUT_MEMORY_TITLE));
  };

  // edge case handling for sub-zero spine index
  if (currentSpineIndex < 0) {
    currentSpineIndex = 0;
  }
  // based bounds of book, show end of book screen
  if (currentSpineIndex > epub->getSpineItemsCount()) {
    currentSpineIndex = epub->getSpineItemsCount();
  }

  // Show end of book screen
  if (currentSpineIndex == epub->getSpineItemsCount()) {
    // Sole load site: runs on the render task (serialized by RenderLock); the main
    // task only reads the suggestions once the loaded flag is published
    endOfBookOptions.loadOnce(epub->getPath());
    renderer.clearScreen();
    endOfBookOptions.render(renderer, mappedInput);
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    showPendingSyncSaveError();
    return;
  }

  const ReaderViewportLayout layout = computeReaderViewportLayout(renderer, automaticPageTurnActive);
  const uint16_t viewportWidth = layout.viewportWidth;
  const uint16_t viewportHeight = layout.viewportHeight;

  if (!section) {
    const auto filepath = epub->getSpineItem(currentSpineIndex).href;
    LOG_DBG("ERS", "Loading file: %s, index: %d (free=%u, maxAlloc=%u)", filepath.c_str(), currentSpineIndex,
            ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    const int readerFontId = SETTINGS.getReaderFontId();
    const EpubRenderMode selectedRenderMode = normalizeRenderMode(SETTINGS.epubRenderMode);
    EpubRenderMode usedRenderMode = selectedRenderMode;
    const bool buildingFootnotePreview = !pendingFootnotePreviewAnchor.empty();
    const bool buildingRelayoutPreview =
        !buildingFootnotePreview && pendingRelayoutPreview && cachedPageParagraphIndex != UINT16_MAX;
    const bool chapterPreviewEligible = !buildingFootnotePreview && !buildingRelayoutPreview && nextPageNumber == 0 &&
                                        !pendingPageJump.has_value() && !pendingPercentJump && pendingAnchor.empty() &&
                                        !pendingRelayoutReposition;
    bool buildingChapterPreview = false;
    bool buildingPreview = buildingFootnotePreview || buildingRelayoutPreview;
    bool loadedSection = false;
    bool safeModeBuildSucceeded = false;
    auto loadSectionWithFont = [&](const int fontId, const EpubRenderMode renderMode) {
      const std::string cacheSuffix =
          buildingFootnotePreview   ? footnotePreviewCacheSuffix(renderMode, fontId, pendingFootnotePreviewAnchor)
          : buildingRelayoutPreview ? relayoutPreviewCacheSuffix(renderMode, fontId, cachedPageParagraphIndex)
                                    : sectionCacheSuffixForLayout(renderMode, fontId);
      section = makeUniqueNoThrow<Section>(epub, currentSpineIndex, renderer, cacheSuffix.c_str());
      if (!section) {
        LOG_ERR("ERS", "Failed to allocate section for spine %d (font=%d, free=%u, maxAlloc=%u)", currentSpineIndex,
                fontId, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        return false;
      }
      if (!section->loadSectionFile(fontId, SETTINGS.getReaderLineCompression(), SETTINGS.extraParagraphSpacing,
                                    SETTINGS.forceParagraphIndents, SETTINGS.paragraphAlignment, viewportWidth,
                                    viewportHeight, SETTINGS.hyphenationEnabled, SETTINGS.embeddedStyle,
                                    SETTINGS.imageRendering, SETTINGS.bionicReadingEnabled,
                                    SETTINGS.guideReadingEnabled, renderMode)) {
        section.reset();
        return false;
      }
      activeSectionFontId = fontId;
      usedRenderMode = renderMode;
      return true;
    };

    loadedSection = loadSectionWithFont(readerFontId, selectedRenderMode);
    if (loadedSection && !pendingRelayoutReposition) {
      cachedChapterTotalPageCount = 0;
    }

    if (!loadedSection) {
      LOG_DBG("ERS", "Cache not found, building... (free=%u, maxAlloc=%u)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      if (chapterPreviewEligible) {
        buildingChapterPreview = true;
        buildingPreview = true;
        LOG_DBG("ERS", "Using visible-page-first chapter preview: spine=%d", currentSpineIndex);
      }

      const auto popupFn = [this]() { GUI.drawPopup(renderer, tr(STR_INDEXING)); };

      bool imagesWereSuppressed = false;
      bool layoutAbortedForLowMemory = false;
      bool fallbackBuildSucceeded = false;
      auto buildSectionWithProfile = [&](const int fontId, const SectionBuildProfile& profile) {
        const std::string cacheSuffix =
            buildingFootnotePreview
                ? footnotePreviewCacheSuffix(profile.renderMode, fontId, pendingFootnotePreviewAnchor)
            : buildingRelayoutPreview ? relayoutPreviewCacheSuffix(profile.renderMode, fontId, cachedPageParagraphIndex)
            : buildingChapterPreview  ? chapterPreviewCacheSuffix(profile.renderMode, fontId)
                                      : sectionCacheSuffixForLayout(profile.renderMode, fontId);
        section.reset();
        section = makeUniqueNoThrow<Section>(epub, currentSpineIndex, renderer, cacheSuffix.c_str());
        if (!section) {
          LOG_ERR("ERS", "Failed to allocate %s section builder for spine %d (free=%u, maxAlloc=%u)", profile.label,
                  currentSpineIndex, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
          layoutAbortedForLowMemory = true;
          return false;
        }

        bool attemptImagesWereSuppressed = false;
        bool attemptLayoutAbortedForLowMemory = false;
        SectionBuildOptions buildOptions;
        // Whole-section SD-font prewarming can take minutes and retain enough
        // glyph data to starve layout. Visible pages already prewarm the exact
        // glyphs they need immediately before rendering.
        buildOptions.skipFontPrewarm = true;
        if (buildingFootnotePreview) {
          buildOptions.previewAnchor = pendingFootnotePreviewAnchor.c_str();
          buildOptions.previewMaxPages = FOOTNOTE_PREVIEW_MAX_PAGES;
        } else if (buildingRelayoutPreview) {
          buildOptions.previewParagraphIndex = cachedPageParagraphIndex;
          buildOptions.previewMaxPages = RELAYOUT_PREVIEW_MAX_PAGES;
        } else if (buildingChapterPreview) {
          buildOptions.previewParagraphIndex = 0;
          buildOptions.previewMaxPages = CHAPTER_PREVIEW_MAX_PAGES;
        }
        if (!buildingRelayoutPreview && !buildingChapterPreview) {
          GUI.drawPopup(renderer, tr(STR_INDEXING));
          // The popup's own refresh is a plain FAST, so force the page that replaces it onto the HALF
          // ghost-cleanup path -- otherwise the "INDEXING" text ghosts under the rendered page.
          pagesUntilFullRefresh = 1;
        }
        const unsigned long sectionBuildStartMs = millis();
        const std::function<void()> buildPopupFn = (buildingRelayoutPreview || buildingChapterPreview)
                                                       ? std::function<void()>{}
                                                       : std::function<void()>{popupFn};
        const bool buildSucceeded = section->createSectionFile(
            fontId, SETTINGS.getReaderLineCompression(), SETTINGS.extraParagraphSpacing, SETTINGS.forceParagraphIndents,
            SETTINGS.paragraphAlignment, viewportWidth, viewportHeight, SETTINGS.hyphenationEnabled,
            profile.embeddedStyle, SETTINGS.imageRendering, profile.bionicReadingEnabled, profile.guideReadingEnabled,
            buildPopupFn, &attemptImagesWereSuppressed, &attemptLayoutAbortedForLowMemory, profile.renderMode,
            buildOptions);
        timingLastSectionBuildMs = millis() - sectionBuildStartMs;
        timingMaxSectionBuildMs = std::max(timingMaxSectionBuildMs, timingLastSectionBuildMs);
        if (timingSectionBuildCount < UINT16_MAX) timingSectionBuildCount++;
        if (buildingRelayoutPreview) {
          timingLastRelayoutPreviewMs = timingLastSectionBuildMs;
          timingMaxRelayoutPreviewMs = std::max(timingMaxRelayoutPreviewMs, timingLastRelayoutPreviewMs);
          if (timingRelayoutPreviewCount < UINT16_MAX) timingRelayoutPreviewCount++;
        } else if (buildingChapterPreview) {
          timingLastChapterPreviewMs = timingLastSectionBuildMs;
          timingMaxChapterPreviewMs = std::max(timingMaxChapterPreviewMs, timingLastChapterPreviewMs);
          if (timingChapterPreviewCount < UINT16_MAX) timingChapterPreviewCount++;
        }
        imagesWereSuppressed = imagesWereSuppressed || attemptImagesWereSuppressed;
        layoutAbortedForLowMemory = attemptLayoutAbortedForLowMemory;
        if (buildSucceeded) {
          activeSectionFontId = fontId;
          usedRenderMode = profile.renderMode;
          safeModeBuildSucceeded = profile.safeMode;
          LOG_DBG("ERS",
                  "%s section cache built: spine=%d font=%d mode=%u embedded=%u bionic=%u guide=%u pages=%u free=%u "
                  "maxAlloc=%u",
                  profile.label, currentSpineIndex, fontId, static_cast<unsigned>(profile.renderMode),
                  static_cast<unsigned>(profile.embeddedStyle), static_cast<unsigned>(profile.bionicReadingEnabled),
                  static_cast<unsigned>(profile.guideReadingEnabled), section->pageCount, ESP.getFreeHeap(),
                  ESP.getMaxAllocHeap());
        }
        return buildSucceeded;
      };

      uint8_t fallbackCount = 0;
      const auto fallbackModes = fallbackModesForSelection(selectedRenderMode, fallbackCount);
      for (uint8_t i = 0; i < fallbackCount && !fallbackBuildSucceeded; ++i) {
        const EpubRenderMode attemptMode = fallbackModes[i];
        if (i > 0) {
          if (!layoutAbortedForLowMemory) {
            break;
          }
          LOG_ERR("ERS", "EPUB section layout aborted for low heap; retrying mode %u",
                  static_cast<unsigned>(attemptMode));
          releaseReaderSdFontCachesForLowMemory(renderer, "ERS", "fallback section rebuild");
        }
        layoutAbortedForLowMemory = false;
        fallbackBuildSucceeded = buildSectionWithProfile(readerFontId, buildProfileForRenderMode(attemptMode));
      }

      if (!fallbackBuildSucceeded && layoutAbortedForLowMemory && shouldAttemptSafeModeFallback()) {
        LOG_ERR("ERS", "EPUB section layout aborted for low heap; retrying Safe Mode%s",
                buildingChapterPreview ? " preview" : "");
        releaseReaderSdFontCachesForLowMemory(renderer, "ERS", "safe mode section rebuild");
        layoutAbortedForLowMemory = false;
        fallbackBuildSucceeded = buildSectionWithProfile(readerFontId, safeModeBuildProfile());
      }

      if (!fallbackBuildSucceeded) {
        if (layoutAbortedForLowMemory) {
          LOG_ERR("ERS", "EPUB section layout aborted for low heap; chapter exceeds safe layout memory");
        }
        if (!layoutAbortedForLowMemory) {
          LOG_ERR("ERS", "Failed to persist page data to SD");
        }
        const bool shouldRestoreFootnoteOrigin = buildingFootnotePreview && footnoteDepth > 0;
        section.reset();
        if (buildingFootnotePreview) {
          pendingFootnotePreviewAnchor.clear();
          activeFootnotePreview = false;
        }
        if (buildingRelayoutPreview) {
          pendingRelayoutPreview = false;
          pendingRelayoutPreviewFromBack = false;
          relayoutPreviewUserMoved = false;
          activeRelayoutPreview = false;
          requestUpdate();
          return;
        }
        if (shouldRestoreFootnoteOrigin) {
          footnoteDepth--;
          const SavedPosition& origin = savedPositions[footnoteDepth];
          LOG_ERR("ERS", "Footnote preview build failed; restoring origin spine %d page %d", origin.spineIndex,
                  origin.pageNumber);
          pendingAnchor.clear();
          pendingPageJump.reset();
          currentSpineIndex = origin.spineIndex;
          nextPageNumber = origin.pageNumber;
          armReadingPaceWarmup("footnote_preview_failed_restore");
          requestUpdate();
          return;
        }
        if (layoutAbortedForLowMemory) {
          showLowMemoryLayoutError();
        } else {
          showPendingSyncSaveError();
        }
        return;
      }
      releaseReaderSdFontCachesForLowMemory(renderer, "ERS", "section cache build");
      LOG_DBG("ERS", "Cache build complete: pages=%u font=%d mode=%u free=%u maxAlloc=%u", section->pageCount,
              activeSectionFontId, static_cast<unsigned>(usedRenderMode), ESP.getFreeHeap(), ESP.getMaxAllocHeap());

      if (safeModeBuildSucceeded) {
        applySafeModeReaderSettings();
        bookHasCustomReaderSettings = true;
        bookHasRenderModeOverride = true;
        if (!saveRuntimeReaderSettingsForCache(epub->getCachePath())) {
          LOG_ERR("ERS", "Failed to save Safe Mode reader settings");
        }
      }

      if (!buildingPreview && imagesWereSuppressed) {
        snprintf(APP_STATE.pendingAlertTitle, sizeof(APP_STATE.pendingAlertTitle), "%s",
                 tr(STR_LOW_MEMORY_IMAGES_TITLE));
        snprintf(APP_STATE.pendingAlertBody, sizeof(APP_STATE.pendingAlertBody), "%s", tr(STR_LOW_MEMORY_IMAGES_BODY));
        APP_STATE.pendingAlertGoHomeOnBack.store(false, std::memory_order_relaxed);
        APP_STATE.hasPendingAlert.store(true, std::memory_order_release);
      }
    } else {
      LOG_DBG("ERS", "Cache found, skipping build... (pages=%u, font=%d mode=%u free=%u, maxAlloc=%u)",
              section->pageCount, activeSectionFontId, static_cast<unsigned>(usedRenderMode), ESP.getFreeHeap(),
              ESP.getMaxAllocHeap());
    }

    activeFootnotePreview = buildingFootnotePreview;
    activeChapterPreview = buildingChapterPreview;
    activeRelayoutPreview = buildingRelayoutPreview || buildingChapterPreview;
    pendingRelayoutPreview = false;
    if (!activeRelayoutPreview) {
      pendingRelayoutPreviewFromBack = false;
      relayoutPreviewUserMoved = false;
    }
    if (activeRelayoutPreview) {
      if (activeChapterPreview) {
        cachedSpineIndex = currentSpineIndex;
        cachedChapterPageNumber = 0;
        cachedChapterTotalPageCount = section ? section->pageCount : 0;
        cachedPageParagraphIndex = 0;
        cachedPageParagraphOffset = 0;
        cachedPageParagraphSpan = 1;
        pendingRelayoutReposition = true;
      }
      relayoutBuildAttempts.store(0, std::memory_order_release);
      relayoutBuildRetired.store(false, std::memory_order_release);
      relayoutBuildSafeModePending.store(false, std::memory_order_release);
      relayoutBuildEligibleAfterMs.store(
          millis() + (activeChapterPreview ? CHAPTER_COMPLETION_IDLE_MS : RELAYOUT_BUILD_IDLE_MS),
          std::memory_order_release);
    }

    if (!buildingPreview && usedRenderMode != selectedRenderMode && !safeModeBuildSucceeded) {
      SETTINGS.epubRenderMode = static_cast<uint8_t>(usedRenderMode);
      bookHasRenderModeOverride = true;
      saveBookRenderModeForCache(epub->getCachePath(), SETTINGS.epubRenderMode);
    }
    const bool renderModeChangedDuringLoad = usedRenderMode != selectedRenderMode;
    if (!buildingPreview && safeModeBuildSucceeded && !safeModeToastShown) {
      showSafeModeToast();
    } else if (!buildingPreview && renderModeChangedDuringLoad && usedRenderMode != EpubRenderMode::CrossInkDefault &&
               !renderModeToastShown) {
      showRenderModeToast(static_cast<uint8_t>(usedRenderMode));
    }

    if (!section) {
      LOG_ERR("ERS", "Section load/build did not produce a section");
      showPendingSyncSaveError();
      return;
    }

    if (pendingPageJump.has_value()) {
      section->currentPage = *pendingPageJump;
      if (pendingClippingIndex != UINT16_MAX && pendingClippingIndex < CLIPPINGS.getClippings().size()) {
        const Clipping& clipping = CLIPPINGS.getClippings()[pendingClippingIndex];
        const uint16_t fallbackPage = static_cast<uint16_t>(std::max(0, section->currentPage));
        section->currentPage = resolveClippingJumpPage(*section, clipping, fallbackPage);
        LOG_DBG("ERS", "Resolved clipping %u to page %d", pendingClippingIndex, section->currentPage);
      } else if (pendingParagraphIndex != UINT16_MAX) {
        if (const auto paragraphPage = section->getPageForParagraphIndex(pendingParagraphIndex)) {
          section->currentPage = *paragraphPage;
          LOG_DBG("ERS", "Resolved paragraph %u to page %u", pendingParagraphIndex, *paragraphPage);
        } else {
          LOG_DBG("ERS", "Paragraph %u not found; using saved section page", pendingParagraphIndex);
        }
      }
      pendingClippingIndex = UINT16_MAX;
      pendingParagraphIndex = UINT16_MAX;
      pendingPageJump.reset();
    } else {
      section->currentPage = nextPageNumber;
      if (section->currentPage < 0) {
        section->currentPage = 0;
      }
    }

    if (!pendingAnchor.empty()) {
      // Resolve from the finalized on-disk anchor map.
      const auto page = section->getPageForAnchor(pendingAnchor);
      if (page && static_cast<int>(*page) < static_cast<int>(section->pageCount)) {
        section->currentPage = *page;
        LOG_DBG("ERS", "Resolved anchor '%s' to page %d", pendingAnchor.c_str(), *page);
      } else if (page) {
        LOG_DBG("ERS", "Anchor '%s' resolved to unavailable page %d in section %d (pages=%u)", pendingAnchor.c_str(),
                *page, currentSpineIndex, section->pageCount);
      } else {
        LOG_DBG("ERS", "Anchor '%s' not found in section %d", pendingAnchor.c_str(), currentSpineIndex);
      }
      pendingAnchor.clear();
      pendingFootnotePreviewAnchor.clear();
    }

    if (pendingPercentJump && section->pageCount > 0) {
      // Apply the pending percent jump now that we know the new section's page count.
      int newPage = static_cast<int>(pendingSpineProgress * static_cast<float>(section->pageCount));
      if (newPage >= section->pageCount) {
        newPage = section->pageCount - 1;
      }
      section->currentPage = newPage;
      if (pendingClippingIndex != UINT16_MAX && pendingClippingIndex < CLIPPINGS.getClippings().size()) {
        const Clipping& clipping = CLIPPINGS.getClippings()[pendingClippingIndex];
        const uint16_t fallbackPage = static_cast<uint16_t>(std::max(0, section->currentPage));
        section->currentPage = resolveClippingJumpPage(*section, clipping, fallbackPage);
        LOG_DBG("ERS", "Resolved clipping %u to page %d", pendingClippingIndex, section->currentPage);
      } else if (pendingParagraphIndex != UINT16_MAX) {
        if (const auto paragraphPage = section->getPageForParagraphIndex(pendingParagraphIndex)) {
          section->currentPage = *paragraphPage;
          LOG_DBG("ERS", "Resolved paragraph %u to page %u", pendingParagraphIndex, *paragraphPage);
        } else {
          LOG_DBG("ERS", "Paragraph %u not found; using saved chapter progress", pendingParagraphIndex);
        }
      }
      pendingClippingIndex = UINT16_MAX;
      pendingParagraphIndex = UINT16_MAX;
      pendingPercentJump = false;
    }

    if (section->currentPage < 0) {
      section->currentPage = 0;
    }
    if (activeRelayoutPreview) {
      positionRelayoutPreview();
      if (activeChapterPreview) {
        cacheRelayoutPreviewPosition();
      }
    }
  }

  if (!section) {
    LOG_ERR("ERS", "Section became unavailable before page render");
    automaticPageTurnActive = false;
    showPendingSyncSaveError();
    return;
  }

  if (section->pageCount > 0 && section->currentPage >= static_cast<int>(section->pageCount)) {
    section->currentPage = section->pageCount - 1;
  }

  if (!activeRelayoutPreview) {
    applyDeferredReposition();
  }

  renderer.clearScreen(ReaderUtils::readerBackgroundColor());

  if (section->pageCount == 0) {
    LOG_DBG("ERS", "No pages to render");
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_CHAPTER), ReaderUtils::readerForegroundBlack(),
                              EpdFontFamily::BOLD);
    renderStatusBar();
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    showPendingSyncSaveError();
    return;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
    LOG_DBG("ERS", "Page out of bounds: %d (max %d)", section->currentPage, section->pageCount);
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_OUT_OF_BOUNDS), ReaderUtils::readerForegroundBlack(),
                              EpdFontFamily::BOLD);
    renderStatusBar();
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    showPendingSyncSaveError();
    return;
  }

  {
    auto p = section->loadPageFromSectionFile();
    if (!p) {
      pageLoadRetryCount++;
      if (pageLoadRetryCount < MAX_PAGE_LOAD_RETRIES) {
        LOG_ERR("ERS", "Failed to load page from SD (retry %d) - clearing section cache", pageLoadRetryCount);
        section->clearCache();
        section.reset();
        requestUpdate();
        automaticPageTurnActive = false;
        showPendingSyncSaveError();
        return;
      }

      LOG_ERR("ERS", "Failed to load page from SD after %d retries", pageLoadRetryCount);
      renderer.clearScreen(ReaderUtils::readerBackgroundColor());
      renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_PAGE_LOAD_ERROR), ReaderUtils::readerForegroundBlack(),
                                EpdFontFamily::BOLD);
      renderStatusBar();
      renderer.displayBuffer();
      automaticPageTurnActive = false;
      showPendingSyncSaveError();
      return;
    }

    pageLoadRetryCount = 0;

    // Preview pages are transient note windows, not full chapter pages with reusable footnote metadata.
    if (activeFootnotePreview || activeRelayoutPreview) {
      currentPageFootnotes.clear();
    } else {
      currentPageFootnotes = std::move(p->footnotes);
    }

    const auto start = millis();
    const int renderFontId = activeSectionFontId != 0 ? activeSectionFontId : SETTINGS.getReaderFontId();
    renderContents(std::move(p), renderFontId, layout.marginTop, layout.marginRight, layout.marginBottom,
                   layout.marginLeft);
    pageShownAtMs = activeFootnotePreview ? 0UL : millis();
    if (timingFirstPaintMs == 0) timingFirstPaintMs = millis();
    {
      const unsigned long renderMs = millis() - start;
      if (timingRenderCount < 6) {
        timingEarlyRenderMs[timingRenderCount] = static_cast<uint16_t>(std::min<unsigned long>(renderMs, UINT16_MAX));
      } else {
        timingLaterRenderTotalMs += renderMs;
        timingLaterRenderCount++;
      }
      if (timingRenderCount < UINT16_MAX) timingRenderCount++;
    }
    LOG_DBG("ERS", "Rendered page in %dms", millis() - start);
  }
  if (!activeFootnotePreview && !activeRelayoutPreview) {
    saveProgressDebounced(currentSpineIndex, section->currentPage, section->pageCount);
    queueCompletionPromptIfNeeded();
  }

  showPendingSyncSaveError();

  if (pendingScreenshot) {
    pendingScreenshot = false;
    ScreenshotUtil::takeScreenshot(renderer);
  }
  if (!activeRelayoutPreview) {
    armNextChapterPreindex(viewportWidth, viewportHeight);
  }
}

bool EpubReaderActivity::applyDeferredReposition() {
  if (cachedChapterTotalPageCount == 0 || !section) {
    return false;
  }

  bool changed = false;
  if (currentSpineIndex == cachedSpineIndex) {
    bool restoredFromParagraph = false;
    if (cachedRelayoutAtChapterStart) {
      if (section->currentPage != 0) {
        section->currentPage = 0;
        changed = true;
      }
      restoredFromParagraph = true;
    } else if (cachedPageParagraphIndex != UINT16_MAX) {
      if (const auto paragraphPage = section->getPageForParagraphIndex(cachedPageParagraphIndex)) {
        uint16_t newStartPage = *paragraphPage;
        if (newStartPage >= section->pageCount) {
          newStartPage = section->pageCount > 0 ? section->pageCount - 1 : 0;
        }

        uint16_t newEndPage = newStartPage + 1;
        while (newEndPage < section->pageCount) {
          const auto pIdx = section->getParagraphIndexForPage(newEndPage);
          if (!pIdx || *pIdx != cachedPageParagraphIndex) {
            break;
          }
          newEndPage++;
        }

        const uint16_t newSpan = std::max<uint16_t>(1, newEndPage - newStartPage);
        const uint16_t oldSpan = std::max<uint16_t>(1, cachedPageParagraphSpan);
        const uint16_t oldOffset = std::min<uint16_t>(cachedPageParagraphOffset, oldSpan - 1);
        uint16_t newOffset = 0;
        if (oldSpan > 1 && newSpan > 1) {
          newOffset = static_cast<uint16_t>((static_cast<uint32_t>(oldOffset) * (newSpan - 1) + (oldSpan - 1) / 2) /
                                            (oldSpan - 1));
        }

        section->currentPage = newStartPage + newOffset;
        restoredFromParagraph = true;
        changed = true;
        LOG_DBG("ERS", "Resolved cached paragraph %u offset %u/%u to page %d (span %u)", cachedPageParagraphIndex,
                oldOffset, oldSpan, section->currentPage, newSpan);
      }
    }

    if (!restoredFromParagraph && section->pageCount != cachedChapterTotalPageCount) {
      const float progress =
          static_cast<float>(cachedChapterPageNumber) / static_cast<float>(cachedChapterTotalPageCount);
      int newPage = static_cast<int>(progress * static_cast<float>(section->pageCount));
      if (newPage < 0) newPage = 0;
      if (section->pageCount > 0 && newPage >= static_cast<int>(section->pageCount)) {
        newPage = section->pageCount - 1;
      }
      if (newPage != section->currentPage) {
        section->currentPage = newPage;
        changed = true;
      }
    }
  }

  cachedChapterPageNumber = 0;
  cachedChapterTotalPageCount = 0;
  pendingRelayoutReposition = false;
  cachedRelayoutAtChapterStart = false;
  pendingRelayoutPreviewFromBack = false;
  relayoutPreviewUserMoved = false;
  cachedPageParagraphIndex = UINT16_MAX;
  cachedPageParagraphOffset = 0;
  cachedPageParagraphSpan = 0;
  return changed;
}

bool EpubReaderActivity::saveProgress(int spineIndex, int currentPage, int pageCount) {
  std::lock_guard<std::mutex> lock(progressPersistMutex_);
  return saveProgressLocked(spineIndex, currentPage, pageCount);
}

bool EpubReaderActivity::saveProgressLocked(int spineIndex, int currentPage, int pageCount) {
  if (activeFootnotePreview) {
    return true;
  }
  if (hasSavedProgress && spineIndex == lastSavedProgressSpineIndex && currentPage == lastSavedProgressPageNumber &&
      pageCount == lastSavedProgressPageCount) {
    // progress.bin already holds exactly this position (repaints, toasts, menu
    // returns, page-forward-then-back) — skip the two fsync'd file rewrites.
    progressDirty = false;
    return true;
  }
  if (!EpubReaderUtils::saveProgress(*epub, spineIndex, currentPage, pageCount)) {
    return false;
  }
  hasSavedProgress = true;
  progressDirty = false;
  lastSavedProgressSpineIndex = spineIndex;
  lastSavedProgressPageNumber = currentPage;
  lastSavedProgressPageCount = pageCount;
  lastProgressWriteMs = millis();
  pageTurnsSinceProgressWrite = 0;
  return true;
}

void EpubReaderActivity::saveProgressDebounced(int spineIndex, int currentPage, int pageCount) {
  if (activeFootnotePreview) {
    return;
  }
  std::lock_guard<std::mutex> lock(progressPersistMutex_);
  if (hasSavedProgress && spineIndex == lastSavedProgressSpineIndex && currentPage == lastSavedProgressPageNumber &&
      pageCount == lastSavedProgressPageCount) {
    progressDirty = false;
    return;
  }
  progressDirty = true;
  pendingProgressSpineIndex = spineIndex;
  pendingProgressPageNumber = currentPage;
  pendingProgressPageCount = pageCount;
  if (pageTurnsSinceProgressWrite < UINT8_MAX) {
    pageTurnsSinceProgressWrite++;
  }
  const bool spineChanged = !hasSavedProgress || spineIndex != lastSavedProgressSpineIndex;
  const bool turnBudgetExceeded = pageTurnsSinceProgressWrite >= kProgressWriteMaxTurns;
  const bool timeBudgetExceeded = millis() - lastProgressWriteMs >= kProgressWriteMaxMs;
  if (spineChanged || turnBudgetExceeded || timeBudgetExceeded) {
    if (!saveProgressLocked(spineIndex, currentPage, pageCount)) {
      pendingSyncSaveError = true;
    }
  }
}

bool EpubReaderActivity::flushPendingProgress() {
  std::lock_guard<std::mutex> lock(progressPersistMutex_);
  if (!progressDirty || !epub) {
    return true;
  }
  return saveProgressLocked(pendingProgressSpineIndex, pendingProgressPageNumber, pendingProgressPageCount);
}

void EpubReaderActivity::cacheCurrentSectionPosition() {
  if (activeFootnotePreview) {
    return;
  }
  cachedSpineIndex = currentSpineIndex;
  cachedChapterPageNumber = section->currentPage;
  cachedChapterTotalPageCount = section->pageCount;
  pendingRelayoutReposition = true;
  cachedRelayoutAtChapterStart = section->currentPage <= 0;
  cachedPageParagraphIndex = UINT16_MAX;
  cachedPageParagraphOffset = 0;
  cachedPageParagraphSpan = 0;
  nextPageNumber = section->currentPage;

  if (section->currentPage >= 0 && section->currentPage < section->pageCount) {
    const uint16_t currentPage = static_cast<uint16_t>(section->currentPage);
    if (const auto pIdx = section->getParagraphIndexForPage(currentPage)) {
      cachedPageParagraphIndex = *pIdx;

      uint16_t paragraphStartPage = currentPage;
      while (paragraphStartPage > 0) {
        const auto prevPIdx = section->getParagraphIndexForPage(paragraphStartPage - 1);
        if (!prevPIdx || *prevPIdx != *pIdx) {
          break;
        }
        paragraphStartPage--;
      }

      uint16_t paragraphEndPage = currentPage + 1;
      while (paragraphEndPage < section->pageCount) {
        const auto nextPIdx = section->getParagraphIndexForPage(paragraphEndPage);
        if (!nextPIdx || *nextPIdx != *pIdx) {
          break;
        }
        paragraphEndPage++;
      }

      cachedPageParagraphOffset = currentPage - paragraphStartPage;
      cachedPageParagraphSpan = std::max<uint16_t>(1, paragraphEndPage - paragraphStartPage);
    }
  }
}

void EpubReaderActivity::renderContents(std::unique_ptr<Page> page, const int fontId, const int orientedMarginTop,
                                        const int orientedMarginRight, const int orientedMarginBottom,
                                        const int orientedMarginLeft) {
  const auto t0 = millis();

  // Font prewarm: scan pass accumulates text, then prewarm, then real render
  auto* fcm = renderer.getFontCacheManager();
  fcm->resetStats();
  const auto heapBefore = MemoryBudget::snapshot();
  auto scope = fcm->createPrewarmScope();
  page->renderText(renderer, fontId, orientedMarginLeft, orientedMarginTop);  // scan pass
  scope.endScanAndPrewarm();
  const auto heapAfter = MemoryBudget::snapshot();
  fcm->logStats("prewarm");
  const auto tPrewarm = millis();

  LOG_DBG(
      "ERS", "Heap prewarm: free=%u->%u delta=%ld maxAlloc=%u->%u delta=%ld largestPct=%u->%u", heapBefore.freeHeap,
      heapAfter.freeHeap,
      static_cast<long>(static_cast<int32_t>(heapAfter.freeHeap) - static_cast<int32_t>(heapBefore.freeHeap)),
      heapBefore.maxAllocHeap, heapAfter.maxAllocHeap,
      static_cast<long>(static_cast<int32_t>(heapAfter.maxAllocHeap) - static_cast<int32_t>(heapBefore.maxAllocHeap)),
      largestBlockPercent(heapBefore), largestBlockPercent(heapAfter));

  const bool pageHasImages = page->hasImages();
  const bool foregroundBlack = ReaderUtils::readerForegroundBlack();
  const bool needsImageGrayscale = pageHasImages;
  const bool needsTextGrayscale = SETTINGS.textAntiAliasing && foregroundBlack;
  const bool needsAnyGrayscale = needsTextGrayscale || needsImageGrayscale;
  const int contentBottom = renderer.getScreenHeight() - orientedMarginBottom;

  const auto finalizeBufferComposition = [&]() {
    drawClippingHighlights(*page, fontId, orientedMarginTop, orientedMarginLeft);
    drawPublisherPageMarkers(renderer, *page, orientedMarginTop, contentBottom, foregroundBlack);
  };

  const auto composePageBuffer = [&]() {
    page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop, foregroundBlack);
    finalizeBufferComposition();
  };

  const auto composeGrayscaleBuffer = [&]() {
    if (needsTextGrayscale) {
      page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop, foregroundBlack);
    } else {
      page->renderImages(renderer, fontId, orientedMarginLeft, orientedMarginTop);
    }
    finalizeBufferComposition();
  };

  composePageBuffer();
  renderStatusBar();
  if (pendingBookmarkFeedback) {
    const char* msg = tr(STR_BOOKMARK_ADDED);
    switch (bookmarkFeedbackType) {
      case BookmarkFeedbackType::Added:
        msg = tr(STR_BOOKMARK_ADDED);
        break;
      case BookmarkFeedbackType::Removed:
        msg = tr(STR_BOOKMARK_REMOVED);
        break;
      case BookmarkFeedbackType::LimitReached:
        msg = tr(STR_BOOKMARK_LIMIT_REACHED);
        break;
    }
    drawToastBuffer(renderer, msg);
  }
  if (pendingCompletedFeedback) {
    const char* msg = completedFeedbackIsFinished ? tr(STR_MARKED_FINISHED) : tr(STR_MARKED_UNFINISHED);
    drawToastBuffer(renderer, msg);
  }
  if (pendingTiltPageTurnFeedback) {
    const char* msg = tiltPageTurnFeedbackEnabled ? tr(STR_TILT_TO_TURN_ON) : tr(STR_TILT_TO_TURN_OFF);
    drawToastBuffer(renderer, msg);
  }
  if (pendingBionicFeedback) {
    char msg[48];
    const char* modeLabel = tr(STR_STATE_OFF);
    if (bionicFeedbackMode == BionicReadingMode::Normal) {
      modeLabel = tr(STR_BIONIC_NORMAL);
    } else if (bionicFeedbackMode == BionicReadingMode::Subtle) {
      modeLabel = tr(STR_BIONIC_SUBTLE);
    }
    snprintf(msg, sizeof(msg), "%s: %s", tr(STR_BIONIC_READING), modeLabel);
    drawToastBuffer(renderer, msg);
  }
  if (pendingSafeModeToast) {
    drawRenderModeToastBuffer(tr(STR_SAFE_MODE_READING_AIDS_OFF));
  } else if (pendingRenderModeToast) {
    drawRenderModeToastBuffer(labelForRenderModeToast(normalizeRenderMode(renderModeToastMode)));
  }
  fcm->logStats("bw_render");
  const auto tBwRender = millis();
  const auto logImagePageProfile = [](const uint32_t imageBlankDisplayMs, const uint32_t imageRestoreRenderMs,
                                      const uint32_t imageFinalDisplayMs) {
    LOG_DBG("ERS", "Image page profile: blank_display=%lums restore_render=%lums final_display=%lums",
            imageBlankDisplayMs, imageRestoreRenderMs, imageFinalDisplayMs);
  };

  if (pageHasImages) {
    if (!ReaderUtils::displayConfiguredGrayscaleBase(renderer, pagesUntilFullRefresh)) {
      // Double FAST_REFRESH with selective image blanking (pablohc's technique):
      // HALF_REFRESH sets particles too firmly for the grayscale LUT to adjust.
      // Instead, blank only the image area and do two fast refreshes.
      // Step 1: Display page with image area blanked (text appears, image area white)
      // Step 2: Re-render with images and display again (images appear clean)
      int16_t imgX, imgY, imgW, imgH;
      if (page->getImageBoundingBox(imgX, imgY, imgW, imgH)) {
        renderer.fillRect(imgX + orientedMarginLeft, imgY + orientedMarginTop, imgW, imgH, false);
        const auto tImageBlankDisplay = millis();
        renderer.displayBuffer(HalDisplay::FAST_REFRESH);
        const uint32_t imageBlankDisplayMs = millis() - tImageBlankDisplay;

        // Re-render page content to restore images into the blanked area
        // Status bar is not re-rendered here to avoid reading stale dynamic values (e.g. battery %)
        const auto tImageRestoreRender = millis();
        composePageBuffer();
        const uint32_t imageRestoreRenderMs = millis() - tImageRestoreRender;
        const auto tImageFinalDisplay = millis();
        renderer.displayBuffer(HalDisplay::FAST_REFRESH);
        const uint32_t imageFinalDisplayMs = millis() - tImageFinalDisplay;
        logImagePageProfile(imageBlankDisplayMs, imageRestoreRenderMs, imageFinalDisplayMs);
      } else {
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      }
      // The image's own page is handled above and doesn't count toward the full
      // refresh cadence. But the grayscale pass below leaves gray charge in the
      // image region that a plain fast diff on the *next* page can't clear, so
      // text there ghosts gray (#2190). Force the next ordinary page onto the
      // HALF ghost-cleanup path, which drives every pixel to its target
      // regardless of residue.
      pagesUntilFullRefresh = 1;
    }
  } else if (needsAnyGrayscale) {
    if (ReaderUtils::displayConfiguredGrayscaleBase(renderer, pagesUntilFullRefresh)) {
      // The configured mode replaces the automatic cadence for this page.
    } else if (pagesUntilFullRefresh <= 1) {
      // Cleanup turns still need the stronger HALF pass, but X3 grayscale
      // overlays settle better if the OEM precondition step runs before the
      // gray planes are written.
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      renderer.preconditionGrayscale();
      pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
    } else {
      // Use the grayscale-aware base waveform so the first visible pass is
      // closer to the final anti-aliased result instead of flashing darker
      // text first and softening after the grayscale overlay.
      renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
      pagesUntilFullRefresh--;
    }
  } else {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
  }
  const auto tDisplay = millis();

  TiledGrayscaleTimings tiledTimings;
  if (runTiledGrayscalePass(renderer, *page, fontId, orientedMarginLeft, orientedMarginTop, foregroundBlack,
                            needsTextGrayscale, needsImageGrayscale, tiledTimings)) {
    const auto tEnd = millis();
    LOG_DBG("ERS",
            "Page render (tiled): prewarm=%lums bw_render=%lums display=%lums "
            "gray_lsb=%lums gray_msb=%lums gray_display=%lums cleanup=%lums total=%lums",
            tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tiledTimings.grayLsb - tDisplay,
            tiledTimings.grayMsb - tiledTimings.grayLsb, tiledTimings.grayDisplay - tiledTimings.grayMsb,
            tiledTimings.cleanup - tiledTimings.grayDisplay, tEnd - t0);
    return;
  }

  // Save bw buffer to reset buffer state after grayscale data sync
  const auto bwStoreHeapBefore = MemoryBudget::snapshot();
  const bool storedBwBuffer = needsAnyGrayscale && renderer.storeBwBuffer();
  const auto bwStoreHeapAfter = MemoryBudget::snapshot();
  const auto tBwStore = millis();
  const bool canApplyGrayscale = needsAnyGrayscale && storedBwBuffer;
  if (needsAnyGrayscale && !storedBwBuffer) {
    LOG_ERR("ERS", "Skipping grayscale enhancement: failed to store BW backup");
  }

  // grayscale rendering
  if (canApplyGrayscale) {
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    composeGrayscaleBuffer();
    renderer.copyGrayscaleLsbBuffers();
    const auto tGrayLsb = millis();

    // Render and copy to MSB buffer
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    composeGrayscaleBuffer();
    renderer.copyGrayscaleMsbBuffers();
    const auto tGrayMsb = millis();

    // display grayscale part
    renderer.displayGrayBuffer();
    const auto tGrayDisplay = millis();
    renderer.setRenderMode(GfxRenderer::BW);
    // restore the bw data
    renderer.restoreBwBuffer();
    const auto tBwRestore = millis();

    const auto tEnd = millis();
    LOG_DBG("ERS",
            "Page render: prewarm=%lums bw_render=%lums display=%lums bw_store=%lums bw_store_ok=%d "
            "bw_store_free=%u->%u delta=%ld bw_store_maxAlloc=%u->%u delta=%ld bw_store_largestPct=%u->%u "
            "gray_lsb=%lums gray_msb=%lums gray_display=%lums bw_restore=%lums total=%lums",
            tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tBwStore - tDisplay, storedBwBuffer,
            bwStoreHeapBefore.freeHeap, bwStoreHeapAfter.freeHeap,
            static_cast<long>(static_cast<int32_t>(bwStoreHeapAfter.freeHeap) -
                              static_cast<int32_t>(bwStoreHeapBefore.freeHeap)),
            bwStoreHeapBefore.maxAllocHeap, bwStoreHeapAfter.maxAllocHeap,
            static_cast<long>(static_cast<int32_t>(bwStoreHeapAfter.maxAllocHeap) -
                              static_cast<int32_t>(bwStoreHeapBefore.maxAllocHeap)),
            largestBlockPercent(bwStoreHeapBefore), largestBlockPercent(bwStoreHeapAfter), tGrayLsb - tBwStore,
            tGrayMsb - tGrayLsb, tGrayDisplay - tGrayMsb, tBwRestore - tGrayDisplay, tEnd - t0);
  } else {
    if (storedBwBuffer) {
      // Restore the BW data when we skipped grayscale entirely.
      renderer.restoreBwBuffer();
    }
    const auto tBwRestore = millis();

    const auto tEnd = millis();
    LOG_DBG("ERS",
            "Page render: prewarm=%lums bw_render=%lums display=%lums bw_store=%lums bw_store_ok=%d "
            "bw_store_free=%u->%u delta=%ld bw_store_maxAlloc=%u->%u delta=%ld bw_store_largestPct=%u->%u "
            "bw_restore=%lums total=%lums",
            tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tBwStore - tDisplay, storedBwBuffer,
            bwStoreHeapBefore.freeHeap, bwStoreHeapAfter.freeHeap,
            static_cast<long>(static_cast<int32_t>(bwStoreHeapAfter.freeHeap) -
                              static_cast<int32_t>(bwStoreHeapBefore.freeHeap)),
            bwStoreHeapBefore.maxAllocHeap, bwStoreHeapAfter.maxAllocHeap,
            static_cast<long>(static_cast<int32_t>(bwStoreHeapAfter.maxAllocHeap) -
                              static_cast<int32_t>(bwStoreHeapBefore.maxAllocHeap)),
            largestBlockPercent(bwStoreHeapBefore), largestBlockPercent(bwStoreHeapAfter), tBwRestore - tBwStore,
            tEnd - t0);
  }
}

void EpubReaderActivity::drawClippingHighlights(const Page& page, const int fontId, const int orientedMarginTop,
                                                const int orientedMarginLeft) const {
  if (!section || !CLIPPINGS.hasClippings()) {
    return;
  }

  std::array<ClippingPageMatch, CLIPPING_MAX_PER_BOOK> matches;
  uint16_t matchCount = 0;
  const bool canUseStoredRanges = section->pageCount > 0 && section->pageCount <= UINT16_MAX &&
                                  section->currentPage >= 0 && section->currentPage < section->pageCount;
  const uint16_t currentPage = canUseStoredRanges ? static_cast<uint16_t>(section->currentPage) : 0;
  const uint16_t currentPageCount = canUseStoredRanges ? static_cast<uint16_t>(section->pageCount) : 0;
  for (const Clipping& clipping : CLIPPINGS.getClippings()) {
    if (clipping.spineIndex != static_cast<uint16_t>(currentSpineIndex)) {
      continue;
    }
    ClippingPageMatch match;
    const bool matchedStoredRange =
        canUseStoredRanges && findClippingStoredRangeOnPage(page, clipping, currentPage, currentPageCount, match);
    const bool shouldSearchText = !canUseStoredRanges || clipping.pageCount != currentPageCount ||
                                  (currentPage >= clipping.startPage && currentPage <= clipping.endPage);
    if (matchedStoredRange || (shouldSearchText && findClippingTextOnPage(page, clipping, match))) {
      matches[matchCount++] = match;
      if (matchCount >= matches.size()) {
        break;
      }
    }
  }
  if (matchCount == 0) {
    return;
  }

  const bool foregroundBlack = ReaderUtils::readerForegroundBlack();
  const auto isHighlightedWord = [&matches, matchCount](const uint16_t pageWordIndex) {
    for (uint16_t matchIndex = 0; matchIndex < matchCount; ++matchIndex) {
      if (pageWordIndex >= matches[matchIndex].startWord && pageWordIndex <= matches[matchIndex].endWord) {
        return true;
      }
    }
    return false;
  };

  forEachVisiblePageWord(
      page, [&](const uint16_t pageWordIndex, const PageLine& line, const TextBlock& block, const size_t i) {
        if (!isHighlightedWord(pageWordIndex)) {
          return true;
        }

        const auto& wordList = block.getWords();
        const auto& xpos = block.getWordXpos();
        const auto& styles = block.getWordStyles();
        if (i >= wordList.size() || i >= xpos.size() || i >= styles.size()) {
          return true;
        }

        const std::string& wordText = wordList[i];
        const bool hasEmSpace = hasEmSpacePrefix(wordText);
        const char* visibleText = wordText.c_str() + (hasEmSpace ? 3 : 0);
        const auto textStyle = static_cast<EpdFontFamily::Style>(styles[i] & ~EpdFontFamily::UNDERLINE);
        const int skipX = hasEmSpace ? renderer.getTextAdvanceX(fontId, "\xe2\x80\x83", textStyle) : 0;
        const int wordX = orientedMarginLeft + line.xPos + xpos[i] + skipX;
        const int wordY = orientedMarginTop + line.yPos;
        int wordW = renderer.getTextAdvanceX(fontId, wordText.c_str(), textStyle) - skipX;
        const int wordH = renderer.getLineHeight(fontId);
        if (i + 1 < wordList.size() && i + 1 < xpos.size() && i + 1 < styles.size()) {
          const std::string& nextWordText = wordList[i + 1];
          const bool nextHasEmSpace = hasEmSpacePrefix(nextWordText);
          const auto nextTextStyle = static_cast<EpdFontFamily::Style>(styles[i + 1] & ~EpdFontFamily::UNDERLINE);
          const int nextSkipX = nextHasEmSpace ? renderer.getTextAdvanceX(fontId, "\xe2\x80\x83", nextTextStyle) : 0;
          const int nextWordX = orientedMarginLeft + line.xPos + xpos[i + 1] + nextSkipX;
          if (isHighlightedWord(pageWordIndex + 1) && nextWordX > wordX + wordW) {
            wordW = nextWordX - wordX;
          } else if (nextWordX > wordX && wordW > nextWordX - wordX) {
            wordW = nextWordX - wordX;
          }
        }
        if (wordW > 0) {
          renderer.fillRectDither(wordX, wordY, wordW, wordH, Color::LightGray);
          renderer.drawText(fontId, wordX, wordY, visibleText, foregroundBlack, textStyle);
        }
        return true;
      });
}

void EpubReaderActivity::renderStatusBar() const {
  const int totalPageCount = section->pageCount;
  int currentPage = section->currentPage + 1;
  int pageCount = totalPageCount;
  const bool layoutPreview = activeRelayoutPreview;
  const float bookProgress = activeFootnotePreview ? 0.0f : getCurrentBookProgressPercent();
  const float sectionProgress =
      (totalPageCount > 0) ? static_cast<float>(section->currentPage) / static_cast<float>(totalPageCount) : 0.0f;
  float chapterProgress =
      layoutPreview && cachedChapterTotalPageCount > 0
          ? static_cast<float>(cachedChapterPageNumber) / static_cast<float>(cachedChapterTotalPageCount)
          : sectionProgress;

  uint32_t referencePage = 0;
  uint32_t referencePageCount = 0;
  if (activeFootnotePreview || layoutPreview || !SETTINGS.stablePageNumbers ||
      !epub->resolveReferencePage(currentSpineIndex, sectionProgress, referencePage, referencePageCount)) {
    referencePage = 0;
    referencePageCount = 0;
  }

  std::string title;

  int textYOffset = 0;

  if (automaticPageTurnActive) {
    title = tr(STR_AUTO_TURN_ENABLED) + std::to_string(pageTurnDuration / 1000);

    // calculates textYOffset when rendering title in status bar
    const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();

    // offsets text if no status bar or progress bar only
    if (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight()) {
      textYOffset += UITheme::getInstance().getMetrics().statusBarVerticalMargin;
    }

  } else if (activeFootnotePreview && SETTINGS.statusBarTitle != CrossPointSettings::STATUS_BAR_TITLE::HIDE_TITLE) {
    title = tr(STR_FOOTNOTES);
  } else if (SETTINGS.statusBarTitle == CrossPointSettings::STATUS_BAR_TITLE::CHAPTER_TITLE) {
    // Chapter-constant: resolving via book.bin costs several SD seek+read pairs,
    // so refresh the cached title only when the chapter changes.
    if (cachedTocTitleSpineIndex != currentSpineIndex) {
      cachedTocTitleSpineIndex = currentSpineIndex;
      cachedTocTitle = tr(STR_UNNAMED);
      const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
      if (tocIndex != -1) {
        cachedTocTitle = epub->getTocItem(tocIndex).title;
      }
    }
    title = cachedTocTitle;

  } else if (SETTINGS.statusBarTitle == CrossPointSettings::STATUS_BAR_TITLE::BOOK_TITLE) {
    title = epub->getTitle();
  }

  const int bookmarkPageCount = totalPageCount > 0 ? totalPageCount : 1;
  const float rawProgress =
      (totalPageCount > 0) ? (static_cast<float>(section->currentPage) / static_cast<float>(totalPageCount)) : 0.0f;
  const bool bookmarked =
      !activeFootnotePreview &&
      BOOKMARKS.hasBookmarkForPage(static_cast<uint16_t>(currentSpineIndex), rawProgress, bookmarkPageCount);
  char timeLeftLabel[24] = {};
  const char* timeLeft =
      (!activeFootnotePreview && formatTimeLeftLabel(timeLeftLabel, sizeof(timeLeftLabel))) ? timeLeftLabel : nullptr;
  const char* progressTextOverride = nullptr;
  if (layoutPreview) {
    progressTextOverride = relayoutBuildRetired.load(std::memory_order_acquire) ? tr(STR_PREVIEW) : tr(STR_INDEXING);
  }
  GUI.drawStatusBar(renderer, bookProgress, currentPage, pageCount, title, 0, textYOffset, bookmarked, timeLeft,
                    ReaderUtils::readerDarkModeEnabled(), chapterProgress * 100.0f, static_cast<int>(referencePage),
                    static_cast<int>(referencePageCount), !activeFootnotePreview, progressTextOverride);
  GUI.drawTopStatusBarClock(renderer, UITheme::getInstance().getMetrics().topPadding, nullptr, true, 0,
                            ReaderUtils::readerDarkModeEnabled());
}

bool EpubReaderActivity::shouldUseFootnotePreview(const int targetSpineIndex, const std::string& anchor) const {
  if (!epub || anchor.empty() || targetSpineIndex < 0 || targetSpineIndex >= epub->getSpineItemsCount()) {
    return false;
  }
  return targetSpineIndex != currentSpineIndex;
}

std::string EpubReaderActivity::footnotePreviewCacheSuffix(const EpubRenderMode renderMode, const int fontId,
                                                           const std::string& anchor) const {
  const uint64_t anchorHash = hashFootnotePreviewAnchor(anchor);
  char previewSuffix[32];
  snprintf(previewSuffix, sizeof(previewSuffix), "_fn_%08lx%08lx", static_cast<unsigned long>(anchorHash >> 32),
           static_cast<unsigned long>(anchorHash & 0xffffffffULL));
  return sectionCacheSuffixForLayout(renderMode, fontId) + previewSuffix;
}

std::string EpubReaderActivity::relayoutPreviewCacheSuffix(const EpubRenderMode renderMode, const int fontId,
                                                           const uint16_t paragraphIndex) const {
  char previewSuffix[24] = {};
  snprintf(previewSuffix, sizeof(previewSuffix), "_rp_%u", paragraphIndex);
  return sectionCacheSuffixForLayout(renderMode, fontId) + previewSuffix;
}

std::string EpubReaderActivity::chapterPreviewCacheSuffix(const EpubRenderMode renderMode, const int fontId) const {
  return sectionCacheSuffixForLayout(renderMode, fontId) + "_cp";
}

void EpubReaderActivity::clearFootnotePreviewState() {
  pendingFootnotePreviewAnchor.clear();
  activeFootnotePreview = false;
  pendingRelayoutPreviewFromBack = false;
  relayoutPreviewUserMoved = false;
}

void EpubReaderActivity::navigateToHref(const std::string& hrefStr, const bool savePosition) {
  pageLoadRetryCount = 0;
  if (!epub) return;

  // Push current position onto saved stack
  if (savePosition && section && footnoteDepth < MAX_FOOTNOTE_DEPTH) {
    savedPositions[footnoteDepth] = {currentSpineIndex, section->currentPage};
    footnoteDepth++;
    LOG_DBG("ERS", "Saved position [%d]: spine %d, page %d", footnoteDepth, currentSpineIndex, section->currentPage);
  }

  // Extract fragment anchor (e.g. "#note1" or "chapter2.xhtml#note1")
  std::string anchor;
  const auto hashPos = hrefStr.find('#');
  if (hashPos != std::string::npos && hashPos + 1 < hrefStr.size()) {
    anchor = hrefStr.substr(hashPos + 1);
  }

  // Check for same-file anchor reference (#anchor only)
  bool sameFile = !hrefStr.empty() && hrefStr[0] == '#';

  int targetSpineIndex;
  if (sameFile) {
    targetSpineIndex = currentSpineIndex;
  } else {
    targetSpineIndex = epub->resolveHrefToSpineIndex(hrefStr);
  }

  if (targetSpineIndex < 0) {
    LOG_DBG("ERS", "Could not resolve href: %s", hrefStr.c_str());
    if (savePosition && footnoteDepth > 0) footnoteDepth--;  // undo push
    return;
  }

  {
    RenderLock lock(*this);
    const bool useFootnotePreview = savePosition && shouldUseFootnotePreview(targetSpineIndex, anchor);
    pendingAnchor = anchor;
    pendingFootnotePreviewAnchor = useFootnotePreview ? anchor : std::string{};
    activeFootnotePreview = false;
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = 0;
    section.reset();
  }
  armReadingPaceWarmup(savePosition ? "href_navigation" : "href_restore");
  requestUpdate();
  LOG_DBG("ERS", "Navigated to spine %d for href: %s", targetSpineIndex, hrefStr.c_str());
}

void EpubReaderActivity::restoreSavedPosition() {
  pageLoadRetryCount = 0;
  if (footnoteDepth <= 0) return;
  footnoteDepth--;
  const auto& pos = savedPositions[footnoteDepth];
  LOG_DBG("ERS", "Restoring position [%d]: spine %d, page %d", footnoteDepth, pos.spineIndex, pos.pageNumber);

  {
    RenderLock lock(*this);
    clearFootnotePreviewState();
    pendingAnchor.clear();
    currentSpineIndex = pos.spineIndex;
    nextPageNumber = pos.pageNumber;
    section.reset();
  }
  armReadingPaceWarmup("saved_position_restore");
  requestUpdate();
}
bool EpubReaderActivity::drawCurrentPageToBuffer(const std::string& filePath, GfxRenderer& renderer) {
  auto epub = std::make_shared<Epub>(filePath, DUET_BOOKS_ROOT_PATH "");
  epub->setupCacheDir();

  ScopedReaderSettingsRestore restoreReaderSettings;
  const auto readerSettings = loadBookReaderSettingsFile(epub->getCachePath());
  if (readerSettings.hasCustomReaderSettings) {
    applyReaderSettings(readerSettings.readerSettings);
  }
  SETTINGS.epubRenderMode = readerSettings.hasRenderModeOverride
                                ? normalizeRenderModeRaw(readerSettings.renderMode)
                                : static_cast<uint8_t>(EpubRenderMode::CrossInkDefault);
  sdFontSystem.ensureLoaded(renderer);

  // Load CSS when embeddedStyle is enabled, as createSectionFile may need it to rebuild the cache.
  if (!epub->load(true, SETTINGS.embeddedStyle == 0)) {
    LOG_DBG("SLP", "EPUB: failed to load %s", filePath.c_str());
    return false;
  }

  // Load saved spine index and page number
  int spineIndex = 0, pageNumber = 0;
  EpubReaderUtils::Progress progress;
  if (EpubReaderUtils::loadProgress(*epub, progress, "SLP")) {
    spineIndex = progress.spineIndex;
    pageNumber = progress.pageNumber;
  }
  if (spineIndex < 0 || spineIndex >= epub->getSpineItemsCount()) spineIndex = 0;

  // Apply the reader orientation so margins match what the reader would produce
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

  const ReaderViewportLayout layout = computeReaderViewportLayout(renderer, /*automaticPageTurnActive=*/false);
  const uint16_t viewportWidth = layout.viewportWidth;
  const uint16_t viewportHeight = layout.viewportHeight;

  // Load or rebuild the section cache. Rebuilding is needed when the cache is missing or stale
  // (e.g. after a firmware update). A no-op popup callback avoids any UI during sleep preparation.
  const int readerFontId = SETTINGS.getReaderFontId();
  int renderFontId = readerFontId;
  const EpubRenderMode selectedRenderMode = normalizeRenderMode(SETTINGS.epubRenderMode);
  const std::string selectedCacheSuffix = sectionCacheSuffixForLayout(selectedRenderMode, readerFontId);
  auto section = makeUniqueNoThrow<Section>(epub, spineIndex, renderer, selectedCacheSuffix.c_str());
  if (!section) {
    LOG_ERR("SLP", "EPUB: failed to allocate section for spine %d", spineIndex);
    return false;
  }
  bool loadedSection = section->loadSectionFile(
      readerFontId, SETTINGS.getReaderLineCompression(), SETTINGS.extraParagraphSpacing, SETTINGS.forceParagraphIndents,
      SETTINGS.paragraphAlignment, viewportWidth, viewportHeight, SETTINGS.hyphenationEnabled, SETTINGS.embeddedStyle,
      SETTINGS.imageRendering, SETTINGS.bionicReadingEnabled, SETTINGS.guideReadingEnabled, selectedRenderMode);

  if (!loadedSection) {
    if (!MemoryBudget::hasHeapForOptionalEpubRebuild("SLP", "EPUB sleep-page cache rebuild", spineIndex)) {
      return false;
    }

    LOG_DBG("SLP", "EPUB: section cache not found for spine %d, rebuilding (free=%u, maxAlloc=%u)", spineIndex,
            ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    bool layoutAbortedForLowMemory = false;
    bool buildSucceeded = false;
    bool safeModeBuildSucceeded = false;
    EpubRenderMode usedRenderMode = selectedRenderMode;
    SectionBuildOptions sleepBuildOptions;
    sleepBuildOptions.skipFontPrewarm = true;
    uint8_t fallbackCount = 0;
    const auto fallbackModes = fallbackModesForSelection(selectedRenderMode, fallbackCount);
    for (uint8_t i = 0; i < fallbackCount && !buildSucceeded; ++i) {
      const EpubRenderMode attemptMode = fallbackModes[i];
      if (i > 0) {
        if (!layoutAbortedForLowMemory) {
          break;
        }
        LOG_DBG("SLP", "EPUB: retrying sleep-page rebuild with mode %u for spine %d",
                static_cast<unsigned>(attemptMode), spineIndex);
        releaseReaderSdFontCachesForLowMemory(renderer, "SLP", "sleep-page fallback rebuild");
      }
      layoutAbortedForLowMemory = false;
      const SectionBuildProfile profile = buildProfileForRenderMode(attemptMode);
      const std::string cacheSuffix = sectionCacheSuffixForLayout(profile.renderMode, readerFontId);
      section = makeUniqueNoThrow<Section>(epub, spineIndex, renderer, cacheSuffix.c_str());
      if (!section) {
        LOG_ERR("SLP", "EPUB: failed to allocate section builder for spine %d", spineIndex);
        return false;
      }
      buildSucceeded = section->createSectionFile(
          readerFontId, SETTINGS.getReaderLineCompression(), SETTINGS.extraParagraphSpacing,
          SETTINGS.forceParagraphIndents, SETTINGS.paragraphAlignment, viewportWidth, viewportHeight,
          SETTINGS.hyphenationEnabled, profile.embeddedStyle, SETTINGS.imageRendering, profile.bionicReadingEnabled,
          profile.guideReadingEnabled, []() {}, nullptr, &layoutAbortedForLowMemory, profile.renderMode,
          sleepBuildOptions);
      if (buildSucceeded) {
        usedRenderMode = profile.renderMode;
      }
    }
    if (!buildSucceeded && layoutAbortedForLowMemory && shouldAttemptSafeModeFallback()) {
      LOG_DBG("SLP", "EPUB: retrying sleep-page rebuild with Safe Mode for spine %d", spineIndex);
      releaseReaderSdFontCachesForLowMemory(renderer, "SLP", "sleep-page safe mode rebuild");
      layoutAbortedForLowMemory = false;
      const SectionBuildProfile profile = safeModeBuildProfile();
      const std::string cacheSuffix = sectionCacheSuffixForLayout(profile.renderMode, readerFontId);
      section = makeUniqueNoThrow<Section>(epub, spineIndex, renderer, cacheSuffix.c_str());
      if (!section) {
        LOG_ERR("SLP", "EPUB: failed to allocate Safe Mode section builder for spine %d", spineIndex);
        return false;
      }
      buildSucceeded = section->createSectionFile(
          readerFontId, SETTINGS.getReaderLineCompression(), SETTINGS.extraParagraphSpacing,
          SETTINGS.forceParagraphIndents, SETTINGS.paragraphAlignment, viewportWidth, viewportHeight,
          SETTINGS.hyphenationEnabled, profile.embeddedStyle, SETTINGS.imageRendering, profile.bionicReadingEnabled,
          profile.guideReadingEnabled, []() {}, nullptr, &layoutAbortedForLowMemory, profile.renderMode,
          sleepBuildOptions);
      if (buildSucceeded) {
        safeModeBuildSucceeded = true;
        usedRenderMode = profile.renderMode;
      }
    }
    if (!buildSucceeded) {
      LOG_ERR("SLP", "EPUB: failed to rebuild section cache for spine %d", spineIndex);
      return false;
    }
    if (safeModeBuildSucceeded) {
      applySafeModeReaderSettings();
      if (!saveRuntimeReaderSettingsForCache(epub->getCachePath())) {
        LOG_ERR("SLP", "EPUB: failed to save Safe Mode reader settings");
      }
    } else if (usedRenderMode != selectedRenderMode) {
      SETTINGS.epubRenderMode = static_cast<uint8_t>(usedRenderMode);
      saveBookRenderModeForCache(epub->getCachePath(), SETTINGS.epubRenderMode);
    }
    releaseReaderSdFontCachesForLowMemory(renderer, "SLP", "sleep-page section cache rebuild");
    LOG_DBG("SLP", "EPUB: section cache rebuilt for spine %d (pages=%u, font=%d, mode=%u free=%u, maxAlloc=%u)",
            spineIndex, section->pageCount, renderFontId, static_cast<unsigned>(usedRenderMode), ESP.getFreeHeap(),
            ESP.getMaxAllocHeap());
  }

  if (pageNumber < 0 || pageNumber >= section->pageCount) return false;
  section->currentPage = pageNumber;

  auto page = section->loadPageFromSectionFile();
  if (!page) {
    LOG_DBG("SLP", "EPUB: failed to load page %d", pageNumber);
    return false;
  }

  renderer.clearScreen(ReaderUtils::readerBackgroundColor());
  page->render(renderer, renderFontId, layout.marginLeft, layout.marginTop, ReaderUtils::readerForegroundBlack());
  drawPublisherPageMarkers(renderer, *page, layout.marginTop, renderer.getScreenHeight() - layout.marginBottom,
                           ReaderUtils::readerForegroundBlack());
  // No displayBuffer call; caller (SleepActivity) handles that after compositing the overlay.
  return true;
}

ScreenshotInfo EpubReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Epub;
  if (epub) {
    snprintf(info.title, sizeof(info.title), "%s", epub->getTitle().c_str());
    info.spineIndex = currentSpineIndex;
  }
  if (section) {
    const int totalPages = section->pageCount;
    info.currentPage = section->currentPage + 1;
    info.totalPages = totalPages;
    if (epub && epub->getBookSize() > 0 && totalPages > 0) {
      const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(totalPages);
      int pct = static_cast<int>(epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f + 0.5f);
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      info.progressPercent = pct;
    }
  }
  return info;
}
