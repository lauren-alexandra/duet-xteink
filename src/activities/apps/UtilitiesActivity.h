#pragma once

#include "LauncherLayoutStore.h"
#include "activities/Activity.h"
#include "activities/reader/BookStatsActivity.h"
#include "util/ButtonNavigator.h"

class UtilitiesActivity final : public Activity {
 public:
  UtilitiesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Apps", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;

  int itemCount() const;
  LauncherItem selectedItem() const;
  void openSelected(LauncherItem item);
  void openStats(BookStatsActivity::InitialPage initialPage);
};
