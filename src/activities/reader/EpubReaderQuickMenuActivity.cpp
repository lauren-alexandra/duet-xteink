#include "EpubReaderQuickMenuActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <new>

#include <HalTiltSensor.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

EpubReaderQuickMenuActivity::EpubReaderQuickMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                         const std::string& title, const int currentPage,
                                                         const int totalPages, const int bookProgressPercent,
                                                         const bool autoPageTurnActive,
                                                         const uint16_t autoPageTurnIntervalSeconds)
    : Activity("EpubReaderQuickMenu", renderer, mappedInput),
      title(title),
      currentPage(currentPage),
      totalPages(totalPages),
      bookProgressPercent(bookProgressPercent),
      autoPageTurnActive(autoPageTurnActive),
      autoPageTurnIntervalSeconds(autoPageTurnIntervalSeconds) {
  const auto add = [this](const ItemKind kind, const StrId labelId, const EpubReaderMenuActivity::MenuAction action) {
    if (itemCount < items.size()) items[itemCount++] = Item{kind, labelId, action};
  };
  add(ItemKind::Action, StrId::STR_SELECT_CHAPTER, EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER);
  add(ItemKind::Action, StrId::STR_DICTIONARY, EpubReaderMenuActivity::MenuAction::LOOK_UP_WORD);
  add(ItemKind::Action, StrId::STR_GO_TO_PERCENT, EpubReaderMenuActivity::MenuAction::GO_TO_PERCENT);
  add(ItemKind::Action, StrId::STR_SYNC_PROGRESS, EpubReaderMenuActivity::MenuAction::SYNC);
  add(ItemKind::Action, StrId::STR_READING_STATS, EpubReaderMenuActivity::MenuAction::READING_STATS);
  // The X4 has no accelerometer; offering the toggle there is a no-op trap.
  if (halTiltSensor.isAvailable()) {
    add(ItemKind::Tilt, StrId::STR_TILT_PAGE_TURN, EpubReaderMenuActivity::MenuAction::OPEN_FULL_MENU);
  }
  add(ItemKind::Action, StrId::STR_AUTO_PAGE_TURN, EpubReaderMenuActivity::MenuAction::AUTO_PAGE_TURN);
  add(ItemKind::LineSpacing, StrId::STR_SPACING, EpubReaderMenuActivity::MenuAction::OPEN_FULL_MENU);
  add(ItemKind::Action, StrId::STR_READER_OPTIONS, EpubReaderMenuActivity::MenuAction::READER_OPTIONS);
  add(ItemKind::Action, StrId::STR_MORE, EpubReaderMenuActivity::MenuAction::OPEN_FULL_MENU);
}

void EpubReaderQuickMenuActivity::layoutPanel() {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const bool portrait = screenHeight >= screenWidth;

  panelX = 10;
  panelWidth = portrait ? std::max(240, screenWidth * 70 / 100) : std::max(240, screenWidth * 52 / 100);
  panelHeight = std::min(screenHeight - 36, 78 + 40 + static_cast<int>(itemCount) * 48 + 12);
  panelY = std::max(18, (screenHeight - panelHeight) / 2);

  if (panelX + panelWidth > screenWidth - 10) panelWidth = screenWidth - panelX - 10;
  if (panelY + panelHeight > screenHeight) panelHeight = screenHeight - panelY;
}

void EpubReaderQuickMenuActivity::capturePanelBackground() {
  panelBackgroundSize = renderer.getRegionByteSize(panelX, panelY, panelWidth, panelHeight);
  if (panelBackgroundSize == 0) return;

  panelBackground.reset(new (std::nothrow) uint8_t[panelBackgroundSize]);
  if (!panelBackground) {
    panelBackgroundSize = 0;
    return;
  }

  panelBackgroundCaptured = renderer.copyRegionToBuffer(panelX, panelY, panelWidth, panelHeight,
                                                        panelBackground.get(), panelBackgroundSize);
  if (!panelBackgroundCaptured) {
    panelBackground.reset();
    panelBackgroundSize = 0;
  }
}

void EpubReaderQuickMenuActivity::restorePanelBackground() {
  if (!panelBackgroundCaptured || !panelBackground) return;
  renderer.copyBufferToRegion(panelX, panelY, panelWidth, panelHeight, panelBackground.get(), panelBackgroundSize);
}

void EpubReaderQuickMenuActivity::onEnter() {
  Activity::onEnter();
  layoutPanel();
  capturePanelBackground();
  requestUpdate();
}

void EpubReaderQuickMenuActivity::onExit() {
  restorePanelBackground();
  SETTINGS.saveToFile();
  Activity::onExit();
}

const char* EpubReaderQuickMenuActivity::lineSpacingLabel() {
  if (SETTINGS.lineHeightPercent <= 95) return tr(STR_TIGHT);
  if (SETTINGS.lineHeightPercent >= 110) return tr(STR_WIDE);
  return tr(STR_NORMAL);
}

void EpubReaderQuickMenuActivity::cycleLineSpacing(const int direction) {
  constexpr std::array<uint8_t, 3> values = {90, 100, 120};
  int index = SETTINGS.lineHeightPercent <= 95 ? 0 : (SETTINGS.lineHeightPercent >= 110 ? 2 : 1);
  index = (index + (direction < 0 ? -1 : 1) + static_cast<int>(values.size())) %
          static_cast<int>(values.size());
  SETTINGS.lineHeightPercent = values[index];
}

std::string EpubReaderQuickMenuActivity::itemValue(const int index) const {
  if (index < 0 || index >= static_cast<int>(itemCount)) return {};
  switch (items[index].kind) {
    case ItemKind::Tilt:
      return SETTINGS.tiltPageTurn == CrossPointSettings::TILT_OFF ? tr(STR_STATE_OFF) : tr(STR_STATE_ON);
    case ItemKind::LineSpacing: {
      char value[32];
      snprintf(value, sizeof(value), "%s %u%%", lineSpacingLabel(), SETTINGS.lineHeightPercent);
      return value;
    }
    case ItemKind::Action:
      if (items[index].action == EpubReaderMenuActivity::MenuAction::AUTO_PAGE_TURN) {
        return autoPageTurnActive ? std::to_string(autoPageTurnIntervalSeconds) + "s" : tr(STR_STATE_OFF);
      }
      return {};
  }
  return {};
}

void EpubReaderQuickMenuActivity::finishWithAction(const EpubReaderMenuActivity::MenuAction action) {
  setResult(MenuResult{static_cast<int>(action), SETTINGS.orientation, settingsChanged});
  finish();
}

void EpubReaderQuickMenuActivity::finishCancelled() {
  ActivityResult result;
  result.isCancelled = true;
  result.data = MenuResult{-1, SETTINGS.orientation, settingsChanged};
  setResult(std::move(result));
  finish();
}

void EpubReaderQuickMenuActivity::activateSelected(const int direction) {
  const Item& item = items[selectedIndex];
  switch (item.kind) {
    case ItemKind::Tilt:
      SETTINGS.tiltPageTurn = SETTINGS.tiltPageTurn == CrossPointSettings::TILT_OFF ? CrossPointSettings::TILT_ON
                                                                                    : CrossPointSettings::TILT_OFF;
      requestUpdate();
      return;
    case ItemKind::LineSpacing:
      cycleLineSpacing(direction);
      settingsChanged = true;
      requestUpdate();
      return;
    case ItemKind::Action:
      if (item.action == EpubReaderMenuActivity::MenuAction::SYNC) {
        // Two sync transports live behind one entry: KOReader (server, WiFi)
        // and Nearby Position Sync (device-to-device, ESP-NOW).
        syncSubmenuOpen = true;
        syncSubmenuIndex = 0;
        requestUpdate();
        return;
      }
      finishWithAction(item.action);
      return;
  }
}

void EpubReaderQuickMenuActivity::loop() {
  if (syncSubmenuOpen) {
    buttonNavigator.onNextRelease([this] {
      syncSubmenuIndex = ButtonNavigator::nextIndex(syncSubmenuIndex, 2);
      requestUpdate();
    });
    buttonNavigator.onPreviousRelease([this] {
      syncSubmenuIndex = ButtonNavigator::previousIndex(syncSubmenuIndex, 2);
      requestUpdate();
    });
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      finishWithAction(syncSubmenuIndex == 0 ? EpubReaderMenuActivity::MenuAction::SYNC
                                             : EpubReaderMenuActivity::MenuAction::NEARBY_POSITION_SYNC);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      syncSubmenuOpen = false;
      requestUpdate();
    }
    return;
  }

  buttonNavigator.onNextRelease([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(itemCount));
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(itemCount));
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelected();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finishCancelled();
  }
}

void EpubReaderQuickMenuActivity::render(RenderLock&&) {
  restorePanelBackground();

  renderer.fillRoundedRect(panelX, panelY, panelWidth, panelHeight, 6, Color::White);
  renderer.drawRoundedRect(panelX, panelY, panelWidth, panelHeight, 2, 6, true);

  constexpr int inset = 12;
  constexpr int headerHeight = 78;
  constexpr int footerHeight = 40;
  const int textWidth = panelWidth - inset * 2;
  const auto visibleTitle = renderer.truncatedText(UI_12_FONT_ID, title.c_str(), textWidth, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, panelX + inset, panelY + 10, visibleTitle.c_str(), true, EpdFontFamily::BOLD);

  char progress[64];
  if (totalPages > 0) {
    snprintf(progress, sizeof(progress), "%d/%d pages  |  %d%% book", currentPage, totalPages, bookProgressPercent);
  } else {
    snprintf(progress, sizeof(progress), "%d%% book", bookProgressPercent);
  }
  const auto visibleProgress = renderer.truncatedText(SMALL_FONT_ID, progress, textWidth);
  renderer.drawText(SMALL_FONT_ID, panelX + inset, panelY + 42, visibleProgress.c_str());
  renderer.drawLine(panelX + 1, panelY + headerHeight, panelX + panelWidth - 2, panelY + headerHeight, true);

  const Rect listRect{panelX + 4, panelY + headerHeight + 4, panelWidth - 8,
                      panelHeight - headerHeight - footerHeight - 8};
  if (syncSubmenuOpen) {
    static constexpr std::array<StrId, 2> syncChoices = {StrId::STR_KOREADER_SYNC, StrId::STR_NEARBY_POSITION_SYNC};
    GUI.drawList(
        renderer, listRect, syncChoices.size(), syncSubmenuIndex,
        [](const int index) { return I18N.get(syncChoices[index]); }, nullptr, nullptr, nullptr, true);
  } else {
    GUI.drawList(
        renderer, listRect, itemCount, selectedIndex,
        [this](const int index) { return I18N.get(items[index].labelId); }, nullptr, nullptr,
        [this](const int index) { return itemValue(index); }, true);
  }

  const int footerY = panelY + panelHeight - footerHeight;
  renderer.drawLine(panelX + 1, footerY, panelX + panelWidth - 2, footerY, true);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  const std::array<const char*, 4> footerLabels = {labels.btn1, labels.btn2, labels.btn3, labels.btn4};
  const int slotWidth = panelWidth / static_cast<int>(footerLabels.size());
  for (size_t i = 0; i < footerLabels.size(); ++i) {
    const int slotX = panelX + static_cast<int>(i) * slotWidth;
    const int actualWidth = i + 1 == footerLabels.size() ? panelX + panelWidth - slotX : slotWidth;
    if (i > 0) renderer.drawLine(slotX, footerY, slotX, panelY + panelHeight - 1, true);
    const auto label = renderer.truncatedText(SMALL_FONT_ID, footerLabels[i], actualWidth - 6);
    const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, label.c_str());
    renderer.drawText(SMALL_FONT_ID, slotX + std::max(3, (actualWidth - labelWidth) / 2), footerY + 10,
                      label.c_str());
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
