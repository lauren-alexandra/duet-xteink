#include "Epub.h"

#include <ArduinoJson.h>
#include <DuetStorageMigration.h>
#include <DuetStoragePaths.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>
#include <Memory.h>
#include <MemoryBudget.h>
#include <PngToBmpConverter.h>
#include <ThumbnailCache.h>
#include <Utf8.h>
#include <ZipFile.h>

#include <mutex>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <utility>

#include "Epub/parsers/ContainerParser.h"
#include "Epub/parsers/ContentOpfParser.h"
#include "Epub/parsers/TocNavParser.h"
#include "Epub/parsers/TocNcxParser.h"

namespace {
constexpr int kDefaultThumbHeight = 180;
constexpr char kXLocationsPath[] = "META-INF/x-locations.json";
constexpr char kXLocationsFormat[] = "x-locations";
constexpr char kLegacyXLocationsFormat[] = "crossink-locations";
constexpr size_t kXLocationsMaxBytes = 64 * 1024;
constexpr uint32_t kDefaultReferenceCharactersPerPage = 1500;

bool isSupportedLocationsFormat(const char* format) {
  return std::strcmp(format, kXLocationsFormat) == 0 || std::strcmp(format, kLegacyXLocationsFormat) == 0;
}

void buildXLocationsJsonFilter(JsonDocument& filter) {
  JsonObject root = filter.to<JsonObject>();
  root["format"] = true;
  root["version"] = true;
  root["totalLocations"] = true;
  root["totalWords"] = true;
  root["totalCharacters"] = true;
  root["wordsPerReferencePage"] = true;
  root["charactersPerReferencePage"] = true;
  root["totalReferencePages"] = true;

  JsonArray spine = root["spine"].to<JsonArray>();
  JsonObject spineEntry = spine.add<JsonObject>();
  spineEntry["index"] = true;
  spineEntry["startLocation"] = true;
  spineEntry["endLocation"] = true;
  spineEntry["wordStart"] = true;
  spineEntry["wordCount"] = true;
  spineEntry["characterStart"] = true;
  spineEntry["characterCount"] = true;
}

float clampUnit(const float value) {
  if (value <= 0.0f) {
    return 0.0f;
  }
  if (value >= 1.0f) {
    return 1.0f;
  }
  return value;
}

int32_t readLe32(const uint8_t* data) {
  return static_cast<int32_t>(static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
                              (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24));
}

void normalizeThumbDimensions(int& width, int& height) {
  if (height <= 0) {
    height = kDefaultThumbHeight;
  }
  if (width <= 0) {
    width = static_cast<int>((static_cast<int64_t>(height) * 2 + 1) / 3);
  }
}

std::unique_ptr<BookMetadataCache> makeBookMetadataCacheNoThrow(const std::string& cachePath) {
  auto cache = makeUniqueNoThrow<BookMetadataCache>(cachePath);
  if (!cache) {
    LOG_ERR("EBP", "OOM: BookMetadataCache (%u free, %u max alloc)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  }
  return cache;
}

std::unique_ptr<CssParser> makeCssParserNoThrow(const std::string& cachePath) {
  auto parser = makeUniqueNoThrow<CssParser>(cachePath);
  if (!parser) {
    LOG_ERR("EBP", "OOM: CssParser (%u free, %u max alloc)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  }
  return parser;
}

bool cachedBmpMatchesDimensions(const std::string& path, const int width, const int height,
                                const bool allowContainedDimensions = false) {
  if (!Storage.exists(path.c_str())) {
    return false;
  }

  FsFile file;
  if (!Storage.openFileForRead("EBP", path, file)) {
    return false;
  }

  uint8_t header[26] = {};
  const bool hasHeader = file.size() >= sizeof(header) && file.read(header, sizeof(header)) == sizeof(header);
  file.close();
  const bool isBmp = hasHeader && header[0] == 'B' && header[1] == 'M';
  const int32_t bmpWidth = isBmp ? readLe32(header + 18) : 0;
  const int32_t bmpHeight = isBmp ? readLe32(header + 22) : 0;
  const int32_t absHeight = bmpHeight < 0 ? -bmpHeight : bmpHeight;
  const bool exactMatch = isBmp && bmpWidth == width && absHeight == height;
  const bool containedMatch = allowContainedDimensions && isBmp && bmpWidth > 0 && absHeight > 0 && bmpWidth <= width &&
                              absHeight <= height && (bmpWidth == width || absHeight == height);
  const bool matches = exactMatch || containedMatch;
  if (!matches) {
    LOG_DBG("EBP", "Removing stale thumbnail dimensions: %s (%dx%d expected %dx%d)", path.c_str(), bmpWidth, absHeight,
            width, height);
    Storage.remove(path.c_str());
  }
  return matches;
}

void releaseReaderSdFontCachesBeforeCoverDecode(const GfxRenderer* renderer, const int readerFontId,
                                                const char* reason) {
  if (!renderer) return;
  if (readerFontId <= 0) return;
  if (!renderer->isSdCardFont(readerFontId)) return;

  const auto before = MemoryBudget::snapshot();
  if (!MemoryBudget::shouldReleaseSdFontCachesForEpubInlineImage(before)) return;

  if (!renderer->releaseSdCardFontForLowMemory(readerFontId)) return;

  const auto after = MemoryBudget::snapshot();
  LOG_DBG("EBP", "Released SD font caches before %s: free=%u->%u maxAlloc=%u->%u", reason, before.freeHeap,
          after.freeHeap, before.maxAllocHeap, after.maxAllocHeap);
}

// Thumbnails live in small sharded directories under Duet's cache root instead of
// inside each book's cache dir: on FAT every open scans the containing
// directory, and the cache root holds thousands of entries, so an in-cache
// thumbnail cost seconds per open.
std::string thumbShardHashFromCachePath(const std::string& cachePath) {
  const size_t slash = cachePath.find_last_of('/');
  const std::string dirName = slash == std::string::npos ? cachePath : cachePath.substr(slash + 1);
  const size_t underscore = dirName.find_last_of('_');
  return underscore == std::string::npos ? dirName : dirName.substr(underscore + 1);
}

std::string thumbShardDirForHash(const std::string& hash) {
  return ThumbnailCache::shardDirForHash(hash);
}

void ensureThumbShardDir(const std::string& hash) {
  Storage.mkdir(DuetStorage::ROOT);
  Storage.mkdir(DuetStorage::CACHE_ROOT);
  Storage.mkdir(DuetStorage::THUMBS_ROOT);
  Storage.mkdir(thumbShardDirForHash(hash).c_str());
}

std::string getThumbBmpPathForDimensions(const std::string& cachePath, int width, int height) {
  const std::string hash = thumbShardHashFromCachePath(cachePath);
  return ThumbnailCache::pathForHash(hash, width, height);
}

std::string getAdaptiveThumbBmpPathForDimensions(const std::string& cachePath, int width, int height) {
  const std::string hash = thumbShardHashFromCachePath(cachePath);
  return ThumbnailCache::pathForHash(hash, width, height, true);
}

std::string layoutShardDirForHash(const std::string& hash) {
  return std::string(DuetStorage::LAYOUTS_ROOT) + "/" + (hash.empty() ? '0' : hash[0]);
}

void ensureLayoutShardDir(const std::string& hash) {
  Storage.mkdir(DuetStorage::ROOT);
  Storage.mkdir(DuetStorage::CACHE_ROOT);
  Storage.mkdir(DuetStorage::LAYOUTS_ROOT);
  Storage.mkdir(layoutShardDirForHash(hash).c_str());
}

// One existence probe per path per session, keyed by path hash so 768 entries
// cost ~12 KB instead of the ~50 KB that string keys would. Every probe of a
// path under the crowded cache root pays a linear directory scan, and cover
// shelves re-ask for the same paths on every pass; without this cache a
// 266-book shelf froze for minutes. A stale "absent" answer is harmless: a
// cache directory created later this session can only contain freshly
// written (sharded) files, never legacy thumbnails.
std::mutex pathMemoMutex;
std::vector<std::pair<uint64_t, bool>> pathMemoEntries;

void rememberPathExists(const std::string& path, const bool exists) {
  const uint64_t key = ZipFile::fnvHash64(path.c_str(), path.size());
  std::lock_guard<std::mutex> lock(pathMemoMutex);
  for (auto& known : pathMemoEntries) {
    if (known.first == key) {
      known.second = exists;
      return;
    }
  }
  if (pathMemoEntries.size() < 256) pathMemoEntries.emplace_back(key, exists);
}

bool pathExistsMemoized(const std::string& path) {
  const uint64_t key = ZipFile::fnvHash64(path.c_str(), path.size());
  {
    std::lock_guard<std::mutex> lock(pathMemoMutex);
    for (const auto& known : pathMemoEntries) {
      if (known.first == key) return known.second;
    }
  }
  const bool exists = Storage.existsForRead(path);
  rememberPathExists(path, exists);
  return exists;
}

// Copies a legacy thumbnail to its canonical sharded home on first touch.
// The existence probe scans the crowded cache directory, so each legacy path
// is probed at most once per session (cover views re-request paths per pass).
bool migrateLegacyThumb(const std::string& cachePath, const std::string& legacyPath,
                        const std::string& shardedPath) {
  static std::mutex probeMutex;
  static std::vector<uint64_t> probedLegacy;
  const uint64_t legacyKey = ZipFile::fnvHash64(legacyPath.c_str(), legacyPath.size());
  {
    std::lock_guard<std::mutex> lock(probeMutex);
    for (const uint64_t seen : probedLegacy) {
      if (seen == legacyKey) return false;
    }
    if (probedLegacy.size() < 1024) probedLegacy.push_back(legacyKey);
  }
  const bool shardedLegacy = legacyPath.rfind(DUET_LEGACY_STATE_ROOT_PATH "/thumbs/", 0) == 0;
  if (!shardedLegacy && !pathExistsMemoized(cachePath)) return false;
  if (!Storage.existsForRead(legacyPath)) return false;
  ensureThumbShardDir(thumbShardHashFromCachePath(cachePath));
  return DuetStorage::copyLegacyFileToCanonical(legacyPath.c_str(), shardedPath.c_str());
}

std::string legacyCachePathForFilePath(const std::string& filepath, const std::string& cacheDir) {
  const char* legacyRoot = cacheDir == DuetStorage::BOOKS_ROOT ? DuetStorage::LEGACY_BOOKS_ROOT : cacheDir.c_str();
  return std::string(legacyRoot) + "/epub_" + std::to_string(std::hash<std::string>{}(filepath));
}

class CoverImageRefScanner final : public Print {
 public:
  std::string imageRef;

  size_t write(uint8_t data) override { return write(&data, 1); }

  size_t write(const uint8_t* buffer, size_t size) override {
    for (size_t i = 0; i < size && imageRef.empty(); ++i) {
      consume(static_cast<char>(buffer[i]));
    }
    return size;
  }

 private:
  static constexpr size_t kMaxImageRefLen = 512;
  static constexpr const char* kXlinkPattern = "xlink:href=\"";
  static constexpr const char* kSrcPattern = "src=\"";

  size_t xlinkMatched = 0;
  size_t srcMatched = 0;
  bool collecting = false;
  std::string candidate;

  static bool isSupportedImageRef(const std::string& ref) {
    const auto view = std::string_view{ref};
    return FsHelpers::hasPngExtension(view) || FsHelpers::hasJpgExtension(view) || FsHelpers::hasGifExtension(view);
  }

  void consume(const char c) {
    if (collecting) {
      if (c == '"') {
        if (isSupportedImageRef(candidate)) {
          imageRef = candidate;
        }
        candidate.clear();
        collecting = false;
        xlinkMatched = 0;
        srcMatched = 0;
        return;
      }
      if (candidate.size() < kMaxImageRefLen) {
        candidate.push_back(c);
      } else {
        candidate.clear();
        collecting = false;
      }
      return;
    }

    const auto advance = [c](const char* pattern, size_t matched) {
      if (c == pattern[matched]) {
        return matched + 1;
      }
      return c == pattern[0] ? size_t{1} : size_t{0};
    };

    xlinkMatched = advance(kXlinkPattern, xlinkMatched);
    srcMatched = advance(kSrcPattern, srcMatched);

    if (kXlinkPattern[xlinkMatched] == '\0' || kSrcPattern[srcMatched] == '\0') {
      collecting = true;
      candidate.clear();
    }
  }
};
}  // namespace

Epub::Epub(std::string filepath, const std::string& cacheDir) : filepath(std::move(filepath)) {
  cachePath = cachePathForFilePath(this->filepath, cacheDir);
  layoutCachePath = layoutCachePathForFilePath(this->filepath, cacheDir);
}

std::string Epub::cachePathForFilePath(const std::string& filepath, const std::string& cacheDir) {
  // Keep on-disk EPUB cache keys stable across standard library/toolchain changes.
  return cacheDir + "/epub_" + std::to_string(ZipFile::fnvHash64(filepath.c_str(), filepath.size()));
}

std::string Epub::layoutCachePathForFilePath(const std::string& filepath, const std::string& cacheDir) {
  const std::string metadataPath = cachePathForFilePath(filepath, cacheDir);
  if (cacheDir != DuetStorage::BOOKS_ROOT) {
    return metadataPath;
  }

  const std::string hash = thumbShardHashFromCachePath(metadataPath);
  return layoutShardDirForHash(hash) + "/epub_" + hash;
}

bool Epub::migrateLayoutCacheForFileMove(const std::string& oldFilepath, const std::string& newFilepath,
                                         const std::string& cacheDir) {
  const std::string oldLayoutPath = layoutCachePathForFilePath(oldFilepath, cacheDir);
  const std::string newLayoutPath = layoutCachePathForFilePath(newFilepath, cacheDir);
  if (oldLayoutPath == newLayoutPath || !Storage.exists(oldLayoutPath.c_str())) return true;
  if (Storage.exists(newLayoutPath.c_str())) {
    LOG_ERR("EBP", "Refusing to overwrite reader-layout cache during book move: %s", newLayoutPath.c_str());
    return false;
  }

  ensureLayoutShardDir(thumbShardHashFromCachePath(cachePathForFilePath(newFilepath, cacheDir)));
  if (!Storage.rename(oldLayoutPath.c_str(), newLayoutPath.c_str())) {
    LOG_ERR("EBP", "Failed to migrate reader-layout cache: %s -> %s", oldLayoutPath.c_str(), newLayoutPath.c_str());
    return false;
  }
  return true;
}

bool Epub::hasCache(const std::string& filepath, const std::string& cacheDir) {
  return BookMetadataCache::exists(cachePathForFilePath(filepath, cacheDir));
}

void Epub::migrateLegacyCachePath(const std::string& cacheDir) const {
  if (cacheDir == DuetStorage::BOOKS_ROOT && !DuetStorage::migrateLegacyBookStateOnDevice(cachePath.c_str())) {
    LOG_ERR("EBP", "Stable legacy EPUB state import incomplete: %s", cachePath.c_str());
  }

  // This runs only when a book is genuinely opened. The probes remain memoized
  // because opening several uncached books can still revisit the legacy root.
  if (pathExistsMemoized(cachePath)) {
    return;
  }

  const std::string legacyCachePath = legacyCachePathForFilePath(filepath, cacheDir);
  if (legacyCachePath == cachePath || !pathExistsMemoized(legacyCachePath)) {
    return;
  }

  static constexpr const char* USER_STATE_FILES[] = {
      "progress.bin", "progress.bin.bak", "progress_pct.bin", "reader_settings.bin",
      "stats.bin",    "stats_v1.bin",     "stats_v2.bin",     "stats_v3.bin",
      "stats_v4.bin", "stats_v5.bin",     "stats_v6.bin",     "stats_v7.bin",
  };
  Storage.mkdir(DuetStorage::ROOT);
  Storage.mkdir(DuetStorage::BOOKS_ROOT);
  Storage.mkdir(cachePath.c_str());

  bool copiedAny = false;
  bool ok = true;
  for (const char* fileName : USER_STATE_FILES) {
    const std::string source = legacyCachePath + "/" + fileName;
    if (!Storage.existsForRead(source)) continue;
    const std::string destination = cachePath + "/" + fileName;
    if (!DuetStorage::copyLegacyFileToCanonical(source.c_str(), destination.c_str())) {
      ok = false;
    } else {
      copiedAny = true;
    }
  }
  if (copiedAny) {
    rememberPathExists(cachePath, true);
    LOG_INF("EBP", "Imported legacy EPUB user state without removing source: %s -> %s", legacyCachePath.c_str(),
            cachePath.c_str());
  } else if (!ok) {
    LOG_ERR("EBP", "Legacy EPUB user-state import incomplete: %s -> %s", legacyCachePath.c_str(),
            cachePath.c_str());
  }
}

void Epub::importLegacyUserState() const { migrateLegacyCachePath(DuetStorage::BOOKS_ROOT); }

bool Epub::findContentOpfFile(std::string* contentOpfFile, const StreamCancelCallback shouldCancel,
                              void* cancelContext) const {
  const auto containerPath = "META-INF/container.xml";
  size_t containerSize;

  // Get file size without loading it all into heap
  if (!getItemSize(containerPath, &containerSize)) {
    LOG_ERR("EBP", "Could not find or size META-INF/container.xml");
    return false;
  }

  ContainerParser containerParser(containerSize);

  if (!containerParser.setup()) {
    return false;
  }

  // Stream read (reusing your existing stream logic)
  if (!readItemContentsToStream(containerPath, containerParser, 512, shouldCancel, cancelContext)) {
    LOG_ERR("EBP", "Could not read META-INF/container.xml");
    return false;
  }

  // Extract the result
  if (containerParser.fullPath.empty()) {
    LOG_ERR("EBP", "Could not find valid rootfile in container.xml");
    return false;
  }

  *contentOpfFile = std::move(containerParser.fullPath);
  return true;
}

bool Epub::parseContentOpf(BookMetadataCache::BookMetadata& bookMetadata, const bool writeSpineEntries,
                           const StreamCancelCallback shouldCancel, void* cancelContext) {
  std::string contentOpfFilePath;
  if (!findContentOpfFile(&contentOpfFilePath, shouldCancel, cancelContext)) {
    LOG_ERR("EBP", "Could not find content.opf in zip");
    return false;
  }

  contentBasePath = contentOpfFilePath.substr(0, contentOpfFilePath.find_last_of('/') + 1);

  LOG_DBG("EBP", "Parsing content.opf: %s", contentOpfFilePath.c_str());

  size_t contentOpfSize;
  if (!getItemSize(contentOpfFilePath, &contentOpfSize)) {
    LOG_ERR("EBP", "Could not get size of content.opf");
    return false;
  }

  ContentOpfParser opfParser(getCachePath(), getBasePath(), contentOpfSize,
                             writeSpineEntries ? bookMetadataCache.get() : nullptr);
  if (!opfParser.setup()) {
    LOG_ERR("EBP", "Could not setup content.opf parser");
    return false;
  }

  if (!readItemContentsToStream(contentOpfFilePath, opfParser, 1024, shouldCancel, cancelContext)) {
    LOG_ERR("EBP", "Could not read content.opf");
    return false;
  }

  // Grab data from opfParser into epub. Normalize titles to NFC so NFD (combining
  // mark) text renders correctly — the device fonts have no mark positioning.
  bookMetadata.title = utf8ComposeNfc(opfParser.title);
  bookMetadata.author = opfParser.author;
  bookMetadata.language = opfParser.language;
  bookMetadata.coverItemHref = opfParser.coverItemHref;

  // Guide-based cover fallback: if no cover found via metadata/properties,
  // try extracting the image reference from the guide's cover page XHTML
  if (bookMetadata.coverItemHref.empty() && !opfParser.guideCoverPageHref.empty()) {
    LOG_DBG("EBP", "No cover from metadata, trying guide cover page: %s", opfParser.guideCoverPageHref.c_str());
    CoverImageRefScanner scanner;
    if (readItemContentsToStream(opfParser.guideCoverPageHref, scanner, 512, shouldCancel, cancelContext) &&
        !scanner.imageRef.empty()) {
      std::string coverPageBase;
      const auto lastSlash = opfParser.guideCoverPageHref.rfind('/');
      if (lastSlash != std::string::npos) {
        coverPageBase = opfParser.guideCoverPageHref.substr(0, lastSlash + 1);
      }
      bookMetadata.coverItemHref =
          FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(coverPageBase + scanner.imageRef));
      LOG_DBG("EBP", "Found cover image from guide: %s", bookMetadata.coverItemHref.c_str());
    }
  }

  bookMetadata.textReferenceHref = opfParser.textReferenceHref;

  if (!opfParser.tocNcxPath.empty()) {
    tocNcxItem = opfParser.tocNcxPath;
  }

  if (!opfParser.tocNavPath.empty()) {
    tocNavItem = opfParser.tocNavPath;
  }

  if (!opfParser.cssFiles.empty()) {
    cssFiles = opfParser.cssFiles;
  }

  LOG_DBG("EBP", "Successfully parsed content.opf");
  return true;
}

bool Epub::parseTocNcxFile(const StreamCancelCallback shouldCancel, void* cancelContext) const {
  // the ncx file should have been specified in the content.opf file
  if (tocNcxItem.empty()) {
    LOG_DBG("EBP", "No ncx file specified");
    return false;
  }

  LOG_DBG("EBP", "Parsing toc ncx file: %s", tocNcxItem.c_str());

  const auto tmpNcxPath = getCachePath() + "/toc.ncx";
  FsFile tempNcxFile;
  if (!Storage.openFileForWrite("EBP", tmpNcxPath, tempNcxFile)) {
    return false;
  }
  if (!readItemContentsToStream(tocNcxItem, tempNcxFile, 1024, shouldCancel, cancelContext)) {
    tempNcxFile.close();
    Storage.remove(tmpNcxPath.c_str());
    return false;
  }
  // Explicitly close() file before reopening for reading
  tempNcxFile.close();
  if (!Storage.openFileForRead("EBP", tmpNcxPath, tempNcxFile)) {
    return false;
  }
  const auto ncxSize = tempNcxFile.size();

  TocNcxParser ncxParser(contentBasePath, ncxSize, bookMetadataCache.get());

  if (!ncxParser.setup()) {
    LOG_ERR("EBP", "Could not setup toc ncx parser");
    return false;
  }

  const auto ncxBuffer = static_cast<uint8_t*>(malloc(1024));
  if (!ncxBuffer) {
    LOG_ERR("EBP", "Could not allocate memory for toc ncx parser");
    return false;
  }

  while (tempNcxFile.available()) {
    if (shouldCancel && shouldCancel(cancelContext)) {
      LOG_DBG("EBP", "TOC NCX parse cancelled");
      free(ncxBuffer);
      tempNcxFile.close();
      Storage.remove(tmpNcxPath.c_str());
      return false;
    }
    const auto readSize = tempNcxFile.read(ncxBuffer, 1024);
    if (readSize == 0) break;
    const auto processedSize = ncxParser.write(ncxBuffer, readSize);

    if (processedSize != readSize) {
      LOG_ERR("EBP", "Could not process all toc ncx data");
      free(ncxBuffer);
      return false;
    }
  }

  free(ncxBuffer);
  // Explicitly close() file before calling Storage.remove()
  tempNcxFile.close();
  Storage.remove(tmpNcxPath.c_str());

  LOG_DBG("EBP", "Parsed TOC items");
  return true;
}

bool Epub::parseTocNavFile(const StreamCancelCallback shouldCancel, void* cancelContext) const {
  // the nav file should have been specified in the content.opf file (EPUB 3)
  if (tocNavItem.empty()) {
    LOG_DBG("EBP", "No nav file specified");
    return false;
  }

  LOG_DBG("EBP", "Parsing toc nav file: %s", tocNavItem.c_str());

  const auto tmpNavPath = getCachePath() + "/toc.nav";
  FsFile tempNavFile;
  if (!Storage.openFileForWrite("EBP", tmpNavPath, tempNavFile)) {
    return false;
  }
  if (!readItemContentsToStream(tocNavItem, tempNavFile, 1024, shouldCancel, cancelContext)) {
    tempNavFile.close();
    Storage.remove(tmpNavPath.c_str());
    return false;
  }
  // Explicitly close() file before reopening for reading
  tempNavFile.close();
  if (!Storage.openFileForRead("EBP", tmpNavPath, tempNavFile)) {
    return false;
  }
  const auto navSize = tempNavFile.size();

  // Note: We can't use `contentBasePath` here as the nav file may be in a different folder to the content.opf
  // and the HTMLX nav file will have hrefs relative to itself
  const std::string navContentBasePath = tocNavItem.substr(0, tocNavItem.find_last_of('/') + 1);
  TocNavParser navParser(navContentBasePath, navSize, bookMetadataCache.get());

  if (!navParser.setup()) {
    LOG_ERR("EBP", "Could not setup toc nav parser");
    return false;
  }

  const auto navBuffer = static_cast<uint8_t*>(malloc(1024));
  if (!navBuffer) {
    LOG_ERR("EBP", "Could not allocate memory for toc nav parser");
    return false;
  }

  while (tempNavFile.available()) {
    if (shouldCancel && shouldCancel(cancelContext)) {
      LOG_DBG("EBP", "TOC nav parse cancelled");
      free(navBuffer);
      tempNavFile.close();
      Storage.remove(tmpNavPath.c_str());
      return false;
    }
    const auto readSize = tempNavFile.read(navBuffer, 1024);
    const auto processedSize = navParser.write(navBuffer, readSize);

    if (processedSize != readSize) {
      LOG_ERR("EBP", "Could not process all toc nav data");
      free(navBuffer);
      return false;
    }
  }

  free(navBuffer);
  // Explicitly close() file before calling Storage.remove()
  tempNavFile.close();
  Storage.remove(tmpNavPath.c_str());

  LOG_DBG("EBP", "Parsed TOC nav items");
  return true;
}

void Epub::discoverCssFilesFromZip() {
  const std::string& opfDir = contentBasePath;
  ZipFile zf(filepath);

  if (!zf.enumerateFilePaths([&](std::string_view filePath) {
        if (!opfDir.empty() && filePath.find(opfDir) != 0) {
          return;
        }

        if (!FsHelpers::hasCssExtension(filePath)) {
          return;
        }

        if (std::find(cssFiles.begin(), cssFiles.end(), filePath) != cssFiles.end()) {
          return;
        }

        LOG_DBG("EBP", "Discovered CSS file via ZIP enumeration: %.*s", (int)filePath.size(), filePath.data());
        cssFiles.push_back(std::string{filePath});
      })) {
    LOG_ERR("EBP", "Failed to enumerate ZIP file paths for CSS discovery");
  }
}

Epub::CssParseStatus Epub::parseCssFiles(const bool forceRebuild, const StreamCancelCallback shouldCancel,
                                         void* cancelContext) const {
  // Maximum CSS file size we'll attempt to parse (uncompressed)
  // Larger files risk memory exhaustion on ESP32
  constexpr size_t MAX_CSS_FILE_SIZE = 128 * 1024;  // 128KB
  // Minimum heap required before attempting CSS parsing
  constexpr size_t MIN_HEAP_FOR_CSS_PARSING = 64 * 1024;  // 64KB

  if (cssFiles.empty()) {
    LOG_DBG("EBP", "No CSS files to parse, but CssParser created for inline styles");
  }

  LOG_DBG("EBP", "CSS files to parse: %zu", cssFiles.size());

  // See if we have a usable cached version of the CSS rules. File existence alone is not enough:
  // stale cache formats are removed by loadFromCache(), then rebuilt below.
  if (cssParser->hasCache() && !forceRebuild) {
    if (cssParser->loadFromCache()) {
      const bool partialCache = cssParser->isCachePartial();
      LOG_DBG("EBP", "CSS cache valid, skipping parseCssFiles");
      cssParser->clear();
      return partialCache ? CssParseStatus::Partial : CssParseStatus::Complete;
    }
    LOG_DBG("EBP", "CSS cache invalid, rebuilding CSS rules");
    cssParser->clear();
  }

  // No cache yet - parse CSS files. If memory runs out partway through, keep
  // the rules already parsed and persist them as a marked partial cache so
  // chapter layout can still use most of the book's stylesheet.
  bool parsedAllCss = true;
  size_t parsedCssFileCount = 0;
  size_t failedCssFileIndex = 0;
  std::string failedCssPath;
  for (size_t cssFileIndex = 0; cssFileIndex < cssFiles.size(); ++cssFileIndex) {
    if (shouldCancel && shouldCancel(cancelContext)) {
      LOG_DBG("EBP", "CSS parse cancelled");
      return CssParseStatus::Failed;
    }
    const auto& cssPath = cssFiles[cssFileIndex];
    LOG_DBG("EBP", "Parsing CSS file: %s", cssPath.c_str());

    // Check heap before parsing - CSS parsing allocates heavily
    const uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < MIN_HEAP_FOR_CSS_PARSING) {
      LOG_ERR("EBP", "Insufficient heap for CSS parsing (%u bytes free, need %zu), skipping: %s", freeHeap,
              MIN_HEAP_FOR_CSS_PARSING, cssPath.c_str());
      parsedAllCss = false;
      failedCssFileIndex = cssFileIndex + 1;
      failedCssPath = cssPath;
      break;
    }

    // Check CSS file size before decompressing - skip files that are too large
    size_t cssFileSize = 0;
    if (getItemSize(cssPath, &cssFileSize)) {
      if (cssFileSize > MAX_CSS_FILE_SIZE) {
        LOG_ERR("EBP", "CSS file too large (%zu bytes > %zu max), skipping: %s", cssFileSize, MAX_CSS_FILE_SIZE,
                cssPath.c_str());
        continue;
      }
    }

    // Extract CSS file to temp location
    const auto tmpCssPath = getCachePath() + "/.tmp.css";
    FsFile tempCssFile;
    if (!Storage.openFileForWrite("EBP", tmpCssPath, tempCssFile)) {
      LOG_ERR("EBP", "Could not create temp CSS file");
      continue;
    }
    if (!readItemContentsToStream(cssPath, tempCssFile, 1024, shouldCancel, cancelContext)) {
      LOG_ERR("EBP", "Could not read CSS file: %s", cssPath.c_str());
      // Explicitly close() file before calling Storage.remove()
      tempCssFile.close();
      Storage.remove(tmpCssPath.c_str());
      continue;
    }
    // Explicitly close() file before reopening for reading
    tempCssFile.close();

    // Parse the CSS file
    if (!Storage.openFileForRead("EBP", tmpCssPath, tempCssFile)) {
      LOG_ERR("EBP", "Could not open temp CSS file for reading");
      Storage.remove(tmpCssPath.c_str());
      continue;
    }
    if (!cssParser->loadFromStream(tempCssFile)) {
      failedCssFileIndex = cssFileIndex + 1;
      failedCssPath = cssPath;
      LOG_ERR("EBP", "CSS parsing failed for file %zu/%zu after %zu parsed files: %s", failedCssFileIndex,
              cssFiles.size(), parsedCssFileCount, cssPath.c_str());
      parsedAllCss = false;
    } else {
      ++parsedCssFileCount;
    }
    // Explicitly close() file before calling Storage.remove()
    tempCssFile.close();
    Storage.remove(tmpCssPath.c_str());
    if (!parsedAllCss) {
      break;
    }
  }

  if (!parsedAllCss && cssParser->empty()) {
    LOG_ERR("EBP", "CSS parsing failed for %s before any usable rules were loaded; CSS cache will not be written",
            failedCssPath.empty() ? "<unknown>" : failedCssPath.c_str());
    cssParser->clear();
    return CssParseStatus::Failed;
  }

  if (!parsedAllCss) {
    LOG_ERR("EBP", "Saving %zu partial CSS rules after parse stopped in %s", cssParser->ruleCount(),
            failedCssPath.empty() ? "<unknown>" : failedCssPath.c_str());
  }

  // Save to cache for next time
  if (!cssParser->saveToCache(parsedAllCss)) {
    LOG_ERR("EBP", "Failed to save CSS rules to cache");
    cssParser->clear();
    return CssParseStatus::Failed;
  }

  LOG_DBG("EBP", "Loaded %zu %s CSS style rules from %zu/%zu files", cssParser->ruleCount(),
          parsedAllCss ? "complete" : "partial", parsedCssFileCount, cssFiles.size());
  cssParser->clear();
  return parsedAllCss ? CssParseStatus::Complete : CssParseStatus::Partial;
}

// load in the meta data for the epub file
bool Epub::load(const bool buildIfMissing, const bool skipLoadingCss, const StreamCancelCallback shouldCancel,
                void* cancelContext) {
  LOG_DBG("EBP", "Loading ePub: %s", filepath.c_str());
  if (shouldCancel && shouldCancel(cancelContext)) {
    LOG_DBG("EBP", "EPUB load cancelled before cache setup");
    return false;
  }

  // Initialize spine/TOC cache
  bookMetadataCache = makeBookMetadataCacheNoThrow(cachePath);
  if (!bookMetadataCache) {
    return false;
  }
  // Always create CssParser - needed for inline style parsing even without CSS files
  cssParser = makeCssParserNoThrow(cachePath);
  if (!cssParser) {
    bookMetadataCache.reset();
    return false;
  }

  // Try to load existing cache first
  if (bookMetadataCache->load()) {
    if (!skipLoadingCss) {
      // Rebuild CSS cache when missing or when cache version changed. The open
      // path only needs the header (exists/version/partial), so peek instead of
      // hydrating and discarding the full rule set; sections hydrate on demand.
      bool rebuildCssCache = false;
      bool forceCssRebuild = false;
      bool retryingPartialCssCache = false;
      bool cssCachePartial = false;
      bool cssCachePartialRuleCap = false;
      if (!cssParser->hasCache()) {
        LOG_DBG("EBP", "CSS rules cache missing, attempting to parse CSS files");
        rebuildCssCache = true;
      } else if (!cssParser->peekCacheStatus(cssCachePartial, cssCachePartialRuleCap)) {
        LOG_DBG("EBP", "CSS rules cache stale, attempting to parse CSS files");
        cssParser->deleteCache();
        rebuildCssCache = true;
      } else if (cssCachePartial && cssCachePartialRuleCap) {
        // The parse stopped at the deterministic MAX_RULES cap; re-parsing the
        // same stylesheets re-fails identically, so keep the usable subset
        // instead of paying a multi-second rebuild on every open.
        LOG_DBG("EBP", "CSS rules cache partial due to rule cap; keeping cached subset");
      } else if (cssCachePartial) {
        LOG_DBG("EBP", "CSS rules cache is partial, attempting to rebuild complete CSS cache");
        rebuildCssCache = true;
        forceCssRebuild = true;
        retryingPartialCssCache = true;
      }

      if (rebuildCssCache) {
        BookMetadataCache::BookMetadata cachedMetadata = bookMetadataCache->coreMetadata;
        if (!parseContentOpf(cachedMetadata, /*writeSpineEntries=*/false, shouldCancel, cancelContext)) {
          LOG_ERR("EBP", "Could not parse content.opf from cached bookMetadata for CSS files");
          // continue anyway - book will work without CSS and we'll still load any inline style CSS
        } else {
          discoverCssFilesFromZip();
        }
        bookMetadataCache.reset();
        const CssParseStatus cssStatus = parseCssFiles(forceCssRebuild, shouldCancel, cancelContext);
        bookMetadataCache = makeBookMetadataCacheNoThrow(cachePath);
        if (!bookMetadataCache) {
          return false;
        }
        if (!bookMetadataCache->load()) {
          LOG_ERR("EBP", "Failed to reload cache after CSS rebuild");
          return false;
        }
        if (cssStatus == CssParseStatus::Complete ||
            (cssStatus == CssParseStatus::Partial && !retryingPartialCssCache)) {
          // Invalidate section caches so they are rebuilt with the new CSS.
          setupLayoutCacheDir();
          Storage.removeDir((layoutCachePath + "/sections").c_str());
        } else if (cssStatus == CssParseStatus::Partial) {
          LOG_ERR("EBP", "CSS cache is still partial after rebuild; preserving existing section caches");
        } else {
          LOG_ERR("EBP", "CSS cache rebuild failed; preserving existing section caches");
        }
      }
    }
    loadXLocations();
    LOG_DBG("EBP", "Loaded ePub: %s", filepath.c_str());
    return true;
  }

  // If we didn't load from cache above and we aren't allowed to build, fail now
  if (!buildIfMissing) {
    return false;
  }

  // Cache doesn't exist or is invalid, build it
  LOG_DBG("EBP", "Cache not found, building spine/TOC cache");
  setupCacheDir();

  const uint32_t indexingStart = millis();

  // Begin building cache - stream entries to disk immediately
  if (shouldCancel && shouldCancel(cancelContext)) {
    LOG_DBG("EBP", "EPUB load cancelled before cache build");
    return false;
  }

  if (!bookMetadataCache->beginWrite()) {
    LOG_ERR("EBP", "Could not begin writing cache");
    return false;
  }

  // OPF Pass
  const uint32_t opfStart = millis();
  BookMetadataCache::BookMetadata bookMetadata;
  if (!bookMetadataCache->beginContentOpfPass()) {
    LOG_ERR("EBP", "Could not begin writing content.opf pass");
    return false;
  }
  if (!parseContentOpf(bookMetadata, true, shouldCancel, cancelContext)) {
    LOG_ERR("EBP", "Could not parse content.opf");
    return false;
  }
  discoverCssFilesFromZip();
  if (!bookMetadataCache->endContentOpfPass()) {
    LOG_ERR("EBP", "Could not end writing content.opf pass");
    return false;
  }
  LOG_DBG("EBP", "OPF pass completed in %lu ms", millis() - opfStart);

  // TOC Pass - try EPUB 3 nav first, fall back to NCX
  const uint32_t tocStart = millis();
  if (!bookMetadataCache->beginTocPass()) {
    LOG_ERR("EBP", "Could not begin writing toc pass");
    return false;
  }

  bool tocParsed = false;

  // Try EPUB 3 nav document first (preferred)
  if (!tocNavItem.empty()) {
    LOG_DBG("EBP", "Attempting to parse EPUB 3 nav document");
    tocParsed = parseTocNavFile(shouldCancel, cancelContext);
  }

  // Fall back to NCX if nav parsing failed or wasn't available
  if (!tocParsed && !tocNcxItem.empty()) {
    LOG_DBG("EBP", "Falling back to NCX TOC");
    tocParsed = parseTocNcxFile(shouldCancel, cancelContext);
  }

  if (!tocParsed) {
    LOG_ERR("EBP", "Warning: Could not parse any TOC format");
    // Continue anyway - book will work without TOC
  }

  if (!bookMetadataCache->endTocPass()) {
    LOG_ERR("EBP", "Could not end writing toc pass");
    return false;
  }
  LOG_DBG("EBP", "TOC pass completed in %lu ms", millis() - tocStart);

  // Close the cache files
  if (!bookMetadataCache->endWrite()) {
    LOG_ERR("EBP", "Could not end writing cache");
    return false;
  }

  // Build final book.bin
  const uint32_t buildStart = millis();
  if (!bookMetadataCache->buildBookBin(filepath, bookMetadata, shouldCancel, cancelContext)) {
    LOG_ERR("EBP", "Could not update mappings and sizes");
    return false;
  }
  LOG_DBG("EBP", "buildBookBin completed in %lu ms", millis() - buildStart);
  LOG_DBG("EBP", "Total indexing completed in %lu ms", millis() - indexingStart);

  if (!bookMetadataCache->cleanupTmpFiles()) {
    LOG_DBG("EBP", "Could not cleanup tmp files - ignoring");
  }

  if (!skipLoadingCss) {
    // Parse CSS before reloading book.bin to keep heap as open as possible for rule-table growth.
    bookMetadataCache.reset();
    if (parseCssFiles(false, shouldCancel, cancelContext) != CssParseStatus::Failed) {
      setupLayoutCacheDir();
      Storage.removeDir((layoutCachePath + "/sections").c_str());
    } else {
      LOG_ERR("EBP", "CSS cache build failed; leaving any existing section caches in place");
    }
  }

  // Reload the cache from disk so it's in the correct state
  bookMetadataCache = makeBookMetadataCacheNoThrow(cachePath);
  if (!bookMetadataCache) {
    return false;
  }
  if (!bookMetadataCache->load()) {
    LOG_ERR("EBP", "Failed to reload cache after writing");
    return false;
  }

  loadXLocations();
  LOG_DBG("EBP", "Loaded ePub: %s", filepath.c_str());
  return true;
}

bool Epub::clearCache() const {
  const bool hasMetadataCache = Storage.exists(cachePath.c_str());
  const bool hasLayoutCache = layoutCachePath != cachePath && Storage.exists(layoutCachePath.c_str());
  if (!hasMetadataCache && !hasLayoutCache) {
    LOG_DBG("EPB", "Cache does not exist, no action needed");
    return true;
  }

  bool success = true;
  if (hasMetadataCache && !Storage.removeDir(cachePath.c_str())) {
    LOG_ERR("EPB", "Failed to clear cache");
    success = false;
  }
  if (hasLayoutCache && !Storage.removeDir(layoutCachePath.c_str())) {
    LOG_ERR("EPB", "Failed to clear sharded reader-layout cache");
    success = false;
  }

  if (success) LOG_DBG("EPB", "Cache cleared successfully");
  return success;
}

void Epub::setupCacheDir() const {
  if (Storage.exists(cachePath.c_str())) {
    return;
  }

  Storage.mkdir(cachePath.c_str());
}

void Epub::setupLayoutCacheDir() const {
  if (layoutCachePrepared) return;
  if (layoutCachePath == cachePath) {
    setupCacheDir();
    layoutCachePrepared = true;
    return;
  }

  const std::string hash = thumbShardHashFromCachePath(cachePath);
  ensureLayoutShardDir(hash);
  Storage.mkdir(layoutCachePath.c_str());

  // Preserve already-built chapters on first reader use. Renaming the two
  // disposable directories is constant work and keeps old section files
  // usable; their serialized image paths may continue pointing at the legacy
  // per-book directory, which remains in place for durable state.
  for (const char* child : {"sections", "html"}) {
    const std::string oldPath = cachePath + "/" + child;
    const std::string newPath = layoutCachePath + "/" + child;
    if (!Storage.exists(newPath.c_str()) && Storage.exists(oldPath.c_str())) {
      if (!Storage.rename(oldPath.c_str(), newPath.c_str())) {
        LOG_DBG("EBP", "Could not migrate reader cache %s -> %s; it will rebuild", oldPath.c_str(), newPath.c_str());
      }
    }
  }

  layoutCachePrepared = true;
}

const std::string& Epub::getCachePath() const { return cachePath; }

const std::string& Epub::getLayoutCachePath() const { return layoutCachePath; }

const std::string& Epub::getPath() const { return filepath; }

const std::string& Epub::getTitle() const {
  static std::string blank;
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return blank;
  }

  return bookMetadataCache->coreMetadata.title;
}

const std::string& Epub::getAuthor() const {
  static std::string blank;
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return blank;
  }

  return bookMetadataCache->coreMetadata.author;
}

const std::string& Epub::getLanguage() const {
  static std::string blank;
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return blank;
  }

  return bookMetadataCache->coreMetadata.language;
}

std::string Epub::getCoverBmpPath(bool cropped) const {
  const auto coverFileName = std::string("cover") + (cropped ? "_crop" : "");
  return cachePath + "/" + coverFileName + ".bmp";
}

bool Epub::generateCoverBmp(bool cropped, const GfxRenderer* renderer, const int readerFontId) const {
  // Already generated, return true
  if (Storage.existsForRead(getCoverBmpPath(cropped))) {
    return true;
  }

  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR("EBP", "Cannot generate cover BMP, cache not loaded");
    return false;
  }

  const auto coverImageHref = bookMetadataCache->coreMetadata.coverItemHref;
  if (coverImageHref.empty()) {
    LOG_ERR("EBP", "No known cover image");
    return false;
  }

  if (FsHelpers::hasJpgExtension(coverImageHref)) {
    LOG_DBG("EBP", "Generating BMP from JPG cover image (%s mode)", cropped ? "cropped" : "fit");
    std::string coverJpgPath;
    if (!ensureCachedCoverImage(coverImageHref, coverJpgPath)) {
      return false;
    }

    FsFile coverJpg;
    if (!Storage.openFileForRead("EBP", coverJpgPath, coverJpg)) {
      return false;
    }

    FsFile coverBmp;
    if (!Storage.openFileForWrite("EBP", getCoverBmpPath(cropped), coverBmp)) {
      coverJpg.close();
      return false;
    }
    releaseReaderSdFontCachesBeforeCoverDecode(renderer, readerFontId, "cover JPG decode");
    const bool success = JpegToBmpConverter::jpegFileToBmpStream(coverJpg, coverBmp, cropped);
    // Explicitly close() files before leaving the converter path.
    coverJpg.close();
    coverBmp.close();

    if (!success) {
      LOG_ERR("EBP", "Failed to generate BMP from cover image");
      Storage.remove(getCoverBmpPath(cropped).c_str());
    }
    LOG_DBG("EBP", "Generated BMP from JPG cover image, success: %s", success ? "yes" : "no");
    return success;
  }

  if (FsHelpers::hasPngExtension(coverImageHref)) {
    LOG_DBG("EBP", "Generating BMP from PNG cover image (%s mode)", cropped ? "cropped" : "fit");
    std::string coverPngPath;
    if (!ensureCachedCoverImage(coverImageHref, coverPngPath)) {
      return false;
    }

    FsFile coverPng;
    if (!Storage.openFileForRead("EBP", coverPngPath, coverPng)) {
      return false;
    }

    FsFile coverBmp;
    if (!Storage.openFileForWrite("EBP", getCoverBmpPath(cropped), coverBmp)) {
      coverPng.close();
      return false;
    }
    releaseReaderSdFontCachesBeforeCoverDecode(renderer, readerFontId, "cover PNG decode");
    const bool success = PngToBmpConverter::pngFileToBmpStream(coverPng, coverBmp, cropped);
    // Explicitly close() files before leaving the converter path.
    coverPng.close();
    coverBmp.close();

    if (!success) {
      LOG_ERR("EBP", "Failed to generate BMP from PNG cover image");
      Storage.remove(getCoverBmpPath(cropped).c_str());
    }
    LOG_DBG("EBP", "Generated BMP from PNG cover image, success: %s", success ? "yes" : "no");
    return success;
  }

  LOG_ERR("EBP", "Cover image is not a supported format, skipping");
  return false;
}

std::string Epub::getThumbBmpPath() const {
  const std::string hash = thumbShardHashFromCachePath(cachePath);
  return thumbShardDirForHash(hash) + "/" + hash + "_[WIDTH]x[HEIGHT].bmp";
}
std::string Epub::thumbBmpPathForCache(const std::string& cachePath, int width, int height) {
  normalizeThumbDimensions(width, height);
  return getThumbBmpPathForDimensions(cachePath, width, height);
}
std::string Epub::getThumbBmpPath(int height) const { return getThumbBmpPath(0, height); }
std::string Epub::getThumbBmpPath(int width, int height) const {
  normalizeThumbDimensions(width, height);
  const std::string newPath = getThumbBmpPathForDimensions(cachePath, width, height);
  if (Storage.exists(newPath.c_str())) {
    return newPath;
  }
  const std::string hash = thumbShardHashFromCachePath(cachePath);
  const std::string oldShardPath = ThumbnailCache::legacyPathForHash(hash, width, height);
  if (oldShardPath != newPath && migrateLegacyThumb(cachePath, oldShardPath, newPath)) {
    return newPath;
  }
  const std::string cacheDirPath =
      cachePath + "/thumb_" + std::to_string(width) + "x" + std::to_string(height) + ".bmp";
  if (migrateLegacyThumb(cachePath, cacheDirPath, newPath)) {
    return newPath;
  }
  if (pathExistsMemoized(cachePath)) {
    if (Storage.exists(cacheDirPath.c_str())) {
      return cacheDirPath;
    }
    const std::string legacyPath = cachePath + "/thumb_" + std::to_string(height) + ".bmp";
    if (Storage.exists(legacyPath.c_str())) {
      return legacyPath;
    }
  }
  return newPath;
}

std::string Epub::getAdaptiveThumbBmpPath(int width, int height) const {
  normalizeThumbDimensions(width, height);
  const std::string shardedPath = getAdaptiveThumbBmpPathForDimensions(cachePath, width, height);
  if (Storage.exists(shardedPath.c_str())) {
    return shardedPath;
  }
  const std::string hash = thumbShardHashFromCachePath(cachePath);
  const std::string oldShardPath = ThumbnailCache::legacyPathForHash(hash, width, height, true);
  if (oldShardPath != shardedPath && migrateLegacyThumb(cachePath, oldShardPath, shardedPath)) {
    return shardedPath;
  }
  const std::string cacheDirPath =
      cachePath + "/thumb_" + std::to_string(width) + "x" + std::to_string(height) + "_fit.bmp";
  if (migrateLegacyThumb(cachePath, cacheDirPath, shardedPath)) {
    return shardedPath;
  }
  if (pathExistsMemoized(cachePath) && Storage.exists(cacheDirPath.c_str())) {
    return cacheDirPath;
  }
  return shardedPath;
}

bool Epub::generateThumbBmp(int height, const GfxRenderer* renderer, const int readerFontId) const {
  return generateThumbBmp(0, height, renderer, readerFontId);
}

bool Epub::generateThumbBmp(int width, int height, const GfxRenderer* renderer, const int readerFontId,
                            const StreamCancelCallback shouldCancel, void* cancelContext) const {
  return generateThumbBmpInternal(width, height, false, renderer, readerFontId, shouldCancel, cancelContext);
}

bool Epub::generateAdaptiveThumbBmp(int width, int height, const GfxRenderer* renderer, const int readerFontId,
                                    const StreamCancelCallback shouldCancel, void* cancelContext) const {
  return generateThumbBmpInternal(width, height, true, renderer, readerFontId, shouldCancel, cancelContext);
}

std::string Epub::getCachedCoverImagePath(const std::string& coverImageHref) const {
  if (FsHelpers::hasJpgExtension(coverImageHref)) {
    return getCachePath() + "/cover_src.jpg";
  }
  if (FsHelpers::hasPngExtension(coverImageHref)) {
    return getCachePath() + "/cover_src.png";
  }
  return {};
}

bool Epub::ensureCachedCoverImage(const std::string& coverImageHref, std::string& outPath,
                                  const StreamCancelCallback shouldCancel, void* cancelContext) const {
  outPath = getCachedCoverImagePath(coverImageHref);
  if (outPath.empty()) {
    LOG_ERR("EBP", "Cover image is not a supported format, cannot cache source");
    return false;
  }
  if (Storage.exists(outPath.c_str())) {
    return true;
  }

  const std::string tmpPath = outPath + ".tmp";
  if (Storage.exists(tmpPath.c_str())) {
    Storage.remove(tmpPath.c_str());
  }

  FsFile coverFile;
  if (!Storage.openFileForWrite("EBP", tmpPath, coverFile)) {
    return false;
  }
  if (!readItemContentsToStream(coverImageHref, coverFile, 1024, shouldCancel, cancelContext)) {
    LOG_ERR("EBP", "Failed to cache cover image item: %s", coverImageHref.c_str());
    coverFile.close();
    Storage.remove(tmpPath.c_str());
    return false;
  }
  coverFile.close();

  if (Storage.exists(outPath.c_str())) {
    Storage.remove(outPath.c_str());
  }
  if (!Storage.rename(tmpPath.c_str(), outPath.c_str())) {
    LOG_ERR("EBP", "Failed to finalize cached cover image: %s", outPath.c_str());
    Storage.remove(tmpPath.c_str());
    return false;
  }

  LOG_DBG("EBP", "Cached cover image source: %s", outPath.c_str());
  return true;
}

bool Epub::generateThumbBmpInternal(int width, int height, const bool adaptiveContain, const GfxRenderer* renderer,
                                    const int readerFontId, const StreamCancelCallback shouldCancel,
                                    void* cancelContext) const {
  if (height <= 0) {
    LOG_DBG("EBP", "Using default thumb BMP height for requested dimensions: %dx%d", width, height);
  }
  normalizeThumbDimensions(width, height);
  if (shouldCancel && shouldCancel(cancelContext)) {
    LOG_DBG("EBP", "Thumbnail generation cancelled before cache probe");
    return false;
  }
  ensureThumbShardDir(thumbShardHashFromCachePath(cachePath));
  const std::string thumbPath = adaptiveContain ? getAdaptiveThumbBmpPathForDimensions(cachePath, width, height)
                                                : getThumbBmpPathForDimensions(cachePath, width, height);

  // Already generated with matching dimensions, return true
  if (cachedBmpMatchesDimensions(thumbPath, width, height, adaptiveContain)) {
    return true;
  }

  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR("EBP", "Cannot generate thumb BMP, cache not loaded");
    return false;
  }

  const auto coverImageHref = bookMetadataCache->coreMetadata.coverItemHref;
  if (coverImageHref.empty()) {
    LOG_DBG("EBP", "No known cover image for thumbnail");
  } else if (FsHelpers::hasJpgExtension(coverImageHref)) {
    LOG_DBG("EBP", "Generating thumb BMP from JPG cover image");
    std::string coverJpgPath;
    if (!ensureCachedCoverImage(coverImageHref, coverJpgPath, shouldCancel, cancelContext)) {
      return false;
    }

    FsFile coverJpg;
    if (!Storage.openFileForRead("EBP", coverJpgPath, coverJpg)) {
      return false;
    }

    FsFile thumbBmp;
    if (!Storage.openFileForWrite("EBP", thumbPath, thumbBmp)) {
      coverJpg.close();
      return false;
    }
    int THUMB_TARGET_WIDTH = width;
    int THUMB_TARGET_HEIGHT = height;
    releaseReaderSdFontCachesBeforeCoverDecode(renderer, readerFontId, "thumbnail JPG decode");
    const bool success = JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(coverJpg, thumbBmp, THUMB_TARGET_WIDTH,
                                                                             THUMB_TARGET_HEIGHT, adaptiveContain,
                                                                             shouldCancel, cancelContext);
    // Explicitly close() files before leaving the converter path.
    coverJpg.close();
    thumbBmp.close();

    if (!success) {
      LOG_ERR("EBP", "Failed to generate thumb BMP from JPG cover image");
      Storage.remove(thumbPath.c_str());
    }
    LOG_DBG("EBP", "Generated thumb BMP from JPG cover image, success: %s", success ? "yes" : "no");
    return success;
  } else if (FsHelpers::hasPngExtension(coverImageHref)) {
    LOG_DBG("EBP", "Generating thumb BMP from PNG cover image");
    std::string coverPngPath;
    if (!ensureCachedCoverImage(coverImageHref, coverPngPath, shouldCancel, cancelContext)) {
      return false;
    }

    FsFile coverPng;
    if (!Storage.openFileForRead("EBP", coverPngPath, coverPng)) {
      return false;
    }

    FsFile thumbBmp;
    if (!Storage.openFileForWrite("EBP", thumbPath, thumbBmp)) {
      coverPng.close();
      return false;
    }
    int THUMB_TARGET_WIDTH = width;
    int THUMB_TARGET_HEIGHT = height;
    releaseReaderSdFontCachesBeforeCoverDecode(renderer, readerFontId, "thumbnail PNG decode");
    const bool success = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(coverPng, thumbBmp, THUMB_TARGET_WIDTH,
                                                                           THUMB_TARGET_HEIGHT, adaptiveContain,
                                                                           shouldCancel, cancelContext);
    // Explicitly close() files before leaving the converter path.
    coverPng.close();
    thumbBmp.close();

    if (!success) {
      LOG_ERR("EBP", "Failed to generate thumb BMP from PNG cover image");
      Storage.remove(thumbPath.c_str());
    }
    LOG_DBG("EBP", "Generated thumb BMP from PNG cover image, success: %s", success ? "yes" : "no");
    return success;
  } else {
    LOG_ERR("EBP", "Cover image is not a supported format, skipping thumbnail");
  }

  return false;
}

uint8_t* Epub::readItemContentsToBytes(const std::string& itemHref, size_t* size, const bool trailingNullByte) const {
  if (itemHref.empty()) {
    LOG_DBG("EBP", "Failed to read item, empty href");
    return nullptr;
  }

  const std::string path = FsHelpers::normalisePath(itemHref);

  const auto content = ZipFile(filepath).readFileToMemory(path.c_str(), size, trailingNullByte);
  if (!content) {
    LOG_DBG("EBP", "Failed to read item %s", path.c_str());
    return nullptr;
  }

  return content;
}

bool Epub::readItemContentsToStream(const std::string& itemHref, Print& out, const size_t chunkSize,
                                    const StreamCancelCallback shouldCancel, void* cancelContext) const {
  if (itemHref.empty()) {
    LOG_DBG("EBP", "Failed to read item, empty href");
    return false;
  }

  const std::string path = FsHelpers::normalisePath(itemHref);
  return ZipFile(filepath).readFileToStream(path.c_str(), out, chunkSize, shouldCancel, cancelContext);
}

bool Epub::getItemSize(const std::string& itemHref, size_t* size) const {
  const std::string path = FsHelpers::normalisePath(itemHref);
  return ZipFile(filepath).getInflatedFileSize(path.c_str(), size);
}

bool Epub::loadXLocations() {
  locationSpine.clear();
  totalLocations = 0;
  totalWords = 0;
  wordsPerReferencePage = 0;
  totalReferencePages = 0;
  xLocationsLoaded = false;

  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return false;
  }

  const int spineCount = getSpineItemsCount();
  if (spineCount <= 0) {
    return false;
  }

  size_t manifestSize = 0;
  if (!getItemSize(kXLocationsPath, &manifestSize)) {
    return false;
  }
  if (manifestSize == 0 || manifestSize > kXLocationsMaxBytes) {
    LOG_ERR("EBP", "Ignoring X locations manifest with unsupported size: %zu bytes", manifestSize);
    return false;
  }

  size_t bytesRead = 0;
  uint8_t* manifestData = readItemContentsToBytes(kXLocationsPath, &bytesRead, true);
  if (!manifestData) {
    LOG_ERR("EBP", "Failed to read X locations manifest");
    return false;
  }

  JsonDocument filter;
  buildXLocationsJsonFilter(filter);
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, reinterpret_cast<const char*>(manifestData), bytesRead,
                                                   DeserializationOption::Filter(filter.as<JsonVariantConst>()));
  free(manifestData);

  if (err) {
    LOG_ERR("EBP", "X locations parse error: %s", err.c_str());
    return false;
  }

  const char* format = doc["format"] | "";
  const int version = doc["version"] | 0;
  const uint32_t parsedTotalLocations = doc["totalLocations"] | 0;
  const uint32_t parsedTotalWords = doc["totalWords"] | 0;
  const uint32_t parsedTotalCharacters = doc["totalCharacters"] | 0;
  const uint32_t parsedWordsPerReferencePage = doc["wordsPerReferencePage"] | 0;
  const uint32_t parsedCharactersPerReferencePage = doc["charactersPerReferencePage"] | 0;
  const bool useCharacterReferencePages = parsedTotalCharacters > 0 && parsedCharactersPerReferencePage > 0;
  const uint32_t parsedReferenceUnits = useCharacterReferencePages ? parsedTotalCharacters : parsedTotalWords;
  const uint32_t parsedReferenceUnitsPerPage =
      useCharacterReferencePages ? parsedCharactersPerReferencePage : parsedWordsPerReferencePage;
  const uint32_t parsedTotalReferencePages = doc["totalReferencePages"] | 0;
  JsonArrayConst spine = doc["spine"];

  if (!isSupportedLocationsFormat(format) || version != 1 || parsedTotalLocations == 0 || spine.isNull()) {
    LOG_ERR("EBP", "Ignoring unsupported X locations manifest");
    return false;
  }

  locationSpine.assign(static_cast<size_t>(spineCount), {});
  bool hasValidEntry = false;
  size_t ordinal = 0;
  for (JsonObjectConst spineItem : spine) {
    const int index = spineItem["index"] | static_cast<int>(ordinal);
    ordinal++;
    if (index < 0 || index >= spineCount) {
      continue;
    }

    const uint32_t startLocation = spineItem["startLocation"] | 0;
    const uint32_t endLocation = spineItem["endLocation"] | 0;
    const uint32_t wordStart =
        useCharacterReferencePages ? (spineItem["characterStart"] | 0) : (spineItem["wordStart"] | 0);
    const uint32_t wordCount =
        useCharacterReferencePages ? (spineItem["characterCount"] | 0) : (spineItem["wordCount"] | 0);
    if (startLocation == 0 && endLocation == 0) {
      continue;
    }
    if (startLocation == 0 || endLocation < startLocation || endLocation > parsedTotalLocations) {
      LOG_ERR("EBP", "Ignoring invalid X location range at spine %d", index);
      continue;
    }

    locationSpine[static_cast<size_t>(index)] = {startLocation, endLocation, wordStart, wordCount};
    hasValidEntry = true;
  }

  if (!hasValidEntry) {
    locationSpine.clear();
    return false;
  }

  totalLocations = parsedTotalLocations;
  totalWords = parsedReferenceUnits;
  wordsPerReferencePage =
      parsedReferenceUnitsPerPage > 0 ? parsedReferenceUnitsPerPage : kDefaultReferenceCharactersPerPage;
  totalReferencePages = parsedTotalReferencePages;
  if (totalReferencePages == 0 && totalWords > 0 && wordsPerReferencePage > 0) {
    totalReferencePages = (totalWords + wordsPerReferencePage - 1) / wordsPerReferencePage;
  }
  xLocationsLoaded = true;
  LOG_INF("EBP", "Loaded X locations: %lu locations, %lu reference pages across %zu spine items",
          static_cast<unsigned long>(totalLocations), static_cast<unsigned long>(totalReferencePages),
          locationSpine.size());
  return true;
}

int Epub::getSpineItemsCount() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return 0;
  }
  return bookMetadataCache->getSpineCount();
}

size_t Epub::getCumulativeSpineItemSize(const int spineIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR("EBP", "getCumulativeSpineItemSize called but cache not loaded");
    return 0;
  }

  return bookMetadataCache->getSpineCumulativeSize(spineIndex);
}

BookMetadataCache::SpineEntry Epub::getSpineItem(const int spineIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR("EBP", "getSpineItem called but cache not loaded");
    return {};
  }

  if (spineIndex < 0 || spineIndex >= bookMetadataCache->getSpineCount()) {
    LOG_ERR("EBP", "getSpineItem index:%d is out of range", spineIndex);
    return bookMetadataCache->getSpineEntry(0);
  }

  return bookMetadataCache->getSpineEntry(spineIndex);
}

BookMetadataCache::TocEntry Epub::getTocItem(const int tocIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_DBG("EBP", "getTocItem called but cache not loaded");
    return {};
  }

  if (tocIndex < 0 || tocIndex >= bookMetadataCache->getTocCount()) {
    LOG_DBG("EBP", "getTocItem index:%d is out of range", tocIndex);
    return {};
  }

  return bookMetadataCache->getTocEntry(tocIndex);
}

int Epub::getTocItemsCount() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return 0;
  }

  return bookMetadataCache->getTocCount();
}

// work out the section index for a toc index
int Epub::getSpineIndexForTocIndex(const int tocIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR("EBP", "getSpineIndexForTocIndex called but cache not loaded");
    return 0;
  }

  if (tocIndex < 0 || tocIndex >= bookMetadataCache->getTocCount()) {
    LOG_ERR("EBP", "getSpineIndexForTocIndex: tocIndex %d out of range", tocIndex);
    return 0;
  }

  const int spineIndex = bookMetadataCache->getTocEntry(tocIndex).spineIndex;
  if (spineIndex < 0) {
    LOG_DBG("EBP", "Section not found for TOC index %d", tocIndex);
    return 0;
  }

  return spineIndex;
}

int Epub::getTocIndexForSpineIndex(const int spineIndex) const { return getSpineItem(spineIndex).tocIndex; }

size_t Epub::getBookSize() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded() || bookMetadataCache->getSpineCount() == 0) {
    return 0;
  }
  return getCumulativeSpineItemSize(getSpineItemsCount() - 1);
}

int Epub::getSpineIndexForTextReference() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR("EBP", "getSpineIndexForTextReference called but cache not loaded");
    return 0;
  }
  LOG_DBG("EBP", "Core Metadata: cover(%d)=%s, textReference(%d)=%s",
          bookMetadataCache->coreMetadata.coverItemHref.size(), bookMetadataCache->coreMetadata.coverItemHref.c_str(),
          bookMetadataCache->coreMetadata.textReferenceHref.size(),
          bookMetadataCache->coreMetadata.textReferenceHref.c_str());

  if (bookMetadataCache->coreMetadata.textReferenceHref.empty()) {
    // there was no textReference in epub, so we return 0 (the first chapter)
    return 0;
  }

  // loop through spine items to get the correct index matching the text href
  for (size_t i = 0; i < getSpineItemsCount(); i++) {
    if (getSpineItem(i).href == bookMetadataCache->coreMetadata.textReferenceHref) {
      LOG_DBG("EBP", "Text reference %s found at index %d", bookMetadataCache->coreMetadata.textReferenceHref.c_str(),
              i);
      return i;
    }
  }
  // This should not happen, as we checked for empty textReferenceHref earlier
  LOG_DBG("EBP", "Section not found for text reference");
  return 0;
}

float Epub::calculateSizeProgress(const int currentSpineIndex, const float currentSpineRead) const {
  const size_t bookSize = getBookSize();
  if (bookSize == 0) {
    return 0.0f;
  }
  const size_t prevChapterSize = (currentSpineIndex >= 1) ? getCumulativeSpineItemSize(currentSpineIndex - 1) : 0;
  const size_t curChapterSize = getCumulativeSpineItemSize(currentSpineIndex) - prevChapterSize;
  const float sectionProgSize = clampUnit(currentSpineRead) * static_cast<float>(curChapterSize);
  const float totalProgress = static_cast<float>(prevChapterSize) + sectionProgSize;
  return totalProgress / static_cast<float>(bookSize);
}

// Calculate progress in book (returns 0.0-1.0)
float Epub::calculateProgress(const int currentSpineIndex, const float currentSpineRead) const {
  if (!xLocationsLoaded || totalLocations == 0 || currentSpineIndex < 0 ||
      currentSpineIndex >= static_cast<int>(locationSpine.size())) {
    return calculateSizeProgress(currentSpineIndex, currentSpineRead);
  }

  const LocationSpineEntry& entry = locationSpine[static_cast<size_t>(currentSpineIndex)];
  if (entry.startLocation == 0 || entry.endLocation < entry.startLocation) {
    return calculateSizeProgress(currentSpineIndex, currentSpineRead);
  }

  const uint32_t locationCount = entry.endLocation - entry.startLocation + 1;
  const float completedBeforeSpine = static_cast<float>(entry.startLocation - 1);
  const float completedInSpine = clampUnit(currentSpineRead) * static_cast<float>(locationCount);
  return clampUnit((completedBeforeSpine + completedInSpine) / static_cast<float>(totalLocations));
}

bool Epub::resolveLocationPercentToSpineProgress(const int percent, int& spineIndex, float& spineProgress) const {
  if (!xLocationsLoaded || totalLocations == 0 || locationSpine.empty()) {
    return false;
  }

  const int clampedPercent = std::max(0, std::min(100, percent));
  if (clampedPercent <= 0) {
    spineIndex = 0;
    spineProgress = 0.0f;
    return true;
  }

  if (clampedPercent >= 100) {
    for (int i = static_cast<int>(locationSpine.size()) - 1; i >= 0; i--) {
      const LocationSpineEntry& entry = locationSpine[static_cast<size_t>(i)];
      if (entry.startLocation > 0 && entry.endLocation >= entry.startLocation) {
        spineIndex = i;
        spineProgress = 1.0f;
        return true;
      }
    }
    return false;
  }

  const float targetCompletedLocations =
      static_cast<float>(totalLocations) * static_cast<float>(clampedPercent) / 100.0f;
  for (size_t i = 0; i < locationSpine.size(); i++) {
    const LocationSpineEntry& entry = locationSpine[i];
    if (entry.startLocation == 0 || entry.endLocation < entry.startLocation) {
      continue;
    }

    const uint32_t locationCount = entry.endLocation - entry.startLocation + 1;
    const float completedBeforeSpine = static_cast<float>(entry.startLocation - 1);
    const float completedThroughSpine = static_cast<float>(entry.endLocation);
    if (targetCompletedLocations > completedThroughSpine) {
      continue;
    }

    spineIndex = static_cast<int>(i);
    spineProgress = clampUnit((targetCompletedLocations - completedBeforeSpine) / static_cast<float>(locationCount));
    return true;
  }

  return false;
}

bool Epub::resolveReferencePage(const int currentSpineIndex, const float currentSpineRead, uint32_t& currentPage,
                                uint32_t& pageCount) const {
  currentPage = 0;
  pageCount = 0;
  if (!xLocationsLoaded || totalWords == 0 || wordsPerReferencePage == 0 || totalReferencePages == 0 ||
      currentSpineIndex < 0 || currentSpineIndex >= static_cast<int>(locationSpine.size())) {
    return false;
  }

  const LocationSpineEntry& entry = locationSpine[static_cast<size_t>(currentSpineIndex)];
  if (entry.wordCount == 0 || entry.wordStart >= totalWords) {
    return false;
  }

  const float clampedProgress = clampUnit(currentSpineRead);
  const uint32_t completedWords =
      entry.wordStart + static_cast<uint32_t>(clampedProgress * static_cast<float>(entry.wordCount));
  currentPage = std::min<uint32_t>(completedWords / wordsPerReferencePage + 1, totalReferencePages);
  pageCount = totalReferencePages;
  return true;
}

int Epub::resolveHrefToSpineIndex(const std::string& href) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) return -1;

  // Split before decoding so escaped '#' characters in filenames stay part of the path.
  const size_t hashPos = href.find('#');
  const std::string rawTarget = hashPos != std::string::npos ? href.substr(0, hashPos) : href;
  const std::string target = FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(rawTarget));

  // Same-file reference (anchor-only)
  if (target.empty()) return -1;

  // Extract just the filename for comparison
  size_t targetSlash = target.find_last_of('/');
  std::string targetFilename = (targetSlash != std::string::npos) ? target.substr(targetSlash + 1) : target;

  for (int i = 0; i < getSpineItemsCount(); i++) {
    const auto& spineHref = getSpineItem(i).href;
    // Try exact match first
    if (spineHref == target) return i;
    // Then filename-only match
    size_t spineSlash = spineHref.find_last_of('/');
    std::string spineFilename = (spineSlash != std::string::npos) ? spineHref.substr(spineSlash + 1) : spineHref;
    if (spineFilename == targetFilename) return i;
  }
  return -1;
}
