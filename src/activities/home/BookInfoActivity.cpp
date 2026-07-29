#include "BookInfoActivity.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Xtc.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <new>
#include <vector>

#include "RecentBookProgress.h"
#include "RecentBooksStore.h"
#include "activities/reader/BookReadingStats.h"
#include "activities/reader/LibraryInsights.h"
#include "components/CompactHeader.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int kCoverWidth = 120;
constexpr int kCoverHeight = 180;
constexpr int kCoverCornerRadius = 4;

bool statsExist(const std::string& cachePath) {
  return Storage.existsForRead(cachePath + "/stats_v7.bin") ||
         Storage.existsForRead(cachePath + "/stats_v6.bin") ||
         Storage.existsForRead(cachePath + "/stats_v5.bin") ||
         Storage.existsForRead(cachePath + "/stats_v4.bin") ||
         Storage.existsForRead(cachePath + "/stats_v3.bin") ||
         Storage.existsForRead(cachePath + "/stats_v2.bin") ||
         Storage.existsForRead(cachePath + "/stats_v1.bin") || Storage.existsForRead(cachePath + "/stats.bin");
}

bool progressExists(const std::string& cachePath) {
  return Storage.existsForRead(cachePath + "/progress.bin") ||
         Storage.existsForRead(cachePath + "/progress.bin.bak");
}

void drawLines(GfxRenderer& renderer, const int fontId, const std::vector<std::string>& lines, const int x, int& y,
               const int gap = 0, const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  const int lineHeight = renderer.getLineHeight(fontId);
  for (const auto& line : lines) {
    renderer.drawText(fontId, x, y, line.c_str(), true, style);
    y += lineHeight + gap;
  }
}
}  // namespace

void BookInfoActivity::onEnter() {
  Activity::onEnter();
  detailsLoaded = false;
  coverLoadAttempted = false;
  cachedCover = CachedCoverBitmap{};
  info.title = fallbackTitle;
  const auto& recentBooks = RECENT_BOOKS.getBooks();
  const auto recent = std::find_if(recentBooks.begin(), recentBooks.end(), [this](const RecentBook& candidate) {
    return candidate.path == bookPath;
  });
  if (recent != recentBooks.end()) {
    if (!recent->title.empty()) info.title = recent->title;
    info.author = recent->author;
    if (thumbnailPath.empty()) thumbnailPath = recent->coverBmpPath;
  }

  if (FsHelpers::hasEpubExtension(bookPath)) {
    Epub epub(bookPath, DUET_BOOKS_ROOT_PATH "");
    cachePath = epub.getCachePath();
  } else if (FsHelpers::hasXtcExtension(bookPath)) {
    Xtc xtc(bookPath, DUET_BOOKS_ROOT_PATH "");
    cachePath = xtc.getCachePath();
  }
  selectCachedThumbnail();
  statusText = tr(STR_LOADING);
  detailsLoadAt = millis() + 60UL;
  requestUpdate();
}

void BookInfoActivity::loadCachedCover() {
  coverLoadAttempted = true;
  cachedCover = CachedCoverBitmap{};
  if (thumbnailPath.empty()) return;

  FsFile file;
  if (!Storage.openFileForRead("BOOKINFO", thumbnailPath, file)) return;
  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok || !bitmap.is1Bit() || bitmap.getWidth() <= 0 ||
      bitmap.getHeight() <= 0) {
    file.close();
    return;
  }

  const size_t dataSize =
      static_cast<size_t>(bitmap.getRowBytes()) * static_cast<size_t>(bitmap.getHeight());
  auto rows = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[dataSize]);
  if (!rows || bitmap.readRawRows(rows.get(), dataSize) != BmpReaderError::Ok) {
    file.close();
    return;
  }

  cachedCover.rows = std::move(rows);
  cachedCover.rowDataSize = dataSize;
  cachedCover.width = bitmap.getWidth();
  cachedCover.height = bitmap.getHeight();
  cachedCover.rowBytes = bitmap.getRowBytes();
  cachedCover.topDown = bitmap.isTopDown();
  cachedCover.blackPaletteIndex = bitmap.get1BitBlackPaletteIndex();
  file.close();
}

void BookInfoActivity::selectCachedThumbnail() {
  if (!thumbnailPath.empty()) {
    const std::string sized = UITheme::getCoverThumbPath(thumbnailPath, kCoverWidth, kCoverHeight);
    if (!sized.empty() && Storage.existsForRead(sized)) {
      thumbnailPath = sized;
      return;
    }
    if (Storage.existsForRead(thumbnailPath)) return;
  }

  const auto selectFirstExisting = [this](const auto& candidates) {
    for (const auto& candidate : candidates) {
      if (!candidate.empty() && Storage.existsForRead(candidate)) {
        thumbnailPath = candidate;
        return true;
      }
    }
    return false;
  };

  if (FsHelpers::hasEpubExtension(bookPath)) {
    Epub epub(bookPath, DUET_BOOKS_ROOT_PATH "");
    const std::array<std::string, 4> candidates = {
        epub.getThumbBmpPath(kCoverWidth, kCoverHeight), epub.getThumbBmpPath(123, 180),
        epub.getThumbBmpPath(230, 338), epub.getThumbBmpPath()};
    selectFirstExisting(candidates);
  } else if (FsHelpers::hasXtcExtension(bookPath)) {
    Xtc xtc(bookPath, DUET_BOOKS_ROOT_PATH "");
    const std::array<std::string, 4> candidates = {
        xtc.getThumbBmpPath(kCoverWidth, kCoverHeight), xtc.getThumbBmpPath(123, 180),
        xtc.getThumbBmpPath(230, 338), xtc.getThumbBmpPath()};
    selectFirstExisting(candidates);
  }
}

void BookInfoActivity::loadDetails() {
  LibraryBookInfo loaded = LibraryBookInfo::load(bookPath, info.title, info.author);
  if (loaded.title.empty()) loaded.title = info.title.empty() ? fallbackTitle : info.title;
  if (loaded.author.empty()) loaded.author = info.author;
  info = std::move(loaded);

  BookReadingStats stats;
  bool hasProgress = progressExists(cachePath);
  LibraryBookStatus indexedStatus;
  if (!cachePath.empty() && LibraryInsights::lookupBookStatus(cachePath, indexedStatus)) {
    stats.totalReadingSeconds = indexedStatus.readingSeconds;
    stats.sessionCount = indexedStatus.sessions;
    stats.isCompleted = indexedStatus.completed;
    hasProgress = indexedStatus.hasProgress;
  } else if (statsExist(cachePath)) {
    stats = BookReadingStats::load(cachePath);
  }
  if (stats.isCompleted) {
    statusText = tr(STR_STATS_FINISHED);
  } else if (!cachePath.empty() && (stats.sessionCount > 0 || stats.totalReadingSeconds > 0 || hasProgress)) {
    RecentBook book;
    book.path = bookPath;
    const float progress = RecentBookProgress::loadPercent(book);
    if (RecentBookProgress::hasPercent(progress)) {
      char buffer[32];
      snprintf(buffer, sizeof(buffer), "%s - %d%%", tr(STR_STATS_READING),
               std::clamp(static_cast<int>(progress + 0.5f), 0, 100));
      statusText = buffer;
    } else {
      statusText = tr(STR_STATS_READING);
    }
  } else {
    statusText = tr(STR_STATS_UNREAD);
  }
  detailsLoaded = true;
}

void BookInfoActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    onSelectBook(bookPath);
    return;
  }
  if (!detailsLoaded && static_cast<long>(millis() - detailsLoadAt) >= 0) {
    const unsigned long startedAt = millis();
    loadDetails();
    loadCachedCover();
    LOG_INF("BOOKINFO", "Deferred metadata and cover loaded in %lums", millis() - startedAt);
    requestUpdate();
  }
}

void BookInfoActivity::render(RenderLock&&) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_MORE_INFO), true);

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int screenWidth = renderer.getScreenWidth();
  const int contentX = metrics.contentSidePadding;
  const int contentWidth = screenWidth - metrics.contentSidePadding * 2;
  const int contentTop = CompactHeader::contentTop(metrics) + metrics.verticalSpacing;
  const int contentBottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  const bool coverDrawn = cachedCover.isReady();
  if (coverDrawn) {
    const float scale = std::min(static_cast<float>(kCoverWidth) / static_cast<float>(cachedCover.width),
                                 static_cast<float>(kCoverHeight) / static_cast<float>(cachedCover.height));
    const int drawWidth = std::max(1, static_cast<int>(cachedCover.width * scale + 0.5f));
    const int drawHeight = std::max(1, static_cast<int>(cachedCover.height * scale + 0.5f));
    const int drawX = contentX + (kCoverWidth - drawWidth) / 2;
    const int drawY = contentTop + (kCoverHeight - drawHeight) / 2;
    renderer.fillRoundedRect(contentX, contentTop, kCoverWidth, kCoverHeight, kCoverCornerRadius, Color::White);
    renderer.drawPerspectiveBitmap1Bit(cachedCover.rows.get(), cachedCover.width, cachedCover.height,
                                       cachedCover.rowBytes, cachedCover.topDown, cachedCover.blackPaletteIndex,
                                       drawX, drawY, drawWidth, drawHeight, drawHeight);
    renderer.maskRoundedRectOutsideCorners(contentX, contentTop, kCoverWidth, kCoverHeight, kCoverCornerRadius,
                                           Color::White);
    renderer.drawRoundedRect(contentX, contentTop, kCoverWidth, kCoverHeight, 1, kCoverCornerRadius, true);
  }
  if (!coverDrawn) {
    renderer.drawRoundedRect(contentX, contentTop, kCoverWidth, kCoverHeight, 1, kCoverCornerRadius, true);
  }

  const int metaX = contentX + kCoverWidth + 16;
  const int metaWidth = std::max(1, contentWidth - kCoverWidth - 16);
  int metaY = contentTop;
  drawLines(renderer, UI_10_FONT_ID,
            renderer.wrappedText(UI_10_FONT_ID, info.title.c_str(), metaWidth, 3, EpdFontFamily::BOLD), metaX, metaY, 1,
            EpdFontFamily::BOLD);
  metaY += 3;
  if (!info.author.empty()) {
    drawLines(renderer, UI_10_FONT_ID, renderer.wrappedText(UI_10_FONT_ID, info.author.c_str(), metaWidth, 2), metaX,
              metaY);
    metaY += 3;
  }

  const auto drawMeta = [&](const std::string& value) {
    if (value.empty() || metaY >= contentTop + kCoverHeight - renderer.getLineHeight(SMALL_FONT_ID)) return;
    const std::string visible = renderer.truncatedText(SMALL_FONT_ID, value.c_str(), metaWidth);
    renderer.drawText(SMALL_FONT_ID, metaX, metaY, visible.c_str());
    metaY += renderer.getLineHeight(SMALL_FONT_ID) + 3;
  };
  if (!info.series.empty()) {
    std::string series = info.series;
    if (!info.seriesIndex.empty() && info.seriesIndex != "0") series += " #" + info.seriesIndex;
    drawMeta(series);
  }
  drawMeta(info.genre);
  drawMeta(info.spice);
  drawMeta(statusText);

  int aboutY = contentTop + kCoverHeight + 18;
  renderer.drawLine(contentX, aboutY - 9, contentX + contentWidth, aboutY - 9);
  renderer.drawText(UI_10_FONT_ID, contentX, aboutY, tr(STR_ABOUT_THIS_BOOK), true, EpdFontFamily::BOLD);
  aboutY += renderer.getLineHeight(UI_10_FONT_ID) + 9;
  const std::string description = !detailsLoaded ? tr(STR_LOADING)
                                  : info.description.empty() ? tr(STR_BOOK_DESCRIPTION_UNAVAILABLE)
                                                             : info.description;
  const int availableDescriptionHeight = std::max(1, contentBottom - aboutY);
  const int maxDescriptionLines = std::max(1, availableDescriptionHeight / renderer.getLineHeight(UI_10_FONT_ID));
  drawLines(renderer, UI_10_FONT_ID,
            renderer.wrappedText(UI_10_FONT_ID, description.c_str(), contentWidth, maxDescriptionLines), contentX,
            aboutY, 2);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
