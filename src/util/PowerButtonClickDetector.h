#pragma once

#include <cstdint>

class PowerButtonClickDetector {
 public:
  static constexpr uint32_t MULTI_CLICK_WINDOW_MS = 400;

  void update(bool shortRelease, bool powerPressed, bool multiClickEnabled, uint32_t nowMs) {
    finalizedClicks_ = 0;

    if (!multiClickEnabled) {
      pendingClicks_ = 0;
      if (shortRelease) finalizedClicks_ = 1;
      return;
    }

    if (shortRelease) {
      if (pendingClicks_ < 3) ++pendingClicks_;
      lastReleaseMs_ = nowMs;
      if (pendingClicks_ == 3) finalize();
      return;
    }

    if (pendingClicks_ > 0 && !powerPressed && nowMs - lastReleaseMs_ >= MULTI_CLICK_WINDOW_MS) {
      finalize();
    }
  }

  void cancel() {
    pendingClicks_ = 0;
    finalizedClicks_ = 0;
  }

  uint8_t finalizedClicks() const { return finalizedClicks_; }
  bool hasPendingClicks() const { return pendingClicks_ > 0; }

 private:
  void finalize() {
    finalizedClicks_ = pendingClicks_;
    pendingClicks_ = 0;
  }

  uint8_t pendingClicks_ = 0;
  uint8_t finalizedClicks_ = 0;
  uint32_t lastReleaseMs_ = 0;
};
