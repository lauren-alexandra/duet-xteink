#include "AlertActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointState.h"
#include "activities/apps/AchievementsActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

void AlertActivity::onEnter() {
  Activity::onEnter();
  title = APP_STATE.pendingAlertTitle;
  body = APP_STATE.pendingAlertBody;
  goHomeOnBack = APP_STATE.pendingAlertGoHomeOnBack.exchange(false, std::memory_order_relaxed);
  action = static_cast<PendingAlertAction>(
      APP_STATE.pendingAlertAction.exchange(static_cast<uint8_t>(PendingAlertAction::None),
                                            std::memory_order_relaxed));
  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentWidth = renderer.getScreenWidth() - 2 * metrics.contentSidePadding;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;
  visibleLineCount = std::max(1, (contentBottom - contentTop) / std::max(1, lineHeight));
  bodyLines = renderer.wrappedText(UI_10_FONT_ID, body.c_str(), contentWidth, 96);
  if (bodyLines.empty()) bodyLines.push_back("");
  // Queue the first frame without blocking the input/activity loop on a full
  // e-ink refresh. Achievement alerts often arrive directly after another
  // action and should not make that action appear frozen.
  requestUpdate();
}

void AlertActivity::loop() {
  if (waitForConfirmRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) waitForConfirmRelease = false;
    return;
  }

  const int maxFirstLine = std::max(0, static_cast<int>(bodyLines.size()) - visibleLineCount);
  buttonNavigator.onNextRelease([this, maxFirstLine] {
    firstVisibleLine = std::min(maxFirstLine, firstVisibleLine + 1);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    firstVisibleLine = std::max(0, firstVisibleLine - 1);
    requestUpdate();
  });

  if (action == PendingAlertAction::Achievements &&
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // Release the popup's wrapped copy before the achievements catalogue is
    // opened. Keeping both allocations alive caused a large transient heap
    // spike on X3 and could strand the reader on the alert.
    body.clear();
    body.shrink_to_fit();
    title.clear();
    title.shrink_to_fit();
    bodyLines.clear();
    bodyLines.shrink_to_fit();
    firstVisibleLine = 0;
    visibleLineCount = 0;
    startActivityForResult(std::make_unique<AchievementsActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) { finish(); });
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (goHomeOnBack) {
      onGoHome();
    } else {
      finish();
    }
  }
}

void AlertActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto contentWidth = pageWidth - 2 * metrics.contentSidePadding;
  const auto x = metrics.contentSidePadding;
  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title.c_str());

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  const int lineEnd = std::min(static_cast<int>(bodyLines.size()), firstVisibleLine + visibleLineCount);
  for (int i = firstVisibleLine; i < lineEnd; ++i) {
    renderer.drawText(UI_10_FONT_ID, x, y, bodyLines[i].c_str());
    y += lineHeight;
  }

  const bool canScroll = static_cast<int>(bodyLines.size()) > visibleLineCount;
  const auto labels = mappedInput.mapLabels(
      goHomeOnBack ? tr(STR_HOME) : tr(STR_BACK),
      action == PendingAlertAction::Achievements ? tr(STR_SEE_ALL) : "",
      canScroll ? tr(STR_DIR_UP) : "", canScroll ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
