#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class LauncherCustomizeActivity final : public Activity {
 public:
  LauncherCustomizeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("LauncherCustomize", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;

  void cyclePlacement();
  void moveSelected(int delta);
};
