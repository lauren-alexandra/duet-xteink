#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

enum class LauncherItem : uint8_t {
  BrowseFiles = 0,
  SearchLibrary,
  RecentBooks,
  ReadingStats,
  ReadingHeatmap,
  ReadingProfile,
  SavedItems,
  Favorites,
  Achievements,
  Dictionary,
  Tetris,
  IfFound,
  ScreenClean,
  NearbyStatsSync,
  FileTransfer,
  OpdsBrowser,
  KOReaderSync,
  Sleep,
  ReadMe,
  Apps,
  CustomizeHomeApps,
  Settings,
  Count
};

enum class LauncherSurface : uint8_t { Home = 0, Apps = 1 };

enum LauncherPlacement : uint8_t {
  LAUNCHER_HIDDEN = 0,
  LAUNCHER_HOME = 1 << 0,
  LAUNCHER_APPS = 1 << 1,
  LAUNCHER_BOTH = LAUNCHER_HOME | LAUNCHER_APPS,
};

class LauncherLayoutStore {
 public:
  static constexpr size_t ITEM_COUNT = static_cast<size_t>(LauncherItem::Count);

  static LauncherLayoutStore& getInstance() { return instance; }

  bool loadFromFile();
  bool saveToFile() const;
  bool resetDefaults(bool save = true);

  size_t count(LauncherSurface surface) const;
  LauncherItem itemAt(LauncherSurface surface, size_t index) const;
  uint8_t placement(LauncherItem item) const;
  bool setPlacement(LauncherItem item, uint8_t placement);
  bool move(LauncherItem item, LauncherSurface surface, int delta);
  bool isRequired(LauncherItem item) const;

 private:
  static LauncherLayoutStore instance;

  std::array<LauncherItem, ITEM_COUNT> homeItems{};
  std::array<LauncherItem, ITEM_COUNT> appItems{};
  uint8_t homeCount = 0;
  uint8_t appCount = 0;

  bool loadFromPath(const char* path);
  void normalize();
  bool contains(LauncherSurface surface, LauncherItem item) const;
  void remove(LauncherSurface surface, LauncherItem item);
  void append(LauncherSurface surface, LauncherItem item);
  static bool allowedOnSurface(LauncherItem item, LauncherSurface surface);
};

#define LAUNCHER_LAYOUT LauncherLayoutStore::getInstance()
