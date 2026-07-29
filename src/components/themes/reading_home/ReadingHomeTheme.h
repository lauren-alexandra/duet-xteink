#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "components/themes/minimal/MinimalTheme.h"

struct BookReadingStats;
struct GlobalReadingStats;
struct RecentBook;

namespace ReadingHomeMetrics {
constexpr ThemeMetrics makeValues() {
  ThemeMetrics v = MinimalMetrics::values;
  // The home card needs four lightweight recent-book records: the continuing
  // book plus the three covers in the recent row.
  v.homeTopPadding = 34;
  v.homeCoverHeight = 188;
  v.homeCoverTileHeight = 0;
  v.homeRecentBooksCount = 4;
  v.homeContinueReadingInMenu = false;
  v.homeMenuTopOffset = 0;
  return v;
}

constexpr ThemeMetrics values = makeValues();
}  // namespace ReadingHomeMetrics

class ReadingHomeTheme : public MinimalTheme {
 public:
  static constexpr int kRecentCoverCount = 3;
  static constexpr int kRecentBookCount = kRecentCoverCount + 1;
  static constexpr int kCoverWidth = 125;
  static constexpr int kCoverHeight = 188;

  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle = nullptr,
                  bool readerContext = false) const override;
  void drawReadingHome(GfxRenderer& renderer, const std::vector<RecentBook>& recentBooks, int selectorIndex,
                       const BookReadingStats* currentStats, float currentProgressPercent,
                       uint32_t currentBookWordCount, const char* currentChapterTitle,
                       const std::array<float, kRecentBookCount>& progressByBook, const GlobalReadingStats& deviceStats,
                       const GlobalReadingStats& allDevicesStats, uint32_t todayReadingSeconds,
                       uint16_t currentStreak) const;
  void drawReadingHomeSelection(GfxRenderer& renderer, int selectorIndex, int recentCount) const;
};
