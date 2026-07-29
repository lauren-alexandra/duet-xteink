#include "SdCardFontSystem.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include "CrossPointSettings.h"

namespace {
uint8_t closestSizeIndex(const std::vector<uint8_t>& sizes, const uint8_t targetPointSize) {
  uint8_t bestIndex = 0;
  uint8_t bestDiff = UINT8_MAX;
  for (size_t i = 0; i < sizes.size(); i++) {
    const uint8_t size = sizes[i];
    const uint8_t diff = size > targetPointSize ? size - targetPointSize : targetPointSize - size;
    if (diff < bestDiff || (diff == bestDiff && size < sizes[bestIndex])) {
      bestIndex = static_cast<uint8_t>(i);
      bestDiff = diff;
    }
  }
  return bestIndex;
}
}  // namespace

bool SdCardFontSystem::reconcileSelectedSize(const SdCardFontFamilyInfo& family) {
  const std::vector<uint8_t> sizes = family.availableSizes();
  if (sizes.empty()) return false;

  const uint8_t oldIndex = SETTINGS.fontSize;
  const uint8_t oldPointSize = SETTINGS.sdFontPointSize;
  if (SETTINGS.sdFontPointSize != 0) {
    SETTINGS.fontSize = closestSizeIndex(sizes, SETTINGS.sdFontPointSize);
  } else if (SETTINGS.fontSize >= sizes.size()) {
    SETTINGS.fontSize = static_cast<uint8_t>(sizes.size() - 1);
  }
  SETTINGS.sdFontPointSize = sizes[SETTINGS.fontSize];
  return oldIndex != SETTINGS.fontSize || oldPointSize != SETTINGS.sdFontPointSize;
}

void SdCardFontSystem::begin(GfxRenderer& renderer) {
  (void)renderer;
  registry_.discoverCachedOrScan();

  // Register this system as the SD font ID resolver in settings.
  // Uses a static trampoline since CrossPointSettings stores a plain function pointer.
  SETTINGS.sdFontIdResolver = [](void* ctx, const char* familyName, uint8_t fontSizeEnum) -> int {
    return static_cast<SdCardFontSystem*>(ctx)->resolveFontId(familyName, fontSizeEnum);
  };
  SETTINGS.sdFontResolverCtx = this;

  LOG_DBG("SDFS", "SD font system ready (%d families discovered)", registry_.getFamilyCount());
}

void SdCardFontSystem::loadSelectedFamily(GfxRenderer& renderer) {
  if (SETTINGS.sdFontFamilyName[0] == '\0') return;

  for (int attempt = 0; attempt < 2; attempt++) {
    const auto* family = registry_.findFamily(SETTINGS.sdFontFamilyName);
    if (family) {
      const bool sizeReconciled = reconcileSelectedSize(*family);
      if (manager_.loadFamily(*family, renderer, SETTINGS.getSdFontTargetPointSize(), SETTINGS.fontSize)) {
        LOG_DBG("SDFS", "Loaded SD card font family: %s", SETTINGS.sdFontFamilyName);
        if (sizeReconciled) SETTINGS.saveToFile();
        return;
      }
    }
    if (attempt == 0) {
      // The catalog cache may be stale (fonts replaced from the desktop).
      // Rescan the card once — discover() also rewrites the cache — and retry.
      LOG_ERR("SDFS", "Selected SD font missing or failed to load; rescanning card");
      registry_.discover();
    }
  }

  LOG_ERR("SDFS", "Failed to load SD font family: %s (clearing)", SETTINGS.sdFontFamilyName);
  SETTINGS.sdFontFamilyName[0] = '\0';
  SETTINGS.sdFontPointSize = 0;
  SETTINGS.saveToFile();
}

void SdCardFontSystem::ensureLoaded(GfxRenderer& renderer) {
  // If the web server (or another task) installed/deleted fonts, re-discover.
  // Track whether we just re-discovered so we can force a reload below even
  // when the wanted family/size still maps to the same point size — the file
  // contents on disk may have changed (e.g. user re-uploaded a new build).
  const bool registryWasDirty = registryDirty_.exchange(false, std::memory_order_acquire);
  if (registryWasDirty) {
    LOG_DBG("SDFS", "Registry dirty — re-discovering fonts");
    registry_.discover();
  }

  const char* wantedFamily = SETTINGS.sdFontFamilyName;
  const std::string& currentFamily = manager_.currentFamilyName();
  if (wantedFamily[0] == '\0') {
    if (!currentFamily.empty()) {
      manager_.unloadAll(renderer);
    }
    return;
  }

  // Reload if family changed OR if the user-selected size maps to a
  // different file than what's currently loaded OR if the registry was
  // just rediscovered (file may have been replaced on disk).
  bool familyMatches = (currentFamily == wantedFamily);
  if (familyMatches) {
    const auto* family = registry_.findFamily(wantedFamily);
    if (!family) {
      LOG_DBG("SDFS", "SD font family disappeared: %s (clearing)", wantedFamily);
      manager_.unloadAll(renderer);
      SETTINGS.sdFontFamilyName[0] = '\0';
      SETTINGS.sdFontPointSize = 0;
      SETTINGS.saveToFile();
      return;
    }
    reconcileSelectedSize(*family);
    const uint8_t targetPointSize = SETTINGS.getSdFontTargetPointSize();
    const uint8_t sizeStep = SETTINGS.fontSize;
    const auto* wantedFile = family->selectFile(targetPointSize, sizeStep);
    uint8_t wantedPt = wantedFile ? wantedFile->pointSize : 0;
    if (!registryWasDirty && wantedPt == manager_.currentPointSize()) return;
    LOG_DBG("SDFS", "Reloading %s: size %u -> %u (target %u step %u)%s", wantedFamily, manager_.currentPointSize(),
            wantedPt, targetPointSize, sizeStep, registryWasDirty ? " [registry dirty]" : "");
  }

  if (!currentFamily.empty()) {
    manager_.unloadAll(renderer);
  }

  const auto* family = registry_.findFamily(wantedFamily);
  if (family) {
    reconcileSelectedSize(*family);
    const uint8_t targetPointSize = SETTINGS.getSdFontTargetPointSize();
    const uint8_t sizeStep = SETTINGS.fontSize;
    if (manager_.loadFamily(*family, renderer, targetPointSize, sizeStep)) {
      LOG_DBG("SDFS", "Loaded SD font family: %s", wantedFamily);
    } else {
      LOG_ERR("SDFS", "Failed to load SD font family: %s (clearing)", wantedFamily);
      SETTINGS.sdFontFamilyName[0] = '\0';
      SETTINGS.sdFontPointSize = 0;
      SETTINGS.saveToFile();
    }
  } else {
    LOG_DBG("SDFS", "SD font family not found: %s (clearing)", wantedFamily);
    SETTINGS.sdFontFamilyName[0] = '\0';
    SETTINGS.sdFontPointSize = 0;
    SETTINGS.saveToFile();
  }
}

void SdCardFontSystem::releaseLoadedFont(GfxRenderer& renderer) {
  if (manager_.currentFamilyName().empty()) return;

  const std::string familyName = manager_.currentFamilyName();
  (void)familyName;
  manager_.unloadAll(renderer);
  LOG_DBG("SDFS", "Released SD card font before low-memory operation: %s", familyName.c_str());
}

void SdCardFontSystem::releaseForNetwork(GfxRenderer& renderer) {
  releaseLoadedFont(renderer);

  const int familyCount = registry_.getFamilyCount();
  if (familyCount == 0) return;

  registry_.clear();
  registryDirty_.store(true, std::memory_order_release);
  LOG_DBG("SDFS", "Released SD font registry before network operation (%d families)", familyCount);
}

int SdCardFontSystem::resolveFontId(const char* familyName, uint8_t /*fontSizeEnum*/) const {
  // The manager loads exactly one size (closest to SETTINGS.fontSize), so the
  // enum is implicit — always return the single loaded font ID for this family.
  // ensureLoaded() must have been called with the current settings before this.
  return manager_.getFontId(familyName);
}

bool SdCardFontSystem::changeReaderFontSize(const bool larger) {
  refreshIfDirty();

  if (SETTINGS.sdFontFamilyName[0] != '\0') {
    const auto* family = registry_.findFamily(SETTINGS.sdFontFamilyName);
    if (family) {
      const auto sizes = family->availableSizes();
      if (sizes.size() > 1) {
        uint8_t current = SETTINGS.fontSize < sizes.size() ? SETTINGS.fontSize : static_cast<uint8_t>(sizes.size() - 1);
        if (larger) {
          current = static_cast<uint8_t>((current + 1) % sizes.size());
        } else {
          current = current == 0 ? static_cast<uint8_t>(sizes.size() - 1) : static_cast<uint8_t>(current - 1);
        }
        SETTINGS.fontSize = current;
        SETTINGS.sdFontPointSize = sizes[current];
        return true;
      }
    }
  }

  return SETTINGS.changeReaderFontSize(larger);
}
