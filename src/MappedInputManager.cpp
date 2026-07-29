#include "MappedInputManager.h"

#include <algorithm>
#include <utility>

#include "CrossPointSettings.h"
#include "GlobalActions.h"

namespace {
using ButtonIndex = uint8_t;
constexpr ButtonIndex kNoButton = UINT8_MAX;

struct SideLayoutMap {
  ButtonIndex pageBackPrimary;
  ButtonIndex pageBackSecondary;
  ButtonIndex pageForwardPrimary;
  ButtonIndex pageForwardSecondary;
};

// Order matches CrossPointSettings::SIDE_BUTTON_LAYOUT.
constexpr SideLayoutMap kSideLayouts[] = {
    {HalGPIO::BTN_UP, kNoButton, HalGPIO::BTN_DOWN, kNoButton},
    {HalGPIO::BTN_DOWN, kNoButton, HalGPIO::BTN_UP, kNoButton},
    {kNoButton, kNoButton, kNoButton, kNoButton},
    {kNoButton, kNoButton, HalGPIO::BTN_UP, HalGPIO::BTN_DOWN},
};

bool shouldSwapReaderSideButtons(const bool readerMode) {
  return readerMode && SETTINGS.sideButtonOrientationAware && SETTINGS.orientation != CrossPointSettings::PORTRAIT;
}

bool shouldSwapReaderFrontNavButtons(const CrossPointSettings::FRONT_BUTTON_ORIENTATION_AWARE orientationMode) {
  if (orientationMode == CrossPointSettings::FRONT_ORIENTATION_AWARE_OFF) {
    return false;
  }
  return SETTINGS.orientation == CrossPointSettings::LANDSCAPE_CW ||
         SETTINGS.orientation == CrossPointSettings::LANDSCAPE_CCW ||
         (orientationMode == CrossPointSettings::FRONT_ORIENTATION_AWARE_NAV_BUTTONS &&
          SETTINGS.orientation == CrossPointSettings::INVERTED);
}

ButtonIndex invertFrontButtonPosition(const ButtonIndex button) {
  switch (button) {
    case HalGPIO::BTN_BACK:
      return HalGPIO::BTN_RIGHT;
    case HalGPIO::BTN_CONFIRM:
      return HalGPIO::BTN_LEFT;
    case HalGPIO::BTN_LEFT:
      return HalGPIO::BTN_CONFIRM;
    case HalGPIO::BTN_RIGHT:
      return HalGPIO::BTN_BACK;
    default:
      return button;
  }
}

ButtonIndex mapFrontButtonForReaderOrientation(const ButtonIndex button, const ButtonIndex leftButton,
                                               const ButtonIndex rightButton, const bool readerMode) {
  if (!readerMode) {
    return button;
  }

  const auto orientationMode =
      static_cast<CrossPointSettings::FRONT_BUTTON_ORIENTATION_AWARE>(SETTINGS.frontButtonOrientationAware);

  if (orientationMode == CrossPointSettings::FRONT_ORIENTATION_AWARE_ALL_BUTTONS &&
      SETTINGS.orientation == CrossPointSettings::INVERTED) {
    return invertFrontButtonPosition(button);
  }

  if (shouldSwapReaderFrontNavButtons(orientationMode)) {
    if (button == leftButton) {
      return rightButton;
    }
    if (button == rightButton) {
      return leftButton;
    }
  }

  return button;
}

SideLayoutMap mapSideLayoutForReaderOrientation(SideLayoutMap side, const bool readerMode) {
  if (shouldSwapReaderSideButtons(readerMode)) {
    const bool hasPageBack = side.pageBackPrimary != kNoButton || side.pageBackSecondary != kNoButton;
    const bool hasPageForward = side.pageForwardPrimary != kNoButton || side.pageForwardSecondary != kNoButton;
    if (hasPageBack && hasPageForward) {
      std::swap(side.pageBackPrimary, side.pageForwardPrimary);
      std::swap(side.pageBackSecondary, side.pageForwardSecondary);
    }
  }
  return side;
}

ButtonIndex mapSideButtonForReaderOrientation(const ButtonIndex button, const bool readerMode) {
  if (!shouldSwapReaderSideButtons(readerMode)) {
    return button;
  }
  if (button == HalGPIO::BTN_UP) {
    return HalGPIO::BTN_DOWN;
  }
  if (button == HalGPIO::BTN_DOWN) {
    return HalGPIO::BTN_UP;
  }
  return button;
}

bool readMappedSideButtons(const HalGPIO& gpio, bool (HalGPIO::*fn)(uint8_t) const, const ButtonIndex primary,
                           const ButtonIndex secondary) {
  return (primary != kNoButton && (gpio.*fn)(primary)) || (secondary != kNoButton && (gpio.*fn)(secondary));
}

#ifdef SIMULATOR
size_t buttonIndex(MappedInputManager::Button button) { return static_cast<size_t>(button); }
#endif

}  // namespace

void MappedInputManager::update() const {
  gpio.update();

  const bool powerPressed = isPressed(Button::Power);
  const bool powerReleased =
#ifdef SIMULATOR
      simulatorReleased[buttonIndex(Button::Power)] ||
#endif
      gpio.wasReleased(HalGPIO::BTN_POWER);

  unsigned long powerHeldTime = gpio.getHeldTime();
#ifdef SIMULATOR
  const size_t powerIndex = buttonIndex(Button::Power);
  if ((simulatorHeld[powerIndex] || simulatorReleased[powerIndex]) && simulatorPressStart[powerIndex] > 0) {
    powerHeldTime = std::max(powerHeldTime, millis() - simulatorPressStart[powerIndex]);
  }
#endif

  if (powerPressed && powerHeldTime >= SETTINGS.getPowerButtonLongPressDuration()) {
    powerButtonClicks.cancel();
    return;
  }

  const bool releaseSuppressed = suppressPowerRelease || suppressPowerConfirmRelease;
  const bool shortRelease = powerReleased && powerHeldTime < SETTINGS.getPowerButtonLongPressDuration() &&
                            !releaseSuppressed;
  const bool multiClickEnabled = SETTINGS.doublePwrBtn != CrossPointSettings::SHORT_PWRBTN::IGNORE ||
                                 SETTINGS.triplePwrBtn != CrossPointSettings::SHORT_PWRBTN::IGNORE;
  powerButtonClicks.update(shortRelease, powerPressed, multiClickEnabled, millis());
}

void MappedInputManager::updatePreservingEvents() const {
  // Power-click resolution belongs to the normal main-loop update. The
  // preserved edge is replayed there exactly once.
#ifdef SIMULATOR
  // The external native simulator HAL does not model preserved edge queues.
  gpio.update();
#else
  gpio.updatePreservingEvents();
#endif
}

uint8_t MappedInputManager::getResolvedShortPowerAction() const {
  switch (powerButtonClicks.finalizedClicks()) {
    case 1:
      return SETTINGS.shortPwrBtn;
    case 2:
      return SETTINGS.doublePwrBtn;
    case 3:
      return SETTINGS.triplePwrBtn;
    default:
      return CrossPointSettings::SHORT_PWRBTN::IGNORE;
  }
}

bool MappedInputManager::mapButton(const Button button, bool (HalGPIO::*fn)(uint8_t) const) const {
  const auto sideLayout = static_cast<CrossPointSettings::SIDE_BUTTON_LAYOUT>(SETTINGS.sideButtonLayout);
  const auto side = mapSideLayoutForReaderOrientation(kSideLayouts[sideLayout], readerMode);

  const bool useReaderMapping = readerMode && SETTINGS.readerFrontButtonsEnabled;
  const ButtonIndex btnBack = useReaderMapping ? SETTINGS.readerFrontButtonBack : SETTINGS.frontButtonBack;
  const ButtonIndex btnConfirm = useReaderMapping ? SETTINGS.readerFrontButtonConfirm : SETTINGS.frontButtonConfirm;
  const ButtonIndex btnLeft = useReaderMapping ? SETTINGS.readerFrontButtonLeft : SETTINGS.frontButtonLeft;
  const ButtonIndex btnRight = useReaderMapping ? SETTINGS.readerFrontButtonRight : SETTINGS.frontButtonRight;
  const ButtonIndex mappedBack = mapFrontButtonForReaderOrientation(btnBack, btnLeft, btnRight, readerMode);
  const ButtonIndex mappedConfirm = mapFrontButtonForReaderOrientation(btnConfirm, btnLeft, btnRight, readerMode);
  const ButtonIndex mappedLeft = mapFrontButtonForReaderOrientation(btnLeft, btnLeft, btnRight, readerMode);
  const ButtonIndex mappedRight = mapFrontButtonForReaderOrientation(btnRight, btnLeft, btnRight, readerMode);

  switch (button) {
    case Button::Back:
      return (gpio.*fn)(mappedBack);
    case Button::Confirm:
      return (gpio.*fn)(mappedConfirm);
    case Button::Left:
      return (gpio.*fn)(mappedLeft);
    case Button::Right:
      return (gpio.*fn)(mappedRight);
    case Button::Up:
      // Reader menus should follow the same top/bottom side-button orientation as reader page turns.
      return (gpio.*fn)(mapSideButtonForReaderOrientation(HalGPIO::BTN_UP, readerMode));
    case Button::Down:
      // Reader menus should follow the same top/bottom side-button orientation as reader page turns.
      return (gpio.*fn)(mapSideButtonForReaderOrientation(HalGPIO::BTN_DOWN, readerMode));
    case Button::Power:
      // Power button bypasses remapping.
      return (gpio.*fn)(HalGPIO::BTN_POWER);
    case Button::PageBack:
      // Reader page navigation uses side buttons and can be swapped via settings.
      return readMappedSideButtons(gpio, fn, side.pageBackPrimary, side.pageBackSecondary);
    case Button::PageForward:
      // Reader page navigation uses side buttons and can be swapped via settings.
      return readMappedSideButtons(gpio, fn, side.pageForwardPrimary, side.pageForwardSecondary);
  }

  return false;
}

bool MappedInputManager::shouldUsePowerAsConfirmFallback() const { return !readerMode || powerAsConfirmInReaderMode; }

bool MappedInputManager::shouldMirrorPowerAsConfirmHold() const {
  return shouldUsePowerAsConfirmFallback() &&
         !isPowerButtonActionAvailableOutsideReader(static_cast<CrossPointSettings::SHORT_PWRBTN>(SETTINGS.longPwrBtn));
}

bool MappedInputManager::wasPressed(const Button button) const {
#ifdef SIMULATOR
  if (simulatorPressed[buttonIndex(button)]) {
    return true;
  }
#endif

  if (button == Button::Confirm) {
    if (mapButton(button, &HalGPIO::wasPressed)) {
      return true;
    }

    return shouldUsePowerAsConfirmFallback() &&
           !isPowerButtonActionAvailableOutsideReader(
               static_cast<CrossPointSettings::SHORT_PWRBTN>(SETTINGS.shortPwrBtn)) &&
           gpio.wasPressed(HalGPIO::BTN_POWER);
  }

  return mapButton(button, &HalGPIO::wasPressed);
}

bool MappedInputManager::wasReleased(const Button button) const {
#ifdef SIMULATOR
  if (simulatorReleased[buttonIndex(button)]) {
    return true;
  }
#endif

  if (button == Button::Back) {
    if (!mapButton(button, &HalGPIO::wasReleased)) {
      return false;
    }

    if (suppressBackRelease) {
      suppressBackRelease = false;
      return false;
    }

    return true;
  }

  if (button == Button::Confirm) {
    if (mapButton(button, &HalGPIO::wasReleased)) {
      if (suppressConfirmRelease) {
        suppressConfirmRelease = false;
        return false;
      }
      return true;
    }

    if (!shouldUsePowerAsConfirmFallback()) {
      return false;
    }

    if (wasShortPowerActionResolved()) {
      if (suppressConfirmRelease) {
        suppressConfirmRelease = false;
        return false;
      }
      if (suppressPowerConfirmRelease) {
        suppressPowerConfirmRelease = false;
        return false;
      }
      return !isPowerButtonActionAvailableOutsideReader(
          static_cast<CrossPointSettings::SHORT_PWRBTN>(getResolvedShortPowerAction()));
    }

    if (!gpio.wasReleased(HalGPIO::BTN_POWER)) {
      return false;
    }

    if (suppressConfirmRelease) {
      suppressConfirmRelease = false;
      suppressPowerConfirmRelease = false;
      return false;
    }

    if (suppressPowerConfirmRelease) {
      suppressPowerConfirmRelease = false;
      return false;
    }

    const bool longPress = gpio.getHeldTime() >= SETTINGS.getPowerButtonLongPressDuration();
    if (!longPress) {
      return false;
    }
    const auto action = static_cast<CrossPointSettings::SHORT_PWRBTN>(SETTINGS.longPwrBtn);
    return !isPowerButtonActionAvailableOutsideReader(action);
  }

  if (button == Button::Power) {
    if (!mapButton(button, &HalGPIO::wasReleased)) {
      return false;
    }

    if (suppressPowerRelease) {
      suppressPowerRelease = false;
      return false;
    }

    return true;
  }

  return mapButton(button, &HalGPIO::wasReleased);
}

bool MappedInputManager::isPressed(const Button button) const {
#ifdef SIMULATOR
  if (simulatorHeld[buttonIndex(button)]) {
    return true;
  }
#endif

  if (button == Button::Confirm) {
    if (mapButton(button, &HalGPIO::isPressed)) {
      return true;
    }

    if (!shouldMirrorPowerAsConfirmHold() || !gpio.isPressed(HalGPIO::BTN_POWER)) {
      return false;
    }

    return !isPowerButtonActionAvailableOutsideReader(
               static_cast<CrossPointSettings::SHORT_PWRBTN>(SETTINGS.shortPwrBtn)) ||
           gpio.getHeldTime() >= SETTINGS.getPowerButtonLongPressDuration();
  }

  return mapButton(button, &HalGPIO::isPressed);
}

bool MappedInputManager::wasAnyPressed() const {
#ifdef SIMULATOR
  if (std::any_of(simulatorPressed.begin(), simulatorPressed.end(), [](bool pressed) { return pressed; })) {
    return true;
  }
#endif
  return gpio.wasAnyPressed();
}

bool MappedInputManager::wasAnyReleased() const {
#ifdef SIMULATOR
  if (std::any_of(simulatorReleased.begin(), simulatorReleased.end(), [](bool released) { return released; })) {
    return true;
  }
#endif
  return gpio.wasAnyReleased();
}

unsigned long MappedInputManager::getHeldTime() const {
  unsigned long heldTime = gpio.getHeldTime();
#ifdef SIMULATOR
  const unsigned long now = millis();
  for (size_t i = 0; i < BUTTON_COUNT; i++) {
    if (simulatorHeld[i] && simulatorPressStart[i] > 0) {
      heldTime = std::max(heldTime, now - simulatorPressStart[i]);
    }
  }
#endif
  return heldTime;
}

MappedInputManager::Labels MappedInputManager::mapLabels(const char* back, const char* confirm, const char* previous,
                                                         const char* next) const {
  const bool useReaderMapping = readerMode && SETTINGS.readerFrontButtonsEnabled;
  const ButtonIndex btnBack = useReaderMapping ? SETTINGS.readerFrontButtonBack : SETTINGS.frontButtonBack;
  const ButtonIndex btnConfirm = useReaderMapping ? SETTINGS.readerFrontButtonConfirm : SETTINGS.frontButtonConfirm;
  const ButtonIndex btnLeft = useReaderMapping ? SETTINGS.readerFrontButtonLeft : SETTINGS.frontButtonLeft;
  const ButtonIndex btnRight = useReaderMapping ? SETTINGS.readerFrontButtonRight : SETTINGS.frontButtonRight;
  const ButtonIndex mappedBack = mapFrontButtonForReaderOrientation(btnBack, btnLeft, btnRight, readerMode);
  const ButtonIndex mappedConfirm = mapFrontButtonForReaderOrientation(btnConfirm, btnLeft, btnRight, readerMode);
  const ButtonIndex mappedLeft = mapFrontButtonForReaderOrientation(btnLeft, btnLeft, btnRight, readerMode);
  const ButtonIndex mappedRight = mapFrontButtonForReaderOrientation(btnRight, btnLeft, btnRight, readerMode);

  // Build the label order based on the configured hardware mapping.
  auto labelForHardware = [&](ButtonIndex hw) -> const char* {
    if (hw == mappedBack) return back;
    if (hw == mappedConfirm) return confirm;
    if (hw == mappedLeft) return previous;
    if (hw == mappedRight) return next;
    return "";
  };

  return {labelForHardware(HalGPIO::BTN_BACK), labelForHardware(HalGPIO::BTN_CONFIRM),
          labelForHardware(HalGPIO::BTN_LEFT), labelForHardware(HalGPIO::BTN_RIGHT)};
}

int MappedInputManager::getPressedFrontButton() const {
  // Scan the raw front buttons in hardware order.
  // This bypasses remapping so the remap activity can capture physical presses.
  if (gpio.wasPressed(HalGPIO::BTN_BACK)) {
    return HalGPIO::BTN_BACK;
  }
  if (gpio.wasPressed(HalGPIO::BTN_CONFIRM)) {
    return HalGPIO::BTN_CONFIRM;
  }
  if (gpio.wasPressed(HalGPIO::BTN_LEFT)) {
    return HalGPIO::BTN_LEFT;
  }
  if (gpio.wasPressed(HalGPIO::BTN_RIGHT)) {
    return HalGPIO::BTN_RIGHT;
  }
  return -1;
}

int MappedInputManager::getReleasedFrontButton() const {
  // Scan the raw front buttons in hardware order.
  // This bypasses remapping for screens whose labels are fixed to physical slots.
  if (gpio.wasReleased(HalGPIO::BTN_BACK)) {
    return HalGPIO::BTN_BACK;
  }
  if (gpio.wasReleased(HalGPIO::BTN_CONFIRM)) {
    return HalGPIO::BTN_CONFIRM;
  }
  if (gpio.wasReleased(HalGPIO::BTN_LEFT)) {
    return HalGPIO::BTN_LEFT;
  }
  if (gpio.wasReleased(HalGPIO::BTN_RIGHT)) {
    return HalGPIO::BTN_RIGHT;
  }
  return -1;
}

bool MappedInputManager::isFrontButtonPressed(const uint8_t buttonIndex) const { return gpio.isPressed(buttonIndex); }

#ifdef SIMULATOR
void MappedInputManager::simulatorInjectPress(Button button) {
  const size_t idx = buttonIndex(button);
  simulatorPressed[idx] = true;
  simulatorReleased[idx] = false;
  simulatorHeld[idx] = true;
  simulatorPressStart[idx] = millis();
}

void MappedInputManager::simulatorInjectRelease(Button button) {
  const size_t idx = buttonIndex(button);
  simulatorPressed[idx] = false;
  simulatorReleased[idx] = true;
  simulatorHeld[idx] = false;
}

void MappedInputManager::simulatorClearInputFrame() {
  simulatorPressed.fill(false);
  simulatorReleased.fill(false);
}
#endif
