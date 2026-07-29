#include "PurgeOrphanCachesActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <functional>

#include "Epub.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"

namespace {
constexpr int MAX_SCAN_DEPTH = 8;
constexpr uint16_t MAX_DIR_ENTRIES = 4096;
constexpr size_t MAX_SUBDIRS_PER_DIR = 256;
constexpr uint32_t MAX_TOTAL_ENTRIES = 20000;
constexpr size_t PURGE_BATCH_SIZE = 128;
constexpr int MAX_PURGE_PASSES = 48;
constexpr char ATTIC_PATH[] = DUET_BOOKS_ROOT_PATH "/.attic";

void writePurgeHeartbeat(const char* phase, const std::string& detail) {
  FsFile heartbeat;
  if (!Storage.openFileForWrite("PURGE", DUET_STATE_ROOT_PATH "/purge_hb.txt", heartbeat)) return;
  char line[192];
  const int len = snprintf(line, sizeof(line), "phase=%s ms=%lu detail=%s\n", phase, millis(), detail.c_str());
  if (len > 0) heartbeat.write(reinterpret_cast<const uint8_t*>(line), static_cast<size_t>(len));
  heartbeat.close();
}

uint64_t fnv64(const std::string& value) {
  uint64_t hash = 1469598103934665603ULL;
  for (const char c : value) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 1099511628211ULL;
  }
  return hash;
}
}  // namespace

void PurgeOrphanCachesActivity::onEnter() {
  Activity::onEnter();
  state = WARNING;
  requestUpdate();
}

void PurgeOrphanCachesActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_PURGE_ORPHAN_CACHES));

  if (state == WARNING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 60, tr(STR_PURGE_CACHE_WARNING_1), true);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 30, tr(STR_PURGE_CACHE_WARNING_2), true);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, tr(STR_PURGE_CACHE_WARNING_3), true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 30, tr(STR_PURGE_CACHE_WARNING_4), true);
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_CLEAN_BUTTON), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == SCANNING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, tr(STR_PURGE_SCANNING));
    char progress[64];
    snprintf(progress, sizeof(progress), "%u | %lu", static_cast<unsigned>(scannedBooks),
             static_cast<unsigned long>(visitedEntries));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 20, progress);
    renderer.displayBuffer();
    return;
  }

  if (state == PURGING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, tr(STR_PURGE_CLEANING));
    char progress[64];
    snprintf(progress, sizeof(progress), "%u %s | %u %s", static_cast<unsigned>(movedCount), tr(STR_PURGE_MOVED),
             static_cast<unsigned>(keptCount), tr(STR_PURGE_KEPT));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 20, progress);
    renderer.displayBuffer();
    return;
  }

  if (state == SUCCESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_PURGE_DONE), true, EpdFontFamily::BOLD);
    char result[96];
    snprintf(result, sizeof(result), "%u %s, %u %s", static_cast<unsigned>(movedCount), tr(STR_PURGE_MOVED),
             static_cast<unsigned>(keptCount), tr(STR_PURGE_KEPT));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, result);
    if (failedCount > 0) {
      char failures[48];
      snprintf(failures, sizeof(failures), "%u %s", static_cast<unsigned>(failedCount), tr(STR_FAILED_LOWER));
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 35, failures);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_CLEAR_CACHE_FAILED), true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
}

void PurgeOrphanCachesActivity::collectLiveCacheNames(const std::string& dirPath, const int depth) {
  if (depth > MAX_SCAN_DEPTH || scanIncomplete) return;
  // A wedged walk names its exact directory on the card.
  writePurgeHeartbeat("scan-dir", dirPath);
  auto dir = Storage.open(dirPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }
  // Two-phase: finish reading this directory and close it before recursing.
  // Nested live iteration wedged on hardware, and a corrupted (cross-linked)
  // directory chain can cycle forever — the caps below turn both into a
  // detected, refused-to-purge failure instead of a frozen screen.
  std::vector<std::string> subdirs;
  char name[128];
  uint16_t entriesHere = 0;
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    file.getName(name, sizeof(name));
    const bool isDir = file.isDirectory();
    file.close();
    if (++entriesHere > MAX_DIR_ENTRIES || ++visitedEntries > MAX_TOTAL_ENTRIES) {
      scanIncomplete = true;
      LOG_ERR("PURGE", "Scan cap hit in %s (%u here, %lu total)", dirPath.c_str(),
              static_cast<unsigned>(entriesHere), static_cast<unsigned long>(visitedEntries));
      break;
    }
    if (visitedEntries % 128 == 0) requestUpdate();
    if (visitedEntries % 64 == 0) writePurgeHeartbeat("scan-entry", dirPath + " #" + std::to_string(entriesHere));
    if (name[0] == '.') continue;  // system/hidden trees, including the caches themselves
    const std::string fullPath = (dirPath == "/" ? std::string("/") : dirPath + "/") + name;
    if (isDir) {
      if (subdirs.size() < MAX_SUBDIRS_PER_DIR) {
        subdirs.push_back(fullPath);
      } else {
        scanIncomplete = true;
      }
      continue;
    }
    if (FsHelpers::hasEpubExtension(fullPath)) {
      liveCacheNames.push_back(fnv64(Epub::cachePathForFilePath(fullPath, DUET_BOOKS_ROOT_PATH "")));
    } else if (FsHelpers::hasXtcExtension(fullPath)) {
      // Mirrors Xtc's cache naming exactly (std::hash of the full path).
      liveCacheNames.push_back(
          fnv64(std::string(DUET_BOOKS_ROOT_PATH "/xtc_") + std::to_string(std::hash<std::string>{}(fullPath))));
    } else {
      continue;
    }
    scannedBooks++;
    if (scannedBooks % 24 == 0) requestUpdate();
  }
  dir.close();
  for (const std::string& subdir : subdirs) {
    collectLiveCacheNames(subdir, depth + 1);
  }
}

void PurgeOrphanCachesActivity::runPurge() {
  liveCacheNames.clear();
  liveCacheNames.reserve(384);
  scannedBooks = 0;
  movedCount = 0;
  keptCount = 0;
  failedCount = 0;

  visitedEntries = 0;
  scanIncomplete = false;
  collectLiveCacheNames("/", 0);
  LOG_INF("PURGE", "Live books: %u (%u cache names, %lu entries seen)", scannedBooks,
          static_cast<unsigned>(liveCacheNames.size()), static_cast<unsigned long>(visitedEntries));
  if (scanIncomplete || scannedBooks == 0) {
    // Never purge from a partial or empty book list: live caches would be
    // treated as orphans. Zero books on a card full of books means the scan
    // itself failed (bad directory chain), not that the library is empty.
    LOG_ERR("PURGE", "Scan incomplete or empty; refusing to move anything");
    {
      RenderLock lock(*this);
      state = FAILED;
    }
    requestUpdate(true);
    return;
  }

  {
    RenderLock lock(*this);
    state = PURGING;
  }
  requestUpdate();

  writePurgeHeartbeat("purge-start", std::to_string(liveCacheNames.size()) + " live");
  Storage.mkdir(ATTIC_PATH);

  // Work in small batches: collect up to PURGE_BATCH_SIZE orphan names (a few
  // KB), close the directory, move them, and rescan. Renaming while iterating
  // a FAT directory invalidates the iteration, and holding every orphan name
  // at once does not fit this device's heap. The root shrinks every pass, so
  // later rescans get cheaper.
  std::vector<std::string> batch;
  batch.reserve(PURGE_BATCH_SIZE);
  char name[128];
  for (int pass = 0; pass < MAX_PURGE_PASSES; ++pass) {
    batch.clear();
    keptCount = 0;
    uint16_t failedSeen = 0;
    auto root = Storage.open(DUET_BOOKS_ROOT_PATH "");
    if (!root || !root.isDirectory()) {
      if (root) root.close();
      {
        RenderLock lock(*this);
        state = FAILED;
      }
      requestUpdate();
      return;
    }
    for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
      file.getName(name, sizeof(name));
      const bool isDir = file.isDirectory();
      file.close();
      if (!isDir || !isBookCacheDirectoryName(name)) continue;
      const uint64_t key = fnv64(std::string(DUET_BOOKS_ROOT_PATH "/") + name);
      bool live = false;
      for (const uint64_t liveKey : liveCacheNames) {
        if (liveKey == key) {
          live = true;
          break;
        }
      }
      if (live) {
        keptCount++;
      } else if (batch.size() < PURGE_BATCH_SIZE) {
        batch.push_back(name);
      } else {
        failedSeen++;  // orphans beyond this batch; another pass will get them
      }
    }
    root.close();

    for (const std::string& orphan : batch) {
      const std::string from = std::string(DUET_BOOKS_ROOT_PATH "/") + orphan;
      const std::string to = std::string(ATTIC_PATH) + "/" + orphan;
      if (Storage.rename(from.c_str(), to.c_str())) {
        movedCount++;
      } else {
        LOG_ERR("PURGE", "Could not move %s", from.c_str());
        failedCount++;
      }
      if ((movedCount + failedCount) % 25 == 0) requestUpdate();
    }
    if (failedSeen == 0) break;  // nothing left beyond this batch
  }

  LOG_INF("PURGE", "Done: moved=%u kept=%u failed=%u", movedCount, keptCount, failedCount);
  {
    RenderLock lock(*this);
    state = SUCCESS;
  }
  requestUpdate(true);
}

void PurgeOrphanCachesActivity::loop() {
  if (state == WARNING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      {
        RenderLock lock(*this);
        state = SCANNING;
      }
      if (requestUpdateAndWait() != RequestUpdateResult::Rendered) {
        {
          RenderLock lock(*this);
          state = FAILED;
        }
        requestUpdate(true);
        return;
      }
      runPurge();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
    }
    return;
  }

  if (state == SUCCESS || state == FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
    }
    return;
  }
}
