#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class ScreenCleanActivity final : public Activity {
 public:
  ScreenCleanActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ScreenClean", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return cleaning; }

 private:
  enum class Mode : uint8_t { Quick, Deep };
  enum class Pattern : uint8_t { White, Black, LightGray, DarkGray, Checker, InverseChecker };

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  bool cleaning = false;
  bool completed = false;
  Mode mode = Mode::Quick;
  uint8_t stageIndex = 0;
  unsigned long lastStageRenderedAt = 0;

  void startCleaning(Mode cleanMode);
  void finishCleaning(bool markCompleted);
  int stageCount() const;
  Pattern patternForStage(uint8_t index) const;
  void drawPattern(Pattern pattern) const;
};
