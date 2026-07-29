#include "StatsDateActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "activities/reader/ReadingStatsClock.h"
#include "components/UITheme.h"
#include "fontIds.h"

void StatsDateActivity::onEnter() {
  Activity::onEnter();
  ReadingStatsDateTime current;
  date = getCurrentLocalReadingStatsDateTime(current) ? current.date : ReadingStatsDate{2024, 1, 1};
  selectedField = 0;
  saveFailed = false;
  requestUpdate();
}

void StatsDateActivity::adjustSelectedField(const int delta) {
  if (delta == 0) return;
  if (selectedField == 0) {
    int month = static_cast<int>(date.month) + delta;
    while (month < 1) month += 12;
    while (month > 12) month -= 12;
    date.month = static_cast<uint8_t>(month);
  } else if (selectedField == 1) {
    const int maxDay = daysInMonth(date.year, date.month);
    int day = static_cast<int>(date.day) + delta;
    while (day < 1) day += maxDay;
    while (day > maxDay) day -= maxDay;
    date.day = static_cast<uint8_t>(day);
  } else {
    date.year = static_cast<uint16_t>(std::clamp(static_cast<int>(date.year) + delta, 2000, 2099));
  }
  date.day = std::min<uint8_t>(date.day, daysInMonth(date.year, date.month));
  saveFailed = false;
  requestUpdate();
}

void StatsDateActivity::advanceOrSave() {
  if (selectedField < 2) {
    selectedField++;
    requestUpdate();
    return;
  }
  if (setClocklessReadingStatsDate(date)) {
    finish();
  } else {
    saveFailed = true;
    requestUpdate();
  }
}

void StatsDateActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    mappedInput.suppressNextBackRelease();
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    advanceOrSave();
    return;
  }
  navigator.onPrevious([this] { adjustSelectedField(-1); });
  navigator.onNext([this] { adjustSelectedField(1); });
}

void StatsDateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_STATS_DATE));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listHeight = metrics.listWithSubtitleRowHeight * 3;
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, listHeight}, 3, selectedField,
      [](const int index) {
        if (index == 0) return std::string(tr(STR_MONTH));
        if (index == 1) return std::string(tr(STR_DAY));
        return std::string(tr(STR_YEAR));
      },
      [this](const int index) {
        char value[8];
        if (index == 0) snprintf(value, sizeof(value), "%02u", static_cast<unsigned>(date.month));
        if (index == 1) snprintf(value, sizeof(value), "%02u", static_cast<unsigned>(date.day));
        if (index == 2) snprintf(value, sizeof(value), "%04u", static_cast<unsigned>(date.year));
        return std::string(value);
      },
      nullptr, nullptr, false);

  const int helpY = contentTop + listHeight + metrics.verticalSpacing;
  const char* help = saveFailed ? tr(STR_STATS_DATE_SAVE_FAILED) : tr(STR_STATS_DATE_HELP);
  const std::string visibleHelp = renderer.truncatedText(SMALL_FONT_ID, help, pageWidth - 32);
  renderer.drawCenteredText(SMALL_FONT_ID, helpY, visibleHelp.c_str());

  const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), selectedField == 2 ? tr(STR_SAVE) : tr(STR_CONFIRM),
                                             tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

