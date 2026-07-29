#include "DashboardTheme.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "activities/reader/BookReadingStats.h"
#include "components/themes/HomeStatFormat.h"
#include "activities/reader/GlobalReadingStats.h"
#include "activities/reader/ReadingStatsUtils.h"
#include "components/UITheme.h"
#include "components/icons/afternoon.h"
#include "components/icons/book24.h"
#include "components/icons/cover.h"
#include "components/icons/evening.h"
#include "components/icons/morning.h"
#include "components/icons/night.h"
#include "components/icons/streak.h"
#include "fontIds.h"

namespace {
constexpr int kContentInsetX4 = 20;
constexpr int kContentInsetX3 = 75;
constexpr int kTopInset = 20;
constexpr int kCoverCornerRadius = 8;
constexpr int kStatsColumnWidth = 105;
constexpr int kStatsColumnWidthWide = 120;
constexpr int kCoverStatsGap = 15;
constexpr int kPairInwardShiftX3 = 15;
constexpr int kTitleTopGap = 28;
constexpr int kTitleChapterGap = 8;
constexpr int kBookTitleMaxLines = 2;
constexpr int kBookChapterMaxLines = 2;
constexpr int kFooterIconSize = 24;
constexpr int kFooterIconTextGap = 18;
constexpr int kFooterBottomGap = 57;
constexpr int kStatsRowCount = 7;
constexpr int kStatsValueLabelGap = 1;

bool isWideScreen(const GfxRenderer& renderer) { return renderer.getScreenWidth() >= 560; }

int contentInset(const GfxRenderer& renderer) { return isWideScreen(renderer) ? kContentInsetX3 : kContentInsetX4; }

Rect coverRectForScreen(const GfxRenderer& renderer, const Rect& rect) {
  const int inset = contentInset(renderer);
  const int statsW = isWideScreen(renderer) ? kStatsColumnWidthWide : kStatsColumnWidth;
  const int maxCoverW = renderer.getScreenWidth() - inset * 2 - statsW - kCoverStatsGap;
  const int coverW = std::min(DashboardMetrics::homeCoverImageWidth, maxCoverW);
  const int coverH = std::min(DashboardMetrics::homeCoverImageHeight, (coverW * 3) / 2);
  return Rect{inset + (gpio.deviceIsX3() ? kPairInwardShiftX3 : 0), rect.y + kTopInset, coverW, coverH};
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

std::string coverPathForRect(const RecentBook& book, const Rect& imageRect) {
  if (book.coverBmpPath.empty()) {
    return {};
  }
  if (FsHelpers::hasEpubExtension(book.path)) {
    const std::string adaptivePath =
        Epub(book.path, DUET_BOOKS_ROOT_PATH "").getAdaptiveThumbBmpPath(imageRect.width, imageRect.height);
    if (Storage.existsForRead(adaptivePath)) {
      return adaptivePath;
    }
  }
  return UITheme::getCoverThumbPath(book.coverBmpPath, imageRect.width, imageRect.height);
}

void drawMissingBookCover(const GfxRenderer& renderer, const Rect& coverRect, const RecentBook& book) {
  renderer.fillRoundedRect(coverRect.x, coverRect.y, coverRect.width, coverRect.height, kCoverCornerRadius,
                           Color::White);
  renderer.drawRoundedRect(coverRect.x, coverRect.y, coverRect.width, coverRect.height, 1, kCoverCornerRadius, true);

  constexpr int iconSize = 32;
  renderer.drawIcon(CoverIcon, coverRect.x + (coverRect.width - iconSize) / 2, coverRect.y + 36, iconSize, iconSize);

  constexpr int textPadding = 14;
  const int textW = coverRect.width - textPadding * 2;
  const char* title = book.title.empty() ? book.path.c_str() : book.title.c_str();
  auto titleLines = renderer.wrappedText(UI_12_FONT_ID, title, textW, 4, EpdFontFamily::BOLD);
  const int lineH = renderer.getLineHeight(UI_12_FONT_ID);
  int textY = coverRect.y + (coverRect.height - static_cast<int>(titleLines.size()) * lineH) / 2;
  for (const auto& line : titleLines) {
    const int lineW = renderer.getTextWidth(UI_12_FONT_ID, line.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, coverRect.x + (coverRect.width - lineW) / 2, textY, line.c_str(), true,
                      EpdFontFamily::BOLD);
    textY += lineH;
  }
}

void drawBookCover(const GfxRenderer& renderer, const Rect& coverRect, const RecentBook& book,
                   const Color backgroundColor) {
  bool hasCover = false;
  const std::string coverBmpPath = coverPathForRect(book, coverRect);
  if (!coverBmpPath.empty() && Storage.existsForRead(coverBmpPath)) {
    FsFile file;
    if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        const Rect bitmapRect = fittedBitmapRect(bitmap, coverRect);
        renderer.fillRoundedRect(coverRect.x, coverRect.y, coverRect.width, coverRect.height, kCoverCornerRadius,
                                 backgroundColor);
        renderer.fillRoundedRect(bitmapRect.x, bitmapRect.y, bitmapRect.width, bitmapRect.height, kCoverCornerRadius,
                                 Color::White);
        renderer.drawBitmap(bitmap, bitmapRect.x, bitmapRect.y, bitmapRect.width, bitmapRect.height);
        renderer.maskRoundedRectOutsideCorners(bitmapRect.x, bitmapRect.y, bitmapRect.width, bitmapRect.height,
                                               kCoverCornerRadius, backgroundColor);
        renderer.drawRoundedRect(bitmapRect.x, bitmapRect.y, bitmapRect.width, bitmapRect.height, 1, kCoverCornerRadius,
                                 true);
        hasCover = true;
      }
      file.close();
    }
  }

  if (!hasCover) {
    drawMissingBookCover(renderer, coverRect, book);
  }
}

void drawRightAlignedText(const GfxRenderer& renderer, const int fontId, const int rightX, const int y,
                          const char* text, const bool bold = false, const bool black = true) {
  const EpdFontFamily::Style style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  const int width = renderer.getTextWidth(fontId, text, style);
  renderer.drawText(fontId, rightX - width, y, text, black, style);
}

void formatCompactDuration(const uint32_t seconds, char* buf, const size_t len) {
  if (seconds < 60) {
    snprintf(buf, len, "%s", tr(STR_STATS_LESS_THAN_MIN));
    return;
  }
  const uint32_t minutes = (seconds + 30u) / 60u;
  if (minutes < 60) {
    snprintf(buf, len, "%lu min", static_cast<unsigned long>(minutes));
    return;
  }
  const uint32_t hours = minutes / 60u;
  const uint32_t remainder = minutes % 60u;
  if (remainder == 0) {
    snprintf(buf, len, "%luh", static_cast<unsigned long>(hours));
  } else {
    snprintf(buf, len, "%luh %lum", static_cast<unsigned long>(hours), static_cast<unsigned long>(remainder));
  }
}

bool fallbackEstimatedTimeLeft(const BookReadingStats& stats, const float progressPercent, uint32_t& seconds) {
  seconds = 0;
  if (!stats.hasReliableTimeLeftBasis() || progressPercent <= 0.0f || progressPercent >= 100.0f) {
    return false;
  }
  const float progress = progressPercent / 100.0f;
  const float estimate = (static_cast<float>(stats.totalReadingSeconds) * (1.0f - progress)) / progress;
  if (estimate <= 0.0f) {
    return false;
  }
  seconds = static_cast<uint32_t>(estimate + 0.5f);
  return seconds > 0;
}

bool estimatedTimeLeft(const BookReadingStats& stats, const float progressPercent, uint32_t& seconds) {
  if (!stats.hasReliableTimeLeftBasis()) {
    seconds = 0;
    return false;
  }
  if (stats.estimatedTimeLeftSeconds > 0) {
    seconds = stats.estimatedTimeLeftSeconds;
    return true;
  }
  return fallbackEstimatedTimeLeft(stats, progressPercent, seconds);
}

bool estimateFinishDateFromDailyPace(const BookReadingStats& stats, const ReadingStatsDateTime& today,
                                     const uint32_t estimatedReadingSeconds, ReadingStatsDate& outDate) {
  outDate = {};
  if (!today.isValid() || !stats.startDate.isValid() || estimatedReadingSeconds == 0 ||
      stats.totalReadingSeconds == 0) {
    return false;
  }

  const uint16_t elapsedDays = readingSpanDaysElapsed(stats.startDate, today.date);
  const uint16_t readingDays = std::max<uint16_t>(1, elapsedDays);
  const uint64_t estimatedCalendarSeconds =
      (static_cast<uint64_t>(estimatedReadingSeconds) * static_cast<uint64_t>(readingDays) * 86400ULL +
       static_cast<uint64_t>(stats.totalReadingSeconds) / 2ULL) /
      static_cast<uint64_t>(stats.totalReadingSeconds);
  if (estimatedCalendarSeconds == 0) {
    return false;
  }

  ReadingStatsDateTime estimatedFinish = today;
  addSecondsToReadingStatsDateTime(estimatedFinish,
                                   static_cast<uint32_t>(std::min<uint64_t>(estimatedCalendarSeconds, UINT32_MAX)));
  outDate = estimatedFinish.date;
  return outDate.isValid();
}

const char* dayCountText(const uint16_t days) { return days == 1 ? tr(STR_STATS_DAY) : tr(STR_STATS_DAYS); }

int statsBlockHeight(const GfxRenderer& renderer) {
  const int valueLineH = renderer.getLineHeight(UI_12_FONT_ID);
  const int labelLineH = renderer.getLineHeight(SMALL_FONT_ID);
  return valueLineH + kStatsValueLabelGap + labelLineH;
}

int statsBlockTop(const Rect& coverRect, const int index, const int blockH, const int rowCount) {
  const int remainingH = std::max(0, coverRect.height - blockH * rowCount);
  const int gapCount = rowCount - 1;
  const int gap = gapCount > 0 ? remainingH / gapCount : 0;
  const int remainder = gapCount > 0 ? remainingH % gapCount : 0;
  return coverRect.y + index * (blockH + gap) + std::min(index, remainder);
}

void drawStatsRow(const GfxRenderer& renderer, const int rightX, const int y, const char* value, const char* label,
                  const bool black = true, const int valueFontId = UI_12_FONT_ID) {
  const int valueLineH = renderer.getLineHeight(UI_12_FONT_ID);
  drawRightAlignedText(renderer, valueFontId, rightX, y, value, true, black);
  drawRightAlignedText(renderer, SMALL_FONT_ID, rightX, y + valueLineH + kStatsValueLabelGap, label, false, black);
}

const uint8_t* readerTypeIcon(const GlobalReadingStats* globalStats);

void drawDashboardStats(const GfxRenderer& renderer, const Rect& coverRect, const BookReadingStats* stats,
                        const float progressPercent, const GlobalReadingStats* globalStats,
                        const GlobalReadingStats* allDevicesStats, const uint32_t currentBookWordCount,
                        const bool black = true) {
  const int rightX = renderer.getScreenWidth() - contentInset(renderer) - (gpio.deviceIsX3() ? kPairInwardShiftX3 : 0);
  const int blockH = statsBlockHeight(renderer);
  const BookReadingStats emptyStats{};
  const BookReadingStats& bookStats = stats != nullptr ? *stats : emptyStats;
  HomeStatContext ctx = buildHomeStatContext(bookStats, progressPercent, globalStats);
  ctx.bookWordCount = currentBookWordCount;
  if (allDevicesStats != nullptr) ctx.allStats = allDevicesStats;

  const uint8_t slots[] = {SETTINGS.homeStatSlot1, SETTINGS.homeStatSlot2, SETTINGS.homeStatSlot3,
                           SETTINGS.homeStatSlot4, SETTINGS.homeStatSlot5, SETTINGS.homeStatSlot6,
                           SETTINGS.homeStatSlot7};
  char value[48];
  char label[48];
  int rowIndex = 0;
  for (const uint8_t kind : slots) {
    if (rowIndex >= kStatsRowCount) break;
    int valueFontId = UI_12_FONT_ID;
    if (!formatHomeStat(kind, ctx, value, sizeof(value), label, sizeof(label), valueFontId)) continue;
    const int rowY = statsBlockTop(coverRect, rowIndex, blockH, kStatsRowCount);
    drawStatsRow(renderer, rightX, rowY, value, label, black, valueFontId);
    rowIndex++;
  }
}

bool dominantReaderTypeBucket(const GlobalReadingStats& globalStats, ReadingTimeBucket& bucketOut) {
  const auto& values = globalStats.timeOfDaySeconds;
  const uint32_t totalSeconds = std::accumulate(values.begin(), values.end(), 0u);
  if (totalSeconds == 0) {
    return false;
  }

  const size_t dominantIndex =
      static_cast<size_t>(std::distance(values.begin(), std::max_element(values.begin(), values.end())));
  bucketOut = static_cast<ReadingTimeBucket>(dominantIndex);
  return true;
}

const char* readerTypeLabel(const GlobalReadingStats* globalStats) {
  if (globalStats == nullptr) {
    return tr(STR_STATS_NEW_READER);
  }

  ReadingTimeBucket bucket = ReadingTimeBucket::Night;
  if (!dominantReaderTypeBucket(*globalStats, bucket)) {
    return tr(STR_STATS_NEW_READER);
  }

  switch (bucket) {
    case ReadingTimeBucket::Morning:
      return tr(STR_STATS_MORNING_READER);
    case ReadingTimeBucket::Afternoon:
      return tr(STR_STATS_AFTERNOON_READER);
    case ReadingTimeBucket::Evening:
      return tr(STR_STATS_EVENING_READER);
    case ReadingTimeBucket::Night:
    default:
      return tr(STR_STATS_NIGHT_READER);
  }
}

const uint8_t* readerTypeIcon(const GlobalReadingStats* globalStats) {
  if (globalStats == nullptr) {
    return Book24Icon;
  }

  ReadingTimeBucket bucket = ReadingTimeBucket::Night;
  if (!dominantReaderTypeBucket(*globalStats, bucket)) {
    return Book24Icon;
  }

  switch (bucket) {
    case ReadingTimeBucket::Morning:
      return MorningReaderIcon;
    case ReadingTimeBucket::Afternoon:
      return AfternoonReaderIcon;
    case ReadingTimeBucket::Evening:
      return EveningReaderIcon;
    case ReadingTimeBucket::Night:
    default:
      return NightReaderIcon;
  }
}

void formatStreakStat(const GlobalReadingStats* globalStats, char* buf, const size_t len) {
  if (len == 0) {
    return;
  }
  if (globalStats == nullptr) {
    snprintf(buf, len, "%s", tr(STR_STATS_NO_STREAK));
    return;
  }

  ReadingStatsDateTime today;
  const uint16_t streak =
      getCurrentLocalReadingStatsDateTime(today) ? globalStats->currentReadingStreak(&today.date) : 0;
  if (streak == 0) {
    snprintf(buf, len, "%s", tr(STR_STATS_NO_STREAK));
    return;
  }
  snprintf(buf, len, tr(STR_STATS_DAY_STREAK_FORMAT), static_cast<unsigned>(streak));
}

void drawIconLabel(const GfxRenderer& renderer, const uint8_t* icon, const int iconX, const int centerY,
                   const char* label, const int maxTextW, const bool inverted = false) {
  const std::string visibleLabel = renderer.truncatedText(UI_10_FONT_ID, label, maxTextW);
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  if (inverted) {
    renderer.drawIconInverted(icon, iconX, centerY - kFooterIconSize / 2, kFooterIconSize, kFooterIconSize);
  } else {
    renderer.drawIcon(icon, iconX, centerY - kFooterIconSize / 2, kFooterIconSize, kFooterIconSize);
  }
  renderer.drawText(UI_10_FONT_ID, iconX + kFooterIconSize + kFooterIconTextGap, centerY - lineH / 2,
                    visibleLabel.c_str(), !inverted);
}

void drawRightAlignedIconLabel(const GfxRenderer& renderer, const uint8_t* icon, const int rightX, const int centerY,
                               const char* label, const int maxTextW, const bool inverted = false) {
  const std::string visibleLabel = renderer.truncatedText(UI_10_FONT_ID, label, maxTextW);
  const int labelW = renderer.getTextWidth(UI_10_FONT_ID, visibleLabel.c_str());
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int textX = rightX - labelW;
  const int iconX = textX - kFooterIconTextGap - kFooterIconSize;
  if (inverted) {
    renderer.drawIconInverted(icon, iconX, centerY - kFooterIconSize / 2, kFooterIconSize, kFooterIconSize);
  } else {
    renderer.drawIcon(icon, iconX, centerY - kFooterIconSize / 2, kFooterIconSize, kFooterIconSize);
  }
  renderer.drawText(UI_10_FONT_ID, textX, centerY - lineH / 2, visibleLabel.c_str(), !inverted);
}

void drawLeftAnchoredFooterStat(const GfxRenderer& renderer, const int labelX, const int centerY, const int maxTextW,
                                const char* value, const char* label, const bool inverted = false) {
  const int valueLineH = renderer.getLineHeight(UI_12_FONT_ID);
  const int labelLineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int totalH = valueLineH + kStatsValueLabelGap + labelLineH;
  const int valueW = renderer.getTextWidth(UI_12_FONT_ID, value, EpdFontFamily::BOLD);
  const std::string visibleLabel = renderer.truncatedText(UI_10_FONT_ID, label, maxTextW);
  const int labelW = renderer.getTextWidth(UI_10_FONT_ID, visibleLabel.c_str());
  const int topY = centerY - totalH / 2;
  renderer.drawText(UI_12_FONT_ID, labelX + (labelW - valueW) / 2, topY, value, !inverted, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, labelX, topY + valueLineH + kStatsValueLabelGap, visibleLabel.c_str(), !inverted);
}

void drawRightAnchoredFooterStat(const GfxRenderer& renderer, const int labelRightX, const int centerY,
                                 const int maxTextW, const char* value, const char* label,
                                 const bool inverted = false) {
  const int valueLineH = renderer.getLineHeight(UI_12_FONT_ID);
  const int labelLineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int totalH = valueLineH + kStatsValueLabelGap + labelLineH;
  const int valueW = renderer.getTextWidth(UI_12_FONT_ID, value, EpdFontFamily::BOLD);
  const std::string visibleLabel = renderer.truncatedText(UI_10_FONT_ID, label, maxTextW);
  const int labelW = renderer.getTextWidth(UI_10_FONT_ID, visibleLabel.c_str());
  const int labelX = labelRightX - labelW;
  const int topY = centerY - totalH / 2;
  renderer.drawText(UI_12_FONT_ID, labelX + (labelW - valueW) / 2, topY, value, !inverted, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, labelX, topY + valueLineH + kStatsValueLabelGap, visibleLabel.c_str(), !inverted);
}

void drawFooterStats(const GfxRenderer& renderer, const Rect& coverRect, const GlobalReadingStats* globalStats,
                     const GlobalReadingStats* allDevicesStats, const BookReadingStats* bookStats,
                     const float progressPercent, const uint32_t currentBookWordCount, const bool inverted = false) {
  const int inset = contentInset(renderer);
  const int footerY = renderer.getScreenHeight() - DashboardMetrics::values.buttonHintsHeight - kFooterBottomGap;
  const int centerY = std::max(coverRect.y + coverRect.height + 120, footerY);
  const int maxTextW = std::max(1, renderer.getScreenWidth() / 2 - inset * 2);
  const int iconTextW = renderer.getScreenWidth() / 2 - inset - kFooterIconSize - kFooterIconTextGap;

  const BookReadingStats emptyStats{};
  HomeStatContext ctx = buildHomeStatContext(bookStats != nullptr ? *bookStats : emptyStats, progressPercent,
                                             globalStats);
  ctx.bookWordCount = currentBookWordCount;
  if (allDevicesStats != nullptr) ctx.allStats = allDevicesStats;

  char value[48];
  char label[48];
  int valueFontId = UI_12_FONT_ID;
  // Streak and reader-type are person-level: prefer the merged history.
  const GlobalReadingStats* personStats = allDevicesStats != nullptr ? allDevicesStats : globalStats;

  // Left anchor
  const uint8_t leftKind = SETTINGS.homeFooterLeft;
  if (leftKind == CrossPointSettings::HOME_STAT_STREAK) {
    formatStreakStat(personStats, value, sizeof(value));
    drawIconLabel(renderer, StreakIcon, coverRect.x, centerY, value, iconTextW, inverted);
  } else if (leftKind == CrossPointSettings::HOME_STAT_READER_TYPE) {
    drawIconLabel(renderer, readerTypeIcon(personStats), coverRect.x, centerY, readerTypeLabel(personStats), iconTextW,
                  inverted);
  } else if (formatHomeStat(leftKind, ctx, value, sizeof(value), label, sizeof(label), valueFontId)) {
    drawLeftAnchoredFooterStat(renderer, coverRect.x, centerY, maxTextW, value, label, inverted);
  }

  // Right anchor
  const int rightX = renderer.getScreenWidth() - inset - (gpio.deviceIsX3() ? kPairInwardShiftX3 : 0);
  const uint8_t rightKind = SETTINGS.homeFooterRight;
  if (rightKind == CrossPointSettings::HOME_STAT_STREAK) {
    formatStreakStat(personStats, value, sizeof(value));
    drawRightAlignedIconLabel(renderer, StreakIcon, rightX, centerY, value, iconTextW, inverted);
  } else if (rightKind == CrossPointSettings::HOME_STAT_READER_TYPE) {
    drawRightAlignedIconLabel(renderer, readerTypeIcon(personStats), rightX, centerY, readerTypeLabel(personStats),
                              iconTextW, inverted);
  } else if (formatHomeStat(rightKind, ctx, value, sizeof(value), label, sizeof(label), valueFontId)) {
    drawRightAnchoredFooterStat(renderer, rightX, centerY, maxTextW, value, label, inverted);
  }
}

void drawBookText(const GfxRenderer& renderer, const Rect& coverRect, const RecentBook& book,
                  const char* currentChapterTitle, const bool black = true) {
  const int inset = contentInset(renderer);
  const int textW = renderer.getScreenWidth() - inset * 2;
  const char* title = book.title.empty() ? book.path.c_str() : book.title.c_str();
  auto titleLines = renderer.wrappedText(UI_12_FONT_ID, title, textW, kBookTitleMaxLines, EpdFontFamily::BOLD);
  int textY = coverRect.y + coverRect.height + kTitleTopGap;
  const int titleLineH = renderer.getLineHeight(UI_12_FONT_ID);
  for (const auto& line : titleLines) {
    renderer.drawText(UI_12_FONT_ID, coverRect.x, textY, line.c_str(), black, EpdFontFamily::BOLD);
    textY += titleLineH;
  }

  const char* subtitle =
      (currentChapterTitle != nullptr && currentChapterTitle[0] != '\0') ? currentChapterTitle : book.author.c_str();
  if (subtitle != nullptr && subtitle[0] != '\0') {
    auto subtitleLines = renderer.wrappedText(UI_12_FONT_ID, subtitle, textW, kBookChapterMaxLines);
    int subtitleY = textY + kTitleChapterGap;
    for (const auto& line : subtitleLines) {
      renderer.drawText(UI_12_FONT_ID, coverRect.x, subtitleY, line.c_str(), black);
      subtitleY += titleLineH;
    }
  }
}
}  // namespace

void DashboardTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                         int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                         bool& bufferRestored, const std::function<bool()>& storeCoverBuffer,
                                         const BookReadingStats* stats, const float progressPercent,
                                         const GlobalReadingStats* globalStats, const char* currentChapterTitle,
                                         const uint32_t currentBookWordCount) const {
  (void)selectorIndex;
  (void)bufferRestored;

  const Rect coverRect = coverRectForScreen(renderer, rect);
  if (recentBooks.empty()) {
    renderer.drawRoundedRect(coverRect.x, coverRect.y, coverRect.width, coverRect.height, 1, kCoverCornerRadius, true);
    coverRendered = false;
    coverBufferStored = false;
    return;
  }

  if (!coverRendered) {
    drawBookCover(renderer, coverRect, recentBooks[0], Color::White);
    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  }

  // All-devices totals are read on demand so themes that never select them
  // pay nothing; synced stats live in Duet's small state directory.
  GlobalReadingStats aggregated{};
  const GlobalReadingStats* allStats = nullptr;
  if (globalStats != nullptr && homeStatSelectionWantsAllDevices()) {
    aggregated = GlobalReadingStats::loadAggregated(*globalStats);
    allStats = &aggregated;
  }
  drawDashboardStats(renderer, coverRect, stats, progressPercent, globalStats, allStats, currentBookWordCount);
  drawBookText(renderer, coverRect, recentBooks[0], currentChapterTitle);
  drawFooterStats(renderer, coverRect, globalStats, allStats, stats, progressPercent, currentBookWordCount);
}

void DashboardTheme::drawSleepScreen(const GfxRenderer& renderer, const RecentBook& book, const BookReadingStats* stats,
                                     const GlobalReadingStats* globalStats, const float progressPercent,
                                     const char* currentChapterTitle) const {
  renderer.clearScreen(0x00);

  const Rect contentRect{0, DashboardMetrics::values.homeTopPadding, renderer.getScreenWidth(),
                         DashboardMetrics::values.homeCoverTileHeight};
  const Rect coverRect = coverRectForScreen(renderer, contentRect);
  drawBookCover(renderer, coverRect, book, Color::Black);
  GlobalReadingStats aggregated{};
  const GlobalReadingStats* allStats = nullptr;
  if (globalStats != nullptr && homeStatSelectionWantsAllDevices()) {
    aggregated = GlobalReadingStats::loadAggregated(*globalStats);
    allStats = &aggregated;
  }
  drawDashboardStats(renderer, coverRect, stats, progressPercent, globalStats, allStats, 0, false);
  drawBookText(renderer, coverRect, book, currentChapterTitle, false);
  drawFooterStats(renderer, coverRect, globalStats, allStats, stats, progressPercent, 0, true);
}
