#include "BookStatsView.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "CrossPointSettings.h"
#include "LibraryInsights.h"
#include "MappedInputManager.h"
#include "ReadingLedger.h"
#include "components/CompactHeader.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int kStatsButtonHintTopGap = 10;
constexpr int kStandaloneNoRtcMaxTopCardHeightDivisor = 2;
constexpr int kStandaloneNoRtcMaxVerticalOffset = 32;
constexpr int kPerBookRtcTopCardMaxExtra = 84;

struct StatsLayout {
  int headerHeight;
  int headerDrawHeight;
  int topGap;
  int cardGap;
  int topCardTitleH;
  int topCardH;
  int globalCardH;
  int sectionTitleH;
  int sectionTitleFontId;
  int chartLabelFontId;
  int chartLabelW;
  int barH;
  int barGap;
  int chartTopPadding;
  int chartBottomPadding;
};

constexpr StatsLayout kDefaultLayout = {
    .headerHeight = 78,
    .headerDrawHeight = 67,
    .topGap = 8,
    .cardGap = 26,
    .topCardTitleH = 36,
    .topCardH = 214,
    .globalCardH = 154,
    .sectionTitleH = 34,
    .sectionTitleFontId = UI_10_FONT_ID,
    .chartLabelFontId = UI_10_FONT_ID,
    .chartLabelW = 88,
    .barH = 22,
    .barGap = 12,
    .chartTopPadding = 14,
    .chartBottomPadding = 14,
};

constexpr StatsLayout kCompactLayout = {
    .headerHeight = 67,
    .headerDrawHeight = 67,
    .topGap = 6,
    .cardGap = 8,
    .topCardTitleH = 30,
    .topCardH = 156,
    .globalCardH = 110,
    .sectionTitleH = 30,
    .sectionTitleFontId = UI_10_FONT_ID,
    .chartLabelFontId = SMALL_FONT_ID,
    .chartLabelW = 78,
    .barH = 16,
    .barGap = 8,
    .chartTopPadding = 8,
    .chartBottomPadding = 8,
};

constexpr std::array<StrId, READING_TIME_BUCKET_COUNT> TIME_BUCKET_LABELS = {
    StrId::STR_STATS_MORNING, StrId::STR_STATS_AFTERNOON, StrId::STR_STATS_EVENING, StrId::STR_STATS_NIGHT};
constexpr std::array<StrId, READING_DAY_OF_WEEK_COUNT> DAY_LABELS = {
    StrId::STR_STATS_MON, StrId::STR_STATS_TUE, StrId::STR_STATS_WED, StrId::STR_STATS_THU,
    StrId::STR_STATS_FRI, StrId::STR_STATS_SAT, StrId::STR_STATS_SUN};

const char* dayCountText(const uint16_t days) { return days == 1 ? tr(STR_STATS_DAY) : tr(STR_STATS_DAYS); }

int sectionCardHeight(const GfxRenderer& renderer, const StatsLayout& layout, const int rowCount) {
  if (rowCount <= 0) {
    return layout.sectionTitleH + layout.chartTopPadding + layout.chartBottomPadding;
  }
  const int rowContentH = std::max(renderer.getLineHeight(layout.chartLabelFontId), layout.barH);
  const int rowStride = rowContentH + layout.barGap;
  return layout.sectionTitleH + layout.chartTopPadding + layout.chartBottomPadding + rowContentH +
         (rowCount - 1) * rowStride;
}

bool shouldShowRtcBasedStats() {
#ifdef SIMULATOR
  if (std::getenv("CROSSINK_SIMULATOR_SMOKE_FORCE_RTC") != nullptr) return true;
#endif
  ReadingStatsDateTime now;
  return getCurrentLocalReadingStatsDateTime(now);
}

int noRtcCardBaseHeight(const StatsLayout& layout) { return layout.globalCardH; }

int statsContentHeight(const GfxRenderer& renderer, const StatsLayout& layout, const bool globalPage,
                       const bool showRtcStats) {
  const int topCardH = globalPage ? layout.globalCardH : layout.topCardH;
  if (!showRtcStats) {
    return layout.headerHeight + layout.topGap + topCardH;
  }
  const int timeOfDayH = sectionCardHeight(renderer, layout, static_cast<int>(TIME_BUCKET_LABELS.size()));
  const int dayOfWeekH = sectionCardHeight(renderer, layout, static_cast<int>(DAY_LABELS.size()));
  return layout.headerHeight + layout.topGap + topCardH + layout.cardGap + timeOfDayH + layout.cardGap + dayOfWeekH;
}

int noRtcCombinedContentHeight(const StatsLayout& layout, const bool showAllDevicesStats) {
  const int cardBaseH = noRtcCardBaseHeight(layout);
  return layout.headerHeight + layout.topGap + cardBaseH + layout.cardGap + layout.globalCardH +
         (showAllDevicesStats ? layout.cardGap + layout.globalCardH : 0);
}

int statsBottomInset(const ThemeMetrics& metrics, const bool showButtonHints) {
  return metrics.verticalSpacing + (showButtonHints ? metrics.buttonHintsHeight + kStatsButtonHintTopGap : 0);
}

int statsTabReserve(const ThemeMetrics& metrics, const bool showButtonHints) {
  return showButtonHints ? metrics.tabBarHeight + metrics.verticalSpacing : 0;
}

int statsContentTop(const ThemeMetrics& metrics, const bool showButtonHints) {
  return CompactHeader::contentTop(metrics) + statsTabReserve(metrics, showButtonHints);
}

int perBookRtcTopCardHeight(const StatsLayout& layout, const int extraHeight) {
  return layout.topCardH + std::min(extraHeight, kPerBookRtcTopCardMaxExtra);
}

int globalRtcCardHeightForPerBookRowSpacing(const StatsLayout& layout, const int perBookExtraHeight) {
  constexpr int perBookDataRowCount = 3;
  constexpr int globalDataRowCount = 2;
  const int perBookDataRowH =
      (perBookRtcTopCardHeight(layout, perBookExtraHeight) - layout.topCardTitleH) / perBookDataRowCount;
  return std::max(layout.globalCardH, layout.topCardTitleH + perBookDataRowH * globalDataRowCount);
}

const StatsLayout& getStatsLayout(const GfxRenderer& renderer, const bool globalPage, const bool showButtonHints,
                                  const bool showRtcStats) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int availableHeight = renderer.getScreenHeight() - metrics.topPadding -
                              statsBottomInset(metrics, showButtonHints) - statsTabReserve(metrics, showButtonHints);
  if (statsContentHeight(renderer, kDefaultLayout, globalPage, showRtcStats) <= availableHeight) {
    return kDefaultLayout;
  }
  return kCompactLayout;
}

const StatsLayout& getNoRtcCombinedLayout(const GfxRenderer& renderer, const bool showButtonHints,
                                          const bool showAllDevicesStats) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int availableHeight =
      renderer.getScreenHeight() - metrics.topPadding - statsBottomInset(metrics, showButtonHints);
  if (noRtcCombinedContentHeight(kDefaultLayout, showAllDevicesStats) <= availableHeight) {
    return kDefaultLayout;
  }
  return kCompactLayout;
}

void formatCompactEstimate(const uint32_t seconds, char* buf, const size_t len) {
  if (seconds < 60) {
    snprintf(buf, len, "<1m");
    return;
  }
  const uint32_t minutes = (seconds + 30u) / 60u;
  if (minutes < 60) {
    snprintf(buf, len, "%lum", static_cast<unsigned long>(minutes));
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

bool cachedEstimatedTimeLeft(const BookReadingStats& stats, uint32_t& seconds) {
  if (!stats.hasReliableTimeLeftBasis()) {
    seconds = 0;
    return false;
  }
  seconds = stats.estimatedTimeLeftSeconds;
  return seconds > 0;
}

bool shouldShowTimeLeftCalculating(const BookReadingStats& stats, const float progressPercent) {
  return !stats.isCompleted && progressPercent > 0.0f && progressPercent < 100.0f && !stats.hasReliableTimeLeftBasis();
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

  // Convert remaining reading time into calendar time using the book's average reading seconds per calendar day.
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

uint32_t estimatedWordsRead(const uint32_t totalWords, const float progressPercent) {
  if (totalWords == 0 || progressPercent <= 0.0f) return 0;
  const float clampedProgress = std::clamp(progressPercent, 0.0f, 100.0f) / 100.0f;
  return static_cast<uint32_t>(static_cast<float>(totalWords) * clampedProgress + 0.5f);
}

uint32_t wordsPerMinute(const BookReadingStats& stats, const uint32_t totalWords, const float progressPercent) {
  constexpr uint32_t MIN_WPM_READING_SECONDS = 10;
  if (stats.totalReadingSeconds < MIN_WPM_READING_SECONDS) return 0;
  const uint32_t wordsRead = estimatedWordsRead(totalWords, progressPercent);
  if (wordsRead == 0) return 0;
  return static_cast<uint32_t>((static_cast<uint64_t>(wordsRead) * 60ULL + stats.totalReadingSeconds / 2ULL) /
                               stats.totalReadingSeconds);
}

void formatWordsPerMinute(const BookReadingStats& stats, const uint32_t totalWords, const float progressPercent,
                          char* buf, const size_t len) {
  if (const uint32_t wpm = wordsPerMinute(stats, totalWords, progressPercent); wpm > 0) {
    snprintf(buf, len, "%lu", static_cast<unsigned long>(wpm));
  } else {
    snprintf(buf, len, "-");
  }
}

uint32_t addSaturatedValue(const uint32_t current, const uint32_t value) {
  return UINT32_MAX - current < value ? UINT32_MAX : current + value;
}

uint16_t addSaturatedValue16(const uint16_t current, const uint16_t value) {
  return UINT16_MAX - current < value ? UINT16_MAX : static_cast<uint16_t>(current + value);
}

void drawCenteredLabel(const GfxRenderer& renderer, const int fontId, const int x, const int w, const int y,
                       const char* text, const bool bold = false) {
  const int textWidth = renderer.getTextWidth(fontId, text, bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  renderer.drawText(fontId, x + (w - textWidth) / 2, y, text, true,
                    bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
}

void drawStatCell(const GfxRenderer& renderer, const int x, const int w, const int y, const int h, const char* value,
                  const char* label, const int valueFontOverride = -1) {
  const int innerWidth = std::max(1, w - 10);
  const int valueFont =
      valueFontOverride >= 0
          ? valueFontOverride
          : (renderer.getTextWidth(UI_12_FONT_ID, value, EpdFontFamily::BOLD) <= innerWidth ? UI_12_FONT_ID
                                                                                            : UI_10_FONT_ID);
  const std::string visibleValue = renderer.truncatedText(valueFont, value, innerWidth, EpdFontFamily::BOLD);
  const std::vector<std::string> labelLines = renderer.wrappedText(SMALL_FONT_ID, label, innerWidth, 2);
  const int valueLineH = renderer.getLineHeight(valueFont);
  const int labelLineH = renderer.getLineHeight(SMALL_FONT_ID);
  const int labelHeight = labelLineH * static_cast<int>(labelLines.size());
  const int totalTextH = valueLineH + 4 + labelHeight;
  const int textY = y + (h - totalTextH) / 2;
  drawCenteredLabel(renderer, valueFont, x, w, textY, visibleValue.c_str(), true);
  int labelY = textY + valueLineH + 4;
  for (const auto& line : labelLines) {
    drawCenteredLabel(renderer, SMALL_FONT_ID, x, w, labelY, line.c_str());
    labelY += labelLineH;
  }
}

constexpr float kPi = 3.14159265f;

void drawCircleOutline(GfxRenderer& renderer, const int cx, const int cy, const int radius) {
  const int pointCount = std::max(120, radius * 6);
  for (int i = 0; i < pointCount; ++i) {
    const float angle = static_cast<float>(i) * 2.0f * kPi / static_cast<float>(pointCount);
    renderer.drawPixel(cx + static_cast<int>(std::cos(angle) * radius + 0.5f),
                       cy + static_cast<int>(std::sin(angle) * radius + 0.5f), true);
  }
}

void fillAnnulusDither(GfxRenderer& renderer, const int cx, const int cy, const int outerRadius,
                       const int innerRadius) {
  const int screenWidth = renderer.getScreenWidth();
  for (int y = cy - outerRadius + 1; y < cy + outerRadius; ++y) {
    const int dy = y - cy;
    const int outerSquared = outerRadius * outerRadius - dy * dy;
    if (outerSquared <= 0) continue;
    const int outerX = static_cast<int>(std::sqrt(static_cast<float>(outerSquared)));
    if (std::abs(dy) >= innerRadius) {
      const int left = std::max(0, cx - outerX);
      const int right = std::min(screenWidth - 1, cx + outerX);
      if (right >= left) renderer.fillRectDither(left, y, right - left + 1, 1, Color::LightGray);
      continue;
    }
    const int innerSquared = innerRadius * innerRadius - dy * dy;
    const int innerX = innerSquared > 0 ? static_cast<int>(std::sqrt(static_cast<float>(innerSquared))) : 0;
    const int leftStart = std::max(0, cx - outerX);
    const int leftEnd = std::min(screenWidth - 1, cx - innerX);
    const int rightStart = std::max(0, cx + innerX);
    const int rightEnd = std::min(screenWidth - 1, cx + outerX);
    if (leftEnd >= leftStart) renderer.fillRectDither(leftStart, y, leftEnd - leftStart + 1, 1, Color::LightGray);
    if (rightEnd >= rightStart) renderer.fillRectDither(rightStart, y, rightEnd - rightStart + 1, 1, Color::LightGray);
  }
}

void drawDonutGauge(GfxRenderer& renderer, const int cx, const int cy, const int outerRadius, const int thickness,
                    const float progress, const char* centerText) {
  const int innerRadius = std::max(3, outerRadius - thickness);
  fillAnnulusDither(renderer, cx, cy, outerRadius - 1, innerRadius + 1);
  const float clampedProgress = std::clamp(progress, 0.0f, 1.0f);
  if (clampedProgress > 0.0f) {
    const int outerSquared = (outerRadius - 1) * (outerRadius - 1);
    const int innerSquared = (innerRadius + 1) * (innerRadius + 1);
    const float sweep = clampedProgress * 2.0f * kPi;
    for (int y = cy - outerRadius + 1; y <= cy + outerRadius - 1; ++y) {
      const int dy = y - cy;
      for (int x = cx - outerRadius + 1; x <= cx + outerRadius - 1; ++x) {
        const int dx = x - cx;
        const int radiusSquared = dx * dx + dy * dy;
        if (radiusSquared <= innerSquared || radiusSquared > outerSquared) continue;
        float angle = std::atan2(static_cast<float>(dy), static_cast<float>(dx)) + kPi / 2.0f;
        if (angle < 0.0f) angle += 2.0f * kPi;
        if (angle <= sweep) renderer.drawPixel(x, y, true);
      }
    }
  }
  drawCircleOutline(renderer, cx, cy, outerRadius);
  drawCircleOutline(renderer, cx, cy, innerRadius);
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, centerText, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, cx - textWidth / 2, cy - renderer.getLineHeight(UI_12_FONT_ID) / 2, centerText, true,
                    EpdFontFamily::BOLD);
}

uint32_t journalSecondsForDay(const ReadingJournal* journal, const uint32_t dayIndex, const uint32_t todayDayIndex,
                              const ReadingSessionSnapshot& session) {
  uint32_t seconds = journal ? journal->secondsOnDay(dayIndex) : 0;
  if (dayIndex == todayDayIndex) seconds = addSaturatedValue(seconds, session.readingSeconds);
  return seconds;
}

bool activeOnDay(const ReadingJournal* journal, const GlobalReadingStats& history,
                 const ReadingSessionSnapshot& session, const uint32_t dayIndex, const uint32_t todayDayIndex) {
  return journalSecondsForDay(journal, dayIndex, todayDayIndex, session) > 0 || history.hasReadingOnDay(dayIndex);
}

uint32_t heatmapSecondsForDay(const ReadingJournal* journal, const GlobalReadingStats& history,
                              const ReadingSessionSnapshot& session, const uint32_t dayIndex,
                              const uint32_t todayDayIndex) {
  const uint32_t detailedSeconds = journalSecondsForDay(journal, dayIndex, todayDayIndex, session);
  // Older global history knows that reading occurred, but not how long it lasted.
  return detailedSeconds > 0 || !history.hasReadingOnDay(dayIndex) ? detailedSeconds : 1;
}

uint16_t activeDaysEndingOn(const ReadingJournal* journal, const GlobalReadingStats& history,
                            const ReadingSessionSnapshot& session, const uint32_t dayIndex, const uint16_t days) {
  uint16_t activeDays = 0;
  for (uint16_t offset = 0; offset < days && dayIndex >= offset; ++offset) {
    if (activeOnDay(journal, history, session, dayIndex - offset, dayIndex)) activeDays++;
  }
  return activeDays;
}

ReadingStreakSummary summarizeReadingStreaksImpl(const ReadingJournal* journal, const GlobalReadingStats& history,
                                                 const ReadingSessionSnapshot& session, const uint32_t todayDayIndex,
                                                 const uint16_t lookbackDays) {
  ReadingStreakSummary summary;
  if (lookbackDays == 0) return summary;

  const uint32_t firstDay = todayDayIndex >= lookbackDays - 1u ? todayDayIndex - (lookbackDays - 1u) : 0;
  uint16_t running = 0;
  for (uint32_t dayIndex = firstDay; dayIndex <= todayDayIndex; ++dayIndex) {
    if (activeOnDay(journal, history, session, dayIndex, todayDayIndex)) {
      summary.activeDays = addSaturatedValue16(summary.activeDays, 1);
      running = addSaturatedValue16(running, 1);
      summary.longest = std::max(summary.longest, running);
    } else {
      running = 0;
    }
  }

  uint32_t cursor = todayDayIndex;
  if (!activeOnDay(journal, history, session, cursor, todayDayIndex)) {
    if (cursor == 0 || !activeOnDay(journal, history, session, cursor - 1u, todayDayIndex)) {
      summary.longest = std::max(summary.longest, history.displayLongestReadingStreak());
      return summary;
    }
    cursor--;
  }
  while (summary.current < lookbackDays && activeOnDay(journal, history, session, cursor, todayDayIndex)) {
    summary.current = addSaturatedValue16(summary.current, 1);
    if (cursor == 0 || cursor == firstDay) break;
    cursor--;
  }
  summary.longest = std::max(summary.longest, history.displayLongestReadingStreak());
  summary.longest = std::max(summary.longest, summary.current);
  return summary;
}

void fillHeatCell(GfxRenderer& renderer, const int x, const int y, const int size, const uint32_t seconds,
                  const uint32_t goalSeconds) {
  if (seconds == 0) {
    renderer.drawRect(x, y, size, size);
    return;
  }
  const uint32_t reference = goalSeconds > 0 ? goalSeconds : 3600u;
  if (seconds < reference / 4u) {
    renderer.fillRectDither(x, y, size, size, Color::LightGray);
  } else if (seconds < reference / 2u) {
    renderer.fillRectDither(x, y, size, size, Color::DarkGray);
  } else if (seconds < reference) {
    renderer.fillRectDither(x, y, size, size, Color::DarkGray);
    renderer.drawRect(x, y, size, size, 2, true);
    return;
  } else {
    renderer.fillRect(x, y, size, size, true);
    return;
  }
  renderer.drawRect(x, y, size, size);
}

void drawSectionCard(const GfxRenderer& renderer, const int x, const int y, const int w, const int h, const char* title,
                     const StatsLayout& layout) {
  renderer.drawRect(x, y, w, h);
  renderer.drawLine(x, y + layout.sectionTitleH, x + w, y + layout.sectionTitleH);
  drawCenteredLabel(renderer, layout.sectionTitleFontId, x, w,
                    y + (layout.sectionTitleH - renderer.getLineHeight(layout.sectionTitleFontId)) / 2, title, true);
}

void formatCompactDuration(uint32_t seconds, char* buffer, size_t length);

template <size_t N>
void drawHorizontalBars(GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                        const std::array<uint32_t, N>& values, const std::array<StrId, N>& labels,
                        const StatsLayout& layout) {
  constexpr int labelLeftPadding = 10;
  constexpr int labelRightPadding = 18;
  constexpr int barLeftGap = 8;
  constexpr int valueGap = 8;
  constexpr int rightPadding = 10;
  const uint32_t maxValue = *std::max_element(values.begin(), values.end());
  const int labelLineH = renderer.getLineHeight(layout.chartLabelFontId);
  const int rowContentH = std::max(labelLineH, layout.barH);
  const int baseContentH = layout.sectionTitleH + layout.chartTopPadding + layout.chartBottomPadding + rowContentH +
                           (static_cast<int>(N) - 1) * (rowContentH + layout.barGap);
  const int extraHeight = std::max(0, h - baseContentH);
  const int spacingSlotCount = static_cast<int>(N) + 1;
  const int extraPerSlot = spacingSlotCount > 0 ? extraHeight / spacingSlotCount : 0;
  const int extraRemainder = spacingSlotCount > 0 ? extraHeight % spacingSlotCount : 0;
  const int topPadding = layout.chartTopPadding + extraPerSlot + (extraRemainder > 0 ? 1 : 0);
  const int rowGap = layout.barGap + extraPerSlot;
  const int contentTop = y + layout.sectionTitleH + topPadding;
  const int rowStride = rowContentH + rowGap;
  int maxLabelW = 0;
  for (size_t i = 0; i < N; ++i) {
    maxLabelW = std::max(maxLabelW, renderer.getTextWidth(layout.chartLabelFontId, I18N.get(labels[i])));
  }
  const int labelColumnW = std::max(layout.chartLabelW, labelLeftPadding + maxLabelW + labelRightPadding);
  std::array<std::array<char, 16>, N> valueTexts{};
  int valueColumnW = 0;
  for (size_t i = 0; i < N; ++i) {
    if (values[i] > 0) {
      formatCompactDuration(values[i], valueTexts[i].data(), valueTexts[i].size());
    } else {
      snprintf(valueTexts[i].data(), valueTexts[i].size(), "-");
    }
    valueColumnW = std::max(valueColumnW,
                            renderer.getTextWidth(layout.chartLabelFontId, valueTexts[i].data(), EpdFontFamily::BOLD));
  }
  const int barX = x + labelColumnW + barLeftGap;
  const int barW = std::max(0, w - labelColumnW - barLeftGap - valueGap - valueColumnW - rightPadding);
  for (size_t i = 0; i < N; ++i) {
    const int rowTop = contentTop + static_cast<int>(i) * rowStride;
    const int labelY = rowTop + (rowContentH - labelLineH) / 2;
    const int barY = rowTop + (rowContentH - layout.barH) / 2;
    const bool isMaximum = maxValue > 0 && values[i] == maxValue;
    renderer.drawText(layout.chartLabelFontId, x + labelLeftPadding, labelY, I18N.get(labels[i]), true,
                      isMaximum ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    if (maxValue > 0 && values[i] > 0) {
      const int fillW = std::max(2, static_cast<int>((static_cast<uint64_t>(barW) * values[i]) / maxValue));
      renderer.fillRect(barX, barY, fillW, layout.barH, true);
    }
    const int valueWidth = renderer.getTextWidth(layout.chartLabelFontId, valueTexts[i].data(),
                                                 isMaximum ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    renderer.drawText(layout.chartLabelFontId, x + w - rightPadding - valueWidth, labelY, valueTexts[i].data(), true,
                      isMaximum ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }
}

void drawPerBookStatsCard(GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                          const std::string& bookTitle, const BookReadingStats& stats, const float progressPercent,
                          const bool hasEstimatedTimeLeft, const uint32_t estimatedTimeLeftSeconds,
                          const StatsLayout& layout, const uint32_t bookWordCount) {
  renderer.drawRect(x, y, w, h);
  renderer.drawLine(x, y + layout.topCardTitleH, x + w, y + layout.topCardTitleH);
  const std::string visibleTitle =
      renderer.truncatedText(UI_10_FONT_ID, bookTitle.c_str(), w - 20, EpdFontFamily::BOLD);
  drawCenteredLabel(renderer, UI_10_FONT_ID, x, w,
                    y + (layout.topCardTitleH - renderer.getLineHeight(UI_10_FONT_ID)) / 2, visibleTitle.c_str(), true);

  const bool showRtcStats = shouldShowRtcBasedStats();
  const auto thirdX = [x, w](const int column) { return x + (w * column) / 3; };
  const auto thirdWidth = [w](const int column) { return (w * (column + 1)) / 3 - (w * column) / 3; };
  const int rowCount = showRtcStats ? 3 : 2;
  const int rowH = (h - layout.topCardTitleH) / rowCount;
  char buf[40];

  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(stats.sessionCount));
  drawStatCell(renderer, thirdX(0), thirdWidth(0), y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_SESSIONS_LBL));

  BookReadingStats::formatDuration(stats.totalReadingSeconds, buf, sizeof(buf));
  drawStatCell(renderer, thirdX(1), thirdWidth(1), y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_TIME_LBL));

  if (progressPercent >= 0.0f) {
    snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(progressPercent + 0.5f));
  } else {
    snprintf(buf, sizeof(buf), "-");
  }
  drawStatCell(renderer, thirdX(2), thirdWidth(2), y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_PROGRESS_LBL));

  const uint32_t avgSecs = stats.sessionCount > 0 ? stats.countedSessionSeconds / stats.sessionCount : 0;
  BookReadingStats::formatDuration(avgSecs, buf, sizeof(buf));
  drawStatCell(renderer, thirdX(0), thirdWidth(0), y + layout.topCardTitleH + rowH, rowH, buf,
               tr(STR_STATS_AVG_SESSION_LBL));

  uint32_t fallbackEstimateSeconds = 0;
  uint32_t cachedEstimateSeconds = 0;
  const bool hasCachedEstimate = cachedEstimatedTimeLeft(stats, cachedEstimateSeconds);
  const bool hasFallbackEstimate = fallbackEstimatedTimeLeft(stats, progressPercent, fallbackEstimateSeconds);
  bool showingTimeLeftCalculating = false;
  if (stats.hasReliableTimeLeftBasis() && !stats.isCompleted &&
      (hasEstimatedTimeLeft || hasCachedEstimate || hasFallbackEstimate)) {
    formatCompactEstimate(hasEstimatedTimeLeft ? estimatedTimeLeftSeconds
                          : hasCachedEstimate  ? cachedEstimateSeconds
                                               : fallbackEstimateSeconds,
                          buf, sizeof(buf));
  } else if (shouldShowTimeLeftCalculating(stats, progressPercent)) {
    snprintf(buf, sizeof(buf), "%s", tr(STR_TIME_LEFT_CALCULATING));
    showingTimeLeftCalculating = true;
  } else {
    snprintf(buf, sizeof(buf), "-");
  }
  drawStatCell(renderer, thirdX(1), thirdWidth(1), y + layout.topCardTitleH + rowH, rowH, buf, tr(STR_TIME_LEFT),
               showingTimeLeftCalculating ? SMALL_FONT_ID : -1);

  formatWordsPerMinute(stats, bookWordCount, progressPercent, buf, sizeof(buf));
  drawStatCell(renderer, thirdX(2), thirdWidth(2), y + layout.topCardTitleH + rowH, rowH, buf,
               tr(STR_STATS_PAGES_PER_MIN));

  if (!showRtcStats) {
    return;
  }

  ReadingStatsDateTime today;
  const bool hasToday = getCurrentLocalReadingStatsDateTime(today);
  const ReadingStatsDate endDate = stats.isCompleted && stats.finishedDate.isValid()
                                       ? stats.finishedDate
                                       : (hasToday ? today.date : ReadingStatsDate{});
  const bool hasDaySpan = stats.startDate.isValid() && endDate.isValid();
  const uint16_t daysReading = hasDaySpan ? readingSpanDaysElapsed(stats.startDate, endDate) : 0;
  if (hasDaySpan) {
    snprintf(buf, sizeof(buf), "%u %s", static_cast<unsigned>(daysReading), dayCountText(daysReading));
  } else {
    snprintf(buf, sizeof(buf), "-");
  }
  char startedLabel[32];
  char dateBuf[24];
  formatReadingStatsShortDate(stats.startDate, dateBuf, sizeof(dateBuf));
  snprintf(startedLabel, sizeof(startedLabel), "%s %s", tr(STR_STATS_STARTED), dateBuf);
  drawStatCell(renderer, thirdX(0), thirdWidth(0), y + layout.topCardTitleH + rowH * 2, rowH, buf, startedLabel);

  ReadingStatsDate finishDisplayDate;
  bool finished = stats.isCompleted;
  if (finished) {
    finishDisplayDate = stats.finishedDate;
  } else if (hasToday && (hasEstimatedTimeLeft || hasCachedEstimate || hasFallbackEstimate)) {
    const uint32_t remainingReadingSeconds = hasEstimatedTimeLeft ? estimatedTimeLeftSeconds
                                             : hasCachedEstimate  ? cachedEstimateSeconds
                                                                  : fallbackEstimateSeconds;
    if (!estimateFinishDateFromDailyPace(stats, today, remainingReadingSeconds, finishDisplayDate)) {
      ReadingStatsDateTime estimatedFinish = today;
      addSecondsToReadingStatsDateTime(estimatedFinish, remainingReadingSeconds);
      finishDisplayDate = estimatedFinish.date;
    }
  }
  formatReadingStatsShortDate(finishDisplayDate, buf, sizeof(buf));
  drawStatCell(renderer, thirdX(2), thirdWidth(2), y + layout.topCardTitleH + rowH * 2, rowH, buf,
               finished ? tr(STR_STATS_FINISHED_DATE) : tr(STR_STATS_EST_FINISH_DATE));
}

void drawGlobalStatsCard(GfxRenderer& renderer, const int x, const int y, const int w, const int h, const char* title,
                         const GlobalReadingStats& stats, const StatsLayout& layout) {
  renderer.drawRect(x, y, w, h);
  renderer.drawLine(x, y + layout.topCardTitleH, x + w, y + layout.topCardTitleH);
  const bool showRtcStats = shouldShowRtcBasedStats();
  drawCenteredLabel(renderer, UI_10_FONT_ID, x, w,
                    y + (layout.topCardTitleH - renderer.getLineHeight(UI_10_FONT_ID)) / 2, title, true);

  const int thirdW = w / 3;
  const int halfW = w / 2;
  const int rowH = (h - layout.topCardTitleH) / 2;
  char buf[40];

  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.totalSessions));
  drawStatCell(renderer, x, thirdW, y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_SESSIONS_LBL));

  BookReadingStats::formatDuration(stats.totalReadingSeconds, buf, sizeof(buf));
  drawStatCell(renderer, x + thirdW, thirdW, y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_TIME_LBL));

  // Whole-library historical stats do not yet persist per-book word totals, so avoid relabeling
  // screen-page pace as WPM.
  snprintf(buf, sizeof(buf), "-");
  drawStatCell(renderer, x + thirdW * 2, thirdW, y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_PAGES_PER_MIN));

  const uint32_t avgSecs = stats.totalSessions > 0 ? stats.countedSessionSeconds / stats.totalSessions : 0;
  BookReadingStats::formatDuration(avgSecs, buf, sizeof(buf));
  if (showRtcStats) {
    drawStatCell(renderer, x, thirdW, y + layout.topCardTitleH + rowH, rowH, buf, tr(STR_STATS_AVG_SESSION_LBL));
  } else {
    drawStatCell(renderer, x, halfW, y + layout.topCardTitleH + rowH, rowH, buf, tr(STR_STATS_AVG_SESSION_LBL));
  }

  if (showRtcStats) {
    ReadingStatsDateTime today;
    const bool hasToday = getCurrentLocalReadingStatsDateTime(today);
    const uint16_t currentStreak = hasToday ? stats.currentReadingStreak(&today.date) : 0;
    if (currentStreak > 0) {
      snprintf(buf, sizeof(buf), "%u %s", static_cast<unsigned>(currentStreak), dayCountText(currentStreak));
    } else {
      snprintf(buf, sizeof(buf), "-");
    }
    drawStatCell(renderer, x + thirdW, thirdW, y + layout.topCardTitleH + rowH, rowH, buf,
                 tr(STR_STATS_READING_STREAK_LBL));
  }

  if (stats.completedBooks > 0) {
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.completedBooks));
  } else {
    snprintf(buf, sizeof(buf), "-");
  }
  drawStatCell(renderer, showRtcStats ? x + thirdW * 2 : x + halfW, showRtcStats ? thirdW : halfW,
               y + layout.topCardTitleH + rowH, rowH, buf, tr(STR_STATS_COMPLETED_LBL));
}

void drawDateField(const GfxRenderer& renderer, const int x, const int y, const int w, const char* text,
                   const bool selected) {
  const int h = renderer.getLineHeight(UI_12_FONT_ID) + 10;
  renderer.fillRectDither(x, y, w, h, selected ? Color::LightGray : Color::White);
  renderer.drawRect(x, y, w, h, true);
  if (selected) {
    renderer.drawRect(x + 1, y + 1, w - 2, h - 2, true);
  }
  drawCenteredLabel(renderer, UI_12_FONT_ID, x, w, y + 5, text);
}

struct MetricGrid {
  int x;
  int y;
  int width;
  int cellWidth;
  int rowHeight;
  int contentY;
};

MetricGrid beginMetricGrid(GfxRenderer& renderer, const int rows, const int columns, const char* cardTitle,
                           const bool showButtonHints) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int x = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
  const int top = statsContentTop(metrics, showButtonHints);
  const int bottomInset = statsBottomInset(metrics, showButtonHints);
  const int height = std::max(1, renderer.getScreenHeight() - top - bottomInset);
  const int titleHeight = cardTitle && cardTitle[0] ? 38 : 0;
  const int contentHeight = std::max(1, height - titleHeight);
  renderer.drawRect(x, top, width, height);
  if (titleHeight > 0) {
    renderer.drawLine(x, top + titleHeight, x + width, top + titleHeight);
    const std::string visibleTitle = renderer.truncatedText(UI_10_FONT_ID, cardTitle, width - 20, EpdFontFamily::BOLD);
    drawCenteredLabel(renderer, UI_10_FONT_ID, x, width,
                      top + (titleHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2, visibleTitle.c_str(), true);
  }
  return {x, top, width, width / columns, contentHeight / rows, top + titleHeight};
}

void drawMetric(const GfxRenderer& renderer, const MetricGrid& grid, const int columns, const int index,
                const char* value, const char* label, const int valueFontOverride = -1) {
  const int row = index / columns;
  const int column = index % columns;
  const int x = grid.x + column * grid.cellWidth;
  const int width = column == columns - 1 ? grid.width - grid.cellWidth * column : grid.cellWidth;
  drawStatCell(renderer, x, width, grid.contentY + row * grid.rowHeight, grid.rowHeight, value, label,
               valueFontOverride);
}

void drawStatsButtonHints(GfxRenderer& renderer, const MappedInputManager* mappedInput, const bool showButtonHints,
                          const bool showEditButton, const bool showMoreButton) {
  if (!showButtonHints || !mappedInput) return;
  (void)showEditButton;
  (void)showMoreButton;
  const auto labels = mappedInput->mapLabels(tr(STR_BACK), "", tr(STR_PREVIOUS_TAB), tr(STR_NEXT_TAB));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
}

void addLiveSessionToPeriod(ReadingJournalPeriod& period, const ReadingJournal* journal,
                            const ReadingSessionSnapshot& session, const uint32_t todayDayIndex) {
  if (session.readingSeconds == 0) return;
  period.readingSeconds = addSaturatedValue(period.readingSeconds, session.readingSeconds);
  period.screenPages = addSaturatedValue(period.screenPages, session.screenPages);
  if (session.screenPages > 0) {
    period.sessions = addSaturatedValue16(period.sessions, 1);
  }
  if (!journal || journal->secondsOnDay(todayDayIndex) == 0) {
    period.activeDays = addSaturatedValue16(period.activeDays, 1);
  }
}

void formatCompactDuration(const uint32_t seconds, char* buffer, const size_t length) {
  if (seconds < 60) {
    snprintf(buffer, length, "<1m");
    return;
  }
  const uint32_t hours = seconds / 3600u;
  const uint32_t minutes = (seconds % 3600u) / 60u;
  if (hours == 0) {
    snprintf(buffer, length, "%lum", static_cast<unsigned long>(minutes));
  } else {
    snprintf(buffer, length, "%luh%02lum", static_cast<unsigned long>(hours), static_cast<unsigned long>(minutes));
  }
}

void drawPeriodCard(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                    const char* title, const ReadingJournalPeriod& period) {
  constexpr int titleHeight = 32;
  renderer.drawRect(x, y, width, height);
  renderer.drawLine(x, y + titleHeight, x + width, y + titleHeight);
  drawCenteredLabel(renderer, UI_10_FONT_ID, x, width, y + (titleHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                    title, true);
  const int cellWidth = width / 4;
  const int rowHeight = height - titleHeight;
  char buf[40];
  formatCompactDuration(period.readingSeconds, buf, sizeof(buf));
  drawStatCell(renderer, x, cellWidth, y + titleHeight, rowHeight, buf, tr(STR_STATS_TIME_LBL));
  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(period.sessions));
  drawStatCell(renderer, x + cellWidth, cellWidth, y + titleHeight, rowHeight, buf, tr(STR_STATS_SESSIONS_LBL));
  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(period.activeDays));
  drawStatCell(renderer, x + cellWidth * 2, cellWidth, y + titleHeight, rowHeight, buf,
               tr(STR_STATS_ACTIVE_DAYS_SHORT_LBL));
  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(period.completedBooks));
  drawStatCell(renderer, x + cellWidth * 3, width - cellWidth * 3, y + titleHeight, rowHeight, buf,
               tr(STR_STATS_BOOKS_FINISHED_LBL));
}

void formatSessionStartTime(const uint16_t minuteOfDay, char* buf, const size_t len) {
  const uint8_t hour24 = static_cast<uint8_t>((minuteOfDay / 60u) % 24u);
  const uint8_t minute = static_cast<uint8_t>(minuteOfDay % 60u);
  if (SETTINGS.clockFormat == 1) {
    const uint8_t hour12 = static_cast<uint8_t>(hour24 % 12u == 0 ? 12 : hour24 % 12u);
    snprintf(buf, len, "%u:%02u%s", static_cast<unsigned>(hour12), static_cast<unsigned>(minute),
             hour24 < 12 ? "a" : "p");
  } else {
    snprintf(buf, len, "%02u:%02u", static_cast<unsigned>(hour24), static_cast<unsigned>(minute));
  }
}

void formatInsightValue(const LibraryInsightItem& item, char* buffer, const size_t len) {
  if (item.readingSeconds > 0) {
    BookReadingStats::formatDuration(item.readingSeconds, buffer, len);
  } else {
    snprintf(buffer, len, "%u %s", static_cast<unsigned>(item.books), item.books == 1 ? "book" : "books");
  }
}

void drawInsightListCard(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                         const char* title, const LibraryInsightItem* items, const size_t itemCount,
                         const size_t maxRows) {
  constexpr int titleHeight = 32;
  renderer.drawRect(x, y, width, height);
  renderer.drawLine(x, y + titleHeight, x + width, y + titleHeight);
  drawCenteredLabel(renderer, UI_10_FONT_ID, x, width, y + (titleHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                    title, true);
  const size_t rows = std::min(itemCount, maxRows);
  if (rows == 0) {
    drawCenteredLabel(renderer, SMALL_FONT_ID, x, width,
                      y + titleHeight + (height - titleHeight - renderer.getLineHeight(SMALL_FONT_ID)) / 2,
                      tr(STR_STATS_NOT_ENOUGH_DATA));
    return;
  }

  const int rowHeight = std::max(1, (height - titleHeight) / static_cast<int>(rows));
  for (size_t i = 0; i < rows; ++i) {
    const int rowY = y + titleHeight + static_cast<int>(i) * rowHeight;
    if (i > 0) renderer.drawLine(x + 8, rowY, x + width - 8, rowY);
    char value[32];
    formatInsightValue(items[i], value, sizeof(value));
    const int valueWidth = renderer.getTextWidth(SMALL_FONT_ID, value, EpdFontFamily::BOLD);
    const int availableNameWidth = std::max(1, width - 32 - valueWidth);
    const std::string name = renderer.truncatedText(UI_10_FONT_ID, items[i].name.c_str(), availableNameWidth);
    const int nameY = rowY + (rowHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawText(UI_10_FONT_ID, x + 12, nameY, name.c_str());
    renderer.drawText(SMALL_FONT_ID, x + width - valueWidth - 12,
                      rowY + (rowHeight - renderer.getLineHeight(SMALL_FONT_ID)) / 2, value, true, EpdFontFamily::BOLD);
  }
}
}  // namespace

ReadingStreakSummary summarizeReadingStreaks(const ReadingJournal* journal, const GlobalReadingStats& history,
                                             const ReadingSessionSnapshot& session, const uint32_t todayDayIndex,
                                             const uint16_t lookbackDays) {
  return summarizeReadingStreaksImpl(journal, history, session, todayDayIndex, lookbackDays);
}

void renderStatsTabBar(GfxRenderer& renderer, const char* const* labels, const size_t labelCount,
                       size_t selectedIndex) {
  if (!labels || labelCount == 0) return;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int screenWidth = renderer.getScreenWidth();
  const int tabTop = CompactHeader::headerBottomY(metrics);
  const int tabHeight = metrics.tabBarHeight;
  constexpr int arrowReserve = 22;
  constexpr int horizontalPadding = 9;
  constexpr size_t maxVisibleTabs = 3;
  // A fixed number of slots keeps the tail of the stats tab row readable on
  // the X3, where text-width rounding used to let Device and Dates collide.
  // Sized for the full 33-page pager with headroom.
  const size_t count = std::min(labelCount, maxVisibleTabs * 12);
  if (count == 0) return;
  selectedIndex = std::min(selectedIndex, count - 1);
  const size_t visibleCount = std::min(maxVisibleTabs, count);
  size_t first = selectedIndex > visibleCount / 2 ? selectedIndex - visibleCount / 2 : 0;
  if (first + visibleCount > count) first = count - visibleCount;
  const size_t last = first + visibleCount;
  const int availableWidth = std::max(1, screenWidth - arrowReserve * 2);

  renderer.fillRect(0, tabTop, screenWidth, tabHeight, false);
  renderer.drawLine(0, tabTop, screenWidth - 1, tabTop);
  renderer.drawLine(0, tabTop + tabHeight - 1, screenWidth - 1, tabTop + tabHeight - 1);
  if (first > 0) {
    renderer.drawText(UI_10_FONT_ID, 7, tabTop + (tabHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2, "<", true,
                      EpdFontFamily::BOLD);
  }
  if (last < count) {
    const int arrowWidth = renderer.getTextWidth(UI_10_FONT_ID, ">", EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, screenWidth - arrowWidth - 7,
                      tabTop + (tabHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2, ">", true, EpdFontFamily::BOLD);
  }

  int x = arrowReserve;
  int remainingWidth = availableWidth;
  size_t remainingTabs = visibleCount;
  for (size_t i = first; i < last; ++i) {
    const bool active = i == selectedIndex;
    const int tabWidth = std::max(1, remainingWidth / static_cast<int>(remainingTabs));
    if (active) renderer.fillRectDither(x, tabTop + 1, tabWidth, tabHeight - 2, Color::LightGray);
    if (i > first) renderer.drawLine(x, tabTop + 5, x, tabTop + tabHeight - 6);
    const auto style = active ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const std::string label =
        renderer.truncatedText(UI_10_FONT_ID, labels[i], std::max(1, tabWidth - horizontalPadding * 2), style);
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, label.c_str(), style);
    const int textY = tabTop + (tabHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawText(UI_10_FONT_ID, x + (tabWidth - textWidth) / 2, textY, label.c_str(), true, style);
    if (active) renderer.fillRect(x + 6, tabTop + tabHeight - 4, std::max(0, tabWidth - 12), 2, true);
    x += tabWidth;
    remainingWidth -= tabWidth;
    --remainingTabs;
  }
}

void renderCurrentBookStatsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                const std::string& bookTitle, const BookReadingStats& stats,
                                const ReadingSessionSnapshot& session, const float progressPercent,
                                const bool hasEstimatedTimeLeft, const uint32_t estimatedTimeLeftSeconds,
                                const bool showButtonHints, const bool showEditButton, const bool showMoreButton,
                                const uint32_t bookWordCount) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_CURRENT_BOOK), true);
  const MetricGrid grid = beginMetricGrid(renderer, 5, 3, bookTitle.c_str(), showButtonHints);
  char buf[40];

  const bool hasCurrentSession = session.hasStartedAt || session.readingSeconds > 0 || session.screenPages > 0;
  const uint32_t latestSessionSeconds = hasCurrentSession ? session.readingSeconds : stats.latestSessionReadingSeconds;
  const uint16_t latestSessionPages = hasCurrentSession ? session.screenPages : stats.latestSessionScreenPages;
  BookReadingStats::formatDuration(latestSessionSeconds, buf, sizeof(buf));
  drawMetric(renderer, grid, 3, 0, latestSessionSeconds > 0 ? buf : "-",
             hasCurrentSession ? tr(STR_STATS_CURRENT_SESSION_LBL) : tr(STR_STATS_LATEST_SESSION_LBL));
  if (progressPercent >= 0.0f) {
    snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(progressPercent + 0.5f));
  } else {
    snprintf(buf, sizeof(buf), "-");
  }
  drawMetric(renderer, grid, 3, 1, buf, tr(STR_STATS_PROGRESS_LBL));

  uint32_t fallbackSeconds = 0;
  uint32_t cachedSeconds = 0;
  const bool hasFallback = fallbackEstimatedTimeLeft(stats, progressPercent, fallbackSeconds);
  const bool hasCached = cachedEstimatedTimeLeft(stats, cachedSeconds);
  const uint32_t remainingSeconds = stats.hasReliableTimeLeftBasis() ? hasEstimatedTimeLeft ? estimatedTimeLeftSeconds
                                                                       : hasCached          ? cachedSeconds
                                                                       : hasFallback        ? fallbackSeconds
                                                                                            : 0
                                                                     : 0;
  bool showingTimeLeftCalculating = false;
  if (!stats.isCompleted && remainingSeconds > 0) {
    formatCompactEstimate(remainingSeconds, buf, sizeof(buf));
  } else if (shouldShowTimeLeftCalculating(stats, progressPercent)) {
    snprintf(buf, sizeof(buf), "%s", tr(STR_TIME_LEFT_CALCULATING));
    showingTimeLeftCalculating = true;
  } else {
    snprintf(buf, sizeof(buf), "-");
  }
  drawMetric(renderer, grid, 3, 2, buf, tr(STR_TIME_LEFT), showingTimeLeftCalculating ? SMALL_FONT_ID : -1);

  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(latestSessionPages));
  drawMetric(renderer, grid, 3, 3, latestSessionPages > 0 ? buf : "-",
             hasCurrentSession ? tr(STR_STATS_SESSION_PAGES_LBL) : tr(STR_STATS_LATEST_PAGES_LBL));
  formatWordsPerMinute(stats, bookWordCount, progressPercent, buf, sizeof(buf));
  drawMetric(renderer, grid, 3, 4, buf, tr(STR_STATS_PAGES_PER_MIN));

  ReadingStatsDateTime today;
  const bool hasToday = getCurrentLocalReadingStatsDateTime(today);
  ReadingStatsDate estimatedFinish;
  if (!stats.isCompleted && hasToday && remainingSeconds > 0) {
    if (!estimateFinishDateFromDailyPace(stats, today, remainingSeconds, estimatedFinish)) {
      ReadingStatsDateTime fallbackFinish = today;
      addSecondsToReadingStatsDateTime(fallbackFinish, remainingSeconds);
      estimatedFinish = fallbackFinish.date;
    }
  }
  if (estimatedFinish.isValid()) {
    formatReadingStatsShortDate(estimatedFinish, buf, sizeof(buf));
  } else {
    snprintf(buf, sizeof(buf), "-");
  }
  drawMetric(renderer, grid, 3, 5, buf, tr(STR_STATS_EST_FINISH_DATE));

  BookReadingStats::formatDuration(stats.totalReadingSeconds, buf, sizeof(buf));
  drawMetric(renderer, grid, 3, 6, buf, tr(STR_STATS_TIME_LBL));
  BookReadingStats::formatDuration(stats.countedSessionSeconds, buf, sizeof(buf));
  drawMetric(renderer, grid, 3, 7, stats.countedSessionSeconds > 0 ? buf : "-", tr(STR_STATS_SESSION_TIME_LBL));
  const uint32_t avgSession = stats.sessionCount > 0 ? stats.countedSessionSeconds / stats.sessionCount : 0;
  BookReadingStats::formatDuration(avgSession, buf, sizeof(buf));
  drawMetric(renderer, grid, 3, 8, avgSession > 0 ? buf : "-", tr(STR_STATS_AVG_SESSION_LBL));
  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(stats.sessionCount));
  drawMetric(renderer, grid, 3, 9, stats.sessionCount > 0 ? buf : "-", tr(STR_STATS_SESSIONS_LBL));
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.totalPagesTurned));
  drawMetric(renderer, grid, 3, 10, stats.totalPagesTurned > 0 ? buf : "-", tr(STR_STATS_PAGES_LBL));
  const char* status = stats.isCompleted                                           ? tr(STR_STATS_FINISHED)
                       : (progressPercent > 0.0f || stats.totalReadingSeconds > 0) ? tr(STR_STATS_READING)
                                                                                   : tr(STR_STATS_UNREAD);
  drawMetric(renderer, grid, 3, 11, status, tr(STR_STATS_STATUS_LBL));

  formatReadingStatsShortDate(stats.startDate, buf, sizeof(buf));
  drawMetric(renderer, grid, 3, 12, buf, tr(STR_STATS_START_DATE));
  const ReadingStatsDate spanEnd = stats.isCompleted && stats.finishedDate.isValid()
                                       ? stats.finishedDate
                                       : (hasToday ? today.date : ReadingStatsDate{});
  const uint16_t readingDays =
      stats.startDate.isValid() && spanEnd.isValid() ? readingSpanDaysInclusive(stats.startDate, spanEnd) : 0;
  if (readingDays > 0) {
    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(readingDays));
  } else {
    snprintf(buf, sizeof(buf), "-");
  }
  drawMetric(renderer, grid, 3, 13, buf, tr(STR_STATS_DAYS_SINCE_START_LBL));
  formatReadingStatsShortDate(stats.finishedDate, buf, sizeof(buf));
  drawMetric(renderer, grid, 3, 14, buf, tr(STR_STATS_FINISHED_DATE));
  drawStatsButtonHints(renderer, mappedInput, showButtonHints, showEditButton, showMoreButton);
}

void renderBookProgressGraphPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                 const std::string& bookTitle, const BookReadingStats& stats,
                                 const float progressPercent, const bool hasEstimatedTimeLeft,
                                 const uint32_t estimatedTimeLeftSeconds, const bool showButtonHints,
                                 const bool showMoreButton, const uint32_t bookWordCount) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_BOOK_PROGRESS), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentX = metrics.contentSidePadding;
  const int contentWidth = renderer.getScreenWidth() - contentX * 2;
  const int contentTop = statsContentTop(metrics, showButtonHints) + 4;
  const int contentBottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);
  const int titleLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const std::string visibleTitle =
      renderer.truncatedText(UI_10_FONT_ID, bookTitle.c_str(), contentWidth, EpdFontFamily::BOLD);
  drawCenteredLabel(renderer, UI_10_FONT_ID, contentX, contentWidth, contentTop, visibleTitle.c_str(), true);

  constexpr int metricReserve = 180;
  const int radius = std::clamp((contentBottom - contentTop - titleLineHeight - metricReserve) / 2, 42, 88);
  const int centerY = contentTop + titleLineHeight + 12 + radius;
  const float normalizedProgress = progressPercent >= 0.0f ? progressPercent / 100.0f : 0.0f;
  char progressText[20];
  if (progressPercent >= 0.0f) {
    snprintf(progressText, sizeof(progressText), "%d%%", static_cast<int>(progressPercent + 0.5f));
  } else {
    snprintf(progressText, sizeof(progressText), "-");
  }
  drawDonutGauge(renderer, renderer.getScreenWidth() / 2, centerY, radius, std::max(9, radius / 7), normalizedProgress,
                 progressText);

  const int metricTop = centerY + radius + 14;
  const int metricHeight = std::max(1, contentBottom - metricTop);
  const int cellWidth = contentWidth / 2;
  const int rowHeight = metricHeight / 2;
  renderer.drawRect(contentX, metricTop, contentWidth, metricHeight);
  renderer.drawLine(contentX + cellWidth, metricTop, contentX + cellWidth, contentBottom);
  renderer.drawLine(contentX, metricTop + rowHeight, contentX + contentWidth, metricTop + rowHeight);
  char value[40];
  BookReadingStats::formatDuration(stats.totalReadingSeconds, value, sizeof(value));
  drawStatCell(renderer, contentX, cellWidth, metricTop, rowHeight, value, tr(STR_STATS_TIME_LBL));
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(stats.sessionCount));
  drawStatCell(renderer, contentX + cellWidth, contentWidth - cellWidth, metricTop, rowHeight, value,
               tr(STR_STATS_SESSIONS_LBL));
  snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(stats.totalPagesTurned));
  drawStatCell(renderer, contentX, cellWidth, metricTop + rowHeight, metricHeight - rowHeight, value,
               tr(STR_STATS_PAGES_LBL));
  uint32_t fallbackSeconds = 0;
  uint32_t cachedSeconds = 0;
  const bool hasFallback = fallbackEstimatedTimeLeft(stats, progressPercent, fallbackSeconds);
  const bool hasCached = cachedEstimatedTimeLeft(stats, cachedSeconds);
  const uint32_t remainingSeconds = stats.hasReliableTimeLeftBasis() ? hasEstimatedTimeLeft ? estimatedTimeLeftSeconds
                                                                       : hasCached          ? cachedSeconds
                                                                       : hasFallback        ? fallbackSeconds
                                                                                            : 0
                                                                     : 0;
  bool showingTimeLeftCalculating = false;
  if (!stats.isCompleted && remainingSeconds > 0) {
    formatCompactEstimate(remainingSeconds, value, sizeof(value));
  } else if (shouldShowTimeLeftCalculating(stats, progressPercent)) {
    snprintf(value, sizeof(value), "%s", tr(STR_TIME_LEFT_CALCULATING));
    showingTimeLeftCalculating = true;
  } else {
    snprintf(value, sizeof(value), "-");
  }
  drawStatCell(renderer, contentX + cellWidth, contentWidth - cellWidth, metricTop + rowHeight,
               metricHeight - rowHeight, value, tr(STR_TIME_LEFT), showingTimeLeftCalculating ? SMALL_FONT_ID : -1);
  drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
}

namespace {
// Monday-first weekday index for a journal day (Sakamoto's method on the
// civil date reconstructed from the day index).
int mondayFirstWeekday(const uint32_t dayIndex) {
  ReadingStatsDate date;
  if (!readingStatsDateFromDayIndex(dayIndex, date)) return -1;
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int y = date.year;
  if (date.month < 3) y -= 1;
  const int sunFirst = (y + y / 4 - y / 100 + y / 400 + t[date.month - 1] + date.day) % 7;
  return (sunFirst + 6) % 7;
}

const char* weekdayLabel(const int mondayFirst) {
  switch (mondayFirst) {
    case 0:
      return tr(STR_STATS_MON);
    case 1:
      return tr(STR_STATS_TUE);
    case 2:
      return tr(STR_STATS_WED);
    case 3:
      return tr(STR_STATS_THU);
    case 4:
      return tr(STR_STATS_FRI);
    case 5:
      return tr(STR_STATS_SAT);
    default:
      return tr(STR_STATS_SUN);
  }
}
}  // namespace

namespace {
// Ported from the retired ReadingProfileActivity radar (Codex research fork),
// expanded from its 4-axis diamond to a pair of descriptive 8-axis profiles.
bool radarDitherPixel(const int x, const int y, const Color color) {
  switch (color) {
    case Color::Black:
      return true;
    case Color::DarkGray:
      return (x + y) % 2 == 0;
    case Color::LightGray:
      return x % 2 == 0 && y % 2 == 0;
    default:
      return false;
  }
}

void radarFillPolygon(GfxRenderer& renderer, const int* xPoints, const int* yPoints, const int numPoints,
                      const Color color) {
  if (numPoints < 3) return;
  int minY = yPoints[0];
  int maxY = yPoints[0];
  for (int index = 1; index < numPoints; ++index) {
    minY = std::min(minY, yPoints[index]);
    maxY = std::max(maxY, yPoints[index]);
  }
  minY = std::max(0, minY);
  maxY = std::min(renderer.getScreenHeight() - 1, maxY);
  std::array<int, 8> nodeX = {};
  for (int scanY = minY; scanY <= maxY; ++scanY) {
    int nodes = 0;
    int previous = numPoints - 1;
    for (int index = 0; index < numPoints; ++index) {
      if ((yPoints[index] < scanY && yPoints[previous] >= scanY) ||
          (yPoints[previous] < scanY && yPoints[index] >= scanY)) {
        const int deltaY = yPoints[previous] - yPoints[index];
        if (deltaY != 0 && nodes < static_cast<int>(nodeX.size())) {
          nodeX[static_cast<size_t>(nodes++)] =
              xPoints[index] + (scanY - yPoints[index]) * (xPoints[previous] - xPoints[index]) / deltaY;
        }
      }
      previous = index;
    }
    std::sort(nodeX.begin(), nodeX.begin() + nodes);
    for (int index = 0; index + 1 < nodes; index += 2) {
      const int startX = std::max(0, nodeX[static_cast<size_t>(index)]);
      const int endX = std::min(renderer.getScreenWidth() - 1, nodeX[static_cast<size_t>(index + 1)]);
      for (int x = startX; x <= endX; ++x) {
        if (radarDitherPixel(x, scanY, color)) renderer.drawPixel(x, scanY, true);
      }
    }
  }
}

// Pointy-top octagon unit vectors in thousandths.
constexpr int kRadarUx[8] = {0, 707, 1000, 707, 0, -707, -1000, -707};
constexpr int kRadarUy[8] = {-1000, -707, 0, 707, 1000, 707, 0, -707};

void radarPoint(const int cx, const int cy, const int radius, const int axis, int& outX, int& outY) {
  outX = cx + radius * kRadarUx[axis] / 1000;
  outY = cy + radius * kRadarUy[axis] / 1000;
}

int ratioPercent(const uint64_t numerator, const uint64_t denominator) {
  if (numerator == 0 || denominator == 0) return 0;
  const uint64_t rounded = (numerator * 100u + denominator / 2u) / denominator;
  return static_cast<int>(std::min<uint64_t>(100u, rounded));
}

void formatCompactDuration(const uint64_t seconds, char* out, const size_t outSize) {
  if (seconds >= 3600u) {
    const uint64_t hoursX10 = (seconds + 180u) / 360u;
    snprintf(out, outSize, "%llu.%lluh", static_cast<unsigned long long>(hoursX10 / 10u),
             static_cast<unsigned long long>(hoursX10 % 10u));
  } else if (seconds >= 60u) {
    snprintf(out, outSize, "%llum", static_cast<unsigned long long>((seconds + 30u) / 60u));
  } else {
    snprintf(out, outSize, "%llus", static_cast<unsigned long long>(seconds));
  }
}

void drawEightAxisProfile(GfxRenderer& renderer, const int contentX, const int contentWidth, const int contentTop,
                          const int contentBottom, const std::array<int, 8>& intensities,
                          const std::array<StrId, 8>& labels, const std::array<const char*, 8>& rawValues,
                          const std::array<StrId, 8>& descriptions, const char* sectionHeading, const char* timeframe,
                          const char* footer = nullptr) {
  const int smallLineH = renderer.getLineHeight(SMALL_FONT_ID);
  const int headingLineH = renderer.getLineHeight(UI_10_FONT_ID);
  drawCenteredLabel(renderer, UI_10_FONT_ID, contentX, contentWidth, contentTop, sectionHeading, EpdFontFamily::BOLD);
  drawCenteredLabel(renderer, SMALL_FONT_ID, contentX, contentWidth, contentTop + headingLineH + 2, timeframe);

  const int footerHeight = footer != nullptr && footer[0] != '\0' ? smallLineH + 6 : 0;
  const int metricTextHeight = renderer.getLineHeight(UI_10_FONT_ID) + smallLineH * 2 + 4;
  const int minimumLegendHeight = (metricTextHeight + 8) * 4;
  const int chartTop = contentTop + headingLineH + smallLineH + 28;
  const int maximumRadarDiameter = std::max(100, contentBottom - footerHeight - chartTop - minimumLegendHeight - 36);
  const int radius = std::clamp(std::min({108, contentWidth / 4, maximumRadarDiameter / 2}), 50, 108);
  const int centerX = renderer.getScreenWidth() / 2;
  const int centerY = chartTop + radius + 10;

  std::array<int, 8> guideX{};
  std::array<int, 8> guideY{};
  for (const int pct : {33, 66, 100}) {
    for (int axis = 0; axis < 8; ++axis) {
      radarPoint(centerX, centerY, radius * pct / 100, axis, guideX[static_cast<size_t>(axis)],
                 guideY[static_cast<size_t>(axis)]);
    }
    for (int axis = 0; axis < 8; ++axis) {
      const int next = (axis + 1) % 8;
      renderer.drawLine(guideX[static_cast<size_t>(axis)], guideY[static_cast<size_t>(axis)],
                        guideX[static_cast<size_t>(next)], guideY[static_cast<size_t>(next)]);
    }
  }
  for (int axis = 0; axis < 8; ++axis) {
    int tipX = 0;
    int tipY = 0;
    radarPoint(centerX, centerY, radius, axis, tipX, tipY);
    renderer.drawLine(centerX, centerY, tipX, tipY);
  }

  std::array<int, 8> profileX{};
  std::array<int, 8> profileY{};
  for (int axis = 0; axis < 8; ++axis) {
    radarPoint(centerX, centerY, std::max(4, radius * intensities[static_cast<size_t>(axis)] / 100), axis,
               profileX[static_cast<size_t>(axis)], profileY[static_cast<size_t>(axis)]);
  }
  radarFillPolygon(renderer, profileX.data(), profileY.data(), static_cast<int>(profileX.size()), Color::DarkGray);
  for (int axis = 0; axis < 8; ++axis) {
    const int next = (axis + 1) % 8;
    renderer.drawLine(profileX[static_cast<size_t>(axis)], profileY[static_cast<size_t>(axis)],
                      profileX[static_cast<size_t>(next)], profileY[static_cast<size_t>(next)], 2, true);
  }

  for (int axis = 0; axis < 8; ++axis) {
    int tipX = 0;
    int tipY = 0;
    radarPoint(centerX, centerY, radius, axis, tipX, tipY);
    const char* label = I18N.get(labels[static_cast<size_t>(axis)]);
    const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, label);
    int textX = 0;
    int textY = 0;
    if (kRadarUy[axis] <= -900) {
      textX = tipX - labelWidth / 2;
      textY = tipY - smallLineH - 6;
    } else if (kRadarUy[axis] >= 900) {
      textX = tipX - labelWidth / 2;
      textY = tipY + 7;
    } else {
      textX = kRadarUx[axis] > 0 ? tipX + 7 : tipX - labelWidth - 7;
      textY = tipY - smallLineH / 2;
    }
    textX = std::clamp(textX, contentX, contentX + contentWidth - labelWidth);
    renderer.drawText(SMALL_FONT_ID, textX, textY, label);
  }

  const int legendTop = centerY + radius + 28;
  const int legendBottom = contentBottom - footerHeight - 4;
  const int legendRowHeight = std::max(metricTextHeight + 8, (legendBottom - legendTop) / 4);
  const int colWidth = contentWidth / 2;
  for (int axis = 0; axis < 8; ++axis) {
    const int col = axis % 2;
    const int row = axis / 2;
    const int cellX = contentX + col * colWidth;
    const int cellY = legendTop + row * legendRowHeight + std::max(0, (legendRowHeight - metricTextHeight) / 2);
    renderer.drawText(UI_10_FONT_ID, cellX, cellY, I18N.get(labels[static_cast<size_t>(axis)]), true,
                      EpdFontFamily::BOLD);
    char scoreAndRaw[80];
    snprintf(scoreAndRaw, sizeof(scoreAndRaw), "%d%% | %s", intensities[static_cast<size_t>(axis)],
             rawValues[static_cast<size_t>(axis)]);
    renderer.drawText(SMALL_FONT_ID, cellX, cellY + renderer.getLineHeight(UI_10_FONT_ID) + 2, scoreAndRaw);
    renderer.drawText(SMALL_FONT_ID, cellX, cellY + renderer.getLineHeight(UI_10_FONT_ID) + smallLineH + 4,
                      I18N.get(descriptions[static_cast<size_t>(axis)]));
  }

  if (footerHeight > 0) {
    drawCenteredLabel(renderer, SMALL_FONT_ID, contentX, contentWidth, contentBottom - smallLineH - 2, footer);
  }
}

void drawEightMetricPanels(GfxRenderer& renderer, const int contentX, const int contentWidth, const int contentTop,
                           const int contentBottom, const std::array<StrId, 8>& labels,
                           const std::array<const char*, 8>& values, const std::array<StrId, 8>& descriptions,
                           const std::array<const char*, 8>& comparisons, const char* sectionHeading,
                           const char* timeframe) {
  const int headingLineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int smallLineH = renderer.getLineHeight(SMALL_FONT_ID);
  drawCenteredLabel(renderer, UI_10_FONT_ID, contentX, contentWidth, contentTop, sectionHeading, EpdFontFamily::BOLD);
  drawCenteredLabel(renderer, SMALL_FONT_ID, contentX, contentWidth, contentTop + headingLineH + 2, timeframe);

  const int gridTop = contentTop + headingLineH + smallLineH + 22;
  const int gridBottom = contentBottom - 4;
  const int colWidth = contentWidth / 2;
  const int rowHeight = std::max(88, (gridBottom - gridTop) / 4);
  const int valueLineH = renderer.getLineHeight(UI_12_FONT_ID);
  const int labelLineH = renderer.getLineHeight(UI_10_FONT_ID);

  renderer.drawLine(contentX + colWidth, gridTop, contentX + colWidth, gridTop + rowHeight * 4);
  for (int row = 1; row < 4; ++row) {
    renderer.drawLine(contentX, gridTop + row * rowHeight, contentX + contentWidth, gridTop + row * rowHeight);
  }

  for (int metric = 0; metric < 8; ++metric) {
    const int col = metric % 2;
    const int row = metric / 2;
    const int cellX = contentX + col * colWidth + 8;
    const int cellY = gridTop + row * rowHeight + 7;
    const int textWidth = colWidth - 18;
    const char* metricLabel = I18N.get(labels[static_cast<size_t>(metric)]);
    const int labelFont = renderer.getTextWidth(UI_10_FONT_ID, metricLabel, EpdFontFamily::BOLD) <= textWidth
                              ? UI_10_FONT_ID
                              : SMALL_FONT_ID;
    renderer.drawText(labelFont, cellX, cellY, metricLabel, true, EpdFontFamily::BOLD);
    int valueFont = UI_12_FONT_ID;
    if (renderer.getTextWidth(valueFont, values[static_cast<size_t>(metric)], EpdFontFamily::BOLD) > textWidth) {
      valueFont = UI_10_FONT_ID;
    }
    if (renderer.getTextWidth(valueFont, values[static_cast<size_t>(metric)], EpdFontFamily::BOLD) > textWidth) {
      valueFont = SMALL_FONT_ID;
    }
    renderer.drawText(valueFont, cellX, cellY + labelLineH + 3, values[static_cast<size_t>(metric)], true,
                      EpdFontFamily::BOLD);
    const std::string description =
        renderer.truncatedText(SMALL_FONT_ID, I18N.get(descriptions[static_cast<size_t>(metric)]), textWidth);
    renderer.drawText(SMALL_FONT_ID, cellX, cellY + labelLineH + valueLineH + 7, description.c_str());
    const std::string comparison =
        renderer.truncatedText(SMALL_FONT_ID, comparisons[static_cast<size_t>(metric)], textWidth, EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, cellX, cellY + labelLineH + valueLineH + smallLineH + 10, comparison.c_str(), true,
                      EpdFontFamily::BOLD);
  }
}

struct RecentVarietyTally {
  uint32_t firstDay = 0;
  uint32_t lastDay = UINT32_MAX;
  std::array<uint64_t, 16> keys{};
  uint8_t count = 0;
  bool overflow = false;
};

void tallyRecentVariety(const uint32_t dayIndex, const int32_t secondsDelta, const char* cachePath, const char* title,
                        void* ctx) {
  (void)title;
  auto* tally = static_cast<RecentVarietyTally*>(ctx);
  if (dayIndex < tally->firstDay || dayIndex > tally->lastDay || secondsDelta <= 0 || cachePath[0] == '\0') return;
  const uint64_t key = LibraryInsights::keyForCachePath(cachePath);
  for (uint8_t index = 0; index < tally->count; ++index) {
    if (tally->keys[index] == key) return;
  }
  if (tally->count >= tally->keys.size()) {
    tally->overflow = true;
    return;
  }
  tally->keys[tally->count++] = key;
}

struct ReadingMetricWindow {
  uint16_t days = 0;
  uint16_t activeDays = 0;
  uint16_t activeWeeks = 0;
  uint16_t goalDays = 0;
  uint16_t completedBooks = 0;
  uint32_t seconds = 0;
  uint32_t pages = 0;
  uint32_t sessions = 0;
  uint32_t bestDailyPages = 0;
  uint32_t bestDailySessions = 0;
  uint32_t bestDailyPaceX10 = 0;
};

uint16_t trackedReadingDays(const ReadingJournal* journal, const GlobalReadingStats& history,
                            const ReadingSessionSnapshot& session, const uint32_t today) {
  uint16_t tracked = 0;
  for (uint32_t offset = 0; offset < ReadingJournal::HISTORY_DAYS && today >= offset; ++offset) {
    if (activeOnDay(journal, history, session, today - offset, today)) {
      tracked = static_cast<uint16_t>(offset + 1u);
    }
  }
  return tracked;
}

ReadingMetricWindow collectReadingWindow(const ReadingJournal* journal, const GlobalReadingStats& history,
                                         const ReadingSessionSnapshot& session, const uint32_t today,
                                         const uint16_t startOffset, const uint16_t requestedDays,
                                         const uint32_t goalSeconds) {
  ReadingMetricWindow window;
  bool weekActive = false;
  uint16_t daysInWeek = 0;
  for (uint16_t relative = 0; relative < requestedDays; ++relative) {
    const uint32_t offset = static_cast<uint32_t>(startOffset) + relative;
    if (today < offset) break;
    const uint32_t dayIndex = today - offset;
    const uint32_t daySeconds = journalSecondsForDay(journal, dayIndex, today, session);
    uint32_t dayPages = journal != nullptr ? journal->pagesOnDay(dayIndex) : 0;
    uint32_t daySessions = journal != nullptr ? journal->sessionsOnDay(dayIndex) : 0;
    if (dayIndex == today) {
      dayPages = addSaturatedValue(dayPages, session.screenPages);
      if (session.screenPages > 0) daySessions = addSaturatedValue(daySessions, 1);
    }

    window.days++;
    window.seconds = addSaturatedValue(window.seconds, daySeconds);
    window.pages = addSaturatedValue(window.pages, dayPages);
    window.sessions = addSaturatedValue(window.sessions, daySessions);
    if (journal != nullptr) {
      window.completedBooks =
          addSaturatedValue16(window.completedBooks, journal->periodEndingOn(dayIndex, 1).completedBooks);
    }
    if (daySeconds > 0 || history.hasReadingOnDay(dayIndex)) {
      window.activeDays++;
      weekActive = true;
    }
    if (goalSeconds > 0 && daySeconds >= goalSeconds) window.goalDays++;
    window.bestDailyPages = std::max(window.bestDailyPages, dayPages);
    window.bestDailySessions = std::max(window.bestDailySessions, daySessions);
    if (daySeconds >= 5u * 60u && dayPages >= 3u) {
      const uint32_t paceX10 = static_cast<uint32_t>((static_cast<uint64_t>(dayPages) * 600u) / daySeconds);
      window.bestDailyPaceX10 = std::max(window.bestDailyPaceX10, paceX10);
    }

    daysInWeek++;
    if (daysInWeek == 7) {
      if (weekActive) window.activeWeeks++;
      weekActive = false;
      daysInWeek = 0;
    }
  }
  if (weekActive) window.activeWeeks++;
  return window;
}

RecentVarietyTally collectRecentVariety(const uint32_t firstDay, const uint32_t lastDay) {
  RecentVarietyTally variety;
  variety.firstDay = firstDay;
  variety.lastDay = lastDay;
  ReadingLedger::forEachRecord(&tallyRecentVariety, &variety);
  return variety;
}

void formatSignedDurationDelta(const int64_t deltaSeconds, char* out, const size_t outSize, const char* suffix) {
  if (deltaSeconds == 0) {
    snprintf(out, outSize, "%s", tr(STR_STATS_UNCHANGED));
    return;
  }
  char duration[24];
  BookReadingStats::formatDuration(
      static_cast<uint32_t>(std::min<uint64_t>(static_cast<uint64_t>(std::llabs(deltaSeconds)), UINT32_MAX)), duration,
      sizeof(duration));
  snprintf(out, outSize, "%c%s %s", deltaSeconds > 0 ? '+' : '-', duration, suffix);
}

void formatSignedCountDelta(const int64_t delta, char* out, const size_t outSize, const char* unit,
                            const char* suffix) {
  if (delta == 0) {
    snprintf(out, outSize, "%s", tr(STR_STATS_UNCHANGED));
    return;
  }
  snprintf(out, outSize, "%+lld %s %s", static_cast<long long>(delta), unit, suffix);
}

void formatSignedPaceDelta(const int32_t deltaX10, char* out, const size_t outSize, const char* suffix) {
  if (deltaX10 == 0) {
    snprintf(out, outSize, "%s", tr(STR_STATS_UNCHANGED));
    return;
  }
  const uint32_t magnitude = static_cast<uint32_t>(std::abs(deltaX10));
  snprintf(out, outSize, "%c%lu.%lu %s", deltaX10 > 0 ? '+' : '-', static_cast<unsigned long>(magnitude / 10u),
           static_cast<unsigned long>(magnitude % 10u), suffix);
}
}  // namespace

namespace {
struct BookTitleIdentity {
  uint64_t key = 0;
  uint64_t titleIdentity = 0;
  std::string cachePath;
  std::string title;
};

uint64_t normalizedTitleIdentity(const char* title) {
  if (!title || title[0] == '\0') return 0;
  constexpr uint64_t FNV_OFFSET = 1469598103934665603ULL;
  constexpr uint64_t FNV_PRIME = 1099511628211ULL;
  uint64_t hash = FNV_OFFSET;
  bool added = false;
  for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(title); *cursor != '\0'; ++cursor) {
    unsigned char value = *cursor;
    if (value < 0x80) {
      const bool alpha = (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
      const bool digit = value >= '0' && value <= '9';
      if (!alpha && !digit) continue;
      if (value >= 'A' && value <= 'Z') value = static_cast<unsigned char>(value - 'A' + 'a');
    }
    hash ^= value;
    hash *= FNV_PRIME;
    added = true;
  }
  return added ? hash : 0;
}

void collectBookTitleIdentity(const uint32_t dayIndex, const int32_t secondsDelta, const char* cachePath,
                              const char* title, void* ctx) {
  (void)dayIndex;
  (void)secondsDelta;
  if (cachePath[0] == '\0' || title[0] == '\0') return;
  auto* titles = static_cast<std::vector<BookTitleIdentity>*>(ctx);
  const uint64_t key = LibraryInsights::keyForCachePath(cachePath);
  for (const auto& known : *titles) {
    if (known.key == key) return;
  }
  titles->push_back({key, normalizedTitleIdentity(title), cachePath, title});
}

uint64_t identityForBookKey(const std::vector<BookTitleIdentity>& titles, const uint64_t key) {
  for (const auto& known : titles) {
    if (known.key == key && known.titleIdentity != 0) return known.titleIdentity;
  }
  return key;
}

const char* titleForBookKey(const std::vector<BookTitleIdentity>& titles, const uint64_t key) {
  for (const auto& known : titles) {
    if (known.key == key) return known.title.c_str();
  }
  return nullptr;
}

const char* cachePathForBookKey(const std::vector<BookTitleIdentity>& titles, const uint64_t key) {
  for (const auto& known : titles) {
    if (known.key == key) return known.cachePath.c_str();
  }
  return nullptr;
}

struct FastestEntry {
  uint64_t key = 0;
  uint64_t identity = 0;
  uint16_t days = 0;
  uint32_t seconds = 0;
};

struct FastestCollectBooks {
  std::array<FastestEntry, 5> top{};
  size_t count = 0;
  const std::vector<BookTitleIdentity>* titles = nullptr;
};

struct ReadingDatesCollect {
  const std::vector<BookTitleIdentity>* titles = nullptr;
  std::vector<ReadingDateStatsEntry>* entries = nullptr;
};

struct StartedBooksCollect {
  const std::vector<BookTitleIdentity>* titles = nullptr;
  std::vector<StartedBookStatsEntry>* entries = nullptr;
};

void collectReadingDateStats(const uint64_t key, const BookReadingStats& stats, void* ctx) {
  if (!stats.startDate.isValid() && !stats.finishedDate.isValid() && stats.totalReadingSeconds == 0 &&
      stats.sessionCount == 0 && !stats.isCompleted) {
    return;
  }
  auto* collect = static_cast<ReadingDatesCollect*>(ctx);
  if (!collect->entries) return;
  const uint64_t identity = collect->titles ? identityForBookKey(*collect->titles, key) : key;
  ReadingDateStatsEntry* entry = nullptr;
  for (auto& known : *collect->entries) {
    if (known.key == identity) {
      entry = &known;
      break;
    }
  }
  const char* cachePath = collect->titles ? cachePathForBookKey(*collect->titles, key) : nullptr;
  const char* title = collect->titles ? titleForBookKey(*collect->titles, key) : nullptr;
  if (!entry) {
    collect->entries->push_back({identity, cachePath ? cachePath : "", title ? title : tr(STR_STATS_UNKNOWN_BOOK),
                                 stats.startDate, stats.finishedDate, stats.isCompleted});
    return;
  }

  if (stats.startDate.isValid() &&
      (!entry->startDate.isValid() || compareReadingStatsDate(stats.startDate, entry->startDate) < 0)) {
    entry->startDate = stats.startDate;
  }
  if (stats.finishedDate.isValid() &&
      (!entry->finishedDate.isValid() || compareReadingStatsDate(stats.finishedDate, entry->finishedDate) > 0)) {
    entry->finishedDate = stats.finishedDate;
  }
  entry->completed = entry->completed || stats.isCompleted;
  if (entry->cachePath.empty() && cachePath) entry->cachePath = cachePath;
  if (entry->title == tr(STR_STATS_UNKNOWN_BOOK) && title) entry->title = title;
}

void collectStartedBookStats(const uint64_t key, const BookReadingStats& stats, void* ctx) {
  if (stats.isCompleted || (!stats.startDate.isValid() && stats.totalReadingSeconds == 0 && stats.sessionCount == 0)) {
    return;
  }

  auto* collect = static_cast<StartedBooksCollect*>(ctx);
  if (!collect || !collect->entries) return;
  const char* cachePath = collect->titles ? cachePathForBookKey(*collect->titles, key) : nullptr;
  const char* title = collect->titles ? titleForBookKey(*collect->titles, key) : nullptr;
  const std::string path = cachePath ? cachePath : "";
  const std::string visibleTitle = title ? title : tr(STR_STATS_UNKNOWN_BOOK);

  for (StartedBookStatsEntry& entry : *collect->entries) {
    if ((!path.empty() && entry.path == path) || (path.empty() && entry.title == visibleTitle)) {
      entry.readingSeconds = addSaturatedValue(entry.readingSeconds, stats.totalReadingSeconds);
      entry.sessions = static_cast<uint16_t>(
          std::min<uint32_t>(UINT16_MAX, static_cast<uint32_t>(entry.sessions) + stats.sessionCount));
      return;
    }
  }

  collect->entries->push_back(
      {path, visibleTitle, "", "", stats.totalReadingSeconds, stats.sessionCount, ReadingStatsDate{}});
}

void fastestBookVisit(const uint64_t key, const BookReadingStats& stats, void* ctx) {
  if (!stats.isCompleted || !stats.startDate.isValid() || !stats.finishedDate.isValid()) return;
  const uint16_t days = std::max<uint16_t>(1, readingSpanDaysElapsed(stats.startDate, stats.finishedDate));
  auto* collect = static_cast<FastestCollectBooks*>(ctx);
  const uint64_t identity = collect->titles ? identityForBookKey(*collect->titles, key) : key;
  for (size_t i = 0; i < collect->count; ++i) {
    if (collect->top[i].identity != identity) continue;
    if (days > collect->top[i].days ||
        (days == collect->top[i].days && stats.totalReadingSeconds <= collect->top[i].seconds)) {
      return;
    }
    for (size_t remove = i; remove + 1 < collect->count; ++remove) {
      collect->top[remove] = collect->top[remove + 1];
    }
    collect->count--;
    break;
  }

  size_t insertAt = collect->count;
  for (size_t i = 0; i < collect->count; ++i) {
    if (days < collect->top[i].days) {
      insertAt = i;
      break;
    }
  }
  if (insertAt >= collect->top.size()) return;
  for (size_t i = std::min(collect->count, collect->top.size() - 1); i > insertAt; --i) {
    collect->top[i] = collect->top[i - 1];
  }
  collect->top[insertAt] = FastestEntry{key, identity, days, stats.totalReadingSeconds};
  if (collect->count < collect->top.size()) collect->count++;
}
}  // namespace

std::vector<ReadingDateStatsEntry> loadReadingDateStatsEntries() {
  std::vector<BookTitleIdentity> titles;
  titles.reserve(32);
  ReadingLedger::forEachRecord(&collectBookTitleIdentity, &titles);

  std::vector<ReadingDateStatsEntry> entries;
  entries.reserve(titles.size());
  ReadingDatesCollect collect{&titles, &entries};
  LibraryInsights::forEachDetailedBookStats(&collectReadingDateStats, &collect);
  std::sort(entries.begin(), entries.end(), [](const ReadingDateStatsEntry& lhs, const ReadingDateStatsEntry& rhs) {
    const ReadingStatsDate lhsDate = lhs.completed && lhs.finishedDate.isValid() ? lhs.finishedDate : lhs.startDate;
    const ReadingStatsDate rhsDate = rhs.completed && rhs.finishedDate.isValid() ? rhs.finishedDate : rhs.startDate;
    if (lhsDate.isValid() != rhsDate.isValid()) return lhsDate.isValid();
    if (lhsDate.isValid()) {
      const int dateOrder = compareReadingStatsDate(lhsDate, rhsDate);
      if (dateOrder != 0) return dateOrder > 0;
    }
    return lhs.title < rhs.title;
  });
  return entries;
}

std::vector<StartedBookStatsEntry> loadStartedBookStatsEntries() {
  std::vector<BookTitleIdentity> titles;
  titles.reserve(32);
  ReadingLedger::forEachRecord(&collectBookTitleIdentity, &titles);

  std::vector<StartedBookStatsEntry> entries;
  entries.reserve(titles.size());
  StartedBooksCollect collect{&titles, &entries};
  LibraryInsights::forEachDetailedBookStats(&collectStartedBookStats, &collect);
  std::sort(entries.begin(), entries.end(), [](const StartedBookStatsEntry& lhs, const StartedBookStatsEntry& rhs) {
    if (lhs.readingSeconds != rhs.readingSeconds) return lhs.readingSeconds > rhs.readingSeconds;
    if (lhs.sessions != rhs.sessions) return lhs.sessions > rhs.sessions;
    return lhs.title < rhs.title;
  });
  return entries;
}

void renderFastestReadsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const bool showButtonHints,
                            const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_FASTEST_READS), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentX = metrics.contentSidePadding;
  const int contentWidth = renderer.getScreenWidth() - contentX * 2;
  const int contentTop = statsContentTop(metrics, showButtonHints) + 10;
  const int contentBottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);

  std::vector<BookTitleIdentity> titles;
  titles.reserve(32);
  ReadingLedger::forEachRecord(&collectBookTitleIdentity, &titles);
  FastestCollectBooks collect;
  collect.titles = &titles;
  LibraryInsights::forEachDetailedBookStats(&fastestBookVisit, &collect);

  if (collect.count == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + 40, tr(STR_STATS_FASTEST_EMPTY));
    drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
    return;
  }

  const int rowHeight = std::max(48, std::min(72, (contentBottom - contentTop) / static_cast<int>(collect.count)));
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  int rowY = contentTop + 4;
  char line[96];
  for (size_t i = 0; i < collect.count && rowY + rowHeight <= contentBottom + 8; ++i) {
    const FastestEntry& entry = collect.top[i];
    const char* title = titleForBookKey(titles, entry.key);
    renderer.drawRect(contentX, rowY, contentWidth, rowHeight - 8, i == 0 ? 2 : 1, true);
    snprintf(line, sizeof(line), "%u.", static_cast<unsigned>(i + 1));
    renderer.drawText(UI_10_FONT_ID, contentX + 10, rowY + 8, line, true, EpdFontFamily::BOLD);
    const std::string visibleTitle =
        renderer.truncatedText(UI_10_FONT_ID, title != nullptr ? title : tr(STR_STATS_UNKNOWN_BOOK), contentWidth - 46);
    renderer.drawText(UI_10_FONT_ID, contentX + 34, rowY + 8, visibleTitle.c_str(), true, EpdFontFamily::BOLD);
    char timeText[24];
    BookReadingStats::formatDuration(entry.seconds, timeText, sizeof(timeText));
    snprintf(line, sizeof(line), "%u %s  |  %s", static_cast<unsigned>(entry.days),
             entry.days == 1 ? tr(STR_STATS_DAY) : tr(STR_STATS_DAYS), timeText);
    renderer.drawText(SMALL_FONT_ID, contentX + 34, rowY + 8 + lineH + 4, line);
    rowY += rowHeight;
  }

  drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
}

void renderReaderRadarPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const ReadingJournal* journal,
                           const GlobalReadingStats& history, const ReadingSessionSnapshot& session,
                           const LibraryInsights* insights, const uint8_t goalMinutes, const bool showButtonHints,
                           const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_READER_DNA), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentX = metrics.contentSidePadding;
  const int contentWidth = renderer.getScreenWidth() - contentX * 2;
  const int contentTop = statsContentTop(metrics, showButtonHints) + 10;
  const int contentBottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);

  ReadingStatsDateTime now;
  const bool hasNow = getCurrentLocalReadingStatsDateTime(now);
  const uint32_t today = hasNow ? readingStatsDayIndex(now.date) : 0;
  const uint16_t trackedDays = hasNow ? trackedReadingDays(journal, history, session, today) : 0;
  const uint32_t goalSeconds = std::max<uint32_t>(1u, static_cast<uint32_t>(goalMinutes) * 60u);
  const ReadingMetricWindow tracked =
      hasNow ? collectReadingWindow(journal, history, session, today, 0, trackedDays, goalSeconds)
             : ReadingMetricWindow{};
  const ReadingStreakSummary streaks =
      hasNow ? summarizeReadingStreaks(journal, history, session, today, ReadingJournal::HISTORY_DAYS)
             : ReadingStreakSummary{};

  const uint32_t totalBooks = insights != nullptr && insights->available ? insights->totalBooks : 0;
  const uint32_t finishedBooks = insights != nullptr && insights->available ? insights->finishedBooks : 0;
  const uint32_t startedBooks =
      insights != nullptr && insights->available ? insights->readingBooks + insights->finishedBooks : 0;
  const uint64_t totalSeconds = static_cast<uint64_t>(history.totalReadingSeconds) + session.readingSeconds;
  const uint64_t trackedGoalSeconds = static_cast<uint64_t>(goalSeconds) * tracked.days;
  const std::array<int, 8> intensities = {
      ratioPercent(finishedBooks, totalBooks),
      ratioPercent(startedBooks, totalBooks),
      ratioPercent(finishedBooks, startedBooks),
      ratioPercent(tracked.activeDays, tracked.days),
      ratioPercent(tracked.goalDays, tracked.days),
      ratioPercent(streaks.current, streaks.longest),
      ratioPercent(tracked.activeWeeks, tracked.days == 0 ? 0 : (tracked.days + 6u) / 7u),
      ratioPercent(totalSeconds, trackedGoalSeconds),
  };
  const std::array<StrId, 8> labels = {
      StrId::STR_STATS_AXIS_LIBRARY_COMPLETION, StrId::STR_STATS_AXIS_EXPLORATION,
      StrId::STR_STATS_AXIS_FINISH_RATE,        StrId::STR_STATS_AXIS_HABIT,
      StrId::STR_STATS_AXIS_GOAL_STRENGTH,      StrId::STR_STATS_AXIS_STREAK_STRENGTH,
      StrId::STR_STATS_AXIS_CONSISTENCY,        StrId::STR_STATS_AXIS_VOLUME_STRENGTH,
  };

  char rawLibrary[40];
  snprintf(rawLibrary, sizeof(rawLibrary), "%lu / %lu", static_cast<unsigned long>(finishedBooks),
           static_cast<unsigned long>(totalBooks));
  char rawExploration[40];
  snprintf(rawExploration, sizeof(rawExploration), "%lu / %lu", static_cast<unsigned long>(startedBooks),
           static_cast<unsigned long>(totalBooks));
  char rawFinishRate[40];
  snprintf(rawFinishRate, sizeof(rawFinishRate), "%lu / %lu", static_cast<unsigned long>(finishedBooks),
           static_cast<unsigned long>(startedBooks));
  char rawHabit[40];
  snprintf(rawHabit, sizeof(rawHabit), "%u / %u %s", static_cast<unsigned>(tracked.activeDays),
           static_cast<unsigned>(tracked.days), tr(STR_STATS_DAYS));
  char rawGoal[40];
  snprintf(rawGoal, sizeof(rawGoal), "%u / %u %s", static_cast<unsigned>(tracked.goalDays),
           static_cast<unsigned>(tracked.days), tr(STR_STATS_DAYS));
  char rawStreak[40];
  snprintf(rawStreak, sizeof(rawStreak), "%u / %u %s", static_cast<unsigned>(streaks.current),
           static_cast<unsigned>(streaks.longest), tr(STR_STATS_DAYS));
  char rawConsistency[40];
  const uint16_t trackedWeeks = tracked.days == 0 ? 0 : static_cast<uint16_t>((tracked.days + 6u) / 7u);
  snprintf(rawConsistency, sizeof(rawConsistency), "%u / %u %s", static_cast<unsigned>(tracked.activeWeeks),
           static_cast<unsigned>(trackedWeeks), tr(STR_STATS_ACTIVE_WEEKS));
  char rawVolume[40];
  char trackedTime[24];
  char goalTime[24];
  formatCompactDuration(totalSeconds, trackedTime, sizeof(trackedTime));
  formatCompactDuration(trackedGoalSeconds, goalTime, sizeof(goalTime));
  snprintf(rawVolume, sizeof(rawVolume), "%s/%s", trackedTime, goalTime);
  const std::array<const char*, 8> raws = {
      rawLibrary, rawExploration, rawFinishRate, rawHabit, rawGoal, rawStreak, rawConsistency, rawVolume,
  };
  const std::array<StrId, 8> descriptions = {
      StrId::STR_STATS_FORMULA_LIBRARY_COMPLETION, StrId::STR_STATS_FORMULA_EXPLORATION,
      StrId::STR_STATS_FORMULA_FINISH_RATE,        StrId::STR_STATS_FORMULA_HABIT,
      StrId::STR_STATS_FORMULA_GOAL_STRENGTH,      StrId::STR_STATS_FORMULA_STREAK_STRENGTH,
      StrId::STR_STATS_FORMULA_CONSISTENCY,        StrId::STR_STATS_FORMULA_VOLUME_STRENGTH,
  };

  drawEightAxisProfile(renderer, contentX, contentWidth, contentTop, contentBottom, intensities, labels, raws,
                       descriptions, tr(STR_STATS_READER_DNA_SUBTITLE), tr(STR_STATS_PERCENT_SCALE));

  drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
}

void renderReaderDnaDetailsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                const ReadingJournal* journal, const GlobalReadingStats& history,
                                const ReadingSessionSnapshot& session, const bool showButtonHints,
                                const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_DNA_DETAILS), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentX = metrics.contentSidePadding;
  const int contentWidth = renderer.getScreenWidth() - contentX * 2;
  const int contentTop = statsContentTop(metrics, showButtonHints) + 10;
  const int contentBottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);

  ReadingStatsDateTime now;
  const bool hasNow = getCurrentLocalReadingStatsDateTime(now);
  const uint32_t today = hasNow ? readingStatsDayIndex(now.date) : 0;
  const ReadingMetricWindow current =
      hasNow ? collectReadingWindow(journal, history, session, today, 0, 30, 0) : ReadingMetricWindow{};
  const uint16_t trackedDays = hasNow ? trackedReadingDays(journal, history, session, today) : 0;
  const ReadingMetricWindow tracked =
      hasNow ? collectReadingWindow(journal, history, session, today, 0, trackedDays, 0) : ReadingMetricWindow{};
  const ReadingStreakSummary streaks =
      hasNow ? summarizeReadingStreaks(journal, history, session, today, ReadingJournal::HISTORY_DAYS)
             : ReadingStreakSummary{};
  const uint64_t totalSeconds = static_cast<uint64_t>(history.totalReadingSeconds) + session.readingSeconds;
  const uint64_t totalPages = static_cast<uint64_t>(history.totalPagesTurned) + session.screenPages;
  const uint32_t totalSessions = history.totalSessions + (session.screenPages > 0 ? 1u : 0u);
  const uint32_t countedSeconds =
      history.countedSessionSeconds > 0 ? history.countedSessionSeconds : history.totalReadingSeconds;
  const uint32_t avgSession = totalSessions > 0
                                  ? static_cast<uint32_t>(std::min<uint64_t>(
                                        UINT32_MAX, (static_cast<uint64_t>(countedSeconds) +
                                                     (session.screenPages > 0 ? session.readingSeconds : 0u)) /
                                                        totalSessions))
                                  : 0;
  const uint32_t paceX10 =
      totalSeconds > 60 ? static_cast<uint32_t>(std::min<uint64_t>(UINT32_MAX, totalPages * 600u / totalSeconds)) : 0;
  const uint32_t currentAvgSession = current.sessions > 0 ? current.seconds / current.sessions : 0;
  const uint32_t currentPaceX10 =
      current.seconds > 60 ? static_cast<uint32_t>((static_cast<uint64_t>(current.pages) * 600u) / current.seconds) : 0;

  char valueTime[32];
  BookReadingStats::formatDuration(static_cast<uint32_t>(std::min<uint64_t>(totalSeconds, UINT32_MAX)), valueTime,
                                   sizeof(valueTime));
  char valuePages[32];
  snprintf(valuePages, sizeof(valuePages), "%llu", static_cast<unsigned long long>(totalPages));
  char valueSessions[32];
  snprintf(valueSessions, sizeof(valueSessions), "%lu", static_cast<unsigned long>(totalSessions));
  char valueFinished[32];
  snprintf(valueFinished, sizeof(valueFinished), "%lu", static_cast<unsigned long>(history.completedBooks));
  char valueReadDays[32];
  snprintf(valueReadDays, sizeof(valueReadDays), "%u", static_cast<unsigned>(tracked.activeDays));
  char valueStreak[32];
  snprintf(valueStreak, sizeof(valueStreak), "%u %s", static_cast<unsigned>(streaks.current), tr(STR_STATS_DAYS));
  char valueAvgSession[32];
  BookReadingStats::formatDuration(avgSession, valueAvgSession, sizeof(valueAvgSession));
  char valuePace[32];
  snprintf(valuePace, sizeof(valuePace), "%lu.%lu", static_cast<unsigned long>(paceX10 / 10u),
           static_cast<unsigned long>(paceX10 % 10u));
  const std::array<const char*, 8> values = {
      valueTime, valuePages, valueSessions, valueFinished, valueReadDays, valueStreak, valueAvgSession, valuePace,
  };

  char deltaTime[48];
  char recentDuration[24];
  BookReadingStats::formatDuration(current.seconds, recentDuration, sizeof(recentDuration));
  snprintf(deltaTime, sizeof(deltaTime), "%s: %s", tr(STR_STATS_LAST_30_DAYS), recentDuration);
  char deltaPages[48];
  snprintf(deltaPages, sizeof(deltaPages), "%s: %lu", tr(STR_STATS_LAST_30_DAYS),
           static_cast<unsigned long>(current.pages));
  char deltaSessions[48];
  snprintf(deltaSessions, sizeof(deltaSessions), "%s: %lu", tr(STR_STATS_LAST_30_DAYS),
           static_cast<unsigned long>(current.sessions));
  char deltaFinished[48];
  snprintf(deltaFinished, sizeof(deltaFinished), "%s: %u", tr(STR_STATS_LAST_30_DAYS),
           static_cast<unsigned>(current.completedBooks));
  char deltaReadDays[48];
  snprintf(deltaReadDays, sizeof(deltaReadDays), "%s: %u", tr(STR_STATS_LAST_30_DAYS),
           static_cast<unsigned>(current.activeDays));
  char deltaStreak[48];
  snprintf(deltaStreak, sizeof(deltaStreak), "%s: %u %s", tr(STR_STATS_LONGEST_PREFIX),
           static_cast<unsigned>(streaks.longest), tr(STR_STATS_DAYS));
  char deltaAvgSession[48];
  BookReadingStats::formatDuration(currentAvgSession, recentDuration, sizeof(recentDuration));
  snprintf(deltaAvgSession, sizeof(deltaAvgSession), "%s: %s", tr(STR_STATS_LAST_30_DAYS), recentDuration);
  char deltaPace[48];
  snprintf(deltaPace, sizeof(deltaPace), "%s: %lu.%lu", tr(STR_STATS_LAST_30_DAYS),
           static_cast<unsigned long>(currentPaceX10 / 10u), static_cast<unsigned long>(currentPaceX10 % 10u));
  const std::array<const char*, 8> comparisons = {
      deltaTime, deltaPages, deltaSessions, deltaFinished, deltaReadDays, deltaStreak, deltaAvgSession, deltaPace,
  };
  const std::array<StrId, 8> labels = {
      StrId::STR_STATS_AXIS_TIME,        StrId::STR_STATS_AXIS_PAGES,     StrId::STR_STATS_AXIS_SESSIONS,
      StrId::STR_STATS_AXIS_FINISHED,    StrId::STR_STATS_AXIS_READ_DAYS, StrId::STR_STATS_AXIS_STREAK,
      StrId::STR_STATS_AXIS_AVG_SESSION, StrId::STR_STATS_AXIS_PACE,
  };
  const std::array<StrId, 8> descriptions = {
      StrId::STR_STATS_DESC_TIME,        StrId::STR_STATS_DESC_PAGES,     StrId::STR_STATS_DESC_SESSIONS,
      StrId::STR_STATS_DESC_FINISHED,    StrId::STR_STATS_DESC_READ_DAYS, StrId::STR_STATS_DESC_STREAK,
      StrId::STR_STATS_DESC_AVG_SESSION, StrId::STR_STATS_DESC_PACE,
  };

  drawEightMetricPanels(renderer, contentX, contentWidth, contentTop, contentBottom, labels, values, descriptions,
                        comparisons, tr(STR_STATS_READER_DNA_SUBTITLE), tr(STR_STATS_DNA_DETAILS_TIMEFRAME));
  drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
}

void renderReadingSignaturePage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                const ReadingJournal* journal, const GlobalReadingStats& history,
                                const ReadingSessionSnapshot& session, const uint8_t goalMinutes,
                                const bool showButtonHints, const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_READING_SIGNATURE), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentX = metrics.contentSidePadding;
  const int contentWidth = renderer.getScreenWidth() - contentX * 2;
  const int contentTop = statsContentTop(metrics, showButtonHints) + 10;
  const int contentBottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);

  ReadingStatsDateTime now;
  const bool hasNow = getCurrentLocalReadingStatsDateTime(now);
  const uint32_t today = hasNow ? readingStatsDayIndex(now.date) : 0;
  const uint16_t trackedDays = hasNow ? trackedReadingDays(journal, history, session, today) : 0;
  const uint16_t currentDays = std::min<uint16_t>(30, trackedDays);
  const uint16_t previousDays = trackedDays > currentDays ? std::min<uint16_t>(30, trackedDays - currentDays) : 0;
  const uint32_t goalSeconds = std::max<uint32_t>(1u, static_cast<uint32_t>(goalMinutes) * 60u);
  const ReadingMetricWindow current =
      hasNow ? collectReadingWindow(journal, history, session, today, 0, currentDays, goalSeconds)
             : ReadingMetricWindow{};
  const ReadingMetricWindow previous =
      hasNow ? collectReadingWindow(journal, history, session, today, currentDays, previousDays, goalSeconds)
             : ReadingMetricWindow{};
  const ReadingMetricWindow currentWeek = hasNow ? collectReadingWindow(journal, history, session, today, 0,
                                                                        std::min<uint16_t>(7, trackedDays), goalSeconds)
                                                 : ReadingMetricWindow{};
  const ReadingMetricWindow priorWeek = hasNow && trackedDays > 7
                                            ? collectReadingWindow(journal, history, session, today, 7,
                                                                   std::min<uint16_t>(7, trackedDays - 7), goalSeconds)
                                            : ReadingMetricWindow{};
  const ReadingMetricWindow tracked =
      hasNow ? collectReadingWindow(journal, history, session, today, 0, trackedDays, goalSeconds)
             : ReadingMetricWindow{};
  const uint32_t avgReadDay = current.activeDays > 0 ? current.seconds / current.activeDays : 0;
  const uint32_t paceX10 =
      current.seconds > 60 ? static_cast<uint32_t>((static_cast<uint64_t>(current.pages) * 600u) / current.seconds) : 0;
  const uint32_t currentVarietyStart = currentDays > 0 && today + 1u >= currentDays ? today + 1u - currentDays : 0;
  const uint32_t previousVarietyStart =
      previousDays > 0 && today + 1u >= currentDays + previousDays ? today + 1u - currentDays - previousDays : 0;
  const RecentVarietyTally currentVariety =
      currentDays > 0 ? collectRecentVariety(currentVarietyStart, today) : RecentVarietyTally{};
  const RecentVarietyTally previousVariety =
      previousDays > 0 ? collectRecentVariety(previousVarietyStart, today - currentDays) : RecentVarietyTally{};
  const uint16_t trackedWeeks = tracked.days == 0 ? 0 : static_cast<uint16_t>((tracked.days + 6u) / 7u);
  const uint32_t varietyReference = std::max<uint32_t>(currentVariety.count, previousVariety.count);
  const uint64_t currentGoalSeconds = static_cast<uint64_t>(goalSeconds) * current.days;
  int momentumScore = 0;
  if (currentWeek.seconds > 0 || priorWeek.seconds > 0) {
    momentumScore = priorWeek.seconds == 0 ? 100 : ratioPercent(currentWeek.seconds, priorWeek.seconds);
  }

  const std::array<int, 8> intensities = {
      ratioPercent(current.activeDays, current.days),
      ratioPercent(current.seconds, currentGoalSeconds),
      ratioPercent(current.goalDays, current.activeDays),
      ratioPercent(paceX10, tracked.bestDailyPaceX10),
      momentumScore,
      ratioPercent(currentVariety.count, varietyReference),
      ratioPercent(tracked.activeWeeks, trackedWeeks),
      ratioPercent(avgReadDay, goalSeconds),
  };
  const std::array<StrId, 8> labels = {
      StrId::STR_STATS_AXIS_FREQUENCY,   StrId::STR_STATS_AXIS_VOLUME,      StrId::STR_STATS_AXIS_FOCUS,
      StrId::STR_STATS_AXIS_PACE,        StrId::STR_STATS_AXIS_MOMENTUM,    StrId::STR_STATS_AXIS_VARIETY,
      StrId::STR_STATS_AXIS_CONSISTENCY, StrId::STR_STATS_AXIS_DAILY_DEPTH,
  };

  char rawFrequency[40];
  snprintf(rawFrequency, sizeof(rawFrequency), "%u / %u %s", static_cast<unsigned>(current.activeDays),
           static_cast<unsigned>(current.days), tr(STR_STATS_DAYS));
  char rawVolume[40];
  char volumeGoal[24];
  formatCompactDuration(current.seconds, rawVolume, sizeof(rawVolume));
  formatCompactDuration(currentGoalSeconds, volumeGoal, sizeof(volumeGoal));
  char rawVolumeRatio[48];
  snprintf(rawVolumeRatio, sizeof(rawVolumeRatio), "%s/%s", rawVolume, volumeGoal);
  char rawFocusRatio[48];
  snprintf(rawFocusRatio, sizeof(rawFocusRatio), "%u/%u %s", static_cast<unsigned>(current.goalDays),
           static_cast<unsigned>(current.activeDays), tr(STR_STATS_DAYS));
  char rawPace[40];
  snprintf(rawPace, sizeof(rawPace), "%lu.%lu / %lu.%lu", static_cast<unsigned long>(paceX10 / 10u),
           static_cast<unsigned long>(paceX10 % 10u), static_cast<unsigned long>(tracked.bestDailyPaceX10 / 10u),
           static_cast<unsigned long>(tracked.bestDailyPaceX10 % 10u));
  char rawMomentum[48];
  char currentWeekTime[24];
  char priorWeekTime[24];
  formatCompactDuration(currentWeek.seconds, currentWeekTime, sizeof(currentWeekTime));
  formatCompactDuration(priorWeek.seconds, priorWeekTime, sizeof(priorWeekTime));
  snprintf(rawMomentum, sizeof(rawMomentum), "%s/%s", currentWeekTime, priorWeekTime);
  char rawVariety[40];
  snprintf(rawVariety, sizeof(rawVariety), "%u%s / %lu", static_cast<unsigned>(currentVariety.count),
           currentVariety.overflow ? "+" : "", static_cast<unsigned long>(varietyReference));
  char rawConsistency[40];
  snprintf(rawConsistency, sizeof(rawConsistency), "%u / %u %s", static_cast<unsigned>(tracked.activeWeeks),
           static_cast<unsigned>(trackedWeeks), tr(STR_STATS_ACTIVE_WEEKS));
  char rawDepth[40];
  char depthGoal[24];
  formatCompactDuration(avgReadDay, rawDepth, sizeof(rawDepth));
  formatCompactDuration(goalSeconds, depthGoal, sizeof(depthGoal));
  char rawDepthRatio[48];
  snprintf(rawDepthRatio, sizeof(rawDepthRatio), "%s/%s", rawDepth, depthGoal);
  const std::array<const char*, 8> raws = {
      rawFrequency, rawVolumeRatio, rawFocusRatio, rawPace, rawMomentum, rawVariety, rawConsistency, rawDepthRatio,
  };
  const std::array<StrId, 8> descriptions = {
      StrId::STR_STATS_FORMULA_FREQUENCY,   StrId::STR_STATS_FORMULA_VOLUME,      StrId::STR_STATS_FORMULA_FOCUS,
      StrId::STR_STATS_FORMULA_PACE,        StrId::STR_STATS_FORMULA_MOMENTUM,    StrId::STR_STATS_FORMULA_VARIETY,
      StrId::STR_STATS_FORMULA_CONSISTENCY, StrId::STR_STATS_FORMULA_DAILY_DEPTH,
  };

  size_t bestTime = 0;
  size_t bestWeekday = 0;
  for (size_t index = 1; index < history.timeOfDaySeconds.size(); ++index) {
    if (history.timeOfDaySeconds[index] > history.timeOfDaySeconds[bestTime]) bestTime = index;
  }
  for (size_t index = 1; index < history.dayOfWeekSeconds.size(); ++index) {
    if (history.dayOfWeekSeconds[index] > history.dayOfWeekSeconds[bestWeekday]) bestWeekday = index;
  }
  char footer[64];
  snprintf(footer, sizeof(footer), "%s: %s / %s", tr(STR_STATS_PEAK_RHYTHM), I18N.get(TIME_BUCKET_LABELS[bestTime]),
           weekdayLabel(static_cast<int>(bestWeekday)));
  drawEightAxisProfile(renderer, contentX, contentWidth, contentTop, contentBottom, intensities, labels, raws,
                       descriptions, tr(STR_STATS_SIGNATURE_SUBTITLE), tr(STR_STATS_PERCENT_SCALE), footer);

  drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
}

void renderReadingSignatureDetailsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                       const ReadingJournal* journal, const GlobalReadingStats& history,
                                       const ReadingSessionSnapshot& session, const uint8_t goalMinutes,
                                       const bool showButtonHints, const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_SIGNATURE_DETAILS), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentX = metrics.contentSidePadding;
  const int contentWidth = renderer.getScreenWidth() - contentX * 2;
  const int contentTop = statsContentTop(metrics, showButtonHints) + 10;
  const int contentBottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);

  ReadingStatsDateTime now;
  const bool hasNow = getCurrentLocalReadingStatsDateTime(now);
  const uint32_t today = hasNow ? readingStatsDayIndex(now.date) : 0;
  const uint16_t trackedDays = hasNow ? trackedReadingDays(journal, history, session, today) : 0;
  const uint16_t currentDays = std::min<uint16_t>(30, trackedDays);
  const uint16_t previousDays = trackedDays > currentDays ? std::min<uint16_t>(30, trackedDays - currentDays) : 0;
  const uint32_t goalSeconds = std::max<uint32_t>(1u, static_cast<uint32_t>(goalMinutes) * 60u);
  const ReadingMetricWindow current =
      hasNow ? collectReadingWindow(journal, history, session, today, 0, currentDays, goalSeconds)
             : ReadingMetricWindow{};
  const ReadingMetricWindow previous =
      hasNow ? collectReadingWindow(journal, history, session, today, currentDays, previousDays, goalSeconds)
             : ReadingMetricWindow{};
  const ReadingMetricWindow currentWeek =
      hasNow ? collectReadingWindow(journal, history, session, today, 0, std::min<uint16_t>(7, trackedDays), 0)
             : ReadingMetricWindow{};
  const ReadingMetricWindow priorWeek =
      hasNow && trackedDays > 7
          ? collectReadingWindow(journal, history, session, today, 7, std::min<uint16_t>(7, trackedDays - 7), 0)
          : ReadingMetricWindow{};
  const uint32_t currentAvgReadDay = current.activeDays > 0 ? current.seconds / current.activeDays : 0;
  const uint32_t previousAvgReadDay = previous.activeDays > 0 ? previous.seconds / previous.activeDays : 0;
  const uint32_t currentPaceX10 =
      current.seconds > 60 ? static_cast<uint32_t>((static_cast<uint64_t>(current.pages) * 600u) / current.seconds) : 0;
  const uint32_t previousPaceX10 =
      previous.seconds > 60 ? static_cast<uint32_t>((static_cast<uint64_t>(previous.pages) * 600u) / previous.seconds)
                            : 0;
  const uint32_t currentVarietyStart = currentDays > 0 && today + 1u >= currentDays ? today + 1u - currentDays : 0;
  const uint32_t previousVarietyStart =
      previousDays > 0 && today + 1u >= currentDays + previousDays ? today + 1u - currentDays - previousDays : 0;
  const RecentVarietyTally currentVariety =
      currentDays > 0 ? collectRecentVariety(currentVarietyStart, today) : RecentVarietyTally{};
  const RecentVarietyTally previousVariety =
      previousDays > 0 ? collectRecentVariety(previousVarietyStart, today - currentDays) : RecentVarietyTally{};

  char valueFrequency[32];
  snprintf(valueFrequency, sizeof(valueFrequency), "%u / %u %s", static_cast<unsigned>(current.activeDays),
           static_cast<unsigned>(current.days), tr(STR_STATS_DAYS));
  char valueVolume[32];
  BookReadingStats::formatDuration(current.seconds, valueVolume, sizeof(valueVolume));
  char valueFocus[32];
  snprintf(valueFocus, sizeof(valueFocus), "%u / %u %s", static_cast<unsigned>(current.goalDays),
           static_cast<unsigned>(current.activeDays), tr(STR_STATS_DAYS));
  char valuePace[32];
  snprintf(valuePace, sizeof(valuePace), "%lu.%lu", static_cast<unsigned long>(currentPaceX10 / 10u),
           static_cast<unsigned long>(currentPaceX10 % 10u));
  char valueMomentum[32];
  if (currentWeek.seconds > priorWeek.seconds) {
    snprintf(valueMomentum, sizeof(valueMomentum), "%s", tr(STR_STATS_READING_TIME_RISING));
  } else if (currentWeek.seconds < priorWeek.seconds) {
    snprintf(valueMomentum, sizeof(valueMomentum), "%s", tr(STR_STATS_READING_TIME_FALLING));
  } else {
    snprintf(valueMomentum, sizeof(valueMomentum), "%s", tr(STR_STATS_READING_TIME_STEADY));
  }
  char valueVariety[32];
  snprintf(valueVariety, sizeof(valueVariety), "%u%s %s", static_cast<unsigned>(currentVariety.count),
           currentVariety.overflow ? "+" : "", tr(STR_STATS_BOOKS_SHORT));
  char valueConsistency[32];
  snprintf(valueConsistency, sizeof(valueConsistency), "%u / %u %s", static_cast<unsigned>(current.activeWeeks),
           static_cast<unsigned>(current.days == 0 ? 0 : (current.days + 6u) / 7u), tr(STR_STATS_WEEKS_SHORT));
  char valueDepth[32];
  BookReadingStats::formatDuration(currentAvgReadDay, valueDepth, sizeof(valueDepth));
  const std::array<const char*, 8> values = {
      valueFrequency, valueVolume, valueFocus, valuePace, valueMomentum, valueVariety, valueConsistency, valueDepth,
  };

  char deltaFrequency[48];
  formatSignedCountDelta(static_cast<int64_t>(current.activeDays) - previous.activeDays, deltaFrequency,
                         sizeof(deltaFrequency), tr(STR_STATS_DAYS), tr(STR_STATS_VS_PRIOR_30_DAYS));
  char deltaVolume[48];
  formatSignedDurationDelta(static_cast<int64_t>(current.seconds) - previous.seconds, deltaVolume, sizeof(deltaVolume),
                            tr(STR_STATS_VS_PRIOR_30_DAYS));
  char deltaFocus[48];
  formatSignedCountDelta(static_cast<int64_t>(current.goalDays) - previous.goalDays, deltaFocus, sizeof(deltaFocus),
                         tr(STR_STATS_DAYS), tr(STR_STATS_VS_PRIOR_30_DAYS));
  char deltaPace[48];
  if (previous.seconds <= 60 || previous.pages == 0) {
    snprintf(deltaPace, sizeof(deltaPace), "%s", tr(STR_STATS_NOT_ENOUGH_DATA));
  } else {
    formatSignedPaceDelta(static_cast<int32_t>(currentPaceX10) - static_cast<int32_t>(previousPaceX10), deltaPace,
                          sizeof(deltaPace), tr(STR_STATS_VS_PRIOR_30_DAYS));
  }
  char deltaMomentum[48];
  formatSignedDurationDelta(static_cast<int64_t>(currentWeek.seconds) - priorWeek.seconds, deltaMomentum,
                            sizeof(deltaMomentum), tr(STR_STATS_VS_PRIOR_WEEK));
  char deltaVariety[48];
  formatSignedCountDelta(static_cast<int64_t>(currentVariety.count) - previousVariety.count, deltaVariety,
                         sizeof(deltaVariety), tr(STR_STATS_BOOKS_SHORT), tr(STR_STATS_VS_PRIOR_30_DAYS));
  char deltaConsistency[48];
  formatSignedCountDelta(static_cast<int64_t>(current.activeWeeks) - previous.activeWeeks, deltaConsistency,
                         sizeof(deltaConsistency), tr(STR_STATS_WEEKS_SHORT), tr(STR_STATS_VS_PRIOR_30_DAYS));
  char deltaDepth[48];
  formatSignedDurationDelta(static_cast<int64_t>(currentAvgReadDay) - previousAvgReadDay, deltaDepth,
                            sizeof(deltaDepth), tr(STR_STATS_VS_PRIOR_30_DAYS));
  const std::array<const char*, 8> comparisons = {
      deltaFrequency, deltaVolume, deltaFocus, deltaPace, deltaMomentum, deltaVariety, deltaConsistency, deltaDepth,
  };
  const std::array<StrId, 8> labels = {
      StrId::STR_STATS_AXIS_FREQUENCY,   StrId::STR_STATS_AXIS_VOLUME,      StrId::STR_STATS_AXIS_FOCUS,
      StrId::STR_STATS_AXIS_PACE,        StrId::STR_STATS_AXIS_MOMENTUM,    StrId::STR_STATS_AXIS_VARIETY,
      StrId::STR_STATS_AXIS_CONSISTENCY, StrId::STR_STATS_AXIS_DAILY_DEPTH,
  };
  const std::array<StrId, 8> descriptions = {
      StrId::STR_STATS_DESC_FREQUENCY,   StrId::STR_STATS_DESC_VOLUME,      StrId::STR_STATS_DESC_FOCUS,
      StrId::STR_STATS_DESC_PACE,        StrId::STR_STATS_DESC_MOMENTUM,    StrId::STR_STATS_DESC_VARIETY,
      StrId::STR_STATS_DESC_CONSISTENCY, StrId::STR_STATS_DESC_DAILY_DEPTH,
  };

  drawEightMetricPanels(renderer, contentX, contentWidth, contentTop, contentBottom, labels, values, descriptions,
                        comparisons, tr(STR_STATS_SIGNATURE_SUBTITLE), tr(STR_STATS_SIGNATURE_DETAILS_TIMEFRAME));
  drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
}

void renderYearLinePage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const ReadingJournal* journal,
                        const ReadingSessionSnapshot& session, const bool showButtonHints, const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_YEAR_LINE), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentX = metrics.contentSidePadding;
  const int contentWidth = renderer.getScreenWidth() - contentX * 2;
  const int contentTop = statsContentTop(metrics, showButtonHints) + 10;
  const int contentBottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);

  ReadingStatsDateTime now;
  const bool hasNow = getCurrentLocalReadingStatsDateTime(now);
  const uint32_t today = hasNow ? readingStatsDayIndex(now.date) : 0;

  constexpr uint32_t weeks = 52;
  constexpr uint32_t daysPerWeek = 7;
  constexpr uint32_t lookbackDays = weeks * daysPerWeek;
  uint64_t yearSeconds = 0;
  uint64_t pagesTotal = 0;
  uint16_t activeWeeks = 0;
  std::array<uint32_t, weeks> weeklySeconds{};
  for (uint32_t offset = lookbackDays; offset-- > 0 && hasNow;) {
    if (today < offset) continue;
    const uint32_t dayIndex = today - offset;
    const uint32_t seconds = journalSecondsForDay(journal, dayIndex, today, session);
    const uint32_t weekIndex = (lookbackDays - 1 - offset) / daysPerWeek;
    weeklySeconds[weekIndex] = static_cast<uint32_t>(
        std::min<uint64_t>(static_cast<uint64_t>(weeklySeconds[weekIndex]) + seconds, UINT32_MAX));
    yearSeconds += seconds;
    uint32_t pages = journal != nullptr ? journal->pagesOnDay(dayIndex) : 0;
    if (dayIndex == today) pages += session.screenPages;
    pagesTotal += pages;
  }
  uint32_t bestWeekSeconds = 0;
  for (const uint32_t seconds : weeklySeconds) {
    if (seconds > 0) activeWeeks++;
    bestWeekSeconds = std::max(bestWeekSeconds, seconds);
  }
  constexpr std::array<uint8_t, 5> trendWeights = {1, 2, 3, 2, 1};
  std::array<uint32_t, weeks> plottedSeconds{};
  uint32_t smoothedPeak = 0;
  for (int week = 0; week < static_cast<int>(weeks); ++week) {
    uint64_t weightedSeconds = 0;
    uint32_t weightTotal = 0;
    for (int offset = -2; offset <= 2; ++offset) {
      const int sourceWeek = week + offset;
      if (sourceWeek < 0 || sourceWeek >= static_cast<int>(weeks)) continue;
      const uint8_t weight = trendWeights[static_cast<size_t>(offset + 2)];
      weightedSeconds += static_cast<uint64_t>(weeklySeconds[static_cast<size_t>(sourceWeek)]) * weight;
      weightTotal += weight;
    }
    plottedSeconds[static_cast<size_t>(week)] =
        weightTotal > 0 ? static_cast<uint32_t>(weightedSeconds / weightTotal) : 0;
    smoothedPeak = std::max(smoothedPeak, plottedSeconds[static_cast<size_t>(week)]);
  }
  if (smoothedPeak > 0 && bestWeekSeconds > 0) {
    for (uint32_t& seconds : plottedSeconds) {
      seconds =
          static_cast<uint32_t>((static_cast<uint64_t>(seconds) * bestWeekSeconds + smoothedPeak / 2) / smoothedPeak);
    }
  }
  const uint32_t maxSeconds = std::max<uint32_t>(60, bestWeekSeconds);

  constexpr int summaryHeight = 128;
  constexpr int xLabelHeight = 28;
  const int summaryTop = std::max(contentTop + 120, contentBottom - summaryHeight);
  const int chartLeft = contentX + 36;
  const int chartRight = contentX + contentWidth;
  const int chartLabelY = contentTop + 2;
  const int chartLabelHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int chartTop = chartLabelY + chartLabelHeight * 2 + 8;
  const int chartBottom = summaryTop - xLabelHeight;
  const int chartHeight = std::max(20, chartBottom - chartTop);
  const int plotWidth = std::max(14, chartRight - chartLeft);
  renderer.drawLine(chartLeft, chartTop, chartLeft, chartBottom);
  renderer.drawLine(chartLeft, chartBottom, chartRight, chartBottom);

  char scaleText[20];
  formatCompactEstimate(maxSeconds, scaleText, sizeof(scaleText));
  renderer.drawText(SMALL_FONT_ID, contentX, chartTop - chartLabelHeight - 3, scaleText);
  const char* chartLabel = tr(STR_STATS_WEEKLY_READING_TIME);
  renderer.drawText(SMALL_FONT_ID,
                    chartLeft + (plotWidth - renderer.getTextWidth(SMALL_FONT_ID, chartLabel, EpdFontFamily::BOLD)) / 2,
                    chartLabelY, chartLabel, true, EpdFontFamily::BOLD);

  struct ChartPoint {
    int x = 0;
    int y = 0;
  };
  std::array<ChartPoint, weeks> weeklyPoints{};
  for (uint32_t i = 0; i < weeks; ++i) {
    const int x = chartLeft + static_cast<int>((static_cast<uint64_t>(plotWidth) * i) / (weeks - 1));
    const int y = chartBottom - static_cast<int>((static_cast<uint64_t>(chartHeight) * plottedSeconds[i]) / maxSeconds);
    weeklyPoints[i] = {x, y};
  }

  std::array<float, weeks> tangents{};
  for (size_t i = 1; i + 1 < weeklyPoints.size(); ++i) {
    const float incoming = static_cast<float>(weeklyPoints[i].y - weeklyPoints[i - 1].y);
    const float outgoing = static_cast<float>(weeklyPoints[i + 1].y - weeklyPoints[i].y);
    if ((incoming > 0.0f && outgoing > 0.0f) || (incoming < 0.0f && outgoing < 0.0f)) {
      tangents[i] = (incoming + outgoing) * 0.5f;
    }
  }
  constexpr int curveSteps = 5;
  ChartPoint previous = weeklyPoints[0];
  for (size_t segment = 0; segment + 1 < weeklyPoints.size(); ++segment) {
    const ChartPoint& start = weeklyPoints[segment];
    const ChartPoint& end = weeklyPoints[segment + 1];
    for (int step = 1; step <= curveSteps; ++step) {
      const float t = static_cast<float>(step) / curveSteps;
      const float t2 = t * t;
      const float t3 = t2 * t;
      const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
      const float h10 = t3 - 2.0f * t2 + t;
      const float h01 = -2.0f * t3 + 3.0f * t2;
      const float h11 = t3 - t2;
      ChartPoint point;
      point.x = start.x + static_cast<int>((end.x - start.x) * t);
      point.y = std::clamp(
          static_cast<int>(h00 * start.y + h10 * tangents[segment] + h01 * end.y + h11 * tangents[segment + 1]),
          chartTop, chartBottom);
      renderer.drawLine(previous.x, previous.y, point.x, point.y);
      previous = point;
    }
  }

  const int xLabelY = chartBottom + 4;
  renderer.drawText(SMALL_FONT_ID, chartLeft, xLabelY, tr(STR_STATS_52_WEEKS_AGO));
  const char* midpointLabel = tr(STR_STATS_26_WEEKS_AGO);
  renderer.drawText(SMALL_FONT_ID, chartLeft + plotWidth / 2 - renderer.getTextWidth(SMALL_FONT_ID, midpointLabel) / 2,
                    xLabelY, midpointLabel);
  const char* nowLabel = tr(STR_STATS_NOW);
  renderer.drawText(SMALL_FONT_ID, chartRight - renderer.getTextWidth(SMALL_FONT_ID, nowLabel), xLabelY, nowLabel);

  renderer.drawRect(contentX, summaryTop, contentWidth, summaryHeight);
  const int halfWidth = contentWidth / 2;
  const int rowHeight = summaryHeight / 2;
  renderer.drawLine(contentX + halfWidth, summaryTop, contentX + halfWidth, summaryTop + summaryHeight);
  renderer.drawLine(contentX, summaryTop + rowHeight, contentX + contentWidth, summaryTop + rowHeight);
  char value[40];
  BookReadingStats::formatDuration(static_cast<uint32_t>(std::min<uint64_t>(yearSeconds, UINT32_MAX)), value,
                                   sizeof(value));
  drawStatCell(renderer, contentX, halfWidth, summaryTop, rowHeight, value, tr(STR_STATS_TIME_LBL));
  snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(pagesTotal));
  drawStatCell(renderer, contentX + halfWidth, contentWidth - halfWidth, summaryTop, rowHeight, value,
               tr(STR_STATS_PAGES_TURNED));
  BookReadingStats::formatDuration(bestWeekSeconds, value, sizeof(value));
  drawStatCell(renderer, contentX, halfWidth, summaryTop + rowHeight, summaryHeight - rowHeight, value,
               tr(STR_STATS_BEST_WEEK));
  snprintf(value, sizeof(value), "%u / %u", static_cast<unsigned>(activeWeeks), static_cast<unsigned>(weeks));
  drawStatCell(renderer, contentX + halfWidth, contentWidth - halfWidth, summaryTop + rowHeight,
               summaryHeight - rowHeight, value, tr(STR_STATS_ACTIVE_WEEKS_LABEL));

  drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
}

void renderSessionLengthsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                              const ReadingJournal* journal, const ReadingSessionSnapshot& session,
                              const bool showButtonHints, const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_SESSION_LENGTHS), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentX = metrics.contentSidePadding;
  const int contentWidth = renderer.getScreenWidth() - contentX * 2;
  const int contentTop = statsContentTop(metrics, showButtonHints) + 10;
  const int contentBottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);

  ReadingStatsDateTime now;
  const bool hasNow = getCurrentLocalReadingStatsDateTime(now);
  const uint32_t today = hasNow ? readingStatsDayIndex(now.date) : 0;

  // Average session length per day over the last 90 days, bucketed. The
  // journal keeps daily aggregates rather than full session history.
  const char* labels[4] = {tr(STR_STATS_BUCKET_SHORT), tr(STR_STATS_BUCKET_QUARTER), tr(STR_STATS_BUCKET_HALF),
                           tr(STR_STATS_BUCKET_LONG)};
  std::array<uint16_t, 4> bucketDays{};
  uint16_t sampledDays = 0;
  for (uint32_t offset = 0; offset < 90 && hasNow && today >= offset; ++offset) {
    const uint32_t dayIndex = today - offset;
    const uint32_t seconds = journalSecondsForDay(journal, dayIndex, today, session);
    const uint32_t sessions = journal != nullptr ? journal->sessionsOnDay(dayIndex) : 0;
    if (seconds == 0 || sessions == 0) continue;
    const uint32_t avg = seconds / sessions;
    sampledDays++;
    if (avg < 15u * 60u) {
      bucketDays[0]++;
    } else if (avg < 30u * 60u) {
      bucketDays[1]++;
    } else if (avg < 60u * 60u) {
      bucketDays[2]++;
    } else {
      bucketDays[3]++;
    }
  }
  uint16_t maxDays = 1;
  size_t best = 0;
  for (size_t i = 0; i < 4; ++i) {
    if (bucketDays[i] > bucketDays[best]) best = i;
    maxDays = std::max(maxDays, bucketDays[i]);
  }

  constexpr int summaryHeight = 112;
  constexpr int xLabelHeight = 28;
  const int summaryTop = std::max(contentTop + 120, contentBottom - summaryHeight);
  const int chartLeft = contentX + 36;
  const int chartRight = contentX + contentWidth;
  const int scaleY = contentTop + 4;
  const int chartTop = scaleY + renderer.getLineHeight(SMALL_FONT_ID) + 8;
  const int chartBottom = summaryTop - xLabelHeight;
  const int chartHeight = std::max(20, chartBottom - chartTop);
  const int plotWidth = std::max(14, chartRight - chartLeft);
  renderer.drawLine(chartLeft, chartTop, chartLeft, chartBottom);
  renderer.drawLine(chartLeft, chartBottom, chartRight, chartBottom);

  char scaleText[20];
  snprintf(scaleText, sizeof(scaleText), "%u", static_cast<unsigned>(maxDays));
  renderer.drawText(SMALL_FONT_ID, contentX, scaleY, scaleText);

  const int slotWidth = std::max(1, plotWidth / 4);
  const int barWidth = std::max(10, slotWidth - 18);
  for (size_t i = 0; i < 4; ++i) {
    const int barX = chartLeft + static_cast<int>(i) * slotWidth + (slotWidth - barWidth) / 2;
    const int barHeight =
        bucketDays[i] > 0
            ? std::max(2, static_cast<int>((static_cast<uint64_t>(chartHeight) * bucketDays[i]) / maxDays))
            : 0;
    if (barHeight > 0) {
      if (i == best) {
        renderer.fillRect(barX, chartBottom - barHeight, barWidth, barHeight, true);
      } else {
        renderer.fillRectDither(barX, chartBottom - barHeight, barWidth, barHeight, Color::DarkGray);
        renderer.drawRect(barX, chartBottom - barHeight, barWidth, barHeight);
      }
    }
    const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);
    renderer.drawText(SMALL_FONT_ID, barX + (barWidth - labelWidth) / 2, chartBottom + 6, labels[i]);
  }

  renderer.drawRect(contentX, summaryTop, contentWidth, summaryHeight);
  const int halfWidth = contentWidth / 2;
  char value[40];
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(sampledDays));
  drawStatCell(renderer, contentX, halfWidth, summaryTop, summaryHeight, value, tr(STR_STATS_DAYS_SAMPLED));
  drawStatCell(renderer, contentX + halfWidth, contentWidth - halfWidth, summaryTop, summaryHeight, labels[best],
               tr(STR_STATS_TYPICAL_SESSION));

  drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
}

void renderStreakMilestonesPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                const ReadingJournal* journal, const GlobalReadingStats& history,
                                const ReadingSessionSnapshot& session, const bool showButtonHints,
                                const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_STREAK_MILESTONES), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentX = metrics.contentSidePadding;
  const int contentWidth = renderer.getScreenWidth() - contentX * 2;
  const int contentTop = statsContentTop(metrics, showButtonHints) + 10;
  const int contentBottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);

  ReadingStatsDateTime now;
  const bool hasNow = getCurrentLocalReadingStatsDateTime(now);
  const uint32_t today = hasNow ? readingStatsDayIndex(now.date) : 0;
  const ReadingStreakSummary streaks =
      hasNow ? summarizeReadingStreaks(journal, history, session, today, ReadingJournal::HISTORY_DAYS)
             : ReadingStreakSummary{};
  const uint16_t activeDays7 = hasNow ? activeDaysEndingOn(journal, history, session, today, 7) : 0;
  const uint16_t activeDays30 = hasNow ? activeDaysEndingOn(journal, history, session, today, 30) : 0;
  uint16_t activeDaysYear = 0;
  for (uint32_t offset = 0; offset < 366 && hasNow && today >= offset; ++offset) {
    if (activeOnDay(journal, history, session, today - offset, today)) activeDaysYear++;
  }

  const int cardTop = contentTop + 6;
  const int upperHeight = 96;
  renderer.drawRoundedRect(contentX, cardTop, contentWidth, upperHeight, 2, 10, true);
  const int halfWidth = contentWidth / 2;
  char value[40];
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(streaks.current));
  drawStatCell(renderer, contentX, halfWidth, cardTop, upperHeight, value, tr(STR_STATS_CURRENT_STREAK));
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(streaks.longest));
  drawStatCell(renderer, contentX + halfWidth, contentWidth - halfWidth, cardTop, upperHeight, value,
               tr(STR_STATS_LONGEST_STREAK));

  constexpr std::array<uint16_t, 49> milestones = {
      3,   7,   14,  21,  30,  45,  60,  75,  90,  100, 120, 135, 150, 165,  180,  200, 220,
      240, 260, 280, 300, 320, 340, 365, 380, 395, 410, 425, 440, 455, 470,  485,  500, 550,
      600, 650, 700, 730, 750, 775, 800, 825, 850, 875, 900, 925, 950, 1000, 1095,
  };
  uint16_t nextMilestone = milestones.back();
  bool foundNextMilestone = false;
  for (const uint16_t candidate : milestones) {
    if (candidate > streaks.current) {
      nextMilestone = candidate;
      foundNextMilestone = true;
      break;
    }
  }
  if (!foundNextMilestone) {
    constexpr std::array<uint16_t, 4> futureMilestones = {1250, 1460, 1825, 3650};
    nextMilestone = futureMilestones.back();
    for (const uint16_t candidate : futureMilestones) {
      if (candidate > streaks.current) {
        nextMilestone = candidate;
        break;
      }
    }
  }

  constexpr int summaryHeight = 82;
  constexpr int nextCardHeight = 70;
  const int summaryTop = contentBottom - summaryHeight;
  const int nextCardTop = summaryTop - nextCardHeight - 12;
  const int ladderTop = cardTop + upperHeight + 16;
  constexpr int milestoneColumns = 7;
  constexpr int milestoneRows = 7;
  const int stepWidth = contentWidth / milestoneColumns;
  renderer.drawText(UI_10_FONT_ID, contentX, ladderTop - 4, tr(STR_STATS_MILESTONES), true, EpdFontFamily::BOLD);
  const int gridTop = ladderTop + renderer.getLineHeight(UI_10_FONT_ID) + 8;
  const int rowStride = std::max(32, (nextCardTop - gridTop - 8) / milestoneRows);
  const int boxSize = std::clamp(std::min(stepWidth - 6, rowStride - 4), 30, 36);
  for (size_t i = 0; i < milestones.size(); ++i) {
    const int col = static_cast<int>(i % milestoneColumns);
    const int row = static_cast<int>(i / milestoneColumns);
    const int boxX = contentX + col * stepWidth + (stepWidth - boxSize) / 2;
    const int boxY = gridTop + row * rowStride;
    const bool reached = streaks.longest >= milestones[i];
    if (reached) {
      renderer.fillRoundedRect(boxX, boxY, boxSize, boxSize, 6, Color::Black);
    } else {
      renderer.drawRoundedRect(boxX, boxY, boxSize, boxSize, 1, 6, true);
    }
    char label[8];
    if (milestones[i] == 1000) {
      snprintf(label, sizeof(label), "1K");
    } else if (milestones[i] == 1095) {
      snprintf(label, sizeof(label), "3Y");
    } else {
      snprintf(label, sizeof(label), "%u", static_cast<unsigned>(milestones[i]));
    }
    const int labelFont = UI_10_FONT_ID;
    const int labelWidth = renderer.getTextWidth(labelFont, label);
    renderer.drawText(labelFont, boxX + (boxSize - labelWidth) / 2,
                      boxY + (boxSize - renderer.getLineHeight(labelFont)) / 2, label, !reached);
  }

  renderer.drawRoundedRect(contentX, nextCardTop, contentWidth, nextCardHeight, 1, 8, true);
  char nextLabel[64];
  snprintf(nextLabel, sizeof(nextLabel), "%s: %u %s", tr(STR_STATS_NEXT_MILESTONE),
           static_cast<unsigned>(nextMilestone), tr(STR_STATS_DAYS));
  renderer.drawText(UI_10_FONT_ID, contentX + 10, nextCardTop + 8, nextLabel, true, EpdFontFamily::BOLD);
  char progressLabel[32];
  snprintf(progressLabel, sizeof(progressLabel), "%u / %u", static_cast<unsigned>(streaks.current),
           static_cast<unsigned>(nextMilestone));
  const int progressLabelWidth = renderer.getTextWidth(SMALL_FONT_ID, progressLabel);
  renderer.drawText(SMALL_FONT_ID, contentX + contentWidth - progressLabelWidth - 10, nextCardTop + 10, progressLabel);
  const int progressX = contentX + 10;
  const int progressY = nextCardTop + nextCardHeight - 22;
  const int progressWidth = contentWidth - 20;
  renderer.drawRect(progressX, progressY, progressWidth, 10);
  const int filledWidth = std::clamp(
      static_cast<int>((static_cast<uint32_t>(streaks.current) * progressWidth) / std::max<uint16_t>(1, nextMilestone)),
      0, progressWidth);
  if (filledWidth > 0) renderer.fillRect(progressX, progressY, filledWidth, 10, true);

  renderer.drawRect(contentX, summaryTop, contentWidth, summaryHeight);
  const int thirdWidth = contentWidth / 3;
  renderer.drawLine(contentX + thirdWidth, summaryTop, contentX + thirdWidth, contentBottom);
  renderer.drawLine(contentX + thirdWidth * 2, summaryTop, contentX + thirdWidth * 2, contentBottom);
  snprintf(value, sizeof(value), "%u / 7", static_cast<unsigned>(activeDays7));
  drawStatCell(renderer, contentX, thirdWidth, summaryTop, summaryHeight, value, tr(STR_STATS_LAST_7_DAYS));
  snprintf(value, sizeof(value), "%u / 30", static_cast<unsigned>(activeDays30));
  drawStatCell(renderer, contentX + thirdWidth, thirdWidth, summaryTop, summaryHeight, value,
               tr(STR_STATS_LAST_30_DAYS));
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(activeDaysYear));
  drawStatCell(renderer, contentX + thirdWidth * 2, contentWidth - thirdWidth * 2, summaryTop, summaryHeight, value,
               tr(STR_STATS_THIS_YEAR));

  drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
}

namespace {
struct StartFinishTally {
  int nowMonth = 0;
  int nowYear = 0;
  std::array<uint8_t, 12> started{};
  std::array<uint8_t, 12> finished{};
  const std::vector<BookTitleIdentity>* titles = nullptr;
  std::vector<uint64_t> seenIdentities;
};

int startFinishSlotFor(const StartFinishTally& tally, const ReadingStatsDate& date) {
  if (!date.isValid()) return -1;
  const int monthsBack = (tally.nowYear - date.year) * 12 + (tally.nowMonth - date.month);
  if (monthsBack < 0 || monthsBack > 11) return -1;
  return 11 - monthsBack;
}

void tallyStartFinish(const uint64_t key, const BookReadingStats& stats, void* ctx) {
  auto* tally = static_cast<StartFinishTally*>(ctx);
  const uint64_t identity = tally->titles ? identityForBookKey(*tally->titles, key) : key;
  if (std::find(tally->seenIdentities.begin(), tally->seenIdentities.end(), identity) != tally->seenIdentities.end()) {
    return;
  }
  tally->seenIdentities.push_back(identity);
  const int startSlot = startFinishSlotFor(*tally, stats.startDate);
  if (startSlot >= 0 && tally->started[static_cast<size_t>(startSlot)] < UINT8_MAX) {
    tally->started[static_cast<size_t>(startSlot)]++;
  }
  if (stats.isCompleted) {
    const int finishSlot = startFinishSlotFor(*tally, stats.finishedDate);
    if (finishSlot >= 0 && tally->finished[static_cast<size_t>(finishSlot)] < UINT8_MAX) {
      tally->finished[static_cast<size_t>(finishSlot)]++;
    }
  }
}
}  // namespace

void renderStartedFinishedPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const bool showButtonHints,
                               const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_STARTED_FINISHED), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentX = metrics.contentSidePadding;
  const int contentWidth = renderer.getScreenWidth() - contentX * 2;
  const int contentTop = statsContentTop(metrics, showButtonHints) + 10;
  const int contentBottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);

  ReadingStatsDateTime now;
  StartFinishTally tally;
  uint32_t totalStarted = 0;
  uint32_t totalFinished = 0;
  if (getCurrentLocalReadingStatsDateTime(now)) {
    std::vector<BookTitleIdentity> titles;
    titles.reserve(32);
    ReadingLedger::forEachRecord(&collectBookTitleIdentity, &titles);
    tally.nowMonth = now.date.month;
    tally.nowYear = now.date.year;
    tally.titles = &titles;
    tally.seenIdentities.reserve(titles.size());
    LibraryInsights::forEachDetailedBookStats(&tallyStartFinish, &tally);
    for (size_t i = 0; i < 12; ++i) {
      totalStarted += tally.started[i];
      totalFinished += tally.finished[i];
    }
  }

  uint8_t maxCount = 1;
  for (size_t i = 0; i < 12; ++i) {
    maxCount = std::max(maxCount, std::max(tally.started[i], tally.finished[i]));
  }

  constexpr int summaryHeight = 112;
  constexpr int xLabelHeight = 28;
  const int summaryTop = std::max(contentTop + 120, contentBottom - summaryHeight);
  const int chartLeft = contentX + 36;
  const int chartRight = contentX + contentWidth;
  const int scaleY = contentTop + 4;
  const int chartTop = scaleY + renderer.getLineHeight(SMALL_FONT_ID) + 8;
  const int chartBottom = summaryTop - xLabelHeight;
  const int chartHeight = std::max(20, chartBottom - chartTop);
  const int plotWidth = std::max(14, chartRight - chartLeft);
  renderer.drawLine(chartLeft, chartTop, chartLeft, chartBottom);
  renderer.drawLine(chartLeft, chartBottom, chartRight, chartBottom);

  char scaleText[20];
  snprintf(scaleText, sizeof(scaleText), "%u", static_cast<unsigned>(maxCount));
  renderer.drawText(SMALL_FONT_ID, contentX, scaleY, scaleText);

  // Per month: outlined bar = started, solid bar = finished.
  const int slotWidth = std::max(2, plotWidth / 12);
  const int pairWidth = std::max(4, slotWidth - 6);
  const int barWidth = std::max(2, pairWidth / 2 - 1);
  for (size_t i = 0; i < 12; ++i) {
    const int slotX = chartLeft + static_cast<int>(i) * slotWidth + (slotWidth - pairWidth) / 2;
    const int startedHeight =
        tally.started[i] > 0
            ? std::max(2, static_cast<int>((static_cast<uint64_t>(chartHeight) * tally.started[i]) / maxCount))
            : 0;
    if (startedHeight > 0) {
      renderer.drawRect(slotX, chartBottom - startedHeight, barWidth, startedHeight);
    }
    const int finishedHeight =
        tally.finished[i] > 0
            ? std::max(2, static_cast<int>((static_cast<uint64_t>(chartHeight) * tally.finished[i]) / maxCount))
            : 0;
    if (finishedHeight > 0) {
      renderer.fillRect(slotX + barWidth + 2, chartBottom - finishedHeight, barWidth, finishedHeight, true);
    }
    if (i % 2 == 1 || i == 11) {
      int month = tally.nowMonth - (11 - static_cast<int>(i));
      while (month <= 0) month += 12;
      char monthLabel[6];
      snprintf(monthLabel, sizeof(monthLabel), "%d", month);
      const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, monthLabel);
      renderer.drawText(SMALL_FONT_ID, slotX + (pairWidth - labelWidth) / 2, chartBottom + 6, monthLabel);
    }
  }

  renderer.drawRect(contentX, summaryTop, contentWidth, summaryHeight);
  const int halfWidth = contentWidth / 2;
  char value[40];
  snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(totalStarted));
  drawStatCell(renderer, contentX, halfWidth, summaryTop, summaryHeight, value, tr(STR_STATS_STARTED_12MO));
  snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(totalFinished));
  drawStatCell(renderer, contentX + halfWidth, contentWidth - halfWidth, summaryTop, summaryHeight, value,
               tr(STR_STATS_FINISHED_12MO));

  drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
}

void renderReadingDatesPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                            const std::vector<ReadingDateStatsEntry>& books, const size_t selectedIndex,
                            const bool loading, const bool showButtonHints, const bool showMoreButton) {
  (void)showMoreButton;
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_READING_DATES), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int x = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - x * 2;
  const int top = statsContentTop(metrics, showButtonHints);
  const int bottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);
  const int headerHeight = renderer.getLineHeight(SMALL_FONT_ID) + 8;
  const int listTop = top + headerHeight;
  const int listHeight = std::max(1, bottom - listTop);

  char countText[48];
  snprintf(countText, sizeof(countText), "%u %s", static_cast<unsigned>(books.size()), tr(STR_STATS_BOOKS_TRACKED));
  renderer.drawText(SMALL_FONT_ID, x, top, countText, true, EpdFontFamily::BOLD);

  if (loading) {
    drawCenteredLabel(renderer, UI_10_FONT_ID, x, width, listTop + listHeight / 2, tr(STR_LOADING), true);
  } else if (books.empty()) {
    drawCenteredLabel(renderer, UI_10_FONT_ID, x, width, listTop + listHeight / 2, tr(STR_STATS_NO_READING_DATES),
                      true);
  } else {
    const size_t selected = std::min(selectedIndex, books.size() - 1);
    const size_t first = selected >= BOOK_STATS_VISIBLE_DATE_ROWS ? selected - BOOK_STATS_VISIBLE_DATE_ROWS + 1 : 0;
    const size_t visible = std::min(BOOK_STATS_VISIBLE_DATE_ROWS, books.size() - first);
    const int rowHeight = std::max(1, listHeight / static_cast<int>(BOOK_STATS_VISIBLE_DATE_ROWS));
    for (size_t row = 0; row < visible; ++row) {
      const size_t index = first + row;
      const ReadingDateStatsEntry& book = books[index];
      const int rowY = listTop + static_cast<int>(row) * rowHeight;
      renderer.drawRect(x, rowY, width, rowHeight, index == selected ? 2 : 1, true);
      const std::string title =
          renderer.truncatedText(UI_10_FONT_ID, book.title.c_str(), width - 16, EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, x + 8, rowY + 7, title.c_str(), true, EpdFontFamily::BOLD);

      char startDate[24];
      char finishDate[24];
      formatReadingStatsShortDate(book.startDate, startDate, sizeof(startDate));
      formatReadingStatsShortDate(book.finishedDate, finishDate, sizeof(finishDate));
      char dates[96];
      if (book.completed) {
        snprintf(dates, sizeof(dates), "%s %s | %s %s", tr(STR_STATS_STARTED), startDate, tr(STR_STATS_FINISHED),
                 finishDate);
      } else {
        snprintf(dates, sizeof(dates), "%s %s | %s", tr(STR_STATS_STARTED), startDate, tr(STR_STATS_IN_PROGRESS));
      }
      const std::string visibleDates = renderer.truncatedText(SMALL_FONT_ID, dates, width - 16);
      renderer.drawText(SMALL_FONT_ID, x + 8, rowY + rowHeight - renderer.getLineHeight(SMALL_FONT_ID) - 7,
                        visibleDates.c_str());
    }

    if (books.size() > BOOK_STATS_VISIBLE_DATE_ROWS) {
      char position[28];
      snprintf(position, sizeof(position), "%u-%u / %u", static_cast<unsigned>(first + 1),
               static_cast<unsigned>(first + visible), static_cast<unsigned>(books.size()));
      const int positionWidth = renderer.getTextWidth(SMALL_FONT_ID, position);
      renderer.drawText(SMALL_FONT_ID, x + width - positionWidth, top, position);
    }
  }

  if (showButtonHints && mappedInput) {
    const auto labels = mappedInput->mapLabels(tr(STR_BACK), tr(STR_EDIT), tr(STR_PREVIOUS_TAB), tr(STR_NEXT_TAB));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
}

void renderTimeOfDayPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                         const GlobalReadingStats& history, const bool showButtonHints, const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_TIME_OF_DAY), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentX = metrics.contentSidePadding;
  const int contentWidth = renderer.getScreenWidth() - contentX * 2;
  const int contentTop = statsContentTop(metrics, showButtonHints) + 10;
  const int contentBottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);

  const auto& buckets = history.timeOfDaySeconds;
  const char* labels[4] = {tr(STR_STATS_MORNING), tr(STR_STATS_AFTERNOON), tr(STR_STATS_EVENING), tr(STR_STATS_NIGHT)};
  uint64_t maxSeconds = 60;
  uint64_t totalSeconds = 0;
  size_t best = 0;
  for (size_t i = 0; i < 4 && i < buckets.size(); ++i) {
    totalSeconds += buckets[i];
    if (buckets[i] > buckets[best]) best = i;
    maxSeconds = std::max<uint64_t>(maxSeconds, buckets[i]);
  }

  constexpr int summaryHeight = 112;
  constexpr int xLabelHeight = 28;
  const int summaryTop = std::max(contentTop + 120, contentBottom - summaryHeight);
  const int chartLeft = contentX + 36;
  const int chartRight = contentX + contentWidth;
  const int scaleY = contentTop + 4;
  const int chartTop = scaleY + renderer.getLineHeight(SMALL_FONT_ID) + 8;
  const int chartBottom = summaryTop - xLabelHeight;
  const int chartHeight = std::max(20, chartBottom - chartTop);
  const int plotWidth = std::max(14, chartRight - chartLeft);
  renderer.drawLine(chartLeft, chartTop, chartLeft, chartBottom);
  renderer.drawLine(chartLeft, chartBottom, chartRight, chartBottom);

  char scaleText[20];
  formatCompactEstimate(static_cast<uint32_t>(std::min<uint64_t>(maxSeconds, UINT32_MAX)), scaleText,
                        sizeof(scaleText));
  renderer.drawText(SMALL_FONT_ID, contentX, scaleY, scaleText);

  const int slotWidth = std::max(1, plotWidth / 4);
  const int barWidth = std::max(10, slotWidth - 18);
  for (size_t i = 0; i < 4 && i < buckets.size(); ++i) {
    const int barX = chartLeft + static_cast<int>(i) * slotWidth + (slotWidth - barWidth) / 2;
    const int barHeight =
        buckets[i] > 0 ? std::max(2, static_cast<int>((static_cast<uint64_t>(chartHeight) * buckets[i]) / maxSeconds))
                       : 0;
    if (barHeight > 0) {
      if (i == best) {
        renderer.fillRect(barX, chartBottom - barHeight, barWidth, barHeight, true);
      } else {
        renderer.fillRectDither(barX, chartBottom - barHeight, barWidth, barHeight, Color::DarkGray);
        renderer.drawRect(barX, chartBottom - barHeight, barWidth, barHeight);
      }
    }
    const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);
    renderer.drawText(SMALL_FONT_ID, barX + (barWidth - labelWidth) / 2, chartBottom + 6, labels[i]);
  }

  renderer.drawRect(contentX, summaryTop, contentWidth, summaryHeight);
  const int halfWidth = contentWidth / 2;
  char value[40];
  BookReadingStats::formatDuration(static_cast<uint32_t>(std::min<uint64_t>(totalSeconds, UINT32_MAX)), value,
                                   sizeof(value));
  drawStatCell(renderer, contentX, halfWidth, summaryTop, summaryHeight, value, tr(STR_STATS_TIME_LBL));
  drawStatCell(renderer, contentX + halfWidth, contentWidth - halfWidth, summaryTop, summaryHeight, labels[best],
               tr(STR_STATS_PEAK_HOURS));

  drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
}

void renderMonthlyTrendPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const ReadingJournal* journal,
                            const ReadingSessionSnapshot& session, const bool showButtonHints,
                            const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_MONTHLY_TREND), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentX = metrics.contentSidePadding;
  const int contentWidth = renderer.getScreenWidth() - contentX * 2;
  const int contentTop = statsContentTop(metrics, showButtonHints) + 10;
  const int contentBottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);

  ReadingStatsDateTime now;
  const bool hasNow = getCurrentLocalReadingStatsDateTime(now);
  const uint32_t today = hasNow ? readingStatsDayIndex(now.date) : 0;

  // Last 12 calendar months, oldest first; slot 11 is the current month.
  std::array<uint64_t, 12> monthSeconds{};
  std::array<uint8_t, 12> monthNumber{};
  if (hasNow) {
    int month = now.date.month;
    int year = now.date.year;
    for (int slot = 11; slot >= 0; --slot) {
      monthNumber[static_cast<size_t>(slot)] = static_cast<uint8_t>(month);
      month--;
      if (month == 0) {
        month = 12;
        year--;
      }
    }
    for (uint32_t offset = 0; offset < 366 && today >= offset; ++offset) {
      const uint32_t dayIndex = today - offset;
      const uint32_t seconds = journalSecondsForDay(journal, dayIndex, today, session);
      if (seconds == 0) continue;
      ReadingStatsDate date;
      if (!readingStatsDateFromDayIndex(dayIndex, date)) continue;
      for (size_t slot = 0; slot < 12; ++slot) {
        const int slotOffsetFromNow = 11 - static_cast<int>(slot);
        int slotMonth = now.date.month - slotOffsetFromNow;
        int slotYear = now.date.year;
        while (slotMonth <= 0) {
          slotMonth += 12;
          slotYear--;
        }
        if (date.month == slotMonth && date.year == slotYear) {
          monthSeconds[slot] += seconds;
          break;
        }
      }
    }
  }

  uint64_t maxSeconds = 60;
  uint64_t totalSeconds = 0;
  size_t best = 0;
  for (size_t i = 0; i < 12; ++i) {
    totalSeconds += monthSeconds[i];
    if (monthSeconds[i] > monthSeconds[best]) best = i;
    maxSeconds = std::max<uint64_t>(maxSeconds, monthSeconds[i]);
  }

  constexpr int summaryHeight = 112;
  constexpr int xLabelHeight = 28;
  const int summaryTop = std::max(contentTop + 120, contentBottom - summaryHeight);
  const int chartLeft = contentX + 36;
  const int chartRight = contentX + contentWidth;
  const int scaleY = contentTop + 4;
  const int chartTop = scaleY + renderer.getLineHeight(SMALL_FONT_ID) + 8;
  const int chartBottom = summaryTop - xLabelHeight;
  const int chartHeight = std::max(20, chartBottom - chartTop);
  const int plotWidth = std::max(14, chartRight - chartLeft);
  renderer.drawLine(chartLeft, chartTop, chartLeft, chartBottom);
  renderer.drawLine(chartLeft, chartBottom, chartRight, chartBottom);

  char scaleText[20];
  formatCompactEstimate(static_cast<uint32_t>(std::min<uint64_t>(maxSeconds, UINT32_MAX)), scaleText,
                        sizeof(scaleText));
  renderer.drawText(SMALL_FONT_ID, contentX, scaleY, scaleText);

  const int slotWidth = std::max(1, plotWidth / 12);
  const int barWidth = std::max(4, slotWidth - 8);
  for (size_t i = 0; i < 12; ++i) {
    const int barX = chartLeft + static_cast<int>(i) * slotWidth + (slotWidth - barWidth) / 2;
    const int barHeight =
        monthSeconds[i] > 0
            ? std::max(2, static_cast<int>((static_cast<uint64_t>(chartHeight) * monthSeconds[i]) / maxSeconds))
            : 0;
    if (barHeight > 0) {
      if (i == 11) {
        renderer.fillRect(barX, chartBottom - barHeight, barWidth, barHeight, true);
      } else {
        renderer.fillRectDither(barX, chartBottom - barHeight, barWidth, barHeight, Color::DarkGray);
        renderer.drawRect(barX, chartBottom - barHeight, barWidth, barHeight);
      }
    }
    if (i % 2 == 1 || i == 11) {
      char monthLabel[6];
      snprintf(monthLabel, sizeof(monthLabel), "%u", static_cast<unsigned>(monthNumber[i]));
      const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, monthLabel);
      renderer.drawText(SMALL_FONT_ID, barX + (barWidth - labelWidth) / 2, chartBottom + 6, monthLabel);
    }
  }

  renderer.drawRect(contentX, summaryTop, contentWidth, summaryHeight);
  const int halfWidth = contentWidth / 2;
  char value[40];
  BookReadingStats::formatDuration(static_cast<uint32_t>(std::min<uint64_t>(totalSeconds, UINT32_MAX)), value,
                                   sizeof(value));
  drawStatCell(renderer, contentX, halfWidth, summaryTop, summaryHeight, value, tr(STR_STATS_TIME_LBL));
  char bestLabel[16];
  snprintf(bestLabel, sizeof(bestLabel), "%u", static_cast<unsigned>(monthNumber[best]));
  drawStatCell(renderer, contentX + halfWidth, contentWidth - halfWidth, summaryTop, summaryHeight, bestLabel,
               tr(STR_STATS_BEST_MONTH));

  drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
}

void renderDeviceSplitPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                           const GlobalReadingStats& localStats, const bool showButtonHints,
                           const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_DEVICE_SPLIT), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentX = metrics.contentSidePadding;
  const int contentWidth = renderer.getScreenWidth() - contentX * 2;
  const int contentTop = statsContentTop(metrics, showButtonHints) + 10;
  const int contentBottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);

  constexpr uint8_t maxPeers = 3;
  std::array<GlobalReadingStats, maxPeers> peers{};
  std::array<std::string, maxPeers> peerNames{};
  const uint8_t peerCount = GlobalReadingStats::loadSyncedPeers(peers.data(), peerNames.data(), maxPeers);
  const int deviceCount = 1 + peerCount;

  uint64_t totalSeconds = localStats.totalReadingSeconds;
  uint64_t maxSeconds = std::max<uint64_t>(60, localStats.totalReadingSeconds);
  for (uint8_t i = 0; i < peerCount; ++i) {
    totalSeconds += peers[i].totalReadingSeconds;
    maxSeconds = std::max<uint64_t>(maxSeconds, peers[i].totalReadingSeconds);
  }

  // One row per device: name, share bar, and the key totals.
  const int rowGap = 16;
  const int rowHeight =
      std::max(84, (contentBottom - contentTop - rowGap * (deviceCount - 1)) / std::max(1, deviceCount));
  int rowY = contentTop;
  char value[48];
  char line[96];
  for (int device = 0; device < deviceCount && rowY + rowHeight <= contentBottom + 4; ++device) {
    const GlobalReadingStats& stats = device == 0 ? localStats : peers[static_cast<size_t>(device - 1)];
    renderer.drawRoundedRect(contentX, rowY, contentWidth, rowHeight, 1, 8, true);
    if (device == 0) {
      const char* localName = SETTINGS.getEffectiveDeviceName();
      snprintf(line, sizeof(line), "%s (%s)",
               localName != nullptr && localName[0] != '\0' ? localName : tr(STR_STATS_THIS_DEVICE_SCREEN),
               tr(STR_STATS_THIS_DEVICE_SCREEN));
      renderer.drawText(UI_10_FONT_ID, contentX + 12, rowY + 8, line, true, EpdFontFamily::BOLD);
    } else {
      const std::string& peerName = peerNames[static_cast<size_t>(device - 1)];
      if (!peerName.empty()) {
        renderer.drawText(UI_10_FONT_ID, contentX + 12, rowY + 8, peerName.c_str(), true, EpdFontFamily::BOLD);
      } else {
        snprintf(line, sizeof(line), "%s %d", tr(STR_STATS_SYNCED_DEVICE), device);
        renderer.drawText(UI_10_FONT_ID, contentX + 12, rowY + 8, line, true, EpdFontFamily::BOLD);
      }
    }

    const int barTop = rowY + 8 + renderer.getLineHeight(UI_10_FONT_ID) + 6;
    const int barWidth = contentWidth - 24;
    const int barHeight = 10;
    renderer.drawRect(contentX + 12, barTop, barWidth, barHeight);
    if (stats.totalReadingSeconds > 0) {
      const int fill =
          std::max(2, static_cast<int>((static_cast<uint64_t>(barWidth - 2) * stats.totalReadingSeconds) / maxSeconds));
      renderer.fillRect(contentX + 13, barTop + 1, std::min(fill, barWidth - 2), barHeight - 2, true);
    }

    BookReadingStats::formatDuration(stats.totalReadingSeconds, value, sizeof(value));
    const unsigned share =
        totalSeconds > 0
            ? static_cast<unsigned>((static_cast<uint64_t>(stats.totalReadingSeconds) * 100u) / totalSeconds)
            : 0;
    snprintf(line, sizeof(line), "%s  |  %u%%  |  %lu %s  |  %lu %s", value, share,
             static_cast<unsigned long>(stats.totalSessions), tr(STR_STATS_SESSIONS_LBL),
             static_cast<unsigned long>(stats.completedBooks), tr(STR_STATS_COMPLETED_LBL));
    renderer.drawText(SMALL_FONT_ID, contentX + 12, barTop + barHeight + 8, line);
    rowY += rowHeight + rowGap;
  }

  drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
}

void renderWeekdayPatternPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                              const ReadingJournal* journal, const GlobalReadingStats& history,
                              const ReadingSessionSnapshot& session, const bool showButtonHints,
                              const bool showMoreButton) {
  (void)history;
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_WEEKDAY_PATTERN), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentX = metrics.contentSidePadding;
  const int contentWidth = renderer.getScreenWidth() - contentX * 2;
  const int contentTop = statsContentTop(metrics, showButtonHints) + 10;
  const int contentBottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);

  ReadingStatsDateTime now;
  const bool hasNow = getCurrentLocalReadingStatsDateTime(now);
  const uint32_t today = hasNow ? readingStatsDayIndex(now.date) : 0;

  std::array<uint64_t, 7> weekdaySeconds{};
  std::array<uint16_t, 7> weekdayActiveDays{};
  constexpr uint32_t lookbackDays = 366;
  for (uint32_t offset = 0; offset < lookbackDays && hasNow && today >= offset; ++offset) {
    const uint32_t dayIndex = today - offset;
    const uint32_t seconds = journalSecondsForDay(journal, dayIndex, today, session);
    if (seconds == 0) continue;
    const int weekday = mondayFirstWeekday(dayIndex);
    if (weekday < 0) continue;
    weekdaySeconds[static_cast<size_t>(weekday)] += seconds;
    weekdayActiveDays[static_cast<size_t>(weekday)]++;
  }

  uint64_t maxSeconds = 60;
  uint64_t totalSeconds = 0;
  int bestWeekday = 0;
  for (int i = 0; i < 7; ++i) {
    totalSeconds += weekdaySeconds[static_cast<size_t>(i)];
    if (weekdaySeconds[static_cast<size_t>(i)] > weekdaySeconds[static_cast<size_t>(bestWeekday)]) bestWeekday = i;
    maxSeconds = std::max<uint64_t>(maxSeconds, weekdaySeconds[static_cast<size_t>(i)]);
  }

  constexpr int summaryHeight = 112;
  constexpr int xLabelHeight = 28;
  const int summaryTop = std::max(contentTop + 120, contentBottom - summaryHeight);
  const int chartLeft = contentX + 36;
  const int chartRight = contentX + contentWidth;
  const int scaleY = contentTop + 4;
  const int chartTop = scaleY + renderer.getLineHeight(SMALL_FONT_ID) + 8;
  const int chartBottom = summaryTop - xLabelHeight;
  const int chartHeight = std::max(20, chartBottom - chartTop);
  const int plotWidth = std::max(14, chartRight - chartLeft);
  renderer.drawLine(chartLeft, chartTop, chartLeft, chartBottom);
  renderer.drawLine(chartLeft, chartBottom, chartRight, chartBottom);

  char scaleText[20];
  formatCompactEstimate(static_cast<uint32_t>(std::min<uint64_t>(maxSeconds, UINT32_MAX)), scaleText,
                        sizeof(scaleText));
  renderer.drawText(SMALL_FONT_ID, contentX, scaleY, scaleText);

  const int slotWidth = std::max(1, plotWidth / 7);
  const int barWidth = std::max(6, slotWidth - 12);
  for (int i = 0; i < 7; ++i) {
    const uint64_t value = weekdaySeconds[static_cast<size_t>(i)];
    const int barX = chartLeft + i * slotWidth + (slotWidth - barWidth) / 2;
    const int barHeight =
        value > 0 ? std::max(2, static_cast<int>((static_cast<uint64_t>(chartHeight) * value) / maxSeconds)) : 0;
    if (barHeight > 0) {
      if (i == bestWeekday) {
        renderer.fillRect(barX, chartBottom - barHeight, barWidth, barHeight, true);
      } else {
        renderer.fillRectDither(barX, chartBottom - barHeight, barWidth, barHeight, Color::DarkGray);
        renderer.drawRect(barX, chartBottom - barHeight, barWidth, barHeight);
      }
    }
    const char* label = weekdayLabel(i);
    const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, label);
    renderer.drawText(SMALL_FONT_ID, barX + (barWidth - labelWidth) / 2, chartBottom + 6, label);
  }

  renderer.drawRect(contentX, summaryTop, contentWidth, summaryHeight);
  const int thirdWidth = contentWidth / 3;
  char value[40];
  BookReadingStats::formatDuration(static_cast<uint32_t>(std::min<uint64_t>(totalSeconds, UINT32_MAX)), value,
                                   sizeof(value));
  drawStatCell(renderer, contentX, thirdWidth, summaryTop, summaryHeight, value, tr(STR_STATS_TIME_LBL));
  drawStatCell(renderer, contentX + thirdWidth, thirdWidth, summaryTop, summaryHeight, weekdayLabel(bestWeekday),
               tr(STR_STATS_BEST_WEEKDAY));
  const uint16_t bestDays = weekdayActiveDays[static_cast<size_t>(bestWeekday)];
  const uint64_t bestAvg = bestDays > 0 ? weekdaySeconds[static_cast<size_t>(bestWeekday)] / bestDays : 0;
  BookReadingStats::formatDuration(static_cast<uint32_t>(std::min<uint64_t>(bestAvg, UINT32_MAX)), value,
                                   sizeof(value));
  drawStatCell(renderer, contentX + thirdWidth * 2, contentWidth - thirdWidth * 2, summaryTop, summaryHeight, value,
               tr(STR_STATS_BEST_DAY_AVG));

  drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
}

void renderPaceTrendPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const ReadingJournal* journal,
                         const ReadingSessionSnapshot& session, const bool showButtonHints, const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_PACE_TREND), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentX = metrics.contentSidePadding;
  const int contentWidth = renderer.getScreenWidth() - contentX * 2;
  const int contentTop = statsContentTop(metrics, showButtonHints) + 10;
  const int contentBottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);

  ReadingStatsDateTime now;
  const bool hasNow = getCurrentLocalReadingStatsDateTime(now);
  const uint32_t today = hasNow ? readingStatsDayIndex(now.date) : 0;

  // Daily pace in pages-per-minute x10 fixed point over the last 30 days.
  constexpr size_t dayCount = 30;
  std::array<uint16_t, dayCount> paceX10{};
  uint32_t maxPaceX10 = 10;
  uint64_t recentPagesWeek = 0;
  uint64_t recentSecondsWeek = 0;
  uint64_t pagesMonth = 0;
  uint64_t secondsMonth = 0;
  for (size_t i = 0; i < dayCount && hasNow; ++i) {
    const uint32_t offset = static_cast<uint32_t>(dayCount - 1 - i);
    if (today < offset) continue;
    const uint32_t dayIndex = today - offset;
    const uint32_t seconds = journalSecondsForDay(journal, dayIndex, today, session);
    uint32_t pages = journal != nullptr ? journal->pagesOnDay(dayIndex) : 0;
    if (dayIndex == today) pages += session.screenPages;
    // Under two minutes of reading produces meaningless pace noise.
    if (seconds < 120 || pages == 0) continue;
    const uint32_t pace = static_cast<uint32_t>((static_cast<uint64_t>(pages) * 600u) / seconds);
    paceX10[i] = static_cast<uint16_t>(std::min<uint32_t>(pace, UINT16_MAX));
    maxPaceX10 = std::max(maxPaceX10, pace);
    pagesMonth += pages;
    secondsMonth += seconds;
    if (offset < 7) {
      recentPagesWeek += pages;
      recentSecondsWeek += seconds;
    }
  }

  constexpr int summaryHeight = 112;
  constexpr int xLabelHeight = 28;
  const int summaryTop = std::max(contentTop + 120, contentBottom - summaryHeight);
  const int chartLeft = contentX + 36;
  const int chartRight = contentX + contentWidth;
  const int scaleY = contentTop + 4;
  const int chartTop = scaleY + renderer.getLineHeight(SMALL_FONT_ID) + 8;
  const int chartBottom = summaryTop - xLabelHeight;
  const int chartHeight = std::max(20, chartBottom - chartTop);
  const int plotWidth = std::max(14, chartRight - chartLeft);
  renderer.drawLine(chartLeft, chartTop, chartLeft, chartBottom);
  renderer.drawLine(chartLeft, chartBottom, chartRight, chartBottom);

  char scaleText[20];
  snprintf(scaleText, sizeof(scaleText), "%lu.%lu", static_cast<unsigned long>(maxPaceX10 / 10),
           static_cast<unsigned long>(maxPaceX10 % 10));
  renderer.drawText(SMALL_FONT_ID, contentX, chartTop - renderer.getLineHeight(SMALL_FONT_ID) / 2, scaleText);

  const int slotWidth = std::max(1, plotWidth / static_cast<int>(dayCount));
  const int barWidth = std::max(2, slotWidth - 3);
  for (size_t i = 0; i < dayCount; ++i) {
    if (paceX10[i] == 0) continue;
    const int barX = chartLeft + static_cast<int>(i) * slotWidth + (slotWidth - barWidth) / 2;
    const int barHeight = std::max(2, static_cast<int>((static_cast<uint64_t>(chartHeight) * paceX10[i]) / maxPaceX10));
    if (i + 1 == dayCount) {
      renderer.fillRect(barX, chartBottom - barHeight, barWidth, barHeight, true);
    } else {
      renderer.fillRectDither(barX, chartBottom - barHeight, barWidth, barHeight, Color::DarkGray);
    }
  }
  if (hasNow) {
    for (size_t i = 0; i < dayCount; i += 7) {
      const uint32_t offset = static_cast<uint32_t>(dayCount - 1 - i);
      if (today < offset) continue;
      ReadingStatsDate date;
      char dayLabel[8] = "-";
      if (readingStatsDateFromDayIndex(today - offset, date)) {
        snprintf(dayLabel, sizeof(dayLabel), "%u", static_cast<unsigned>(date.day));
      }
      renderer.drawText(SMALL_FONT_ID, chartLeft + static_cast<int>(i) * slotWidth, chartBottom + 6, dayLabel);
    }
  }

  renderer.drawRect(contentX, summaryTop, contentWidth, summaryHeight);
  const int thirdWidth = contentWidth / 3;
  char value[40];
  const uint32_t weekPaceX10 =
      recentSecondsWeek > 0 ? static_cast<uint32_t>((recentPagesWeek * 600u) / recentSecondsWeek) : 0;
  const uint32_t monthPaceX10 = secondsMonth > 0 ? static_cast<uint32_t>((pagesMonth * 600u) / secondsMonth) : 0;
  snprintf(value, sizeof(value), "%lu.%lu", static_cast<unsigned long>(weekPaceX10 / 10),
           static_cast<unsigned long>(weekPaceX10 % 10));
  drawStatCell(renderer, contentX, thirdWidth, summaryTop, summaryHeight, value, tr(STR_STATS_PACE_7D));
  snprintf(value, sizeof(value), "%lu.%lu", static_cast<unsigned long>(monthPaceX10 / 10),
           static_cast<unsigned long>(monthPaceX10 % 10));
  drawStatCell(renderer, contentX + thirdWidth, thirdWidth, summaryTop, summaryHeight, value, tr(STR_STATS_PACE_30D));
  const char* trend = weekPaceX10 == 0 || monthPaceX10 == 0            ? "-"
                      : weekPaceX10 > monthPaceX10 + monthPaceX10 / 20 ? tr(STR_STATS_TREND_UP)
                      : weekPaceX10 + monthPaceX10 / 20 < monthPaceX10 ? tr(STR_STATS_TREND_DOWN)
                                                                       : tr(STR_STATS_TREND_FLAT);
  drawStatCell(renderer, contentX + thirdWidth * 2, contentWidth - thirdWidth * 2, summaryTop, summaryHeight, trend,
               tr(STR_STATS_TREND));

  drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
}

void renderReadingWrappedPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                              const ReadingJournal* journal, const GlobalReadingStats& history,
                              const ReadingSessionSnapshot& session, const bool showButtonHints,
                              const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_WRAPPED), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentX = metrics.contentSidePadding;
  const int contentWidth = renderer.getScreenWidth() - contentX * 2;
  const int contentTop = statsContentTop(metrics, showButtonHints) + 10;
  const int contentBottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);

  ReadingStatsDateTime now;
  const bool hasNow = getCurrentLocalReadingStatsDateTime(now);
  const uint32_t today = hasNow ? readingStatsDayIndex(now.date) : 0;

  uint64_t yearSeconds = 0;
  uint16_t yearActiveDays = 0;
  std::array<uint64_t, 7> weekdaySeconds{};
  for (uint32_t offset = 0; offset < 366 && hasNow && today >= offset; ++offset) {
    const uint32_t dayIndex = today - offset;
    const uint32_t seconds = journalSecondsForDay(journal, dayIndex, today, session);
    if (seconds == 0) continue;
    yearSeconds += seconds;
    yearActiveDays++;
    const int weekday = mondayFirstWeekday(dayIndex);
    if (weekday >= 0) weekdaySeconds[static_cast<size_t>(weekday)] += seconds;
  }
  int bestWeekday = 0;
  for (int i = 1; i < 7; ++i) {
    if (weekdaySeconds[static_cast<size_t>(i)] > weekdaySeconds[static_cast<size_t>(bestWeekday)]) bestWeekday = i;
  }

  const int cardTop = contentTop + 8;
  const int cardHeight = std::max(200, contentBottom - cardTop - 8);
  renderer.drawRoundedRect(contentX, cardTop, contentWidth, cardHeight, 2, 10, true);
  const int cellW = contentWidth / 2;
  const int cellH = cardHeight / 3;
  char value[40];

  BookReadingStats::formatDuration(static_cast<uint32_t>(std::min<uint64_t>(yearSeconds, UINT32_MAX)), value,
                                   sizeof(value));
  drawStatCell(renderer, contentX, cellW, cardTop, cellH, value, tr(STR_STATS_WRAPPED_HOURS));
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(yearActiveDays));
  drawStatCell(renderer, contentX + cellW, contentWidth - cellW, cardTop, cellH, value,
               tr(STR_STATS_WRAPPED_ACTIVE_DAYS));

  snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(history.completedBooks));
  drawStatCell(renderer, contentX, cellW, cardTop + cellH, cellH, value, tr(STR_STATS_WRAPPED_BOOKS));
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(history.displayLongestReadingStreak()));
  drawStatCell(renderer, contentX + cellW, contentWidth - cellW, cardTop + cellH, cellH, value,
               tr(STR_STATS_WRAPPED_LONGEST_STREAK));

  drawStatCell(renderer, contentX, cellW, cardTop + cellH * 2, cardHeight - cellH * 2, weekdayLabel(bestWeekday),
               tr(STR_STATS_BEST_WEEKDAY));
  const uint32_t avgSession = history.totalSessions > 0 ? history.totalReadingSeconds / history.totalSessions : 0;
  BookReadingStats::formatDuration(avgSession, value, sizeof(value));
  drawStatCell(renderer, contentX + cellW, contentWidth - cellW, cardTop + cellH * 2, cardHeight - cellH * 2, value,
               tr(STR_STATS_AVG_SESSION_LBL));

  drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
}

void renderReadingActivityChartPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                    const ReadingJournal* journal, const GlobalReadingStats& history,
                                    const ReadingSessionSnapshot& session, const uint8_t goalMinutes,
                                    const bool showButtonHints, const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_ACTIVITY_CHART), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentX = metrics.contentSidePadding;
  const int contentWidth = renderer.getScreenWidth() - contentX * 2;
  const int contentTop = statsContentTop(metrics, showButtonHints) + 10;
  const int contentBottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);

  ReadingStatsDateTime now;
  const bool hasNow = getCurrentLocalReadingStatsDateTime(now);
  const uint32_t today = hasNow ? readingStatsDayIndex(now.date) : 0;
  constexpr size_t dayCount = 14;
  std::array<uint32_t, dayCount> values{};
  uint32_t totalSeconds = 0;
  uint32_t maxSeconds = static_cast<uint32_t>(goalMinutes) * 60u;
  uint16_t activeDays = 0;
  uint16_t goalDays = 0;
  for (size_t i = 0; i < dayCount && hasNow; ++i) {
    const uint32_t dayIndex = today >= dayCount - 1 - i ? today - static_cast<uint32_t>(dayCount - 1 - i) : 0;
    const uint32_t detailedSeconds = journalSecondsForDay(journal, dayIndex, today, session);
    values[i] = heatmapSecondsForDay(journal, history, session, dayIndex, today);
    totalSeconds = addSaturatedValue(totalSeconds, detailedSeconds);
    maxSeconds = std::max(maxSeconds, values[i]);
    if (activeOnDay(journal, history, session, dayIndex, today)) activeDays++;
    if (goalMinutes > 0 && detailedSeconds >= static_cast<uint32_t>(goalMinutes) * 60u) goalDays++;
  }
  maxSeconds = std::max<uint32_t>(maxSeconds, 60u);

  constexpr int summaryHeight = 112;
  constexpr int xLabelHeight = 28;
  const int summaryTop = std::max(contentTop + 120, contentBottom - summaryHeight);
  const int chartLeft = contentX + 36;
  const int chartRight = contentX + contentWidth;
  const int scaleY = contentTop + 4;
  const int chartTop = scaleY + renderer.getLineHeight(SMALL_FONT_ID) + 8;
  const int chartBottom = summaryTop - xLabelHeight;
  const int chartHeight = std::max(20, chartBottom - chartTop);
  const int plotWidth = std::max(14, chartRight - chartLeft);
  renderer.drawLine(chartLeft, chartTop, chartLeft, chartBottom);
  renderer.drawLine(chartLeft, chartBottom, chartRight, chartBottom);

  char scaleText[20];
  formatCompactEstimate(maxSeconds, scaleText, sizeof(scaleText));
  renderer.drawText(SMALL_FONT_ID, contentX, scaleY, scaleText);
  const uint32_t goalSeconds = static_cast<uint32_t>(goalMinutes) * 60u;
  if (goalSeconds > 0 && goalSeconds <= maxSeconds) {
    const int goalY = chartBottom - static_cast<int>((static_cast<uint64_t>(chartHeight) * goalSeconds) / maxSeconds);
    for (int x = chartLeft; x < chartRight; x += 8) {
      renderer.drawLine(x, goalY, std::min(x + 3, chartRight), goalY);
    }
  }

  const int slotWidth = std::max(1, plotWidth / static_cast<int>(dayCount));
  const int barWidth = std::max(3, slotWidth - 6);
  for (size_t i = 0; i < dayCount; ++i) {
    const int barX = chartLeft + static_cast<int>(i) * slotWidth + (slotWidth - barWidth) / 2;
    const int barHeight =
        values[i] > 0 ? std::max(2, static_cast<int>((static_cast<uint64_t>(chartHeight) * values[i]) / maxSeconds))
                      : 0;
    if (barHeight > 0) {
      if (i + 1 == dayCount) {
        renderer.fillRect(barX, chartBottom - barHeight, barWidth, barHeight, true);
      } else {
        renderer.fillRectDither(barX, chartBottom - barHeight, barWidth, barHeight, Color::DarkGray);
        renderer.drawRect(barX, chartBottom - barHeight, barWidth, barHeight);
      }
    }
    if (hasNow && (i % 2 == 1 || i + 1 == dayCount)) {
      const uint32_t dayIndex = today >= dayCount - 1 - i ? today - static_cast<uint32_t>(dayCount - 1 - i) : 0;
      ReadingStatsDate date;
      char dayLabel[8] = "-";
      if (readingStatsDateFromDayIndex(dayIndex, date)) {
        snprintf(dayLabel, sizeof(dayLabel), "%u", static_cast<unsigned>(date.day));
      }
      const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, dayLabel);
      renderer.drawText(SMALL_FONT_ID, barX + (barWidth - labelWidth) / 2, chartBottom + 6, dayLabel);
    }
  }

  renderer.drawRect(contentX, summaryTop, contentWidth, summaryHeight);
  const int thirdWidth = contentWidth / 3;
  char value[40];
  BookReadingStats::formatDuration(totalSeconds, value, sizeof(value));
  drawStatCell(renderer, contentX, thirdWidth, summaryTop, summaryHeight, value, tr(STR_STATS_TIME_LBL));
  snprintf(value, sizeof(value), "%u / %u", static_cast<unsigned>(activeDays), static_cast<unsigned>(dayCount));
  drawStatCell(renderer, contentX + thirdWidth, thirdWidth, summaryTop, summaryHeight, value,
               tr(STR_STATS_ACTIVE_DAYS_LBL));
  if (goalMinutes > 0) {
    snprintf(value, sizeof(value), "%u / %u", static_cast<unsigned>(goalDays), static_cast<unsigned>(dayCount));
  } else {
    snprintf(value, sizeof(value), "-");
  }
  drawStatCell(renderer, contentX + thirdWidth * 2, contentWidth - thirdWidth * 2, summaryTop, summaryHeight, value,
               tr(STR_STATS_GOAL_DAYS_14_LBL));
  drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
}

void renderReadingDailyMinutesPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                   const ReadingJournal* journal, const GlobalReadingStats& history,
                                   const ReadingSessionSnapshot& session, const uint8_t selectedDayOffset,
                                   const bool showButtonHints, const bool showMoreButton) {
  (void)showMoreButton;
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_DAILY_MINUTES), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int x = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - x * 2;
  const int top = statsContentTop(metrics, showButtonHints) + 2;
  const int bottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);

  ReadingStatsDateTime now;
  if (!getCurrentLocalReadingStatsDateTime(now)) {
    drawCenteredLabel(renderer, UI_10_FONT_ID, x, width, top + 120, tr(STR_STATS_NOT_ENOUGH_DATA), true);
    drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, false);
    return;
  }

  constexpr uint8_t historyDays = 90;
  constexpr uint8_t visibleRows = 7;
  const uint8_t selected = std::min<uint8_t>(selectedDayOffset, historyDays - 1);
  const uint8_t firstOffset = selected >= visibleRows ? static_cast<uint8_t>(selected - visibleRows + 1) : 0;
  const uint8_t lastOffset = std::min<uint8_t>(historyDays - 1, static_cast<uint8_t>(firstOffset + visibleRows - 1));
  const uint32_t today = readingStatsDayIndex(now.date);

  char position[28];
  snprintf(position, sizeof(position), "Day %u / %u", static_cast<unsigned>(selected + 1),
           static_cast<unsigned>(historyDays));
  renderer.drawText(SMALL_FONT_ID, x, top, position, true, EpdFontFamily::BOLD);

  ReadingStatsDate newestDate;
  ReadingStatsDate oldestDate;
  char newest[16] = "-";
  char oldest[16] = "-";
  if (readingStatsDateFromDayIndex(today >= firstOffset ? today - firstOffset : 0, newestDate)) {
    formatReadingStatsShortDate(newestDate, newest, sizeof(newest));
  }
  if (readingStatsDateFromDayIndex(today >= lastOffset ? today - lastOffset : 0, oldestDate)) {
    formatReadingStatsShortDate(oldestDate, oldest, sizeof(oldest));
  }
  char range[40];
  snprintf(range, sizeof(range), "%s - %s", oldest, newest);
  const int rangeWidth = renderer.getTextWidth(SMALL_FONT_ID, range);
  renderer.drawText(SMALL_FONT_ID, x + width - rangeWidth, top, range);

  const int listTop = top + renderer.getLineHeight(SMALL_FONT_ID) + 10;
  const int listHeight = std::max(1, bottom - listTop);
  const int rowHeight = std::max(1, listHeight / visibleRows);
  uint32_t maxSeconds = 60;
  for (uint8_t offset = firstOffset; offset <= lastOffset; ++offset) {
    const uint32_t dayIndex = today >= offset ? today - offset : 0;
    maxSeconds = std::max(maxSeconds, journalSecondsForDay(journal, dayIndex, today, session));
  }

  for (uint8_t offset = firstOffset, row = 0; offset <= lastOffset; ++offset, ++row) {
    const uint32_t dayIndex = today >= offset ? today - offset : 0;
    ReadingStatsDate date;
    if (!readingStatsDateFromDayIndex(dayIndex, date)) continue;
    const uint32_t seconds = journalSecondsForDay(journal, dayIndex, today, session);
    const bool active = activeOnDay(journal, history, session, dayIndex, today);
    const int rowY = listTop + row * rowHeight;
    renderer.drawRect(x, rowY, width, rowHeight, offset == selected ? 2 : 1, true);

    char dateText[24];
    char shortDate[16];
    formatReadingStatsShortDate(date, shortDate, sizeof(shortDate));
    const char* weekday = I18N.get(DAY_LABELS[readingStatsDayOfWeekIndex(date)]);
    snprintf(dateText, sizeof(dateText), "%s %s", weekday, shortDate);
    renderer.drawText(UI_10_FONT_ID, x + 8, rowY + 8, dateText, true,
                      offset == selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

    char duration[32];
    if (seconds > 0) {
      formatCompactDuration(seconds, duration, sizeof(duration));
    } else if (active) {
      snprintf(duration, sizeof(duration), "%s", tr(STR_STATS_TIME_NOT_RECORDED));
    } else {
      snprintf(duration, sizeof(duration), "-");
    }
    const int durationWidth = renderer.getTextWidth(SMALL_FONT_ID, duration);
    renderer.drawText(SMALL_FONT_ID, x + width - durationWidth - 8,
                      rowY + rowHeight - renderer.getLineHeight(SMALL_FONT_ID) - 7, duration, true,
                      seconds > 0 ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

    const int barX = x + 8;
    const int barY = rowY + rowHeight - 16;
    const int barWidth = std::max(20, width - durationWidth - 32);
    if (seconds > 0) {
      const int fillWidth = std::max(2, static_cast<int>((static_cast<uint64_t>(barWidth) * seconds) / maxSeconds));
      renderer.fillRect(barX, barY, std::min(barWidth, fillWidth), 5, true);
    } else {
      renderer.drawLine(barX, barY + 2, barX + barWidth, barY + 2, active);
    }
  }

  if (showButtonHints && mappedInput) {
    const auto labels =
        mappedInput->mapLabels(tr(STR_BACK), tr(STR_STATS_DETAILS), tr(STR_PREVIOUS_TAB), tr(STR_NEXT_TAB));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
}

void renderReadingHeatmapPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                              const ReadingJournal* journal, const GlobalReadingStats& history,
                              const ReadingSessionSnapshot& session, const uint8_t goalMinutes,
                              const bool showButtonHints, const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_ACTIVITY_HEATMAP), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentX = metrics.contentSidePadding;
  const int contentWidth = renderer.getScreenWidth() - contentX * 2;
  const int heatmapX = std::max(8, contentX / 2);
  const int heatmapWidth = renderer.getScreenWidth() - heatmapX * 2;
  const int contentTop = statsContentTop(metrics, showButtonHints) + 4;
  const int contentBottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);
  const uint32_t goalSeconds = static_cast<uint32_t>(goalMinutes) * 60u;

  ReadingStatsDateTime now;
  const bool hasNow = getCurrentLocalReadingStatsDateTime(now);
  const uint32_t today = hasNow ? readingStatsDayIndex(now.date) : 0;
  const uint8_t todayDow = hasNow ? readingStatsDayOfWeekIndex(now.date) : 0;
  const uint32_t daysBack = 52u * 7u + todayDow;
  const uint32_t firstMonday = today >= daysBack ? today - daysBack : 0;

  constexpr int labelWidth = 20;
  constexpr int cellGap = 1;
  constexpr int maxWeeks = 27;
  const int cellSize = std::clamp((heatmapWidth - labelWidth - (maxWeeks - 1) * cellGap) / maxWeeks, 6, 17);
  const int bandTitleHeight = renderer.getLineHeight(SMALL_FONT_ID) + 6;
  const int bandHeight = bandTitleHeight + 7 * cellSize + 6 * cellGap;

  const auto drawBand = [&](const uint32_t startDay, const int weeks, const int y) {
    ReadingStatsDate startDate;
    ReadingStatsDate endDate;
    char startMonth[8] = "-";
    char endMonth[8] = "-";
    char range[40];
    if (readingStatsDateFromDayIndex(startDay, startDate))
      formatReadingStatsMonthToken(startDate, startMonth, sizeof(startMonth));
    if (readingStatsDateFromDayIndex(startDay + static_cast<uint32_t>(weeks * 7 - 1), endDate)) {
      formatReadingStatsMonthToken(endDate, endMonth, sizeof(endMonth));
    }
    snprintf(range, sizeof(range), "%s %u - %s %u", startMonth, static_cast<unsigned>(startDate.year), endMonth,
             static_cast<unsigned>(endDate.year));
    renderer.drawText(SMALL_FONT_ID, heatmapX + labelWidth, y, range, true, EpdFontFamily::BOLD);
    const int gridY = y + bandTitleHeight;
    const int rowStride = cellSize + cellGap;
    const int columnStride = cellSize + cellGap;
    for (int row = 0; row < 7; ++row) {
      if (row == 0 || row == 2 || row == 4) {
        const char* dayLabel = I18N.get(DAY_LABELS[static_cast<size_t>(row)]);
        char shortLabel[2] = {dayLabel[0], '\0'};
        renderer.drawText(SMALL_FONT_ID, heatmapX, gridY + row * rowStride, shortLabel);
      }
      for (int week = 0; week < weeks; ++week) {
        const uint32_t dayIndex = startDay + static_cast<uint32_t>(week * 7 + row);
        if (!hasNow || dayIndex > today) continue;
        const uint32_t seconds = heatmapSecondsForDay(journal, history, session, dayIndex, today);
        fillHeatCell(renderer, heatmapX + labelWidth + week * columnStride, gridY + row * rowStride, cellSize, seconds,
                     goalSeconds);
      }
    }
  };

  drawCenteredLabel(renderer, UI_10_FONT_ID, heatmapX, heatmapWidth, contentTop, tr(STR_STATS_LAST_12_MONTHS), true);
  const int firstBandY = contentTop + renderer.getLineHeight(UI_10_FONT_ID) + 8;
  drawBand(firstMonday, 26, firstBandY);
  const int secondBandY = firstBandY + bandHeight + 18;
  drawBand(firstMonday + 26u * 7u, 27, secondBandY);

  const ReadingStreakSummary streaks =
      hasNow ? summarizeReadingStreaks(journal, history, session, today, ReadingJournal::HISTORY_DAYS)
             : ReadingStreakSummary{};

  const int summaryTop = secondBandY + bandHeight + 14;
  const int legendCell = std::min(13, cellSize + 2);
  const int legendHeight = legendCell + 10;
  int legendX = contentX;
  const int legendY = summaryTop + std::max(0, (legendHeight - legendCell) / 2);
  renderer.drawText(SMALL_FONT_ID, legendX, legendY, "Less");
  legendX += renderer.getTextWidth(SMALL_FONT_ID, "Less") + 8;
  const uint32_t legendReference = goalSeconds > 0 ? goalSeconds : 3600u;
  const std::array<uint32_t, 4> legendValues = {0u, std::max<uint32_t>(1u, legendReference / 4u),
                                                std::max<uint32_t>(1u, legendReference / 2u), legendReference};
  for (const uint32_t value : legendValues) {
    fillHeatCell(renderer, legendX, legendY, legendCell, value, legendReference);
    legendX += legendCell + 4;
  }
  renderer.drawText(SMALL_FONT_ID, legendX + 2, legendY, "More");

  constexpr int desiredMetricsHeight = 84;
  const int metricsTop = std::max(summaryTop + legendHeight + 12, contentBottom - desiredMetricsHeight);
  const int metricsHeight = std::max(1, contentBottom - metricsTop);
  const int thirdWidth = contentWidth / 3;
  renderer.drawRect(contentX, metricsTop, contentWidth, metricsHeight);
  renderer.drawLine(contentX + thirdWidth, metricsTop, contentX + thirdWidth, metricsTop + metricsHeight);
  renderer.drawLine(contentX + thirdWidth * 2, metricsTop, contentX + thirdWidth * 2, metricsTop + metricsHeight);
  char value[24];
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(streaks.activeDays));
  drawStatCell(renderer, contentX, thirdWidth, metricsTop, metricsHeight, value, tr(STR_STATS_ACTIVE_DAYS_LBL));
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(streaks.current));
  drawStatCell(renderer, contentX + thirdWidth, thirdWidth, metricsTop, metricsHeight, value,
               tr(STR_STATS_CURRENT_READ_STREAK_LBL));
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(streaks.longest));
  drawStatCell(renderer, contentX + thirdWidth * 2, contentWidth - thirdWidth * 2, metricsTop, metricsHeight, value,
               tr(STR_STATS_LONGEST_READ_STREAK_LBL));
  drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
}

void renderMonthlyReadingCalendarPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                      const ReadingJournal* journal, const GlobalReadingStats& history,
                                      const ReadingSessionSnapshot& session, const uint8_t goalMinutes,
                                      const bool showButtonHints, const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_MONTHLY_CALENDAR), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentX = metrics.contentSidePadding;
  const int contentWidth = renderer.getScreenWidth() - contentX * 2;
  const int contentTop = statsContentTop(metrics, showButtonHints) + 2;
  const int contentBottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);

  ReadingStatsDateTime now;
  if (!getCurrentLocalReadingStatsDateTime(now)) {
    drawCenteredLabel(renderer, UI_10_FONT_ID, contentX, contentWidth, contentTop + 120, tr(STR_STATS_NOT_ENOUGH_DATA),
                      true);
    drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
    return;
  }

  const uint32_t today = readingStatsDayIndex(now.date);
  const uint32_t goalSeconds = static_cast<uint32_t>(goalMinutes) * 60u;
  const ReadingStatsDate firstDate{now.date.year, now.date.month, 1};
  const uint32_t firstDayIndex = readingStatsDayIndex(firstDate);
  const uint8_t monthDays = daysInMonth(now.date.year, now.date.month);
  const uint8_t firstDow = readingStatsDayOfWeekIndex(firstDate);

  uint32_t monthSeconds = 0;
  uint32_t bestDaySeconds = 0;
  uint16_t daysRead = 0;
  for (uint8_t day = 1; day <= monthDays; ++day) {
    const uint32_t dayIndex = firstDayIndex + static_cast<uint32_t>(day - 1);
    if (dayIndex > today) break;
    const uint32_t seconds = heatmapSecondsForDay(journal, history, session, dayIndex, today);
    monthSeconds = addSaturatedValue(monthSeconds, seconds);
    if (seconds > 0) daysRead++;
    bestDaySeconds = std::max(bestDaySeconds, seconds);
  }

  const ReadingJournalPeriod calendarMonth =
      journal ? journal->periodEndingOn(today, now.date.day) : ReadingJournalPeriod{};

  char monthToken[12] = "-";
  formatReadingStatsMonthToken(now.date, monthToken, sizeof(monthToken));
  char monthTitle[32];
  snprintf(monthTitle, sizeof(monthTitle), "%s %u", monthToken, static_cast<unsigned>(now.date.year));
  drawCenteredLabel(renderer, UI_12_FONT_ID, contentX, contentWidth, contentTop, monthTitle, true);

  const int monthTitleHeight = renderer.getLineHeight(UI_12_FONT_ID) + 8;
  const int summaryTop = contentTop + monthTitleHeight;
  constexpr int summaryHeight = 112;
  const int halfWidth = contentWidth / 2;
  const int halfHeight = summaryHeight / 2;
  renderer.drawRect(contentX, summaryTop, contentWidth, summaryHeight);
  renderer.drawLine(contentX + halfWidth, summaryTop, contentX + halfWidth, summaryTop + summaryHeight);
  renderer.drawLine(contentX, summaryTop + halfHeight, contentX + contentWidth, summaryTop + halfHeight);
  char value[32];
  BookReadingStats::formatDuration(monthSeconds, value, sizeof(value));
  drawStatCell(renderer, contentX, halfWidth, summaryTop, halfHeight, value, tr(STR_STATS_MONTH_TOTAL_LBL));
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(daysRead));
  drawStatCell(renderer, contentX + halfWidth, contentWidth - halfWidth, summaryTop, halfHeight, value,
               tr(STR_STATS_DAYS_READ_LBL));
  BookReadingStats::formatDuration(bestDaySeconds, value, sizeof(value));
  drawStatCell(renderer, contentX, halfWidth, summaryTop + halfHeight, summaryHeight - halfHeight, value,
               tr(STR_STATS_BEST_DAY_LBL));
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(calendarMonth.completedBooks));
  drawStatCell(renderer, contentX + halfWidth, contentWidth - halfWidth, summaryTop + halfHeight,
               summaryHeight - halfHeight, value, tr(STR_STATS_BOOKS_FINISHED_LBL));

  const int dayHeaderTop = summaryTop + summaryHeight + 14;
  constexpr int dayHeaderHeight = 24;
  const int calendarTop = dayHeaderTop + dayHeaderHeight;
  const int calendarHeight = std::max(1, contentBottom - calendarTop);
  const int cellWidth = contentWidth / 7;
  const int cellHeight = std::max(34, calendarHeight / 6);
  for (int column = 0; column < 7; ++column) {
    const int x = contentX + column * cellWidth;
    const int width = column == 6 ? contentX + contentWidth - x : cellWidth;
    const char* dayLabel = I18N.get(DAY_LABELS[static_cast<size_t>(column)]);
    char shortLabel[2] = {dayLabel[0], '\0'};
    const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, shortLabel, EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, x + (width - labelWidth) / 2, dayHeaderTop, shortLabel, true, EpdFontFamily::BOLD);
  }

  for (uint8_t day = 1; day <= monthDays; ++day) {
    const int slot = static_cast<int>(firstDow) + static_cast<int>(day) - 1;
    const int row = slot / 7;
    const int column = slot % 7;
    const int x = contentX + column * cellWidth;
    const int width = column == 6 ? contentX + contentWidth - x : cellWidth;
    const int y = calendarTop + row * cellHeight;
    const uint32_t dayIndex = firstDayIndex + static_cast<uint32_t>(day - 1);
    const uint32_t seconds = dayIndex <= today ? heatmapSecondsForDay(journal, history, session, dayIndex, today) : 0;
    const bool metGoal = goalSeconds > 0 && seconds >= goalSeconds;
    renderer.drawRect(x, y, width, cellHeight);
    if (seconds > 0) {
      if (metGoal) {
        renderer.fillRect(x + 2, y + 2, width - 4, cellHeight - 4, true);
      } else {
        const Color shade = goalSeconds > 0 && seconds >= goalSeconds / 2u ? Color::DarkGray : Color::LightGray;
        renderer.fillRectDither(x + 2, y + 2, width - 4, cellHeight - 4, shade);
      }
    }
    if (day == now.date.day) renderer.drawRect(x + 1, y + 1, width - 2, cellHeight - 2, 2, !metGoal);

    char dayNumber[4];
    snprintf(dayNumber, sizeof(dayNumber), "%u", static_cast<unsigned>(day));
    renderer.drawText(SMALL_FONT_ID, x + 5, y + 4, dayNumber, !metGoal, EpdFontFamily::BOLD);
    if (seconds >= 60) {
      char minutes[12];
      snprintf(minutes, sizeof(minutes), "%lum", static_cast<unsigned long>(seconds / 60u));
      const int minutesWidth = renderer.getTextWidth(SMALL_FONT_ID, minutes);
      renderer.drawText(SMALL_FONT_ID, x + width - minutesWidth - 5,
                        y + cellHeight - renderer.getLineHeight(SMALL_FONT_ID) - 3, minutes, !metGoal);
    }
  }

  if (showButtonHints && mappedInput) {
    const auto labels =
        mappedInput->mapLabels(tr(STR_BACK), tr(STR_STATS_DETAILS), tr(STR_PREVIOUS_TAB), tr(STR_NEXT_TAB));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
}

void renderReadingDayDetailsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                 const ReadingStatsDate& date, const ReadingLedgerDaySummary& summary,
                                 const uint8_t selectedBook, const bool editMode,
                                 const int32_t pendingCorrectionSeconds, const bool canAdjust,
                                 const bool showButtonHints) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, editMode ? tr(STR_STATS_ADJUST_READING_TIME) : tr(STR_STATS_DAY_DETAILS), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int x = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - x * 2;
  const int top = CompactHeader::contentTop(metrics) + 8;
  const int bottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);

  char dateText[24];
  formatReadingStatsShortDate(date, dateText, sizeof(dateText));
  drawCenteredLabel(renderer, UI_12_FONT_ID, x, width, top, dateText, true);
  const int dateBottom = top + renderer.getLineHeight(UI_12_FONT_ID) + 10;

  if (editMode && summary.bookCount > 0 && selectedBook < summary.bookCount) {
    const ReadingLedgerDayBook& book = summary.books[selectedBook];
    const int titleHeight = renderer.getLineHeight(UI_10_FONT_ID) * 2 + 10;
    const std::vector<std::string> titleLines = renderer.wrappedText(UI_10_FONT_ID, book.title, width - 20, 2);
    int titleY = dateBottom;
    for (const auto& line : titleLines) {
      drawCenteredLabel(renderer, UI_10_FONT_ID, x, width, titleY, line.c_str(), true);
      titleY += renderer.getLineHeight(UI_10_FONT_ID);
    }

    const int gridTop = dateBottom + titleHeight;
    const int gridHeight = std::min(180, std::max(120, bottom - gridTop - 100));
    const int cellWidth = width / 3;
    renderer.drawRect(x, gridTop, width, gridHeight);
    renderer.drawLine(x + cellWidth, gridTop, x + cellWidth, gridTop + gridHeight);
    renderer.drawLine(x + cellWidth * 2, gridTop, x + cellWidth * 2, gridTop + gridHeight);

    char current[24];
    char change[24];
    char revised[24];
    formatCompactDuration(book.readingSeconds, current, sizeof(current));
    const uint32_t magnitude = static_cast<uint32_t>(
        pendingCorrectionSeconds < 0 ? -static_cast<int64_t>(pendingCorrectionSeconds) : pendingCorrectionSeconds);
    char magnitudeText[20];
    formatCompactDuration(magnitude, magnitudeText, sizeof(magnitudeText));
    if (pendingCorrectionSeconds == 0) {
      snprintf(change, sizeof(change), "0m");
    } else {
      snprintf(change, sizeof(change), "%c%s", pendingCorrectionSeconds < 0 ? '-' : '+', magnitudeText);
    }
    const int64_t revisedSigned = static_cast<int64_t>(book.readingSeconds) + pendingCorrectionSeconds;
    formatCompactDuration(static_cast<uint32_t>(std::max<int64_t>(0, revisedSigned)), revised, sizeof(revised));
    drawStatCell(renderer, x, cellWidth, gridTop, gridHeight, current, tr(STR_STATS_CURRENT_TIME_LBL));
    drawStatCell(renderer, x + cellWidth, cellWidth, gridTop, gridHeight, change, tr(STR_STATS_CHANGE_LBL));
    drawStatCell(renderer, x + cellWidth * 2, width - cellWidth * 2, gridTop, gridHeight, revised,
                 tr(STR_STATS_NEW_TOTAL_LBL));

    char sideAdjustment[32];
    snprintf(sideAdjustment, sizeof(sideAdjustment), "-5m                 +5m");
    drawCenteredLabel(renderer, SMALL_FONT_ID, x, width, gridTop + gridHeight + 28, sideAdjustment);
    if (showButtonHints && mappedInput) {
      const auto labels = mappedInput->mapLabels(tr(STR_CANCEL), tr(STR_SAVE), "-1m", "+1m");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
    }
    return;
  }

  const uint32_t totalSeconds = addSaturatedValue(
      addSaturatedValue(summary.attributedSeconds, summary.otherBookSeconds), summary.legacyUnattributedSeconds);
  constexpr int summaryHeight = 92;
  const int halfWidth = width / 2;
  renderer.drawRect(x, dateBottom, width, summaryHeight);
  renderer.drawLine(x + halfWidth, dateBottom, x + halfWidth, dateBottom + summaryHeight);
  char value[32];
  BookReadingStats::formatDuration(totalSeconds, value, sizeof(value));
  drawStatCell(renderer, x, halfWidth, dateBottom, summaryHeight, value, tr(STR_STATS_TIME_LBL));
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(summary.bookCount + (summary.truncated ? 1 : 0)));
  drawStatCell(renderer, x + halfWidth, width - halfWidth, dateBottom, summaryHeight, value,
               tr(STR_STATS_BOOKS_ATTRIBUTED_LBL));

  const int listTop = dateBottom + summaryHeight + 12;
  const int listHeight = std::max(1, bottom - listTop);
  constexpr int visibleRows = 5;
  const int rowHeight = std::max(58, listHeight / visibleRows);
  const uint8_t startBook = summary.bookCount > visibleRows && selectedBook >= visibleRows
                                ? static_cast<uint8_t>(selectedBook - visibleRows + 1)
                                : 0;
  int row = 0;
  for (uint8_t i = startBook; i < summary.bookCount && row < visibleRows; ++i, ++row) {
    const ReadingLedgerDayBook& book = summary.books[i];
    const int rowY = listTop + row * rowHeight;
    renderer.drawRect(x, rowY, width, rowHeight, i == selectedBook ? 2 : 1, true);
    const int valueWidth = 94;
    const std::string visibleTitle =
        renderer.truncatedText(UI_10_FONT_ID, book.title, width - valueWidth - 24, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, x + 8, rowY + 8, visibleTitle.c_str(), true, EpdFontFamily::BOLD);
    formatCompactDuration(book.readingSeconds, value, sizeof(value));
    const int durationWidth = renderer.getTextWidth(UI_10_FONT_ID, value, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, x + width - durationWidth - 8, rowY + 8, value, true, EpdFontFamily::BOLD);
    if (book.screenPages > 0) {
      char pages[28];
      snprintf(pages, sizeof(pages), "%lu %s", static_cast<unsigned long>(book.screenPages), tr(STR_STATS_PAGES_LBL));
      renderer.drawText(SMALL_FONT_ID, x + 8, rowY + rowHeight - renderer.getLineHeight(SMALL_FONT_ID) - 6, pages);
    }
  }

  auto drawUnattributedRow = [&](const char* label, const uint32_t seconds) {
    if (seconds == 0 || row >= visibleRows) return;
    const int rowY = listTop + row * rowHeight;
    renderer.drawRect(x, rowY, width, rowHeight);
    const std::string visibleLabel = renderer.truncatedText(UI_10_FONT_ID, label, width - 110);
    renderer.drawText(UI_10_FONT_ID, x + 8, rowY + (rowHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                      visibleLabel.c_str());
    formatCompactDuration(seconds, value, sizeof(value));
    const int durationWidth = renderer.getTextWidth(UI_10_FONT_ID, value, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, x + width - durationWidth - 8,
                      rowY + (rowHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2, value, true, EpdFontFamily::BOLD);
    row++;
  };
  drawUnattributedRow(tr(STR_STATS_OTHER_BOOKS), summary.otherBookSeconds);
  drawUnattributedRow(tr(STR_STATS_OLDER_BOOK_UNKNOWN), summary.legacyUnattributedSeconds);

  if (summary.bookCount == 0 && summary.otherBookSeconds == 0 && summary.legacyUnattributedSeconds == 0) {
    drawCenteredLabel(renderer, UI_10_FONT_ID, x, width, listTop + listHeight / 2, tr(STR_STATS_NO_BOOK_DETAIL), true);
  }

  if (showButtonHints && mappedInput) {
    const auto labels = mappedInput->mapLabels(tr(STR_BACK), canAdjust ? tr(STR_STATS_ADJUST_TIME) : "",
                                               tr(STR_PREVIOUS_TAB), tr(STR_NEXT_TAB));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
}

void renderReadingProfilePage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                              const ReadingJournal* journal, const GlobalReadingStats& history,
                              const ReadingSessionSnapshot& session, const uint8_t goalMinutes,
                              const bool showButtonHints, const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_READING_PROFILE), true);
  ReadingStatsDateTime now;
  if (!getCurrentLocalReadingStatsDateTime(now)) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int contentX = metrics.contentSidePadding;
    const int contentWidth = renderer.getScreenWidth() - contentX * 2;
    const int contentTop = statsContentTop(metrics, showButtonHints) + 8;
    drawCenteredLabel(renderer, UI_10_FONT_ID, contentX, contentWidth, contentTop + 120, tr(STR_STATS_NOT_ENOUGH_DATA),
                      true);
    drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
    return;
  }

  const uint32_t today = readingStatsDayIndex(now.date);
  constexpr uint16_t profileDays = 7;
  uint32_t knownSeconds = 0;
  uint32_t bestDaySeconds = 0;
  uint16_t readDays = 0;
  uint16_t timedReadDays = 0;
  uint16_t goalDays = 0;
  const uint32_t goalSeconds = static_cast<uint32_t>(goalMinutes) * 60u;
  for (uint16_t offset = 0; offset < profileDays; ++offset) {
    const uint32_t dayIndex = today >= offset ? today - offset : 0;
    const uint32_t detailedSeconds = journalSecondsForDay(journal, dayIndex, today, session);
    if (activeOnDay(journal, history, session, dayIndex, today)) readDays++;
    if (detailedSeconds == 0) continue;
    timedReadDays++;
    knownSeconds = addSaturatedValue(knownSeconds, detailedSeconds);
    bestDaySeconds = std::max(bestDaySeconds, detailedSeconds);
    if (goalSeconds > 0 && detailedSeconds >= goalSeconds) goalDays++;
  }

  ReadingJournalPeriod period = journal ? journal->periodEndingOn(today, 7) : ReadingJournalPeriod{};
  addLiveSessionToPeriod(period, journal, session, today);
  const uint32_t averageReadDay = timedReadDays > 0 ? knownSeconds / timedReadDays : 0;
  const uint32_t averageSession = period.sessions > 0 ? period.readingSeconds / period.sessions : 0;
  const ReadingStreakSummary streaks = summarizeReadingStreaks(journal, history, session, today, READING_HISTORY_DAYS);

  const MetricGrid grid = beginMetricGrid(renderer, 4, 3, tr(STR_STATS_LAST_7_DAYS), showButtonHints);
  char value[40];
  snprintf(value, sizeof(value), "%u / 7", static_cast<unsigned>(readDays));
  drawMetric(renderer, grid, 3, 0, value, tr(STR_STATS_READING_DAYS_LBL));
  snprintf(value, sizeof(value), "%u / 7", static_cast<unsigned>(goalDays));
  drawMetric(renderer, grid, 3, 1, value, tr(STR_STATS_GOAL_DAYS_WEEK_LBL));
  BookReadingStats::formatDuration(knownSeconds, value, sizeof(value));
  drawMetric(renderer, grid, 3, 2, knownSeconds > 0 ? value : "-", tr(STR_STATS_KNOWN_TIME_LBL));

  BookReadingStats::formatDuration(averageReadDay, value, sizeof(value));
  drawMetric(renderer, grid, 3, 3, averageReadDay > 0 ? value : "-", tr(STR_STATS_AVG_READING_DAY_LBL));
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(period.sessions));
  drawMetric(renderer, grid, 3, 4, value, tr(STR_STATS_SESSIONS_LBL));
  BookReadingStats::formatDuration(averageSession, value, sizeof(value));
  drawMetric(renderer, grid, 3, 5, averageSession > 0 ? value : "-", tr(STR_STATS_AVG_SESSION_LBL));

  BookReadingStats::formatDuration(bestDaySeconds, value, sizeof(value));
  drawMetric(renderer, grid, 3, 6, bestDaySeconds > 0 ? value : "-", tr(STR_STATS_BEST_DAY_LBL));
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(period.completedBooks));
  drawMetric(renderer, grid, 3, 7, value, tr(STR_STATS_COMPLETED_LBL));
  snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(period.screenPages));
  drawMetric(renderer, grid, 3, 8, value, tr(STR_STATS_PAGES_LBL));

  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(streaks.current));
  drawMetric(renderer, grid, 3, 9, value, tr(STR_STATS_CURRENT_READ_STREAK_LBL));
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(streaks.longest));
  drawMetric(renderer, grid, 3, 10, value, tr(STR_STATS_LONGEST_READ_STREAK_LBL));
  snprintf(value, sizeof(value), "%u / %u", static_cast<unsigned>(timedReadDays), static_cast<unsigned>(readDays));
  drawMetric(renderer, grid, 3, 11, value, tr(STR_STATS_TIMED_READ_DAYS_LBL));

  drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
}

void renderReadingTrendsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                             const ReadingJournal* journal, const GlobalReadingStats& history,
                             const ReadingSessionSnapshot& session, const bool showButtonHints,
                             const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_READING_TRENDS), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int x = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
  const int top = statsContentTop(metrics, showButtonHints);
  const int availableHeight = renderer.getScreenHeight() - top - statsBottomInset(metrics, showButtonHints);
  constexpr int cardGap = 10;

  ReadingStatsDateTime now;
  const bool hasNow = getCurrentLocalReadingStatsDateTime(now);
  const uint32_t today = hasNow ? readingStatsDayIndex(now.date) : 0;
  ReadingJournalPeriod todayPeriod = journal && hasNow ? journal->periodEndingOn(today, 1) : ReadingJournalPeriod{};
  ReadingJournalPeriod weekPeriod = journal && hasNow ? journal->periodEndingOn(today, 7) : ReadingJournalPeriod{};
  ReadingJournalPeriod monthPeriod = journal && hasNow ? journal->periodEndingOn(today, 30) : ReadingJournalPeriod{};
  const ReadingStatsDate yearStart = hasNow ? ReadingStatsDate{now.date.year, 1, 1} : ReadingStatsDate{};
  const uint16_t yearDays = hasNow ? static_cast<uint16_t>(today - readingStatsDayIndex(yearStart) + 1u) : 0;
  ReadingJournalPeriod yearPeriod =
      journal && hasNow ? journal->periodEndingOn(today, yearDays) : ReadingJournalPeriod{};
  addLiveSessionToPeriod(todayPeriod, journal, session, today);
  addLiveSessionToPeriod(weekPeriod, journal, session, today);
  addLiveSessionToPeriod(monthPeriod, journal, session, today);
  addLiveSessionToPeriod(yearPeriod, journal, session, today);
  if (hasNow) {
    todayPeriod.activeDays = activeDaysEndingOn(journal, history, session, today, 1);
    weekPeriod.activeDays = activeDaysEndingOn(journal, history, session, today, 7);
    monthPeriod.activeDays = activeDaysEndingOn(journal, history, session, today, 30);
    yearPeriod.activeDays = activeDaysEndingOn(journal, history, session, today, yearDays);
  }

  const int fourCardHeight = std::max(1, (availableHeight - cardGap * 3) / 4);
  drawPeriodCard(renderer, x, top, width, fourCardHeight, tr(STR_STATS_TODAY), todayPeriod);
  drawPeriodCard(renderer, x, top + fourCardHeight + cardGap, width, fourCardHeight, tr(STR_STATS_LAST_7_DAYS),
                 weekPeriod);
  drawPeriodCard(renderer, x, top + (fourCardHeight + cardGap) * 2, width, fourCardHeight, tr(STR_STATS_LAST_30_DAYS),
                 monthPeriod);
  drawPeriodCard(renderer, x, top + (fourCardHeight + cardGap) * 3, width,
                 availableHeight - (fourCardHeight + cardGap) * 3, tr(STR_STATS_THIS_YEAR), yearPeriod);
  drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
}

void renderReadingGoalsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const ReadingJournal* journal,
                            const GlobalReadingStats& history, const ReadingSessionSnapshot& session,
                            const uint8_t goalMinutes, const bool showButtonHints, const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_GOALS), true);
  const MetricGrid grid = beginMetricGrid(renderer, 3, 3, nullptr, showButtonHints);
  char buf[40];
  const uint32_t goalSeconds = static_cast<uint32_t>(goalMinutes) * 60u;

  ReadingStatsDateTime now;
  const bool hasNow = getCurrentLocalReadingStatsDateTime(now);
  const uint32_t today = hasNow ? readingStatsDayIndex(now.date) : 0;
  const uint32_t recordedToday = journal && hasNow ? journal->secondsOnDay(today) : 0;
  const uint32_t todaySeconds = addSaturatedValue(recordedToday, session.readingSeconds);
  ReadingJournalPeriod month = journal && hasNow ? journal->periodEndingOn(today, 30) : ReadingJournalPeriod{};
  if (hasNow) {
    addLiveSessionToPeriod(month, journal, session, today);
    month.activeDays = activeDaysEndingOn(journal, history, session, today, 30);
  }

  snprintf(buf, sizeof(buf), "%u min", static_cast<unsigned>(goalMinutes));
  drawMetric(renderer, grid, 3, 0, buf, tr(STR_STATS_GOAL_LBL));
  BookReadingStats::formatDuration(todaySeconds, buf, sizeof(buf));
  drawMetric(renderer, grid, 3, 1, todaySeconds > 0 ? buf : "-", tr(STR_STATS_TODAY_GOAL_LBL));
  const uint32_t goalPercent =
      goalSeconds > 0
          ? static_cast<uint32_t>(std::min<uint64_t>(999, static_cast<uint64_t>(todaySeconds) * 100u / goalSeconds))
          : 0;
  snprintf(buf, sizeof(buf), "%lu%%", static_cast<unsigned long>(goalPercent));
  drawMetric(renderer, grid, 3, 2, buf, tr(STR_STATS_GOAL_PROGRESS_LBL));

  uint16_t currentStreak = journal && hasNow ? journal->currentGoalStreak(today, goalSeconds) : 0;
  if (journal && hasNow && recordedToday < goalSeconds && todaySeconds >= goalSeconds) {
    currentStreak = 1;
    uint32_t cursor = today;
    while (cursor > 0 && currentStreak < ReadingJournal::HISTORY_DAYS) {
      cursor--;
      if (journal->secondsOnDay(cursor) < goalSeconds) break;
      currentStreak++;
    }
  }
  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(currentStreak));
  drawMetric(renderer, grid, 3, 3, currentStreak > 0 ? buf : "-", tr(STR_STATS_CURRENT_GOAL_STREAK_LBL));
  const uint16_t longestStreak =
      std::max<uint16_t>(journal ? journal->longestGoalStreak(goalSeconds) : 0, currentStreak);
  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(longestStreak));
  drawMetric(renderer, grid, 3, 4, longestStreak > 0 ? buf : "-", tr(STR_STATS_LONGEST_GOAL_STREAK_LBL));
  const uint32_t liveLongest = session.screenPages > 0 ? session.readingSeconds : 0;
  const uint32_t longestSession = std::max<uint32_t>(journal ? journal->longestSession() : 0, liveLongest);
  BookReadingStats::formatDuration(longestSession, buf, sizeof(buf));
  drawMetric(renderer, grid, 3, 5, longestSession > 0 ? buf : "-", tr(STR_STATS_LONGEST_SESSION_LBL));

  uint16_t goalDays7 = journal && hasNow ? journal->goalDaysEndingOn(today, 7, goalSeconds) : 0;
  uint16_t goalDays30 = journal && hasNow ? journal->goalDaysEndingOn(today, 30, goalSeconds) : 0;
  if (recordedToday < goalSeconds && todaySeconds >= goalSeconds) {
    goalDays7++;
    goalDays30++;
  }
  snprintf(buf, sizeof(buf), "%u / 7", static_cast<unsigned>(goalDays7));
  drawMetric(renderer, grid, 3, 6, buf, tr(STR_STATS_GOAL_DAYS_WEEK_LBL));
  snprintf(buf, sizeof(buf), "%u / 30", static_cast<unsigned>(goalDays30));
  drawMetric(renderer, grid, 3, 7, buf, tr(STR_STATS_GOAL_DAYS_MONTH_LBL));
  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(month.activeDays));
  drawMetric(renderer, grid, 3, 8, buf, tr(STR_STATS_ACTIVE_DAYS_LBL));
  drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
}

void renderRecentReadingSessionsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                     const ReadingJournal* journal, const uint32_t totalSessions,
                                     const uint8_t startOffset, const bool showButtonHints, const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_RECENT_SESSIONS), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int x = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
  const int top = statsContentTop(metrics, showButtonHints);
  const int availableHeight = renderer.getScreenHeight() - top - statsBottomInset(metrics, showButtonHints);
  const uint8_t totalCount = journal ? journal->recentSessionCount() : 0;
  const uint8_t maxOffset = totalCount > BOOK_STATS_VISIBLE_SESSION_ROWS
                                ? static_cast<uint8_t>(totalCount - BOOK_STATS_VISIBLE_SESSION_ROWS)
                                : 0;
  const uint8_t offset = std::min(startOffset, maxOffset);
  const uint8_t count = std::min<uint8_t>(BOOK_STATS_VISIBLE_SESSION_ROWS, totalCount - offset);
  const int contextLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int contextHeight = contextLineHeight * 2 + 10;
  const int listTop = top + contextHeight;
  const int listHeight = std::max(1, availableHeight - contextHeight);
  const int rowHeight = std::max(1, listHeight / BOOK_STATS_VISIBLE_SESSION_ROWS);

  char summary[48];
  snprintf(summary, sizeof(summary), "%u detailed / %lu total", static_cast<unsigned>(totalCount),
           static_cast<unsigned long>(std::max<uint32_t>(totalSessions, totalCount)));
  renderer.drawText(SMALL_FONT_ID, x, top, summary, true, EpdFontFamily::BOLD);
  if (totalCount > BOOK_STATS_VISIBLE_SESSION_ROWS) {
    char position[32];
    snprintf(position, sizeof(position), "%u-%u / %u", static_cast<unsigned>(offset + 1),
             static_cast<unsigned>(offset + count), static_cast<unsigned>(totalCount));
    const int positionWidth = renderer.getTextWidth(SMALL_FONT_ID, position);
    renderer.drawText(SMALL_FONT_ID, x + width - positionWidth, top, position);
  }
  const std::string rule = renderer.truncatedText(SMALL_FONT_ID, tr(STR_STATS_SESSION_RULE_SHORT), width);
  renderer.drawText(SMALL_FONT_ID, x, top + contextLineHeight + 4, rule.c_str());

  if (count == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, listTop + listHeight / 2, tr(STR_STATS_NO_RECENT_SESSIONS));
  } else {
    for (uint8_t i = 0; i < count; ++i) {
      ReadingJournalSession entry;
      if (!journal->recentSession(static_cast<uint8_t>(offset + i), entry)) continue;
      const int y = listTop + i * rowHeight;
      renderer.drawLine(x, y + rowHeight - 1, x + width, y + rowHeight - 1);
      ReadingStatsDate date;
      char dateBuf[24];
      char timeBuf[16];
      char durationBuf[24];
      char pagesBuf[32];
      if (readingStatsDateFromDayIndex(entry.dayIndex, date)) {
        formatReadingStatsShortDate(date, dateBuf, sizeof(dateBuf));
      } else {
        snprintf(dateBuf, sizeof(dateBuf), "-");
      }
      formatSessionStartTime(entry.startMinute, timeBuf, sizeof(timeBuf));
      BookReadingStats::formatDuration(entry.readingSeconds, durationBuf, sizeof(durationBuf));
      snprintf(pagesBuf, sizeof(pagesBuf), "%u %s", static_cast<unsigned>(entry.screenPages), tr(STR_STATS_PAGES_LBL));
      const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
      const int firstY = y + std::max(4, (rowHeight - lineHeight * 2 - 4) / 2);
      renderer.drawText(UI_10_FONT_ID, x + 8, firstY, dateBuf, true, EpdFontFamily::BOLD);
      renderer.drawText(SMALL_FONT_ID, x + 8, firstY + lineHeight + 4, timeBuf);
      const int durationWidth = renderer.getTextWidth(UI_10_FONT_ID, durationBuf, EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, x + width - durationWidth - 8, firstY, durationBuf, true, EpdFontFamily::BOLD);
      const int pagesWidth = renderer.getTextWidth(SMALL_FONT_ID, pagesBuf);
      renderer.drawText(SMALL_FONT_ID, x + width - pagesWidth - 8, firstY + lineHeight + 4, pagesBuf);
    }
  }
  drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
}

void renderStartedBooksPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                            const std::vector<StartedBookStatsEntry>& books, const size_t selectedIndex,
                            const bool loading, const bool showButtonHints, const bool showMoreButton) {
  (void)showMoreButton;
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_STARTED_BOOKS), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int x = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - x * 2;
  const int top = statsContentTop(metrics, showButtonHints);
  const int bottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);
  const int headerLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int listTop = top + headerLineHeight * 2 + 12;
  const int listHeight = std::max(1, bottom - listTop);

  char countText[32];
  snprintf(countText, sizeof(countText), loading ? "%u in progress - loading" : "%u in progress",
           static_cast<unsigned>(books.size()));
  renderer.drawText(SMALL_FONT_ID, x, top, countText, true, EpdFontFamily::BOLD);
  const std::string actionText = renderer.truncatedText(SMALL_FONT_ID, tr(STR_STATS_OPEN_FOR_DETAILS), width);
  renderer.drawText(SMALL_FONT_ID, x, top + headerLineHeight + 3, actionText.c_str());

  if (books.empty()) {
    drawCenteredLabel(renderer, UI_10_FONT_ID, x, width, listTop + listHeight / 2,
                      loading ? tr(STR_LOADING) : tr(STR_STATS_NO_STARTED_BOOKS), true);
  } else {
    const size_t selected = std::min(selectedIndex, books.size() - 1);
    const size_t first =
        selected >= BOOK_STATS_VISIBLE_STARTED_BOOK_ROWS ? selected - BOOK_STATS_VISIBLE_STARTED_BOOK_ROWS + 1 : 0;
    const size_t visible = std::min(BOOK_STATS_VISIBLE_STARTED_BOOK_ROWS, books.size() - first);
    const int rowHeight = std::max(1, listHeight / static_cast<int>(BOOK_STATS_VISIBLE_STARTED_BOOK_ROWS));
    for (size_t row = 0; row < visible; ++row) {
      const size_t index = first + row;
      const StartedBookStatsEntry& book = books[index];
      const int rowY = listTop + static_cast<int>(row) * rowHeight;
      renderer.drawRect(x, rowY, width, rowHeight, index == selected ? 2 : 1, true);

      char statsText[72];
      char duration[20];
      formatCompactDuration(book.readingSeconds, duration, sizeof(duration));
      if (book.estFinishDate.isValid()) {
        char estDate[24];
        formatReadingStatsShortDate(book.estFinishDate, estDate, sizeof(estDate));
        snprintf(statsText, sizeof(statsText), "%s | %u %s | ~%s", book.readingSeconds > 0 ? duration : "-",
                 static_cast<unsigned>(book.sessions), tr(STR_STATS_SESSIONS_LBL), estDate);
      } else {
        snprintf(statsText, sizeof(statsText), "%s | %u %s", book.readingSeconds > 0 ? duration : "-",
                 static_cast<unsigned>(book.sessions), tr(STR_STATS_SESSIONS_LBL));
      }
      const int statsWidth = renderer.getTextWidth(SMALL_FONT_ID, statsText);
      const std::string title =
          renderer.truncatedText(UI_10_FONT_ID, book.title.c_str(), width - 20, EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, x + 8, rowY + 8, title.c_str(), true, EpdFontFamily::BOLD);

      const int detailY = rowY + rowHeight - renderer.getLineHeight(SMALL_FONT_ID) - 7;
      if (!book.author.empty()) {
        const std::string author =
            renderer.truncatedText(SMALL_FONT_ID, book.author.c_str(), std::max(1, width - statsWidth - 28));
        renderer.drawText(SMALL_FONT_ID, x + 8, detailY, author.c_str());
      }
      renderer.drawText(SMALL_FONT_ID, x + width - statsWidth - 8, detailY, statsText);
    }

    if (books.size() > BOOK_STATS_VISIBLE_STARTED_BOOK_ROWS) {
      char position[28];
      snprintf(position, sizeof(position), "%u-%u / %u", static_cast<unsigned>(first + 1),
               static_cast<unsigned>(first + visible), static_cast<unsigned>(books.size()));
      const int positionWidth = renderer.getTextWidth(SMALL_FONT_ID, position);
      renderer.drawText(SMALL_FONT_ID, x + width - positionWidth, top, position);
    }
  }

  if (showButtonHints && mappedInput) {
    const auto labels = mappedInput->mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_PREVIOUS_TAB), tr(STR_NEXT_TAB));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
}

void renderLibraryInsightsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                               const LibraryInsights* insights, const bool showButtonHints, const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_LIBRARY_INSIGHTS), true);
  if (!insights || !insights->available) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int top = statsContentTop(metrics, showButtonHints);
    renderer.drawCenteredText(UI_10_FONT_ID, top + (renderer.getScreenHeight() - top) / 2,
                              tr(STR_STATS_LIBRARY_CATALOG_MISSING));
    drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentX = metrics.contentSidePadding;
  const int contentWidth = renderer.getScreenWidth() - contentX * 2;
  const int contentTop = statsContentTop(metrics, showButtonHints) + 8;
  const int contentBottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);
  const int radius = std::clamp((contentBottom - contentTop) / 7, 52, 76);
  const float finishedRatio =
      insights->totalBooks > 0 ? static_cast<float>(insights->finishedBooks) / static_cast<float>(insights->totalBooks)
                               : 0.0f;
  char value[40];
  snprintf(value, sizeof(value), "%d%%", static_cast<int>(finishedRatio * 100.0f + 0.5f));
  drawDonutGauge(renderer, renderer.getScreenWidth() / 2, contentTop + radius, radius, std::max(8, radius / 7),
                 finishedRatio, value);

  const int gridTop = contentTop + radius * 2 + 16;
  const int gridHeight = std::max(1, contentBottom - gridTop);
  const int columnWidth = contentWidth / 3;
  const int rowHeight = gridHeight / 2;
  renderer.drawRect(contentX, gridTop, contentWidth, gridHeight);
  renderer.drawLine(contentX + columnWidth, gridTop, contentX + columnWidth, contentBottom);
  renderer.drawLine(contentX + columnWidth * 2, gridTop, contentX + columnWidth * 2, contentBottom);
  renderer.drawLine(contentX, gridTop + rowHeight, contentX + contentWidth, gridTop + rowHeight);

  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(insights->totalBooks));
  drawStatCell(renderer, contentX, columnWidth, gridTop, rowHeight, value, tr(STR_STATS_LIBRARY_BOOKS_LBL));
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(insights->unreadBooks));
  drawStatCell(renderer, contentX + columnWidth, columnWidth, gridTop, rowHeight, value, tr(STR_STATS_UNREAD));
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(insights->readingBooks));
  drawStatCell(renderer, contentX + columnWidth * 2, contentWidth - columnWidth * 2, gridTop, rowHeight, value,
               tr(STR_STATS_READING));
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(insights->finishedBooks));
  drawStatCell(renderer, contentX, columnWidth, gridTop + rowHeight, gridHeight - rowHeight, value,
               tr(STR_STATS_FINISHED));
  BookReadingStats::formatDuration(insights->totalReadingSeconds, value, sizeof(value));
  drawStatCell(renderer, contentX + columnWidth, columnWidth, gridTop + rowHeight, gridHeight - rowHeight,
               insights->totalReadingSeconds > 0 ? value : "-", tr(STR_STATS_TOTAL_READING_TIME_LBL));
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(insights->seriesStarted));
  drawStatCell(renderer, contentX + columnWidth * 2, contentWidth - columnWidth * 2, gridTop + rowHeight,
               gridHeight - rowHeight, value, tr(STR_STATS_SERIES_STARTED_LBL));
  drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
}

void renderReadingTastePage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                            const LibraryInsights* insights, const bool showButtonHints, const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_READING_TASTE), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int x = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
  const int top = statsContentTop(metrics, showButtonHints);
  const int availableHeight = renderer.getScreenHeight() - top - statsBottomInset(metrics, showButtonHints);

  if (!insights || !insights->available) {
    renderer.drawCenteredText(UI_10_FONT_ID, top + availableHeight / 2, tr(STR_STATS_LIBRARY_CATALOG_MISSING));
    drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
    return;
  }

  constexpr int gap = 8;
  constexpr int genreRows = 3;
  constexpr int spiceRows = 6;
  constexpr int authorRows = 3;
  constexpr int titleHeight = 32;
  constexpr int baseRowHeight = 34;
  const int baseGenreHeight = titleHeight + genreRows * baseRowHeight;
  const int baseSpiceHeight = titleHeight + spiceRows * baseRowHeight;
  const int baseAuthorHeight = titleHeight + authorRows * baseRowHeight;
  const int extra = std::max(0, availableHeight - gap * 2 - baseGenreHeight - baseSpiceHeight - baseAuthorHeight);
  const int genreHeight = baseGenreHeight + extra / 3;
  const int spiceHeight = baseSpiceHeight + extra / 3;
  const int authorHeight = availableHeight - gap * 2 - genreHeight - spiceHeight;

  drawInsightListCard(renderer, x, top, width, genreHeight, tr(STR_STATS_FAVORITE_GENRES), insights->topGenres.data(),
                      insights->topGenreCount, genreRows);
  drawInsightListCard(renderer, x, top + genreHeight + gap, width, spiceHeight, tr(STR_STATS_SPICE_BY_TIME),
                      insights->spiceLevels.data(), insights->spiceLevelCount, spiceRows);
  drawInsightListCard(renderer, x, top + genreHeight + gap + spiceHeight + gap, width, authorHeight,
                      tr(STR_STATS_FAVORITE_AUTHORS), insights->topAuthors.data(), insights->topAuthorCount,
                      authorRows);
  drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
}

void renderSeriesProgressPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                              const LibraryInsights* insights, const size_t startOffset, const bool showButtonHints,
                              const bool showMoreButton) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_STATS_SERIES_PROGRESS), true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int x = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
  const int top = statsContentTop(metrics, showButtonHints);
  const int availableHeight = renderer.getScreenHeight() - top - statsBottomInset(metrics, showButtonHints);

  if (!insights || !insights->available) {
    renderer.drawCenteredText(UI_10_FONT_ID, top + availableHeight / 2, tr(STR_STATS_LIBRARY_CATALOG_MISSING));
  } else if (insights->seriesProgressCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, top + availableHeight / 2, tr(STR_STATS_NO_SERIES_PROGRESS));
  } else {
    const size_t maxOffset = insights->seriesProgressCount > BOOK_STATS_VISIBLE_SERIES_ROWS
                                 ? insights->seriesProgressCount - BOOK_STATS_VISIBLE_SERIES_ROWS
                                 : 0;
    const size_t offset = std::min(startOffset, maxOffset);
    const size_t visibleCount = std::min(BOOK_STATS_VISIBLE_SERIES_ROWS, insights->seriesProgressCount - offset);
    const int positionHeight =
        insights->seriesProgressCount > BOOK_STATS_VISIBLE_SERIES_ROWS ? renderer.getLineHeight(SMALL_FONT_ID) + 5 : 0;
    const int listTop = top + positionHeight;
    const int listHeight = std::max(1, availableHeight - positionHeight);
    const int rowSlots = static_cast<int>(std::min(BOOK_STATS_VISIBLE_SERIES_ROWS, insights->seriesProgressCount));
    const int rowHeight = std::max(1, listHeight / std::max(1, rowSlots));
    if (positionHeight > 0) {
      char position[32];
      snprintf(position, sizeof(position), "%u-%u / %u", static_cast<unsigned>(offset + 1),
               static_cast<unsigned>(offset + visibleCount), static_cast<unsigned>(insights->seriesProgressCount));
      const int positionWidth = renderer.getTextWidth(SMALL_FONT_ID, position);
      renderer.drawText(SMALL_FONT_ID, x + width - positionWidth, top, position);
    }
    for (size_t i = 0; i < visibleCount; ++i) {
      const LibraryInsightItem& item = insights->seriesProgress[offset + i];
      const int rowY = listTop + static_cast<int>(i) * rowHeight;
      if (i > 0) renderer.drawLine(x, rowY, x + width, rowY);
      char progress[32];
      snprintf(progress, sizeof(progress), "%u / %u %s", static_cast<unsigned>(item.finished),
               static_cast<unsigned>(item.books), tr(STR_STATS_FINISHED));
      const int progressWidth = renderer.getTextWidth(SMALL_FONT_ID, progress);
      const std::string name =
          renderer.truncatedText(UI_10_FONT_ID, item.name.c_str(), std::max(1, width - progressWidth - 30));
      renderer.drawText(UI_10_FONT_ID, x + 8, rowY + 12, name.c_str(), true, EpdFontFamily::BOLD);
      renderer.drawText(SMALL_FONT_ID, x + width - progressWidth - 8, rowY + 14, progress);
      const int barY = rowY + rowHeight - 18;
      const int barWidth = width - 16;
      renderer.drawRect(x + 8, barY, barWidth, 7);
      if (item.books > 0 && item.finished > 0) {
        const int fillWidth =
            std::max(1, static_cast<int>((static_cast<uint32_t>(barWidth - 2) * item.finished) / item.books));
        renderer.fillRect(x + 9, barY + 1, fillWidth, 5, true);
      }
    }
  }
  drawStatsButtonHints(renderer, mappedInput, showButtonHints, false, showMoreButton);
}

void renderPerBookStatsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const std::string& bookTitle,
                            const BookReadingStats& stats, const float progressPercent, const bool hasEstimatedTimeLeft,
                            const uint32_t estimatedTimeLeftSeconds, const bool showButtonHints,
                            const bool showEditButton, const bool showMoreButton, const uint32_t bookWordCount) {
  renderer.clearScreen();
  const bool showRtcStats = shouldShowRtcBasedStats();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& layout = getStatsLayout(renderer, false, showButtonHints, showRtcStats);
  CompactHeader::drawTitle(renderer, tr(STR_READING_STATS), true);
  const int screenW = renderer.getScreenWidth();
  const int cardX = metrics.contentSidePadding;
  const int cardW = screenW - metrics.contentSidePadding * 2;
  const int availableHeight = renderer.getScreenHeight() - metrics.topPadding -
                              statsBottomInset(metrics, showButtonHints) - statsTabReserve(metrics, showButtonHints);
  int topCardH = layout.topCardH;
  int y = metrics.topPadding + std::min(metrics.headerHeight, layout.headerHeight) +
          statsTabReserve(metrics, showButtonHints) + layout.topGap;

  if (showRtcStats) {
    const int timeOfDayH = sectionCardHeight(renderer, layout, static_cast<int>(TIME_BUCKET_LABELS.size()));
    const int dayOfWeekH = sectionCardHeight(renderer, layout, static_cast<int>(DAY_LABELS.size()));
    const int compactContentHeight = std::min(metrics.headerHeight, layout.headerHeight) + layout.topGap +
                                     layout.topCardH + layout.cardGap + timeOfDayH + layout.cardGap + dayOfWeekH;
    const int extraHeight = std::max(0, availableHeight - compactContentHeight);
    const int extraTopCardHeight = std::min(extraHeight, kPerBookRtcTopCardMaxExtra);
    const int remainingExtraHeight = extraHeight - extraTopCardHeight;
    const int timeOfDayExtraHeight = (remainingExtraHeight * 4) / 11;
    const int dayOfWeekExtraHeight = remainingExtraHeight - timeOfDayExtraHeight;
    const int timeOfDayCardH = timeOfDayH + timeOfDayExtraHeight;
    const int dayOfWeekCardH = dayOfWeekH + dayOfWeekExtraHeight;
    topCardH += extraTopCardHeight;

    drawPerBookStatsCard(renderer, cardX, y, cardW, topCardH, bookTitle, stats, progressPercent, hasEstimatedTimeLeft,
                         estimatedTimeLeftSeconds, layout, bookWordCount);
    y += topCardH + layout.cardGap;

    drawSectionCard(renderer, cardX, y, cardW, timeOfDayCardH, tr(STR_STATS_TIME_OF_DAY), layout);
    drawHorizontalBars(renderer, cardX, y, cardW, timeOfDayCardH, stats.timeOfDaySeconds, TIME_BUCKET_LABELS, layout);
    y += timeOfDayCardH + layout.cardGap;

    drawSectionCard(renderer, cardX, y, cardW, dayOfWeekCardH, tr(STR_STATS_DAY_OF_WEEK), layout);
    drawHorizontalBars(renderer, cardX, y, cardW, dayOfWeekCardH, stats.dayOfWeekSeconds, DAY_LABELS, layout);
  } else {
    const int compactContentHeight =
        std::min(metrics.headerHeight, layout.headerHeight) + layout.topGap + layout.topCardH;
    const int extraHeight = std::max(0, availableHeight - compactContentHeight);
    if (showButtonHints) {
      topCardH += extraHeight;
    } else {
      // The sleep-screen variant has no footer controls, so on tall portrait displays the
      // single card can balloon and create huge internal gaps between the two stat rows.
      // Cap the card growth and spend the rest as outer margin instead.
      const int maxStandaloneCardHeight =
          std::max(layout.topCardH, renderer.getScreenHeight() / kStandaloneNoRtcMaxTopCardHeightDivisor);
      topCardH = std::min(layout.topCardH + extraHeight, maxStandaloneCardHeight);
      const int unusedExtraHeight = extraHeight - (topCardH - layout.topCardH);
      y += std::min(unusedExtraHeight / 3, kStandaloneNoRtcMaxVerticalOffset);
    }
    drawPerBookStatsCard(renderer, cardX, y, cardW, topCardH, bookTitle, stats, progressPercent, hasEstimatedTimeLeft,
                         estimatedTimeLeftSeconds, layout, bookWordCount);
  }

  if (showButtonHints && mappedInput) {
    (void)showEditButton;
    (void)showMoreButton;
    const auto labels = mappedInput->mapLabels(tr(STR_BACK), "", tr(STR_PREVIOUS_TAB), tr(STR_NEXT_TAB));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
}

void renderGlobalStatsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const char* screenTitle,
                           const GlobalReadingStats& stats, const bool showButtonHints, const bool showMoreButton) {
  renderer.clearScreen();
  const bool showRtcStats = shouldShowRtcBasedStats();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& layout = getStatsLayout(renderer, true, showButtonHints, showRtcStats);
  CompactHeader::drawTitle(renderer, screenTitle);
  const int screenW = renderer.getScreenWidth();
  const int cardX = metrics.contentSidePadding;
  const int cardW = screenW - metrics.contentSidePadding * 2;
  const int availableHeight = renderer.getScreenHeight() - metrics.topPadding -
                              statsBottomInset(metrics, showButtonHints) - statsTabReserve(metrics, showButtonHints);
  int globalCardH = layout.globalCardH;
  int y = metrics.topPadding + std::min(metrics.headerHeight, layout.headerHeight) +
          statsTabReserve(metrics, showButtonHints) + layout.topGap;

  if (showRtcStats) {
    const int timeOfDayH = sectionCardHeight(renderer, layout, static_cast<int>(TIME_BUCKET_LABELS.size()));
    const int dayOfWeekH = sectionCardHeight(renderer, layout, static_cast<int>(DAY_LABELS.size()));
    const int compactContentHeight = std::min(metrics.headerHeight, layout.headerHeight) + layout.topGap +
                                     layout.globalCardH + layout.cardGap + timeOfDayH + layout.cardGap + dayOfWeekH;
    const int extraHeight = std::max(0, availableHeight - compactContentHeight);
    const int perBookCompactContentHeight = std::min(metrics.headerHeight, layout.headerHeight) + layout.topGap +
                                            layout.topCardH + layout.cardGap + timeOfDayH + layout.cardGap + dayOfWeekH;
    const int perBookExtraHeight = std::max(0, availableHeight - perBookCompactContentHeight);
    const int targetGlobalCardH = globalRtcCardHeightForPerBookRowSpacing(layout, perBookExtraHeight);
    const int extraTopCardHeight = std::min(extraHeight, std::max(0, targetGlobalCardH - layout.globalCardH));
    const int remainingExtraHeight = extraHeight - extraTopCardHeight;
    const int timeOfDayExtraHeight = (remainingExtraHeight * 4) / 11;
    const int dayOfWeekExtraHeight = remainingExtraHeight - timeOfDayExtraHeight;
    const int timeOfDayCardH = timeOfDayH + timeOfDayExtraHeight;
    const int dayOfWeekCardH = dayOfWeekH + dayOfWeekExtraHeight;
    globalCardH += extraTopCardHeight;

    drawGlobalStatsCard(renderer, cardX, y, cardW, globalCardH, tr(STR_STATS_ALL_TIME), stats, layout);
    y += globalCardH + layout.cardGap;

    drawSectionCard(renderer, cardX, y, cardW, timeOfDayCardH, tr(STR_STATS_TIME_OF_DAY), layout);
    drawHorizontalBars(renderer, cardX, y, cardW, timeOfDayCardH, stats.timeOfDaySeconds, TIME_BUCKET_LABELS, layout);
    y += timeOfDayCardH + layout.cardGap;

    drawSectionCard(renderer, cardX, y, cardW, dayOfWeekCardH, tr(STR_STATS_DAY_OF_WEEK), layout);
    drawHorizontalBars(renderer, cardX, y, cardW, dayOfWeekCardH, stats.dayOfWeekSeconds, DAY_LABELS, layout);
  } else {
    const int compactContentHeight =
        std::min(metrics.headerHeight, layout.headerHeight) + layout.topGap + layout.globalCardH;
    globalCardH += std::max(0, availableHeight - compactContentHeight);
    drawGlobalStatsCard(renderer, cardX, y, cardW, globalCardH, tr(STR_STATS_ALL_TIME), stats, layout);
  }

  if (showButtonHints && mappedInput) {
    (void)showMoreButton;
    const auto labels = mappedInput->mapLabels(tr(STR_BACK), "", tr(STR_PREVIOUS_TAB), tr(STR_NEXT_TAB));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
}

void renderNoRtcCombinedStatsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                  const std::string& bookTitle, const BookReadingStats& bookStats,
                                  const float progressPercent, const bool hasEstimatedTimeLeft,
                                  const uint32_t estimatedTimeLeftSeconds, const GlobalReadingStats& deviceStats,
                                  const GlobalReadingStats* allDevicesStats, const bool showButtonHints,
                                  const uint32_t bookWordCount) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& layout = getNoRtcCombinedLayout(renderer, showButtonHints, allDevicesStats != nullptr);
  CompactHeader::drawTitle(renderer, tr(STR_READING_STATS));
  const int screenW = renderer.getScreenWidth();
  const int cardX = metrics.contentSidePadding;
  const int cardW = screenW - metrics.contentSidePadding * 2;
  const int availableHeight =
      renderer.getScreenHeight() - metrics.topPadding - statsBottomInset(metrics, showButtonHints);
  const int compactContentHeight = noRtcCombinedContentHeight(layout, allDevicesStats != nullptr);
  const int extraHeight = std::max(0, availableHeight - compactContentHeight);
  const int visibleCardCount = allDevicesStats ? 3 : 2;
  const int extraPerCard = visibleCardCount > 0 ? extraHeight / visibleCardCount : 0;
  const int extraRemainder = visibleCardCount > 0 ? extraHeight % visibleCardCount : 0;
  const int perBookExtraHeight = extraPerCard + (extraRemainder > 0 ? 1 : 0);
  const int deviceExtraHeight = extraPerCard + (extraRemainder > 1 ? 1 : 0);
  const int allDevicesExtraHeight = allDevicesStats ? extraPerCard : 0;
  const int perBookCardH = noRtcCardBaseHeight(layout) + perBookExtraHeight;
  const int deviceCardH = layout.globalCardH + deviceExtraHeight;
  const int allDevicesCardH = layout.globalCardH + allDevicesExtraHeight;

  int y = metrics.topPadding + std::min(metrics.headerHeight, layout.headerHeight) + layout.topGap;
  drawPerBookStatsCard(renderer, cardX, y, cardW, perBookCardH, bookTitle, bookStats, progressPercent,
                       hasEstimatedTimeLeft, estimatedTimeLeftSeconds, layout, bookWordCount);
  y += perBookCardH + layout.cardGap;

  drawGlobalStatsCard(renderer, cardX, y, cardW, deviceCardH, tr(STR_STATS_THIS_DEVICE_SCREEN), deviceStats, layout);
  y += deviceCardH;

  if (allDevicesStats) {
    y += layout.cardGap;
    drawGlobalStatsCard(renderer, cardX, y, cardW, allDevicesCardH, tr(STR_STATS_ALL_DEVICES_SCREEN), *allDevicesStats,
                        layout);
  }

  if (showButtonHints && mappedInput) {
    const auto labels = mappedInput->mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
}

void renderEditBookDatesPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const std::string& bookTitle,
                             const BookReadingStats& stats, const int selectedField, const bool editMode,
                             const bool showButtonHints) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_READING_STATS));

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int cardW = pageWidth - 120;
  const int cardH = 250;
  const int cardX = (pageWidth - cardW) / 2;
  const int titleY = statsContentTop(metrics, showButtonHints) + 4;
  const int cardY = titleY + renderer.getLineHeight(UI_12_FONT_ID) + 18;

  const std::string visibleTitle =
      renderer.truncatedText(UI_12_FONT_ID, bookTitle.c_str(), pageWidth - 80, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_12_FONT_ID, titleY, visibleTitle.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawRect(cardX, cardY, cardW, cardH);
  const char* modeLabel = editMode ? "EDITING" : "READ ONLY";
  const int modeWidth = renderer.getTextWidth(SMALL_FONT_ID, modeLabel, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, cardX + cardW - modeWidth - 10, cardY + 8, modeLabel, true, EpdFontFamily::BOLD);

  const int sectionGap = 104;
  const int row1Y = cardY + 66;
  const int row2Y = row1Y + sectionGap;
  const int monthW = 52;
  const int dayW = 46;
  const int yearW = 68;
  const int gap = 14;
  const int totalFieldW = monthW + gap + dayW + gap + yearW;
  const int fieldStartX = cardX + (cardW - totalFieldW) / 2;

  char monthBuf[8];
  char dayBuf[8];
  char yearBuf[8];

  drawCenteredLabel(renderer, UI_10_FONT_ID, cardX, cardW, cardY + 24, tr(STR_STATS_START_DATE), true);
  formatReadingStatsMonthToken(stats.startDate, monthBuf, sizeof(monthBuf));
  snprintf(dayBuf, sizeof(dayBuf), "%s", stats.startDate.isValid() ? "" : "-");
  if (stats.startDate.isValid()) {
    snprintf(dayBuf, sizeof(dayBuf), "%02u", static_cast<unsigned>(stats.startDate.day));
    snprintf(yearBuf, sizeof(yearBuf), "%u", static_cast<unsigned>(stats.startDate.year));
  } else {
    snprintf(dayBuf, sizeof(dayBuf), "-");
    snprintf(yearBuf, sizeof(yearBuf), "-");
  }
  drawDateField(renderer, fieldStartX, row1Y, monthW, monthBuf, editMode && selectedField == 0);
  drawDateField(renderer, fieldStartX + monthW + gap, row1Y, dayW, dayBuf, editMode && selectedField == 1);
  drawDateField(renderer, fieldStartX + monthW + gap + dayW + gap, row1Y, yearW, yearBuf,
                editMode && selectedField == 2);

  drawCenteredLabel(renderer, UI_10_FONT_ID, cardX, cardW, cardY + 24 + sectionGap, tr(STR_STATS_FINISHED_DATE), true);
  const bool showFinishedFields = stats.isCompleted && stats.finishedDate.isValid();
  formatReadingStatsMonthToken(showFinishedFields ? stats.finishedDate : ReadingStatsDate{}, monthBuf,
                               sizeof(monthBuf));
  if (showFinishedFields) {
    snprintf(dayBuf, sizeof(dayBuf), "%02u", static_cast<unsigned>(stats.finishedDate.day));
    snprintf(yearBuf, sizeof(yearBuf), "%u", static_cast<unsigned>(stats.finishedDate.year));
  } else {
    snprintf(dayBuf, sizeof(dayBuf), "-");
    snprintf(yearBuf, sizeof(yearBuf), "-");
  }
  drawDateField(renderer, fieldStartX, row2Y, monthW, monthBuf, editMode && selectedField == 3);
  drawDateField(renderer, fieldStartX + monthW + gap, row2Y, dayW, dayBuf, editMode && selectedField == 4);
  drawDateField(renderer, fieldStartX + monthW + gap + dayW + gap, row2Y, yearW, yearBuf,
                editMode && selectedField == 5);

  if (showButtonHints && mappedInput) {
    const auto labels =
        editMode ? mappedInput->mapLabels(tr(STR_DONE), tr(STR_NEXT_FIELD), tr(STR_PREVIOUS_TAB), tr(STR_NEXT_TAB))
                 : mappedInput->mapLabels(tr(STR_BACK), "Hold Edit", tr(STR_PREVIOUS_TAB), tr(STR_NEXT_TAB));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
}
