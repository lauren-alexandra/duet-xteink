#include "ReadMeActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <array>
#include <cstdio>

#include "MappedInputManager.h"
#include "components/CompactHeader.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr std::array<StrId, ReadMeActivity::PAGE_COUNT> PAGE_TITLES = {
    StrId::STR_READ_ME_WELCOME, StrId::STR_READ_ME_LIBRARY, StrId::STR_READ_ME_READER,
    StrId::STR_READ_ME_STATS, StrId::STR_READ_ME_POWER,
};
constexpr std::array<StrId, ReadMeActivity::PAGE_COUNT> PAGE_BODIES = {
    StrId::STR_READ_ME_WELCOME_BODY, StrId::STR_READ_ME_LIBRARY_BODY, StrId::STR_READ_ME_READER_BODY,
    StrId::STR_READ_ME_STATS_BODY, StrId::STR_READ_ME_POWER_BODY,
};
}  // namespace

void ReadMeActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void ReadMeActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    page = ButtonNavigator::nextIndex(page, PAGE_COUNT);
    requestUpdate();
    return;
  }
  buttonNavigator.onNextRelease([this] {
    page = ButtonNavigator::nextIndex(page, PAGE_COUNT);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    page = ButtonNavigator::previousIndex(page, PAGE_COUNT);
    requestUpdate();
  });
}

void ReadMeActivity::render(RenderLock&&) {
  renderer.clearScreen();
  CompactHeader::drawTitle(renderer, tr(STR_READ_ME), true);

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentX = metrics.contentSidePadding;
  const int contentWidth = renderer.getScreenWidth() - contentX * 2;
  int y = CompactHeader::contentTop(metrics) + metrics.verticalSpacing * 2;

  renderer.drawText(UI_12_FONT_ID, contentX, y, I18N.get(PAGE_TITLES[page]), true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 16;

  const auto lines = renderer.wrappedText(UI_10_FONT_ID, I18N.get(PAGE_BODIES[page]), contentWidth, 18);
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID) + 5;
  for (const auto& line : lines) {
    renderer.drawText(UI_10_FONT_ID, contentX, y, line.c_str());
    y += lineHeight;
  }

  char indicator[20];
  snprintf(indicator, sizeof(indicator), "%d / %d", page + 1, PAGE_COUNT);
  const int indicatorWidth = renderer.getTextWidth(SMALL_FONT_ID, indicator);
  renderer.drawText(SMALL_FONT_ID, renderer.getScreenWidth() - contentX - indicatorWidth,
                    renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing * 2, indicator);

  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), tr(STR_NEXT_TAB), tr(STR_PREVIOUS_TAB), tr(STR_NEXT_TAB));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
