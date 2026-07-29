#pragma once

#include "activities/Activity.h"

class BackupStatsActivity final : public Activity {
 public:
  explicit BackupStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BackupStats", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }
  void render(RenderLock&&) override;

 private:
  enum State { WARNING, SUCCESS, FAILED };

  State state = WARNING;
  char backupFileName[80] = {};
  uint16_t backupFileCount = 0;
  uint32_t backupDataBytes = 0;

  void goBack() { finish(); }
  void runBackup();
};
