#include "SdCardFontRegistry.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

// --- SdCardFontFamilyInfo helpers ---

const char* SdCardFontFamilyInfo::rootPath() const {
  return usesVisibleRoot ? SdCardFontRegistry::FONTS_DIR_VISIBLE : SdCardFontRegistry::FONTS_DIR_HIDDEN;
}

std::string SdCardFontFamilyInfo::fileNameForFile(const SdCardFontFileInfo& file) const {
  return filePrefix + "_" + std::to_string(file.pointSize) + ".cpfont";
}

std::string SdCardFontFamilyInfo::pathForFile(const SdCardFontFileInfo& file) const {
  return std::string(rootPath()) + "/" + name + "/" + fileNameForFile(file);
}

const SdCardFontFileInfo* SdCardFontFamilyInfo::findFile(uint8_t size, uint8_t style) const {
  for (const auto& f : files) {
    if (f.pointSize == size && f.style == style) return &f;
  }
  return nullptr;
}

const SdCardFontFileInfo* SdCardFontFamilyInfo::findClosestFile(uint8_t targetSize, uint8_t style) const {
  const SdCardFontFileInfo* best = nullptr;
  uint8_t bestDiff = UINT8_MAX;
  for (const auto& f : files) {
    if (f.style != style) continue;
    const uint8_t diff = f.pointSize > targetSize ? f.pointSize - targetSize : targetSize - f.pointSize;
    if (!best || diff < bestDiff || (diff == bestDiff && f.pointSize < best->pointSize)) {
      best = &f;
      bestDiff = diff;
    }
  }
  return best;
}

const SdCardFontFileInfo* SdCardFontFamilyInfo::selectFile(uint8_t targetSize, uint8_t sizeStep, uint8_t style) const {
  const std::vector<uint8_t> sizes = availableSizes();
  if (!sizes.empty()) {
    if (sizeStep >= sizes.size()) sizeStep = static_cast<uint8_t>(sizes.size() - 1);
    const SdCardFontFileInfo* selected = findFile(sizes[sizeStep], style);
    if (selected) return selected;
  }
  return findClosestFile(targetSize, style);
}

bool SdCardFontFamilyInfo::hasSize(uint8_t size) const {
  for (const auto& f : files) {
    if (f.pointSize == size) return true;
  }
  return false;
}

std::vector<uint8_t> SdCardFontFamilyInfo::availableSizes() const {
  std::vector<uint8_t> sizes;
  for (const auto& f : files) {
    bool found = false;
    for (uint8_t s : sizes) {
      if (s == f.pointSize) {
        found = true;
        break;
      }
    }
    if (!found) sizes.push_back(f.pointSize);
  }
  std::sort(sizes.begin(), sizes.end());
  return sizes;
}

// --- SdCardFontRegistry ---

bool SdCardFontRegistry::parseFilename(const char* filename, uint8_t& size, uint8_t& style) {
  // V4 naming: <name>_<size>.cpfont (e.g. Bookerly-SD_14.cpfont)
  // Use an ends-with check rather than strstr() so that in-progress downloads
  // like "Foo_14.cpfont.tmp" or backups like "Foo_14.cpfont~" aren't accepted.
  static constexpr char kExt[] = ".cpfont";
  static constexpr size_t kExtLen = sizeof(kExt) - 1;
  const size_t nameLen = strlen(filename);
  if (nameLen <= kExtLen) return false;
  if (strcmp(filename + nameLen - kExtLen, kExt) != 0) return false;
  const char* ext = filename + nameLen - kExtLen;

  size_t baseLen = ext - filename;
  if (baseLen == 0 || baseLen > 127) return false;

  char base[128];
  memcpy(base, filename, baseLen);
  base[baseLen] = '\0';

  char* lastUnderscore = strrchr(base, '_');
  if (!lastUnderscore || lastUnderscore == base) return false;

  const char* sizeStr = lastUnderscore + 1;
  char* endPtr;
  long sizeVal = strtol(sizeStr, &endPtr, 10);
  if (endPtr == sizeStr || *endPtr != '\0' || sizeVal < 1 || sizeVal > 255) return false;
  size = static_cast<uint8_t>(sizeVal);
  // V4 .cpfont files bundle every style (regular/bold/italic/bold-italic) into
  // one file, so style is always 0 at the registry level. The per-style
  // bitstream is selected later by SdCardFont::getEpdFont(style). The `style`
  // field in SdCardFontFileInfo is reserved for future formats that split
  // styles across files; scanDirectory() defends against accidental
  // (pointSize, style) collisions in that scenario.
  style = 0;
  return true;
}

void SdCardFontRegistry::scanDirectory(const char* dirPath, SdCardFontFamilyInfo& family) {
  HalFile dir = Storage.open(dirPath);
  if (!dir || !dir.isDirectory()) return;

  char nameBuffer[128];
  while (true) {
    HalFile entry = dir.openNextFile();
    if (!entry) break;
    if (entry.isDirectory()) {
      entry.close();
      continue;
    }

    entry.getName(nameBuffer, sizeof(nameBuffer));
    entry.close();

    // Skip macOS resource fork files (._*) and other hidden files
    if (nameBuffer[0] == '.' || nameBuffer[0] == '_') continue;

    uint8_t size, style;
    if (!parseFilename(nameBuffer, size, style)) continue;

    const char* lastUnderscore = strrchr(nameBuffer, '_');
    if (!lastUnderscore) continue;
    const std::string filePrefix(nameBuffer, static_cast<size_t>(lastUnderscore - nameBuffer));
    if (family.filePrefix.empty()) {
      family.filePrefix = filePrefix;
    } else if (family.filePrefix != filePrefix) {
      LOG_ERR("SDREG", "Mixed font prefixes in %s (%s, expected %s) — skipping", dirPath, nameBuffer,
              family.filePrefix.c_str());
      continue;
    }

    // Reject duplicate (pointSize, style) entries in the same family. With
    // v4's bundle-everything design parseFilename always returns style=0, so
    // two files at the same size in the same family would silently shadow
    // each other in findFile(). Skip the duplicate and warn.
    bool duplicate = false;
    for (const auto& existing : family.files) {
      if (existing.pointSize == size && existing.style == style) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      LOG_ERR("SDREG", "Duplicate font %s in %s — skipping", nameBuffer, dirPath);
      continue;
    }

    SdCardFontFileInfo info;
    info.pointSize = size;
    info.style = style;
    family.files.push_back(std::move(info));
  }
  family.files.shrink_to_fit();
}

// Scan a single root (e.g. "/.fonts") and append its families to `out`.
// Skips families whose names already exist in `out` (de-duplicates between
// the hidden and visible roots — first scan wins).
void SdCardFontRegistry::scanRoot(const char* rootPath, std::vector<SdCardFontFamilyInfo>& out) {
  HalFile root = Storage.open(rootPath);
  if (!root) {
    LOG_DBG("SDREG", "Fonts directory not found: %s", rootPath);
    return;
  }
  if (!root.isDirectory()) {
    LOG_ERR("SDREG", "Fonts path is not a directory: %s", rootPath);
    return;
  }

  char nameBuffer[128];
  while (true) {
    HalFile entry = root.openNextFile();
    if (!entry) break;
    if (entry.isDirectory()) {
      entry.getName(nameBuffer, sizeof(nameBuffer));
      entry.close();

      // Skip hidden/system directories inside the root (macOS ._*, .Trashes, etc.)
      if (nameBuffer[0] == '.' || nameBuffer[0] == '_') continue;

      // De-dup by family name across roots.
      bool exists = false;
      for (const auto& fam : out) {
        if (fam.name == nameBuffer) {
          exists = true;
          break;
        }
      }
      if (exists) continue;

      SdCardFontFamilyInfo family;
      family.name = nameBuffer;
      family.usesVisibleRoot = strcmp(rootPath, FONTS_DIR_VISIBLE) == 0;
      std::string subDirPath = std::string(rootPath) + "/" + nameBuffer;
      SdCardFontRegistry::scanDirectory(subDirPath.c_str(), family);

      if (!family.files.empty()) {
        out.push_back(std::move(family));
        LOG_DBG("SDREG", "Found family: %s (%d files) in %s", out.back().name.c_str(),
                static_cast<int>(out.back().files.size()), rootPath);
      }
    } else {
      entry.close();
    }
  }
}

bool SdCardFontRegistry::discover() {
  families_.clear();
  families_.reserve(MAX_SD_FAMILIES);

  // Hidden root is scanned first so it wins on name collisions, matching the
  // sleep-folder pattern (/.sleep preferred over /sleep).
  scanRoot(FONTS_DIR_HIDDEN, families_);
  scanRoot(FONTS_DIR_VISIBLE, families_);

  // Sort families alphabetically
  std::sort(families_.begin(), families_.end(),
            [](const SdCardFontFamilyInfo& a, const SdCardFontFamilyInfo& b) { return a.name < b.name; });

  // Cap at MAX_SD_FAMILIES
  if (static_cast<int>(families_.size()) > MAX_SD_FAMILIES) {
    families_.resize(MAX_SD_FAMILIES);
  }
  families_.shrink_to_fit();

  LOG_DBG("SDREG", "Discovery complete: %d families", static_cast<int>(families_.size()));
  saveCatalogCache();
  return !families_.empty();
}

bool SdCardFontRegistry::discoverCachedOrScan() {
  if (loadCatalogCache()) {
    LOG_DBG("SDREG", "Catalog cache hit: %d families", static_cast<int>(families_.size()));
    return !families_.empty();
  }
  return discover();
}

namespace {
constexpr uint32_t CATALOG_MAGIC = 0x31544346u;  // "FCT1" little-endian

bool readByte(HalFile& f, uint8_t& v) { return f.read(&v, 1) == 1; }
bool readShortString(HalFile& f, std::string& out) {
  uint8_t len = 0;
  if (!readByte(f, len)) return false;
  out.resize(len);
  return len == 0 || f.read(reinterpret_cast<uint8_t*>(out.data()), len) == len;
}
void writeByte(HalFile& f, uint8_t v) { f.write(&v, 1); }
void writeShortString(HalFile& f, const std::string& s) {
  const uint8_t len = static_cast<uint8_t>(std::min<size_t>(s.size(), 255));
  writeByte(f, len);
  if (len) f.write(reinterpret_cast<const uint8_t*>(s.data()), len);
}
}  // namespace

bool SdCardFontRegistry::loadCatalogCache() {
  FsFile f;
  if (!Storage.openFileForRead("SDREG", CATALOG_CACHE_PATH, f)) return false;

  families_.clear();
  uint32_t magic = 0;
  uint16_t count = 0;
  bool ok = f.read(reinterpret_cast<uint8_t*>(&magic), 4) == 4 && magic == CATALOG_MAGIC &&
            f.read(reinterpret_cast<uint8_t*>(&count), 2) == 2 && count <= MAX_SD_FAMILIES;
  for (uint16_t i = 0; ok && i < count; i++) {
    SdCardFontFamilyInfo family;
    uint8_t visibleRoot = 0;
    uint8_t fileCount = 0;
    ok = readShortString(f, family.name) && readShortString(f, family.filePrefix) && readByte(f, visibleRoot) &&
         readByte(f, fileCount) && !family.name.empty();
    family.usesVisibleRoot = visibleRoot != 0;
    for (uint8_t j = 0; ok && j < fileCount; j++) {
      SdCardFontFileInfo info;
      ok = readByte(f, info.pointSize) && readByte(f, info.style);
      if (ok) family.files.push_back(info);
    }
    if (ok && !family.files.empty()) families_.push_back(std::move(family));
  }
  f.close();
  if (!ok) {
    families_.clear();
    LOG_ERR("SDREG", "Catalog cache invalid; falling back to full scan");
    Storage.remove(CATALOG_CACHE_PATH);
  }
  return ok && !families_.empty();
}

void SdCardFontRegistry::saveCatalogCache() const {
  Storage.mkdir(DUET_STATE_ROOT_PATH "");
  Storage.remove(CATALOG_CACHE_TMP_PATH);
  FsFile f;
  if (!Storage.openFileForWrite("SDREG", CATALOG_CACHE_TMP_PATH, f)) return;

  const uint32_t magic = CATALOG_MAGIC;
  const uint16_t count = static_cast<uint16_t>(families_.size());
  f.write(reinterpret_cast<const uint8_t*>(&magic), 4);
  f.write(reinterpret_cast<const uint8_t*>(&count), 2);
  for (const auto& family : families_) {
    writeShortString(f, family.name);
    writeShortString(f, family.filePrefix);
    writeByte(f, family.usesVisibleRoot ? 1 : 0);
    writeByte(f, static_cast<uint8_t>(std::min<size_t>(family.files.size(), 255)));
    for (size_t j = 0; j < family.files.size() && j < 255; j++) {
      writeByte(f, family.files[j].pointSize);
      writeByte(f, family.files[j].style);
    }
  }
  const bool closed = f.close();
  if (!closed) {
    Storage.remove(CATALOG_CACHE_TMP_PATH);
    return;
  }
  Storage.remove(CATALOG_CACHE_PATH);
  if (!Storage.rename(CATALOG_CACHE_TMP_PATH, CATALOG_CACHE_PATH)) {
    Storage.remove(CATALOG_CACHE_TMP_PATH);
  }
}

void SdCardFontRegistry::clear() { std::vector<SdCardFontFamilyInfo>().swap(families_); }

const char* SdCardFontRegistry::findFamilyRoot(const char* familyName) {
  if (!familyName || !*familyName) return nullptr;
  char path[160];
  snprintf(path, sizeof(path), "%s/%s", FONTS_DIR_HIDDEN, familyName);
  if (Storage.exists(path)) return FONTS_DIR_HIDDEN;
  snprintf(path, sizeof(path), "%s/%s", FONTS_DIR_VISIBLE, familyName);
  if (Storage.exists(path)) return FONTS_DIR_VISIBLE;
  return nullptr;
}

const char* SdCardFontRegistry::defaultWriteRoot() {
  // If exactly one of the roots already exists, keep using it. Otherwise
  // (neither exists, or both exist) prefer the hidden root for new installs.
  bool hiddenExists = Storage.exists(FONTS_DIR_HIDDEN);
  bool visibleExists = Storage.exists(FONTS_DIR_VISIBLE);
  if (hiddenExists) return FONTS_DIR_HIDDEN;
  if (visibleExists) return FONTS_DIR_VISIBLE;
  return FONTS_DIR_HIDDEN;
}

const SdCardFontFamilyInfo* SdCardFontRegistry::findFamily(const std::string& name) const {
  for (const auto& f : families_) {
    if (f.name == name) return &f;
  }
  return nullptr;
}

int SdCardFontRegistry::getFamilyIndex(const std::string& name) const {
  for (int i = 0; i < static_cast<int>(families_.size()); i++) {
    if (families_[i].name == name) return i;
  }
  return -1;
}
