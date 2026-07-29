#include "AchievementStore.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "CrossPointSettings.h"
#include "CrossPointState.h"

namespace {
constexpr uint32_t ACHIEVEMENT_MAGIC = 0x56484341u;  // ACHV, little-endian on disk
constexpr uint8_t ACHIEVEMENT_VERSION = 1;
constexpr char ACHIEVEMENT_PATH[] = DUET_STATE_ROOT_PATH "/achievements.bin";
constexpr char ACHIEVEMENT_BACKUP_PATH[] = DUET_STATE_ROOT_PATH "/achievements.bin.bak";
constexpr uint16_t ACHIEVEMENT_FILE_SIZE =
    8 + static_cast<uint16_t>(static_cast<size_t>(AchievementMetric::Count) * sizeof(uint64_t));

template <typename T>
bool readPod(FsFile& file, T& value) {
  return file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T)) == static_cast<int>(sizeof(T));
}

template <typename T>
bool writePod(FsFile& file, const T& value) {
  return file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T)) == sizeof(T);
}

bool verifyFileSize(const char* path) {
  FsFile file;
  if (!Storage.openFileForRead("ACHV", path, file)) return false;
  const bool valid = file.fileSize() == ACHIEVEMENT_FILE_SIZE;
  file.close();
  return valid;
}
}  // namespace

AchievementStore AchievementStore::instance;

bool AchievementStore::readTargetsFromPath(const char* path, std::array<uint64_t, METRIC_COUNT>& targets) {
  FsFile file;
  if (!Storage.openFileForRead("ACHV", path, file)) return false;

  std::array<uint64_t, METRIC_COUNT> loadedTargets{};
  uint32_t magic = 0;
  uint8_t version = 0;
  uint8_t metricCount = 0;
  uint16_t fileSize = 0;
  bool ok = file.fileSize() == ACHIEVEMENT_FILE_SIZE && readPod(file, magic) && readPod(file, version) &&
            readPod(file, metricCount) && readPod(file, fileSize) && magic == ACHIEVEMENT_MAGIC &&
            version == ACHIEVEMENT_VERSION && metricCount == METRIC_COUNT && fileSize == ACHIEVEMENT_FILE_SIZE;
  for (auto& target : loadedTargets) ok = ok && readPod(file, target);
  file.close();
  if (ok) targets = loadedTargets;
  return ok;
}

bool AchievementStore::loadFromPath(const char* path) { return readTargetsFromPath(path, highestUnlockedTarget); }

bool AchievementStore::save(const bool rotateBackup) const {
  Storage.mkdir(DUET_STATE_ROOT_PATH "");
  const std::string tempPath = std::string(ACHIEVEMENT_PATH) + ".tmp";
  if (Storage.exists(tempPath.c_str()) && !Storage.remove(tempPath.c_str())) return false;

  FsFile file;
  if (!Storage.openFileForWrite("ACHV", tempPath.c_str(), file)) return false;
  const uint8_t metricCount = static_cast<uint8_t>(METRIC_COUNT);
  bool ok = writePod(file, ACHIEVEMENT_MAGIC) && writePod(file, ACHIEVEMENT_VERSION) && writePod(file, metricCount) &&
            writePod(file, ACHIEVEMENT_FILE_SIZE);
  for (const auto target : highestUnlockedTarget) ok = ok && writePod(file, target);
  if (!ok) {
    file.close();
    Storage.remove(tempPath.c_str());
    return false;
  }

  file.flush();
  const bool synced = file.sync();
  const bool closed = file.close();
  if (!synced || !closed || !verifyFileSize(tempPath.c_str())) {
    Storage.remove(tempPath.c_str());
    return false;
  }

  if (rotateBackup) {
    if (Storage.exists(ACHIEVEMENT_BACKUP_PATH)) Storage.remove(ACHIEVEMENT_BACKUP_PATH);
    if (Storage.exists(ACHIEVEMENT_PATH) && !Storage.rename(ACHIEVEMENT_PATH, ACHIEVEMENT_BACKUP_PATH)) {
      Storage.remove(tempPath.c_str());
      return false;
    }
  } else if (Storage.exists(ACHIEVEMENT_PATH) && !Storage.remove(ACHIEVEMENT_PATH)) {
    Storage.remove(tempPath.c_str());
    return false;
  }

  if (!Storage.rename(tempPath.c_str(), ACHIEVEMENT_PATH)) {
    if (rotateBackup && Storage.exists(ACHIEVEMENT_BACKUP_PATH) && !Storage.exists(ACHIEVEMENT_PATH)) {
      Storage.rename(ACHIEVEMENT_BACKUP_PATH, ACHIEVEMENT_PATH);
    }
    Storage.remove(tempPath.c_str());
    return false;
  }
  return true;
}

bool AchievementStore::adoptCurrentMilestones() {
  auto views = AchievementCatalog::buildViews(AchievementCatalog::loadLightweightSnapshot());
  for (const auto& view : views) {
    if (!view.unlocked) continue;
    const size_t metric = static_cast<size_t>(view.metric);
    if (metric < METRIC_COUNT) highestUnlockedTarget[metric] = std::max(highestUnlockedTarget[metric], view.target);
  }
  return save(false);
}

bool AchievementStore::begin() {
  if (loaded) return true;
  highestUnlockedTarget.fill(0);
  if (loadFromPath(ACHIEVEMENT_PATH)) {
    loaded = true;
    return true;
  }
  if (loadFromPath(ACHIEVEMENT_BACKUP_PATH)) {
    loaded = true;
    LOG_INF("ACHV", "Recovered achievement state from backup");
    return save(false);
  }

  loaded = true;
  const bool saved = adoptCurrentMilestones();
  LOG_INF("ACHV", "Initialized achievement state from current reading history");
  return saved;
}

bool AchievementStore::reloadFromDisk() {
  loaded = false;
  return begin();
}

bool AchievementStore::mergeFromPath(const char* path) {
  if (!begin()) return false;
  std::array<uint64_t, METRIC_COUNT> peerTargets{};
  if (!readTargetsFromPath(path, peerTargets)) return false;

  bool changed = false;
  for (size_t i = 0; i < METRIC_COUNT; ++i) {
    if (peerTargets[i] > highestUnlockedTarget[i]) {
      highestUnlockedTarget[i] = peerTargets[i];
      changed = true;
    }
  }
  return !changed || save();
}

void AchievementStore::applyPersistedUnlocks(std::vector<AchievementView>& views) {
  if (!begin()) return;
  for (auto& view : views) {
    const size_t metric = static_cast<size_t>(view.metric);
    if (metric < METRIC_COUNT && view.target <= highestUnlockedTarget[metric]) view.unlocked = true;
  }
}

void AchievementStore::queueUnlockPopup(const std::vector<AchievementView>& newlyUnlocked) const {
  if (newlyUnlocked.empty() || SETTINGS.achievementPopups == 0 ||
      APP_STATE.hasPendingAlert.load(std::memory_order_acquire)) {
    return;
  }

  snprintf(APP_STATE.pendingAlertTitle, sizeof(APP_STATE.pendingAlertTitle), "%s", tr(STR_ACHIEVEMENT_UNLOCKED_TITLE));
  std::string body;
  for (const auto& achievement : newlyUnlocked) {
    if (!body.empty()) body += '\n';
    body += achievementTargetLabel(achievement);
  }
  snprintf(APP_STATE.pendingAlertBody, sizeof(APP_STATE.pendingAlertBody), "%s", body.c_str());
  APP_STATE.pendingAlertGoHomeOnBack.store(false, std::memory_order_relaxed);
  APP_STATE.pendingAlertAction.store(static_cast<uint8_t>(PendingAlertAction::Achievements), std::memory_order_relaxed);
  APP_STATE.hasPendingAlert.store(true, std::memory_order_release);
}

bool AchievementStore::reconcile(std::vector<AchievementView>& views, const bool queuePopup) {
  if (!begin()) return false;

  std::vector<AchievementView> newlyUnlocked;
  bool changed = false;
  for (auto& view : views) {
    const size_t metric = static_cast<size_t>(view.metric);
    if (metric >= METRIC_COUNT) continue;
    if (view.unlocked && view.target > highestUnlockedTarget[metric]) {
      highestUnlockedTarget[metric] = view.target;
      newlyUnlocked.push_back(view);
      changed = true;
    }
    if (view.target <= highestUnlockedTarget[metric]) view.unlocked = true;
  }

  if (changed && !save()) {
    LOG_ERR("ACHV", "Could not save achievement unlock state");
    return false;
  }
  if (changed && queuePopup) queueUnlockPopup(newlyUnlocked);
  return true;
}

bool AchievementStore::refresh(const bool queuePopup) {
  auto views = AchievementCatalog::buildViews(AchievementCatalog::loadSnapshot());
  return reconcile(views, queuePopup);
}

bool AchievementStore::refreshLightweight(const bool queuePopup) {
  auto views = AchievementCatalog::buildViews(AchievementCatalog::loadLightweightSnapshot());
  return reconcile(views, queuePopup);
}
