#pragma once
#include <vector>

#include "../Activity.h"
#include "CrossPointState.h"
#include "util/ButtonNavigator.h"

class AlertActivity final : public Activity {
  std::string title;
  std::string body;
  std::vector<std::string> bodyLines;
  bool goHomeOnBack = false;
  bool waitForConfirmRelease = false;
  PendingAlertAction action = PendingAlertAction::None;
  int firstVisibleLine = 0;
  int visibleLineCount = 1;
  ButtonNavigator buttonNavigator;

 public:
  explicit AlertActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Alert", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
