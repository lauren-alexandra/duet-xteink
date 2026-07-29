#include "FileBrowserActivity.h"

#include <MemoryBudget.h>

#include <Arduino.h>
#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Xtc.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

#include "BookActions.h"
#include "BookInfoActivity.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "FileBrowserActionActivity.h"
#include "LibrarySearchActivity.h"
#include "MappedInputManager.h"
#include "RecentBookProgress.h"
#include "activities/reader/BookReadingStats.h"
#include "activities/reader/EpubReaderActivity.h"
#include "activities/reader/LibraryInsights.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/OptionSelectionActivity.h"
#include "components/CompactHeader.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "components/icons/folder.h"
#include "components/themes/minimal/MinimalTheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long GO_HOME_MS = 1000;
constexpr unsigned long COMPLETED_FEEDBACK_MS = 1000;
constexpr unsigned long TITLE_MARQUEE_INITIAL_PAUSE_MS = 1000;
constexpr unsigned long TITLE_MARQUEE_STEP_MS = 450;
constexpr unsigned long TITLE_MARQUEE_END_PAUSE_MS = 850;
constexpr size_t TITLE_MARQUEE_DISABLED = std::numeric_limits<size_t>::max();
constexpr uint8_t TITLE_MARQUEE_MAX_LOOPS = 2;
constexpr int ROOT_HINT_GAP = 20;
// Bookshelf grid geometry is adapted from CrumBLE's MIT-licensed
// RecentBooksGridActivity (Copyright (c) 2025 Dave Allie). CrossInk keeps its
// indexed folder model, while the grid keeps CrumBLE's 2x2/3x3/4x4 density,
// exact-cell thumbnails, centered partial rows, progress tracks, and shared
// selected-book strip.
constexpr int COVER_GRID_DEFAULT_COLUMNS = 2;
constexpr int COVER_GRID_DEFAULT_ROWS = 2;
constexpr int COVER_GRID_SHARED_FOOTER_GAP = 4;
constexpr int COVER_GRID_FOOTER_LINE_GAP = 2;
constexpr int COVER_GRID_CORNER_RADIUS = 2;
// The carousel keeps one high-quality source per visible card: a larger
// center tile and right-sized detail tiles for all four perspective neighbors.
constexpr int COVER_GRID_MAX_COVER_WIDTH = 123;
constexpr int COVER_GRID_MAX_COVER_HEIGHT = 180;
constexpr int COVER_SHADOW_X = 2;
constexpr int COVER_SHADOW_Y = 3;
constexpr unsigned long COVER_BROWSER_IDLE_WORK_DELAY_MS = 220;
constexpr unsigned long COVER_BROWSER_IDLE_REFRESH_DELAY_MS = 400;
// Let repeated clicks coalesce, but show the destination page promptly. Cover
// I/O is paint-gated below, so this frame contains lightweight placeholders
// and a live picker rather than waiting for artwork hydration.
constexpr unsigned long COVER_BROWSER_NAVIGATION_SETTLE_DELAY_MS = 220;

unsigned long coverBrowserNavigationSettleDelayMs() {
  // UC8253 full shelf paints take substantially longer than SSD1677 paints.
  // Keep X3 navigation on lightweight partial updates until the user pauses.
  return gpio.deviceIsX3() ? 360UL : COVER_BROWSER_NAVIGATION_SETTLE_DELAY_MS;
}
// EPUB parsing is deliberately not part of live browsing. A missing cover
// stays a lightweight placeholder until the desktop/export pass creates it.
// Viable only with a clean cache root (Settings > System > Clean Library
// Cache): building a never-opened book's cache inside a ~2,700-entry root
// cost minutes of deaf input per book. A runaway-generation circuit breaker
// below abandons the queue for the visit if a single build runs long.
constexpr bool COVER_BROWSER_GENERATE_THUMBNAILS_DURING_BROWSE = true;
constexpr unsigned long COVER_GENERATION_RUNAWAY_MS = 20000;
// Background cover work only runs after the user has been genuinely idle:
// a short window for cheap probes, a long window before multi-second
// thumbnail generation, so navigation presses always find a live loop.
constexpr unsigned long COVER_WORK_MIN_IDLE_MS = 350;
constexpr unsigned long COVER_GENERATION_MIN_IDLE_MS = 1800;
constexpr unsigned long FOLDER_TRANSITION_SELF_HEAL_MS = 2500;
constexpr unsigned long COVER_GRID_SIGNAL_PREFETCH_INITIAL_DELAY_MS = 20;
constexpr unsigned long COVER_GRID_SIGNAL_PREFETCH_GAP_MS = 220;
constexpr unsigned long COVER_GRID_PREFETCH_INITIAL_DELAY_MS = 100;
constexpr unsigned long COVER_GRID_PREFETCH_GAP_MS = 55;
constexpr size_t COVER_GRID_PREFETCH_BATCH_SIZE = 2;
// Off-page covers are disposable. Keep enough heap after each one for cursor
// windows and normal UI work; generation can additionally release all of them.
constexpr uint32_t COVER_GRID_LOOKAHEAD_MIN_FREE_AFTER_LOAD = 44U * 1024U;
constexpr uint32_t COVER_GRID_LOOKAHEAD_MIN_MAX_ALLOC = 24U * 1024U;
constexpr uint32_t COVER_GRID_LOOKAHEAD_ALLOC_MARGIN = 4U * 1024U;
constexpr uint32_t COVER_GRID_VISIBLE_MIN_FREE_AFTER_LOAD = 24U * 1024U;
constexpr unsigned long COVER_GRID_LOOKAHEAD_RETRY_MS = 1500;
constexpr unsigned long COVER_GRID_GENERATION_INITIAL_DELAY_MS = 300;
constexpr unsigned long COVER_GRID_GENERATION_GAP_MS = 500;
constexpr unsigned long COVER_CAROUSEL_GENERATION_INITIAL_DELAY_MS = 250;
constexpr unsigned long COVER_CAROUSEL_GENERATION_GAP_MS = 350;
constexpr unsigned long COVER_GENERATION_CANCEL_RETRY_MS = 900;
constexpr unsigned long COVER_CAROUSEL_COVER_PREFETCH_INITIAL_DELAY_MS = 20;
constexpr unsigned long COVER_CAROUSEL_COVER_PREFETCH_GAP_MS = 12;
constexpr unsigned long COVER_CAROUSEL_DETAIL_PREFETCH_INITIAL_DELAY_MS = 20;
constexpr unsigned long COVER_CAROUSEL_DETAIL_PREFETCH_GAP_MS = 30;
constexpr unsigned long COVER_CAROUSEL_SIGNAL_PREFETCH_INITIAL_DELAY_MS = 20;
constexpr unsigned long COVER_CAROUSEL_SIGNAL_PREFETCH_GAP_MS = 15;
constexpr int COVER_CAROUSEL_CENTER_MAX_WIDTH = 230;
constexpr int COVER_CAROUSEL_CENTER_MAX_HEIGHT = 338;
constexpr int COVER_CAROUSEL_SIDE_SOURCE_WIDTH = 160;
constexpr int COVER_CAROUSEL_SIDE_SOURCE_HEIGHT = 234;
constexpr int COVER_CAROUSEL_MIN_HEIGHT = 96;
constexpr int COVER_CAROUSEL_CORNER_RADIUS = 4;
constexpr int COVER_CAROUSEL_SELECTION_PADDING = 5;
constexpr int COVER_CAROUSEL_TITLE_LINES = 2;
constexpr size_t NAME_BUFFER_SIZE = 500;
constexpr size_t INDEX_THRESHOLD = 200;
constexpr uint32_t FILE_BROWSER_APPEND_MIN_FREE_AFTER_ALLOC = 48U * 1024U;
constexpr uint32_t FILE_BROWSER_APPEND_MIN_MAX_ALLOC_AFTER_ALLOC = 16U * 1024U;

int coverGridLookaheadWidth() {
  return gpio.deviceIsX3() ? 93 : 94;
}

int coverGridLookaheadHeight() {
  return gpio.deviceIsX3() ? 140 : 142;
}

struct CoverGridLayout {
  int columns = COVER_GRID_DEFAULT_COLUMNS;
  int rows = COVER_GRID_DEFAULT_ROWS;
  int itemsPerPage = COVER_GRID_DEFAULT_COLUMNS * COVER_GRID_DEFAULT_ROWS;
  int coverWidth = 0;
  int coverHeight = 0;
  int horizontalGap = 0;
  int rowSpacing = 0;
  int progressTopGap = 4;
  int progressBarHeight = 5;
  int selectionPadding = 4;
  int selectionOutlineGap = 2;
  int gridWidth = 0;
  int gridHeight = 0;
  int startX = 0;
  int startY = 0;
  int footerX = 0;
  int footerY = 0;
  int footerWidth = 0;
  int footerHeight = 0;
};

struct CoverGridCellGeometry {
  int coverX = 0;
  int coverY = 0;
  int snapshotX = 0;
  int snapshotY = 0;
  int snapshotWidth = 0;
  int snapshotHeight = 0;
};

struct CoverCarouselLayout {
  int centerX = 0;
  int centerY = 0;
  int centerWidth = 0;
  int centerHeight = 0;
  int nearWidth = 0;
  int nearInnerHeight = 0;
  int nearOuterHeight = 0;
  int farWidth = 0;
  int farInnerHeight = 0;
  int farOuterHeight = 0;
  int nearY = 0;
  int farY = 0;
  int leftNearX = 0;
  int rightNearX = 0;
  int leftFarX = 0;
  int rightFarX = 0;
  int textWidth = 0;
  int titleY = 0;
  int titleLineHeight = 0;
  int authorY = 0;
  int counterY = 0;
  int progressY = 0;
};

CoverGridLayout calculateCoverGridLayout(const GfxRenderer& renderer, const int contentTop, const int contentHeight,
                                         const int sidePadding, const int themeSpacing) {
  CoverGridLayout layout;
  const int pageWidth = renderer.getScreenWidth();
  int targetCoverWidth = 220;
  int targetCoverHeight = 320;
  switch (SETTINGS.fileBrowserGridLayout) {
    case CrossPointSettings::FILE_BROWSER_GRID_4X4:
      layout.columns = 4;
      layout.rows = 4;
      targetCoverWidth = 100;
      targetCoverHeight = 150;
      layout.horizontalGap = 10;
      layout.rowSpacing = 2;
      layout.progressTopGap = 2;
      layout.progressBarHeight = 4;
      layout.selectionPadding = 2;
      layout.selectionOutlineGap = 1;
      break;
    case CrossPointSettings::FILE_BROWSER_GRID_3X3:
      layout.columns = 3;
      layout.rows = 3;
      targetCoverWidth = 126;
      targetCoverHeight = 184;
      layout.horizontalGap = 12;
      layout.rowSpacing = 12;
      layout.progressTopGap = 3;
      layout.progressBarHeight = 4;
      break;
    case CrossPointSettings::FILE_BROWSER_GRID_2X2:
    default:
      layout.columns = 2;
      layout.rows = 2;
      targetCoverWidth = 220;
      targetCoverHeight = 320;
      layout.horizontalGap = std::max(10, themeSpacing / 2);
      layout.rowSpacing = 2;
      layout.progressTopGap = 3;
      layout.progressBarHeight = 6;
      layout.selectionPadding = 3;
      layout.selectionOutlineGap = 1;
      break;
  }

  const int titleLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int authorLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  layout.footerHeight = titleLineHeight + COVER_GRID_FOOTER_LINE_GAP + authorLineHeight + 4;
  const int gridAreaHeight = std::max(1, contentHeight - layout.footerHeight - COVER_GRID_SHARED_FOOTER_GAP);
  const int usableWidth =
      std::max(1, pageWidth - sidePadding * 2 - layout.horizontalGap * (layout.columns - 1));
  const int maxCoverWidth = std::max(1, usableWidth / layout.columns);
  // Progress lives on the lower edge of the artwork instead of consuming a
  // separate strip below every row. This gives the small X3 panel noticeably
  // larger covers while retaining the same reading signal.
  const int fixedRowHeight = 0;
  const int maxCoverHeight = std::max(
      1, (gridAreaHeight - fixedRowHeight * layout.rows - layout.rowSpacing * (layout.rows - 1)) / layout.rows);

  layout.coverWidth = std::min(targetCoverWidth, maxCoverWidth);
  layout.coverHeight = std::max(1, layout.coverWidth * targetCoverHeight / targetCoverWidth);
  if (layout.coverHeight > maxCoverHeight) {
    layout.coverHeight = maxCoverHeight;
    layout.coverWidth = std::max(1, layout.coverHeight * targetCoverWidth / targetCoverHeight);
  }

  layout.itemsPerPage = layout.columns * layout.rows;
  layout.gridWidth = layout.columns * layout.coverWidth + (layout.columns - 1) * layout.horizontalGap;
  layout.gridHeight = layout.rows * (layout.coverHeight + fixedRowHeight) +
                      (layout.rows - 1) * layout.rowSpacing;
  layout.startX = (pageWidth - layout.gridWidth) / 2;
  layout.startY = contentTop + std::max(0, (gridAreaHeight - layout.gridHeight) / 2);
  layout.footerX = std::max(sidePadding, layout.startX);
  layout.footerY = contentTop + contentHeight - layout.footerHeight;
  layout.footerWidth = std::min(pageWidth - sidePadding * 2, layout.gridWidth);
  return layout;
}

bool calculateCoverGridCellGeometry(const CoverGridLayout& layout, const int pageStart, const size_t index,
                                    const int pageItemCount, CoverGridCellGeometry& geometry) {
  if (pageStart < 0 || index < static_cast<size_t>(pageStart) || layout.itemsPerPage <= 0 || layout.columns <= 0) {
    return false;
  }
  const int indexInPage = static_cast<int>(index) - pageStart;
  if (indexInPage < 0 || indexInPage >= layout.itemsPerPage || indexInPage >= pageItemCount) return false;

  const int column = indexInPage % layout.columns;
  const int row = indexInPage / layout.columns;
  const int rowsThisPage = (pageItemCount + layout.columns - 1) / layout.columns;
  const int lastRowCount = pageItemCount - (rowsThisPage - 1) * layout.columns;
  const bool partialLastRow = row == rowsThisPage - 1 && lastRowCount < layout.columns;
  const int rowCellCount = partialLastRow ? lastRowCount : layout.columns;
  const int rowWidth = rowCellCount * layout.coverWidth + (rowCellCount - 1) * layout.horizontalGap;
  const int rowStartX = layout.startX + (layout.gridWidth - rowWidth) / 2;
  // The progress bar is painted inside the lower edge of the cover. Counting
  // it again here made every row advance farther than calculateCoverGridLayout
  // reserved, so the third row collided with the footer on both X3 and X4.
  const int rowStride = layout.coverHeight + layout.rowSpacing;
  geometry.coverX = rowStartX + column * (layout.coverWidth + layout.horizontalGap);
  geometry.coverY = layout.startY + row * rowStride;

  const int outerInset = layout.selectionPadding + layout.selectionOutlineGap;
  geometry.snapshotX = std::max(0, geometry.coverX - outerInset);
  geometry.snapshotY = std::max(0, geometry.coverY - outerInset);
  geometry.snapshotWidth = layout.coverWidth + outerInset * 2;
  const int snapshotBottom = geometry.coverY + layout.coverHeight;
  geometry.snapshotHeight = std::max(1, snapshotBottom - geometry.snapshotY);
  return true;
}

void drawCoverGridSelection(const GfxRenderer& renderer, const CoverGridLayout& layout,
                            const CoverGridCellGeometry& geometry) {
  const int outerInset = layout.selectionPadding + layout.selectionOutlineGap;
  renderer.drawRoundedRect(geometry.coverX - layout.selectionPadding,
                           geometry.coverY - layout.selectionPadding,
                           layout.coverWidth + layout.selectionPadding * 2,
                           layout.coverHeight + layout.selectionPadding * 2, 3,
                           COVER_GRID_CORNER_RADIUS + layout.selectionPadding, true);
  renderer.drawRoundedRect(geometry.coverX - outerInset, geometry.coverY - outerInset,
                           layout.coverWidth + outerInset * 2, layout.coverHeight + outerInset * 2, 1,
                           COVER_GRID_CORNER_RADIUS + outerInset, true);
}

void eraseCoverGridSelection(const GfxRenderer& renderer, const CoverGridLayout& layout,
                             const CoverGridCellGeometry& geometry) {
  const int outerInset = layout.selectionPadding + layout.selectionOutlineGap;
  const int left = geometry.coverX - outerInset;
  const int top = geometry.coverY - outerInset;
  const int fullWidth = layout.coverWidth + outerInset * 2;

  // Only erase the ring outside the cover. The artwork itself never changes,
  // so this path stays independent of cover loading and decoding.
  renderer.fillRect(left, top, fullWidth, outerInset, false);
  renderer.fillRect(left, geometry.coverY + layout.coverHeight, fullWidth, outerInset, false);
  renderer.fillRect(left, geometry.coverY, outerInset, layout.coverHeight, false);
  renderer.fillRect(geometry.coverX + layout.coverWidth, geometry.coverY, outerInset, layout.coverHeight, false);
}

void beginCoverArtwork(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                       const int cornerRadius) {
  renderer.fillRoundedRect(x + COVER_SHADOW_X, y + COVER_SHADOW_Y, width, height, cornerRadius, Color::Black);
  renderer.fillRoundedRect(x, y, width, height, cornerRadius, Color::White);
}

void finishCoverArtwork(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                        const int cornerRadius) {
  renderer.maskRoundedRectOutsideCorners(x, y, width, height, cornerRadius, Color::White);
  renderer.drawRoundedRect(x, y, width, height, 1, cornerRadius, true);
}

void drawPerspectiveCoverOutline(const GfxRenderer& renderer, const int x, const int y, const int width,
                                 const int leftHeight, const int rightHeight) {
  const int maxHeight = std::max(leftHeight, rightHeight);
  const int topLeft = (maxHeight - leftHeight) / 2;
  const int topRight = (maxHeight - rightHeight) / 2;
  const int bottomLeft = topLeft + leftHeight - 1;
  const int bottomRight = topRight + rightHeight - 1;
  const int rightX = x + width - 1;
  renderer.drawLine(x, y + topLeft, rightX, y + topRight, 2, true);
  renderer.drawLine(x, y + bottomLeft, rightX, y + bottomRight, 2, true);
  renderer.fillRect(x, y + topLeft, 2, leftHeight, true);
  renderer.fillRect(rightX - 1, y + topRight, 2, rightHeight, true);
  renderer.fillRect(x, y + maxHeight + 1, width, 2, false);
}

CoverCarouselLayout calculateCoverCarouselLayout(const GfxRenderer& renderer, const int contentTop,
                                                 const int contentHeight, const int sidePadding) {
  CoverCarouselLayout layout;
  const int pageWidth = renderer.getScreenWidth();
  layout.titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int authorLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int counterLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int footerHeight =
      10 + layout.titleLineHeight * COVER_CAROUSEL_TITLE_LINES + 2 + authorLineHeight + 4 + counterLineHeight + 8 + 6;

  layout.centerHeight =
      std::min(COVER_CAROUSEL_CENTER_MAX_HEIGHT, std::max(COVER_CAROUSEL_MIN_HEIGHT, contentHeight - footerHeight));
  layout.centerWidth = std::max(1, layout.centerHeight * COVER_GRID_MAX_COVER_WIDTH / COVER_GRID_MAX_COVER_HEIGHT);
  const int maxCenterWidth = std::min(COVER_CAROUSEL_CENTER_MAX_WIDTH, pageWidth * 52 / 100);
  if (layout.centerWidth > maxCenterWidth) {
    layout.centerWidth = std::max(1, maxCenterWidth);
    layout.centerHeight = std::max(1, layout.centerWidth * COVER_GRID_MAX_COVER_HEIGHT / COVER_GRID_MAX_COVER_WIDTH);
  }

  const int totalHeight = layout.centerHeight + footerHeight;
  layout.centerX = (pageWidth - layout.centerWidth) / 2;
  layout.centerY = contentTop + std::max(0, (contentHeight - totalHeight) / 2);

  // The surrounding books are deliberately slimmer perspective cards. Their
  // center-facing edge is shorter, which keeps the selected cover dominant
  // while still showing enough artwork to recognize each neighbor.
  layout.nearWidth = std::max(1, layout.centerWidth * 34 / 100);
  layout.farWidth = std::max(1, layout.centerWidth * 22 / 100);
  layout.nearInnerHeight = std::max(1, layout.centerHeight * 66 / 100);
  layout.nearOuterHeight = std::max(1, layout.centerHeight * 76 / 100);
  layout.farInnerHeight = std::max(1, layout.centerHeight * 43 / 100);
  layout.farOuterHeight = std::max(1, layout.centerHeight * 52 / 100);
  const int nearMaxHeight = std::max(layout.nearInnerHeight, layout.nearOuterHeight);
  const int farMaxHeight = std::max(layout.farInnerHeight, layout.farOuterHeight);
  layout.nearY = layout.centerY + (layout.centerHeight - nearMaxHeight) / 2;
  layout.farY = layout.centerY + (layout.centerHeight - farMaxHeight) / 2;
  constexpr int nearOverlap = 4;
  constexpr int farOverlap = 2;
  constexpr int nearInset = 10;
  const int baseLeftNearX = layout.centerX - layout.nearWidth + nearOverlap;
  const int baseRightNearX = layout.centerX + layout.centerWidth - nearOverlap;
  layout.leftNearX = baseLeftNearX + nearInset;
  layout.rightNearX = baseRightNearX - nearInset;
  layout.leftFarX = std::max(sidePadding, baseLeftNearX - layout.farWidth + farOverlap);
  layout.rightFarX =
      std::min(pageWidth - sidePadding - layout.farWidth, baseRightNearX + layout.nearWidth - farOverlap);

  layout.textWidth = std::max(40, pageWidth - sidePadding * 2);
  layout.titleY = layout.centerY + layout.centerHeight + 10;
  layout.authorY = layout.titleY + layout.titleLineHeight * COVER_CAROUSEL_TITLE_LINES + 2;
  layout.counterY = layout.authorY + authorLineHeight + 4;
  layout.progressY = layout.counterY + counterLineHeight + 5;
  return layout;
}

int moveHorizontalInCoverGrid(const int currentIndex, const int totalItems, const bool moveRight) {
  if (totalItems <= 0) return 0;
  return moveRight ? ButtonNavigator::nextIndex(currentIndex, totalItems)
                   : ButtonNavigator::previousIndex(currentIndex, totalItems);
}

int moveVerticalInCoverGrid(const int currentIndex, const int totalItems, const int columns, const int itemsPerPage,
                            const bool moveDown) {
  if (totalItems <= 0 || columns <= 0) return 0;
  const int safeItemsPerPage = std::max(columns, itemsPerPage);
  if (safeItemsPerPage % columns != 0) {
    LOG_ERR("FileBrowser", "Cover grid requires whole rows (itemsPerPage=%d columns=%d)", safeItemsPerPage, columns);
    return currentIndex;
  }

  const int totalPages = (totalItems + safeItemsPerPage - 1) / safeItemsPerPage;
  const int currentPage = currentIndex / safeItemsPerPage;
  const int indexInPage = currentIndex % safeItemsPerPage;
  const int currentRow = indexInPage / columns;
  const int currentColumn = indexInPage % columns;
  const int rowsPerPage = safeItemsPerPage / columns;

  if (moveDown) {
    if (currentRow < rowsPerPage - 1) {
      const int nextRowCandidate = currentIndex + columns;
      if (nextRowCandidate < totalItems && nextRowCandidate / safeItemsPerPage == currentPage) {
        return nextRowCandidate;
      }
    }
    const int nextPage = (currentPage + 1) % totalPages;
    const int nextPageStart = nextPage * safeItemsPerPage;
    const int nextPageCount = std::min(safeItemsPerPage, totalItems - nextPageStart);
    return nextPageStart + std::min(currentColumn, std::max(0, nextPageCount - 1));
  }

  if (currentRow > 0) {
    return currentIndex - columns;
  }
  const int previousPage = (currentPage - 1 + totalPages) % totalPages;
  const int previousPageStart = previousPage * safeItemsPerPage;
  const int previousPageCount = std::min(safeItemsPerPage, totalItems - previousPageStart);
  int previousPageCandidate = previousPageStart + ((previousPageCount - 1) / columns) * columns + currentColumn;
  while (previousPageCandidate >= previousPageStart + previousPageCount) {
    previousPageCandidate -= columns;
  }
  return std::max(previousPageStart, previousPageCandidate);
}

bool isDefaultSleepFolderPath(const std::string& path) { return path == "/sleep" || path == "/.sleep"; }

bool isSleepImageFile(const std::string& path) {
  return FsHelpers::hasBmpExtension(path) || FsHelpers::hasPngExtension(path);
}

bool isMacOSMetadataEntry(std::string_view filename) {
  return filename.rfind("._", 0) == 0 || filename == ".DS_Store" || filename == ".Spotlight-V100" ||
         filename == ".Trashes" || filename == ".fseventsd";
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.length() != b.length()) return false;
  for (size_t i = 0; i < a.length(); ++i) {
    if (tolower(static_cast<unsigned char>(a[i])) != tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

bool isWindowsMetadataEntry(std::string_view filename) {
  return equalsIgnoreCase(filename, "System Volume Information") || equalsIgnoreCase(filename, "$RECYCLE.BIN") ||
         equalsIgnoreCase(filename, "desktop.ini") || equalsIgnoreCase(filename, "Thumbs.db") ||
         equalsIgnoreCase(filename, "IndexerVolumeGuid") || equalsIgnoreCase(filename, "WPSettings.dat");
}

size_t estimateNextVectorCapacity(size_t size, size_t capacity) {
  if (size < capacity) {
    return capacity;
  }
  if (capacity == 0) {
    return 1;
  }
  return capacity * 2;
}

bool hasHeapForFileEntryAppend(const std::vector<std::string>& files, size_t entryLen) {
  const size_t nextCapacity = estimateNextVectorCapacity(files.size(), files.capacity());
  const uint32_t vectorGrowthBytes =
      (nextCapacity == files.capacity()) ? 0U : static_cast<uint32_t>(nextCapacity * sizeof(std::string));
  const uint32_t stringBytes = static_cast<uint32_t>(entryLen + 1);
  const uint32_t largestNeeded = std::max(vectorGrowthBytes, stringBytes);

  return ESP.getFreeHeap() >= vectorGrowthBytes + stringBytes + FILE_BROWSER_APPEND_MIN_FREE_AFTER_ALLOC &&
         ESP.getMaxAllocHeap() >= largestNeeded + FILE_BROWSER_APPEND_MIN_MAX_ALLOC_AFTER_ALLOC;
}

bool hasFileMetadata(const std::string& path) {
  return FsHelpers::hasEpubExtension(path) || FsHelpers::hasXtcExtension(path) || FsHelpers::hasTxtExtension(path) ||
         FsHelpers::hasMarkdownExtension(path);
}

bool isSupportedBrowserFile(std::string_view filename) {
  return FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename) ||
         FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename) ||
         FsHelpers::hasBmpExtension(filename) || FsHelpers::hasPngExtension(filename);
}

bool acceptCommon(const char* name, bool isDir) {
  if (isMacOSMetadataEntry(name) || isWindowsMetadataEntry(name) || (!SETTINGS.showHiddenFiles && name[0] == '.')) {
    return false;
  }
  return isDir || isSupportedBrowserFile(name);
}

bool acceptFirmware(const char* name, bool isDir) {
  if (isMacOSMetadataEntry(name) || isWindowsMetadataEntry(name) || (!SETTINGS.showHiddenFiles && name[0] == '.')) {
    return false;
  }
  return isDir || FsHelpers::checkFileExtension(std::string_view{name}, ".bin");
}

std::string buildFullPath(std::string basepath, const std::string& entry) {
  if (basepath.back() != '/') basepath += "/";
  return basepath + entry;
}

std::string normalizeDirectoryPath(std::string path) {
  while (path.length() > 1 && path.back() == '/') {
    path.pop_back();
  }
  return path;
}

bool isBooksLibraryPath(const std::string& path) {
  const std::string normalized = normalizeDirectoryPath(path);
  if (normalized.size() < 2 || normalized.front() != '/') return false;
  const size_t segmentEnd = normalized.find('/', 1);
  const std::string_view topLevel =
      segmentEnd == std::string::npos ? std::string_view(normalized).substr(1)
                                      : std::string_view(normalized).substr(1, segmentEnd - 1);
  return equalsIgnoreCase(topLevel, "Books");
}

bool isSleepFolderPreferencePath(const std::string& path) { return !path.empty() && !isDefaultSleepFolderPath(path); }

FileIndex::SortMode currentFileIndexSortMode() {
  return SETTINGS.fileBrowserSort == CrossPointSettings::FILE_BROWSER_SORT_TITLE ? FileIndex::SortMode::BookTitle
                                                                                 : FileIndex::SortMode::Filename;
}

void sortLoadedFileList(std::vector<std::string>& files, const bool booksMode) {
  if (booksMode && SETTINGS.fileBrowserSort == CrossPointSettings::FILE_BROWSER_SORT_TITLE) {
    FsHelpers::sortFileListByBookTitle(files);
  } else {
    FsHelpers::sortFileList(files);
  }
}

bool containsHiddenPathSegment(const std::string& path) {
  if (path.empty()) return false;
  size_t segmentStart = (path.front() == '/') ? 1 : 0;
  while (segmentStart < path.length()) {
    const size_t segmentEnd = path.find('/', segmentStart);
    if (segmentStart < path.length() && path[segmentStart] == '.') {
      return true;
    }
    if (segmentEnd == std::string::npos) {
      break;
    }
    segmentStart = segmentEnd + 1;
  }
  return false;
}

uint32_t hashTitleMarqueeEntry(const std::string& basepath, const std::string& entry) {
  uint32_t hash = 2166136261u;
  const auto addBytes = [&hash](const std::string& value) {
    for (const unsigned char byte : value) {
      hash ^= byte;
      hash *= 16777619u;
    }
  };
  addBytes(basepath);
  addBytes(entry);
  return hash == 0 ? 1 : hash;
}

bool deadlineReached(const unsigned long now, const unsigned long deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

size_t nextUtf8Offset(const std::string& text, size_t offset) {
  if (offset >= text.size()) return text.size();
  ++offset;
  while (offset < text.size() && (static_cast<unsigned char>(text[offset]) & 0xC0) == 0x80) {
    ++offset;
  }
  return offset;
}

void collectMetadataPathsRecursively(const std::string& dirPath, std::vector<std::string>& paths) {
  auto dir = Storage.open(dirPath.c_str());
  if (!dir || !dir.isDirectory()) {
    LOG_ERR("FileBrowser", "Failed to scan directory metadata before delete: %s", dirPath.c_str());
    return;
  }

  char name[256];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    file.getName(name, sizeof(name));
    const std::string childPath = buildFullPath(dirPath, name);
    if (file.isDirectory()) {
      collectMetadataPathsRecursively(childPath, paths);
    } else if (hasFileMetadata(childPath)) {
      paths.push_back(childPath);
    }
    file.close();
  }
  dir.close();
}

std::string getFileName(std::string filename);
}  // namespace

bool FileBrowserActivity::loadFilesIntoVector(size_t cap, bool& overflow) {
  files.clear();
  overflow = false;

  auto root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
    if (root) {
      root.close();
    }
    return false;
  }

  root.rewindDirectory();

  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "fileNameBuffer not allocated");
    root.close();
    return false;
  }

  const auto accept = (mode == Mode::PickFirmware) ? acceptFirmware : acceptCommon;

  files.reserve(std::min<size_t>(cap, INDEX_THRESHOLD));
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
    const bool isDir = file.isDirectory();
    if (!accept(fileNameBuffer.get(), isDir)) {
      file.close();
      continue;
    }

    if (files.size() >= cap) {
      overflow = true;
      file.close();
      break;
    }

    size_t entryLen = std::strlen(fileNameBuffer.get());
    if (isDir) {
      if (entryLen + 1 >= NAME_BUFFER_SIZE) {
        LOG_ERR("FileBrowser", "Skipping oversized directory entry: %s", fileNameBuffer.get());
        file.close();
        continue;
      }
      fileNameBuffer[entryLen++] = '/';
      fileNameBuffer[entryLen] = '\0';
    }

    if (!hasHeapForFileEntryAppend(files, entryLen)) {
      fileListMemoryLimited = true;
      LOG_ERR("FileBrowser", "Low heap while loading %s (entries=%u free=%u maxAlloc=%u)", basepath.c_str(),
              static_cast<unsigned>(files.size()), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      file.close();
      root.close();
      files.clear();
      return true;
    }

    files.emplace_back(fileNameBuffer.get());
    file.close();
  }
  root.close();
  return true;
}

void FileBrowserActivity::loadFiles(const bool forceRescan) {
  // Accumulates across folder navigations; the exit breadcrumb reports the sum.
  struct ScopedLoadTimer {
    unsigned long startMs;
    unsigned long& accumulator;
    ~ScopedLoadTimer() { accumulator += millis() - startMs; }
  } loadTimer{millis(), pickerLoadFilesMs};
  writePickerHeartbeat("loading", 0);
  resetTitleMarquee();
  resetCoverPage();
  coverGridAvailable = false;
  usingIndex = false;
  clearIndexNameCache();
  fileListMemoryLimited = false;
  if (fileIndex) fileIndex->close();
  if (forceRescan && fileIndex) fileIndex->forgetTrustedIndex(basepath.c_str());

  const auto accept = (mode == Mode::PickFirmware) ? acceptFirmware : acceptCommon;
  // Skip the small vector probe entirely when this activity has already
  // verified an index for the folder. This makes Back out of a large library
  // folder use a few index reads instead of another directory scan.
  const FileIndex::SortMode sortMode = mode == Mode::Books ? currentFileIndexSortMode() : FileIndex::SortMode::Filename;
  if (!forceRescan && fileIndex && indexEntry && fileIndex->hasTrustedIndex(basepath.c_str(), accept, sortMode) &&
      fileIndex->open(basepath.c_str(), accept, sortMode)) {
    usingIndex = true;
    refreshCoverGridAvailability();
    requestUpdate(true);
    return;
  }

  bool overflow = false;
  if (!loadFilesIntoVector(INDEX_THRESHOLD, overflow)) {
    return;
  }

  if (!overflow || fileListMemoryLimited) {
    sortLoadedFileList(files, mode == Mode::Books);
    refreshCoverGridAvailability();
    applyBookStatusFilter();
    return;
  }

  files.clear();
  files.shrink_to_fit();

  if (!fileIndex) fileIndex = makeUniqueNoThrow<FileIndex>();
  if (!indexEntry) indexEntry = makeUniqueNoThrow<FileIndex::Entry>();
  if (fileIndex && indexEntry) {
    {
      RenderLock lock(*this);
      GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
    }

    if (fileIndex->open(basepath.c_str(), accept, sortMode)) {
      usingIndex = true;
      refreshCoverGridAvailability();
      requestUpdate(true);
      return;
    }
  } else {
    LOG_ERR("FileBrowser", "index alloc failed");
  }

  LOG_ERR("FileBrowser", "index unavailable for %s; showing first %u entries", basepath.c_str(),
          static_cast<unsigned>(INDEX_THRESHOLD));
  overflow = false;
  loadFilesIntoVector(INDEX_THRESHOLD, overflow);
  sortLoadedFileList(files, mode == Mode::Books);
  refreshCoverGridAvailability();
  applyBookStatusFilter();
  requestUpdate(true);
}

size_t FileBrowserActivity::entryCount() const {
  return usingIndex && fileIndex ? fileIndex->totalCount() : files.size();
}

void FileBrowserActivity::clearIndexNameCache() {
  for (size_t i = 0; i < INDEX_ROW_CACHE_SIZE; i++) {
    indexCachedRows[i] = SIZE_MAX;
    indexCachedNames[i].clear();
  }
}

const char* FileBrowserActivity::entryNameAt(size_t row) {
  if (!usingIndex) {
    return files[row].c_str();
  }

  const size_t cacheSlot = row % INDEX_ROW_CACHE_SIZE;
  if (indexCachedRows[cacheSlot] != row) {
    if (!fileIndex || !indexEntry || !fileIndex->entryAt(row, *indexEntry)) {
      LOG_ERR("FileBrowser", "index read failed at row %u", static_cast<unsigned>(row));
      indexCachedRows[cacheSlot] = SIZE_MAX;
      indexCachedNames[cacheSlot] = "?";
      return indexCachedNames[cacheSlot].c_str();
    }

    indexCachedNames[cacheSlot].assign(indexEntry->name);
    if (indexEntry->isDir) indexCachedNames[cacheSlot] += '/';
    indexCachedRows[cacheSlot] = row;
  }
  return indexCachedNames[cacheSlot].c_str();
}

void FileBrowserActivity::onEnter() {
  Activity::onEnter();
  pickerEnterMs = millis();

  fileNameBuffer = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "name buffer alloc failed (%u bytes)", static_cast<unsigned>(NAME_BUFFER_SIZE));
    fileListMemoryLimited = true;
    requestUpdate();
    return;
  }

  selectorIndex = 0;

  // If Confirm was held while this activity opened (typical when launched from a menu), ignore
  // its release — otherwise we'd immediately auto-open whatever is at index 0.
  lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);

  auto root = Storage.open(basepath.c_str());
  if (!root) {
    basepath = "/";
    loadFiles(true);
    requestUpdate();
    return;
  }

  const bool rootIsDirectory = root.isDirectory();
  root.close();

  if (!rootIsDirectory) {
    lockLongPressBack = mappedInput.isPressed(MappedInputManager::Button::Back);

    const std::string oldPath = basepath;
    basepath = FsHelpers::extractFolderPath(basepath);
    loadFiles(true);

    const auto pos = oldPath.find_last_of('/');
    const std::string fileName = oldPath.substr(pos + 1);
    selectorIndex = findEntry(fileName);
  } else {
    loadFiles(true);
  }

  requestUpdate();
}

void FileBrowserActivity::onExit() {
  Activity::onExit();
  if (pickerEnterMs != 0 && mode == Mode::Books) {
    FsFile timingFile;
    if (Storage.openFileForWrite("FileBrowser", DUET_STATE_ROOT_PATH "/picker_timing.txt", timingFile)) {
      char buf[256];
      const int n = snprintf(
          buf, sizeof(buf),
          "entries=%u loadFiles=%lums firstRender=%lums signals=%u signalMs=%lums signalMax=%lums gens=%u "
          "genMs=%lums genMax=%lums genQ=%u genSkipHeap=%u free=%u maxAlloc=%u\n",
          static_cast<unsigned>(files.size()), pickerLoadFilesMs,
          pickerFirstRenderMs > pickerEnterMs ? pickerFirstRenderMs - pickerEnterMs : 0UL,
          static_cast<unsigned>(pickerSignalCount), pickerSignalTotalMs, pickerSignalMaxMs,
          static_cast<unsigned>(pickerGenCount), pickerGenTotalMs, pickerGenMaxMs,
          static_cast<unsigned>(pickerGenQueuedPeak), static_cast<unsigned>(pickerGenSkipHeap), ESP.getFreeHeap(),
          ESP.getMaxAllocHeap());
      if (n > 0) timingFile.write(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(n));
      timingFile.close();
    }
  }
  resetTitleMarquee();
  resetCoverPage();
  files.clear();
  fileNameBuffer.reset();
  fileIndex.reset();
  indexEntry.reset();
  clearIndexNameCache();
  usingIndex = false;
}

void FileBrowserActivity::requestUpdate(const bool immediate) {
  const auto pending = static_cast<CoverRefresh>(pendingCoverRefresh.load(std::memory_order_acquire));
  // Background cover work must never overwrite the focused partial refresh
  // that was just queued by a button press. Its next idle pass will request a
  // full shelf only after that focus update has landed.
  if (!immediate && (isCoverGridActive() || isCoverCarouselActive()) && isFocusCoverRefresh(pending)) {
    return;
  }
  pendingCoverRefresh.store(static_cast<uint8_t>(CoverRefresh::Full), std::memory_order_release);
  pendingCoverRefreshEpoch.store(coverNavigationEpoch.load(std::memory_order_acquire), std::memory_order_release);
  Activity::requestUpdate(immediate);
}

void FileBrowserActivity::requestFastCoverUpdate(const CoverRefresh refresh) {
  // A stale background repaint must never win over a fresh cursor move. The
  // next idle pass will draw hydrated covers after the user stops navigating.
  pendingCoverRefresh.store(static_cast<uint8_t>(refresh), std::memory_order_release);
  pendingCoverRefreshEpoch.store(coverNavigationEpoch.load(std::memory_order_acquire), std::memory_order_release);
  Activity::requestUpdate();
}

void FileBrowserActivity::promptDeleteFile(const std::string& fullPath, const std::string& entry) {
  auto handler = [this, fullPath](const ActivityResult& res) {
    if (res.isCancelled) {
      LOG_DBG("FileBrowser", "Delete cancelled by user");
      return;
    }

    LOG_DBG("FileBrowser", "Attempting to delete: %s", fullPath.c_str());
    BookActions::clearFileMetadata(fullPath);
    if (!Storage.remove(fullPath.c_str())) {
      LOG_ERR("FileBrowser", "Failed to delete file: %s", fullPath.c_str());
      return;
    }

    LOG_DBG("FileBrowser", "Deleted successfully");
    if (isPinnedSleepFavorite(fullPath)) {
      unpinSleepFavorite();
    }

    loadFiles(true);
    if (entryCount() == 0) {
      selectorIndex = 0;
    } else if (selectorIndex >= entryCount()) {
      selectorIndex = entryCount() - 1;
    }
    requestUpdate(true);
  };

  const std::string heading = tr(STR_DELETE) + std::string("? ");
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, entry), handler);
}

void FileBrowserActivity::promptDeleteDirectory(const std::string& fullPath, const std::string& entry,
                                                const bool ignoreInitialConfirmRelease) {
  const std::string dirPath = normalizeDirectoryPath(fullPath);
  auto handler = [this, dirPath](const ActivityResult& res) {
    longPressConfirmHandled = false;
    if (res.isCancelled) {
      LOG_DBG("FileBrowser", "Delete cancelled by user");
      return;
    }

    std::vector<std::string> metadataPaths;
    collectMetadataPathsRecursively(dirPath, metadataPaths);

    LOG_DBG("FileBrowser", "Attempting to delete directory: %s", dirPath.c_str());
    if (!Storage.removeDir(dirPath.c_str())) {
      LOG_ERR("FileBrowser", "Failed to delete directory: %s", dirPath.c_str());
      return;
    }

    LOG_DBG("FileBrowser", "Deleted successfully");
    for (const auto& metadataPath : metadataPaths) {
      BookActions::clearFileMetadata(metadataPath);
    }

    const std::string favoritePrefix = dirPath + "/";
    if (!APP_STATE.favoriteSleepImagePath.empty() && APP_STATE.favoriteSleepImagePath.rfind(favoritePrefix, 0) == 0) {
      unpinSleepFavorite();
    }
    if (isPreferredSleepFolder(dirPath)) {
      clearPreferredSleepFolder();
    }

    loadFiles(true);
    if (entryCount() == 0) {
      selectorIndex = 0;
    } else if (selectorIndex >= entryCount()) {
      selectorIndex = entryCount() - 1;
    }
    requestUpdate(true);
  };

  const std::string heading = tr(STR_DELETE) + std::string("? ");
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, entry, ignoreInitialConfirmRelease),
      handler);
}

void FileBrowserActivity::showDirectoryActionMenu(const std::string& entry, bool ignoreInitialConfirmRelease) {
  const std::string fullPath = normalizeDirectoryPath(buildFullPath(basepath, entry));
  const bool useDefaultFolders = isDefaultSleepFolderPath(fullPath) || isPreferredSleepFolder(fullPath);
  std::vector<FileBrowserActionActivity::MenuItem> items;
  if (mode == Mode::Books) {
    items.push_back({FileBrowserAction::SearchLibrary, StrId::STR_SEARCH});
    items.push_back({FileBrowserAction::SortBooks, StrId::STR_FILE_BROWSER_SORT});
  }
  items.push_back({useDefaultFolders ? FileBrowserAction::ClearSleepFolder : FileBrowserAction::SetSleepFolder,
                   useDefaultFolders ? StrId::STR_USE_DEFAULT_SLEEP_FOLDERS : StrId::STR_SET_AS_SLEEP_FOLDER});
  items.push_back({FileBrowserAction::Delete, StrId::STR_DELETE});

  startActivityForResult(std::make_unique<FileBrowserActionActivity>(renderer, mappedInput, getFileName(entry),
                                                                     std::move(items), ignoreInitialConfirmRelease),
                         [this, fullPath, entry](const ActivityResult& result) {
                           longPressConfirmHandled = false;
                           if (result.isCancelled) {
                             return;
                           }

                           const auto action =
                               static_cast<FileBrowserAction>(std::get<FileBrowserActionResult>(result.data).action);
                           switch (action) {
                             case FileBrowserAction::Delete:
                               promptDeleteDirectory(fullPath, entry);
                               return;
                             case FileBrowserAction::SetSleepFolder:
                               setPreferredSleepFolder(fullPath);
                               return;
                             case FileBrowserAction::ClearSleepFolder:
                               clearPreferredSleepFolder();
                               return;
                             case FileBrowserAction::SearchLibrary:
                               showLibrarySearch();
                               return;
                             case FileBrowserAction::SortBooks:
                               showBookSortMenu();
                               return;
                             case FileBrowserAction::DeleteCache:
                             case FileBrowserAction::DeleteStats:
                             case FileBrowserAction::ToggleCompleted:
                             case FileBrowserAction::RemoveFromRecents:
                             case FileBrowserAction::PinFavorite:
                             case FileBrowserAction::UnpinFavorite:
                             case FileBrowserAction::ViewBookmarks:
                             case FileBrowserAction::ViewClippings:
                             case FileBrowserAction::DeleteBookmarks:
                             case FileBrowserAction::DeleteClippings:
                             case FileBrowserAction::EpubRenderMode:
                             case FileBrowserAction::ResetReaderSettings:
                             case FileBrowserAction::FilterBooks:
                             case FileBrowserAction::MoreInfo:
                             case FileBrowserAction::AddBookFavorite:
                             case FileBrowserAction::RemoveBookFavorite:
                             case FileBrowserAction::MoveFavoriteUp:
                             case FileBrowserAction::MoveFavoriteDown:
                               return;
                           }
                         });
}

void FileBrowserActivity::pinSleepFavorite(const std::string& fullPath) {
  APP_STATE.favoriteSleepImagePath = fullPath;
  if (!APP_STATE.saveToFile()) {
    LOG_ERR("FileBrowser", "Failed to save favorite sleep image path: %s", fullPath.c_str());
    return;
  }
  LOG_INF("FileBrowser", "Pinned favorite sleep image: %s", fullPath.c_str());
  requestUpdate();
}

void FileBrowserActivity::unpinSleepFavorite() {
  if (APP_STATE.favoriteSleepImagePath.empty()) {
    return;
  }

  APP_STATE.favoriteSleepImagePath.clear();
  if (!APP_STATE.saveToFile()) {
    LOG_ERR("FileBrowser", "Failed to clear favorite sleep image path");
    return;
  }
  LOG_INF("FileBrowser", "Cleared favorite sleep image");
  requestUpdate();
}

bool FileBrowserActivity::isPinnedSleepFavorite(const std::string& fullPath) const {
  return APP_STATE.favoriteSleepImagePath == fullPath;
}

void FileBrowserActivity::setPreferredSleepFolder(const std::string& fullPath) {
  const std::string normalizedPath = normalizeDirectoryPath(fullPath);
  const std::string nextPath = isSleepFolderPreferencePath(normalizedPath) ? normalizedPath : std::string();
  if (APP_STATE.preferredSleepFolderPath == nextPath) {
    requestUpdate();
    return;
  }

  APP_STATE.preferredSleepFolderPath = nextPath;
  APP_STATE.clearRecentSleepHistory();
  if (!APP_STATE.saveToFile()) {
    LOG_ERR("FileBrowser", "Failed to save preferred sleep folder path: %s", normalizedPath.c_str());
    return;
  }
  LOG_INF("FileBrowser", "Preferred sleep folder set to: %s", nextPath.empty() ? "<default>" : nextPath.c_str());
  requestUpdate();
}

void FileBrowserActivity::clearPreferredSleepFolder() {
  if (APP_STATE.preferredSleepFolderPath.empty()) {
    requestUpdate();
    return;
  }

  APP_STATE.preferredSleepFolderPath.clear();
  APP_STATE.clearRecentSleepHistory();
  if (!APP_STATE.saveToFile()) {
    LOG_ERR("FileBrowser", "Failed to clear preferred sleep folder path");
    return;
  }
  LOG_INF("FileBrowser", "Cleared preferred sleep folder");
  requestUpdate();
}

bool FileBrowserActivity::isPreferredSleepFolder(const std::string& fullPath) const {
  return APP_STATE.preferredSleepFolderPath == normalizeDirectoryPath(fullPath);
}

bool FileBrowserActivity::isSleepFavoriteFolder(const std::string& fullPath) const {
  const std::string normalizedPath = normalizeDirectoryPath(fullPath);
  return isDefaultSleepFolderPath(normalizedPath) || isPreferredSleepFolder(normalizedPath);
}

void FileBrowserActivity::showFileActionMenu(const std::string& entry, bool ignoreInitialConfirmRelease) {
  const std::string fullPath = buildFullPath(basepath, entry);
  std::vector<FileBrowserActionActivity::MenuItem> items =
      BookActions::buildBookActionItems(fullPath, RECENT_BOOKS.containsPath(fullPath));
  if (mode == Mode::Books && hasFileMetadata(entry)) {
    items.insert(items.begin(), {FileBrowserAction::SearchLibrary, StrId::STR_SEARCH});
  }
  if (mode == Mode::Books) {
    items.push_back({FileBrowserAction::SortBooks, StrId::STR_FILE_BROWSER_SORT});
  }
  if (coverGridAvailable) {
    items.push_back({FileBrowserAction::FilterBooks, StrId::STR_FILTER_BOOKS});
  }

  const bool canPinFavorite = isSleepFavoriteFolder(basepath) && isSleepImageFile(entry);
  if (canPinFavorite) {
    items.push_back(
        {isPinnedSleepFavorite(fullPath) ? FileBrowserAction::UnpinFavorite : FileBrowserAction::PinFavorite,
         isPinnedSleepFavorite(fullPath) ? StrId::STR_UNPIN_AS_FAVORITE : StrId::STR_PIN_AS_FAVORITE});
  }

  startActivityForResult(
      std::make_unique<FileBrowserActionActivity>(renderer, mappedInput, getFileName(entry), std::move(items),
                                                  ignoreInitialConfirmRelease),
      [this, fullPath, entry](const ActivityResult& result) {
        longPressConfirmHandled = false;
        if (result.isCancelled) {
          return;
        }

        const auto action = static_cast<FileBrowserAction>(std::get<FileBrowserActionResult>(result.data).action);
        switch (action) {
          case FileBrowserAction::SearchLibrary:
            showLibrarySearch();
            return;
          case FileBrowserAction::SortBooks:
            showBookSortMenu();
            return;
          case FileBrowserAction::MoreInfo: {
            std::string thumbnailPath;
            if (loadedCoverWidth > 0 && loadedCoverHeight > 0) {
              thumbnailPath = coverThumbPathForEntry(entry, loadedCoverWidth, loadedCoverHeight);
            }
            startActivityForResult(std::make_unique<BookInfoActivity>(renderer, mappedInput, fullPath,
                                                                      getFileName(entry), std::move(thumbnailPath)),
                                   [](const ActivityResult&) {});
            return;
          }
          case FileBrowserAction::Delete:
            promptDeleteFile(fullPath, entry);
            return;
          case FileBrowserAction::DeleteCache:
            startActivityForResult(std::make_unique<ConfirmationActivity>(
                                       renderer, mappedInput, BookActions::confirmationHeading(StrId::STR_DELETE_CACHE),
                                       getFileName(entry)),
                                   [this, fullPath](const ActivityResult& confirmation) {
                                     if (!confirmation.isCancelled) {
                                       if (!BookActions::clearBookCache(fullPath)) {
                                         LOG_ERR("FileBrowser", "Failed to clear book cache for: %s", fullPath.c_str());
                                       } else {
                                         BookActions::drawToast(renderer, tr(STR_BOOK_CACHE_DELETED));
                                         delay(1000);
                                       }
                                     }
                                     requestUpdate();
                                   });
            return;
          case FileBrowserAction::DeleteStats:
            startActivityForResult(
                std::make_unique<ConfirmationActivity>(renderer, mappedInput,
                                                       BookActions::confirmationHeading(StrId::STR_DELETE_BOOK_STATS),
                                                       getFileName(entry)),
                [this, fullPath](const ActivityResult& confirmation) {
                  if (!confirmation.isCancelled) {
                    if (!BookActions::deleteBookStats(fullPath)) {
                      LOG_ERR("FileBrowser", "Failed to delete book stats for: %s", fullPath.c_str());
                    } else {
                      BookActions::drawToast(renderer, tr(STR_BOOK_STATS_DELETED));
                      delay(1000);
                    }
                  }
                  requestUpdate();
                });
            return;
          case FileBrowserAction::ResetReaderSettings:
            startActivityForResult(
                std::make_unique<ConfirmationActivity>(
                    renderer, mappedInput, BookActions::confirmationHeading(StrId::STR_RESET_BOOK_READER_SETTINGS),
                    getFileName(entry)),
                [this, fullPath](const ActivityResult& confirmation) {
                  if (!confirmation.isCancelled) {
                    if (!BookActions::resetBookReaderSettings(fullPath)) {
                      LOG_ERR("FileBrowser", "Failed to reset reader settings for: %s", fullPath.c_str());
                    } else {
                      BookActions::drawToast(renderer, tr(STR_BOOK_READER_SETTINGS_RESET));
                      delay(1000);
                    }
                  }
                  requestUpdate();
                });
            return;
          case FileBrowserAction::ToggleCompleted:
            if (BookActions::toggleBookCompleted(fullPath, getFileName(entry), completedFeedbackIsFinished)) {
              pendingCompletedFeedback = true;
              completedFeedbackShowTime = millis();
            }
            loadFiles();
            selectorIndex = entryCount() == 0 ? 0 : std::min(selectorIndex, entryCount() - 1);
            requestUpdate(true);
            return;
          case FileBrowserAction::FilterBooks:
            showBookStatusFilterMenu();
            return;
          case FileBrowserAction::AddBookFavorite:
          case FileBrowserAction::RemoveBookFavorite: {
            bool favorite = false;
            if (BookActions::toggleBookFavorite(fullPath, favorite)) {
              BookActions::drawToast(renderer,
                                     favorite ? tr(STR_ADDED_TO_FAVORITES) : tr(STR_REMOVED_FROM_FAVORITES));
              delay(700);
            }
            requestUpdate();
            return;
          }
          case FileBrowserAction::EpubRenderMode: {
            const uint8_t currentIndex =
                BookActions::epubRenderModeDisplayIndex(EpubReaderActivity::loadBookRenderMode(fullPath));
            startActivityForResult(
                std::make_unique<OptionSelectionActivity>(renderer, mappedInput, "EpubRenderModeSelect",
                                                          StrId::STR_EPUB_RENDER_MODE,
                                                          BookActions::epubRenderModeOptions(), currentIndex),
                [this, fullPath](const ActivityResult& selectionResult) {
                  if (!selectionResult.isCancelled) {
                    const auto* selection = std::get_if<OptionSelectionResult>(&selectionResult.data);
                    if (selection != nullptr &&
                        !EpubReaderActivity::saveBookRenderMode(
                            fullPath, BookActions::epubRenderModeForDisplayIndex(selection->index))) {
                      LOG_ERR("FileBrowser", "Failed to save render mode for: %s", fullPath.c_str());
                    }
                  }
                  requestUpdate();
                });
            return;
          }
          case FileBrowserAction::PinFavorite:
            if (FsHelpers::hasPngExtension(fullPath)) {
              startActivityForResult(
                  std::make_unique<ConfirmationActivity>(renderer, mappedInput, "", tr(STR_PIN_PNG_WARNING)),
                  [this, fullPath](const ActivityResult& confirmation) {
                    if (!confirmation.isCancelled) {
                      pinSleepFavorite(fullPath);
                    }
                  });
            } else {
              pinSleepFavorite(fullPath);
            }
            return;
          case FileBrowserAction::UnpinFavorite:
            unpinSleepFavorite();
            return;
          case FileBrowserAction::SetSleepFolder:
          case FileBrowserAction::ClearSleepFolder:
          case FileBrowserAction::RemoveFromRecents:
            startActivityForResult(std::make_unique<ConfirmationActivity>(
                                       renderer, mappedInput, tr(STR_REMOVE_FROM_RECENTS), getFileName(entry)),
                                   [this, fullPath](const ActivityResult& confirmation) {
                                     if (!confirmation.isCancelled && RECENT_BOOKS.removeByPath(fullPath)) {
                                       BookActions::drawToast(renderer, tr(STR_REMOVED_FROM_RECENTS));
                                       delay(1000);
                                     }
                                     requestUpdate();
                                   });
            return;
          case FileBrowserAction::ViewBookmarks:
          case FileBrowserAction::ViewClippings:
          case FileBrowserAction::DeleteBookmarks:
          case FileBrowserAction::DeleteClippings:
          case FileBrowserAction::MoveFavoriteUp:
          case FileBrowserAction::MoveFavoriteDown:
            return;
        }
      });
}

void FileBrowserActivity::showLibrarySearch() {
  startActivityForResult(std::make_unique<LibrarySearchActivity>(renderer, mappedInput),
                         [this](const ActivityResult&) { requestUpdate(); });
}

void FileBrowserActivity::showBookSortMenu() {
  std::vector<std::string> options = {tr(STR_FILE_BROWSER_SORT_AUTHOR), tr(STR_FILE_BROWSER_SORT_TITLE)};
  startActivityForResult(
      std::make_unique<OptionSelectionActivity>(renderer, mappedInput, "BookSort", StrId::STR_FILE_BROWSER_SORT,
                                                std::move(options), SETTINGS.fileBrowserSort),
      [this](const ActivityResult& result) {
        if (result.isCancelled) return;
        const auto* selection = std::get_if<OptionSelectionResult>(&result.data);
        if (!selection || selection->index >= CrossPointSettings::FILE_BROWSER_SORT_COUNT) return;
        SETTINGS.fileBrowserSort = selection->index;
        SETTINGS.saveToFile();
        selectorIndex = 0;
        loadFiles(true);
        requestUpdate(true);
      });
}

void FileBrowserActivity::navigateIntoDirectory(const std::string& entry) {
  if (entry.empty() || entry.back() != '/') return;

  pendingFolderNavigation = PendingFolderNavigation::Into;
  pendingDirectoryEntry = entry;
  folderTransitionInProgress.store(true, std::memory_order_release);
  folderTransitionSetMs = millis();
  coverNavigationEpoch.fetch_add(1, std::memory_order_acq_rel);
  pendingCoverRefresh.store(static_cast<uint8_t>(CoverRefresh::None), std::memory_order_release);
  processPendingFolderNavigation();
}

void FileBrowserActivity::navigateBack() {
  if (basepath == "/") {
    mappedInput.suppressNextBackRelease();
    cancelCoverRenderForExit();
    onGoHome();
    return;
  }

  pendingFolderNavigation = PendingFolderNavigation::Back;
  pendingDirectoryEntry.clear();
  folderTransitionInProgress.store(true, std::memory_order_release);
  folderTransitionSetMs = millis();
  coverNavigationEpoch.fetch_add(1, std::memory_order_acq_rel);
  pendingCoverRefresh.store(static_cast<uint8_t>(CoverRefresh::None), std::memory_order_release);
  processPendingFolderNavigation();
}

bool FileBrowserActivity::processPendingFolderNavigation() {
  if (pendingFolderNavigation == PendingFolderNavigation::None) return false;

  // Do not make the input task wait behind a stale full-screen e-ink paint.
  // The transition flag tells that render to abort before displayBuffer(); the
  // next loop completes the path change as soon as the render lock is free.
  RenderLock lock(RenderLock::AcquireMode::Try);
  if (!lock.ownsLock()) return true;
  lock.unlock();

  const PendingFolderNavigation navigation = pendingFolderNavigation;
  pendingFolderNavigation = PendingFolderNavigation::None;
  if (navigation == PendingFolderNavigation::Into) {
    const std::string entry = std::move(pendingDirectoryEntry);
    pendingDirectoryEntry.clear();
    if (basepath.back() != '/') basepath += "/";
    basepath += entry.substr(0, entry.length() - 1);
    loadFiles();
    selectorIndex = 0;
    LOG_DBG("FileBrowser", "Entered folder: %s", basepath.c_str());
  } else {
    const std::string oldPath = basepath;
    basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
    if (basepath.empty()) basepath = "/";
    loadFiles();

    const auto pos = oldPath.find_last_of('/');
    const std::string dirName = oldPath.substr(pos + 1) + "/";
    selectorIndex = findEntry(dirName);
    LOG_DBG("FileBrowser", "Navigated back from %s to %s", oldPath.c_str(), basepath.c_str());
  }

  folderTransitionInProgress.store(false, std::memory_order_release);
  requestUpdate(true);
  return true;
}

void FileBrowserActivity::cancelCoverRenderForExit() {
  if (!isCoverGridActive() && !isCoverCarouselActive()) return;
  folderTransitionInProgress.store(true, std::memory_order_release);
  folderTransitionSetMs = millis();
  coverNavigationEpoch.fetch_add(1, std::memory_order_acq_rel);
  pendingCoverRefresh.store(static_cast<uint8_t>(CoverRefresh::None), std::memory_order_release);
  deferredCoverNavigationRefresh = CoverRefresh::None;
  coverBackgroundRefreshPending = false;
}

void FileBrowserActivity::toggleHiddenFiles() {
  const std::string currentEntry =
      (entryCount() > 0 && selectorIndex < entryCount()) ? entryNameAt(selectorIndex) : std::string();
  SETTINGS.showHiddenFiles = SETTINGS.showHiddenFiles ? 0 : 1;
  if (!SETTINGS.saveToFile()) {
    LOG_ERR("FileBrowser", "Failed to save showHiddenFiles=%u", SETTINGS.showHiddenFiles);
  }

  if (!SETTINGS.showHiddenFiles && containsHiddenPathSegment(basepath)) {
    basepath = "/";
  }

  loadFiles(true);
  selectorIndex = currentEntry.empty() ? 0 : findEntry(currentEntry);
  if (entryCount() > 0 && selectorIndex >= entryCount()) {
    selectorIndex = entryCount() - 1;
  }
  requestUpdate();
}

void FileBrowserActivity::releaseNonVisibleCoverBitmapsForHeap() {
  RenderLock lock(*this);
  releaseNonVisibleCoverBitmapsForHeapLocked();
}

void FileBrowserActivity::releaseNonVisibleCoverBitmapsForHeapLocked() {
  // The adjacent pages' bitmaps are a pure cache: drop them so
  // thumbnail generation has heap to parse with; they re-prefetch from exact
  // sharded reads afterwards. Visible cells stay so the shelf keeps its paint.
  const bool carousel = isCoverCarouselActive();
  const int anchor = carousel ? loadedCarouselCenterIndex : loadedCoverPageStart;
  if (anchor == NO_COVER_PAGE_LOADED) return;
  const int visStart = carousel ? anchor - 2 : anchor;
  const int gridItems = std::max(1, loadedCoverItemsPerPage);
  const int visEnd = carousel ? anchor + 2 : anchor + gridItems - 1;
  for (auto& cachedCover : loadedCoverBitmaps) {
    if (!cachedCover.isReady()) continue;
    bool visible = false;
    for (int i = visStart; i <= visEnd; ++i) {
      if (i < 0 || i >= static_cast<int>(entryCount())) continue;
      if (cachedCover.entry == entryNameAt(static_cast<size_t>(i))) {
        visible = true;
        break;
      }
    }
    if (!visible) cachedCover = CachedCoverBitmap{};
  }
  if (carousel) {
    // The last two detail slots are directional look-ahead, never part of the
    // visible five-card composition. They are the first carousel memory to
    // surrender when thumbnail generation needs a larger contiguous block.
    for (size_t i = CAROUSEL_VISIBLE_COUNT; i < loadedCarouselDetailBitmaps.size(); ++i) {
      loadedCarouselDetailBitmaps[i] = CachedCoverBitmap{};
    }
  }
}

void FileBrowserActivity::writePickerHeartbeat(const char* phase, const int detail) const {
  // Overwritten in place at phase boundaries. The exit breadcrumb is only
  // written on a clean exit, so a frozen session used to leave nothing; this
  // file names the last phase the picker entered before it died.
  if (mode != Mode::Books) return;
  FsFile heartbeat;
  if (!Storage.openFileForWrite("FileBrowser", DUET_STATE_ROOT_PATH "/picker_hb.txt", heartbeat)) return;
  char line[144];
  const int len = snprintf(line, sizeof(line), "phase=%s detail=%d ms=%lu entries=%u free=%u max=%u path=%s\n",
                           phase, detail, millis(), static_cast<unsigned>(entryCount()), ESP.getFreeHeap(),
                           ESP.getMaxAllocHeap(), basepath.c_str());
  if (len > 0) heartbeat.write(reinterpret_cast<const uint8_t*>(line), static_cast<size_t>(len));
  heartbeat.close();
}

void FileBrowserActivity::loop() {
  if (processPendingFolderNavigation()) return;

  // cancelCoverRenderForExit() raises the paint-suppression flag with no
  // matching clear; it is meant to die with the activity. If the activity
  // keeps running (the exit never happened), lift the suppression instead of
  // never painting again.
  if (folderTransitionInProgress.load(std::memory_order_acquire) &&
      pendingFolderNavigation == PendingFolderNavigation::None &&
      millis() - folderTransitionSetMs > FOLDER_TRANSITION_SELF_HEAL_MS) {
    folderTransitionInProgress.store(false, std::memory_order_release);
    requestUpdate(true);
  }

  if (pendingCompletedFeedback) {
    const bool timedOut = (millis() - completedFeedbackShowTime) >= COMPLETED_FEEDBACK_MS;
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

  // Cover decoding is intentionally incremental, but a Back click must never
  // wait behind even one SD read. Handle it on press while the shelf is active;
  // the release is swallowed after the parent folder is already visible.
  const bool coverBrowserActive = isCoverGridActive() || isCoverCarouselActive();
  if (coverBrowserActive && !lockLongPressBack &&
      (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
       mappedInput.isPressed(MappedInputManager::Button::Back))) {
    lockLongPressBack = true;
    navigateBack();
    return;
  }

  const std::string selectedEntry =
      (entryCount() > 0 && selectorIndex < entryCount()) ? entryNameAt(selectorIndex) : std::string();

  // Long press BACK/HOME (1s+) toggles hidden files (Books mode only).
  // In firmware-pick mode we keep navigation simple: short Back = up dir / cancel.
  if (mode == Mode::Books && !longPressBackHandled && mappedInput.isPressed(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() >= GO_HOME_MS && !lockLongPressBack) {
    longPressBackHandled = true;
    toggleHiddenFiles();
    return;
  }

  if (lockLongPressBack && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    lockLongPressBack = false;
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pathReserved = renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing;
  const int contentTop = CompactHeader::contentTop(metrics);
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - pathReserved;
  int pageItems = std::max(1, contentHeight / metrics.listRowHeight);
  const bool coverGrid = isCoverGridActive();
  const bool coverCarousel = isCoverCarouselActive();
  if (!coverGrid && !coverCarousel) updateTitleMarquee(selectedEntry);
  const bool compactFileRows =
      !usingIndex && SETTINGS.fileBrowserDisplay == CrossPointSettings::FILE_BROWSER_DISPLAY_2_LINES;
  CoverGridLayout coverLayout;
  CoverCarouselLayout carouselLayout;
  if (coverGrid) {
    coverLayout = calculateCoverGridLayout(renderer, contentTop, contentHeight, metrics.contentSidePadding,
                                           metrics.verticalSpacing);
    pageItems = coverLayout.itemsPerPage;
  } else if (coverCarousel) {
    carouselLayout = calculateCoverCarouselLayout(renderer, contentTop, contentHeight, metrics.contentSidePadding);
    pageItems = 1;
  } else if (compactFileRows) {
    pageItems = std::max(1, contentHeight / MinimalTheme::compactFileBrowserRowHeightFor(renderer));
  }

  if (!selectedEntry.empty()) {
    const bool isDirectory = (selectedEntry.back() == '/');
    if (mode == Mode::Books && !longPressConfirmHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
        mappedInput.getHeldTime() >= GO_HOME_MS) {
      longPressConfirmHandled = true;
      if (isDirectory) {
        showDirectoryActionMenu(selectedEntry, true);
      } else {
        showFileActionMenu(selectedEntry, true);
      }
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (longPressConfirmHandled) {
      longPressConfirmHandled = false;
      return;
    }
    if (lockNextConfirmRelease) {
      lockNextConfirmRelease = false;
      return;
    }
    if (entryCount() == 0) {
      if (mode == Mode::Books && bookStatusFilter != BookStatusFilter::All) {
        showBookStatusFilterMenu();
      }
      return;
    }

    const std::string& entry = selectedEntry;
    const bool isDirectory = (entry.back() == '/');

    // Firmware picker: select file -> return path; navigate into directories normally.
    if (mode == Mode::PickFirmware && !isDirectory) {
      std::string cleanBasePath = basepath;
      if (cleanBasePath.back() != '/') cleanBasePath += "/";
      ActivityResult res{FilePathResult{cleanBasePath + entry}};
      res.isCancelled = false;
      setResult(std::move(res));
      finish();
      return;
    }

    if (mode == Mode::Books && mappedInput.getHeldTime() >= GO_HOME_MS) {
      if (isDirectory) {
        showDirectoryActionMenu(entry);
      } else {
        showFileActionMenu(entry);
      }
      return;
    } else {
      // --- SHORT PRESS ACTION: OPEN/NAVIGATE ---
      if (basepath.back() != '/') basepath += "/";

      if (isDirectory) {
        navigateIntoDirectory(entry);
      } else {
        cancelCoverRenderForExit();
        onSelectBook(basepath + entry);
      }
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (longPressBackHandled) {
      longPressBackHandled = false;
      return;
    }
    // Short press: go up one directory, or go home if at root
    if (mappedInput.getHeldTime() < GO_HOME_MS) {
      if (basepath != "/") {
        navigateBack();
      } else if (mode == Mode::PickFirmware) {
        // Firmware picker at root: cancel back to caller instead of going home.
        ActivityResult res;
        res.isCancelled = true;
        setResult(std::move(res));
        finish();
      } else {
        cancelCoverRenderForExit();
        onGoHome();
      }
    }
    return;
  }

  int listSize = static_cast<int>(entryCount());
  if (coverGrid) {
    bool navigationHandled = false;
    enum class NavDirection { Right, Left, Down, Up };
    const auto handleCoverNavigation = [this, listSize, coverLayout, &navigationHandled](const NavDirection direction) {
      navigationHandled = true;
      const size_t previousIndex = selectorIndex;
      switch (direction) {
        case NavDirection::Right:
          selectorIndex =
              static_cast<size_t>(moveHorizontalInCoverGrid(static_cast<int>(selectorIndex), listSize, true));
          break;
        case NavDirection::Left:
          selectorIndex =
              static_cast<size_t>(moveHorizontalInCoverGrid(static_cast<int>(selectorIndex), listSize, false));
          break;
        case NavDirection::Down:
          selectorIndex = static_cast<size_t>(moveVerticalInCoverGrid(
              static_cast<int>(selectorIndex), listSize, coverLayout.columns, coverLayout.itemsPerPage, true));
          break;
        case NavDirection::Up:
          selectorIndex = static_cast<size_t>(moveVerticalInCoverGrid(
              static_cast<int>(selectorIndex), listSize, coverLayout.columns, coverLayout.itemsPerPage, false));
          break;
      }
      if (selectorIndex == previousIndex) return;
      resetTitleMarquee();
      coverNavigationEpoch.fetch_add(1, std::memory_order_acq_rel);
      const int previousPageStart =
          static_cast<int>(previousIndex / static_cast<size_t>(coverLayout.itemsPerPage)) * coverLayout.itemsPerPage;
      const int nextPageStart =
          static_cast<int>(selectorIndex / static_cast<size_t>(coverLayout.itemsPerPage)) * coverLayout.itemsPerPage;
      deferCoverBackgroundWork();
      const bool canUseSelectionSnapshot =
          previousPageStart == nextPageStart && nextPageStart == loadedCoverPageStart &&
          coverGridSelectionBackgroundIndex == previousIndex &&
          coverGridSelectionBackgroundPageStart == previousPageStart;
      if (canUseSelectionSnapshot) {
        requestFastCoverUpdate(CoverRefresh::GridSelection);
        // The footer is useful, but a second e-ink partial update after every
        // move made the grid feel sticky. Refresh it once the user pauses.
        queueCoverNavigationRefresh(CoverRefresh::GridTitle);
      } else {
        // A small focus-only repaint confirms every move even while the shelf
        // snapshot is unavailable or a new page is still hydrating.
        requestFastCoverUpdate(CoverRefresh::GridFocus);
        suppressNextCoverPagePaint = previousPageStart != nextPageStart;
        queueCoverNavigationRefresh(CoverRefresh::Full);
      }
      LOG_DBG("FileBrowser", "Moved cover selection %u -> %u", static_cast<unsigned>(previousIndex),
              static_cast<unsigned>(selectorIndex));
    };

    buttonNavigator.onPress({MappedInputManager::Button::Right}, [&] { handleCoverNavigation(NavDirection::Right); });
    buttonNavigator.onPress({MappedInputManager::Button::Left}, [&] { handleCoverNavigation(NavDirection::Left); });
    buttonNavigator.onPress({MappedInputManager::Button::Down}, [&] { handleCoverNavigation(NavDirection::Down); });
    buttonNavigator.onPress({MappedInputManager::Button::Up}, [&] { handleCoverNavigation(NavDirection::Up); });
    buttonNavigator.onRelease({MappedInputManager::Button::Right, MappedInputManager::Button::Left,
                               MappedInputManager::Button::Down, MappedInputManager::Button::Up},
                              [] {});

    const bool coverInputHeld = mappedInput.isPressed(MappedInputManager::Button::Left) ||
                                mappedInput.isPressed(MappedInputManager::Button::Right) ||
                                mappedInput.isPressed(MappedInputManager::Button::Up) ||
                                mappedInput.isPressed(MappedInputManager::Button::Down) ||
                                mappedInput.isPressed(MappedInputManager::Button::Confirm) ||
                                mappedInput.isPressed(MappedInputManager::Button::Back);

    // Prepare the selected page only when input is quiet. Even cache setup can
    // touch enough entries to make the picker feel frozen on page changes.
    const int coverPageStart =
        static_cast<int>(selectorIndex / static_cast<size_t>(coverLayout.itemsPerPage)) * coverLayout.itemsPerPage;
    if (!navigationHandled && !coverInputHeld &&
        (loadedCoverPageStart != coverPageStart || loadedCoverWidth != coverLayout.coverWidth ||
         loadedCoverHeight != coverLayout.coverHeight)) {
      loadCoverPage(coverPageStart, coverLayout.itemsPerPage, coverLayout.coverWidth, coverLayout.coverHeight);
    }
    if (!navigationHandled && !coverInputHeld && selectorIndex < entryCount()) {
      updateTitleMarquee(entryNameAt(selectorIndex));
    }

    if (refreshCoverNavigationWhenIdle(coverInputHeld)) return;
    if (refreshCoverBrowserWhenIdle(coverInputHeld)) return;
    // A tap that lands inside a multi-second SD operation is invisible to the
    // polled input path, so background cover work may only run after a real
    // idle window. Any observed input pushes the window out immediately.
    if (navigationHandled || coverInputHeld || mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased()) {
      lastPickerInputMs = millis();
    }
    const unsigned long coverIdleMs = millis() - lastPickerInputMs;
    const bool canRunBackgroundWork =
        !navigationHandled && !coverInputHeld && coverIdleMs >= COVER_WORK_MIN_IDLE_MS && canRunCoverBackgroundWork();
    if (canRunBackgroundWork && pendingCoverPrefetchNext < pendingCoverPrefetchCount &&
        deadlineReached(millis(), pendingCoverPrefetchNextAt)) {
      prefetchNextQueuedCover();
      return;
    }

    if (canRunBackgroundWork && pendingCoverSignalNext < pendingCoverSignalCount &&
        deadlineReached(millis(), pendingCoverSignalNextAt)) {
      const unsigned long signalStart = millis();
      prefetchNextQueuedCoverSignal();
      const unsigned long signalMs = millis() - signalStart;
      pickerSignalCount++;
      pickerSignalTotalMs += signalMs;
      if (signalMs > pickerSignalMaxMs) pickerSignalMaxMs = signalMs;
      return;
    }

    // Generate one missing thumbnail per idle slice on the input task because
    // FsFile handles bypass the storage mutex. The converter polls input
    // cooperatively, preserves the observed edge for the next normal loop, and
    // cancels without leaving the picker stuck behind a long EPUB parse.
    if (pendingCoverGenerationCount > pickerGenQueuedPeak) {
      pickerGenQueuedPeak = static_cast<uint16_t>(pendingCoverGenerationCount);
    }
    if (COVER_BROWSER_GENERATE_THUMBNAILS_DURING_BROWSE && canRunBackgroundWork &&
        coverIdleMs >= COVER_GENERATION_MIN_IDLE_MS &&
        pendingCoverGenerationNext < pendingCoverGenerationCount &&
        deadlineReached(millis(), pendingCoverGenerationNextAt)) {
      bool heapOk = MemoryBudget::hasHeapForCoverThumbGeneration("FileBrowser");
      if (!heapOk) {
        releaseNonVisibleCoverBitmapsForHeap();
        heapOk = MemoryBudget::hasHeapForCoverThumbGeneration("FileBrowser");
      }
      if (heapOk) {
        const unsigned long genStart = millis();
        generateNextQueuedCover();
        const unsigned long genMs = millis() - genStart;
        pickerGenCount++;
        pickerGenTotalMs += genMs;
        if (genMs > pickerGenMaxMs) pickerGenMaxMs = genMs;
        if (genMs > COVER_GENERATION_RUNAWAY_MS) {
          // One build ate this much wall clock: the cache root is still
          // crowded. Stop generating for this visit so input stays usable.
          pendingCoverGenerationNext = pendingCoverGenerationCount;
          writePickerHeartbeat("genslow", static_cast<int>(genMs));
        }
      } else {
        pickerGenSkipHeap++;
        if (millis() - lastGenSkipHeartbeatMs > 30000UL) {
          lastGenSkipHeartbeatMs = millis();
          writePickerHeartbeat("genskip", static_cast<int>(ESP.getMaxAllocHeap()));
        }
        pendingCoverGenerationNextAt = millis() + 30000UL;
      }
    }
    return;
  }

  if (coverCarousel) {
    bool navigationHandled = false;
    const auto moveCarousel = [this, listSize](const bool forward) {
      const size_t previousIndex = selectorIndex;
      selectorIndex =
          static_cast<size_t>(forward ? ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize)
                                      : ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize));
      if (selectorIndex == previousIndex) return;
      resetTitleMarquee();
      coverNavigationEpoch.fetch_add(1, std::memory_order_acq_rel);
      deferCoverBackgroundWork();
      // The title/status strip confirms the new destination immediately; the
      // five-cover composition is painted once after the user pauses.
      requestFastCoverUpdate(CoverRefresh::CarouselTitle);
      queueCoverNavigationRefresh(CoverRefresh::Full);
      LOG_DBG("FileBrowser", "Moved carousel selection %u -> %u", static_cast<unsigned>(previousIndex),
              static_cast<unsigned>(selectorIndex));
    };
    const auto handleCarouselNavigation = [&moveCarousel, &navigationHandled](const bool forward) {
      navigationHandled = true;
      moveCarousel(forward);
    };
    buttonNavigator.onPress({MappedInputManager::Button::Right}, [&] { handleCarouselNavigation(true); });
    buttonNavigator.onPress({MappedInputManager::Button::Down}, [&] { handleCarouselNavigation(true); });
    buttonNavigator.onPress({MappedInputManager::Button::Left}, [&] { handleCarouselNavigation(false); });
    buttonNavigator.onPress({MappedInputManager::Button::Up}, [&] { handleCarouselNavigation(false); });
    buttonNavigator.onRelease({MappedInputManager::Button::Right, MappedInputManager::Button::Left,
                               MappedInputManager::Button::Down, MappedInputManager::Button::Up},
                              [] {});

    const bool carouselInputHeld = mappedInput.isPressed(MappedInputManager::Button::Left) ||
                                   mappedInput.isPressed(MappedInputManager::Button::Right) ||
                                   mappedInput.isPressed(MappedInputManager::Button::Up) ||
                                   mappedInput.isPressed(MappedInputManager::Button::Down) ||
                                   mappedInput.isPressed(MappedInputManager::Button::Confirm) ||
                                   mappedInput.isPressed(MappedInputManager::Button::Back);

    const int carouselSideHeight = std::max(carouselLayout.nearInnerHeight, carouselLayout.nearOuterHeight);
    if (!navigationHandled && !carouselInputHeld &&
        (loadedCarouselCenterIndex != static_cast<int>(selectorIndex) ||
         loadedCoverWidth != carouselLayout.centerWidth || loadedCoverHeight != carouselLayout.centerHeight ||
         loadedCarouselSideWidth != carouselLayout.nearWidth || loadedCarouselSideHeight != carouselSideHeight)) {
      loadCarouselCovers(static_cast<int>(selectorIndex), carouselLayout.centerWidth, carouselLayout.centerHeight,
                         carouselLayout.nearWidth, carouselSideHeight);
    }

    const bool selectedDetailPending =
        pendingCarouselDetailNext == 0 && pendingCarouselDetailNext < pendingCarouselDetailCount;
    const bool selectedSignalPending =
        pendingCarouselSignalNext == 0 && pendingCarouselSignalNext < pendingCarouselSignalCount;

    if (refreshCoverNavigationWhenIdle(carouselInputHeld)) return;
    if (refreshCoverBrowserWhenIdle(carouselInputHeld)) return;
    if (navigationHandled || carouselInputHeld || mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased()) {
      lastPickerInputMs = millis();
    }
    const unsigned long carouselIdleMs = millis() - lastPickerInputMs;
    const bool canRunBackgroundWork = !navigationHandled && !carouselInputHeld &&
                                      carouselIdleMs >= COVER_WORK_MIN_IDLE_MS && canRunCoverBackgroundWork();
    if (canRunBackgroundWork && selectedSignalPending &&
        deadlineReached(millis(), pendingCarouselSignalNextAt)) {
      prefetchNextCarouselSignal();
      return;
    }
    if (canRunBackgroundWork && selectedDetailPending &&
        deadlineReached(millis(), pendingCarouselDetailNextAt)) {
      prefetchNextCarouselDetail();
      return;
    }
    if (canRunBackgroundWork && pendingCarouselDetailNext < pendingCarouselDetailCount &&
        deadlineReached(millis(), pendingCarouselDetailNextAt)) {
      prefetchNextCarouselDetail();
      return;
    }
    if (canRunBackgroundWork && pendingCarouselSignalNext < pendingCarouselSignalCount &&
        deadlineReached(millis(), pendingCarouselSignalNextAt)) {
      prefetchNextCarouselSignal();
      return;
    }

    if (!navigationHandled && !carouselInputHeld && selectorIndex < entryCount()) {
      updateTitleMarquee(entryNameAt(selectorIndex));
    }

    // Generation runs here on the input task: all picker SD work stays on one
    // task (FsFile handles bypass the Storage mutex, so cross-task file work
    // corrupts SdFat state — proven by the repair18-5 crash pair). One book
    // per idle slice; input is deaf only while that single book builds. If
    // heap is short, the off-page bitmap cache is dropped first; a skip is
    // recorded on the card so "no covers" is diagnosable from breadcrumbs.
    if (pendingCoverGenerationCount > pickerGenQueuedPeak) {
      pickerGenQueuedPeak = static_cast<uint16_t>(pendingCoverGenerationCount);
    }
    if (COVER_BROWSER_GENERATE_THUMBNAILS_DURING_BROWSE && canRunBackgroundWork &&
        carouselIdleMs >= COVER_GENERATION_MIN_IDLE_MS &&
        pendingCoverGenerationNext < pendingCoverGenerationCount &&
        deadlineReached(millis(), pendingCoverGenerationNextAt)) {
      bool heapOk = MemoryBudget::hasHeapForCoverThumbGeneration("FileBrowser");
      if (!heapOk) {
        releaseNonVisibleCoverBitmapsForHeap();
        heapOk = MemoryBudget::hasHeapForCoverThumbGeneration("FileBrowser");
      }
      if (heapOk) {
        const unsigned long genStart = millis();
        generateNextQueuedCover();
        const unsigned long genMs = millis() - genStart;
        pickerGenCount++;
        pickerGenTotalMs += genMs;
        if (genMs > pickerGenMaxMs) pickerGenMaxMs = genMs;
        if (genMs > COVER_GENERATION_RUNAWAY_MS) {
          // One build ate this much wall clock: the cache root is still
          // crowded. Stop generating for this visit so input stays usable.
          pendingCoverGenerationNext = pendingCoverGenerationCount;
          writePickerHeartbeat("genslow", static_cast<int>(genMs));
        }
      } else {
        pickerGenSkipHeap++;
        if (millis() - lastGenSkipHeartbeatMs > 30000UL) {
          lastGenSkipHeartbeatMs = millis();
          writePickerHeartbeat("genskip", static_cast<int>(ESP.getMaxAllocHeap()));
        }
        pendingCoverGenerationNextAt = millis() + 30000UL;
      }
    }
    return;
  }

  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    resetTitleMarquee();
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    resetTitleMarquee();
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    resetTitleMarquee();
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    resetTitleMarquee();
    requestUpdate();
  });
}

namespace {

std::string getFileName(std::string filename) {
  if (filename.back() == '/') {
    filename.pop_back();
    if (!UITheme::getInstance().getTheme().showsFileIcons()) {
      return "[" + filename + "]";
    }
    return filename;
  }
  const auto pos = filename.rfind('.');
  return filename.substr(0, pos);
}

std::string getCoverTitle(const std::string& entry) {
  std::string title = getFileName(entry);
  if (!entry.empty() && entry.back() != '/') {
    const size_t authorSeparator = title.find(" - ");
    if (authorSeparator != std::string::npos && authorSeparator + 3 < title.size()) {
      title.erase(0, authorSeparator + 3);
    }
  }
  return title;
}

std::string getCoverAuthor(const std::string& entry) {
  if (entry.empty() || entry.back() == '/') return "";
  const std::string filename = getFileName(entry);
  const size_t authorSeparator = filename.find(" - ");
  if (authorSeparator == std::string::npos) return "";
  return filename.substr(0, authorSeparator);
}

size_t advanceUtf8Offset(const std::string& text, size_t offset, size_t steps) {
  while (steps-- > 0 && offset < text.size()) offset = nextUtf8Offset(text, offset);
  return offset;
}

void trimSpaces(std::string& text) {
  while (!text.empty() && text.front() == ' ') text.erase(text.begin());
  while (!text.empty() && text.back() == ' ') text.pop_back();
}

std::vector<std::string> marqueeTitleLines(const GfxRenderer& renderer, const int fontId, const std::string& title,
                                           const int maxWidth, const int maxLines,
                                           const EpdFontFamily::Style style, const size_t marqueeSteps) {
  const auto initialLines = renderer.wrappedText(fontId, title.c_str(), maxWidth, maxLines, style);
  if (marqueeSteps == 0 || maxLines != 2 || initialLines.size() < 2 || initialLines.front().empty() ||
      title.compare(0, initialLines.front().size(), initialLines.front()) != 0) {
    return initialLines;
  }

  // Keep the original wrap boundary moving with the leading edge. Both rows
  // therefore advance one UTF-8 character per marquee tick instead of leaving
  // the truncated second row visually frozen.
  size_t secondLineStart = initialLines.front().size();
  while (secondLineStart < title.size() && title[secondLineStart] == ' ') ++secondLineStart;
  const size_t firstStart = advanceUtf8Offset(title, 0, marqueeSteps);
  const size_t secondStart = advanceUtf8Offset(title, secondLineStart, marqueeSteps);
  if (firstStart >= title.size()) return initialLines;

  std::string firstLine = title.substr(firstStart, secondStart > firstStart ? secondStart - firstStart : 0);
  std::string secondLine = secondStart < title.size() ? title.substr(secondStart) : std::string();
  trimSpaces(firstLine);
  trimSpaces(secondLine);

  std::vector<std::string> lines;
  if (!firstLine.empty()) lines.push_back(renderer.truncatedText(fontId, firstLine.c_str(), maxWidth, style));
  if (!secondLine.empty()) lines.push_back(renderer.truncatedText(fontId, secondLine.c_str(), maxWidth, style));
  return lines.empty() ? initialLines : lines;
}

void drawCoverGridFooter(const GfxRenderer& renderer, const std::string& entry, const CoverGridLayout& layout,
                         const char* statusLabel, const int progressPercent, const size_t marqueeSteps) {
  renderer.fillRect(layout.footerX, layout.footerY, layout.footerWidth, layout.footerHeight, false);

  std::string displayTitle = getCoverTitle(entry);
  const size_t titleOffset = advanceUtf8Offset(displayTitle, 0, marqueeSteps);
  if (titleOffset > 0 && titleOffset < displayTitle.size()) displayTitle.erase(0, titleOffset);
  const std::string title = renderer.truncatedText(UI_10_FONT_ID, displayTitle.c_str(), layout.footerWidth,
                                                    EpdFontFamily::BOLD);
  const int titleWidth = renderer.getTextWidth(UI_10_FONT_ID, title.c_str(), EpdFontFamily::BOLD);
  const int titleY = layout.footerY + 1;
  renderer.drawText(UI_10_FONT_ID, layout.footerX + (layout.footerWidth - titleWidth) / 2, titleY, title.c_str(), true,
                    EpdFontFamily::BOLD);

  const int authorY = titleY + renderer.getLineHeight(UI_10_FONT_ID) + COVER_GRID_FOOTER_LINE_GAP;
  const int statusWidth = statusLabel && statusLabel[0]
                              ? renderer.getTextWidth(SMALL_FONT_ID, statusLabel, EpdFontFamily::REGULAR)
                              : 0;
  char progressLabel[16] = {};
  if (progressPercent >= 0) {
    snprintf(progressLabel, sizeof(progressLabel), "%d%%", std::clamp(progressPercent, 0, 100));
  }
  const int progressWidth = progressLabel[0]
                                ? renderer.getTextWidth(SMALL_FONT_ID, progressLabel, EpdFontFamily::REGULAR)
                                : 0;
  constexpr int sideLabelGap = 8;
  const int sideBudget = std::max(statusWidth, progressWidth);
  const int authorBudget = std::max(0, layout.footerWidth - sideBudget * 2 - sideLabelGap * 2);
  const std::string author = renderer.truncatedText(SMALL_FONT_ID, getCoverAuthor(entry).c_str(), authorBudget,
                                                     EpdFontFamily::REGULAR);
  if (statusWidth > 0) {
    renderer.drawText(SMALL_FONT_ID, layout.footerX, authorY, statusLabel, true, EpdFontFamily::REGULAR);
  }
  if (!author.empty()) {
    const int authorWidth = renderer.getTextWidth(SMALL_FONT_ID, author.c_str(), EpdFontFamily::REGULAR);
    renderer.drawText(SMALL_FONT_ID, layout.footerX + (layout.footerWidth - authorWidth) / 2, authorY, author.c_str(),
                      true, EpdFontFamily::REGULAR);
  }
  if (progressWidth > 0) {
    renderer.drawText(SMALL_FONT_ID, layout.footerX + layout.footerWidth - progressWidth, authorY,
                      progressLabel, true, EpdFontFamily::REGULAR);
  }
}

std::string getFileExtension(const std::string& filename) {
  if (filename.back() == '/') {
    return "";
  }
  const auto pos = filename.rfind('.');
  if (pos == std::string::npos) {
    return "";
  }
  return filename.substr(pos);
}

}  // namespace

std::string FileBrowserActivity::rowValueForEntry(const std::string& entry) const {
  const std::string extension = SETTINGS.hideFileExtension != 0 ? std::string() : getFileExtension(entry);
  const std::string fullPath = buildFullPath(basepath, entry);
  if (entry.back() == '/' && isPreferredSleepFolder(fullPath)) {
    return "*";
  }
  if (isPinnedSleepFavorite(fullPath)) {
    return extension.empty() ? std::string("*") : "* " + extension;
  }
  return extension;
}

void FileBrowserActivity::resetCoverPage() {
  invalidateCoverGridSelectionBackground();
  loadedCoverPageStart = NO_COVER_PAGE_LOADED;
  loadedCoverItemsPerPage = 0;
  loadedCoverWidth = 0;
  loadedCoverHeight = 0;
  loadedCarouselCenterIndex = NO_COVER_PAGE_LOADED;
  loadedCarouselSideWidth = 0;
  loadedCarouselSideHeight = 0;
  for (auto& detail : loadedCarouselDetailBitmaps) detail = CachedCoverBitmap{};
  pendingCarouselDetailCount = 0;
  pendingCarouselDetailNext = 0;
  pendingVisibleCarouselDetailCount = 0;
  pendingCarouselDetailNextAt = 0UL;
  pendingCarouselSignalCount = 0;
  pendingCarouselSignalNext = 0;
  pendingCarouselSignalNextAt = 0UL;
  pendingCoverSignalCount = 0;
  pendingCoverSignalNext = 0;
  pendingCoverSignalNextAt = 0UL;
  pendingCoverPrefetchCount = 0;
  pendingCoverPrefetchNext = 0;
  pendingVisibleCoverPrefetchCount = 0;
  pendingCoverPrefetchNextAt = 0UL;
  pendingCoverGenerationCount = 0;
  pendingCoverGenerationNext = 0;
  pendingCoverGenerationNextAt = 0UL;
  coverBackgroundWorkNotBefore = 0UL;
  coverBackgroundRefreshNotBefore = 0UL;
  coverBackgroundRefreshPending = false;
  deferredCoverNavigationRefresh = CoverRefresh::None;
  deferredCoverNavigationRefreshNotBefore = 0UL;
  suppressNextCoverPagePaint = false;
  paintedCoverGridSelection.store(static_cast<size_t>(-1), std::memory_order_release);
  paintedCoverGridPageStart.store(NO_COVER_PAGE_LOADED, std::memory_order_release);
  paintedCarouselCenterIndex.store(NO_COVER_PAGE_LOADED, std::memory_order_release);
  for (auto& signal : loadedCoverSignals) signal = CoverBookSignal{};
  for (auto& cover : loadedCoverBitmaps) cover = CachedCoverBitmap{};
}

void FileBrowserActivity::deferCoverBackgroundWork() {
  const unsigned long resumeAt = millis() + COVER_BROWSER_IDLE_WORK_DELAY_MS;
  coverBackgroundWorkNotBefore = resumeAt;
  if (coverBackgroundRefreshPending) coverBackgroundRefreshNotBefore = resumeAt;
}

bool FileBrowserActivity::canRunCoverBackgroundWork() const {
  if (!deadlineReached(millis(), coverBackgroundWorkNotBefore)) return false;
  // Never let SD reads race the first interactive frame. On physical X3
  // hardware the loader can otherwise consume the entire idle queue before
  // the e-ink task paints the cursor, making the browser appear frozen.
  if (isCoverGridActive()) {
    return loadedCoverPageStart != NO_COVER_PAGE_LOADED &&
           paintedCoverGridPageStart.load(std::memory_order_acquire) == loadedCoverPageStart;
  }
  if (isCoverCarouselActive()) {
    return loadedCarouselCenterIndex != NO_COVER_PAGE_LOADED &&
           paintedCarouselCenterIndex.load(std::memory_order_acquire) == loadedCarouselCenterIndex;
  }
  return true;
}

void FileBrowserActivity::queueCoverNavigationRefresh(const CoverRefresh refresh) {
  deferredCoverNavigationRefresh = refresh;
  deferredCoverNavigationRefreshNotBefore = millis() + coverBrowserNavigationSettleDelayMs();
}

bool FileBrowserActivity::refreshCoverNavigationWhenIdle(const bool inputHeld) {
  if (inputHeld || deferredCoverNavigationRefresh == CoverRefresh::None ||
      !deadlineReached(millis(), deferredCoverNavigationRefreshNotBefore)) {
    return false;
  }

  const CoverRefresh refresh = deferredCoverNavigationRefresh;
  deferredCoverNavigationRefresh = CoverRefresh::None;
  deferredCoverNavigationRefreshNotBefore = 0UL;
  if (refresh == CoverRefresh::Full) {
    requestUpdate();
  } else {
    requestFastCoverUpdate(refresh);
  }
  return true;
}

void FileBrowserActivity::queueCoverBackgroundRefresh() {
  coverBackgroundRefreshPending = true;
  coverBackgroundRefreshNotBefore = millis() + COVER_BROWSER_IDLE_REFRESH_DELAY_MS;
}

bool FileBrowserActivity::isFocusCoverRefresh(const CoverRefresh refresh) {
  return refresh == CoverRefresh::GridSelection || refresh == CoverRefresh::GridFocus ||
         refresh == CoverRefresh::GridTitle || refresh == CoverRefresh::CarouselTitle;
}

bool FileBrowserActivity::claimCoverBackgroundRefresh() {
  if (!coverBackgroundRefreshPending) return false;

  const auto pending = static_cast<CoverRefresh>(pendingCoverRefresh.load(std::memory_order_acquire));
  if (isFocusCoverRefresh(pending)) {
    // The cursor/title frame owns the next paint. Keep the hydrated-cover
    // repaint queued instead of clearing it and silently losing the artwork.
    coverBackgroundRefreshNotBefore = millis() + COVER_BROWSER_IDLE_REFRESH_DELAY_MS;
    return false;
  }

  coverBackgroundRefreshPending = false;
  return true;
}

bool FileBrowserActivity::refreshCoverBrowserWhenIdle(const bool inputHeld) {
  if (inputHeld || !coverBackgroundRefreshPending ||
      !deadlineReached(millis(), coverBackgroundRefreshNotBefore) ||
      !canRunCoverBackgroundWork() || deferredCoverNavigationRefresh != CoverRefresh::None) {
    return false;
  }
  if (!claimCoverBackgroundRefresh()) return false;
  // Keep the old clean snapshot valid until the replacement frame actually
  // paints. Invalidating it here creates a dead interval where the logical
  // selection moves but the focus ring cannot follow.
  requestUpdate();
  return true;
}

#ifdef SIMULATOR
bool FileBrowserActivity::simulatorVerifyCoverBackgroundRefreshCoalescing() {
  const bool savedRefreshPending = coverBackgroundRefreshPending;
  const unsigned long savedRefreshNotBefore = coverBackgroundRefreshNotBefore;
  const uint8_t savedCoverRefresh = pendingCoverRefresh.load(std::memory_order_acquire);

  coverBackgroundRefreshPending = true;
  coverBackgroundRefreshNotBefore = 0UL;
  pendingCoverRefresh.store(static_cast<uint8_t>(CoverRefresh::GridFocus), std::memory_order_release);
  const bool preservedBehindFocus = !claimCoverBackgroundRefresh() && coverBackgroundRefreshPending;

  pendingCoverRefresh.store(static_cast<uint8_t>(CoverRefresh::None), std::memory_order_release);
  const bool claimedAfterFocus = claimCoverBackgroundRefresh() && !coverBackgroundRefreshPending;

  coverBackgroundRefreshPending = savedRefreshPending;
  coverBackgroundRefreshNotBefore = savedRefreshNotBefore;
  pendingCoverRefresh.store(savedCoverRefresh, std::memory_order_release);
  return preservedBehindFocus && claimedAfterFocus;
}
#endif

void FileBrowserActivity::invalidateCoverGridSelectionBackground() {
  coverGridSelectionBackgroundSize = 0;
  coverGridSelectionBackgroundIndex = static_cast<size_t>(-1);
  coverGridSelectionBackgroundPageStart = NO_COVER_PAGE_LOADED;
  coverGridSelectionBackgroundX = 0;
  coverGridSelectionBackgroundY = 0;
  coverGridSelectionBackgroundWidth = 0;
  coverGridSelectionBackgroundHeight = 0;
  coverGridFooterBackgroundSize = 0;
  coverGridFooterBackgroundPageStart = NO_COVER_PAGE_LOADED;
  coverGridFooterBackgroundX = 0;
  coverGridFooterBackgroundY = 0;
  coverGridFooterBackgroundWidth = 0;
  coverGridFooterBackgroundHeight = 0;
}

bool FileBrowserActivity::captureCoverGridSelectionBackground(const int x, const int y, const int width,
                                                              const int height, const size_t index,
                                                              const int pageStart) {
  const size_t required = renderer.getRegionByteSize(x, y, width, height);
  if (required == 0) {
    invalidateCoverGridSelectionBackground();
    return false;
  }

  if (!coverGridSelectionBackground || coverGridSelectionBackgroundCapacity < required) {
    auto replacement = makeUniqueNoThrow<uint8_t[]>(required);
    if (!replacement) {
      LOG_ERR("FileBrowser", "Could not allocate %u-byte cover selection snapshot",
              static_cast<unsigned>(required));
      invalidateCoverGridSelectionBackground();
      return false;
    }
    coverGridSelectionBackground = std::move(replacement);
    coverGridSelectionBackgroundCapacity = required;
  }

  if (!renderer.copyRegionToBuffer(x, y, width, height, coverGridSelectionBackground.get(),
                                   coverGridSelectionBackgroundCapacity)) {
    invalidateCoverGridSelectionBackground();
    return false;
  }

  coverGridSelectionBackgroundSize = required;
  coverGridSelectionBackgroundIndex = index;
  coverGridSelectionBackgroundPageStart = pageStart;
  coverGridSelectionBackgroundX = x;
  coverGridSelectionBackgroundY = y;
  coverGridSelectionBackgroundWidth = width;
  coverGridSelectionBackgroundHeight = height;
  return true;
}

bool FileBrowserActivity::restoreCoverGridSelectionBackground(const size_t expectedIndex,
                                                              const int expectedPageStart) {
  return coverGridSelectionBackground && coverGridSelectionBackgroundSize > 0 &&
         coverGridSelectionBackgroundIndex == expectedIndex &&
         coverGridSelectionBackgroundPageStart == expectedPageStart &&
         renderer.copyBufferToRegion(coverGridSelectionBackgroundX, coverGridSelectionBackgroundY,
                                     coverGridSelectionBackgroundWidth, coverGridSelectionBackgroundHeight,
                                     coverGridSelectionBackground.get(), coverGridSelectionBackgroundSize);
}

bool FileBrowserActivity::captureCoverGridFooterBackground(const int x, const int y, const int width,
                                                           const int height, const int pageStart) {
  const size_t required = renderer.getRegionByteSize(x, y, width, height);
  if (required == 0) {
    coverGridFooterBackgroundSize = 0;
    return false;
  }
  if (!coverGridFooterBackground || coverGridFooterBackgroundCapacity < required) {
    auto replacement = makeUniqueNoThrow<uint8_t[]>(required);
    if (!replacement) {
      LOG_ERR("FileBrowser", "Could not allocate %u-byte cover footer snapshot",
              static_cast<unsigned>(required));
      coverGridFooterBackgroundSize = 0;
      return false;
    }
    coverGridFooterBackground = std::move(replacement);
    coverGridFooterBackgroundCapacity = required;
  }
  if (!renderer.copyRegionToBuffer(x, y, width, height, coverGridFooterBackground.get(),
                                   coverGridFooterBackgroundCapacity)) {
    coverGridFooterBackgroundSize = 0;
    return false;
  }
  coverGridFooterBackgroundSize = required;
  coverGridFooterBackgroundPageStart = pageStart;
  coverGridFooterBackgroundX = x;
  coverGridFooterBackgroundY = y;
  coverGridFooterBackgroundWidth = width;
  coverGridFooterBackgroundHeight = height;
  return true;
}

bool FileBrowserActivity::restoreCoverGridFooterBackground(const int expectedPageStart) {
  return coverGridFooterBackground && coverGridFooterBackgroundSize > 0 &&
         coverGridFooterBackgroundPageStart == expectedPageStart &&
         renderer.copyBufferToRegion(coverGridFooterBackgroundX, coverGridFooterBackgroundY,
                                     coverGridFooterBackgroundWidth, coverGridFooterBackgroundHeight,
                                     coverGridFooterBackground.get(), coverGridFooterBackgroundSize);
}

bool FileBrowserActivity::moveCoverGridSelectionFromSnapshot() {
  if (!isCoverGridActive() || entryCount() == 0) return false;
  const size_t previousIndex = coverGridSelectionBackgroundIndex;
  if (previousIndex == static_cast<size_t>(-1)) return false;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pathReserved = renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing;
  const int contentTop = CompactHeader::contentTop(metrics);
  const int contentHeight = renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight -
                            metrics.verticalSpacing - pathReserved;
  const CoverGridLayout layout = calculateCoverGridLayout(renderer, contentTop, contentHeight,
                                                          metrics.contentSidePadding, metrics.verticalSpacing);
  const int previousPageStart =
      static_cast<int>(previousIndex / static_cast<size_t>(layout.itemsPerPage)) * layout.itemsPerPage;
  const int pageStart =
      static_cast<int>(selectorIndex / static_cast<size_t>(layout.itemsPerPage)) * layout.itemsPerPage;
  const int pageItemCount = std::min(layout.itemsPerPage, static_cast<int>(entryCount()) - pageStart);
  if (pageStart != previousPageStart || pageStart != loadedCoverPageStart ||
      coverGridSelectionBackgroundIndex != previousIndex ||
      coverGridSelectionBackgroundPageStart != previousPageStart) {
    invalidateCoverGridSelectionBackground();
    return false;
  }

  const int previousX = coverGridSelectionBackgroundX;
  const int previousY = coverGridSelectionBackgroundY;
  const int previousWidth = coverGridSelectionBackgroundWidth;
  const int previousHeight = coverGridSelectionBackgroundHeight;
  if (!restoreCoverGridSelectionBackground(previousIndex, previousPageStart)) return false;

  CoverGridCellGeometry geometry;
  if (!calculateCoverGridCellGeometry(layout, pageStart, selectorIndex, pageItemCount, geometry) ||
      !captureCoverGridSelectionBackground(geometry.snapshotX, geometry.snapshotY, geometry.snapshotWidth,
                                           geometry.snapshotHeight, selectorIndex, pageStart)) {
    return false;
  }

  drawCoverGridSelection(renderer, layout, geometry);
  const int windowX = std::min(previousX, geometry.snapshotX);
  const int windowY = std::min(previousY, geometry.snapshotY);
  const int windowRight = std::max(previousX + previousWidth, geometry.snapshotX + geometry.snapshotWidth);
  const int windowBottom = std::max(previousY + previousHeight, geometry.snapshotY + geometry.snapshotHeight);
  if (folderTransitionInProgress.load(std::memory_order_acquire)) return true;
  const size_t windowBytes =
      renderer.getRegionByteSize(windowX, windowY, windowRight - windowX, windowBottom - windowY);
  constexpr size_t WINDOW_HEAP_MARGIN = 4U * 1024U;
  const auto heap = MemoryBudget::snapshot();
  if (windowBytes > 0 &&
      (heap.freeHeap < windowBytes + WINDOW_HEAP_MARGIN || heap.maxAllocHeap < windowBytes + WINDOW_HEAP_MARGIN)) {
    LOG_DBG("FileBrowser",
            "Releasing look-ahead covers before %u-byte grid cursor window (free=%u maxAlloc=%u)",
            static_cast<unsigned>(windowBytes), heap.freeHeap, heap.maxAllocHeap);
    releaseNonVisibleCoverBitmapsForHeapLocked();
  }
  const auto displayHeap = MemoryBudget::snapshot();
  if (windowBytes > 0 &&
      (displayHeap.freeHeap < windowBytes + WINDOW_HEAP_MARGIN ||
       displayHeap.maxAllocHeap < windowBytes + WINDOW_HEAP_MARGIN)) {
    LOG_INF("FileBrowser",
            "Grid cursor window still constrained after cache release (%u bytes, free=%u maxAlloc=%u); "
            "using full fast refresh",
            static_cast<unsigned>(windowBytes), displayHeap.freeHeap, displayHeap.maxAllocHeap);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  } else {
    renderer.displayWindow(windowX, windowY, windowRight - windowX, windowBottom - windowY, false, false);
  }
  paintedCoverGridSelection.store(selectorIndex, std::memory_order_release);
  paintedCoverGridPageStart.store(pageStart, std::memory_order_release);
  LOG_DBG("FileBrowser", "Moved cover selection %u -> %u without reloading thumbnails",
          static_cast<unsigned>(previousIndex), static_cast<unsigned>(selectorIndex));
  return true;
}

bool FileBrowserActivity::refreshCoverGridFocus() {
  if (!isCoverGridActive() || entryCount() == 0 || selectorIndex >= entryCount()) return false;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pathReserved = renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing;
  const int contentTop = CompactHeader::contentTop(metrics);
  const int contentHeight = renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight -
                            metrics.verticalSpacing - pathReserved;
  const CoverGridLayout layout = calculateCoverGridLayout(renderer, contentTop, contentHeight,
                                                          metrics.contentSidePadding, metrics.verticalSpacing);
  const int pageStart =
      static_cast<int>(selectorIndex / static_cast<size_t>(layout.itemsPerPage)) * layout.itemsPerPage;
  const size_t paintedIndex = paintedCoverGridSelection.load(std::memory_order_acquire);
  const int paintedPageStart = paintedCoverGridPageStart.load(std::memory_order_acquire);

  // On a page crossing, the old covers remain visible until the user pauses.
  // A title-strip repaint provides immediate acknowledgement without drawing a
  // new ring around an old-page book.
  if (paintedIndex == static_cast<size_t>(-1) || paintedPageStart != pageStart) {
    const std::string entry = entryNameAt(selectorIndex);
    const CoverBookSignal* signal = coverSignalForEntry(entry);
    const char* status = tr(STR_FILTER_UNREAD);
    int progressPercent = -1;
    if (signal) {
      progressPercent = signal->progressPercent;
      if (signal->status == BookStatus::Finished) {
        status = tr(STR_FILTER_FINISHED);
        progressPercent = 100;
      } else if (signal->status == BookStatus::Reading) {
        status = tr(STR_FILTER_READING);
      }
    }
    drawCoverGridFooter(renderer, entry, layout, status, progressPercent, 0);
    if (folderTransitionInProgress.load(std::memory_order_acquire)) return true;
    renderer.displayWindow(layout.footerX, layout.footerY, layout.footerWidth, layout.footerHeight, false, false);
    return true;
  }

  const int pageItemCount = std::min(layout.itemsPerPage, static_cast<int>(entryCount()) - pageStart);
  CoverGridCellGeometry previousGeometry;
  CoverGridCellGeometry nextGeometry;
  if (!calculateCoverGridCellGeometry(layout, pageStart, paintedIndex, pageItemCount, previousGeometry) ||
      !calculateCoverGridCellGeometry(layout, pageStart, selectorIndex, pageItemCount, nextGeometry)) {
    return false;
  }

  eraseCoverGridSelection(renderer, layout, previousGeometry);
  drawCoverGridSelection(renderer, layout, nextGeometry);
  const int windowX = std::min(previousGeometry.snapshotX, nextGeometry.snapshotX);
  const int windowY = std::min(previousGeometry.snapshotY, nextGeometry.snapshotY);
  const int windowRight = std::max(previousGeometry.snapshotX + previousGeometry.snapshotWidth,
                                   nextGeometry.snapshotX + nextGeometry.snapshotWidth);
  const int windowBottom = std::max(previousGeometry.snapshotY + previousGeometry.snapshotHeight,
                                    nextGeometry.snapshotY + nextGeometry.snapshotHeight);
  if (folderTransitionInProgress.load(std::memory_order_acquire)) return true;
  renderer.displayWindow(windowX, windowY, windowRight - windowX, windowBottom - windowY, false, false);
  paintedCoverGridSelection.store(selectorIndex, std::memory_order_release);
  paintedCoverGridPageStart.store(pageStart, std::memory_order_release);
  return true;
}

bool FileBrowserActivity::refreshCoverGridSelectionFromSnapshot() {
  if (!isCoverGridActive() || entryCount() == 0) return false;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pathReserved = renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing;
  const int contentTop = CompactHeader::contentTop(metrics);
  const int contentHeight = renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight -
                            metrics.verticalSpacing - pathReserved;
  const CoverGridLayout layout = calculateCoverGridLayout(renderer, contentTop, contentHeight,
                                                          metrics.contentSidePadding, metrics.verticalSpacing);
  const int pageStart =
      static_cast<int>(selectorIndex / static_cast<size_t>(layout.itemsPerPage)) * layout.itemsPerPage;
  if (pageStart != loadedCoverPageStart || coverGridSelectionBackgroundIndex != selectorIndex ||
      coverGridSelectionBackgroundPageStart != pageStart) {
    invalidateCoverGridSelectionBackground();
    return false;
  }
  if (!restoreCoverGridFooterBackground(pageStart)) return false;

  const std::string entry = entryNameAt(selectorIndex);
  const CoverBookSignal* signal = coverSignalForEntry(entry);
  const char* status = tr(STR_FILTER_UNREAD);
  int progressPercent = -1;
  if (signal) {
    progressPercent = signal->progressPercent;
    if (signal->status == BookStatus::Finished) {
      status = tr(STR_FILTER_FINISHED);
      progressPercent = 100;
    } else if (signal->status == BookStatus::Reading) {
      status = tr(STR_FILTER_READING);
    }
  }
  drawCoverGridFooter(renderer, entry, layout, status, progressPercent,
                      titleMarqueeStep.load(std::memory_order_relaxed));
  if (folderTransitionInProgress.load(std::memory_order_acquire)) return true;
  renderer.displayWindow(coverGridFooterBackgroundX, coverGridFooterBackgroundY, coverGridFooterBackgroundWidth,
                         coverGridFooterBackgroundHeight, false, false);
  return true;
}

bool FileBrowserActivity::refreshCoverCarouselTitle() {
  if (!isCoverCarouselActive() || entryCount() == 0 || selectorIndex >= entryCount()) return false;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pathReserved = renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing;
  const int contentTop = CompactHeader::contentTop(metrics);
  const int contentHeight = renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight -
                            metrics.verticalSpacing - pathReserved;
  const CoverCarouselLayout layout =
      calculateCoverCarouselLayout(renderer, contentTop, contentHeight, metrics.contentSidePadding);

  const int pageWidth = renderer.getScreenWidth();
  const std::string entry = entryNameAt(selectorIndex);
  std::string title = getCoverTitle(entry);
  const size_t steps = titleMarqueeStep.load(std::memory_order_relaxed);

  const int titleHeight = layout.titleLineHeight * COVER_CAROUSEL_TITLE_LINES;
  const int progressBottom = layout.progressY + 7;
  const int infoBottom = std::max(progressBottom, layout.counterY + renderer.getLineHeight(SMALL_FONT_ID));
  renderer.fillRect(0, layout.titleY, pageWidth, infoBottom - layout.titleY + 1, false);
  const auto titleLines = marqueeTitleLines(renderer, UI_12_FONT_ID, title, layout.textWidth,
                                            COVER_CAROUSEL_TITLE_LINES, EpdFontFamily::BOLD, steps);
  int titleY = layout.titleY;
  for (const auto& line : titleLines) {
    const int lineWidth = renderer.getTextWidth(UI_12_FONT_ID, line.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, (pageWidth - lineWidth) / 2, titleY, line.c_str(), true, EpdFontFamily::BOLD);
    titleY += layout.titleLineHeight;
  }

  const std::string coverAuthor = getCoverAuthor(entry);
  const std::string author =
      renderer.truncatedText(UI_10_FONT_ID, coverAuthor.c_str(), layout.textWidth, EpdFontFamily::REGULAR);
  if (!author.empty()) {
    const int authorWidth = renderer.getTextWidth(UI_10_FONT_ID, author.c_str(), EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, (pageWidth - authorWidth) / 2, layout.authorY, author.c_str(), true,
                      EpdFontFamily::REGULAR);
  }

  const CoverBookSignal* signal = coverSignalForEntry(entry);
  const char* status = tr(STR_FILTER_UNREAD);
  int progressPercent = -1;
  if (signal) {
    progressPercent = signal->progressPercent;
    if (signal->status == BookStatus::Finished) {
      status = tr(STR_FILTER_FINISHED);
      progressPercent = 100;
    } else if (signal->status == BookStatus::Reading) {
      status = tr(STR_FILTER_READING);
    }
  }
  char counter[48];
  if (progressPercent >= 0) {
    snprintf(counter, sizeof(counter), "%u of %u  |  %s %d%%", static_cast<unsigned>(selectorIndex + 1),
             static_cast<unsigned>(entryCount()), status, progressPercent);
  } else {
    snprintf(counter, sizeof(counter), "%u of %u  |  %s", static_cast<unsigned>(selectorIndex + 1),
             static_cast<unsigned>(entryCount()), status);
  }
  const auto counterText = renderer.truncatedText(SMALL_FONT_ID, counter, layout.textWidth, EpdFontFamily::REGULAR);
  const int counterWidth = renderer.getTextWidth(SMALL_FONT_ID, counterText.c_str(), EpdFontFamily::REGULAR);
  renderer.drawText(SMALL_FONT_ID, (pageWidth - counterWidth) / 2, layout.counterY, counterText.c_str(), true,
                    EpdFontFamily::REGULAR);
  if (progressPercent > 0) {
    const int progressWidth = layout.centerWidth;
    const int progressX = (pageWidth - progressWidth) / 2;
    renderer.drawRect(progressX, layout.progressY, progressWidth, 6, true);
    const int filled = std::max(1, ((progressWidth - 2) * std::clamp(progressPercent, 0, 100)) / 100);
    renderer.fillRect(progressX + 1, layout.progressY + 1, filled, 4, true);
  }
  if (folderTransitionInProgress.load(std::memory_order_acquire)) return true;
  renderer.displayWindow(0, layout.titleY, pageWidth, infoBottom - layout.titleY + 1, false, false);
  return true;
}

void FileBrowserActivity::refreshCoverGridAvailability() {
  // Indexed folders already know whether they contain directories. A pure
  // 266-book shelf such as All Books should not reread all 266 index rows just
  // to prove the absence of a subfolder before showing covers.
  if (usingIndex && fileIndex) {
    if (fileIndex->directoryCount() > 0 || fileIndex->fileCount() == 0) {
      coverGridAvailable = false;
      return;
    }
    const size_t count = entryCount();
    for (size_t i = 0; i < count; ++i) {
      const std::string entry = entryNameAt(i);
      if (entry.empty() || entry.back() == '/') continue;
      const std::string fullPath = buildFullPath(basepath, entry);
      if (FsHelpers::hasEpubExtension(fullPath) || FsHelpers::hasXtcExtension(fullPath)) {
        coverGridAvailable = true;
        return;
      }
    }
    coverGridAvailable = false;
    return;
  }

  bool foundBook = false;
  bool foundDirectory = false;
  const size_t count = entryCount();
  for (size_t i = 0; i < count; ++i) {
    const std::string entry = entryNameAt(i);
    if (entry.empty()) {
      continue;
    }
    if (entry.back() == '/') {
      foundDirectory = true;
    } else {
      const std::string fullPath = buildFullPath(basepath, entry);
      foundBook = foundBook || FsHelpers::hasEpubExtension(fullPath) || FsHelpers::hasXtcExtension(fullPath);
    }
    if (foundBook && foundDirectory) {
      break;
    }
  }

  // Folder levels retain the normal list. Cover layouts activate only on a pure book shelf.
  coverGridAvailable = foundBook && !foundDirectory;
}

FileBrowserActivity::CoverBookSignal FileBrowserActivity::loadBookSignal(const std::string& entry,
                                                                         const bool includeProgress) const {
  if (entry.empty() || entry.back() == '/') return loadBookSignalUncached(entry, includeProgress);

  const std::string fullPath = buildFullPath(basepath, entry);
  uint64_t key = 1469598103934665603ULL;  // FNV-1a 64
  for (const char c : fullPath) {
    key ^= static_cast<uint8_t>(c);
    key *= 1099511628211ULL;
  }

  size_t foundIndex = coverSignalMemo.size();
  for (size_t i = 0; i < coverSignalMemo.size(); ++i) {
    if (coverSignalMemo[i].key != key) continue;
    foundIndex = i;
    if (coverSignalMemo[i].progressComputed || !includeProgress) {
      CoverBookSignal signal;
      signal.entry = entry;
      signal.status = static_cast<BookStatus>(coverSignalMemo[i].status);
      signal.progressPercent = coverSignalMemo[i].progressPercent;
      return signal;
    }
    break;  // memo exists but lacks progress detail; recompute and upgrade it
  }

  CoverBookSignal signal = loadBookSignalUncached(entry, includeProgress);
  CoverSignalMemoEntry memoEntry;
  memoEntry.key = key;
  memoEntry.progressPercent = static_cast<int8_t>(std::clamp(signal.progressPercent, -1, 100));
  memoEntry.status = static_cast<uint8_t>(signal.status);
  memoEntry.progressComputed = includeProgress;
  if (foundIndex < coverSignalMemo.size()) {
    coverSignalMemo[foundIndex] = memoEntry;
  } else if (coverSignalMemo.size() < 320) {
    coverSignalMemo.push_back(memoEntry);
  }
  return signal;
}

FileBrowserActivity::CoverBookSignal FileBrowserActivity::loadBookSignalUncached(const std::string& entry,
                                                                                 const bool includeProgress) const {
  CoverBookSignal signal;
  signal.entry = entry;
  if (entry.empty() || entry.back() == '/') return signal;

  const std::string fullPath = buildFullPath(basepath, entry);
  std::string cachePath;
  if (FsHelpers::hasEpubExtension(fullPath)) {
    cachePath = Epub::cachePathForFilePath(fullPath, DUET_BOOKS_ROOT_PATH "");
  } else if (FsHelpers::hasXtcExtension(fullPath)) {
    cachePath = Xtc(fullPath, DUET_BOOKS_ROOT_PATH "").getCachePath();
  } else {
    return signal;
  }

  BookReadingStats stats;
  bool hasProgress = false;
  LibraryBookStatus indexedStatus;
  if (LibraryInsights::lookupBookStatus(cachePath, indexedStatus)) {
    stats.totalReadingSeconds = indexedStatus.readingSeconds;
    stats.sessionCount = indexedStatus.sessions;
    stats.isCompleted = indexedStatus.completed;
    hasProgress = indexedStatus.hasProgress;
  } else {
    // Index miss: show no badge rather than touch the SD. The cache root
    // holds ~2,400-2,700 entries on real cards, so ANY existence work under
    // it costs a 6-29 s linear scan (measured on-card, repair18.2-18-7), and
    // the insights index already covers every synced book. Cosmetic badge
    // accuracy on unsynced books is not worth frozen input.
  }
  if (stats.isCompleted) {
    signal.status = BookStatus::Finished;
    signal.progressPercent = 100;
    return signal;
  }

  if (stats.sessionCount > 0 || stats.totalReadingSeconds > 0 || hasProgress) {
    signal.status = BookStatus::Reading;
  }
  if (includeProgress && hasProgress) {
    RecentBook book;
    book.path = fullPath;
    const float progress = RecentBookProgress::loadPercent(book);
    if (RecentBookProgress::hasPercent(progress)) {
      signal.progressPercent = std::clamp(static_cast<int>(progress + 0.5f), 0, 100);
    }
  }
  return signal;
}

bool FileBrowserActivity::matchesBookStatusFilter(const BookStatus status) const {
  switch (bookStatusFilter) {
    case BookStatusFilter::All:
      return true;
    case BookStatusFilter::Reading:
      return status == BookStatus::Reading;
    case BookStatusFilter::Unread:
      return status == BookStatus::Unread;
    case BookStatusFilter::Finished:
      return status == BookStatus::Finished;
  }
  return true;
}

void FileBrowserActivity::applyBookStatusFilter() {
  if (mode != Mode::Books || bookStatusFilter == BookStatusFilter::All || !coverGridAvailable || usingIndex) return;
  files.erase(std::remove_if(files.begin(), files.end(),
                             [this](const std::string& entry) {
                               return !matchesBookStatusFilter(loadBookSignal(entry, false).status);
                             }),
              files.end());
}

void FileBrowserActivity::showBookStatusFilterMenu() {
  std::vector<std::string> options = {tr(STR_FILTER_ALL_BOOKS), tr(STR_FILTER_READING), tr(STR_FILTER_UNREAD),
                                      tr(STR_FILTER_FINISHED)};
  startActivityForResult(
      std::make_unique<OptionSelectionActivity>(renderer, mappedInput, "BookStatusFilter", StrId::STR_FILTER_BOOKS,
                                                std::move(options), static_cast<uint8_t>(bookStatusFilter)),
      [this](const ActivityResult& result) {
        if (result.isCancelled) return;
        const auto* selection = std::get_if<OptionSelectionResult>(&result.data);
        if (!selection || selection->index > static_cast<uint8_t>(BookStatusFilter::Finished)) return;
        bookStatusFilter = static_cast<BookStatusFilter>(selection->index);
        loadFiles();
        selectorIndex = 0;
        requestUpdate(true);
      });
}

const FileBrowserActivity::CoverBookSignal* FileBrowserActivity::coverSignalForEntry(const std::string& entry) const {
  for (const auto& signal : loadedCoverSignals) {
    if (signal.entry == entry) return &signal;
  }
  return nullptr;
}

bool FileBrowserActivity::loadCoverBitmap(const std::string& entry, const int width, const int height,
                                          CachedCoverBitmap& cachedCover) const {
  cachedCover = CachedCoverBitmap{};
  cachedCover.entry = entry;
  cachedCover.requestedWidth = width;
  cachedCover.requestedHeight = height;
  if (entry.empty() || entry.back() == '/') return false;

  const std::string fullPath = buildFullPath(basepath, entry);
  std::array<std::string, 14> candidates{};
  if (FsHelpers::hasEpubExtension(fullPath)) {
    // Candidates must point at Duet's sharded thumbnail home; probing
    // only the legacy in-cache-dir location could never see a relocated or
    // freshly generated thumbnail, so shelves re-queued generation on every
    // visit and every probe paid a linear scan of the crowded cache root.
    // The static helper is pure string work: prefetch slices stay exact,
    // cheap reads, and all crowded-directory probing rides the idle-gated
    // generation queue.
    const std::string cachePath = Epub::cachePathForFilePath(fullPath, DUET_BOOKS_ROOT_PATH "");
    const auto exactThumb = [&cachePath](const int candidateWidth, const int candidateHeight) {
      return Epub::thumbBmpPathForCache(cachePath, candidateWidth, candidateHeight);
    };
    if (width <= 130) {
      candidates = {exactThumb(width, height), exactThumb(123, 180), exactThumb(123, 181),
                    exactThumb(125, 183), exactThumb(160, 234), exactThumb(184, 268),
                    exactThumb(193, 282), exactThumb(196, 286), exactThumb(230, 338),
                    exactThumb(121, 177), exactThumb(120, 180), exactThumb(94, 142),
                    exactThumb(93, 140), std::string()};
    } else {
      candidates = {exactThumb(width, height), exactThumb(230, 338), exactThumb(196, 286),
                    exactThumb(193, 282), exactThumb(184, 268), exactThumb(160, 234),
                    exactThumb(125, 183), exactThumb(123, 181), exactThumb(123, 180),
                    exactThumb(121, 177), exactThumb(120, 180), exactThumb(94, 142),
                    exactThumb(93, 140), std::string()};
    }
  } else if (FsHelpers::hasXtcExtension(fullPath)) {
    Xtc xtc(fullPath, DUET_BOOKS_ROOT_PATH "");
    const auto exactThumb = [&xtc](const int candidateWidth, const int candidateHeight) {
      return xtc.getThumbBmpPath(static_cast<uint16_t>(candidateWidth),
                                 static_cast<uint16_t>(candidateHeight));
    };
    if (width <= 130) {
      candidates = {exactThumb(width, height), exactThumb(123, 180), exactThumb(123, 181),
                    exactThumb(125, 183), exactThumb(160, 234), exactThumb(184, 268),
                    exactThumb(193, 282), exactThumb(196, 286), exactThumb(230, 338),
                    exactThumb(121, 177), exactThumb(120, 180), exactThumb(94, 142),
                    exactThumb(93, 140), std::string()};
    } else {
      candidates = {exactThumb(width, height), exactThumb(230, 338), exactThumb(196, 286),
                    exactThumb(193, 282), exactThumb(184, 268), exactThumb(160, 234),
                    exactThumb(125, 183), exactThumb(123, 181), exactThumb(123, 180),
                    exactThumb(121, 177), exactThumb(120, 180), exactThumb(94, 142),
                    exactThumb(93, 140), std::string()};
    }
  } else {
    return false;
  }

  for (size_t candidateIndex = 0; candidateIndex < candidates.size(); ++candidateIndex) {
    const std::string& thumbPath = candidates[candidateIndex];
    const auto firstCandidate = candidates.begin();
    const auto currentCandidate = firstCandidate + candidateIndex;
    if (thumbPath.empty() || std::find(firstCandidate, currentCandidate, thumbPath) != currentCandidate) {
      continue;
    }

    FsFile file;
    if (!Storage.openFileForRead("FileBrowser", thumbPath, file)) continue;
    Bitmap bitmap(file);
    const BmpReaderError headerResult = bitmap.parseHeaders();
    if (headerResult != BmpReaderError::Ok || !bitmap.is1Bit() || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) {
      file.close();
      continue;
    }

    const size_t dataSize =
        static_cast<size_t>(bitmap.getRowBytes()) * static_cast<size_t>(bitmap.getHeight());
    auto rows = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[dataSize]);
    if (!rows || bitmap.readRawRows(rows.get(), dataSize) != BmpReaderError::Ok) {
      file.close();
      continue;
    }

    cachedCover.rows = std::move(rows);
    cachedCover.rowDataSize = dataSize;
    cachedCover.width = bitmap.getWidth();
    cachedCover.height = bitmap.getHeight();
    cachedCover.rowBytes = bitmap.getRowBytes();
    cachedCover.topDown = bitmap.isTopDown();
    cachedCover.blackPaletteIndex = bitmap.get1BitBlackPaletteIndex();
    cachedCover.exactSize = cachedCover.width == width && cachedCover.height == height;
    file.close();
    // Any valid cached thumbnail can be scaled into the requested cell. The
    // exact-size flag is retained so callers can prefer it, but returning
    // false here incorrectly queued cover generation (disabled during browse)
    // and left perfectly usable universal thumbnails looking "missing".
    return true;
  }
  return false;
}

const FileBrowserActivity::CachedCoverBitmap* FileBrowserActivity::coverBitmapForEntry(
    const std::string& entry, const int width, const int height) const {
  for (const auto& cachedCover : loadedCoverBitmaps) {
    if (cachedCover.matches(entry, width, height) && cachedCover.isReady()) return &cachedCover;
  }
  return nullptr;
}

const FileBrowserActivity::CachedCoverBitmap* FileBrowserActivity::carouselDetailBitmapForEntry(
    const std::string& entry) const {
  const CachedCoverBitmap* best = nullptr;
  for (const auto& cachedCover : loadedCarouselDetailBitmaps) {
    if (cachedCover.entry != entry || !cachedCover.isReady()) continue;
    if (!best || cachedCover.rowDataSize > best->rowDataSize) best = &cachedCover;
  }
  return best;
}

bool FileBrowserActivity::isCoverGridActive() const {
  return mode == Mode::Books && coverGridAvailable &&
         SETTINGS.fileBrowserDisplay == CrossPointSettings::FILE_BROWSER_DISPLAY_COVERS;
}

bool FileBrowserActivity::isCoverCarouselActive() const {
  return mode == Mode::Books && coverGridAvailable &&
         SETTINGS.fileBrowserDisplay == CrossPointSettings::FILE_BROWSER_DISPLAY_CAROUSEL;
}

std::string FileBrowserActivity::coverThumbPathForEntry(const std::string& entry, const int width,
                                                        const int height) const {
  if (entry.empty() || entry.back() == '/' || width <= 0 || height <= 0) {
    return "";
  }

  const std::string fullPath = buildFullPath(basepath, entry);
  if (FsHelpers::hasEpubExtension(fullPath)) {
    // Generation must test the exact sharded destination. The instance helper
    // may return an old height-only path; treating that as the requested file
    // made generation skip while this browser (correctly) probed only the
    // sharded cache, leaving grids blank and carousel cards permanently soft.
    const std::string cachePath = Epub::cachePathForFilePath(fullPath, DUET_BOOKS_ROOT_PATH "");
    return Epub::thumbBmpPathForCache(cachePath, width, height);
  }
  if (FsHelpers::hasXtcExtension(fullPath)) {
    return Xtc(fullPath, DUET_BOOKS_ROOT_PATH "").getThumbBmpPath(static_cast<uint16_t>(width), static_cast<uint16_t>(height));
  }
  return "";
}

void FileBrowserActivity::loadCoverPage(const int pageStart, const int itemsPerPage, const int width,
                                        const int height) {
  const int totalEntries = static_cast<int>(entryCount());
  if (mode != Mode::Books || totalEntries <= 0 || pageStart < 0 || itemsPerPage <= 0 || width <= 0 || height <= 0) {
    return;
  }

  // Rebase the three-page cache only between paints. Waiting here would make
  // input share the e-ink BUSY delay; a failed try simply retries next loop.
  RenderLock cacheLock(RenderLock::AcquireMode::Try);
  if (!cacheLock.ownsLock()) return;

  const int previousPageStart = loadedCoverPageStart;
  const int previousItemsPerPage = loadedCoverItemsPerPage;
  const bool matchesPreviousGeometry =
      loadedCoverWidth == width && loadedCoverHeight == height && previousItemsPerPage == itemsPerPage;
  invalidateCoverGridSelectionBackground();
  const int pageEnd = std::min(pageStart + itemsPerPage, totalEntries);
  loadedCoverPageStart = pageStart;
  loadedCoverItemsPerPage = itemsPerPage;
  loadedCoverWidth = width;
  loadedCoverHeight = height;
  pendingCoverGenerationCount = 0;
  pendingCoverGenerationNext = 0;
  pendingCoverGenerationNextAt = millis() + COVER_GRID_GENERATION_INITIAL_DELAY_MS;
  pendingCoverSignalCount = 0;
  pendingCoverSignalNext = 0;
  pendingCoverSignalNextAt = millis() + COVER_GRID_SIGNAL_PREFETCH_INITIAL_DELAY_MS;
  pendingCoverPrefetchCount = 0;
  pendingCoverPrefetchNext = 0;
  pendingVisibleCoverPrefetchCount = 0;
  pendingCoverPrefetchNextAt = millis() + COVER_GRID_PREFETCH_INITIAL_DELAY_MS;
  deferCoverBackgroundWork();

  auto previousSignals = std::move(loadedCoverSignals);
  auto previousCoverBitmaps = std::move(loadedCoverBitmaps);
  for (auto& signal : loadedCoverSignals) signal = CoverBookSignal{};
  for (auto& cover : loadedCoverBitmaps) cover = CachedCoverBitmap{};
  std::array<bool, COVER_SIGNAL_COUNT> reusedSignalSlots{};
  int reusedBitmaps = 0;
  int reusedSignals = 0;

  const auto reuseBitmap = [&](const std::string& entry, const size_t slot, const int requestedWidth,
                               const int requestedHeight) {
    auto previousBitmap =
        std::find_if(previousCoverBitmaps.begin(), previousCoverBitmaps.end(),
                     [&entry, requestedWidth, requestedHeight](const CachedCoverBitmap& cover) {
                       return cover.matches(entry, requestedWidth, requestedHeight) && cover.isReady();
                     });
    if (previousBitmap == previousCoverBitmaps.end()) {
      previousBitmap =
          std::find_if(previousCoverBitmaps.begin(), previousCoverBitmaps.end(),
                       [&entry](const CachedCoverBitmap& cover) {
                         return cover.entry == entry && cover.isReady();
                       });
    }
    if (previousBitmap == previousCoverBitmaps.end() || slot >= loadedCoverBitmaps.size()) {
      return false;
    }
    loadedCoverBitmaps[slot] = std::move(*previousBitmap);
    loadedCoverBitmaps[slot].requestedWidth = requestedWidth;
    loadedCoverBitmaps[slot].requestedHeight = requestedHeight;
    loadedCoverBitmaps[slot].exactSize =
        loadedCoverBitmaps[slot].width == requestedWidth &&
        loadedCoverBitmaps[slot].height == requestedHeight;
    ++reusedBitmaps;
    return true;
  };

  const auto queueBitmap = [&](const int index, const size_t slot, const int requestedWidth,
                               const int requestedHeight) {
    if (pendingCoverPrefetchCount >= pendingCoverPrefetchIndices.size() ||
        slot >= loadedCoverBitmaps.size()) {
      return false;
    }
    pendingCoverPrefetchIndices[pendingCoverPrefetchCount] = index;
    pendingCoverPrefetchSlots[pendingCoverPrefetchCount] = slot;
    pendingCoverPrefetchWidths[pendingCoverPrefetchCount] =
        static_cast<uint16_t>(requestedWidth);
    pendingCoverPrefetchHeights[pendingCoverPrefetchCount] =
        static_cast<uint16_t>(requestedHeight);
    ++pendingCoverPrefetchCount;
    return true;
  };

  const auto reuseSignal = [&](const std::string& entry, const size_t slot) {
    auto previousSignal = std::find_if(previousSignals.begin(), previousSignals.end(),
                                       [&entry](const CoverBookSignal& signal) { return signal.entry == entry; });
    if (previousSignal == previousSignals.end() || slot >= loadedCoverSignals.size()) return false;
    loadedCoverSignals[slot] = std::move(*previousSignal);
    reusedSignalSlots[slot] = true;
    ++reusedSignals;
    return true;
  };

  const auto queueSignal = [&](const int index, const size_t slot) {
    if (pendingCoverSignalCount >= pendingCoverSignalIndices.size() || slot >= loadedCoverSignals.size()) return;
    pendingCoverSignalIndices[pendingCoverSignalCount] = index;
    pendingCoverSignalSlots[pendingCoverSignalCount] = slot;
    ++pendingCoverSignalCount;
  };

  for (int i = pageStart; i < pageEnd && i - pageStart < static_cast<int>(loadedCoverSignals.size()); ++i) {
    const size_t slot = static_cast<size_t>(i - pageStart);
    const std::string entry = entryNameAt(static_cast<size_t>(i));
    loadedCoverSignals[slot].entry = entry;
    reuseSignal(entry, slot);
    reuseBitmap(entry, slot, width, height);
  }

  // Queue visible signals and bitmap misses selected-first. The loop handles
  // buttons before either queue touches disk, so the cursor is usable while
  // titles, authors, cover outlines, and status placeholders are visible.
  const int selectedIndex = static_cast<int>(selectorIndex);
  if (selectedIndex >= pageStart && selectedIndex < pageEnd) {
    const size_t selectedSlot = static_cast<size_t>(selectedIndex - pageStart);
    if (!reusedSignalSlots[selectedSlot]) queueSignal(selectedIndex, selectedSlot);
    if (!loadedCoverBitmaps[selectedSlot].isReady() ||
        !loadedCoverBitmaps[selectedSlot].exactSize) {
      queueBitmap(selectedIndex, selectedSlot, width, height);
    }
  }
  for (int index = pageStart; index < pageEnd; ++index) {
    if (index == selectedIndex) continue;
    const size_t slot = static_cast<size_t>(index - pageStart);
    if (!reusedSignalSlots[slot]) queueSignal(index, slot);
    if (!loadedCoverBitmaps[slot].isReady() || !loadedCoverBitmaps[slot].exactSize) {
      queueBitmap(index, slot, width, height);
    }
  }
  pendingVisibleCoverPrefetchCount = pendingCoverPrefetchCount;

  // Keep both neighboring pages in distinct slot ranges. Requests alternate
  // previous/next by cell so a low-memory stop leaves useful coverage on both
  // sides. On a page turn, the page just left and the page entered are normally
  // reused from the previous three-page window; only the newly exposed edge
  // needs SD reads.
  const int previousAdjacentStart = pageStart - itemsPerPage;
  const int nextAdjacentStart = pageStart + itemsPerPage;
  const bool previousAdjacentValid = previousAdjacentStart >= 0;
  const bool nextAdjacentValid = nextAdjacentStart < totalEntries;
  const size_t previousSlotStart = static_cast<size_t>(itemsPerPage);
  const size_t nextSlotStart = static_cast<size_t>(itemsPerPage) * 2U;
  const int lookaheadWidth = coverGridLookaheadWidth();
  const int lookaheadHeight = coverGridLookaheadHeight();
  int queuedPrevious = 0;
  int queuedNext = 0;
  int reusedPrevious = 0;
  int reusedNext = 0;
  for (int offset = 0; offset < itemsPerPage; ++offset) {
    if (previousAdjacentValid) {
      const int index = previousAdjacentStart + offset;
      const size_t slot = previousSlotStart + static_cast<size_t>(offset);
      if (index < totalEntries && slot < loadedCoverBitmaps.size()) {
        const std::string entry = entryNameAt(static_cast<size_t>(index));
        if (matchesPreviousGeometry &&
            reuseBitmap(entry, slot, lookaheadWidth, lookaheadHeight)) {
          ++reusedPrevious;
        } else if (queueBitmap(index, slot, lookaheadWidth, lookaheadHeight)) {
          ++queuedPrevious;
        }
      }
    }
    if (nextAdjacentValid) {
      const int index = nextAdjacentStart + offset;
      const size_t slot = nextSlotStart + static_cast<size_t>(offset);
      if (index < totalEntries && slot < loadedCoverBitmaps.size()) {
        const std::string entry = entryNameAt(static_cast<size_t>(index));
        if (matchesPreviousGeometry &&
            reuseBitmap(entry, slot, lookaheadWidth, lookaheadHeight)) {
          ++reusedNext;
        } else if (queueBitmap(index, slot, lookaheadWidth, lookaheadHeight)) {
          ++queuedNext;
        }
      }
    }
  }

  // On a page-crossing button move, defer the expensive shelf paint until the
  // user pauses. Initial entry, density changes, and non-navigation loads still
  // render immediately. Visible covers are populated later during idle passes.
  LOG_DBG("FileBrowser",
          "Cover page %d (%dx%d) interactive: reused %d signal(s), %d bitmap(s), queued %u signal(s), %u visible; "
          "adjacent previous %d reused/%d queued, next %d reused/%d queued",
          pageStart, width, height, reusedSignals, reusedBitmaps, static_cast<unsigned>(pendingCoverSignalCount),
          static_cast<unsigned>(pendingVisibleCoverPrefetchCount), reusedPrevious, queuedPrevious, reusedNext,
          queuedNext);
  if (suppressNextCoverPagePaint) {
    suppressNextCoverPagePaint = false;
  } else {
    requestUpdate();
  }
}

void FileBrowserActivity::prefetchNextQueuedCoverSignal() {
  if (pendingCoverSignalNext >= pendingCoverSignalCount) return;

  const int expectedPageStart = loadedCoverPageStart;
  const size_t queueIndex = pendingCoverSignalNext++;
  const int index = pendingCoverSignalIndices[queueIndex];
  const size_t slot = pendingCoverSignalSlots[queueIndex];
  if (index < 0 || index >= static_cast<int>(entryCount()) || slot >= loadedCoverSignals.size() ||
      expectedPageStart == NO_COVER_PAGE_LOADED) {
    pendingCoverSignalNextAt = millis() + COVER_GRID_SIGNAL_PREFETCH_GAP_MS;
    return;
  }

  const std::string entry = entryNameAt(static_cast<size_t>(index));
  writePickerHeartbeat("sig", index);
  CoverBookSignal prefetched = loadBookSignal(entry, true);
  bool shouldRefresh = false;
  {
    RenderLock lock(*this);
    if (loadedCoverPageStart == expectedPageStart && slot < loadedCoverSignals.size()) {
      loadedCoverSignals[slot] = std::move(prefetched);
      // Avoid a full e-ink paint for every row. When covers are still
      // arriving their final paint carries these signals; otherwise redraw
      // once after the visible page's final status is ready.
      shouldRefresh = pendingCoverSignalNext >= pendingCoverSignalCount &&
                      pendingCoverPrefetchNext >= pendingVisibleCoverPrefetchCount;
    }
  }
  pendingCoverSignalNextAt = millis() + COVER_GRID_SIGNAL_PREFETCH_GAP_MS;
  coverBackgroundWorkNotBefore = pendingCoverSignalNextAt;
  if (shouldRefresh) {
    queueCoverBackgroundRefresh();
  }
}

void FileBrowserActivity::prefetchNextQueuedCover() {
  if (pendingCoverPrefetchNext >= pendingCoverPrefetchCount) return;

  const int expectedPageStart = loadedCoverPageStart;
  const int expectedWidth = loadedCoverWidth;
  const int expectedHeight = loadedCoverHeight;
  bool visiblePageFinished = false;
  bool selectedCoverReady = false;
  for (size_t batchIndex = 0;
       batchIndex < COVER_GRID_PREFETCH_BATCH_SIZE && pendingCoverPrefetchNext < pendingCoverPrefetchCount;
       ++batchIndex) {
    const size_t queueIndex = pendingCoverPrefetchNext;
    const int index = pendingCoverPrefetchIndices[queueIndex];
    const size_t slot = pendingCoverPrefetchSlots[queueIndex];
    const int requestedWidth = pendingCoverPrefetchWidths[queueIndex];
    const int requestedHeight = pendingCoverPrefetchHeights[queueIndex];
    const bool visibleRequest = queueIndex < pendingVisibleCoverPrefetchCount;
    if (index < 0 || index >= static_cast<int>(entryCount()) || slot >= loadedCoverBitmaps.size() ||
        requestedWidth <= 0 || requestedHeight <= 0 ||
        expectedPageStart == NO_COVER_PAGE_LOADED) {
      ++pendingCoverPrefetchNext;
      continue;
    }

    const size_t estimatedRowBytes =
        ((static_cast<size_t>(requestedWidth) + 31U) / 32U) * 4U;
    const size_t estimatedBytes =
        estimatedRowBytes * static_cast<size_t>(requestedHeight);
    if (visibleRequest) {
      const auto heap = MemoryBudget::snapshot();
      const uint32_t requiredFree =
          COVER_GRID_VISIBLE_MIN_FREE_AFTER_LOAD + static_cast<uint32_t>(estimatedBytes);
      const uint32_t requiredMaxAlloc =
          static_cast<uint32_t>(estimatedBytes) + COVER_GRID_LOOKAHEAD_ALLOC_MARGIN;
      if (!MemoryBudget::hasHeap(heap, requiredFree, requiredMaxAlloc)) {
        // Visible art wins over speculative neighbors. Releasing those slots
        // before the allocation prevents a newly entered page from remaining
        // blank simply because its previous page was retained.
        releaseNonVisibleCoverBitmapsForHeap();
      }
    } else {
      const auto heap = MemoryBudget::snapshot();
      const uint32_t requiredFree =
          COVER_GRID_LOOKAHEAD_MIN_FREE_AFTER_LOAD + static_cast<uint32_t>(estimatedBytes);
      const uint32_t requiredMaxAlloc =
          std::max<uint32_t>(COVER_GRID_LOOKAHEAD_MIN_MAX_ALLOC,
                             static_cast<uint32_t>(estimatedBytes) + COVER_GRID_LOOKAHEAD_ALLOC_MARGIN);
      if (!MemoryBudget::hasHeap(heap, requiredFree, requiredMaxAlloc)) {
        LOG_DBG("FileBrowser",
                "Stopping adjacent grid-page hydration at %u/%u: low heap "
                "(free=%u maxAlloc=%u need=%u/%u)",
                static_cast<unsigned>(queueIndex - pendingVisibleCoverPrefetchCount),
                static_cast<unsigned>(pendingCoverPrefetchCount - pendingVisibleCoverPrefetchCount),
                heap.freeHeap, heap.maxAllocHeap, requiredFree, requiredMaxAlloc);
        writePickerHeartbeat("gridlookmem", static_cast<int>(heap.maxAllocHeap));
        // Keep the request alive. A page turn may release a far window and
        // make it affordable; permanently jumping to the queue end was why
        // later pages never hydrated during the same browser visit.
        pendingCoverPrefetchNextAt = millis() + COVER_GRID_LOOKAHEAD_RETRY_MS;
        coverBackgroundWorkNotBefore = pendingCoverPrefetchNextAt;
        return;
      }
    }

    ++pendingCoverPrefetchNext;
    const std::string entry = entryNameAt(static_cast<size_t>(index));
    CachedCoverBitmap prefetched;
    const bool loaded =
        loadCoverBitmap(entry, requestedWidth, requestedHeight, prefetched);
    {
      RenderLock lock(*this);
      if (loadedCoverPageStart == expectedPageStart && loadedCoverWidth == expectedWidth &&
          loadedCoverHeight == expectedHeight) {
        // An exact-size upgrade is optional; never replace an already visible
        // lower-resolution cover with an empty object when allocation or SD
        // access fails.
        if (loaded) {
          loadedCoverBitmaps[slot] = std::move(prefetched);
        }
        if ((!loaded || !loadedCoverBitmaps[slot].exactSize) && visibleRequest &&
            pendingCoverGenerationCount < pendingCoverGenerationIndices.size()) {
          pendingCoverGenerationIndices[pendingCoverGenerationCount++] = index;
        }
        selectedCoverReady =
            selectedCoverReady ||
            (loadedCoverBitmaps[slot].isReady() &&
             index == static_cast<int>(selectorIndex) && visibleRequest);
        visiblePageFinished = pendingVisibleCoverPrefetchCount > 0 &&
                              pendingCoverPrefetchNext >= pendingVisibleCoverPrefetchCount;
      }
    }
  }
  pendingCoverPrefetchNextAt = millis() + COVER_GRID_PREFETCH_GAP_MS;
  coverBackgroundWorkNotBefore = pendingCoverPrefetchNextAt;
  if (selectedCoverReady || visiblePageFinished) {
    queueCoverBackgroundRefresh();
  }
}

void FileBrowserActivity::prefetchNextCarouselCover() {
  if (pendingCoverPrefetchNext >= pendingCoverPrefetchCount) return;

  const int expectedCenterIndex = loadedCarouselCenterIndex;
  const size_t queueIndex = pendingCoverPrefetchNext++;
  const int index = pendingCoverPrefetchIndices[queueIndex];
  const size_t slot = pendingCoverPrefetchSlots[queueIndex];
  if (index < 0 || index >= static_cast<int>(entryCount()) || slot >= loadedCoverBitmaps.size() ||
      expectedCenterIndex == NO_COVER_PAGE_LOADED) {
    pendingCoverPrefetchNextAt = millis() + COVER_CAROUSEL_COVER_PREFETCH_GAP_MS;
    return;
  }

  const std::string entry = entryNameAt(static_cast<size_t>(index));
  CachedCoverBitmap prefetched;
  const bool loaded =
      loadCoverBitmap(entry, COVER_GRID_MAX_COVER_WIDTH, COVER_GRID_MAX_COVER_HEIGHT, prefetched);
  bool shouldRefresh = false;
  {
    RenderLock lock(*this);
    if (loadedCarouselCenterIndex == expectedCenterIndex && slot < loadedCoverBitmaps.size()) {
      if (loaded) {
        loadedCoverBitmaps[slot] = std::move(prefetched);
      }
      if (!loaded && pendingCoverGenerationCount < pendingCoverGenerationIndices.size()) {
        pendingCoverGenerationIndices[pendingCoverGenerationCount++] = index;
      }
      // The selected cover is followed immediately by its detail image. The
      // remaining four covers redraw together to avoid five slow e-paper paints.
      shouldRefresh = pendingCoverPrefetchNext >= pendingCoverPrefetchCount;
    }
  }
  pendingCoverPrefetchNextAt = millis() + COVER_CAROUSEL_COVER_PREFETCH_GAP_MS;
  if (shouldRefresh) queueCoverBackgroundRefresh();
}

void FileBrowserActivity::prefetchNextCarouselDetail() {
  if (pendingCarouselDetailNext >= pendingCarouselDetailCount) return;

  const int expectedCenterIndex = loadedCarouselCenterIndex;
  const size_t queueIndex = pendingCarouselDetailNext++;
  const int index = pendingCarouselDetailIndices[queueIndex];
  const size_t slot = pendingCarouselDetailSlots[queueIndex];
  if (index < 0 || index >= static_cast<int>(entryCount()) || slot >= loadedCarouselDetailBitmaps.size() ||
      expectedCenterIndex == NO_COVER_PAGE_LOADED) {
    pendingCarouselDetailNextAt = millis() + COVER_CAROUSEL_DETAIL_PREFETCH_GAP_MS;
    return;
  }

  const std::string entry = entryNameAt(static_cast<size_t>(index));
  CachedCoverBitmap prefetched;
  const int detailWidth = slot == 0 ? COVER_CAROUSEL_CENTER_MAX_WIDTH : COVER_CAROUSEL_SIDE_SOURCE_WIDTH;
  const int detailHeight = slot == 0 ? COVER_CAROUSEL_CENTER_MAX_HEIGHT : COVER_CAROUSEL_SIDE_SOURCE_HEIGHT;
  const bool loaded = loadCoverBitmap(entry, detailWidth, detailHeight, prefetched);
  bool selectedDetailReady = false;
  bool visibleDetailsFinished = false;
  {
    RenderLock lock(*this);
    if (loadedCarouselCenterIndex == expectedCenterIndex && slot < loadedCarouselDetailBitmaps.size()) {
      // A side cover reused as the new center is already a valid first paint.
      // Keep it if the sharper exact-size allocation or SD read fails.
      if (loaded) {
        loadedCarouselDetailBitmaps[slot] = std::move(prefetched);
        // Carousel detail tiles supersede the old base thumbnail. Keeping
        // both copies consumed enough heap on X4 to prevent sharp thumbnails
        // from ever being generated.
        for (auto& baseCover : loadedCoverBitmaps) {
          if (baseCover.entry == entry) baseCover = CachedCoverBitmap{};
        }
      }
      selectedDetailReady =
          slot == 0 && loadedCarouselDetailBitmaps[slot].isReady();
      visibleDetailsFinished =
          pendingVisibleCarouselDetailCount > 0 &&
          pendingCarouselDetailNext >= pendingVisibleCarouselDetailCount;
      if ((!loaded || !loadedCarouselDetailBitmaps[slot].exactSize) &&
          pendingCoverGenerationCount < pendingCoverGenerationIndices.size()) {
        bool alreadyQueued = false;
        for (size_t i = pendingCoverGenerationNext; i < pendingCoverGenerationCount; ++i) {
          if (pendingCoverGenerationIndices[i] == index) {
            alreadyQueued = true;
            break;
          }
        }
        if (!alreadyQueued) pendingCoverGenerationIndices[pendingCoverGenerationCount++] = index;
      }
    }
  }
  pendingCarouselDetailNextAt = millis() + COVER_CAROUSEL_DETAIL_PREFETCH_GAP_MS;
  if (selectedDetailReady || visibleDetailsFinished) {
    queueCoverBackgroundRefresh();
  }
}

void FileBrowserActivity::prefetchNextCarouselSignal() {
  if (pendingCarouselSignalNext >= pendingCarouselSignalCount) return;

  const int expectedCenterIndex = loadedCarouselCenterIndex;
  const size_t queueIndex = pendingCarouselSignalNext++;
  const int index = pendingCarouselSignalIndices[queueIndex];
  const size_t slot = pendingCarouselSignalSlots[queueIndex];
  if (index < 0 || index >= static_cast<int>(entryCount()) || slot >= loadedCoverSignals.size() ||
      expectedCenterIndex == NO_COVER_PAGE_LOADED) {
    pendingCarouselSignalNextAt = millis() + COVER_CAROUSEL_SIGNAL_PREFETCH_GAP_MS;
    return;
  }

  const std::string entry = entryNameAt(static_cast<size_t>(index));
  writePickerHeartbeat("sig", index);
  CoverBookSignal prefetched = loadBookSignal(entry, true);
  bool selectedSignalReady = false;
  {
    RenderLock lock(*this);
    if (loadedCarouselCenterIndex == expectedCenterIndex && slot < loadedCoverSignals.size()) {
      loadedCoverSignals[slot] = std::move(prefetched);
      selectedSignalReady = slot == 0;
    }
  }
  pendingCarouselSignalNextAt = millis() + COVER_CAROUSEL_SIGNAL_PREFETCH_GAP_MS;
  // If the selected detail is already cached, this is the final visible piece
  // of data. Otherwise the detail worker will paint the cover and status once.
  if (selectedSignalReady && carouselDetailBitmapForEntry(entry) != nullptr) queueCoverBackgroundRefresh();
}

bool FileBrowserActivity::shouldCancelQueuedCoverGeneration(void* context) {
  auto* self = static_cast<FileBrowserActivity*>(context);
  if (!self) return false;

  self->mappedInput.updatePreservingEvents();
  const bool inputObserved =
      self->mappedInput.wasAnyPressed() || self->mappedInput.wasAnyReleased() ||
      self->mappedInput.isPressed(MappedInputManager::Button::Left) ||
      self->mappedInput.isPressed(MappedInputManager::Button::Right) ||
      self->mappedInput.isPressed(MappedInputManager::Button::Up) ||
      self->mappedInput.isPressed(MappedInputManager::Button::Down) ||
      self->mappedInput.isPressed(MappedInputManager::Button::Confirm) ||
      self->mappedInput.isPressed(MappedInputManager::Button::Back) ||
      self->mappedInput.isPressed(MappedInputManager::Button::Power) ||
      self->mappedInput.isPressed(MappedInputManager::Button::PageBack) ||
      self->mappedInput.isPressed(MappedInputManager::Button::PageForward);
  if (!inputObserved) return false;

  self->lastPickerInputMs = millis();
  self->pendingCoverGenerationCancelled = true;
  self->writePickerHeartbeat("gencancel", static_cast<int>(self->selectorIndex));
  return true;
}

void FileBrowserActivity::generateNextQueuedCover() {
  if (pendingCoverGenerationNext >= pendingCoverGenerationCount) return;

  const size_t queueSlot = pendingCoverGenerationNext;
  const int index = pendingCoverGenerationIndices[queueSlot];
  const bool carousel = isCoverCarouselActive();
  if ((!carousel && index < loadedCoverPageStart) || index >= static_cast<int>(entryCount())) {
    pendingCoverGenerationNext = queueSlot + 1;
    return;
  }

  const std::string entry = entryNameAt(static_cast<size_t>(index));
  writePickerHeartbeat("gen", index);
  const std::string fullPath = buildFullPath(basepath, entry);
  const bool carouselCenter = carousel && index == loadedCarouselCenterIndex;
  const int generationWidth = carousel ? (carouselCenter ? COVER_CAROUSEL_CENTER_MAX_WIDTH
                                                         : COVER_CAROUSEL_SIDE_SOURCE_WIDTH)
                                       : loadedCoverWidth;
  const int generationHeight = carousel ? (carouselCenter ? COVER_CAROUSEL_CENTER_MAX_HEIGHT
                                                           : COVER_CAROUSEL_SIDE_SOURCE_HEIGHT)
                                        : loadedCoverHeight;
  const std::string thumbPath = coverThumbPathForEntry(entry, generationWidth, generationHeight);
  bool generated = thumbPath.empty() || Storage.existsForRead(thumbPath);
  pendingCoverGenerationCancelled = false;
  if (!generated && FsHelpers::hasEpubExtension(fullPath)) {
    Epub epub(fullPath, DUET_BOOKS_ROOT_PATH "");
    generated = epub.load(true, true, &FileBrowserActivity::shouldCancelQueuedCoverGeneration, this) &&
                epub.generateThumbBmp(generationWidth, generationHeight, &renderer, SETTINGS.getReaderFontId(),
                                      &FileBrowserActivity::shouldCancelQueuedCoverGeneration, this);
  } else if (!generated && FsHelpers::hasXtcExtension(fullPath)) {
    Xtc xtc(fullPath, DUET_BOOKS_ROOT_PATH "");
    generated = xtc.load() && xtc.generateThumbBmp(static_cast<uint16_t>(generationWidth),
                                                   static_cast<uint16_t>(generationHeight));
  }
  if (!generated) {
    if (pendingCoverGenerationCancelled) {
      LOG_DBG("FileBrowser", "Queued cover thumbnail generation cancelled: %s", fullPath.c_str());
      pendingCoverGenerationNextAt = millis() + COVER_GENERATION_CANCEL_RETRY_MS;
      return;
    }
    LOG_ERR("FileBrowser", "Could not generate queued cover thumbnail: %s", fullPath.c_str());
  } else {
    auto refreshCachedCover = [this, &entry](CachedCoverBitmap& cachedCover) {
      if (cachedCover.entry != entry || cachedCover.requestedWidth <= 0 || cachedCover.requestedHeight <= 0) return;
      CachedCoverBitmap fresh;
      if (loadCoverBitmap(entry, cachedCover.requestedWidth, cachedCover.requestedHeight, fresh)) {
        RenderLock lock(*this);
        if (cachedCover.entry == entry) cachedCover = std::move(fresh);
      }
    };
    for (auto& cachedCover : loadedCoverBitmaps) {
      refreshCachedCover(cachedCover);
    }
    for (auto& cachedCover : loadedCarouselDetailBitmaps) {
      refreshCachedCover(cachedCover);
    }
  }

  pendingCoverGenerationNext = queueSlot + 1;
  pendingCoverGenerationNextAt =
      millis() + (carousel ? COVER_CAROUSEL_GENERATION_GAP_MS : COVER_GRID_GENERATION_GAP_MS);
  queueCoverBackgroundRefresh();
}

void FileBrowserActivity::loadCarouselCovers(const int centerIndex, const int centerWidth, const int centerHeight,
                                             const int sideWidth, const int sideHeight) {
  const int totalEntries = static_cast<int>(entryCount());
  if (mode != Mode::Books || totalEntries <= 0 || centerIndex < 0 || centerIndex >= totalEntries || centerWidth <= 0 ||
      centerHeight <= 0 || sideWidth <= 0 || sideHeight <= 0) {
    return;
  }

  // The render task reads these unique_ptr-backed caches directly. Rebase the
  // seven-cover window only between paints so a title refresh cannot observe
  // a half-moved cache. Never wait on the input task; retry on the next loop.
  RenderLock cacheLock(RenderLock::AcquireMode::Try);
  if (!cacheLock.ownsLock()) return;

  invalidateCoverGridSelectionBackground();

  std::array<int, CAROUSEL_VISIBLE_COUNT> requests{};
  int requestCount = 0;
  requests[requestCount++] = centerIndex;
  if (totalEntries >= 2) {
    requests[requestCount++] = (centerIndex + 1) % totalEntries;
  }
  if (totalEntries >= 3) {
    requests[requestCount++] = (centerIndex + totalEntries - 1) % totalEntries;
  }
  if (totalEntries >= 4) {
    requests[requestCount++] = (centerIndex + 2) % totalEntries;
  }
  if (totalEntries >= 5) {
    requests[requestCount++] = (centerIndex + totalEntries - 2) % totalEntries;
  }

  loadedCoverPageStart = NO_COVER_PAGE_LOADED;
  loadedCoverItemsPerPage = 0;
  loadedCoverWidth = centerWidth;
  loadedCoverHeight = centerHeight;
  loadedCarouselCenterIndex = centerIndex;
  loadedCarouselSideWidth = sideWidth;
  loadedCarouselSideHeight = sideHeight;
  pendingCoverGenerationCount = 0;
  pendingCoverGenerationNext = 0;
  pendingCoverGenerationNextAt = millis() + COVER_CAROUSEL_GENERATION_INITIAL_DELAY_MS;
  pendingCoverPrefetchCount = 0;
  pendingCoverPrefetchNext = 0;
  pendingVisibleCoverPrefetchCount = 0;
  pendingCoverPrefetchNextAt = millis() + COVER_CAROUSEL_COVER_PREFETCH_INITIAL_DELAY_MS;
  pendingCarouselDetailCount = 0;
  pendingCarouselDetailNext = 0;
  pendingVisibleCarouselDetailCount = 0;
  pendingCarouselDetailNextAt = millis() + COVER_CAROUSEL_DETAIL_PREFETCH_INITIAL_DELAY_MS;
  pendingCarouselSignalCount = 0;
  pendingCarouselSignalNext = 0;
  pendingCarouselSignalNextAt = millis() + COVER_CAROUSEL_SIGNAL_PREFETCH_INITIAL_DELAY_MS;
  pendingCoverSignalCount = 0;
  pendingCoverSignalNext = 0;
  pendingCoverSignalNextAt = 0UL;
  deferCoverBackgroundWork();

  auto previousSignals = std::move(loadedCoverSignals);
  auto previousDetailBitmaps = std::move(loadedCarouselDetailBitmaps);
  for (auto& signal : loadedCoverSignals) signal = CoverBookSignal{};
  for (auto& cover : loadedCoverBitmaps) cover = CachedCoverBitmap{};
  for (auto& detail : loadedCarouselDetailBitmaps) detail = CachedCoverBitmap{};

  int reusedSignals = 0;
  for (int i = 0; i < requestCount && i < static_cast<int>(loadedCoverSignals.size()); ++i) {
    const std::string entry = entryNameAt(static_cast<size_t>(requests[i]));
    auto previousSignal = std::find_if(previousSignals.begin(), previousSignals.end(),
                                       [&entry](const CoverBookSignal& signal) { return signal.entry == entry; });
    if (previousSignal != previousSignals.end()) {
      loadedCoverSignals[static_cast<size_t>(i)] = std::move(*previousSignal);
      ++reusedSignals;
    } else if (pendingCarouselSignalCount < pendingCarouselSignalIndices.size()) {
      pendingCarouselSignalIndices[pendingCarouselSignalCount] = requests[i];
      pendingCarouselSignalSlots[pendingCarouselSignalCount] = static_cast<size_t>(i);
      ++pendingCarouselSignalCount;
    }

  }

  int reusedDetails = 0;
  int reusedAtDifferentSize = 0;
  // The center gets a home-card-class source; all four side cards get a
  // source comfortably larger than their rendered perspective width. Avoid a
  // duplicate base cache so X4 retains enough heap to generate and persist
  // the sharp files.
  const int detailCount = std::min(requestCount, static_cast<int>(loadedCarouselDetailBitmaps.size()));
  for (int i = 0; i < detailCount; ++i) {
    const std::string entry = entryNameAt(static_cast<size_t>(requests[i]));
    const int detailWidth = i == 0 ? COVER_CAROUSEL_CENTER_MAX_WIDTH : COVER_CAROUSEL_SIDE_SOURCE_WIDTH;
    const int detailHeight = i == 0 ? COVER_CAROUSEL_CENTER_MAX_HEIGHT : COVER_CAROUSEL_SIDE_SOURCE_HEIGHT;
    auto previousDetail = std::find_if(
        previousDetailBitmaps.begin(), previousDetailBitmaps.end(),
        [&entry, detailWidth, detailHeight](const CachedCoverBitmap& cover) {
          return cover.matches(entry, detailWidth, detailHeight) && cover.isReady();
        });
    if (previousDetail == previousDetailBitmaps.end()) {
      // Carousel roles change on every move. A side thumbnail remains a valid
      // immediate center placeholder, and the former center is already more
      // than sharp enough for a side card. Match by book before scheduling
      // any SD work so overlapping covers never disappear between presses.
      previousDetail =
          std::find_if(previousDetailBitmaps.begin(), previousDetailBitmaps.end(),
                       [&entry](const CachedCoverBitmap& cover) {
                         return cover.entry == entry && cover.isReady();
                       });
    }
    if (previousDetail != previousDetailBitmaps.end()) {
      const bool sufficientResolution =
          previousDetail->width >= detailWidth && previousDetail->height >= detailHeight;
      loadedCarouselDetailBitmaps[static_cast<size_t>(i)] = std::move(*previousDetail);
      ++reusedDetails;
      if (!sufficientResolution &&
          pendingCarouselDetailCount < pendingCarouselDetailIndices.size()) {
        pendingCarouselDetailIndices[pendingCarouselDetailCount] = requests[i];
        pendingCarouselDetailSlots[pendingCarouselDetailCount] = static_cast<size_t>(i);
        ++pendingCarouselDetailCount;
        ++reusedAtDifferentSize;
      }
    } else if (pendingCarouselDetailCount < pendingCarouselDetailIndices.size()) {
      pendingCarouselDetailIndices[pendingCarouselDetailCount] = requests[i];
      pendingCarouselDetailSlots[pendingCarouselDetailCount] = static_cast<size_t>(i);
      ++pendingCarouselDetailCount;
    }
  }
  pendingVisibleCarouselDetailCount = pendingCarouselDetailCount;

  // Keep one hidden cover ready beyond each visible edge. The same seven-slot
  // budget previously held displaced history, but directional look-ahead is
  // more useful: after hydration, both next and previous have all five covers
  // in RAM before the full carousel composition paints.
  const std::array<int, 2> lookaheadIndices = {
      (centerIndex + 3) % totalEntries,
      (centerIndex + totalEntries - (3 % totalEntries)) % totalEntries,
  };
  size_t lookaheadSlot = static_cast<size_t>(detailCount);
  int readyLookahead = 0;
  int queuedLookahead = 0;
  for (size_t lookahead = 0;
       lookahead < lookaheadIndices.size() && lookaheadSlot < loadedCarouselDetailBitmaps.size();
       ++lookahead) {
    const int index = lookaheadIndices[lookahead];
    if (std::find(requests.begin(), requests.begin() + requestCount, index) != requests.begin() + requestCount ||
        std::find(lookaheadIndices.begin(), lookaheadIndices.begin() + lookahead, index) !=
            lookaheadIndices.begin() + lookahead) {
      continue;
    }

    const std::string entry = entryNameAt(static_cast<size_t>(index));
    auto previousDetail =
        std::find_if(previousDetailBitmaps.begin(), previousDetailBitmaps.end(),
                     [&entry](const CachedCoverBitmap& cover) {
                       return cover.entry == entry && cover.isReady();
                     });
    if (previousDetail != previousDetailBitmaps.end()) {
      const bool sufficientResolution =
          previousDetail->width >= COVER_CAROUSEL_SIDE_SOURCE_WIDTH &&
          previousDetail->height >= COVER_CAROUSEL_SIDE_SOURCE_HEIGHT;
      loadedCarouselDetailBitmaps[lookaheadSlot] = std::move(*previousDetail);
      ++readyLookahead;
      if (!sufficientResolution &&
          pendingCarouselDetailCount < pendingCarouselDetailIndices.size()) {
        pendingCarouselDetailIndices[pendingCarouselDetailCount] = index;
        pendingCarouselDetailSlots[pendingCarouselDetailCount] = lookaheadSlot;
        ++pendingCarouselDetailCount;
        ++queuedLookahead;
      }
    } else if (pendingCarouselDetailCount < pendingCarouselDetailIndices.size()) {
      pendingCarouselDetailIndices[pendingCarouselDetailCount] = index;
      pendingCarouselDetailSlots[pendingCarouselDetailCount] = lookaheadSlot;
      ++pendingCarouselDetailCount;
      ++queuedLookahead;
    }
    ++lookaheadSlot;
  }
  LOG_DBG("FileBrowser",
          "Carousel ready at %d: reused %d signal(s), %d detail(s) (%d awaiting center upgrade), "
          "look-ahead %d ready/%d queued, queued %u detail(s), %u signal(s), %u deferred thumbnail(s)",
          centerIndex, reusedSignals, reusedDetails, reusedAtDifferentSize, readyLookahead, queuedLookahead,
          static_cast<unsigned>(pendingCarouselDetailCount), static_cast<unsigned>(pendingCarouselSignalCount),
          static_cast<unsigned>(pendingCoverGenerationCount));
}

bool FileBrowserActivity::drawCoverArtwork(const std::string& entry, const int x, const int y, const int width,
                                           const int height, const int cornerRadius, const int leftHeight,
                                           const int rightHeight) {
  const bool carousel = isCoverCarouselActive();
  const int sourceWidth = carousel ? COVER_GRID_MAX_COVER_WIDTH : width;
  const int sourceHeight = carousel ? COVER_GRID_MAX_COVER_HEIGHT : height;
  const int perspectiveLeftHeight = leftHeight > 0 ? leftHeight : height;
  const int perspectiveRightHeight = rightHeight > 0 ? rightHeight : height;
  const bool perspectiveSide = carousel &&
                               (perspectiveLeftHeight != height || perspectiveRightHeight != height ||
                                perspectiveLeftHeight != perspectiveRightHeight);
  if (carousel) {
    // The selected cover and its immediate neighbors are already prefetched
    // at center quality. Use that sharper source for the visible side cards
    // too; the farthest pair remains intentionally lightweight.
    if (const CachedCoverBitmap* detailCover = carouselDetailBitmapForEntry(entry)) {
      if (perspectiveSide) {
        renderer.fillRect(x, y, width, height, false);
        renderer.drawPerspectiveBitmap1Bit(detailCover->rows.get(), detailCover->width, detailCover->height,
                                           detailCover->rowBytes, detailCover->topDown,
                                           detailCover->blackPaletteIndex, x, y, width,
                                           perspectiveLeftHeight, perspectiveRightHeight);
        drawPerspectiveCoverOutline(renderer, x, y, width, perspectiveLeftHeight, perspectiveRightHeight);
        return true;
      }
      beginCoverArtwork(renderer, x, y, width, height, cornerRadius);
      renderer.drawPerspectiveBitmap1Bit(detailCover->rows.get(), detailCover->width, detailCover->height,
                                         detailCover->rowBytes, detailCover->topDown,
                                         detailCover->blackPaletteIndex, x, y, width, height, height);
      finishCoverArtwork(renderer, x, y, width, height, cornerRadius);
      return true;
    }
    if (const CachedCoverBitmap* cachedCover = coverBitmapForEntry(entry, sourceWidth, sourceHeight)) {
      if (perspectiveSide) {
        renderer.fillRect(x, y, width, height, false);
        renderer.drawPerspectiveBitmap1Bit(cachedCover->rows.get(), cachedCover->width, cachedCover->height,
                                           cachedCover->rowBytes, cachedCover->topDown,
                                           cachedCover->blackPaletteIndex, x, y, width,
                                           perspectiveLeftHeight, perspectiveRightHeight);
        drawPerspectiveCoverOutline(renderer, x, y, width, perspectiveLeftHeight, perspectiveRightHeight);
        return true;
      }
      beginCoverArtwork(renderer, x, y, width, height, cornerRadius);
      renderer.drawPerspectiveBitmap1Bit(cachedCover->rows.get(), cachedCover->width, cachedCover->height,
                                         cachedCover->rowBytes, cachedCover->topDown,
                                         cachedCover->blackPaletteIndex, x, y, width, height, height);
      finishCoverArtwork(renderer, x, y, width, height, cornerRadius);
      return true;
    }
  }

  if (perspectiveSide) {
    renderer.fillRect(x, y, width, height, false);
    for (int dx = 0; dx < width; ++dx) {
      const int columnHeight = width <= 1
                                   ? perspectiveLeftHeight
                                   : perspectiveLeftHeight +
                                         (perspectiveRightHeight - perspectiveLeftHeight) * dx / (width - 1);
      const int columnY = y + (height - columnHeight) / 2;
      renderer.fillRect(x + dx, columnY, 1, columnHeight, true);
    }
    return false;
  }

  beginCoverArtwork(renderer, x, y, width, height, cornerRadius);
  finishCoverArtwork(renderer, x, y, width, height, cornerRadius);
  const uint8_t* placeholderIcon = !entry.empty() && entry.back() == '/' ? FolderIcon : BookIcon;
  constexpr int placeholderSize = 32;
  renderer.drawIcon(placeholderIcon, x + (width - placeholderSize) / 2, y + (height - placeholderSize) / 2,
                    placeholderSize);
  return false;
}

void FileBrowserActivity::drawCoverReadingSignal(const std::string& entry, const int x, const int y, const int width,
                                                 const int height) {
  const CoverBookSignal* signal = coverSignalForEntry(entry);
  if (!signal || signal->status == BookStatus::Unread) return;

  char badge[12];
  if (signal->status == BookStatus::Finished) {
    snprintf(badge, sizeof(badge), "DONE");
  } else if (signal->progressPercent >= 0) {
    snprintf(badge, sizeof(badge), "%d%%", signal->progressPercent);
  } else {
    snprintf(badge, sizeof(badge), "READ");
  }
  const int badgeHeight = renderer.getLineHeight(SMALL_FONT_ID) + 4;
  const int badgeWidth = renderer.getTextWidth(SMALL_FONT_ID, badge, EpdFontFamily::BOLD) + 10;
  const int badgeX = x + width - badgeWidth - 5;
  const int badgeY = y + 5;
  renderer.fillRoundedRect(badgeX, badgeY, badgeWidth, badgeHeight, 3, Color::Black);
  renderer.drawText(SMALL_FONT_ID, badgeX + 5, badgeY + 2, badge, false, EpdFontFamily::BOLD);

  if (signal->progressPercent > 0) {
    const int barX = x + 6;
    const int barY = y + height - 10;
    const int barWidth = width - 12;
    renderer.fillRect(barX, barY, barWidth, 6, false);
    renderer.drawRect(barX, barY, barWidth, 6, true);
    const int filled = std::max(1, ((barWidth - 2) * signal->progressPercent) / 100);
    renderer.fillRect(barX + 1, barY + 1, filled, 4, true);
  }
}

void FileBrowserActivity::resetTitleMarquee() {
  titleMarqueeStep.store(0, std::memory_order_relaxed);
  titleMarqueeEndStep.store(0, std::memory_order_relaxed);
  titleMarqueeEntryHash.store(0, std::memory_order_relaxed);
  titleMarqueeNextStepAt.store(0UL, std::memory_order_relaxed);
  titleMarqueeLoops.store(0, std::memory_order_relaxed);
}

void FileBrowserActivity::updateTitleMarquee(const std::string& entry) {
  const bool compactTwoLineRows =
      !usingIndex && SETTINGS.fileBrowserDisplay == CrossPointSettings::FILE_BROWSER_DISPLAY_2_LINES;
  if (mode != Mode::Books || entry.empty() || entry.back() == '/' || compactTwoLineRows || pendingCompletedFeedback) {
    if (titleMarqueeEntryHash.load(std::memory_order_relaxed) != 0) resetTitleMarquee();
    return;
  }

  const uint32_t entryHash = hashTitleMarqueeEntry(basepath, entry);
  if (entryHash != titleMarqueeEntryHash.load(std::memory_order_acquire)) return;
  const size_t endStep = titleMarqueeEndStep.load(std::memory_order_relaxed);
  if (endStep == 0 || endStep == TITLE_MARQUEE_DISABLED) return;

  const unsigned long now = millis();
  if (!deadlineReached(now, titleMarqueeNextStepAt.load(std::memory_order_relaxed))) return;

  const auto requestMarqueeUpdate = [this]() {
    if (isCoverGridActive()) {
      requestFastCoverUpdate(CoverRefresh::GridTitle);
    } else if (isCoverCarouselActive()) {
      requestFastCoverUpdate(CoverRefresh::CarouselTitle);
    } else {
      requestUpdate();
    }
  };

  const size_t currentStep = titleMarqueeStep.load(std::memory_order_relaxed);
  if (currentStep >= endStep) {
    const uint8_t loops = titleMarqueeLoops.load(std::memory_order_relaxed) + 1;
    titleMarqueeLoops.store(loops, std::memory_order_relaxed);
    titleMarqueeStep.store(0, std::memory_order_relaxed);
    if (loops >= TITLE_MARQUEE_MAX_LOOPS) {
      // Park showing the title head; endStep DISABLED stops further steps (and
      // their repaint/refresh cycles) until the cursor moves to another entry.
      titleMarqueeEndStep.store(TITLE_MARQUEE_DISABLED, std::memory_order_relaxed);
      requestMarqueeUpdate();
      return;
    }
    titleMarqueeNextStepAt.store(now + TITLE_MARQUEE_INITIAL_PAUSE_MS, std::memory_order_relaxed);
    requestMarqueeUpdate();
    return;
  }

  const size_t nextStep = currentStep + 1;
  titleMarqueeStep.store(nextStep, std::memory_order_relaxed);
  titleMarqueeNextStepAt.store(now + (nextStep >= endStep ? TITLE_MARQUEE_END_PAUSE_MS : TITLE_MARQUEE_STEP_MS),
                               std::memory_order_relaxed);
  requestMarqueeUpdate();
}

void FileBrowserActivity::measureTitleMarquee(const std::string& entry, int maxWidth, const int maxLines) {
  if (entry.empty() || entry.back() == '/') {
    return;
  }
  const uint32_t entryHash = hashTitleMarqueeEntry(basepath, entry);
  if (entryHash == titleMarqueeEntryHash.load(std::memory_order_acquire)) return;

  const bool coverGrid = isCoverGridActive();
  const bool coverCarousel = isCoverCarouselActive();
  const bool coverDisplay = coverGrid || coverCarousel;
  const std::string title = coverDisplay ? getCoverTitle(entry) : getFileName(entry);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto theme = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
  const bool roundedRaff = theme == CrossPointSettings::UI_THEME::ROUNDEDRAFF;
  const int titleFont =
      coverCarousel ? UI_12_FONT_ID : (coverGrid ? UI_10_FONT_ID : (roundedRaff ? UI_12_FONT_ID : UI_10_FONT_ID));
  const auto titleStyle = coverCarousel ? EpdFontFamily::BOLD
                                        : (coverGrid ? EpdFontFamily::BOLD
                                                     : (roundedRaff ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR));

  // This follows the shared and RoundedRaff list geometry while staying
  // conservative for icon-bearing themes.
  if (maxWidth <= 0) {
    maxWidth = renderer.getScreenWidth() - metrics.contentSidePadding * 2 - 20;
    if (UITheme::getInstance().getTheme().showsFileIcons()) {
      maxWidth -= renderer.getLineHeight(UI_10_FONT_ID) + 12;
    }
    const std::string value = rowValueForEntry(entry);
    if (!value.empty()) {
      maxWidth -= renderer.getTextWidth(titleFont, value.c_str(), EpdFontFamily::REGULAR) + 15;
    }
  }
  maxWidth = std::max(40, maxWidth);
  const int fitWidth = maxWidth * std::max(1, maxLines);

  size_t endStep = TITLE_MARQUEE_DISABLED;
  if (renderer.getTextWidth(titleFont, title.c_str(), titleStyle) > fitWidth) {
    size_t candidate = nextUtf8Offset(title, 0);
    size_t candidateStep = 1;
    while (candidate < title.size()) {
      if (renderer.getTextWidth(titleFont, title.c_str() + candidate, titleStyle) <= fitWidth) {
        endStep = candidateStep;
        break;
      }
      candidate = nextUtf8Offset(title, candidate);
      ++candidateStep;
    }
  }

  titleMarqueeStep.store(0, std::memory_order_relaxed);
  titleMarqueeEndStep.store(endStep, std::memory_order_relaxed);
  titleMarqueeLoops.store(0, std::memory_order_relaxed);
  titleMarqueeNextStepAt.store(millis() + TITLE_MARQUEE_INITIAL_PAUSE_MS, std::memory_order_relaxed);
  titleMarqueeEntryHash.store(entryHash, std::memory_order_release);
}

void FileBrowserActivity::render(RenderLock&&) {
  if (folderTransitionInProgress.load(std::memory_order_acquire)) return;
  if (pickerFirstRenderMs == 0) pickerFirstRenderMs = millis();

  const CoverRefresh requestedCoverRefresh = static_cast<CoverRefresh>(
      pendingCoverRefresh.exchange(static_cast<uint8_t>(CoverRefresh::None), std::memory_order_acq_rel));
  const uint32_t requestedRefreshEpoch = pendingCoverRefreshEpoch.exchange(
      coverNavigationEpoch.load(std::memory_order_acquire), std::memory_order_acq_rel);
  // A full render can sit behind an e-ink BUSY wait. If the user moved again
  // before it began, drop it and let the latest partial cursor update win.
  if (requestedCoverRefresh == CoverRefresh::Full && (isCoverGridActive() || isCoverCarouselActive()) &&
      requestedRefreshEpoch != coverNavigationEpoch.load(std::memory_order_acquire)) {
    return;
  }
  if (requestedCoverRefresh == CoverRefresh::GridSelection) {
    if (moveCoverGridSelectionFromSnapshot()) return;
    if (refreshCoverGridFocus()) return;
  }
  if (requestedCoverRefresh == CoverRefresh::GridFocus && refreshCoverGridFocus()) return;
  if (requestedCoverRefresh == CoverRefresh::GridTitle && refreshCoverGridSelectionFromSnapshot()) return;
  if (requestedCoverRefresh == CoverRefresh::CarouselTitle && refreshCoverCarouselTitle()) return;

  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  std::string folderName =
      (mode == Mode::PickFirmware)
          ? std::string(tr(STR_SELECT_FIRMWARE_FILE))
          : ((basepath == "/") ? std::string(tr(STR_SD_CARD)) : basepath.substr(basepath.rfind('/') + 1));
  CompactHeader::drawTitle(renderer, folderName.c_str());

  const int pathLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int pathReserved = pathLineHeight + metrics.verticalSpacing;
  const int pathY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - pathLineHeight;
  const int pathMaxWidth = pageWidth - metrics.contentSidePadding * 2;
  const int contentTop = CompactHeader::contentTop(metrics);
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - pathReserved;
  const size_t visibleEntries = entryCount();
  const bool coverGrid = isCoverGridActive();
  const bool coverCarousel = isCoverCarouselActive();
  const bool coverDisplay = coverGrid || coverCarousel;
  CoverGridLayout coverLayout;
  CoverCarouselLayout carouselLayout;
  int coverPageStart = NO_COVER_PAGE_LOADED;
  if (visibleEntries == 0) {
    const char* emptyMsg =
        fileListMemoryLimited
            ? tr(STR_MEMORY_ERROR)
            : ((mode == Mode::PickFirmware) ? tr(STR_NO_BIN_FILES)
                                            : (bookStatusFilter != BookStatusFilter::All ? tr(STR_NO_BOOKS_MATCH_FILTER)
                                                                                         : tr(STR_NO_FILES_FOUND)));
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, emptyMsg);
  } else {
    const bool compactFileRows =
        !usingIndex && SETTINGS.fileBrowserDisplay == CrossPointSettings::FILE_BROWSER_DISPLAY_2_LINES;
    std::function<std::string(int)> compactRowMarker;
    if (compactFileRows) {
      compactRowMarker = [this](int index) {
        const std::string entry = entryNameAt(index);
        return !entry.empty() && entry.back() == '/' ? "folder" : "";
      };
    }
    const auto rowTitle = [this, coverDisplay](int index) {
      const std::string entry = entryNameAt(index);
      std::string title = coverDisplay ? getCoverTitle(entry) : getFileName(entry);
      if (static_cast<size_t>(index) == selectorIndex) {
        size_t offset = 0;
        size_t steps = titleMarqueeStep.load(std::memory_order_relaxed);
        while (steps-- > 0 && offset < title.size()) {
          offset = nextUtf8Offset(title, offset);
        }
        if (offset > 0 && offset < title.size()) {
          title.erase(0, offset);
        }
      }
      return title;
    };
    const auto rowIcon = [this](int index) {
      const std::string entry = entryNameAt(index);
      return UITheme::getFileIcon(entry);
    };
    const auto rowValue = [this](int index) {
      const std::string entry = entryNameAt(index);
      return rowValueForEntry(entry);
    };

    if (coverGrid) {
      coverLayout = calculateCoverGridLayout(renderer, contentTop, contentHeight, metrics.contentSidePadding,
                                             metrics.verticalSpacing);
      coverPageStart =
          static_cast<int>(selectorIndex / static_cast<size_t>(coverLayout.itemsPerPage)) * coverLayout.itemsPerPage;
      const int pageEnd = std::min(coverPageStart + coverLayout.itemsPerPage, static_cast<int>(visibleEntries));
      const int pageItemCount = pageEnd - coverPageStart;
      if (!pendingCompletedFeedback && selectorIndex < visibleEntries) {
        measureTitleMarquee(entryNameAt(selectorIndex), coverLayout.footerWidth, 1);
      }

      for (int bookIndex = coverPageStart; bookIndex < pageEnd; ++bookIndex) {
        if (folderTransitionInProgress.load(std::memory_order_acquire)) return;
        CoverGridCellGeometry geometry;
        if (!calculateCoverGridCellGeometry(coverLayout, coverPageStart, static_cast<size_t>(bookIndex),
                                            pageItemCount, geometry)) {
          continue;
        }
        const int coverX = geometry.coverX;
        const int coverY = geometry.coverY;

        const std::string entry = entryNameAt(static_cast<size_t>(bookIndex));
        bool coverDrawn = false;
        if (const CachedCoverBitmap* cachedCover =
                coverBitmapForEntry(entry, coverLayout.coverWidth, coverLayout.coverHeight)) {
          renderer.fillRoundedRect(coverX, coverY, coverLayout.coverWidth, coverLayout.coverHeight,
                                   COVER_GRID_CORNER_RADIUS, Color::White);
          renderer.drawPerspectiveBitmap1Bit(cachedCover->rows.get(), cachedCover->width, cachedCover->height,
                                             cachedCover->rowBytes, cachedCover->topDown,
                                             cachedCover->blackPaletteIndex, coverX, coverY, coverLayout.coverWidth,
                                             coverLayout.coverHeight, coverLayout.coverHeight);
          renderer.maskRoundedRectOutsideCorners(coverX, coverY, coverLayout.coverWidth, coverLayout.coverHeight,
                                                 COVER_GRID_CORNER_RADIUS, Color::White);
          renderer.drawRoundedRect(coverX, coverY, coverLayout.coverWidth, coverLayout.coverHeight, 1,
                                   COVER_GRID_CORNER_RADIUS, true);
          coverDrawn = true;
        }

        if (!coverDrawn) {
          renderer.fillRoundedRect(coverX, coverY, coverLayout.coverWidth, coverLayout.coverHeight,
                                   COVER_GRID_CORNER_RADIUS, Color::White);
          renderer.drawRoundedRect(coverX, coverY, coverLayout.coverWidth, coverLayout.coverHeight, 1,
                                   COVER_GRID_CORNER_RADIUS, true);
          const std::string placeholderTitle = getCoverTitle(entry);
          const int maxPlaceholderLines = coverLayout.columns >= 3 ? 5 : 12;
          const auto placeholderLines = renderer.wrappedText(
              SMALL_FONT_ID, placeholderTitle.c_str(), std::max(10, coverLayout.coverWidth - 12),
              maxPlaceholderLines, EpdFontFamily::BOLD);
          const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
          int placeholderY = coverY +
                             std::max(0, (coverLayout.coverHeight -
                                          lineHeight * static_cast<int>(placeholderLines.size())) /
                                             2);
          for (const auto& line : placeholderLines) {
            const int lineWidth = renderer.getTextWidth(SMALL_FONT_ID, line.c_str(), EpdFontFamily::BOLD);
            renderer.drawText(SMALL_FONT_ID, coverX + (coverLayout.coverWidth - lineWidth) / 2, placeholderY,
                              line.c_str(), true, EpdFontFamily::BOLD);
            placeholderY += lineHeight;
          }
        }

        const CoverBookSignal* signal = coverSignalForEntry(entry);
        int progressPercent = signal ? signal->progressPercent : -1;
        if (signal && signal->status != BookStatus::Unread) {
          char badge[12];
          if (signal->status == BookStatus::Finished) {
            snprintf(badge, sizeof(badge), "DONE");
            progressPercent = 100;
          } else if (signal->progressPercent >= 0) {
            snprintf(badge, sizeof(badge), "%d%%", signal->progressPercent);
          } else {
            snprintf(badge, sizeof(badge), "READ");
          }
          const int badgeHeight = renderer.getLineHeight(SMALL_FONT_ID) + 4;
          const int badgeWidth = renderer.getTextWidth(SMALL_FONT_ID, badge, EpdFontFamily::BOLD) + 10;
          const int badgeX = coverX + coverLayout.coverWidth - badgeWidth - 5;
          const int badgeY = coverY + 5;
          renderer.fillRoundedRect(badgeX, badgeY, badgeWidth, badgeHeight, 3, Color::Black);
          renderer.drawText(SMALL_FONT_ID, badgeX + 5, badgeY + 2, badge, false, EpdFontFamily::BOLD);
        }

        const int progressX = coverX + 2;
        const int progressWidth = std::max(1, coverLayout.coverWidth - 4);
        const int progressY = coverY + coverLayout.coverHeight - coverLayout.progressBarHeight - 1;
        renderer.fillRectDither(progressX, progressY, progressWidth, coverLayout.progressBarHeight,
                                Color::LightGray);
        if (progressPercent > 0) {
          const int filled = std::max(
              1, progressWidth * std::clamp(progressPercent, 0, 100) / 100);
          renderer.fillRect(progressX, progressY, filled, coverLayout.progressBarHeight, true);
        }
      }

      CoverGridCellGeometry selectionGeometry;
      if (calculateCoverGridCellGeometry(coverLayout, coverPageStart, selectorIndex, pageItemCount,
                                         selectionGeometry)) {
        captureCoverGridSelectionBackground(selectionGeometry.snapshotX, selectionGeometry.snapshotY,
                                            selectionGeometry.snapshotWidth, selectionGeometry.snapshotHeight,
                                            selectorIndex, coverPageStart);
        captureCoverGridFooterBackground(coverLayout.footerX, coverLayout.footerY, coverLayout.footerWidth,
                                         coverLayout.footerHeight, coverPageStart);
        drawCoverGridSelection(renderer, coverLayout, selectionGeometry);
        paintedCoverGridSelection.store(selectorIndex, std::memory_order_release);
        paintedCoverGridPageStart.store(coverPageStart, std::memory_order_release);
        const std::string selectedEntry = entryNameAt(selectorIndex);
        const CoverBookSignal* signal = coverSignalForEntry(selectedEntry);
        const char* status = tr(STR_FILTER_UNREAD);
        int progressPercent = -1;
        if (signal) {
          progressPercent = signal->progressPercent;
          if (signal->status == BookStatus::Finished) {
            status = tr(STR_FILTER_FINISHED);
            progressPercent = 100;
          } else if (signal->status == BookStatus::Reading) {
            status = tr(STR_FILTER_READING);
          }
        }
        drawCoverGridFooter(renderer, selectedEntry, coverLayout, status, progressPercent,
                            titleMarqueeStep.load(std::memory_order_relaxed));
      }
    } else if (coverCarousel) {
      carouselLayout = calculateCoverCarouselLayout(renderer, contentTop, contentHeight, metrics.contentSidePadding);
      const int centerIndex = static_cast<int>(selectorIndex);
      const int bookCount = static_cast<int>(visibleEntries);
      const std::string centerEntry = entryNameAt(selectorIndex);

      if (!pendingCompletedFeedback) {
        measureTitleMarquee(centerEntry, carouselLayout.textWidth, COVER_CAROUSEL_TITLE_LINES);
      }

      const auto drawCarouselCover = [this](const int index, const int x, const int y, const int width,
                                            const int leftHeight, const int rightHeight,
                                            const bool drawSignal) {
        const std::string entry = entryNameAt(static_cast<size_t>(index));
        const int height = std::max(leftHeight, rightHeight);
        drawCoverArtwork(entry, x, y, width, height, COVER_CAROUSEL_CORNER_RADIUS, leftHeight, rightHeight);
        if (drawSignal) drawCoverReadingSignal(entry, x, y, width, height);
      };

      if (bookCount >= 5) {
        const int leftFarIndex = (centerIndex + bookCount - 2) % bookCount;
        drawCarouselCover(leftFarIndex, carouselLayout.leftFarX, carouselLayout.farY, carouselLayout.farWidth,
                          carouselLayout.farInnerHeight, carouselLayout.farOuterHeight, false);
      }
      if (bookCount >= 4) {
        const int rightFarIndex = (centerIndex + 2) % bookCount;
        drawCarouselCover(rightFarIndex, carouselLayout.rightFarX, carouselLayout.farY, carouselLayout.farWidth,
                          carouselLayout.farOuterHeight, carouselLayout.farInnerHeight, false);
      }
      if (bookCount >= 2) {
        const int leftNearIndex = (centerIndex + bookCount - 1) % bookCount;
        drawCarouselCover(leftNearIndex, carouselLayout.leftNearX, carouselLayout.nearY,
                          carouselLayout.nearWidth, carouselLayout.nearInnerHeight,
                          carouselLayout.nearOuterHeight, false);
      }
      if (bookCount >= 3) {
        const int rightNearIndex = (centerIndex + 1) % bookCount;
        drawCarouselCover(rightNearIndex, carouselLayout.rightNearX, carouselLayout.nearY,
                          carouselLayout.nearWidth, carouselLayout.nearOuterHeight,
                          carouselLayout.nearInnerHeight, false);
      }

      drawCarouselCover(centerIndex, carouselLayout.centerX, carouselLayout.centerY, carouselLayout.centerWidth,
                        carouselLayout.centerHeight, carouselLayout.centerHeight, true);
      paintedCarouselCenterIndex.store(centerIndex, std::memory_order_release);
      const int outerInset = COVER_CAROUSEL_SELECTION_PADDING + 3;
      renderer.drawRoundedRect(carouselLayout.centerX - COVER_CAROUSEL_SELECTION_PADDING,
                               carouselLayout.centerY - COVER_CAROUSEL_SELECTION_PADDING,
                               carouselLayout.centerWidth + COVER_CAROUSEL_SELECTION_PADDING * 2,
                               carouselLayout.centerHeight + COVER_CAROUSEL_SELECTION_PADDING * 2, 3,
                               COVER_CAROUSEL_CORNER_RADIUS + COVER_CAROUSEL_SELECTION_PADDING, true);
      renderer.drawRoundedRect(carouselLayout.centerX - outerInset, carouselLayout.centerY - outerInset,
                               carouselLayout.centerWidth + outerInset * 2,
                               carouselLayout.centerHeight + outerInset * 2, 1,
                               COVER_CAROUSEL_CORNER_RADIUS + outerInset, true);

      const auto titleLines = marqueeTitleLines(
          renderer, UI_12_FONT_ID, getCoverTitle(centerEntry), carouselLayout.textWidth, COVER_CAROUSEL_TITLE_LINES,
          EpdFontFamily::BOLD, titleMarqueeStep.load(std::memory_order_relaxed));
      int titleY = carouselLayout.titleY;
      for (const auto& line : titleLines) {
        const int lineWidth = renderer.getTextWidth(UI_12_FONT_ID, line.c_str(), EpdFontFamily::BOLD);
        renderer.drawText(UI_12_FONT_ID, (pageWidth - lineWidth) / 2, titleY, line.c_str(), true, EpdFontFamily::BOLD);
        titleY += carouselLayout.titleLineHeight;
      }

      const std::string coverAuthor = getCoverAuthor(centerEntry);
      const std::string author =
          renderer.truncatedText(UI_10_FONT_ID, coverAuthor.c_str(), carouselLayout.textWidth, EpdFontFamily::REGULAR);
      if (!author.empty()) {
        const int authorWidth = renderer.getTextWidth(UI_10_FONT_ID, author.c_str(), EpdFontFamily::REGULAR);
        renderer.drawText(UI_10_FONT_ID, (pageWidth - authorWidth) / 2, carouselLayout.authorY, author.c_str(), true,
                          EpdFontFamily::REGULAR);
      }

      const CoverBookSignal* signal = coverSignalForEntry(centerEntry);
      const char* status = tr(STR_FILTER_UNREAD);
      int progressPercent = -1;
      if (signal) {
        progressPercent = signal->progressPercent;
        if (signal->status == BookStatus::Finished) {
          status = tr(STR_FILTER_FINISHED);
          progressPercent = 100;
        } else if (signal->status == BookStatus::Reading) {
          status = tr(STR_FILTER_READING);
        }
      }
      char counter[48];
      if (progressPercent >= 0) {
        snprintf(counter, sizeof(counter), "%d of %d  |  %s %d%%", centerIndex + 1, bookCount, status, progressPercent);
      } else {
        snprintf(counter, sizeof(counter), "%d of %d  |  %s", centerIndex + 1, bookCount, status);
      }
      const auto counterText =
          renderer.truncatedText(SMALL_FONT_ID, counter, carouselLayout.textWidth, EpdFontFamily::REGULAR);
      const int counterWidth = renderer.getTextWidth(SMALL_FONT_ID, counterText.c_str(), EpdFontFamily::REGULAR);
      renderer.drawText(SMALL_FONT_ID, (pageWidth - counterWidth) / 2, carouselLayout.counterY, counterText.c_str(),
                        true, EpdFontFamily::REGULAR);

      if (progressPercent > 0) {
        const int progressWidth = carouselLayout.centerWidth;
        const int progressX = (pageWidth - progressWidth) / 2;
        renderer.drawRect(progressX, carouselLayout.progressY, progressWidth, 6, true);
        const int filled = std::max(1, ((progressWidth - 2) * std::clamp(progressPercent, 0, 100)) / 100);
        renderer.fillRect(progressX + 1, carouselLayout.progressY + 1, filled, 4, true);
      }
    } else {
      if (!compactFileRows && mode == Mode::Books && !pendingCompletedFeedback && selectorIndex < visibleEntries) {
        measureTitleMarquee(entryNameAt(selectorIndex));
      }
      const Rect listRect{0, contentTop, pageWidth, contentHeight};
      if (compactFileRows) {
        MinimalTheme::drawCompactFileBrowserList(renderer, listRect, static_cast<int>(visibleEntries), selectorIndex,
                                                 rowTitle, compactRowMarker, rowIcon, rowValue);
      } else {
        GUI.drawList(renderer, listRect, static_cast<int>(visibleEntries), selectorIndex, rowTitle, compactRowMarker,
                     rowIcon, rowValue, false);
      }
    }
  }

  // Full path display
  {
    const int separatorY = pathY - metrics.verticalSpacing / 2;
    renderer.drawLine(0, separatorY, pageWidth - 1, separatorY, 3, true);
    char gridPageStatus[64] = {};
    int gridPageStatusWidth = 0;
    if (coverGrid && visibleEntries > 0 && coverLayout.itemsPerPage > 0) {
      const int pageCount =
          (static_cast<int>(visibleEntries) + coverLayout.itemsPerPage - 1) / coverLayout.itemsPerPage;
      const int currentPage = coverPageStart / coverLayout.itemsPerPage + 1;
      snprintf(gridPageStatus, sizeof(gridPageStatus), "Page %d of %d | %u %s", currentPage, pageCount,
               static_cast<unsigned>(visibleEntries), visibleEntries == 1 ? "book" : "books");
      gridPageStatusWidth = renderer.getTextWidth(SMALL_FONT_ID, gridPageStatus);
    }

    const int availablePathWidth =
        std::max(20, pathMaxWidth - (gridPageStatusWidth > 0 ? gridPageStatusWidth + ROOT_HINT_GAP : 0));
    // Left-truncate so the deepest directory is always visible
    const char* pathStr = basepath.c_str();
    const char* pathDisplay = pathStr;
    char leftTruncBuf[256];
    if (renderer.getTextWidth(SMALL_FONT_ID, pathStr) > availablePathWidth) {
      const char ellipsis[] = "\xe2\x80\xa6";  // UTF-8 ellipsis (…)
      const int ellipsisWidth = renderer.getTextWidth(SMALL_FONT_ID, ellipsis);
      const int available = availablePathWidth - ellipsisWidth;
      // Walk forward from the start until the suffix fits, skipping UTF-8 continuation bytes
      const char* p = pathStr;
      while (*p) {
        if (renderer.getTextWidth(SMALL_FONT_ID, p) <= available) break;
        ++p;
        while (*p && (static_cast<unsigned char>(*p) & 0xC0) == 0x80) ++p;
      }
      snprintf(leftTruncBuf, sizeof(leftTruncBuf), "%s%s", ellipsis, p);
      pathDisplay = leftTruncBuf;
    }
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, pathY, pathDisplay);
    if (gridPageStatusWidth > 0) {
      renderer.drawText(SMALL_FONT_ID, pageWidth - metrics.contentSidePadding - gridPageStatusWidth, pathY,
                        gridPageStatus);
    }
  }

  // Help text
  const char* backLabel = (basepath == "/") ? (mode == Mode::PickFirmware ? tr(STR_BACK) : tr(STR_HOME)) : tr(STR_BACK);
  // In PickFirmware mode, Confirm on a .bin returns the path to the caller (not "open"); show
  // STR_SELECT instead. Directories in the same picker still descend, so keep STR_OPEN there.
  const bool selectingFirmwareFile =
      mode == Mode::PickFirmware && visibleEntries > 0 && std::string(entryNameAt(selectorIndex)).back() != '/';
  const char* confirmLabel =
      visibleEntries == 0
          ? ((mode == Mode::Books && bookStatusFilter != BookStatusFilter::All) ? tr(STR_FILTER_BOOKS) : "")
          : (selectingFirmwareFile ? tr(STR_SELECT) : tr(STR_OPEN));
  const char* previousLabel = visibleEntries == 0 ? "" : (coverDisplay ? tr(STR_DIR_LEFT) : tr(STR_DIR_UP));
  const char* nextLabel = visibleEntries == 0 ? "" : (coverDisplay ? tr(STR_DIR_RIGHT) : tr(STR_DIR_DOWN));
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, previousLabel, nextLabel);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (mode == Mode::Books && basepath == "/" && !coverGrid) {
    const int usedPathWidth = renderer.getTextWidth(SMALL_FONT_ID, basepath.c_str());
    const int hintMaxWidth = pathMaxWidth - usedPathWidth - ROOT_HINT_GAP;
    const auto hint = renderer.truncatedText(SMALL_FONT_ID, tr(STR_TOGGLE_HIDDEN_FILES_HINT), hintMaxWidth);
    const int hintWidth = renderer.getTextWidth(SMALL_FONT_ID, hint.c_str());
    renderer.drawText(SMALL_FONT_ID, pageWidth - metrics.contentSidePadding - hintWidth, pathY, hint.c_str());
  }

  if (pendingCompletedFeedback) {
    GUI.drawPopup(renderer, completedFeedbackIsFinished ? tr(STR_MARKED_FINISHED) : tr(STR_MARKED_UNFINISHED));
  }

  // Keep the controller powered while actively browsing covers. This makes
  // repeated X3 window updates stay differential even if Sunlight Fading Fix
  // is enabled for long reading sessions.
  // Back/folder input may arrive while this frame is being composed. Do not
  // spend an e-ink cycle painting a shelf the user has already left.
  if (folderTransitionInProgress.load(std::memory_order_acquire)) return;
  renderer.displayBuffer(HalDisplay::FAST_REFRESH, false, !coverDisplay);
}

size_t FileBrowserActivity::findEntry(const std::string& name) {
  if (usingIndex && fileIndex) {
    std::string raw = name;
    if (!raw.empty() && raw.back() == '/') raw.pop_back();
    const size_t row = fileIndex->findRowByName(raw.c_str());
    return row == SIZE_MAX ? 0 : row;
  }

  for (size_t i = 0; i < files.size(); i++)
    if (files[i] == name) return i;
  return 0;
}
