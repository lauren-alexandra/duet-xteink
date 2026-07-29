#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class ReadMeActivity final : public Activity {
 public:
  static constexpr int PAGE_COUNT = 5;

  ReadMeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadMe", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  int page = 0;
  ButtonNavigator buttonNavigator;
};
