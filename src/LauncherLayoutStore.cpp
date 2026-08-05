#include "LauncherLayoutStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <string>

namespace {
constexpr uint32_t LAUNCHER_MAGIC = 0x48434E4Cu;  // LNCH, little-endian on disk
constexpr uint8_t LAUNCHER_VERSION = 1;
constexpr char LAUNCHER_PATH[] = DUET_STATE_ROOT_PATH "/launcher_layout.bin";
constexpr char LAUNCHER_BACKUP_PATH[] = DUET_STATE_ROOT_PATH "/launcher_layout.bin.bak";

template <typename T>
bool readPod(FsFile& file, T& value) {
  return file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T)) == static_cast<int>(sizeof(T));
}

template <typename T>
bool writePod(FsFile& file, const T& value) {
  return file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T)) == sizeof(T);
}
}  // namespace

LauncherLayoutStore LauncherLayoutStore::instance;

bool LauncherLayoutStore::allowedOnSurface(const LauncherItem item, const LauncherSurface surface) {
  if (item >= LauncherItem::Count) return false;
  if (surface == LauncherSurface::Apps && item == LauncherItem::Apps) return false;
  if (surface == LauncherSurface::Home && item == LauncherItem::CustomizeHomeApps) return false;
  return true;
}

size_t LauncherLayoutStore::count(const LauncherSurface surface) const {
  return surface == LauncherSurface::Home ? homeCount : appCount;
}

LauncherItem LauncherLayoutStore::itemAt(const LauncherSurface surface, const size_t index) const {
  const auto& items = surface == LauncherSurface::Home ? homeItems : appItems;
  return index < count(surface) ? items[index] : LauncherItem::Count;
}

bool LauncherLayoutStore::contains(const LauncherSurface surface, const LauncherItem item) const {
  const auto& items = surface == LauncherSurface::Home ? homeItems : appItems;
  const size_t itemCount = count(surface);
  return std::find(items.begin(), items.begin() + itemCount, item) != items.begin() + itemCount;
}

void LauncherLayoutStore::remove(const LauncherSurface surface, const LauncherItem item) {
  auto& items = surface == LauncherSurface::Home ? homeItems : appItems;
  uint8_t& itemCount = surface == LauncherSurface::Home ? homeCount : appCount;
  const auto found = std::find(items.begin(), items.begin() + itemCount, item);
  if (found == items.begin() + itemCount) return;
  std::move(found + 1, items.begin() + itemCount, found);
  --itemCount;
}

void LauncherLayoutStore::append(const LauncherSurface surface, const LauncherItem item) {
  if (!allowedOnSurface(item, surface) || contains(surface, item)) return;
  auto& items = surface == LauncherSurface::Home ? homeItems : appItems;
  uint8_t& itemCount = surface == LauncherSurface::Home ? homeCount : appCount;
  if (itemCount < ITEM_COUNT) items[itemCount++] = item;
}

uint8_t LauncherLayoutStore::placement(const LauncherItem item) const {
  uint8_t result = LAUNCHER_HIDDEN;
  if (contains(LauncherSurface::Home, item)) result |= LAUNCHER_HOME;
  if (contains(LauncherSurface::Apps, item)) result |= LAUNCHER_APPS;
  return result;
}

bool LauncherLayoutStore::isRequired(const LauncherItem item) const {
  return item == LauncherItem::Apps || item == LauncherItem::CustomizeHomeApps || item == LauncherItem::Settings;
}

void LauncherLayoutStore::normalize() {
  auto normalizeSurface = [this](const LauncherSurface surface) {
    auto& items = surface == LauncherSurface::Home ? homeItems : appItems;
    uint8_t& itemCount = surface == LauncherSurface::Home ? homeCount : appCount;
    itemCount = std::min<uint8_t>(itemCount, ITEM_COUNT);
    uint8_t writeIndex = 0;
    for (uint8_t readIndex = 0; readIndex < itemCount; ++readIndex) {
      const LauncherItem item = items[readIndex];
      if (!allowedOnSurface(item, surface)) continue;
      if (std::find(items.begin(), items.begin() + writeIndex, item) != items.begin() + writeIndex) continue;
      items[writeIndex++] = item;
    }
    itemCount = writeIndex;
  };

  normalizeSurface(LauncherSurface::Home);
  normalizeSurface(LauncherSurface::Apps);
  // The compact Home popout is for immediate reading actions and recovery
  // information. Settings and file browsing stay one level deeper under Apps.
  remove(LauncherSurface::Home, LauncherItem::BrowseFiles);
  remove(LauncherSurface::Home, LauncherItem::Settings);
  remove(LauncherSurface::Home, LauncherItem::Apps);
  // Settings is the recovery/configuration entry point. Keep it first inside
  // Apps so a fresh or partially configured reader can reach it immediately.
  remove(LauncherSurface::Apps, LauncherItem::Settings);
  // Progress sync is a top-level recovery action. Keep KOReader account setup
  // in Apps, but make the nearby stats exchange available from Home.
  remove(LauncherSurface::Home, LauncherItem::KOReaderSync);
  remove(LauncherSurface::Home, LauncherItem::NearbyStatsSync);
  remove(LauncherSurface::Home, LauncherItem::IfFound);
  append(LauncherSurface::Home, LauncherItem::NearbyStatsSync);
  append(LauncherSurface::Home, LauncherItem::IfFound);
  append(LauncherSurface::Home, LauncherItem::Apps);
  append(LauncherSurface::Apps, LauncherItem::NearbyStatsSync);
  append(LauncherSurface::Apps, LauncherItem::KOReaderSync);
  append(LauncherSurface::Apps, LauncherItem::CustomizeHomeApps);
  if (appCount < ITEM_COUNT) {
    std::move_backward(appItems.begin(), appItems.begin() + appCount, appItems.begin() + appCount + 1);
    appItems[0] = LauncherItem::Settings;
    ++appCount;
  }
}

bool LauncherLayoutStore::resetDefaults(const bool save) {
  homeCount = 0;
  appCount = 0;

  constexpr LauncherItem defaultHome[] = {
      LauncherItem::SearchLibrary,   LauncherItem::RecentBooks, LauncherItem::ReadingStats,
      LauncherItem::SavedItems,      LauncherItem::Favorites,   LauncherItem::Sleep,
      LauncherItem::NearbyStatsSync, LauncherItem::IfFound,     LauncherItem::Apps,
  };
  constexpr LauncherItem defaultApps[] = {
      LauncherItem::Settings,       LauncherItem::BrowseFiles,  LauncherItem::SearchLibrary,
      LauncherItem::RecentBooks,    LauncherItem::ReadingStats, LauncherItem::ReadingHeatmap,
      LauncherItem::ReadingProfile, LauncherItem::SavedItems,   LauncherItem::Favorites,
      LauncherItem::Achievements,   LauncherItem::Dictionary,   LauncherItem::Tetris,
      LauncherItem::IfFound,        LauncherItem::ScreenClean,  LauncherItem::NearbyStatsSync,
      LauncherItem::FileTransfer,   LauncherItem::OpdsBrowser,  LauncherItem::KOReaderSync,
      LauncherItem::Sleep,          LauncherItem::ReadMe,       LauncherItem::CustomizeHomeApps,
  };
  for (const auto item : defaultHome) append(LauncherSurface::Home, item);
  for (const auto item : defaultApps) append(LauncherSurface::Apps, item);
  normalize();
  return !save || saveToFile();
}

bool LauncherLayoutStore::setPlacement(const LauncherItem item, uint8_t newPlacement) {
  if (item >= LauncherItem::Count) return false;
  if (item == LauncherItem::BrowseFiles) newPlacement = LAUNCHER_APPS;
  if (item == LauncherItem::Apps) newPlacement = LAUNCHER_HOME;
  if (item == LauncherItem::CustomizeHomeApps) newPlacement = LAUNCHER_APPS;
  if (item == LauncherItem::Settings) newPlacement = LAUNCHER_APPS;
  if (item == LauncherItem::KOReaderSync) newPlacement = LAUNCHER_APPS;

  remove(LauncherSurface::Home, item);
  remove(LauncherSurface::Apps, item);
  if ((newPlacement & LAUNCHER_HOME) != 0) append(LauncherSurface::Home, item);
  if ((newPlacement & LAUNCHER_APPS) != 0) append(LauncherSurface::Apps, item);
  normalize();
  return saveToFile();
}

bool LauncherLayoutStore::move(const LauncherItem item, const LauncherSurface surface, const int delta) {
  auto& items = surface == LauncherSurface::Home ? homeItems : appItems;
  const size_t itemCount = count(surface);
  const auto found = std::find(items.begin(), items.begin() + itemCount, item);
  if (found == items.begin() + itemCount || delta == 0) return false;
  const int current = static_cast<int>(found - items.begin());
  const int target = std::clamp(current + delta, 0, static_cast<int>(itemCount) - 1);
  if (target == current) return false;
  const LauncherItem moving = items[current];
  if (target < current) {
    std::move_backward(items.begin() + target, items.begin() + current, items.begin() + current + 1);
  } else {
    std::move(items.begin() + current + 1, items.begin() + target + 1, items.begin() + current);
  }
  items[target] = moving;
  return saveToFile();
}

bool LauncherLayoutStore::loadFromPath(const char* path) {
  FsFile file;
  if (!Storage.openFileForRead("LNCH", path, file)) return false;

  uint32_t magic = 0;
  uint8_t version = 0;
  uint8_t storedItemCount = 0;
  uint8_t loadedHomeCount = 0;
  uint8_t loadedAppCount = 0;
  bool ok = readPod(file, magic) && readPod(file, version) && readPod(file, storedItemCount) &&
            readPod(file, loadedHomeCount) && readPod(file, loadedAppCount) && magic == LAUNCHER_MAGIC &&
            version == LAUNCHER_VERSION && storedItemCount == ITEM_COUNT && loadedHomeCount <= ITEM_COUNT &&
            loadedAppCount <= ITEM_COUNT;

  homeCount = loadedHomeCount;
  appCount = loadedAppCount;
  for (uint8_t i = 0; ok && i < homeCount; ++i) {
    uint8_t raw = 0;
    ok = readPod(file, raw) && raw < static_cast<uint8_t>(LauncherItem::Count);
    homeItems[i] = static_cast<LauncherItem>(raw);
  }
  for (uint8_t i = 0; ok && i < appCount; ++i) {
    uint8_t raw = 0;
    ok = readPod(file, raw) && raw < static_cast<uint8_t>(LauncherItem::Count);
    appItems[i] = static_cast<LauncherItem>(raw);
  }
  file.close();
  if (ok) normalize();
  return ok;
}

bool LauncherLayoutStore::loadFromFile() {
  if (loadFromPath(LAUNCHER_PATH)) return true;
  if (loadFromPath(LAUNCHER_BACKUP_PATH)) {
    LOG_INF("LNCH", "Recovered launcher layout from backup");
    return saveToFile();
  }
  LOG_INF("LNCH", "Using default launcher layout");
  return resetDefaults(true);
}

bool LauncherLayoutStore::saveToFile() const {
  Storage.mkdir(DUET_STATE_ROOT_PATH "");
  const std::string tempPath = std::string(LAUNCHER_PATH) + ".tmp";
  if (Storage.exists(tempPath.c_str())) Storage.remove(tempPath.c_str());

  FsFile file;
  if (!Storage.openFileForWrite("LNCH", tempPath.c_str(), file)) return false;
  const uint8_t itemCount = static_cast<uint8_t>(ITEM_COUNT);
  bool ok = writePod(file, LAUNCHER_MAGIC) && writePod(file, LAUNCHER_VERSION) && writePod(file, itemCount) &&
            writePod(file, homeCount) && writePod(file, appCount);
  for (uint8_t i = 0; ok && i < homeCount; ++i) ok = writePod(file, static_cast<uint8_t>(homeItems[i]));
  for (uint8_t i = 0; ok && i < appCount; ++i) ok = writePod(file, static_cast<uint8_t>(appItems[i]));
  if (ok) {
    file.flush();
    ok = file.sync();
  }
  const bool closed = file.close();
  if (!ok || !closed) {
    Storage.remove(tempPath.c_str());
    return false;
  }

  if (Storage.exists(LAUNCHER_BACKUP_PATH)) Storage.remove(LAUNCHER_BACKUP_PATH);
  if (Storage.exists(LAUNCHER_PATH) && !Storage.rename(LAUNCHER_PATH, LAUNCHER_BACKUP_PATH)) {
    Storage.remove(tempPath.c_str());
    return false;
  }
  if (!Storage.rename(tempPath.c_str(), LAUNCHER_PATH)) {
    if (Storage.exists(LAUNCHER_BACKUP_PATH) && !Storage.exists(LAUNCHER_PATH)) {
      Storage.rename(LAUNCHER_BACKUP_PATH, LAUNCHER_PATH);
    }
    Storage.remove(tempPath.c_str());
    return false;
  }
  return true;
}
