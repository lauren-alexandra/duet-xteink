/**
 * Xtc.cpp
 *
 * Main XTC ebook class implementation
 * XTC ebook support for CrossPoint Reader
 */

#include "Xtc.h"

#include <DuetStorageMigration.h>
#include <DuetStoragePaths.h>
#include <Bitmap.h>
#include <BitmapHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <ThumbnailCache.h>

#include <algorithm>

namespace {
bool thumbnailHasDimensions(const std::string& path, const uint16_t width, const uint16_t height) {
  FsFile file;
  if (!Storage.openFileForRead("XTC", path, file)) {
    return false;
  }

  Bitmap bitmap(file);
  const bool matches =
      bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() == width && bitmap.getHeight() == height;
  file.close();
  return matches;
}
}  // namespace

bool Xtc::load() {
  LOG_DBG("XTC", "Loading XTC: %s", filepath.c_str());

  // Initialize parser
  parser.reset(new xtc::XtcParser());

  // Open XTC file
  xtc::XtcError err = parser->open(filepath.c_str());
  if (err != xtc::XtcError::OK) {
    LOG_ERR("XTC", "Failed to load: %s", xtc::errorToString(err));
    parser.reset();
    return false;
  }

  loaded = true;
  LOG_DBG("XTC", "Loaded XTC: %s (%lu pages)", filepath.c_str(), parser->getPageCount());
  return true;
}

bool Xtc::clearCache() const {
  if (!Storage.exists(cachePath.c_str())) {
    LOG_DBG("XTC", "Cache does not exist, no action needed");
    return true;
  }

  if (!Storage.removeDir(cachePath.c_str())) {
    LOG_ERR("XTC", "Failed to clear cache");
    return false;
  }

  LOG_DBG("XTC", "Cache cleared successfully");
  return true;
}

void Xtc::setupCacheDir() const {
  if (Storage.exists(cachePath.c_str())) {
    return;
  }

  // Create directories recursively
  for (size_t i = 1; i < cachePath.length(); i++) {
    if (cachePath[i] == '/') {
      Storage.mkdir(cachePath.substr(0, i).c_str());
    }
  }
  Storage.mkdir(cachePath.c_str());
}

std::string Xtc::getTitle() const {
  if (!loaded || !parser) {
    return "";
  }

  // Try to get title from XTC metadata first
  std::string title = parser->getTitle();
  if (!title.empty()) {
    return title;
  }

  // Fallback: extract filename from path as title
  size_t lastSlash = filepath.find_last_of('/');
  size_t lastDot = filepath.find_last_of('.');

  if (lastSlash == std::string::npos) {
    lastSlash = 0;
  } else {
    lastSlash++;
  }

  if (lastDot == std::string::npos || lastDot <= lastSlash) {
    return filepath.substr(lastSlash);
  }

  return filepath.substr(lastSlash, lastDot - lastSlash);
}

std::string Xtc::getAuthor() const {
  if (!loaded || !parser) {
    return "";
  }

  // Try to get author from XTC metadata
  return parser->getAuthor();
}

bool Xtc::hasChapters() const {
  if (!loaded || !parser) {
    return false;
  }
  return parser->hasChapters();
}

xtc::ChapterListView Xtc::getChapters() {
  if (!loaded || !parser) {
    return {};
  }
  return parser->getChapters();
}

std::string Xtc::getCoverBmpPath() const { return cachePath + "/cover.bmp"; }

bool Xtc::generateCoverBmp() const {
  // Already generated
  if (Storage.existsForRead(getCoverBmpPath())) {
    return true;
  }

  if (!loaded || !parser) {
    LOG_ERR("XTC", "Cannot generate cover BMP, file not loaded");
    return false;
  }

  if (parser->getPageCount() == 0) {
    LOG_ERR("XTC", "No pages in XTC file");
    return false;
  }

  // Setup cache directory
  setupCacheDir();

  // Get first page info for cover
  xtc::PageInfo pageInfo;
  if (!parser->getPageInfo(0, pageInfo)) {
    LOG_DBG("XTC", "Failed to get first page info");
    return false;
  }

  // Get bit depth
  const uint8_t bitDepth = parser->getBitDepth();

  // Allocate buffer for page data
  // XTG (1-bit): Row-major, ((width+7)/8) * height bytes
  // XTH (2-bit): Two bit planes, column-major, ((width * height + 7) / 8) * 2 bytes
  size_t bitmapSize;
  if (bitDepth == 2) {
    bitmapSize = ((static_cast<size_t>(pageInfo.width) * pageInfo.height + 7) / 8) * 2;
  } else {
    bitmapSize = ((pageInfo.width + 7) / 8) * pageInfo.height;
  }
  uint8_t* pageBuffer = static_cast<uint8_t*>(malloc(bitmapSize));
  if (!pageBuffer) {
    LOG_ERR("XTC", "Failed to allocate page buffer (%lu bytes)", bitmapSize);
    return false;
  }

  // Load first page (cover)
  size_t bytesRead = const_cast<xtc::XtcParser*>(parser.get())->loadPage(0, pageBuffer, bitmapSize);
  if (bytesRead == 0) {
    LOG_ERR("XTC", "Failed to load cover page");
    free(pageBuffer);
    return false;
  }

  // Create BMP file
  FsFile coverBmp;
  if (!Storage.openFileForWrite("XTC", getCoverBmpPath(), coverBmp)) {
    LOG_DBG("XTC", "Failed to create cover BMP file");
    free(pageBuffer);
    return false;
  }

  // Write 1-bit BMP header (top-down row order)
  BmpHeader bmpHeader;
  createBmpHeader(&bmpHeader, pageInfo.width, pageInfo.height, BmpRowOrder::TopDown);
  coverBmp.write(reinterpret_cast<const uint8_t*>(&bmpHeader), sizeof(bmpHeader));

  const uint32_t rowSize = ((pageInfo.width + 31) / 32) * 4;

  // Write bitmap data
  // BMP requires 4-byte row alignment
  const size_t dstRowSize = (pageInfo.width + 7) / 8;  // 1-bit destination row size

  if (bitDepth == 2) {
    // XTH 2-bit mode: Two bit planes, column-major order
    // - Columns scanned right to left (x = width-1 down to 0)
    // - 8 vertical pixels per byte (MSB = topmost pixel in group)
    // - First plane: Bit1, Second plane: Bit2
    // - Pixel value = (bit1 << 1) | bit2
    const size_t planeSize = (static_cast<size_t>(pageInfo.width) * pageInfo.height + 7) / 8;
    const uint8_t* plane1 = pageBuffer;                 // Bit1 plane
    const uint8_t* plane2 = pageBuffer + planeSize;     // Bit2 plane
    const size_t colBytes = (pageInfo.height + 7) / 8;  // Bytes per column

    // Allocate a row buffer for 1-bit output
    uint8_t* rowBuffer = static_cast<uint8_t*>(malloc(dstRowSize));
    if (!rowBuffer) {
      free(pageBuffer);
      return false;
    }

    for (uint16_t y = 0; y < pageInfo.height; y++) {
      memset(rowBuffer, 0xFF, dstRowSize);  // Start with all white

      for (uint16_t x = 0; x < pageInfo.width; x++) {
        // Column-major, right to left: column index = (width - 1 - x)
        const size_t colIndex = pageInfo.width - 1 - x;
        const size_t byteInCol = y / 8;
        const size_t bitInByte = 7 - (y % 8);  // MSB = topmost pixel

        const size_t byteOffset = colIndex * colBytes + byteInCol;
        const uint8_t bit1 = (plane1[byteOffset] >> bitInByte) & 1;
        const uint8_t bit2 = (plane2[byteOffset] >> bitInByte) & 1;
        const uint8_t pixelValue = (bit1 << 1) | bit2;

        // Threshold: 0=white (1); 1,2,3=black (0)
        if (pixelValue >= 1) {
          // Set bit to 0 (black) in BMP format
          const size_t dstByte = x / 8;
          const size_t dstBit = 7 - (x % 8);
          rowBuffer[dstByte] &= ~(1 << dstBit);
        }
      }

      // Write converted row
      coverBmp.write(rowBuffer, dstRowSize);

      // Pad to 4-byte boundary
      uint8_t padding[4] = {0, 0, 0, 0};
      size_t paddingSize = rowSize - dstRowSize;
      if (paddingSize > 0) {
        coverBmp.write(padding, paddingSize);
      }
    }

    free(rowBuffer);
  } else {
    // 1-bit source: write directly with proper padding
    const size_t srcRowSize = (pageInfo.width + 7) / 8;

    for (uint16_t y = 0; y < pageInfo.height; y++) {
      // Write source row
      coverBmp.write(pageBuffer + y * srcRowSize, srcRowSize);

      // Pad to 4-byte boundary
      uint8_t padding[4] = {0, 0, 0, 0};
      size_t paddingSize = rowSize - srcRowSize;
      if (paddingSize > 0) {
        coverBmp.write(padding, paddingSize);
      }
    }
  }

  free(pageBuffer);

  LOG_DBG("XTC", "Generated cover BMP: %s", getCoverBmpPath().c_str());
  return true;
}

namespace {
// Same sharded-thumbnail scheme as the EPUB cache: tiny Duet thumbnail
// shard directories instead of the crowded per-book cache root.
std::string xtcThumbHash(const std::string& cachePath) {
  const size_t slash = cachePath.find_last_of('/');
  const std::string dirName = slash == std::string::npos ? cachePath : cachePath.substr(slash + 1);
  const size_t underscore = dirName.find_last_of('_');
  return underscore == std::string::npos ? dirName : dirName.substr(underscore + 1);
}

std::string xtcThumbShardDir(const std::string& hash) {
  return ThumbnailCache::shardDirForHash(hash);
}

void xtcEnsureThumbShardDir(const std::string& hash) {
  Storage.mkdir(DuetStorage::ROOT);
  Storage.mkdir(DuetStorage::CACHE_ROOT);
  Storage.mkdir(DuetStorage::THUMBS_ROOT);
  Storage.mkdir(xtcThumbShardDir(hash).c_str());
}
}  // namespace

std::string Xtc::getThumbBmpPath() const {
  const std::string hash = xtcThumbHash(cachePath);
  return xtcThumbShardDir(hash) + "/" + hash + "_[HEIGHT].bmp";
}
std::string Xtc::getThumbBmpPath(uint16_t height) const {
  const uint16_t width = static_cast<uint16_t>(height * 0.6);
  const std::string newPath = getThumbBmpPath(width, height);
  if (Storage.existsForRead(newPath)) {
    return newPath;
  }

  const std::string cacheDirPath =
      cachePath + "/thumb_" + std::to_string(width) + "x" + std::to_string(height) + ".bmp";
  if (Storage.existsForRead(cacheDirPath)) {
    xtcEnsureThumbShardDir(xtcThumbHash(cachePath));
    if (DuetStorage::copyLegacyFileToCanonical(cacheDirPath.c_str(), newPath.c_str())) {
      return newPath;
    }
    return cacheDirPath;
  }

  const std::string legacyPath = cachePath + "/thumb_" + std::to_string(height) + ".bmp";
  if (Storage.existsForRead(legacyPath)) {
    return legacyPath;
  }

  return newPath;
}
std::string Xtc::getThumbBmpPath(uint16_t width, uint16_t height) const {
  const std::string hash = xtcThumbHash(cachePath);
  xtcEnsureThumbShardDir(hash);
  const std::string newPath = ThumbnailCache::pathForHash(hash, width, height);
  if (!Storage.existsForRead(newPath)) {
    const std::string oldPath = ThumbnailCache::legacyPathForHash(hash, width, height);
    if (oldPath != newPath && Storage.existsForRead(oldPath)) {
      DuetStorage::copyLegacyFileToCanonical(oldPath.c_str(), newPath.c_str());
    }
  }
  return newPath;
}

bool Xtc::generateThumbBmp() const {
  const uint16_t height = getPageHeight();
  return height > 0 && generateThumbBmp(height);
}

bool Xtc::generateThumbBmp(uint16_t height) const {
  return generateThumbBmp(static_cast<uint16_t>(height * 0.6), height);
}

bool Xtc::generateThumbBmp(uint16_t width, uint16_t height) const {
  if (width == 0 || height == 0) {
    LOG_ERR("XTC", "Cannot generate thumb BMP with invalid dimensions: %ux%u", width, height);
    return false;
  }
  const std::string thumbPath = getThumbBmpPath(width, height);
  const bool thumbExists = Storage.existsForRead(thumbPath);
  if (thumbExists) {
    if (thumbnailHasDimensions(thumbPath, width, height)) {
      return true;
    }
  }

  if (!loaded || !parser) {
    LOG_ERR("XTC", "Cannot generate thumb BMP, file not loaded");
    return false;
  }
  if (parser->getPageCount() == 0) {
    LOG_ERR("XTC", "No pages in XTC file");
    return false;
  }

  setupCacheDir();

  xtc::PageInfo pageInfo;
  if (!parser->getPageInfo(0, pageInfo)) {
    LOG_DBG("XTC", "Failed to get first page info");
    return false;
  }
  if (pageInfo.width == 0 || pageInfo.height == 0) {
    LOG_ERR("XTC", "Cannot generate thumb BMP with invalid page dimensions: %ux%u", pageInfo.width, pageInfo.height);
    return false;
  }
  if (thumbExists) {
    Storage.remove(thumbPath.c_str());
  }

  const uint8_t bitDepth = parser->getBitDepth();
  const uint16_t THUMB_TARGET_WIDTH = width;
  const uint16_t THUMB_TARGET_HEIGHT = height;

  const float scaleX = static_cast<float>(THUMB_TARGET_WIDTH) / pageInfo.width;
  const float scaleY = static_cast<float>(THUMB_TARGET_HEIGHT) / pageInfo.height;
  const float scale = std::max(scaleX, scaleY);
  const uint16_t thumbWidth = THUMB_TARGET_WIDTH;
  const uint16_t thumbHeight = THUMB_TARGET_HEIGHT;

  size_t bitmapSize;
  if (bitDepth == 2) {
    bitmapSize = ((static_cast<size_t>(pageInfo.width) * pageInfo.height + 7) / 8) * 2;
  } else {
    bitmapSize = ((pageInfo.width + 7) / 8) * pageInfo.height;
  }
  uint8_t* pageBuffer = static_cast<uint8_t*>(malloc(bitmapSize));
  if (!pageBuffer) {
    LOG_ERR("XTC", "Failed to allocate page buffer (%lu bytes)", bitmapSize);
    return false;
  }

  size_t bytesRead = const_cast<xtc::XtcParser*>(parser.get())->loadPage(0, pageBuffer, bitmapSize);
  if (bytesRead == 0) {
    LOG_ERR("XTC", "Failed to load cover page for thumb");
    free(pageBuffer);
    return false;
  }

  FsFile thumbBmp;
  if (!Storage.openFileForWrite("XTC", thumbPath, thumbBmp)) {
    free(pageBuffer);
    return false;
  }

  const uint32_t rowSize = (thumbWidth + 31) / 32 * 4;
  BmpHeader bmpHeader;
  createBmpHeader(&bmpHeader, thumbWidth, thumbHeight, BmpRowOrder::TopDown);
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&bmpHeader), sizeof(BmpHeader));

  uint8_t* rowBuffer = static_cast<uint8_t*>(malloc(rowSize));
  if (!rowBuffer) {
    free(pageBuffer);
    thumbBmp.close();
    Storage.remove(thumbPath.c_str());
    return false;
  }
  Burkes1BitDitherer ditherer(thumbWidth);
  if (!ditherer.valid()) {
    LOG_ERR("XTC", "Failed to allocate cover thumbnail ditherer");
    free(rowBuffer);
    free(pageBuffer);
    thumbBmp.close();
    Storage.remove(thumbPath.c_str());
    return false;
  }

  const uint32_t scaleInv_fp = static_cast<uint32_t>(65536.0f / scale);
  const uint64_t srcWidth_fp = static_cast<uint64_t>(pageInfo.width) << 16;
  const uint64_t srcHeight_fp = static_cast<uint64_t>(pageInfo.height) << 16;
  const uint64_t visibleWidth_fp = static_cast<uint64_t>(thumbWidth) * scaleInv_fp;
  const uint64_t visibleHeight_fp = static_cast<uint64_t>(thumbHeight) * scaleInv_fp;
  const uint32_t cropX_fp =
      static_cast<uint32_t>(srcWidth_fp > visibleWidth_fp ? (srcWidth_fp - visibleWidth_fp) / 2 : 0);
  const uint32_t cropY_fp =
      static_cast<uint32_t>(srcHeight_fp > visibleHeight_fp ? (srcHeight_fp - visibleHeight_fp) / 2 : 0);
  const size_t planeSize = (bitDepth == 2) ? ((static_cast<size_t>(pageInfo.width) * pageInfo.height + 7) / 8) : 0;
  const uint8_t* plane1 = (bitDepth == 2) ? pageBuffer : nullptr;
  const uint8_t* plane2 = (bitDepth == 2) ? pageBuffer + planeSize : nullptr;
  const size_t colBytes = (bitDepth == 2) ? ((pageInfo.height + 7) / 8) : 0;
  const size_t srcRowBytes = (bitDepth == 1) ? ((pageInfo.width + 7) / 8) : 0;

  for (uint16_t dstY = 0; dstY < thumbHeight; dstY++) {
    memset(rowBuffer, 0xFF, rowSize);
    uint32_t srcYStart = (cropY_fp + static_cast<uint32_t>(dstY) * scaleInv_fp) >> 16;
    uint32_t srcYEnd = (cropY_fp + static_cast<uint32_t>(dstY + 1) * scaleInv_fp) >> 16;
    if (srcYStart >= pageInfo.height) srcYStart = pageInfo.height - 1;
    if (srcYEnd > pageInfo.height) srcYEnd = pageInfo.height;
    if (srcYEnd <= srcYStart) srcYEnd = srcYStart + 1;
    if (srcYEnd > pageInfo.height) srcYEnd = pageInfo.height;

    const bool reverse = ditherer.isReverseRow();
    const int startX = reverse ? static_cast<int>(thumbWidth) - 1 : 0;
    const int endX = reverse ? -1 : static_cast<int>(thumbWidth);
    const int stepX = reverse ? -1 : 1;
    for (int dstX = startX; dstX != endX; dstX += stepX) {
      uint32_t srcXStart = (cropX_fp + static_cast<uint32_t>(dstX) * scaleInv_fp) >> 16;
      uint32_t srcXEnd = (cropX_fp + static_cast<uint32_t>(dstX + 1) * scaleInv_fp) >> 16;
      if (srcXStart >= pageInfo.width) srcXStart = pageInfo.width - 1;
      if (srcXEnd > pageInfo.width) srcXEnd = pageInfo.width;
      if (srcXEnd <= srcXStart) srcXEnd = srcXStart + 1;
      if (srcXEnd > pageInfo.width) srcXEnd = pageInfo.width;

      uint32_t graySum = 0, totalCount = 0;
      for (uint32_t srcY = srcYStart; srcY < srcYEnd && srcY < pageInfo.height; srcY++) {
        for (uint32_t srcX = srcXStart; srcX < srcXEnd && srcX < pageInfo.width; srcX++) {
          uint8_t grayValue = 255;
          if (bitDepth == 2) {
            if (srcX < pageInfo.width) {
              const size_t colIndex = pageInfo.width - 1 - srcX;
              const size_t byteInCol = srcY / 8;
              const size_t bitInByte = 7 - (srcY % 8);
              const size_t byteOffset = colIndex * colBytes + byteInCol;
              if (byteOffset < planeSize) {
                const uint8_t bit1 = (plane1[byteOffset] >> bitInByte) & 1;
                const uint8_t bit2 = (plane2[byteOffset] >> bitInByte) & 1;
                grayValue = (3 - ((bit1 << 1) | bit2)) * 85;
              }
            }
          } else {
            const size_t byteIdx = srcY * srcRowBytes + srcX / 8;
            const size_t bitIdx = 7 - (srcX % 8);
            if (byteIdx < bitmapSize) {
              grayValue = ((pageBuffer[byteIdx] >> bitIdx) & 1) ? 255 : 0;
            }
          }
          graySum += grayValue;
          totalCount++;
        }
      }

      uint8_t avgGray = (totalCount > 0) ? static_cast<uint8_t>(graySum / totalCount) : 255;
      const uint8_t oneBit = ditherer.processPixel(avgGray, dstX);
      const size_t byteIndex = dstX / 8;
      const size_t bitOffset = 7 - (dstX % 8);
      if (byteIndex < rowSize) {
        if (!oneBit) {
          rowBuffer[byteIndex] &= ~(1 << bitOffset);
        }
      }
    }
    ditherer.nextRow();
    thumbBmp.write(rowBuffer, rowSize);
  }

  free(rowBuffer);
  thumbBmp.close();
  free(pageBuffer);
  LOG_DBG("XTC", "Generated thumb BMP (%dx%d): %s", thumbWidth, thumbHeight, getThumbBmpPath(width, height).c_str());
  return true;
}

uint32_t Xtc::getPageCount() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getPageCount();
}

uint16_t Xtc::getPageWidth() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getWidth();
}

uint16_t Xtc::getPageHeight() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getHeight();
}

uint8_t Xtc::getBitDepth() const {
  if (!loaded || !parser) {
    return 1;  // Default to 1-bit
  }
  return parser->getBitDepth();
}

size_t Xtc::loadPage(uint32_t pageIndex, uint8_t* buffer, size_t bufferSize) const {
  if (!loaded || !parser) {
    return 0;
  }
  return const_cast<xtc::XtcParser*>(parser.get())->loadPage(pageIndex, buffer, bufferSize);
}

xtc::XtcError Xtc::loadPageStreaming(uint32_t pageIndex,
                                     std::function<void(const uint8_t* data, size_t size, size_t offset)> callback,
                                     size_t chunkSize) const {
  if (!loaded || !parser) {
    return xtc::XtcError::FILE_NOT_FOUND;
  }
  return const_cast<xtc::XtcParser*>(parser.get())->loadPageStreaming(pageIndex, callback, chunkSize);
}

uint8_t Xtc::calculateProgress(uint32_t currentPage) const {
  if (!loaded || !parser || parser->getPageCount() == 0) {
    return 0;
  }
  return static_cast<uint8_t>((currentPage + 1) * 100 / parser->getPageCount());
}

xtc::XtcError Xtc::getLastError() const {
  if (!parser) {
    return xtc::XtcError::FILE_NOT_FOUND;
  }
  return parser->getLastError();
}
