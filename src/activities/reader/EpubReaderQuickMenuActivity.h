#pragma once

#include <I18n.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "EpubReaderMenuActivity.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct Rect;

class EpubReaderQuickMenuActivity final : public Activity {
 public:
  explicit EpubReaderQuickMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       const std::string& title, int currentPage, int totalPages,
                                       int bookProgressPercent, bool autoPageTurnActive,
                                       uint16_t autoPageTurnIntervalSeconds);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool allowPowerAsConfirmInReaderMode() const override { return true; }

 private:
  enum class ItemKind : uint8_t { Action, Tilt, LineSpacing };

  struct Item {
    ItemKind kind;
    StrId labelId;
    EpubReaderMenuActivity::MenuAction action;
  };

  static constexpr size_t ITEM_COUNT = 10;

  void layoutPanel();
  void capturePanelBackground();
  void restorePanelBackground();
  void activateSelected(int direction = 1);
  void finishWithAction(EpubReaderMenuActivity::MenuAction action);
  void finishCancelled();
  std::string itemValue(int index) const;
  static const char* lineSpacingLabel();
  static void cycleLineSpacing(int direction);

  // Filled in the constructor; the tilt entry is omitted on devices without
  // an accelerometer, so the live length is itemCount, not ITEM_COUNT.
  std::array<Item, ITEM_COUNT> items{};
  size_t itemCount = 0;
  ButtonNavigator buttonNavigator;
  std::string title;
  int currentPage = 0;
  int totalPages = 0;
  int bookProgressPercent = 0;
  bool autoPageTurnActive = false;
  uint16_t autoPageTurnIntervalSeconds = 0;
  int selectedIndex = 0;
  bool settingsChanged = false;
  bool syncSubmenuOpen = false;
  int syncSubmenuIndex = 0;

  int panelX = 0;
  int panelY = 0;
  int panelWidth = 0;
  int panelHeight = 0;
  std::unique_ptr<uint8_t[]> panelBackground;
  size_t panelBackgroundSize = 0;
  bool panelBackgroundCaptured = false;
};
