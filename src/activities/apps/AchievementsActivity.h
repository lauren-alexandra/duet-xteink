#pragma once

#include <cstdint>
#include <vector>

#include "AchievementCatalog.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class AchievementsActivity final : public Activity {
 public:
  AchievementsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Achievements", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Tab : uint8_t { Pending = 0, Completed = 1 };

  AchievementSnapshot snapshot;
  std::vector<AchievementView> achievements;
  std::vector<int> visibleIndexes;
  Tab selectedTab = Tab::Pending;
  int selectedIndex = 0;
  bool waitForConfirmRelease = false;
  bool fullSnapshotPending = false;
  bool lightweightFramePainted = false;
  uint32_t fullSnapshotNotBefore = 0;
  ButtonNavigator buttonNavigator;

  bool hasActiveInput() const;
  void rebuildVisibleIndexes(bool allowFallback = true);
  void toggleTab();
};
