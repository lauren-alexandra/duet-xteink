#include "Uc8279Driver.h"

#include <Arduino.h>
#include <BoardConfig.h>

#include "../lut/Uc8279X3Luts.h"

namespace freeink {
namespace {
// UC8279d command set (UC8279d_B 0.1 datasheet + stock-firmware RE).
constexpr uint8_t CMD_PANEL_SETTING = 0x00;       // PSR
constexpr uint8_t CMD_POWER_OFF = 0x02;           // POF
constexpr uint8_t CMD_POWER_ON = 0x04;            // PON
constexpr uint8_t CMD_DEEP_SLEEP = 0x07;          // DSLP (check code 0xA5)
constexpr uint8_t CMD_DTM1 = 0x10;                // OLD plane in KW mode
constexpr uint8_t CMD_DATA_STOP = 0x11;           // DSP
constexpr uint8_t CMD_DISPLAY_REFRESH = 0x12;     // DRF
constexpr uint8_t CMD_DTM2 = 0x13;                // NEW plane in KW mode
constexpr uint8_t CMD_VCOM_DATA_INTERVAL = 0x50;  // CDI
constexpr uint8_t CMD_PARTIAL_IN = 0x91;          // PTIN
constexpr uint8_t CMD_PARTIAL_OUT = 0x92;         // PTOUT
constexpr uint8_t CMD_CCSET = 0xE0;               // CCSET (DU: 0x02)
constexpr uint8_t CMD_TSSET = 0xE5;               // TSSET (DU: 0x5A)
}  // namespace

Uc8279Driver::Uc8279Driver()
    : _w(BoardConfig::ACTIVE.displayWidth),
      _h(BoardConfig::ACTIVE.displayHeight),
      _wb(BoardConfig::ACTIVE.displayWidth / 8),
      _bufferSize(static_cast<uint32_t>(BoardConfig::ACTIVE.displayWidth / 8) * BoardConfig::ACTIVE.displayHeight) {}

uint32_t Uc8279Driver::spiHz() const {
  // UC8279 serial write timing is rated to 20 MHz, same as UC8253.
  return BoardConfig::ACTIVE.displaySpiHz != 0 ? BoardConfig::ACTIVE.displaySpiHz : 16000000;
}

PanelGeometry Uc8279Driver::geometry() const { return {_w, _h, _wb, _bufferSize}; }

void Uc8279Driver::sendScript(EpdBus& bus, const uint8_t* script, uint16_t len) {
  uint16_t i = 0;
  while (i < len) {
    const uint8_t cmd = script[i++];
    const uint8_t n = script[i++];
    bus.cmd(cmd);
    for (uint8_t k = 0; k < n; k++) bus.data(script[i++]);
  }
}

void Uc8279Driver::loadBank(EpdBus& bus, const uint8_t (*bank)[43]) {
  // Command-prefixed banks (BW_GC / BW_DU): byte 0 is the LUT register
  // (0x20-0x24), the remaining 42 bytes are its data.
  for (int t = 0; t < 5; t++) {
    bus.cmd(bank[t][0]);
    bus.data(&bank[t][1], 42);
  }
}

// The stock init (FUN_42014ad4): a blank-MTP module needs the full register
// bring-up. PSR 0x3F sets REG=1 (external LUT); the PTL window defines the
// active 792x528 area in place of TRES; PWR/VDCS supply the drive rails without
// which nothing develops. No plane seed here — the first refresh writes both.
void Uc8279Driver::initController(EpdBus& bus) {
  sendScript(bus, kUc8279X3_Init, sizeof(kUc8279X3_Init));
  _isScreenOn = false;
  _firstRefresh = true;
  _oldPlaneValid = false;
}

void Uc8279Driver::begin(EpdBus& bus) {
  bus.reset(50);
  _forceFullSyncNext = false;
  initController(bus);
}

void Uc8279Driver::display(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode, bool turnOff) {
  displayStart(bus, fb, prev, mode, turnOff);
  displayFinish(bus, fb);
}

bool Uc8279Driver::displayStart(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode, bool turnOff) {
  (void)prev;  // single-buffer: DTM1 holds the previous frame from displayFinish()'s sync
  // Full (GC) on an explicit Full request or a forced/first refresh; otherwise a
  // differential fast (DU) against the OLD plane synced last time.
  const bool fast = (mode != RefreshMode::Full) && _oldPlaneValid && !_forceFullSyncNext;

  bus.cmd(CMD_PARTIAL_IN);  // enter the full-panel PTL window set in init

  // KW planes: OLD (0x10) + NEW (0x13). Full seeds OLD white (absolute drive);
  // fast diffs against the previous frame already in OLD RAM.
  if (!fast) {
    bus.fillPlane(CMD_DTM1, 0xFF, _h, _wb);
    bus.cmd(CMD_DATA_STOP);
  }
  bus.sendPlaneFlipped(CMD_DTM2, fb, _h, _wb);
  bus.cmd(CMD_DATA_STOP);

  // Refresh setup (RE order): CDI, then DU-only E0/E5, then the waveform bank.
  bus.cmd(CMD_VCOM_DATA_INTERVAL);
  bus.data(_firstRefresh ? kUc8279X3_CdiFirst : kUc8279X3_CdiLater);
  if (fast) {
    bus.cmd(CMD_CCSET);
    bus.data(kUc8279X3_DuE0);
    bus.cmd(CMD_TSSET);
    bus.data(kUc8279X3_DuE5);
  }
  loadBank(bus, fast ? kUc8279X3_BwDu : kUc8279X3_BwGc);

  if (!_isScreenOn) {
    bus.cmd(CMD_POWER_ON);
    bus.waitBusy(" 8279_PON");
    _isScreenOn = true;
  }
  bus.cmd(CMD_DISPLAY_REFRESH);
  // Confirm the waveform started (BUSY_N dropped LOW) before returning so
  // displayFinish() only rides out the completion edge.
  {
    const int8_t busyPin = bus.pins().busy;
    const unsigned long t0 = millis();
    while (digitalRead(busyPin) == HIGH && millis() - t0 < 50) delay(1);
  }
  _pendingTurnOff = turnOff;
  _pendingRefresh = true;
  return true;
}

void Uc8279Driver::displayFinish(EpdBus& bus, const uint8_t* fb) {
  if (!_pendingRefresh) return;
  _pendingRefresh = false;

  bus.waitRefreshComplete(" 8279_DRF");
  bus.cmd(CMD_VCOM_DATA_INTERVAL);  // restore the later-refresh CDI (border hold)
  bus.data(kUc8279X3_CdiLater);
  bus.cmd(CMD_PARTIAL_OUT);

  // Sync the OLD plane with the just-displayed frame so the next fast refresh
  // diffs against it (KW clears erased pixels -> no ghosting).
  bus.sendPlaneFlipped(CMD_DTM1, fb, _h, _wb);
  bus.cmd(CMD_DATA_STOP);
  _oldPlaneValid = true;
  _firstRefresh = false;
  _forceFullSyncNext = false;

  if (_pendingTurnOff) {
    bus.cmd(CMD_POWER_OFF);
    bus.waitBusy(" 8279_POF");
    _isScreenOn = false;
  }
}

void Uc8279Driver::requestResync(uint8_t settlePasses) {
  (void)settlePasses;
  _forceFullSyncNext = true;  // next refresh is a full GC flash from white
}

void Uc8279Driver::skipInitialResync() { _oldPlaneValid = true; }

void Uc8279Driver::deepSleep(EpdBus& bus) {
  if (_isScreenOn) {
    bus.cmd(CMD_POWER_OFF);
    bus.waitBusy(" 8279 power-down");
    _isScreenOn = false;
  }
  bus.cmd(CMD_DEEP_SLEEP);
  bus.data(0xA5);
}

PanelDriver& uc8279Driver() {
  static Uc8279Driver instance;
  return instance;
}

}  // namespace freeink
