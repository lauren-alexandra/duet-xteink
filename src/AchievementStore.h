#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "activities/apps/AchievementCatalog.h"

class AchievementStore {
 public:
  static AchievementStore& getInstance() { return instance; }

  // Loads existing state, or silently adopts all milestones already earned.
  bool begin();
  bool reloadFromDisk();
  bool mergeFromPath(const char* path);
  bool refresh(bool queuePopup);
  bool refreshLightweight(bool queuePopup);
  bool reconcile(std::vector<AchievementView>& views, bool queuePopup);
  void applyPersistedUnlocks(std::vector<AchievementView>& views);

 private:
  static AchievementStore instance;
  static constexpr size_t METRIC_COUNT = static_cast<size_t>(AchievementMetric::Count);

  std::array<uint64_t, METRIC_COUNT> highestUnlockedTarget{};
  bool loaded = false;

  static bool readTargetsFromPath(const char* path, std::array<uint64_t, METRIC_COUNT>& targets);
  bool loadFromPath(const char* path);
  bool save(bool rotateBackup = true) const;
  bool adoptCurrentMilestones();
  void queueUnlockPopup(const std::vector<AchievementView>& newlyUnlocked) const;
};

#define ACHIEVEMENT_STORE AchievementStore::getInstance()
