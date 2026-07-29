#include "LauncherCustomizeActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <string>

#include "LauncherCatalog.h"
#include "LauncherLayoutStore.h"
#include "MappedInputManager.h"
#include "components/CompactHeader.h"
#include "components/UITheme.h"

namespace {
constexpr int rowCount() { return static_cast<int>(LauncherItem::Count) + 1; }

const char* placementLabel(const LauncherItem item) {
  if (LAUNCHER_LAYOUT.isRequired(item)) return tr(STR_LAUNCHER_REQUIRED);
  switch (LAUNCHER_LAYOUT.placement(item)) {
    case LAUNCHER_HOME:
      return tr(STR_LAUNCHER_HOME_ONLY);
    case LAUNCHER_APPS:
      return tr(STR_LAUNCHER_APPS_ONLY);
    case LAUNCHER_BOTH:
      return tr(STR_LAUNCHER_BOTH);
    case LAUNCHER_HIDDEN:
    default:
      return tr(STR_LAUNCHER_HIDDEN);
  }
}
}  // namespace

void LauncherCustomizeActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void LauncherCustomizeActivity::cyclePlacement() {
  if (selectedIndex >= static_cast<int>(LauncherItem::Count)) {
    LAUNCHER_LAYOUT.resetDefaults();
    requestUpdate();
    return;
  }

  const auto item = static_cast<LauncherItem>(selectedIndex);
  if (LAUNCHER_LAYOUT.isRequired(item)) return;

  uint8_t next = LAUNCHER_HOME;
  switch (LAUNCHER_LAYOUT.placement(item)) {
    case LAUNCHER_HOME:
      next = LAUNCHER_APPS;
      break;
    case LAUNCHER_APPS:
      next = LAUNCHER_BOTH;
      break;
    case LAUNCHER_BOTH:
      next = LAUNCHER_HIDDEN;
      break;
    case LAUNCHER_HIDDEN:
    default:
      next = LAUNCHER_HOME;
      break;
  }
  LAUNCHER_LAYOUT.setPlacement(item, next);
  requestUpdate();
}

void LauncherCustomizeActivity::moveSelected(const int delta) {
  if (selectedIndex >= static_cast<int>(LauncherItem::Count)) return;
  const auto item = static_cast<LauncherItem>(selectedIndex);
  const uint8_t itemPlacement = LAUNCHER_LAYOUT.placement(item);
  bool changed = false;
  if ((itemPlacement & LAUNCHER_HOME) != 0) {
    changed = LAUNCHER_LAYOUT.move(item, LauncherSurface::Home, delta) || changed;
  }
  if ((itemPlacement & LAUNCHER_APPS) != 0) {
    changed = LAUNCHER_LAYOUT.move(item, LauncherSurface::Apps, delta) || changed;
  }
  if (changed) requestUpdate();
}

void LauncherCustomizeActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    cyclePlacement();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    moveSelected(-1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    moveSelected(1);
    return;
  }
  buttonNavigator.onNextRelease([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, rowCount());
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, rowCount());
    requestUpdate();
  });
}

void LauncherCustomizeActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = CompactHeader::contentTop(metrics) + metrics.verticalSpacing;
  const int contentHeight = renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  CompactHeader::drawTitle(renderer, tr(STR_CUSTOMIZE_HOME_APPS), true);
  GUI.drawList(
      renderer, Rect{0, contentTop, renderer.getScreenWidth(), contentHeight}, rowCount(), selectedIndex,
      [](const int index) {
        if (index >= static_cast<int>(LauncherItem::Count)) return std::string(tr(STR_RESET_DEFAULTS));
        return std::string(I18N.get(launcherItemLabel(static_cast<LauncherItem>(index))));
      },
      nullptr, nullptr,
      [](const int index) {
        if (index >= static_cast<int>(LauncherItem::Count)) return std::string{};
        return std::string(placementLabel(static_cast<LauncherItem>(index)));
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SHOW), tr(STR_MOVE_UP), tr(STR_MOVE_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
