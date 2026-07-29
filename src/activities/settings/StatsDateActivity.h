#pragma once

#include "activities/Activity.h"
#include "activities/reader/ReadingStatsUtils.h"
#include "util/ButtonNavigator.h"

class StatsDateActivity final : public Activity {
 public:
  StatsDateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("StatsDate", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator navigator{180, 450};
  ReadingStatsDate date;
  int selectedField = 0;
  bool saveFailed = false;

  void adjustSelectedField(int delta);
  void advanceOrSave();
};

