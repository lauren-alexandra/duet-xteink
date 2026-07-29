#pragma once

#include <Print.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Epub/BookMetadataCache.h"
#include "Epub/css/CssParser.h"

class ZipFile;
class GfxRenderer;

class Epub {
 public:
  using StreamCancelCallback = bool (*)(void*);

 private:
  // the ncx file (EPUB 2)
  std::string tocNcxItem;
  // the nav file (EPUB 3)
  std::string tocNavItem;
  // where is the EPUBfile?
  std::string filepath;
  // the base path for items in the EPUB file
  std::string contentBasePath;
  // Stable cache path based on filepath
  std::string cachePath;
  // Disposable reader-layout cache. Standard device builds shard this under
  // Duet's layout cache so chapter pagination never scans the crowded
  // per-book state root.
  std::string layoutCachePath;
  mutable bool layoutCachePrepared = false;
  // Spine and TOC cache
  std::unique_ptr<BookMetadataCache> bookMetadataCache;
  // CSS parser for styling
  std::unique_ptr<CssParser> cssParser;
  // CSS files
  std::vector<std::string> cssFiles;
  struct LocationSpineEntry {
    uint32_t startLocation = 0;
    uint32_t endLocation = 0;
    uint32_t wordStart = 0;
    uint32_t wordCount = 0;
  };
  std::vector<LocationSpineEntry> locationSpine;
  uint32_t totalLocations = 0;
  uint32_t totalWords = 0;
  uint32_t wordsPerReferencePage = 0;
  uint32_t totalReferencePages = 0;
  bool xLocationsLoaded = false;
  enum class CssParseStatus : uint8_t {
    Failed,
    Partial,
    Complete,
  };

  void migrateLegacyCachePath(const std::string& cacheDir) const;
  bool findContentOpfFile(std::string* contentOpfFile, StreamCancelCallback shouldCancel = nullptr,
                          void* cancelContext = nullptr) const;
  bool parseContentOpf(BookMetadataCache::BookMetadata& bookMetadata, bool writeSpineEntries = true,
                       StreamCancelCallback shouldCancel = nullptr, void* cancelContext = nullptr);
  bool parseTocNcxFile(StreamCancelCallback shouldCancel = nullptr, void* cancelContext = nullptr) const;
  bool parseTocNavFile(StreamCancelCallback shouldCancel = nullptr, void* cancelContext = nullptr) const;
  CssParseStatus parseCssFiles(bool forceRebuild = false, StreamCancelCallback shouldCancel = nullptr,
                               void* cancelContext = nullptr) const;
  void discoverCssFilesFromZip();

 public:
  explicit Epub(std::string filepath, const std::string& cacheDir);
  ~Epub() = default;
  static std::string cachePathForFilePath(const std::string& filepath, const std::string& cacheDir);
  static std::string layoutCachePathForFilePath(const std::string& filepath, const std::string& cacheDir);
  static bool migrateLayoutCacheForFileMove(const std::string& oldFilepath, const std::string& newFilepath,
                                            const std::string& cacheDir);
  // Sharded thumbnail path for a cache dir. Pure string work — no SD I/O and
  // no Epub construction (the constructor probes the crowded cache root).
  static std::string thumbBmpPathForCache(const std::string& cachePath, int width, int height);
  // True when a metadata cache already exists for this book, i.e. load() will
  // hit the fast path instead of rebuilding. Cheap: no parsing, just a stat.
  static bool hasCache(const std::string& filepath, const std::string& cacheDir);
  std::string& getBasePath() { return contentBasePath; }
  bool load(bool buildIfMissing = true, bool skipLoadingCss = false, StreamCancelCallback shouldCancel = nullptr,
            void* cancelContext = nullptr);
  // Called only on a real book-open path. Picker construction remains pure so
  // legacy SD migration cannot block cursor movement or run on the render task.
  void importLegacyUserState() const;
  bool clearCache() const;
  void setupCacheDir() const;
  void setupLayoutCacheDir() const;
  const std::string& getCachePath() const;
  const std::string& getLayoutCachePath() const;
  const std::string& getPath() const;
  const std::string& getTitle() const;
  const std::string& getAuthor() const;
  const std::string& getLanguage() const;
  std::string getCoverBmpPath(bool cropped = false) const;
  bool generateCoverBmp(bool cropped = false, const GfxRenderer* renderer = nullptr, int readerFontId = 0) const;
  std::string getThumbBmpPath() const;
  // Deprecated compatibility wrapper; forwards to getThumbBmpPath(0, height).
  [[deprecated("use getThumbBmpPath(int width, int height)")]]
  std::string getThumbBmpPath(int height) const;
  // Returns the thumbnail cache path. width <= 0 derives the default 3:5
  // (width:height) thumbnail width from height; height <= 0 uses the default
  // thumbnail height.
  std::string getThumbBmpPath(int width, int height) const;
  // Returns a Minimal-style adaptive thumbnail path. Normal cover ratios fill
  // the requested box; unusual ratios are contained inside the box.
  std::string getAdaptiveThumbBmpPath(int width, int height) const;
  // Deprecated compatibility wrapper; forwards to generateThumbBmp(0, height).
  [[deprecated("use generateThumbBmp(int width, int height)")]]
  bool generateThumbBmp(int height, const GfxRenderer* renderer = nullptr, int readerFontId = 0) const;
  // Writes a thumbnail BMP to cache. width <= 0 derives the default 3:5
  // (width:height) thumbnail width from height; height <= 0 uses the default
  // thumbnail height.
  // Returns false on missing cache/cover, unsupported image format, or conversion failure.
  bool generateThumbBmp(int width, int height, const GfxRenderer* renderer = nullptr, int readerFontId = 0,
                        StreamCancelCallback shouldCancel = nullptr, void* cancelContext = nullptr) const;
  // Writes a thumbnail that can either crop-to-fill or contain unusual cover
  // ratios, depending on the source image dimensions.
  bool generateAdaptiveThumbBmp(int width, int height, const GfxRenderer* renderer = nullptr,
                                int readerFontId = 0, StreamCancelCallback shouldCancel = nullptr,
                                void* cancelContext = nullptr) const;
  uint8_t* readItemContentsToBytes(const std::string& itemHref, size_t* size = nullptr,
                                   bool trailingNullByte = false) const;
  bool readItemContentsToStream(const std::string& itemHref, Print& out, size_t chunkSize,
                                StreamCancelCallback shouldCancel = nullptr, void* cancelContext = nullptr) const;
  bool getItemSize(const std::string& itemHref, size_t* size) const;
  BookMetadataCache::SpineEntry getSpineItem(int spineIndex) const;
  BookMetadataCache::TocEntry getTocItem(int tocIndex) const;
  int getSpineItemsCount() const;
  int getTocItemsCount() const;
  int getSpineIndexForTocIndex(int tocIndex) const;
  int getTocIndexForSpineIndex(int spineIndex) const;
  size_t getCumulativeSpineItemSize(int spineIndex) const;
  int getSpineIndexForTextReference() const;

  size_t getBookSize() const;
  bool hasXLocations() const { return xLocationsLoaded; }
  bool hasStablePageNumbers() const {
    return xLocationsLoaded && totalWords > 0 && wordsPerReferencePage > 0 && totalReferencePages > 0;
  }
  uint32_t getTotalWords() const { return xLocationsLoaded ? totalWords : 0; }
  float calculateSizeProgress(int currentSpineIndex, float currentSpineRead) const;
  float calculateProgress(int currentSpineIndex, float currentSpineRead) const;
  bool resolveLocationPercentToSpineProgress(int percent, int& spineIndex, float& spineProgress) const;
  bool resolveReferencePage(int currentSpineIndex, float currentSpineRead, uint32_t& currentPage,
                            uint32_t& pageCount) const;
  CssParser* getCssParser() const { return cssParser.get(); }
  int resolveHrefToSpineIndex(const std::string& href) const;

 private:
  bool loadXLocations();
  std::string getCachedCoverImagePath(const std::string& coverImageHref) const;
  bool ensureCachedCoverImage(const std::string& coverImageHref, std::string& outPath,
                              StreamCancelCallback shouldCancel = nullptr, void* cancelContext = nullptr) const;
  bool generateThumbBmpInternal(int width, int height, bool adaptiveContain, const GfxRenderer* renderer,
                                int readerFontId, StreamCancelCallback shouldCancel = nullptr,
                                void* cancelContext = nullptr) const;
};
