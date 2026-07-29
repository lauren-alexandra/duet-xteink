#pragma once

#include <DuetStoragePaths.h>

#include <cstdint>
#include <string>
#include <vector>

struct SdCardFontFileInfo {
  // Paths are reconstructed from the family so hundreds of installed files
  // do not each retain a heap-allocated string on memory-constrained devices.
  uint8_t pointSize;  // parsed from filename: 14
  uint8_t style;      // always 0 in v4 (all 4 styles bundled in one file);
                      // kept for potential future formats
};

struct SdCardFontFamilyInfo {
  std::string name;  // directory name, e.g. "NotoSansCJK"
  std::string filePrefix;  // shared v4 filename prefix, e.g. "NotoSansCJK"
  bool usesVisibleRoot = false;
  std::vector<SdCardFontFileInfo> files;

  const char* rootPath() const;
  std::string fileNameForFile(const SdCardFontFileInfo& file) const;
  std::string pathForFile(const SdCardFontFileInfo& file) const;
  const SdCardFontFileInfo* findFile(uint8_t size, uint8_t style = 0) const;
  const SdCardFontFileInfo* findClosestFile(uint8_t targetSize, uint8_t style = 0) const;
  const SdCardFontFileInfo* selectFile(uint8_t targetSize, uint8_t sizeStep, uint8_t style = 0) const;
  bool hasSize(uint8_t size) const;
  std::vector<uint8_t> availableSizes() const;
};

class SdCardFontRegistry {
 public:
  // Lauren's complete six-size pack currently contains 130 families. Keep a
  // modest amount of headroom so discovery never silently drops the final
  // alphabetic entries while retaining a firm bound on device metadata.
  static constexpr int MAX_SD_FAMILIES = 160;
  // Two top-level roots are scanned at discovery time. Hidden is preferred
  // when creating new installs; both are read from if present.
  static constexpr const char* FONTS_DIR_HIDDEN = "/.fonts";
  static constexpr const char* FONTS_DIR_VISIBLE = "/fonts";

  // Returns the existing root for `familyName` (the one that contains
  // /<root>/<familyName>/), or nullptr if the family is not installed in
  // either root. Used by writers to keep re-installs in their existing dir.
  static const char* findFamilyRoot(const char* familyName);

  // Returns the root path that should be used when creating a brand-new
  // family on disk (no prior install): the existing root if exactly one of
  // the two roots exists, otherwise the hidden root.
  static const char* defaultWriteRoot();

  // Scan SD card, populate families_. Returns true if any families found.
  // Also rewrites the persisted catalog cache.
  bool discover();
  // Boot path: load the persisted catalog cache when valid, else full scan.
  // Walking ~800 font files at every boot cost ~seconds on large libraries.
  bool discoverCachedOrScan();
  void clear();

  const std::vector<SdCardFontFamilyInfo>& getFamilies() const { return families_; }
  const SdCardFontFamilyInfo* findFamily(const std::string& name) const;
  int getFamilyIndex(const std::string& name) const;
  int getFamilyCount() const { return static_cast<int>(families_.size()); }

 private:
  std::vector<SdCardFontFamilyInfo> families_;  // sorted alphabetically

  static constexpr const char* CATALOG_CACHE_PATH = DUET_STATE_ROOT_PATH "/font_catalog_v1.bin";
  static constexpr const char* CATALOG_CACHE_TMP_PATH = DUET_STATE_ROOT_PATH "/font_catalog_v1.tmp";
  bool loadCatalogCache();
  void saveCatalogCache() const;

  static bool parseFilename(const char* filename, uint8_t& size, uint8_t& style);
  static void scanDirectory(const char* dirPath, SdCardFontFamilyInfo& family);
  // Scan one root (e.g. "/.fonts"), append families to `out`, dedup by name.
  static void scanRoot(const char* rootPath, std::vector<SdCardFontFamilyInfo>& out);
};
