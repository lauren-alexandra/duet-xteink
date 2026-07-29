#include "BootActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <I18n.h>

#include "AppVersion.h"
#include "fontIds.h"
#include "images/Logo160.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Keep the established X3 boot presentation. On X4, reserve the expensive
  // black/white scrub for an actual firmware-flash boot: ordinary deep-sleep
  // wake also enters BootActivity, and running three X4 full waveforms there
  // caused the long flash sequence seen on the physical reader.
  const bool runPreSplashScrub =
      gpio.deviceIsX3() || forcePanelScrub || gpio.getWakeupReason() == HalGPIO::WakeupReason::AfterFlash;
  if (runPreSplashScrub) {
    renderer.clearScreen(0x00);
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    renderer.clearScreen();
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  }

  renderer.clearScreen();
  renderer.drawImage(Logo160, (pageWidth - 160) / 2, (pageHeight - 160) / 2 - 10, 160, 160);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 92, tr(STR_DUET), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 117, tr(STR_BOOTING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, DUET_VERSION);
  // Full waveform: the splash follows arbitrary screens (firmware updater,
  // crash report) and a fast paint leaves them ghosting behind the mark for
  // the whole splash hold.
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
}
