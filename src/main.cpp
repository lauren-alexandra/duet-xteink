#include <Arduino.h>
#ifndef SIMULATOR
#include <BoardConfig.h>
#endif
#include <DuetStorageMigration.h>
#include <DuetStoragePaths.h>
#include <Epub.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <Logging.h>
#include <SPI.h>
#include <builtinFonts/all.h>

#ifdef SIMULATOR
using esp_reset_reason_t = int;
using esp_sleep_wakeup_cause_t = int;
enum : int {
  ESP_RST_UNKNOWN = 0,
  ESP_RST_POWERON,
  ESP_RST_EXT,
  ESP_RST_SW,
  ESP_RST_PANIC,
  ESP_RST_INT_WDT,
  ESP_RST_TASK_WDT,
  ESP_RST_WDT,
  ESP_RST_DEEPSLEEP,
  ESP_RST_BROWNOUT,
  ESP_RST_SDIO,
  ESP_RST_USB,
  ESP_RST_JTAG,
  ESP_RST_EFUSE,
  ESP_RST_PWR_GLITCH,
  ESP_RST_CPU_LOCKUP
};
enum : int {
  ESP_SLEEP_WAKEUP_UNDEFINED = 0,
  ESP_SLEEP_WAKEUP_ALL,
  ESP_SLEEP_WAKEUP_EXT0,
  ESP_SLEEP_WAKEUP_EXT1,
  ESP_SLEEP_WAKEUP_TIMER,
  ESP_SLEEP_WAKEUP_TOUCHPAD,
  ESP_SLEEP_WAKEUP_ULP,
  ESP_SLEEP_WAKEUP_GPIO,
  ESP_SLEEP_WAKEUP_UART,
  ESP_SLEEP_WAKEUP_WIFI,
  ESP_SLEEP_WAKEUP_COCPU,
  ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG,
  ESP_SLEEP_WAKEUP_BT
};
inline esp_reset_reason_t esp_reset_reason() { return ESP_RST_UNKNOWN; }
inline esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause() { return ESP_SLEEP_WAKEUP_UNDEFINED; }
#else
#include <esp_sleep.h>
#include <esp_system.h>
#endif

#include <algorithm>
#include <cstring>

#include "AchievementStore.h"
#include "AppVersion.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "FavoritesStore.h"
#include "GlobalActions.h"
#include "KOReaderCredentialStore.h"
#include "LauncherLayoutStore.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "activities/reader/EpubReaderUtils.h"
#include "activities/reader/KOReaderSyncActivity.h"
#include "activities/reader/ReadingStatsUtils.h"
#include "activities/reader/StatsBackup.h"
#include "activities/settings/KOReaderSettingsActivity.h"
#include "activities/settings/SdFirmwareUpdateActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/UsbSerialFileTransfer.h"
#ifdef SIMULATOR
#include "simulator/SimulatorSmokeTest.h"
#endif
#include "images/LoadingIcon.h"
#include "util/ButtonNavigator.h"
#include "util/ScreenshotUtil.h"

MappedInputManager mappedInputManager(gpio);
GfxRenderer renderer(display);
ActivityManager activityManager(renderer, mappedInputManager);
FontDecompressor fontDecompressor;
SdCardFontSystem sdFontSystem;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts());
static unsigned long allowSleepAt = 0;

// Fonts
#ifndef OMIT_MEDIUM_FONT
EpdFont lexenddeca14RegularFont(&lexenddeca_14_regular);
EpdFont lexenddeca14BoldFont(&lexenddeca_14_bold);
EpdFont lexenddeca14ItalicFont(&lexenddeca_14_italic);
EpdFont lexenddeca14BoldItalicFont(&lexenddeca_14_bolditalic);
EpdFontFamily lexenddeca14FontFamily(&lexenddeca14RegularFont, &lexenddeca14BoldFont, &lexenddeca14ItalicFont,
                                     &lexenddeca14BoldItalicFont);
#endif
#ifndef OMIT_TEENSY_FONT
EpdFont lexenddeca8RegularFont(&lexenddeca_8_regular);
EpdFont lexenddeca8BoldFont(&lexenddeca_8_bold);
EpdFont lexenddeca8ItalicFont(&lexenddeca_8_italic);
EpdFont lexenddeca8BoldItalicFont(&lexenddeca_8_bolditalic);
EpdFontFamily lexenddeca8FontFamily(&lexenddeca8RegularFont, &lexenddeca8BoldFont, &lexenddeca8ItalicFont,
                                    &lexenddeca8BoldItalicFont);
#endif
#ifndef OMIT_ITTY_BITTY_FONT
EpdFont lexenddeca9RegularFont(&lexenddeca_9_regular);
EpdFont lexenddeca9BoldFont(&lexenddeca_9_bold);
EpdFont lexenddeca9ItalicFont(&lexenddeca_9_italic);
EpdFont lexenddeca9BoldItalicFont(&lexenddeca_9_bolditalic);
EpdFontFamily lexenddeca9FontFamily(&lexenddeca9RegularFont, &lexenddeca9BoldFont, &lexenddeca9ItalicFont,
                                    &lexenddeca9BoldItalicFont);
#endif
#ifndef OMIT_TINY_FONT
EpdFont lexenddeca10RegularFont(&lexenddeca_10_regular);
EpdFont lexenddeca10BoldFont(&lexenddeca_10_bold);
EpdFont lexenddeca10ItalicFont(&lexenddeca_10_italic);
EpdFont lexenddeca10BoldItalicFont(&lexenddeca_10_bolditalic);
EpdFontFamily lexenddeca10FontFamily(&lexenddeca10RegularFont, &lexenddeca10BoldFont, &lexenddeca10ItalicFont,
                                     &lexenddeca10BoldItalicFont);
#endif
#ifndef OMIT_SMALL_FONT
EpdFont lexenddeca12RegularFont(&lexenddeca_12_regular);
EpdFont lexenddeca12BoldFont(&lexenddeca_12_bold);
EpdFont lexenddeca12ItalicFont(&lexenddeca_12_italic);
EpdFont lexenddeca12BoldItalicFont(&lexenddeca_12_bolditalic);
EpdFontFamily lexenddeca12FontFamily(&lexenddeca12RegularFont, &lexenddeca12BoldFont, &lexenddeca12ItalicFont,
                                     &lexenddeca12BoldItalicFont);
#endif
#ifndef OMIT_LARGE_FONT
EpdFont lexenddeca16RegularFont(&lexenddeca_16_regular);
EpdFont lexenddeca16BoldFont(&lexenddeca_16_bold);
EpdFont lexenddeca16ItalicFont(&lexenddeca_16_italic);
EpdFont lexenddeca16BoldItalicFont(&lexenddeca_16_bolditalic);
EpdFontFamily lexenddeca16FontFamily(&lexenddeca16RegularFont, &lexenddeca16BoldFont, &lexenddeca16ItalicFont,
                                     &lexenddeca16BoldItalicFont);
#endif
#ifndef OMIT_XLARGE_FONT
EpdFont lexenddeca18RegularFont(&lexenddeca_18_regular);
EpdFont lexenddeca18BoldFont(&lexenddeca_18_bold);
EpdFont lexenddeca18ItalicFont(&lexenddeca_18_italic);
EpdFont lexenddeca18BoldItalicFont(&lexenddeca_18_bolditalic);
EpdFontFamily lexenddeca18FontFamily(&lexenddeca18RegularFont, &lexenddeca18BoldFont, &lexenddeca18ItalicFont,
                                     &lexenddeca18BoldItalicFont);
#endif
#ifndef OMIT_HUGE_FONT
EpdFont lexenddeca20RegularFont(&lexenddeca_20_regular);
EpdFont lexenddeca20BoldFont(&lexenddeca_20_bold);
EpdFont lexenddeca20ItalicFont(&lexenddeca_20_italic);
EpdFont lexenddeca20BoldItalicFont(&lexenddeca_20_bolditalic);
EpdFontFamily lexenddeca20FontFamily(&lexenddeca20RegularFont, &lexenddeca20BoldFont, &lexenddeca20ItalicFont,
                                     &lexenddeca20BoldItalicFont);
#endif

#ifndef OMIT_TEENSY_FONT
EpdFont bitter8RegularFont(&bitter_8_regular);
EpdFont bitter8BoldFont(&bitter_8_bold);
EpdFont bitter8ItalicFont(&bitter_8_italic);
EpdFont bitter8BoldItalicFont(&bitter_8_bolditalic);
EpdFontFamily bitter8FontFamily(&bitter8RegularFont, &bitter8BoldFont, &bitter8ItalicFont, &bitter8BoldItalicFont);
#endif
#ifndef OMIT_ITTY_BITTY_FONT
EpdFont bitter9RegularFont(&bitter_9_regular);
EpdFont bitter9BoldFont(&bitter_9_bold);
EpdFont bitter9ItalicFont(&bitter_9_italic);
EpdFont bitter9BoldItalicFont(&bitter_9_bolditalic);
EpdFontFamily bitter9FontFamily(&bitter9RegularFont, &bitter9BoldFont, &bitter9ItalicFont, &bitter9BoldItalicFont);
#endif
#ifndef OMIT_TINY_FONT
EpdFont bitter10RegularFont(&bitter_10_regular);
EpdFont bitter10BoldFont(&bitter_10_bold);
EpdFont bitter10ItalicFont(&bitter_10_italic);
EpdFont bitter10BoldItalicFont(&bitter_10_bolditalic);
EpdFontFamily bitter10FontFamily(&bitter10RegularFont, &bitter10BoldFont, &bitter10ItalicFont, &bitter10BoldItalicFont);
#endif
#ifndef OMIT_SMALL_FONT
EpdFont bitter12RegularFont(&bitter_12_regular);
EpdFont bitter12BoldFont(&bitter_12_bold);
EpdFont bitter12ItalicFont(&bitter_12_italic);
EpdFont bitter12BoldItalicFont(&bitter_12_bolditalic);
EpdFontFamily bitter12FontFamily(&bitter12RegularFont, &bitter12BoldFont, &bitter12ItalicFont, &bitter12BoldItalicFont);
#endif
#ifndef OMIT_MEDIUM_FONT
EpdFont bitter14RegularFont(&bitter_14_regular);
EpdFont bitter14BoldFont(&bitter_14_bold);
EpdFont bitter14ItalicFont(&bitter_14_italic);
EpdFont bitter14BoldItalicFont(&bitter_14_bolditalic);
EpdFontFamily bitter14FontFamily(&bitter14RegularFont, &bitter14BoldFont, &bitter14ItalicFont, &bitter14BoldItalicFont);
#endif
#ifndef OMIT_LARGE_FONT
EpdFont bitter16RegularFont(&bitter_16_regular);
EpdFont bitter16BoldFont(&bitter_16_bold);
EpdFont bitter16ItalicFont(&bitter_16_italic);
EpdFont bitter16BoldItalicFont(&bitter_16_bolditalic);
EpdFontFamily bitter16FontFamily(&bitter16RegularFont, &bitter16BoldFont, &bitter16ItalicFont, &bitter16BoldItalicFont);
#endif
#ifndef OMIT_XLARGE_FONT
EpdFont bitter18RegularFont(&bitter_18_regular);
EpdFont bitter18BoldFont(&bitter_18_bold);
EpdFont bitter18ItalicFont(&bitter_18_italic);
EpdFont bitter18BoldItalicFont(&bitter_18_bolditalic);
EpdFontFamily bitter18FontFamily(&bitter18RegularFont, &bitter18BoldFont, &bitter18ItalicFont, &bitter18BoldItalicFont);
#endif
#ifndef OMIT_HUGE_FONT
EpdFont bitter20RegularFont(&bitter_20_regular);
EpdFont bitter20BoldFont(&bitter_20_bold);
EpdFont bitter20ItalicFont(&bitter_20_italic);
EpdFont bitter20BoldItalicFont(&bitter_20_bolditalic);
EpdFontFamily bitter20FontFamily(&bitter20RegularFont, &bitter20BoldFont, &bitter20ItalicFont, &bitter20BoldItalicFont);
#endif

EpdFont smallFont(&inter_8_regular);
EpdFontFamily smallFontFamily(&smallFont);

EpdFont ui10RegularFont(&inter_10_regular);
EpdFont ui10BoldFont(&inter_10_bold);
EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);

EpdFont ui12RegularFont(&inter_12_regular);
EpdFont ui12BoldFont(&inter_12_bold);
EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont);

// measurement of power button press duration calibration value
unsigned long t1 = 0;

// Boot-phase timing breadcrumbs (Repair13 groundwork). Captured during setup()
// and written once to Duet's state directory a few seconds after boot so
// the measurement itself never slows the perceived wake. Overwritten each boot.
struct BootTiming {
  unsigned long serialReadyMs = 0;
  unsigned long gpioReadyMs = 0;
  unsigned long storageReadyMs = 0;
  unsigned long panicCheckMs = 0;
  unsigned long settingsMs = 0;
  unsigned long gestureMs = 0;
  unsigned long storesMs = 0;
  unsigned long panelMs = 0;
  unsigned long displayInitMs = 0;
  unsigned long builtinFontsMs = 0;
  unsigned long sdDiscoverMs = 0;
  unsigned long sdLoadMs = 0;
  unsigned long displayReadyMs = 0;
  unsigned long dispatchDoneMs = 0;
  unsigned long powerReleaseWaitMs = 0;
  unsigned long setupDoneMs = 0;
  int resetReason = -1;
  int wakeupCause = -1;
  bool written = false;
};
BootTiming bootTiming;
unsigned long t2 = 0;

// Set when the screenshot combo (Power + Volume Down) fires, so the subsequent
// power button release does not also trigger a short-press action (e.g. sleep).
static bool screenshotComboHandled = false;

const char* resetReasonName(const esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "POWERON";
    case ESP_RST_EXT:
      return "EXT";
    case ESP_RST_SW:
      return "SW";
    case ESP_RST_PANIC:
      return "PANIC";
    case ESP_RST_INT_WDT:
      return "INT_WDT";
    case ESP_RST_TASK_WDT:
      return "TASK_WDT";
    case ESP_RST_WDT:
      return "WDT";
    case ESP_RST_DEEPSLEEP:
      return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT";
    case ESP_RST_SDIO:
      return "SDIO";
    case ESP_RST_USB:
      return "USB";
    case ESP_RST_JTAG:
      return "JTAG";
    case ESP_RST_EFUSE:
      return "EFUSE";
    case ESP_RST_PWR_GLITCH:
      return "PWR_GLITCH";
    case ESP_RST_CPU_LOCKUP:
      return "CPU_LOCKUP";
    case ESP_RST_UNKNOWN:
    default:
      return "UNKNOWN";
  }
}

const char* wakeupCauseName(const esp_sleep_wakeup_cause_t cause) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      return "UNDEFINED";
    case ESP_SLEEP_WAKEUP_ALL:
      return "ALL";
    case ESP_SLEEP_WAKEUP_EXT0:
      return "EXT0";
    case ESP_SLEEP_WAKEUP_EXT1:
      return "EXT1";
    case ESP_SLEEP_WAKEUP_TIMER:
      return "TIMER";
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
      return "TOUCHPAD";
    case ESP_SLEEP_WAKEUP_ULP:
      return "ULP";
    case ESP_SLEEP_WAKEUP_GPIO:
      return "GPIO";
    case ESP_SLEEP_WAKEUP_UART:
      return "UART";
    case ESP_SLEEP_WAKEUP_WIFI:
      return "WIFI";
    case ESP_SLEEP_WAKEUP_COCPU:
      return "COCPU";
    case ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG:
      return "COCPU_TRAP";
    case ESP_SLEEP_WAKEUP_BT:
      return "BT";
    default:
      return "UNKNOWN";
  }
}

const char* wakeupRouteName(const HalGPIO::WakeupReason reason) {
  switch (reason) {
    case HalGPIO::WakeupReason::PowerButton:
      return "PowerButton";
    case HalGPIO::WakeupReason::AfterFlash:
      return "AfterFlash";
    case HalGPIO::WakeupReason::AfterUSBPower:
      return "AfterUSBPower";
    case HalGPIO::WakeupReason::Other:
    default:
      return "Other";
  }
}

// Definitions for SilentRestart.h. RTC_NOINIT survives ESP.restart() but not power loss.
RTC_NOINIT_ATTR uint32_t silentRebootMagic;
RTC_NOINIT_ATTR uint32_t silentRebootTarget;
constexpr uint32_t SILENT_REBOOT_MAGIC = 0xC1EAB007;
constexpr uint32_t SILENT_REBOOT_TARGET_HOME = 0;
constexpr uint32_t SILENT_REBOOT_TARGET_READER = 1;

// How the device is coming back to life, resolved once at boot. Both resume
// flows suppress the splash and leave the panel holding its pre-boot frame; a
// plain boot shows the splash. See setup() for the resolution.
enum class BootResume : uint8_t {
  Splash,       // cold boot, flash, panic, or plain reboot
  Silent,       // heap-defrag ESP.restart() (RTC flag; lost on power loss)
  QuickResume,  // wake from a quick-resume deep sleep (SD flag; survives power loss)
};

// Latched true once enterDeepSleep() commits to sleeping, before it tears down
// the current activity. WiFi activities call silentRestart() in onExit() to
// clear heap fragmentation on the way out, but deep sleep is a full chip reset
// on wake and already clears the heap, so rebooting here would just power the
// device back up against the user's sleep gesture. Never cleared:
// startDeepSleep() does not return, so a set latch only ends at the wakeup reset.
static bool deepSleepInProgress = false;

void silentRestart() {
  if (deepSleepInProgress) return;  // sleeping supersedes the heap-defrag reboot
  silentRebootTarget = SILENT_REBOOT_TARGET_HOME;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Silent restart (target=home)");
  // E-ink retains the previous frame until Home's first paint lands (~2-3s).
  // Without an overlay, users don't see the reboot and fire input through to
  // Home. Select on the default selectorIndex=0 then opens the most-recent
  // book, looking like a trampoline back to the reader they just exited.
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  ESP.restart();
}

void silentRestartToReader() {
  if (deepSleepInProgress) return;  // sleeping supersedes the heap-defrag reboot
  silentRebootTarget = SILENT_REBOOT_TARGET_READER;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Silent restart (target=reader)");
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  ESP.restart();
}

void waitForPowerRelease() {
  gpio.update();
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    delay(50);
    gpio.update();
  }
}

bool isGlobalPowerButtonAction(const CrossPointSettings::SHORT_PWRBTN action) {
  return isPowerButtonActionAvailableOutsideReader(action);
}

bool startGlobalSyncProgress() {
  if (!KOREADER_STORE.hasCredentials()) {
    activityManager.pushActivity(std::make_unique<KOReaderSettingsActivity>(renderer, mappedInputManager));
    return true;
  }

  const std::string epubPath = APP_STATE.openEpubPath;
  if (epubPath.empty() || !FsHelpers::hasEpubExtension(epubPath) || !Storage.exists(epubPath.c_str())) {
    LOG_DBG("MAIN", "No syncable EPUB open, opening KOReader settings instead");
    activityManager.pushActivity(std::make_unique<KOReaderSettingsActivity>(renderer, mappedInputManager));
    return true;
  }

  auto epub = std::make_shared<Epub>(epubPath, DUET_BOOKS_ROOT_PATH "");
  if (!epub->load(true, SETTINGS.embeddedStyle == 0)) {
    LOG_ERR("MAIN", "Failed to load EPUB for global sync: %s", epubPath.c_str());
    activityManager.pushActivity(std::make_unique<KOReaderSettingsActivity>(renderer, mappedInputManager));
    return true;
  }

  epub->setupCacheDir();

  int spineIndex = 0;
  int pageNumber = 0;
  int totalPagesInSpine = 1;
  EpubReaderUtils::Progress progress;
  if (EpubReaderUtils::loadProgress(*epub, progress, "MAIN")) {
    spineIndex = progress.spineIndex;
    pageNumber = progress.pageNumber;
    if (progress.hasPageCount) {
      totalPagesInSpine = std::max(1, progress.pageCount);
    }
  }

  if (spineIndex < 0 || spineIndex >= epub->getSpineItemsCount()) {
    spineIndex = 0;
  }

  CrossPointPosition localPos = {spineIndex, pageNumber, totalPagesInSpine};
  KOReaderPosition localKoPos = ProgressMapper::toKOReader(epub, localPos);
  const int tocIdx = epub->getTocIndexForSpineIndex(spineIndex);
  std::string localChapterName = (tocIdx >= 0) ? epub->getTocItem(tocIdx).title : "";

  activityManager.pushActivity(
      std::make_unique<KOReaderSyncActivity>(renderer, mappedInputManager, epubPath, spineIndex, pageNumber,
                                             totalPagesInSpine, std::move(localKoPos), std::move(localChapterName)));
  return true;
}

CrossPointSettings::SHORT_PWRBTN getPowerButtonAction() {
  static bool longPowerButtonHandled = false;

  if (mappedInputManager.wasReleased(MappedInputManager::Button::Power)) {
    if (longPowerButtonHandled) {
      longPowerButtonHandled = false;
      screenshotComboHandled = false;
      return CrossPointSettings::SHORT_PWRBTN::IGNORE;
    }

    if (screenshotComboHandled) {
      screenshotComboHandled = false;
      return CrossPointSettings::SHORT_PWRBTN::IGNORE;
    }

    if (mappedInputManager.getHeldTime() >= SETTINGS.getPowerButtonLongPressDuration()) {
      return static_cast<CrossPointSettings::SHORT_PWRBTN>(SETTINGS.longPwrBtn);
    }
  }

  if (mappedInputManager.wasShortPowerActionResolved()) {
    return static_cast<CrossPointSettings::SHORT_PWRBTN>(mappedInputManager.getResolvedShortPowerAction());
  }

  if (longPowerButtonHandled || !mappedInputManager.isPressed(MappedInputManager::Button::Power) ||
      mappedInputManager.getHeldTime() < SETTINGS.getPowerButtonLongPressDuration()) {
    return CrossPointSettings::SHORT_PWRBTN::IGNORE;
  }

  const auto action = static_cast<CrossPointSettings::SHORT_PWRBTN>(SETTINGS.longPwrBtn);
  if (!isGlobalPowerButtonAction(action)) {
    return CrossPointSettings::SHORT_PWRBTN::IGNORE;
  }

  longPowerButtonHandled = true;
  return action;
}

bool handleGlobalPowerButtonAction(const CrossPointSettings::SHORT_PWRBTN action) {
  switch (action) {
    case CrossPointSettings::SHORT_PWRBTN::SLEEP:
      enterDeepSleep();
      return true;
    case CrossPointSettings::SHORT_PWRBTN::FORCE_REFRESH: {
      LOG_DBG("MAIN", "Manual screen refresh triggered");
      RenderLock lock;
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      return true;
    }
    case CrossPointSettings::SHORT_PWRBTN::SCREENSHOT: {
      if (activityManager.canSnapshotForSleepOverlay()) {
        return false;
      }
      RenderLock lock;
      ScreenshotUtil::takeScreenshot(renderer);
      return true;
    }
    case CrossPointSettings::SHORT_PWRBTN::SYNC_PROGRESS:
      if (activityManager.canSnapshotForSleepOverlay()) {
        return false;
      }
      return startGlobalSyncProgress();
    case CrossPointSettings::SHORT_PWRBTN::FILE_TRANSFER:
      if (activityManager.canSnapshotForSleepOverlay()) {
        return false;
      }
      activityManager.goToFileTransfer();
      return true;
    case CrossPointSettings::SHORT_PWRBTN::CALIBRE_WIRELESS:
      if (activityManager.canSnapshotForSleepOverlay()) {
        return false;
      }
      activityManager.goToCalibreWireless();
      return true;
    case CrossPointSettings::SHORT_PWRBTN::JOIN_NETWORK:
      if (activityManager.canSnapshotForSleepOverlay()) {
        return false;
      }
      activityManager.goToJoinNetworkFileTransfer();
      return true;
    case CrossPointSettings::SHORT_PWRBTN::CREATE_HOTSPOT:
      if (activityManager.canSnapshotForSleepOverlay()) {
        return false;
      }
      activityManager.goToHotspotFileTransfer();
      return true;
    default:
      return false;
  }
}

namespace {
constexpr uint16_t POST_SLEEP_SCREEN_SETTLE_MS = 500;
constexpr uint8_t TILT_SLEEP_MAX_ATTEMPTS = 3;
constexpr uint16_t TILT_SLEEP_RETRY_DELAY_MS = 10;
// A sleeping reader has to boot far enough to observe the follow-up taps, so
// allow a relaxed cadence while keeping the original post-GPIO gesture route.
constexpr uint16_t LOCKED_POWER_INTER_CLICK_MS = 1200;
constexpr uint16_t LOCKED_POWER_HOLD_MS = 900;
constexpr uint32_t LOCKED_POWER_EDGE_DEBOUNCE_US = 25000;

enum class LockedPowerGesture : uint8_t { Wake, SleepUnchanged, Cycle };

#ifndef SIMULATOR
volatile uint8_t lockedPowerClickCount = 0;
volatile uint32_t lockedPowerLastEdgeUs = 0;

void IRAM_ATTR recordLockedPowerPress(const uint32_t nowUs) {
  if (nowUs - lockedPowerLastEdgeUs < LOCKED_POWER_EDGE_DEBOUNCE_US) {
    return;
  }
  lockedPowerLastEdgeUs = nowUs;
  if (lockedPowerClickCount < CrossPointSettings::SLEEP_CYCLE_THREE_CLICKS) {
    lockedPowerClickCount = static_cast<uint8_t>(lockedPowerClickCount + 1);
  }
}

void IRAM_ATTR onLockedPowerPress() { recordLockedPowerPress(micros()); }

void armLockedPowerClickCounter() {
  lockedPowerClickCount = 1;  // The press that powered the sleeping device on.
  lockedPowerLastEdgeUs = micros();
  attachInterrupt(InputManager::POWER_BUTTON_PIN, onLockedPowerPress, FALLING);
}

void disarmLockedPowerClickCounter() { detachInterrupt(InputManager::POWER_BUTTON_PIN); }

bool isRawPowerPressed() { return digitalRead(InputManager::POWER_BUTTON_PIN) == LOW; }

LockedPowerGesture detectLockedPowerGesture(uint8_t requiredClicks, uint16_t wakeHoldMs) {
  requiredClicks = std::min<uint8_t>(requiredClicks, CrossPointSettings::SLEEP_CYCLE_THREE_CLICKS);
  if (requiredClicks == CrossPointSettings::SLEEP_CYCLE_OFF) {
    return LockedPowerGesture::Wake;
  }

  // A normal human tap can easily last longer than 200 ms, especially while
  // waiting for an e-ink response. Reserve wake for a deliberate hold so the
  // configured two/three-click sleep gesture does not demand rapid-fire taps.
  const uint16_t holdThresholdMs = std::max<uint16_t>(wakeHoldMs, LOCKED_POWER_HOLD_MS);
  bool pressed = isRawPowerPressed();
  unsigned long pressStartedMs = millis();
  if (pressed) {
    const uint32_t edgeAgeMs = (micros() - lockedPowerLastEdgeUs) / 1000;
    const unsigned long nowMs = millis();
    pressStartedMs = edgeAgeMs < nowMs ? nowMs - edgeAgeMs : 0;
  }
  unsigned long nextClickDeadlineMs = millis() + LOCKED_POWER_INTER_CLICK_MS;

  while (true) {
    const unsigned long nowMs = millis();
    const bool nowPressed = isRawPowerPressed();

    if (nowPressed) {
      if (!pressed) {
        pressed = true;
        // The ISR normally records this edge. Polling here makes the gesture
        // survive a wake edge that hardware interrupt delivery misses.
        recordLockedPowerPress(micros());
        pressStartedMs = nowMs;
      }
      if (nowMs - pressStartedMs >= holdThresholdMs) {
        LOG_INF("MAIN", "Locked Power gesture: hold detected; waking");
        return LockedPowerGesture::Wake;
      }
    } else {
      if (pressed) {
        pressed = false;
        nextClickDeadlineMs = nowMs + LOCKED_POWER_INTER_CLICK_MS;
      }

      const uint8_t observedClicks = lockedPowerClickCount;
      if (observedClicks >= requiredClicks) {
        LOG_INF("MAIN", "Locked Power gesture: %u/%u clicks; cycling", observedClicks, requiredClicks);
        return LockedPowerGesture::Cycle;
      }
      if (static_cast<int32_t>(nowMs - nextClickDeadlineMs) >= 0) {
        LOG_INF("MAIN", "Locked Power gesture: %u/%u clicks; staying asleep", observedClicks, requiredClicks);
        return LockedPowerGesture::SleepUnchanged;
      }
    }

    delay(5);
  }
}
#else
void armLockedPowerClickCounter() {}
void disarmLockedPowerClickCounter() {}
LockedPowerGesture detectLockedPowerGesture(uint8_t, uint16_t) { return LockedPowerGesture::Wake; }
#endif

void putTiltSensorToSleepForDeepSleep() {
  if (!halTiltSensor.isAvailable()) {
    return;
  }

  for (uint8_t attempt = 0; attempt < TILT_SLEEP_MAX_ATTEMPTS; ++attempt) {
    if (halTiltSensor.deepSleep()) {
      return;
    }
    delay(TILT_SLEEP_RETRY_DELAY_MS);
  }
  LOG_ERR("MAIN", "Tilt sensor did not confirm sleep before deep sleep");
}

[[noreturn]] void finishLockedPowerGesture(bool cycleSleepImage) {
  disarmLockedPowerClickCounter();

  if (cycleSleepImage) {
    APP_STATE.loadFromFile();
    display.begin(true);
    renderer.begin();
    SleepActivity sleepActivity(renderer, mappedInputManager, false);
    sleepActivity.cycleCustomSleepScreen();
  }

  putTiltSensorToSleepForDeepSleep();
  if (cycleSleepImage) {
    display.deepSleep();
  }
  LOG_INF("MAIN", "%s; re-entering deep sleep",
          cycleSleepImage ? "Sleep image cycle complete" : "Lock screen unchanged");
  powerManager.startDeepSleep(gpio);

  while (true) {
    delay(1000);
  }
}
}  // namespace

constexpr char SLEEP_FRAME_FILE[] = DUET_STATE_ROOT_PATH "/sleep_frame.bin";
constexpr char INSTALLED_FIRMWARE_MARKER_FILE[] = DUET_STATE_ROOT_PATH "/installed_firmware.txt";

static bool recordInstalledFirmwareVersion() {
  const String currentIdentity = String(gpio.deviceIsX3() ? "X3" : "X4") + ":" + DUET_VERSION;
  String savedIdentity = Storage.readFile(INSTALLED_FIRMWARE_MARKER_FILE);
  savedIdentity.trim();
  if (savedIdentity == currentIdentity) return false;

  if (!Storage.writeFile(INSTALLED_FIRMWARE_MARKER_FILE, currentIdentity)) {
    LOG_ERR("BOOT", "Failed to write installed-firmware marker");
  }
  LOG_INF("BOOT", "First boot of firmware identity %s (previous=%s)", currentIdentity.c_str(),
          savedIdentity.length() > 0 ? savedIdentity.c_str() : "none");
  return true;
}

static void saveSleepFrameBuffer() {
  HalFile file;
  if (!Storage.openFileForWrite("SLP", SLEEP_FRAME_FILE, file)) return;
  file.write(renderer.getFrameBuffer(), renderer.getBufferSize());
  file.close();
}

static bool loadSleepFrameBuffer() {
  HalFile file;
  if (!Storage.openFileForRead("SLP", SLEEP_FRAME_FILE, file)) return false;
  const size_t bufferSize = display.getBufferSize();
  const size_t bytesRead = file.read(display.getFrameBuffer(), bufferSize);
  file.close();
  if (bytesRead != bufferSize) {
    Storage.remove(SLEEP_FRAME_FILE);
    return false;
  }
  Storage.remove(SLEEP_FRAME_FILE);
  return true;
}

// Enter deep sleep mode
void enterDeepSleep(bool fromTimeout) {
  HalPowerManager::Lock powerLock;  // Ensure we are at normal CPU frequency for sleep preparation
  APP_STATE.lastSleepFromReader = activityManager.isReaderActivity();

  const bool isQuickResumeSleep =
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME ||
      (fromTimeout &&
       SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT);
  APP_STATE.showBootScreen = !isQuickResumeSleep;

  APP_STATE.saveToFile();

  // Commit to sleeping before goToSleep() runs the outgoing activity's onExit():
  // a WiFi activity would otherwise silentRestart() here and reboot instead.
  deepSleepInProgress = true;
  activityManager.goToSleep(fromTimeout);

  if (isQuickResumeSleep) {
    saveSleepFrameBuffer();
  } else {
    delay(POST_SLEEP_SCREEN_SETTLE_MS);
  }

  if (gpio.deviceIsX3() && SETTINGS.autoBackupStats != 0) {
    ReadingStatsDateTime now;
    if (getCurrentLocalReadingStatsDateTime(now) && !backupGlobalStats(false)) {
      LOG_ERR("MAIN", "Automatic reading-stats backup failed before deep sleep");
    }
  }

  putTiltSensorToSleepForDeepSleep();
  display.deepSleep();
  LOG_DBG("MAIN", "Entering deep sleep");

  powerManager.startDeepSleep(gpio);
}

void setupDisplayAndFonts(bool seamless = false) {
#ifdef SIMULATOR
  (void)seamless;
  display.begin();
#else
  display.begin(seamless);
#endif
  bootTiming.panelMs = millis();
  renderer.begin();
  activityManager.begin();
  bootTiming.displayInitMs = millis();
  LOG_DBG("MAIN", "Display initialized");

  // Initialize font decompressor for compressed reader fonts
  if (!fontDecompressor.init()) {
    LOG_ERR("MAIN", "Font decompressor init failed");
  }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);

#ifndef OMIT_TEENSY_FONT
  renderer.insertFont(LEXENDDECA_8_FONT_ID, lexenddeca8FontFamily);
#endif
#ifndef OMIT_ITTY_BITTY_FONT
  renderer.insertFont(LEXENDDECA_9_FONT_ID, lexenddeca9FontFamily);
#endif
#ifndef OMIT_TINY_FONT
  renderer.insertFont(LEXENDDECA_10_FONT_ID, lexenddeca10FontFamily);
#endif
#ifndef OMIT_SMALL_FONT
  renderer.insertFont(LEXENDDECA_12_FONT_ID, lexenddeca12FontFamily);
#endif
#ifndef OMIT_MEDIUM_FONT
  renderer.insertFont(LEXENDDECA_14_FONT_ID, lexenddeca14FontFamily);
#endif
#ifndef OMIT_LARGE_FONT
  renderer.insertFont(LEXENDDECA_16_FONT_ID, lexenddeca16FontFamily);
#endif
#ifndef OMIT_XLARGE_FONT
  renderer.insertFont(LEXENDDECA_18_FONT_ID, lexenddeca18FontFamily);
#endif
#ifndef OMIT_HUGE_FONT
  renderer.insertFont(LEXENDDECA_20_FONT_ID, lexenddeca20FontFamily);
#endif

#ifndef OMIT_TEENSY_FONT
  renderer.insertFont(BITTER_8_FONT_ID, bitter8FontFamily);
#endif
#ifndef OMIT_ITTY_BITTY_FONT
  renderer.insertFont(BITTER_9_FONT_ID, bitter9FontFamily);
#endif
#ifndef OMIT_TINY_FONT
  renderer.insertFont(BITTER_10_FONT_ID, bitter10FontFamily);
#endif
#ifndef OMIT_SMALL_FONT
  renderer.insertFont(BITTER_12_FONT_ID, bitter12FontFamily);
#endif
#ifndef OMIT_MEDIUM_FONT
  renderer.insertFont(BITTER_14_FONT_ID, bitter14FontFamily);
#endif
#ifndef OMIT_LARGE_FONT
  renderer.insertFont(BITTER_16_FONT_ID, bitter16FontFamily);
#endif
#ifndef OMIT_XLARGE_FONT
  renderer.insertFont(BITTER_18_FONT_ID, bitter18FontFamily);
#endif
#ifndef OMIT_HUGE_FONT
  renderer.insertFont(BITTER_20_FONT_ID, bitter20FontFamily);
#endif
  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);

  bootTiming.builtinFontsMs = millis();

  // Discover (catalog cache or scan), then load the selected reading font.
  sdFontSystem.begin(renderer);
  bootTiming.sdDiscoverMs = millis();
  sdFontSystem.loadSelectedFamily(renderer);
  bootTiming.sdLoadMs = millis();

  LOG_DBG("MAIN", "Fonts setup");
}

void setup() {
#ifndef SIMULATOR
  BoardConfig::holdPowerRails();
#endif
  t1 = millis();

  const esp_reset_reason_t rawResetReason = esp_reset_reason();
  const esp_sleep_wakeup_cause_t rawWakeupCause = esp_sleep_get_wakeup_cause();

#ifdef ENABLE_SERIAL_LOG
  // Earliest possible Serial setup. The 250 ms stall before begin() lets the
  // USB Serial/JTAG peripheral finish power-on and lets the host complete USB
  // enumeration before we touch the CDC state — otherwise cold boot races
  // and the host has to be physically replugged for logs to flow. Warm reboot
  // worked without the delay because USB was already enumerated.
  delay(250);
  // Web Serial sends file data in 256-byte chunks and waits for a 1-byte ACK.
  // HWCDC defaults to a 256-byte RX queue, which is fine for logs but too small
  // for chunked file transfer.
#if !defined(SIMULATOR)
  logSerial.setRxBufferSize(1024);
  logSerial.setTxBufferSize(1024);
#endif
  Serial.begin(115200);
#ifndef SIMULATOR
  logSerial.setTxTimeoutMs(1);  // This is a load-bearing 1. Do not modify.
#endif
#endif

  HalSystem::begin();
  bootTiming.serialReadyMs = millis();
  bootTiming.resetReason = static_cast<int>(rawResetReason);
  bootTiming.wakeupCause = static_cast<int>(rawWakeupCause);
  LOG_INF("BOOT", "Reset diagnostic: reset=%d(%s) sleepWake=%d(%s)", static_cast<int>(rawResetReason),
          resetReasonName(rawResetReason), static_cast<int>(rawWakeupCause), wakeupCauseName(rawWakeupCause));

  // Read-and-clear so a panic later in setup() doesn't loop into silent reboot.
  // Bound the target range too — RTC_NOINIT memory is uninitialized on cold boot.
  const bool isSilentReboot = (silentRebootMagic == SILENT_REBOOT_MAGIC);
  const uint32_t snapshotTarget =
      (isSilentReboot && silentRebootTarget <= SILENT_REBOOT_TARGET_READER) ? silentRebootTarget : 0;
  silentRebootMagic = 0;
  silentRebootTarget = 0;

  gpio.begin();
  // X3 hardware detection releases its temporary I2C probe bus. Reopen the
  // production bus before getWakeupReason() asks the fuel gauge whether USB is
  // connected; probing a closed TwoWire bus can leave its mutex locked.
  LOG_INF("BOOT", "Starting power manager");
  powerManager.begin();
  LOG_INF("BOOT", "Power manager ready");
  // Preserve the original locked-screen gesture route: arm only after GPIO
  // initialization has identified a real Power-button wake.
  const auto wakeupReason = gpio.getWakeupReason();
  if (wakeupReason == HalGPIO::WakeupReason::PowerButton) {
    armLockedPowerClickCounter();
  }
  LOG_INF("BOOT", "Starting X3 peripherals");
  halTiltSensor.begin();
  halClock.begin();
  LOG_INF("BOOT", "X3 peripherals ready");
  bootTiming.gpioReadyMs = millis();

  LOG_INF("MAIN", "Hardware detect: %s", gpio.deviceIsX3() ? "X3" : "X4");
  LOG_INF("BOOT", "Post-GPIO diagnostic: device=%s usb=%d silentReboot=%d silentTarget=%lu",
          gpio.deviceIsX3() ? "X3" : "X4", gpio.isUsbConnected() ? 1 : 0, isSilentReboot ? 1 : 0,
          static_cast<unsigned long>(snapshotTarget));

  // SD Card Initialization
  // We need 6 open files concurrently when parsing a new chapter
  if (!Storage.begin()) {
    LOG_ERR("MAIN", "SD card initialization failed");
    setupDisplayAndFonts(isSilentReboot);
    activityManager.goToFullScreenMessage("SD card error", EpdFontFamily::BOLD);
    return;
  }

  bootTiming.storageReadyMs = millis();
  HalSystem::checkPanic();
  bootTiming.panicCheckMs = millis();

  // setup() is still on the input/storage task here. Import bounded global
  // state before any stores load; large book/cache trees stay lazy fallbacks.
  DuetStorage::migrateLegacyNamespaceOnDevice();
  const bool firmwareVersionChangedThisBoot = recordInstalledFirmwareVersion();

  SETTINGS.loadFromFile();
#if !defined(SIMULATOR)
  Storage.installDateTimeCallback(&SETTINGS.clockUtcOffsetQ);
#endif
  bootTiming.settingsMs = millis();

  LOG_INF("BOOT", "Wake route: %s", wakeupRouteName(wakeupReason));
  switch (wakeupReason) {
    case HalGPIO::WakeupReason::PowerButton: {
      if (SETTINGS.sleepScreenCyclePowerClicks != CrossPointSettings::SLEEP_CYCLE_OFF) {
        const LockedPowerGesture gesture =
            detectLockedPowerGesture(SETTINGS.sleepScreenCyclePowerClicks, SETTINGS.getPowerButtonWakeDuration());
        if (gesture == LockedPowerGesture::Cycle) {
          finishLockedPowerGesture(true);
        }
        if (gesture == LockedPowerGesture::SleepUnchanged) {
          finishLockedPowerGesture(false);
        }
        disarmLockedPowerClickCounter();
        LOG_INF("BOOT", "Locked Power hold accepted; continuing wake");
      } else {
        disarmLockedPowerClickCounter();
        LOG_INF("BOOT", "Power-button wake: verifying duration required=%u shortAllowed=%d",
                SETTINGS.getPowerButtonWakeDuration(), SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP);
        gpio.verifyPowerButtonWakeup(SETTINGS.getPowerButtonWakeDuration(),
                                     SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP);
      }
      break;
    }
    case HalGPIO::WakeupReason::AfterUSBPower:
      // TEMP: continue booting while diagnosing post-flash/reset behavior.
      // Normal behavior is to go back to sleep when USB power causes a cold boot.
      LOG_INF("BOOT", "AfterUSBPower route: TEMP continuing boot instead of deep sleep");
      break;
    case HalGPIO::WakeupReason::AfterFlash:
      // After flashing, just proceed to boot
      LOG_INF("BOOT", "AfterFlash route: continuing boot");
      break;
    case HalGPIO::WakeupReason::Other:
    default:
      LOG_INF("BOOT", "Other wake route: continuing boot");
      break;
  }

  bootTiming.gestureMs = millis();
  APP_STATE.loadFromFile();
  RECENT_BOOKS.loadFromFile();
  LAUNCHER_LAYOUT.loadFromFile();
  I18N.setLanguage(static_cast<Language>(SETTINGS.language));
  // FAVORITES / ACHIEVEMENT_STORE / KOREADER_STORE / OPDS_STORE are not needed
  // for the first frame; they load in the first loop() pass (deferredStoreLoads
  // below) while the render task paints, cutting several per-book cache opens —
  // each a linear scan of a huge directory — out of the boot-to-visible path.
  UITheme::getInstance().reload();
  ButtonNavigator::setMappedInputManager(mappedInputManager);
  bootTiming.storesMs = millis();

  // Recovery firmware mode: hold left side button (BTN_UP) together with the power button at
  // boot to skip directly to the SD-card firmware update screen. Useful on devices where USB
  // flashing has been locked down (e.g. recent X3 firmware).
  bool recoveryFirmwareMode = false;
  if (wakeupReason == HalGPIO::WakeupReason::PowerButton) {
    // Refresh the cached button state a few times — isPressed() needs ~half a second to settle
    // after boot per the HalGPIO contract. Use a millis-based deadline so we always wait the full
    // settle window even if the loop body takes longer than expected on slow boots.
    const unsigned long settleStart = millis();
    while (millis() - settleStart < 500) {
      gpio.update();
      delay(10);
    }
    if (gpio.isPressed(HalGPIO::BTN_UP)) {
      recoveryFirmwareMode = true;
      LOG_INF("MAIN", "Recovery firmware mode (UP + POWER held at boot)");
    }
  }

  // First serial output only here to avoid timing inconsistencies for power button press duration verification
  LOG_DBG("MAIN", "Starting " DUET_PRODUCT_NAME " version " DUET_VERSION);

  // Resolve the single boot-presentation decision. Skipping the splash also
  // skips the panel-clearing pass and the X3 initial-full-sync arming (see
  // HalDisplay::begin), so the first paint is FAST_REFRESH (~500ms) over the
  // retained frame and input dispatches against a visible UI.
  const BootResume resume = isSilentReboot              ? BootResume::Silent
                            : !APP_STATE.showBootScreen ? BootResume::QuickResume
                                                        : BootResume::Splash;

  setupDisplayAndFonts(resume != BootResume::Splash);
  bootTiming.displayReadyMs = millis();

  switch (resume) {
    case BootResume::Silent:
      // Splash skipped: the routing block below picks the target activity; the
      // panel keeps showing the pre-reboot popup until that first paint lands.
      break;
    case BootResume::QuickResume:
      // One-shot flag: re-arm the splash for the next non-quick-resume boot. Save
      // before any painting so a hang in the blocking paint path can't strand
      // us in a quick-resume-with-no-frame loop on the next boot.
      APP_STATE.showBootScreen = true;
      APP_STATE.saveToFile();
      if (loadSleepFrameBuffer()) {
        // Frame restored: swap the sleep moon for the loading icon.
        const auto pageHeight = renderer.getScreenHeight();
        if (SETTINGS.readerDarkMode != 0) {
          renderer.drawImageInverted(LoadingIcon, 0, pageHeight - LOADINGICON_HEIGHT, LOADINGICON_WIDTH,
                                     LOADINGICON_HEIGHT);
        } else {
          renderer.drawImage(LoadingIcon, 0, pageHeight - LOADINGICON_HEIGHT, LOADINGICON_WIDTH, LOADINGICON_HEIGHT);
        }
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      } else {
        activityManager.goToBoot();  // frame file missing, fall back to the splash
      }
      break;
    case BootResume::Splash:
      activityManager.goToBoot(firmwareVersionChangedThisBoot);
      break;
  }

  if (resume == BootResume::Splash && !recoveryFirmwareMode && !HalSystem::isRebootFromPanic()) {
    // Boot got fast enough that the Duet splash vanished almost as soon as it
    // appeared; hold it briefly. Skipped for recovery and panic boots, where
    // reaching the tool screen matters more than the artwork.
    constexpr unsigned long SPLASH_HOLD_MS = 3000;
    delay(SPLASH_HOLD_MS);
  }

  if (recoveryFirmwareMode) {
    // Skip normal home/reader routing: jump straight into the SD firmware picker.
    activityManager.replaceActivity(
        std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInputManager, /*recoveryMode=*/true));
  } else if (HalSystem::isRebootFromPanic()) {
    // If we rebooted from a panic, go to crash report screen to show the panic info
    activityManager.goToCrashReport();
  } else if (resume == BootResume::Silent && snapshotTarget == SILENT_REBOOT_TARGET_READER &&
             !APP_STATE.openEpubPath.empty()) {
    activityManager.goToReader(APP_STATE.openEpubPath);
  } else if (resume == BootResume::Silent) {
    // target == home (or reader with no open book): land on home — don't fall
    // through to the sleep-wake "resume reader" logic, which fires on stale
    // openEpubPath + lastSleepFromReader from a prior session.
    activityManager.goHome();
  } else if (APP_STATE.openEpubPath.empty() || !APP_STATE.lastSleepFromReader ||
             mappedInputManager.isPressed(MappedInputManager::Button::Back) || APP_STATE.readerActivityLoadCount > 0) {
    // Boot to home screen if no book is open, last sleep was not from reader, back button is held, or reader activity
    // crashed (indicated by readerActivityLoadCount > 0)
    activityManager.goHome();
  } else {
    // Clear app state to avoid getting into a boot loop if the epub doesn't load
    const auto path = APP_STATE.openEpubPath;
    APP_STATE.openEpubPath = "";
    APP_STATE.readerActivityLoadCount++;
    APP_STATE.saveToFile();
    activityManager.goToReader(path);
  }

  if (resume == BootResume::Silent) {
    // Block until the first paint physically completes. refreshDisplay()
    // waits on the panel BUSY pin so when this returns the user can see the
    // new activity. Without the wait, an edge captured by gpio.update()
    // during boot dispatches against an invisible Home and the default
    // selectorIndex=0 opens the most-recent book.
    activityManager.requestUpdateAndWait();
    // Absorb any button held at this point into currentState as a non-edge:
    // two gpio.update() calls separated by > InputManager's 5ms debounce
    // transition the held bit through lastDebounceTime into currentState
    // without setting pressedEvents, so the first loop()'s own gpio.update()
    // sees state == currentState and emits nothing.
    gpio.update();
    delay(10);
    gpio.update();
  }

  // Ensure we're not still holding the power button before leaving setup
  bootTiming.dispatchDoneMs = millis();
  waitForPowerRelease();
  bootTiming.powerReleaseWaitMs = millis() - bootTiming.dispatchDoneMs;
  bootTiming.setupDoneMs = millis();
  allowSleepAt = millis() + 2000;
}

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;

  mappedInputManager.update();
  halTiltSensor.update(SETTINGS.tiltPageTurn, SETTINGS.tiltPageTurnDirection, SETTINGS.orientation,
                       activityManager.isReaderActivity());

  renderer.setFadingFix(SETTINGS.fadingFix);
  renderer.setTextDarkness(SETTINGS.textDarkness);

  if (Serial && millis() - lastMemPrint >= 10000) {
    LOG_INF("MEM", "Free: %d bytes, Total: %d bytes, Min Free: %d bytes, MaxAlloc: %d bytes, RenderStackHW: %d bytes",
            ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(),
            activityManager.renderTaskStackHighWaterBytes());
    lastMemPrint = millis();
  }

  // Stores deferred off the boot-to-visible path; loaded on the first loop
  // pass while the render task paints the first frame.
  static bool deferredStoreLoadsDone = false;
  if (!deferredStoreLoadsDone) {
    deferredStoreLoadsDone = true;
    FAVORITES.loadFromFile();
    ACHIEVEMENT_STORE.begin();
    KOREADER_STORE.loadFromFile();
    OPDS_STORE.loadFromFile();
  }

  // Repair13 groundwork: persist the boot-phase breadcrumbs once, well after
  // first paint, so measuring wake latency never adds to it.
  if (!bootTiming.written && millis() > 3000) {
    bootTiming.written = true;
    FsFile bootTimingFile;
    if (Storage.openFileForWrite("BOOT", DUET_STATE_ROOT_PATH "/boot_timing.txt", bootTimingFile)) {
      char buf[320];
      const int n =
          snprintf(buf, sizeof(buf),
                   "reset=%d wake=%d serial=%lums gpio=%lums storage=%lums panicChk=%lums settings=%lums gesture=%lums "
                   "stores=%lums panel=%lums displayInit=%lums builtinFonts=%lums sdDiscover=%lums "
                   "sdLoad=%lums dispatch=%lums releaseWait=%lums setupDone=%lums\n",
                   bootTiming.resetReason, bootTiming.wakeupCause, bootTiming.serialReadyMs, bootTiming.gpioReadyMs,
                   bootTiming.storageReadyMs, bootTiming.panicCheckMs, bootTiming.settingsMs, bootTiming.gestureMs,
                   bootTiming.storesMs, bootTiming.panelMs, bootTiming.displayInitMs, bootTiming.builtinFontsMs,
                   bootTiming.sdDiscoverMs, bootTiming.sdLoadMs, bootTiming.dispatchDoneMs,
                   bootTiming.powerReleaseWaitMs, bootTiming.setupDoneMs);
      if (n > 0) bootTimingFile.write(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(n));
      bootTimingFile.close();
    }
  }

  if (UsbSerialFileTransfer::process(activityManager.isHomeActivity()) ==
      UsbSerialFileTransfer::ProcessResult::ScreenshotRequested) {
    const uint32_t bufferSize = display.getBufferSize();
    logSerial.printf("SCREENSHOT_START:%d\n", bufferSize);
    uint8_t* buf = display.getFrameBuffer();
    logSerial.write(buf, bufferSize);
    logSerial.printf("SCREENSHOT_END\n");
  }

  // Check for any user activity (button press or release) or active background work
  static unsigned long lastActivityTime = millis();
  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || halTiltSensor.hadActivity() ||
      activityManager.preventAutoSleep()) {
    lastActivityTime = millis();         // Reset inactivity timer
    powerManager.setPowerSaving(false);  // Restore normal CPU frequency on user activity
  }

  static bool screenshotButtonsReleased = true;
  static bool screenshotComboActive = false;
  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.isPressed(HalGPIO::BTN_DOWN)) {
    mappedInputManager.cancelPowerButtonClicks();
    screenshotComboActive = true;
    if (screenshotButtonsReleased) {
      screenshotButtonsReleased = false;
      screenshotComboHandled = true;
      mappedInputManager.suppressNextPowerConfirmRelease();
      {
        RenderLock lock;
        ScreenshotUtil::takeScreenshot(renderer);
      }
    }
    return;
  }
  if (screenshotComboActive) {
    if (gpio.isPressed(HalGPIO::BTN_POWER)) return;
    if (gpio.wasReleased(HalGPIO::BTN_POWER)) {
      mappedInputManager.cancelPowerButtonClicks();
      screenshotButtonsReleased = true;
      screenshotComboActive = false;
      return;
    }
    screenshotButtonsReleased = true;
    screenshotComboActive = false;
  }

#ifdef SIMULATOR
  if (gpio.consumeSimulatorSleepRequest()) {
    enterDeepSleep();
    lastActivityTime = millis();
    return;
  }
#endif

  const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();
  if (sleepTimeoutMs > 0 && millis() - lastActivityTime >= sleepTimeoutMs) {
    LOG_DBG("SLP", "Auto-sleep triggered after %lu ms of inactivity", sleepTimeoutMs);
    enterDeepSleep(true);
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    // In the simulator, deep sleep is a no-op and returns — reset the timer so
    // the main loop does not immediately re-trigger auto-sleep.
    lastActivityTime = millis();
    return;
  }

  if (millis() >= allowSleepAt && handleGlobalPowerButtonAction(getPowerButtonAction())) {
    lastActivityTime = millis();
    return;
  }

  // Refresh the battery icon when USB is plugged or unplugged.
  // Placed after sleep guards so we never queue a render that won't be processed.
  if (gpio.wasUsbStateChanged()) {
    activityManager.requestUpdate();
  }

  const unsigned long activityStartTime = millis();
  activityManager.loop();
  const unsigned long activityDuration = millis() - activityStartTime;

#ifdef SIMULATOR
  runSimulatorSmokeTestTick();
#endif

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      LOG_DBG("LOOP", "New max loop duration: %lu ms (activity: %lu ms)", maxLoopDuration, activityDuration);
      (void)activityDuration;
    }
  }

  // Add delay at the end of the loop to prevent tight spinning
  // When an activity requests skip loop delay (e.g., webserver running), use yield() for faster response
  // Otherwise, use longer delay to save power
  if (activityManager.skipLoopDelay()) {
    powerManager.setPowerSaving(false);  // Make sure we're at full performance when skipLoopDelay is requested
    yield();                             // Give FreeRTOS a chance to run tasks, but return immediately
  } else {
    if (millis() - lastActivityTime >= HalPowerManager::IDLE_POWER_SAVING_MS) {
      // If we've been inactive for a while, increase the delay to save power
      powerManager.setPowerSaving(true);  // Lower CPU frequency after extended inactivity
      delay(50);
    } else {
      // Short delay to prevent tight loop while still being responsive
      delay(10);
    }
  }
}
