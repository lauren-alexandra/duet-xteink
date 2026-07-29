#include "ReadingHomeTheme.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "activities/reader/BookReadingStats.h"
#include "activities/reader/GlobalReadingStats.h"
#include "activities/reader/ReadingStatsUtils.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/tools.h"
#include "components/themes/HomeStatFormat.h"
#include "fontIds.h"

namespace {
constexpr int kCardMargin = 12;
constexpr int kCardCornerRadius = 14;
constexpr int kCardPadding = 12;
constexpr int kCardHeight = 240;
constexpr int kRecentGap = 10;
constexpr int kBottomBarHeight = 80;
constexpr int kBottomItemCount = 4;
constexpr int kBottomIconSize = 32;
constexpr int kBottomCornerRadius = 10;
constexpr int kHeaderContentInsetY = 9;

struct Layout {
  Rect continueCard;
  Rect mainCover;
  int recentLabelY = 0;
  int recentCoverY = 0;
  int recentCoverWidth = 0;
  int recentCoverHeight = 0;
  int recentTitleY = 0;
  Rect stats;
  int bottomBarY = 0;
};

Layout makeLayout(const GfxRenderer& renderer) {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int margin = std::max(kCardMargin, screenW / 42);
  const int cardY = ReadingHomeMetrics::values.homeTopPadding + 8;
  const int cardHeight = std::min(kCardHeight, std::max(216, (screenH * 30) / 100));
  const int cardWidth = screenW - margin * 2;
  const int cardCoverH = std::min(ReadingHomeTheme::kCoverHeight, cardHeight - kCardPadding * 2);
  const int cardCoverW = std::min(ReadingHomeTheme::kCoverWidth, (cardCoverH * 2) / 3);
  const int recentLabelY = cardY + cardHeight + 24;
  const int recentCoverY = recentLabelY + renderer.getLineHeight(SMALL_FONT_ID) + 8;
  const int recentCoverW = std::max(
      1, (cardWidth - kRecentGap * (ReadingHomeTheme::kRecentCoverCount - 1)) / ReadingHomeTheme::kRecentCoverCount);
  const int recentCoverH = std::min(210, (recentCoverW * 7) / 5);
  const int recentTitleY = recentCoverY + recentCoverH + 6;
  const int recentBottom = recentTitleY + renderer.getLineHeight(SMALL_FONT_ID);
  const int bottomBarY = screenH - ReadingHomeMetrics::values.buttonHintsHeight - kBottomBarHeight;
  constexpr int statsH = 66;
  const int desiredStatsY = (recentBottom + bottomBarY - statsH) / 2;
  const int statsY = std::clamp(desiredStatsY, recentBottom + 7, bottomBarY - statsH - 7);

  return Layout{Rect{margin, cardY, cardWidth, cardHeight},
                Rect{margin + kCardPadding, cardY + kCardPadding, cardCoverW, cardCoverH},
                recentLabelY,
                recentCoverY,
                recentCoverW,
                recentCoverH,
                recentTitleY,
                Rect{margin, statsY, cardWidth, statsH},
                bottomBarY};
}

Rect fittedBitmapRect(const Bitmap& bitmap, const Rect& target) {
  if (bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0 || target.width <= 0 || target.height <= 0) {
    return target;
  }

  const float widthScale = static_cast<float>(target.width) / static_cast<float>(bitmap.getWidth());
  const float heightScale = static_cast<float>(target.height) / static_cast<float>(bitmap.getHeight());
  const float scale = std::min(1.0f, std::min(widthScale, heightScale));
  const int drawnW = std::min(target.width, std::max(1, static_cast<int>(std::ceil(bitmap.getWidth() * scale))));
  const int drawnH = std::min(target.height, std::max(1, static_cast<int>(std::ceil(bitmap.getHeight() * scale))));
  return Rect{target.x + (target.width - drawnW) / 2, target.y + (target.height - drawnH) / 2, drawnW, drawnH};
}

std::string coverPathForRect(const RecentBook& book, const Rect& target) {
  if (book.coverBmpPath.empty()) return {};
  if (FsHelpers::hasEpubExtension(book.path)) {
    const std::string adaptive =
        Epub(book.path, DUET_BOOKS_ROOT_PATH "")
            .getAdaptiveThumbBmpPath(ReadingHomeTheme::kCoverWidth, ReadingHomeTheme::kCoverHeight);
    if (Storage.existsForRead(adaptive)) return adaptive;
  }
  return UITheme::getCoverThumbPath(book.coverBmpPath, ReadingHomeTheme::kCoverWidth, ReadingHomeTheme::kCoverHeight);
}

void drawMissingCover(const GfxRenderer& renderer, const Rect& rect, const RecentBook& book) {
  renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, 6, Color::White);
  renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 1, 6, true);
  constexpr int iconSize = 32;
  renderer.drawIcon(CoverIcon, rect.x + (rect.width - iconSize) / 2, rect.y + 24, iconSize, iconSize);

  const int textWidth = std::max(1, rect.width - 16);
  const char* title = book.title.empty() ? tr(STR_NO_OPEN_BOOK) : book.title.c_str();
  const auto lines = renderer.wrappedText(SMALL_FONT_ID, title, textWidth, 3, EpdFontFamily::BOLD);
  int y = rect.y + rect.height / 2;
  for (const auto& line : lines) {
    const int width = renderer.getTextWidth(SMALL_FONT_ID, line.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, rect.x + (rect.width - width) / 2, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(SMALL_FONT_ID);
  }
}

void drawCover(const GfxRenderer& renderer, const Rect& rect, const RecentBook& book) {
  bool drewCover = false;
  const std::string path = coverPathForRect(book, rect);
  if (!path.empty() && Storage.existsForRead(path)) {
    FsFile file;
    if (Storage.openFileForRead("READ_HOME", path, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        const Rect bitmapRect = fittedBitmapRect(bitmap, rect);
        renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, 6, Color::White);
        renderer.fillRoundedRect(bitmapRect.x, bitmapRect.y, bitmapRect.width, bitmapRect.height, 6, Color::White);
        renderer.drawBitmap(bitmap, bitmapRect.x, bitmapRect.y, bitmapRect.width, bitmapRect.height);
        renderer.maskRoundedRectOutsideCorners(bitmapRect.x, bitmapRect.y, bitmapRect.width, bitmapRect.height, 6,
                                               Color::White);
        renderer.drawRoundedRect(bitmapRect.x, bitmapRect.y, bitmapRect.width, bitmapRect.height, 1, 6, true);
        drewCover = true;
      }
      file.close();
    }
  }
  if (!drewCover) drawMissingCover(renderer, rect, book);
}

void formatCompactDuration(uint32_t seconds, char* buffer, size_t len) {
  if (seconds == 0) {
    snprintf(buffer, len, "--");
    return;
  }
  const uint32_t minutes = std::max<uint32_t>(1, seconds / 60);
  if (minutes >= 60) {
    snprintf(buffer, len, "%luh %lum", static_cast<unsigned long>(minutes / 60),
             static_cast<unsigned long>(minutes % 60));
  } else {
    snprintf(buffer, len, "%lum", static_cast<unsigned long>(minutes));
  }
}

uint32_t estimatedTimeLeft(const BookReadingStats* stats, float progressPercent) {
  if (!stats || !stats->hasReliableTimeLeftBasis()) return 0;
  if (stats->estimatedTimeLeftSeconds > 0) return stats->estimatedTimeLeftSeconds;
  if (progressPercent <= 0.0f || progressPercent >= 100.0f || stats->totalReadingSeconds == 0) return 0;
  const float progress = std::clamp(progressPercent, 0.0f, 100.0f) / 100.0f;
  const float estimate = static_cast<float>(stats->totalReadingSeconds) * (1.0f - progress) / progress;
  return static_cast<uint32_t>(std::min<float>(estimate, static_cast<float>(UINT32_MAX)));
}

bool estimateFinishDate(const BookReadingStats* stats, const float progressPercent, ReadingStatsDate& outDate) {
  outDate.clear();
  if (!stats || stats->isCompleted) return false;

  const uint32_t remainingReadingSeconds = estimatedTimeLeft(stats, progressPercent);
  if (remainingReadingSeconds == 0) return false;

  ReadingStatsDateTime now;
  if (!getCurrentLocalReadingStatsDateTime(now)) return false;

  if (stats->startDate.isValid() && stats->totalReadingSeconds > 0) {
    const uint16_t elapsedDays = readingSpanDaysElapsed(stats->startDate, now.date);
    const uint16_t readingDays = std::max<uint16_t>(1, elapsedDays);
    const uint64_t estimatedCalendarSeconds =
        (static_cast<uint64_t>(remainingReadingSeconds) * static_cast<uint64_t>(readingDays) * 86400ULL +
         static_cast<uint64_t>(stats->totalReadingSeconds) / 2ULL) /
        static_cast<uint64_t>(stats->totalReadingSeconds);
    if (estimatedCalendarSeconds > 0) {
      ReadingStatsDateTime estimatedFinish = now;
      addSecondsToReadingStatsDateTime(estimatedFinish,
                                       static_cast<uint32_t>(std::min<uint64_t>(estimatedCalendarSeconds, UINT32_MAX)));
      if (estimatedFinish.date.isValid()) {
        outDate = estimatedFinish.date;
        return true;
      }
    }
  }

  ReadingStatsDateTime fallbackFinish = now;
  addSecondsToReadingStatsDateTime(fallbackFinish, remainingReadingSeconds);
  if (!fallbackFinish.date.isValid()) return false;
  outDate = fallbackFinish.date;
  return true;
}

bool startsWithChapter(const char* text) {
  constexpr char kPrefix[] = "chapter";
  if (!text) return false;
  for (size_t index = 0; index < sizeof(kPrefix) - 1; ++index) {
    if (text[index] == '\0' || std::tolower(static_cast<unsigned char>(text[index])) != kPrefix[index]) {
      return false;
    }
  }
  return true;
}

std::string chapterLabel(const char* chapterTitle) {
  if (!chapterTitle || chapterTitle[0] == '\0') return {};
  if (startsWithChapter(chapterTitle)) return chapterTitle;
  return std::string(tr(STR_CHAPTER_PREFIX)) + chapterTitle;
}

void drawCenteredText(const GfxRenderer& renderer, int fontId, const Rect& rect, int y, const char* text,
                      bool bold = false) {
  const EpdFontFamily::Style style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  const int width = renderer.getTextWidth(fontId, text, style);
  renderer.drawText(fontId, rect.x + (rect.width - width) / 2, y, text, true, style);
}

int fittingValueFont(const GfxRenderer& renderer, const int preferredFontId, const char* value, const int maxWidth) {
  if (renderer.getTextWidth(preferredFontId, value, EpdFontFamily::BOLD) <= maxWidth) {
    return preferredFontId;
  }
  return SMALL_FONT_ID;
}

void drawBookStatCell(const GfxRenderer& renderer, const int x, const int maxWidth, const int y, const char* label,
                      const char* value, const int preferredValueFontId) {
  const int compactValueFontId = preferredValueFontId == SMALL_FONT_ID ? SMALL_FONT_ID : UI_10_FONT_ID;
  const int valueFontId = fittingValueFont(renderer, compactValueFontId, value, maxWidth);
  const std::string visibleValue = renderer.truncatedText(valueFontId, value, maxWidth, EpdFontFamily::BOLD);
  const std::string visibleLabel = renderer.truncatedText(SMALL_FONT_ID, label, maxWidth);
  renderer.drawText(valueFontId, x, y, visibleValue.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, x, y + renderer.getLineHeight(UI_10_FONT_ID) + 1, visibleLabel.c_str(), true);
}

void drawContinueReadingCard(const GfxRenderer& renderer, const Layout& layout,
                             const std::vector<RecentBook>& recentBooks, int selectorIndex,
                             const BookReadingStats* stats, float progressPercent, const char* currentChapterTitle,
                             const HomeStatContext& statCtx) {
  const Rect& card = layout.continueCard;
  renderer.drawRoundedRect(card.x, card.y, card.width, card.height, 1, kCardCornerRadius, true);

  if (recentBooks.empty()) {
    constexpr int iconSize = 32;
    const int iconY = card.y + (card.height - iconSize) / 2 - 18;
    renderer.drawIcon(CoverIcon, card.x + (card.width - iconSize) / 2, iconY, iconSize, iconSize);
    drawCenteredText(renderer, UI_12_FONT_ID, card, iconY + iconSize + 8, tr(STR_NO_RECENT_BOOKS), true);
    drawCenteredText(renderer, SMALL_FONT_ID, card, iconY + iconSize + 8 + renderer.getLineHeight(UI_12_FONT_ID) + 4,
                     tr(STR_START_READING));
  } else {
    const RecentBook& book = recentBooks[0];
    drawCover(renderer, layout.mainCover, book);

    const int infoX = layout.mainCover.x + layout.mainCover.width + 10;
    const int infoWidth = card.x + card.width - kCardPadding - infoX;
    const int smallLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const int mediumLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    int infoY = layout.mainCover.y;

    const char* rawTitle = book.title.empty() ? tr(STR_NO_OPEN_BOOK) : book.title.c_str();
    const auto title = renderer.truncatedText(UI_12_FONT_ID, rawTitle, infoWidth);
    renderer.drawText(UI_12_FONT_ID, infoX, infoY, title.c_str(), true);
    infoY += mediumLineHeight + 4;
    if (!book.author.empty()) {
      const auto author = renderer.truncatedText(SMALL_FONT_ID, book.author.c_str(), infoWidth);
      renderer.drawText(SMALL_FONT_ID, infoX, infoY, author.c_str(), true);
      infoY += smallLineHeight + 5;
    }

    const std::string chapter = chapterLabel(currentChapterTitle);
    if (!chapter.empty()) {
      const auto visibleChapter = renderer.truncatedText(SMALL_FONT_ID, chapter.c_str(), infoWidth);
      renderer.drawText(SMALL_FONT_ID, infoX, infoY, visibleChapter.c_str(), true);
      infoY += smallLineHeight + 7;
    }

    const int progress = progressPercent < 0.0f ? -1 : std::clamp(static_cast<int>(progressPercent + 0.5f), 0, 100);
    char percentage[8];
    snprintf(percentage, sizeof(percentage), progress >= 0 ? "%d%%" : "--", progress);
    renderer.drawText(UI_12_FONT_ID, infoX, infoY, percentage, true);
    infoY += mediumLineHeight + 4;
    constexpr int barHeight = 8;
    renderer.drawRect(infoX, infoY, infoWidth, barHeight);
    if (progress > 0)
      renderer.fillRect(infoX + 1, infoY + 1, std::max(0, (infoWidth - 2) * progress / 100), barHeight - 2);
    infoY += barHeight + 12;

    constexpr int kMetricColumnCount = 3;
    const int columnWidth = infoWidth / kMetricColumnCount;
    constexpr int kMetricRowGap = 4;
    const int metricRowHeight =
        renderer.getLineHeight(UI_10_FONT_ID) + 1 + renderer.getLineHeight(SMALL_FONT_ID) + kMetricRowGap;

    // Six card cells (two rows of three) driven by the Home Stats picker
    // slots 1-6; empty slots compact so chosen stats fill from the top left.
    const uint8_t cardSlots[] = {SETTINGS.homeStatSlot1, SETTINGS.homeStatSlot2, SETTINGS.homeStatSlot3,
                                 SETTINGS.homeStatSlot4, SETTINGS.homeStatSlot5, SETTINGS.homeStatSlot6};
    char value[48];
    char label[48];
    int cell = 0;
    for (const uint8_t kind : cardSlots) {
      int valueFontId = UI_12_FONT_ID;
      if (!formatHomeStat(kind, statCtx, value, sizeof(value), label, sizeof(label), valueFontId)) continue;
      if (kind == CrossPointSettings::HOME_STAT_DAYS_READING) {
        formatReadingStatsShortDate(statCtx.book.startDate, value, sizeof(value));
        snprintf(label, sizeof(label), "%s", tr(STR_STATS_STARTED));
      }
      const int column = cell % kMetricColumnCount;
      const int cellX = infoX + column * columnWidth;
      const int cellWidth = column == kMetricColumnCount - 1 ? infoX + infoWidth - cellX : columnWidth - 2;
      drawBookStatCell(renderer, cellX, cellWidth, infoY, label, value, valueFontId);
      if (column == kMetricColumnCount - 1) infoY += metricRowHeight;
      cell++;
      if (cell >= 6) break;
    }
  }

  if (selectorIndex == 0) {
    renderer.drawRoundedRect(card.x, card.y, card.width, card.height, 2, kCardCornerRadius, true);
  }
}

void drawRecentCovers(const GfxRenderer& renderer, const Layout& layout, const std::vector<RecentBook>& recentBooks,
                      int selectorIndex, const std::array<float, ReadingHomeTheme::kRecentBookCount>& progressByBook) {
  if (recentBooks.size() <= 1) return;

  renderer.drawText(SMALL_FONT_ID, layout.continueCard.x + kCardPadding, layout.recentLabelY,
                    tr(STR_HOME_RECENTLY_READ), true, EpdFontFamily::BOLD);
  const int count = std::min(ReadingHomeTheme::kRecentCoverCount, static_cast<int>(recentBooks.size()) - 1);
  for (int slot = 0; slot < count; ++slot) {
    const int bookIndex = slot + 1;
    const Rect cover{layout.continueCard.x + slot * (layout.recentCoverWidth + kRecentGap), layout.recentCoverY,
                     layout.recentCoverWidth, layout.recentCoverHeight};
    drawCover(renderer, cover, recentBooks[bookIndex]);

    const float rawProgress = progressByBook[bookIndex];
    const int progress = rawProgress < 0.0f ? 0 : std::clamp(static_cast<int>(rawProgress + 0.5f), 0, 100);
    const int barX = cover.x + 4;
    const int barY = cover.y + cover.height - 5;
    const int barWidth = cover.width - 8;
    renderer.fillRect(barX, barY, barWidth, 4, false);
    if (progress > 0) renderer.fillRect(barX, barY, barWidth * progress / 100, 4);

    const char* rawTitle =
        recentBooks[bookIndex].title.empty() ? tr(STR_NO_OPEN_BOOK) : recentBooks[bookIndex].title.c_str();
    const auto title = renderer.truncatedText(SMALL_FONT_ID, rawTitle, cover.width - 4);
    drawCenteredText(renderer, SMALL_FONT_ID, cover, layout.recentTitleY, title.c_str());

    if (selectorIndex == bookIndex) {
      renderer.drawRoundedRect(cover.x - 3, cover.y - 3, cover.width + 6, cover.height + 6, 2, 14, true);
    }
  }
}

void drawStatsBar(const GfxRenderer& renderer, const Layout& layout, const HomeStatContext& statCtx) {
  const Rect& panel = layout.stats;
  constexpr int kStatCount = 4;
  const int colWidth = panel.width / kStatCount;
  for (int index = 1; index < kStatCount; ++index) {
    renderer.fillRect(panel.x + colWidth * index, panel.y + 8, 1, panel.height - 16, true);
  }

  // Four strip tiles driven by the Home Stats picker.
  const uint8_t stripSlots[] = {SETTINGS.homeStrip1, SETTINGS.homeStrip2, SETTINGS.homeStrip3, SETTINGS.homeStrip4};
  const int labelY = panel.y + 12;
  const int valueY = labelY + renderer.getLineHeight(SMALL_FONT_ID) + 6;
  char value[48];
  char label[48];
  for (int index = 0; index < kStatCount; ++index) {
    int valueFontId = UI_12_FONT_ID;
    if (!formatHomeStat(stripSlots[index], statCtx, value, sizeof(value), label, sizeof(label), valueFontId)) {
      continue;
    }
    const int columnX = panel.x + index * colWidth;
    const int columnWidth = index == kStatCount - 1 ? panel.x + panel.width - columnX : colWidth;
    const Rect column{columnX, panel.y, columnWidth, panel.height};
    drawCenteredText(renderer, SMALL_FONT_ID, column, labelY, label);
    drawCenteredText(renderer, SMALL_FONT_ID, column, valueY, value, true);
  }
}

void drawBottomNavigation(const GfxRenderer& renderer, const Layout& layout, int selectorIndex, int recentCount) {
  const int screenW = renderer.getScreenWidth();
  renderer.drawLine(0, layout.bottomBarY, screenW - 1, layout.bottomBarY);
  constexpr int barPadding = 8;
  constexpr int innerTop = 10;
  const int itemWidth = (screenW - barPadding * 2) / kBottomItemCount;
  const int contentHeight = kBottomBarHeight - innerTop;
  const int navStart = 1 + recentCount;

  struct NavigationItem {
    const uint8_t* icon;
    const char* label;
  };
  const NavigationItem items[] = {
      {ToolsIcon, tr(STR_APPS)},
      {RecentIcon, tr(STR_RECENTS)},
      {LibraryIcon, tr(STR_BROWSE_FILES)},
      {Settings2Icon, tr(STR_SETTINGS_TITLE)},
  };

  for (int index = 0; index < kBottomItemCount; ++index) {
    const int x = barPadding + index * itemWidth;
    const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const int totalHeight = kBottomIconSize + 4 + lineHeight;
    const int startY = layout.bottomBarY + innerTop + (contentHeight - totalHeight) / 2;
    renderer.drawIcon(items[index].icon, x + (itemWidth - kBottomIconSize) / 2, startY, kBottomIconSize,
                      kBottomIconSize);
    const auto label = renderer.truncatedText(SMALL_FONT_ID, items[index].label, itemWidth - 4);
    const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, label.c_str());
    renderer.drawText(SMALL_FONT_ID, x + (itemWidth - labelWidth) / 2, startY + kBottomIconSize + 4, label.c_str(),
                      true);
  }

  const int selectedNav = selectorIndex - navStart;
  if (selectedNav < 0 || selectedNav >= kBottomItemCount) return;
  const int x = barPadding + selectedNav * itemWidth;
  renderer.fillRoundedRect(x + 2, layout.bottomBarY + innerTop - 2, itemWidth - 4, contentHeight, kBottomCornerRadius,
                           Color::Black);
  const auto label = renderer.truncatedText(SMALL_FONT_ID, items[selectedNav].label, itemWidth - 8);
  const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, label.c_str());
  const int labelY = layout.bottomBarY + innerTop + (contentHeight - renderer.getLineHeight(SMALL_FONT_ID)) / 2;
  renderer.drawText(SMALL_FONT_ID, x + (itemWidth - labelWidth) / 2, labelY, label.c_str(), false);
}

void redrawSelectionOnly(const GfxRenderer& renderer, const Layout& layout, const int selectorIndex,
                         const int recentCount) {
  const Rect& card = layout.continueCard;
  renderer.drawRoundedRect(card.x, card.y, card.width, card.height, 3, kCardCornerRadius, false);
  renderer.drawRoundedRect(card.x, card.y, card.width, card.height, 1, kCardCornerRadius, true);
  if (selectorIndex == 0) {
    renderer.drawRoundedRect(card.x, card.y, card.width, card.height, 2, kCardCornerRadius, true);
  }

  for (int slot = 0; slot < recentCount; ++slot) {
    const Rect cover{layout.continueCard.x + slot * (layout.recentCoverWidth + kRecentGap), layout.recentCoverY,
                     layout.recentCoverWidth, layout.recentCoverHeight};
    renderer.drawRoundedRect(cover.x - 3, cover.y - 3, cover.width + 6, cover.height + 6, 3, 14, false);
    if (selectorIndex == slot + 1) {
      renderer.drawRoundedRect(cover.x - 3, cover.y - 3, cover.width + 6, cover.height + 6, 2, 14, true);
    }
  }

  const int navigationHeight =
      renderer.getScreenHeight() - ReadingHomeMetrics::values.buttonHintsHeight - layout.bottomBarY;
  renderer.fillRect(0, layout.bottomBarY, renderer.getScreenWidth(), navigationHeight, false);
  drawBottomNavigation(renderer, layout, selectorIndex, recentCount);
}
}  // namespace

void ReadingHomeTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle,
                                  const bool readerContext) const {
  const int inset = std::min(kHeaderContentInsetY, std::max(0, rect.height - 1));
  rect.y += inset;
  rect.height -= inset;
  MinimalTheme::drawHeader(renderer, rect, title, subtitle, readerContext);
}

void ReadingHomeTheme::drawReadingHome(GfxRenderer& renderer, const std::vector<RecentBook>& recentBooks,
                                       int selectorIndex, const BookReadingStats* currentStats,
                                       float currentProgressPercent, const uint32_t currentBookWordCount,
                                       const char* currentChapterTitle,
                                       const std::array<float, kRecentBookCount>& progressByBook,
                                       const GlobalReadingStats& deviceStats,
                                       const GlobalReadingStats& allDevicesStats, uint32_t todayReadingSeconds,
                                       uint16_t currentStreak) const {
  const Layout layout = makeLayout(renderer);
  const BookReadingStats emptyStats{};
  HomeStatContext statCtx =
      buildHomeStatContext(currentStats != nullptr ? *currentStats : emptyStats, currentProgressPercent, &deviceStats);
  statCtx.bookWordCount = currentBookWordCount;
  statCtx.todaySeconds = todayReadingSeconds;
  statCtx.journalStreakDays = currentStreak;
  statCtx.allStats = &allDevicesStats;
  drawContinueReadingCard(renderer, layout, recentBooks, selectorIndex, currentStats, currentProgressPercent,
                          currentChapterTitle, statCtx);
  drawRecentCovers(renderer, layout, recentBooks, selectorIndex, progressByBook);
  drawStatsBar(renderer, layout, statCtx);
  const int recentCount = std::min(kRecentCoverCount, std::max(0, static_cast<int>(recentBooks.size()) - 1));
  drawBottomNavigation(renderer, layout, selectorIndex, recentCount);
}

void ReadingHomeTheme::drawReadingHomeSelection(GfxRenderer& renderer, const int selectorIndex,
                                                const int recentCount) const {
  redrawSelectionOnly(renderer, makeLayout(renderer), selectorIndex, recentCount);
}
