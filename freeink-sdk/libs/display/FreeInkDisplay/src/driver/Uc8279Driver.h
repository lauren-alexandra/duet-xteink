#pragma once

// UC8279d panel driver — Xteink X3, newer production run (792x528 B/W).
// UltraChip UC8279d ("d_B" silicon, TFT-module variant), driven in KW mode
// with EXTERNAL/custom LUTs (PSR REG=1): 1-bpp differential, DTM1 (0x10) = OLD
// plane, DTM2 (0x13) = NEW plane.
//
// These modules ship a BLANK MTP (address 0x000 != 0xA5, no factory command
// defaults, no per-temperature waveforms), so the host must drive EVERYTHING:
// PSR, the drive voltages (PWR/VDCS), booster, PLL, the full-panel PTL window
// (used instead of TRES), and the waveform LUT banks. An OTP-mode driver runs
// the panel with no drive rails and leaves it completely dark — the failure
// seen on the first UC8279d field units.
//
// The entire register recipe and every waveform bank were reverse-engineered
// from the stock X3 firmware (update.bin) and live in lut/Uc8279X3Luts.h:
//   - kUc8279X3_Init  : PSR 3F 4A, PTL 792x528 window, PFS 20,
//                       PWR 43 00 78 78 17, VDCS 24, BTST 25 25 3C, PLL 0F, E1 02
//   - kUc8279X3_BwGc  : B/W full (GC) waveform  (5 x 43, command-prefixed)
//   - kUc8279X3_BwDu  : B/W fast (DU) waveform  (5 x 43, command-prefixed)
//   - CDI 0x97 first refresh / 0xD7 later; DU adds E0=02, E5=5A.
// (XTF_AA / XTH4 grayscale banks are also captured there for a later AA path.)
//
// BUSY_N: low while busy (PON/DRF/POF all flag), same two-phase shape as the
// UC8253 X3 — reuses BusyPolarity::X3TwoPhase and the async start/finish split.

#include "PanelDriver.h"

namespace freeink {

class Uc8279Driver : public PanelDriver {
 public:
  Uc8279Driver();

  uint32_t spiHz() const override;
  BusyPolarity busyPolarity() const override { return BusyPolarity::X3TwoPhase; }
  PanelGeometry geometry() const override;

  void begin(EpdBus& bus) override;
  void deepSleep(EpdBus& bus) override;

  void display(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode, bool turnOff) override;
  bool displayStart(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode, bool turnOff) override;
  void displayFinish(EpdBus& bus, const uint8_t* fb) override;
  bool supportsAsyncDisplay() const override { return true; }

  void requestResync(uint8_t settlePasses) override;
  void skipInitialResync() override;

 private:
  void initController(EpdBus& bus);
  // Replay a {cmd, len, data...} register script (kUc8279X3_Init).
  void sendScript(EpdBus& bus, const uint8_t* script, uint16_t len);
  // Load a 5-table waveform bank into LUT registers 0x20-0x24. `prefixed` banks
  // (BW_GC/BW_DU) carry the register id in byte 0; raw banks send 0x20+i first.
  void loadBank(EpdBus& bus, const uint8_t (*bank)[43]);

  uint16_t _w;   // visible width  (792)
  uint16_t _h;   // visible height (528)
  uint16_t _wb;  // width in bytes (99)
  uint32_t _bufferSize;

  bool _isScreenOn = false;
  bool _firstRefresh = true;    // CDI 0x97 on the first refresh after init, 0xD7 after
  bool _oldPlaneValid = false;  // DTM1 holds a real previous frame (differential baseline)
  bool _forceFullSyncNext = false;

  // Async split state (see Uc8253X3Driver for the contract).
  bool _pendingRefresh = false;
  bool _pendingTurnOff = false;
};

PanelDriver& uc8279Driver();

}  // namespace freeink
