#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class RestoreStatsActivity final : public Activity {
 public:
  RestoreStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("RestoreStats", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return true; }

 private:
  enum class State : uint8_t { Select, Confirm, Restoring, Success, Failed, Empty };

  State state = State::Select;
  std::vector<std::string> archivePaths;
  std::vector<std::string> archiveLabels;
  ButtonNavigator navigator;
  int selectedIndex = 0;
  char safetyFileName[80] = {};
  uint16_t restoredFileCount = 0;

  void runRestore();
  const char* selectedLabel() const;
};
