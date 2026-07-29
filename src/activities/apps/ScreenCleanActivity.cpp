#include "ScreenCleanActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "components/CompactHeader.h"
#include "components/UITheme.h"

namespace {
constexpr int ACTION_COUNT = 2;
constexpr uint32_t STAGE_HOLD_MS = 450;
constexpr int CHECKER_TILE_SIZE = 32;

const char* titleForAction(const int index) {
  return index == 0 ? tr(STR_SCREEN_CLEAN_QUICK) : tr(STR_SCREEN_CLEAN_DEEP);
}

const char* subtitleForAction(const int index) {
  return index == 0 ? tr(STR_SCREEN_CLEAN_QUICK_DESC) : tr(STR_SCREEN_CLEAN_DEEP_DESC);
}
}  // namespace

void ScreenCleanActivity::onEnter() {
  Activity::onEnter();
  cleaning = false;
  completed = false;
  selectedIndex = 0;
  requestUpdate();
}

void ScreenCleanActivity::startCleaning(const Mode cleanMode) {
  completed = false;
  mode = cleanMode;
  stageIndex = 0;
  cleaning = true;
  requestUpdateAndWait();
}

void ScreenCleanActivity::finishCleaning(const bool markCompleted) {
  cleaning = false;
  completed = markCompleted;
  requestUpdateAndWait();
}

int ScreenCleanActivity::stageCount() const { return mode == Mode::Quick ? 5 : 11; }

ScreenCleanActivity::Pattern ScreenCleanActivity::patternForStage(const uint8_t index) const {
  static constexpr Pattern QUICK_SEQUENCE[] = {
      Pattern::White, Pattern::Black, Pattern::White, Pattern::Black, Pattern::White,
  };
  static constexpr Pattern DEEP_SEQUENCE[] = {
      Pattern::White, Pattern::Black, Pattern::White, Pattern::Checker, Pattern::InverseChecker, Pattern::LightGray,
      Pattern::DarkGray, Pattern::Black, Pattern::White, Pattern::Black, Pattern::White,
  };
  return mode == Mode::Quick ? QUICK_SEQUENCE[index % 5] : DEEP_SEQUENCE[index % 11];
}

void ScreenCleanActivity::drawPattern(const Pattern pattern) const {
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  switch (pattern) {
    case Pattern::White:
      renderer.clearScreen(0xFF);
      return;
    case Pattern::Black:
      renderer.clearScreen(0x00);
      return;
    case Pattern::LightGray:
      renderer.clearScreen(0xFF);
      renderer.fillRectDither(0, 0, width, height, Color::LightGray);
      return;
    case Pattern::DarkGray:
      renderer.clearScreen(0xFF);
      renderer.fillRectDither(0, 0, width, height, Color::DarkGray);
      return;
    case Pattern::Checker:
    case Pattern::InverseChecker:
      break;
  }

  renderer.clearScreen(0xFF);
  const bool inverse = pattern == Pattern::InverseChecker;
  for (int y = 0; y < height; y += CHECKER_TILE_SIZE) {
    for (int x = 0; x < width; x += CHECKER_TILE_SIZE) {
      const bool fillBlack = (((x / CHECKER_TILE_SIZE) + (y / CHECKER_TILE_SIZE)) & 1) != (inverse ? 0 : 1);
      if (!fillBlack) continue;
      renderer.fillRect(x, y, std::min(CHECKER_TILE_SIZE, width - x), std::min(CHECKER_TILE_SIZE, height - y));
    }
  }
}

void ScreenCleanActivity::loop() {
  if (cleaning) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finishCleaning(false);
      return;
    }
    if (millis() - lastStageRenderedAt < STAGE_HOLD_MS) return;
    stageIndex++;
    if (stageIndex >= stageCount()) {
      finishCleaning(true);
      return;
    }
    requestUpdateAndWait();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    startCleaning(selectedIndex == 0 ? Mode::Quick : Mode::Deep);
    return;
  }
  buttonNavigator.onNextRelease([this] {
    completed = false;
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, ACTION_COUNT);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    completed = false;
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, ACTION_COUNT);
    requestUpdate();
  });
}

void ScreenCleanActivity::render(RenderLock&&) {
  if (cleaning) {
    drawPattern(patternForStage(stageIndex));
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    lastStageRenderedAt = millis();
    return;
  }

  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = CompactHeader::contentTop(metrics) + metrics.verticalSpacing;
  const int contentHeight = renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  CompactHeader::drawTitle(renderer, tr(STR_SCREEN_CLEAN), true);
  GUI.drawList(renderer, Rect{0, contentTop, renderer.getScreenWidth(), contentHeight}, ACTION_COUNT, selectedIndex,
               [](const int index) { return std::string(titleForAction(index)); },
               [](const int index) { return std::string(subtitleForAction(index)); });
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (completed) GUI.drawPopup(renderer, tr(STR_SCREEN_CLEAN_DONE));
  renderer.displayBuffer(completed ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
}
