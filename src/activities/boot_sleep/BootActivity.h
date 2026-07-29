#pragma once
#include "activities/Activity.h"

class BootActivity final : public Activity {
 private:
  const bool forcePanelScrub;

 public:
  explicit BootActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool forcePanelScrub = false)
      : Activity("Boot", renderer, mappedInput), forcePanelScrub(forcePanelScrub) {}
  void onEnter() override;
};
