#include "SleepActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <PNGdec.h>
#include <Xtc.h>

#include <algorithm>
#include <cstdint>
#include <new>

#include "../home/RecentBookProgress.h"
#include "../reader/BookStatsView.h"
#include "../reader/EpubReaderActivity.h"
#include "../reader/EpubReaderUtils.h"
#include "../reader/TxtReaderActivity.h"
#include "../reader/XtcReaderActivity.h"
#include "AppVersion.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "RecentBooksStore.h"
#include "SleepCoverAssets.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "components/themes/dashboard/DashboardTheme.h"
#include "components/themes/minimal/MinimalTheme.h"
#include "fontIds.h"
#include "images/Logo160.h"
#include "images/MoonIcon.h"

namespace {

constexpr bool TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH = true;
constexpr int sleepBuildInfoSideMargin = 20;

bool sleepCoverFilterInvertsGeneratedScreen() {
  return SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE;
}

void hideOverlayBatteryStrip(const GfxRenderer& renderer) {
  if (!SETTINGS.statusBarBattery) {
    return;
  }

  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);

  const int statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  if (statusBarHeight <= 0) {
    return;
  }

  const int textY = renderer.getScreenHeight() - statusBarHeight - orientedMarginBottom - 4;
  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage == CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_NEVER;

  // Reserve the full left-side status indicator lane used by bookmark + battery.
  // This keeps chapter/progress text readable while removing the battery glance target.
  static constexpr int bookmarkReserveWidth = 13;  // bookmark width + gap from BaseTheme::drawStatusBar()
  static constexpr int batteryPercentSpacing = 4;  // matches BaseTheme::batteryPercentSpacing
  const int clearWidth =
      bookmarkReserveWidth + metrics.batteryWidth +
      (showBatteryPercentage ? batteryPercentSpacing + renderer.getTextWidth(SMALL_FONT_ID, "100%") : 0);
  const int clearHeight = std::max(renderer.getTextHeight(SMALL_FONT_ID), metrics.batteryHeight + 6);

  renderer.fillRect(metrics.statusBarHorizontalMargin + orientedMarginLeft + 1, textY, clearWidth, clearHeight, false);
}

// Context passed through PNGdec's decode() user-pointer to the per-scanline draw callback.
struct PngOverlayCtx {
  const GfxRenderer* renderer;
  int screenW;
  int screenH;
  int srcWidth;
  int dstWidth;
  int dstX;
  int dstY;
  float yScale;
  int lastDstY;
  // Color-key transparency (tRNS chunk) for TRUECOLOR and GRAYSCALE images.
  // Initialized lazily on the first draw callback because tRNS is processed during decode(),
  // not during open() — so hasAlpha()/getTransparentColor() are only valid once decode() starts.
  // -2 = not yet read; -1 = no color key; >=0 = 0x00RRGGBB (TRUECOLOR) or low-byte gray.
  int32_t transparentColor;
  PNG* pngObj;  // for lazy-init of transparentColor on first callback
};

// PNGdec file I/O callbacks — mirror the pattern in PngToFramebufferConverter.cpp.
void* pngSleepOpen(const char* filename, int32_t* size) {
  FsFile* f = new FsFile();
  if (!Storage.openFileForRead("SLP", std::string(filename), *f)) {
    delete f;
    return nullptr;
  }
  *size = f->size();
  return f;
}
void pngSleepClose(void* handle) {
  FsFile* f = reinterpret_cast<FsFile*>(handle);
  if (f) {
    f->close();
    delete f;
  }
}
int32_t pngSleepRead(PNGFILE* pFile, uint8_t* pBuf, int32_t len) {
  FsFile* f = reinterpret_cast<FsFile*>(pFile->fHandle);
  return f ? f->read(pBuf, len) : 0;
}
int32_t pngSleepSeek(PNGFILE* pFile, int32_t pos) {
  FsFile* f = reinterpret_cast<FsFile*>(pFile->fHandle);
  if (!f) return -1;
  return f->seek(pos);
}

// Per-scanline draw callback for PNG overlay compositing.
// Transparent pixels (alpha < 128) are skipped so the reader page shows through.
// Opaque pixels are drawn in their grayscale brightness (dark → black, light → white).
int pngOverlayDraw(PNGDRAW* pDraw) {
  PngOverlayCtx* ctx = reinterpret_cast<PngOverlayCtx*>(pDraw->pUser);

  // Lazy-init: tRNS chunk is processed during decode() before any IDAT data, so by the time
  // the first draw callback fires, hasAlpha() / getTransparentColor() are already valid.
  if (ctx->transparentColor == -2) {
    const int pt = pDraw->iPixelType;
    ctx->transparentColor = (pDraw->iHasAlpha && (pt == PNG_PIXEL_TRUECOLOR || pt == PNG_PIXEL_GRAYSCALE))
                                ? static_cast<int32_t>(ctx->pngObj->getTransparentColor())
                                : -1;
  }

  const int destY = ctx->dstY + (int)(pDraw->y * ctx->yScale);
  if (destY == ctx->lastDstY) return 1;  // skip duplicate rows from Y scaling
  ctx->lastDstY = destY;
  if (destY < 0 || destY >= ctx->screenH) return 1;

  const int srcWidth = ctx->srcWidth;
  const int dstWidth = ctx->dstWidth;
  const uint8_t* pixels = pDraw->pPixels;
  const int pixelType = pDraw->iPixelType;
  const int hasAlpha = pDraw->iHasAlpha;

  int srcX = 0, error = 0;
  for (int dstX = 0; dstX < dstWidth; dstX++) {
    const int outX = ctx->dstX + dstX;
    if (outX >= 0 && outX < ctx->screenW) {
      uint8_t alpha = 255, gray = 0;
      switch (pixelType) {
        case PNG_PIXEL_TRUECOLOR_ALPHA: {
          const uint8_t* p = &pixels[srcX * 4];
          alpha = p[3];
          gray = (uint8_t)((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
          break;
        }
        case PNG_PIXEL_GRAY_ALPHA:
          gray = pixels[srcX * 2];
          alpha = pixels[srcX * 2 + 1];
          break;
        case PNG_PIXEL_TRUECOLOR: {
          const uint8_t* p = &pixels[srcX * 3];
          gray = (uint8_t)((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
          // tRNS color-key: if pixel matches the designated transparent color, skip it
          if (ctx->transparentColor >= 0 && p[0] == (uint8_t)((ctx->transparentColor >> 16) & 0xFF) &&
              p[1] == (uint8_t)((ctx->transparentColor >> 8) & 0xFF) &&
              p[2] == (uint8_t)(ctx->transparentColor & 0xFF)) {
            alpha = 0;
          }
          break;
        }
        case PNG_PIXEL_GRAYSCALE:
          gray = pixels[srcX];
          // tRNS color-key: transparent gray value stored in low byte
          if (ctx->transparentColor >= 0 && gray == (uint8_t)(ctx->transparentColor & 0xFF)) {
            alpha = 0;
          }
          break;
        case PNG_PIXEL_INDEXED:
          if (pDraw->pPalette) {
            const uint8_t idx = pixels[srcX];
            const uint8_t* p = &pDraw->pPalette[idx * 3];
            gray = (uint8_t)((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
            if (hasAlpha) alpha = pDraw->pPalette[768 + idx];
          }
          break;
        default:
          gray = pixels[srcX];
          break;
      }

      if (alpha >= 128) {
        ctx->renderer->drawPixel(outX, destY, gray < 128);  // true = black, false = white
      }
      // alpha < 128: transparent — leave the reader page pixel intact
    }

    // Bresenham-style X stepping (handles downscaling; 1:1 when srcWidth == dstWidth)
    error += srcWidth;
    while (error >= dstWidth) {
      error -= dstWidth;
      srcX++;
    }
  }
  return 1;
}

std::string filenameFromPath(const std::string& path) {
  const size_t lastSlash = path.find_last_of('/');
  return lastSlash == std::string::npos ? path : path.substr(lastSlash + 1);
}

std::string recentTitleForPath(const std::string& path) {
  const auto& books = RECENT_BOOKS.getBooks();
  const auto book = std::find_if(books.begin(), books.end(), [&path](const RecentBook& candidate) {
    return candidate.path == path && !candidate.title.empty();
  });
  return book == books.end() ? std::string{} : book->title;
}

RecentBook recentBookForPath(const std::string& path) {
  const auto& books = RECENT_BOOKS.getBooks();
  const auto book =
      std::find_if(books.begin(), books.end(), [&path](const RecentBook& candidate) { return candidate.path == path; });
  if (book != books.end()) {
    return *book;
  }

  RecentBook loadedBook = RECENT_BOOKS.getDataFromBook(path);
  if (loadedBook.title.empty()) {
    loadedBook.title = filenameFromPath(path);
  }
  return loadedBook;
}

std::string bookStatsCachePathFor(const std::string& path) {
  if (FsHelpers::hasEpubExtension(path)) {
    return Epub::cachePathForFilePath(path, DUET_BOOKS_ROOT_PATH "");
  }
  if (FsHelpers::hasXtcExtension(path)) {
    return Xtc(path, DUET_BOOKS_ROOT_PATH "").getCachePath();
  }
  return {};
}

BookReadingStats loadBookStatsForPath(const std::string& path) {
  const std::string cachePath = bookStatsCachePathFor(path);
  if (cachePath.empty()) {
    return BookReadingStats{};
  }
  return BookReadingStats::load(cachePath);
}

std::string loadChapterTitleForPath(const std::string& path) {
  if (!FsHelpers::hasEpubExtension(path)) {
    return {};
  }

  Epub epub(path, DUET_BOOKS_ROOT_PATH "");
  if (!epub.load(false, true)) {
    return {};
  }

  EpubReaderUtils::Progress progress;
  if (!EpubReaderUtils::loadProgress(epub, progress, "SLP")) {
    return {};
  }

  const auto spineItem = epub.getSpineItem(progress.spineIndex);
  if (spineItem.tocIndex < 0) {
    return {};
  }

  const auto tocItem = epub.getTocItem(spineItem.tocIndex);
  return tocItem.title;
}

enum class OverlayDrawResult : uint8_t { NotFound, Drawn, Failed };

enum class SleepImageMode : uint8_t { Custom, Overlay };

struct SleepImageSelection {
  std::string path;
  bool isPng = false;
};

bool isBmpSleepImagePath(const std::string& path) { return FsHelpers::hasBmpExtension(path); }

bool isPngSleepImagePath(const std::string& path) { return FsHelpers::hasPngExtension(path); }

// Cycling a sleep image happens while the device has just woken from deep sleep.
// Walking a large wallpaper directory there makes a short multi-click gesture feel
// stalled. Keep a compact, on-card list of BMP filenames so normal cycles only
// read a small cache file rather than reopening every wallpaper on the SD card.
constexpr char SLEEP_IMAGE_INDEX_PATH[] = DUET_STATE_ROOT_PATH "/sleep_image_index_v1.bin";
constexpr char SLEEP_IMAGE_INDEX_TMP_PATH[] = DUET_STATE_ROOT_PATH "/sleep_image_index_v1.tmp";
constexpr uint32_t SLEEP_IMAGE_INDEX_MAGIC = 0x58444C53u;  // SLDX, little-endian on disk
constexpr uint16_t SLEEP_IMAGE_INDEX_VERSION = 1;
constexpr size_t SLEEP_IMAGE_INDEX_FILENAME_BYTES = 255;

struct SleepImageIndexHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
  uint32_t directoryHash;
  uint32_t directorySize;
};

uint32_t sleepDirectoryHash(const std::string& path) {
  uint32_t hash = 2166136261u;
  for (const unsigned char ch : path) {
    hash ^= ch;
    hash *= 16777619u;
  }
  return hash;
}

bool tryOpenSleepDirectory(FsFile& dir, std::string& sleepDir, const std::string& candidate) {
  if (candidate.empty()) {
    return false;
  }

  dir = Storage.open(candidate.c_str());
  if (dir && dir.isDirectory()) {
    sleepDir = candidate;
    return true;
  }

  if (dir) {
    dir.close();
  }
  return false;
}

bool openPreferredSleepDirectory(FsFile& dir, std::string& sleepDir) {
  sleepDir.clear();

  if (tryOpenSleepDirectory(dir, sleepDir, APP_STATE.preferredSleepFolderPath)) {
    return true;
  }

  if (!APP_STATE.preferredSleepFolderPath.empty()) {
    LOG_INF("SLP", "Preferred sleep folder missing, falling back: %s", APP_STATE.preferredSleepFolderPath.c_str());
  }

  if (tryOpenSleepDirectory(dir, sleepDir, "/.sleep")) {
    return true;
  }

  return tryOpenSleepDirectory(dir, sleepDir, "/sleep");
}

bool readSleepImageIndexHeader(HalFile& index, const std::string& sleepDir, const uint32_t directorySize,
                               SleepImageIndexHeader& header) {
  if (index.read(&header, sizeof(header)) != static_cast<int>(sizeof(header))) {
    return false;
  }
  return header.magic == SLEEP_IMAGE_INDEX_MAGIC && header.version == SLEEP_IMAGE_INDEX_VERSION &&
         header.count > 0 && header.directoryHash == sleepDirectoryHash(sleepDir) &&
         header.directorySize == directorySize;
}

bool selectRandomSleepImageFromIndex(const std::string& sleepDir, const uint32_t directorySize,
                                     SleepImageSelection& selection) {
  HalFile index;
  if (!Storage.openFileForRead("SLP", SLEEP_IMAGE_INDEX_PATH, index)) {
    return false;
  }

  SleepImageIndexHeader header{};
  if (!readSleepImageIndexHeader(index, sleepDir, directorySize, header)) {
    index.close();
    return false;
  }

  const uint8_t window = static_cast<uint8_t>(
      std::min(static_cast<uint16_t>(APP_STATE.recentSleepFill), static_cast<uint16_t>(header.count - 1)));
  uint16_t selectedIndex = static_cast<uint16_t>(random(header.count));
  for (uint8_t attempt = 0; attempt < 20 && APP_STATE.isRecentSleep(selectedIndex, window); ++attempt) {
    selectedIndex = static_cast<uint16_t>(random(header.count));
  }

  char filename[SLEEP_IMAGE_INDEX_FILENAME_BYTES + 1] = {};
  for (uint16_t indexEntry = 0; indexEntry <= selectedIndex; ++indexEntry) {
    uint8_t filenameLength = 0;
    if (index.read(&filenameLength, sizeof(filenameLength)) != static_cast<int>(sizeof(filenameLength)) ||
        filenameLength == 0 || filenameLength > SLEEP_IMAGE_INDEX_FILENAME_BYTES ||
        index.read(filename, filenameLength) != filenameLength) {
      index.close();
      return false;
    }
    if (indexEntry != selectedIndex) {
      continue;
    }
    filename[filenameLength] = '\0';
  }
  index.close();

  const std::string selectedPath = sleepDir + "/" + filename;
  if (!isBmpSleepImagePath(selectedPath) || !Storage.exists(selectedPath.c_str())) {
    LOG_INF("SLP", "Sleep index entry is stale, rebuilding: %s", selectedPath.c_str());
    return false;
  }

  selection.path = selectedPath;
  selection.isPng = false;
  APP_STATE.pushRecentSleep(selectedIndex);
  APP_STATE.saveToFile();
  return true;
}

bool rebuildSleepImageIndex(const std::string& sleepDir, const uint32_t directorySize) {
  FsFile dir;
  std::string resolvedDir;
  if (!tryOpenSleepDirectory(dir, resolvedDir, sleepDir)) {
    return false;
  }

  Storage.mkdir(DUET_STATE_ROOT_PATH "");
  Storage.remove(SLEEP_IMAGE_INDEX_TMP_PATH);

  HalFile index;
  if (!Storage.openFileForWrite("SLP", SLEEP_IMAGE_INDEX_TMP_PATH, index)) {
    dir.close();
    return false;
  }

  SleepImageIndexHeader header{SLEEP_IMAGE_INDEX_MAGIC, SLEEP_IMAGE_INDEX_VERSION, 0, sleepDirectoryHash(sleepDir),
                               directorySize};
  bool ok = index.write(&header, sizeof(header)) == sizeof(header);
  char name[500];
  for (auto file = dir.openNextFile(); ok && file; file = dir.openNextFile()) {
    if (file.isDirectory()) {
      file.close();
      continue;
    }

    file.getName(name, sizeof(name));
    const std::string filename(name);
    file.close();
    if (!isBmpSleepImagePath(filename) || filename.empty() || filename[0] == '.' ||
        filename.size() > SLEEP_IMAGE_INDEX_FILENAME_BYTES || header.count == UINT16_MAX) {
      continue;
    }

    const uint8_t filenameLength = static_cast<uint8_t>(filename.size());
    ok = index.write(&filenameLength, sizeof(filenameLength)) == sizeof(filenameLength) &&
         index.write(filename.data(), filenameLength) == filenameLength;
    if (ok) {
      header.count = static_cast<uint16_t>(header.count + 1);
    }
  }
  dir.close();

  if (ok && header.count > 0) {
    ok = index.seek(0) && index.write(&header, sizeof(header)) == sizeof(header);
  }
  index.flush();
  index.close();

  if (!ok || header.count == 0) {
    Storage.remove(SLEEP_IMAGE_INDEX_TMP_PATH);
    return false;
  }

  Storage.remove(SLEEP_IMAGE_INDEX_PATH);
  if (!Storage.rename(SLEEP_IMAGE_INDEX_TMP_PATH, SLEEP_IMAGE_INDEX_PATH)) {
    Storage.remove(SLEEP_IMAGE_INDEX_TMP_PATH);
    return false;
  }

  APP_STATE.clearRecentSleepHistory();
  LOG_INF("SLP", "Built sleep image index for %u BMPs", header.count);
  return true;
}

bool selectPinnedSleepImage(SleepImageMode mode, SleepImageSelection& selection) {
  const std::string& favorite = APP_STATE.favoriteSleepImagePath;
  if (favorite.empty()) {
    return false;
  }

  if (!Storage.exists(favorite.c_str())) {
    LOG_INF("SLP", "Pinned sleep image missing, falling back: %s", favorite.c_str());
    return false;
  }

  if (isBmpSleepImagePath(favorite)) {
    selection.path = favorite;
    selection.isPng = false;
    return true;
  }

  if (isPngSleepImagePath(favorite)) {
    if (mode == SleepImageMode::Overlay) {
      selection.path = favorite;
      selection.isPng = true;
      return true;
    }

    LOG_INF("SLP", "Pinned PNG sleep image requires Page Overlay mode, falling back: %s", favorite.c_str());
    return false;
  }

  LOG_ERR("SLP", "Pinned sleep image has unsupported extension: %s", favorite.c_str());
  return false;
}

bool selectRandomSleepImageByDirectoryScan(SleepImageMode mode, SleepImageSelection& selection) {
  FsFile dir;
  std::string sleepDir;
  if (!openPreferredSleepDirectory(dir, sleepDir)) {
    return false;
  }

  const bool allowPng = mode == SleepImageMode::Overlay;
  const auto isValidSleepImage = [allowPng, &sleepDir](FsFile& file, const std::string& filename) {
    if (filename.empty() || filename[0] == '.') {
      return false;
    }

    const bool isBmp = FsHelpers::hasBmpExtension(filename);
    const bool isPng = allowPng && FsHelpers::hasPngExtension(filename);
    if (!isBmp && !isPng) {
      return false;
    }

    if (isBmp) {
      Bitmap bitmap(file);
      const BmpReaderError parseResult = bitmap.parseHeaders();
      if (parseResult != BmpReaderError::Ok) {
        LOG_ERR("SLP", "Skipping invalid BMP sleep image %s/%s: %s", sleepDir.c_str(), filename.c_str(),
                Bitmap::errorToString(parseResult));
        return false;
      }
    }

    return true;
  };

  // Count first, then reopen and seek to the chosen image. Holding every filename
  // in a vector can require a contiguous 48 KB allocation once a folder exceeds
  // 1,024 images, which is unsafe while an EPUB is already using most of the heap.
  uint16_t fileCount = 0;
  char name[500];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (file.isDirectory()) {
      file.close();
      continue;
    }

    file.getName(name, sizeof(name));
    std::string filename(name);
    if (isValidSleepImage(file, filename)) {
      fileCount++;
    }
    file.close();

    if (fileCount == UINT16_MAX) {
      break;
    }
  }
  dir.close();

  if (fileCount == 0) {
    return false;
  }

  const uint8_t window = static_cast<uint8_t>(
      std::min(static_cast<uint16_t>(APP_STATE.recentSleepFill), static_cast<uint16_t>(fileCount - 1)));
  auto randomFileIndex = static_cast<uint16_t>(random(fileCount));
  for (uint8_t attempt = 0; attempt < 20 && APP_STATE.isRecentSleep(randomFileIndex, window); attempt++) {
    randomFileIndex = static_cast<uint16_t>(random(fileCount));
  }

  FsFile selectedDir;
  if (!openPreferredSleepDirectory(selectedDir, sleepDir)) {
    return false;
  }

  uint16_t validFileIndex = 0;
  for (auto file = selectedDir.openNextFile(); file; file = selectedDir.openNextFile()) {
    if (file.isDirectory()) {
      file.close();
      continue;
    }

    file.getName(name, sizeof(name));
    std::string filename(name);
    if (!isValidSleepImage(file, filename)) {
      file.close();
      continue;
    }

    if (validFileIndex == randomFileIndex) {
      selection.path = sleepDir + "/" + filename;
      selection.isPng = FsHelpers::hasPngExtension(selection.path);
      file.close();
      selectedDir.close();

      // Keep the recent-image state tied to the stable directory index rather
      // than to a heap-resident filename list.
      APP_STATE.pushRecentSleep(randomFileIndex);
      APP_STATE.saveToFile();
      return true;
    }

    validFileIndex++;
    file.close();
  }
  selectedDir.close();

  LOG_ERR("SLP", "Failed to locate selected sleep image index %u of %u", randomFileIndex, fileCount);
  return false;
}

bool selectRandomSleepImage(SleepImageMode mode, SleepImageSelection& selection) {
  // PNG overlays are intentionally left on the legacy path: their transparent
  // composition mode is not used by the asleep power-button cycle.
  if (mode != SleepImageMode::Custom) {
    return selectRandomSleepImageByDirectoryScan(mode, selection);
  }

  FsFile dir;
  std::string sleepDir;
  if (!openPreferredSleepDirectory(dir, sleepDir)) {
    return false;
  }
  const uint32_t directorySize = static_cast<uint32_t>(dir.fileSize());
  dir.close();

  if (selectRandomSleepImageFromIndex(sleepDir, directorySize, selection)) {
    return true;
  }

  if (rebuildSleepImageIndex(sleepDir, directorySize) &&
      selectRandomSleepImageFromIndex(sleepDir, directorySize, selection)) {
    return true;
  }

  // A read-only or full SD card must still be able to display a sleep image.
  return selectRandomSleepImageByDirectoryScan(mode, selection);
}

}  // namespace

void SleepActivity::onEnter() {
  Activity::onEnter();

  const bool renderQuickResume =
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME ||
      (fromTimeout &&
       SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT);

  if (renderQuickResume) {
    return renderLastScreenSleepScreen();
  }

  overlayBackgroundBufferStored =
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::OVERLAY && renderer.storeBwBuffer();

  // Keep the same visible sleep flow on both readers: acknowledge the command,
  // then replace the current screen with the selected sleep frame. The X4
  // previously ran an additional full-panel scrub here; physical testing showed
  // that it added a long black/white flash sequence without removing the
  // remaining ghost image.
  if (APP_STATE.lastSleepFromReader) {
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  } else {
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
  }

  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::BLANK):
      return renderBlankSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM):
      return renderCustomSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER):
      return renderCoverSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      if (APP_STATE.lastSleepFromReader) {
        return renderCoverSleepScreen();
      } else {
        return renderCustomSleepScreen();
      }
    case (CrossPointSettings::SLEEP_SCREEN_MODE::OVERLAY):
      return renderOverlaySleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::READING_STATS_SLEEP):
      return renderReadingStatsSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::MINIMAL_SLEEP):
      return renderMinimalSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::MINIMAL_STATS_SLEEP):
      return renderMinimalStatsSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::DASHBOARD_SLEEP):
      return renderDashboardSleepScreen();
    default:
      return renderDefaultSleepScreen();
  }
}

void SleepActivity::displayFinalSleepFrame(const bool turnOffScreen) const {
  // One complete final paint is the only cleanup pass. In particular, do not
  // precede this with the X4 controller-RAM scrub: device video showed that the
  // scrub repeated the full waveform but did not improve the settled frame.
  renderer.displayBuffer(HalDisplay::FULL_REFRESH, turnOffScreen);
}

void SleepActivity::renderCustomSleepScreen() const {
  SleepImageSelection selection;
  if (selectPinnedSleepImage(SleepImageMode::Custom, selection) ||
      selectRandomSleepImage(SleepImageMode::Custom, selection)) {
    FsFile file;
    if (Storage.openFileForRead("SLP", selection.path, file)) {
      LOG_INF("SLP", "Loading custom sleep image: %s", selection.path.c_str());
      delay(100);
      Bitmap bitmap(file, true);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        renderBitmapSleepScreen(bitmap);
        return;
      }
      LOG_ERR("SLP", "Failed to parse custom sleep BMP: %s", selection.path.c_str());
    } else {
      LOG_ERR("SLP", "Failed to open custom sleep image: %s", selection.path.c_str());
    }
  }

  // Look for sleep.bmp on the root of the sd card to determine if we should
  // render a custom sleep screen instead of the default.
  FsFile file;
  if (Storage.openFileForRead("SLP", "/sleep.bmp", file)) {
    Bitmap bitmap(file, true);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Loading: /sleep.bmp");
      renderBitmapSleepScreen(bitmap);
      return;
    }
  }

  renderDefaultSleepScreen();
}

bool SleepActivity::cycleCustomSleepScreen() const {
  SleepImageSelection selection;
  if (!selectRandomSleepImage(SleepImageMode::Custom, selection)) {
    LOG_INF("SLP", "Cycle skipped: no custom BMP sleep image available");
    return false;
  }

  FsFile file;
  if (!Storage.openFileForRead("SLP", selection.path, file)) {
    LOG_ERR("SLP", "Cycle failed to open custom sleep image: %s", selection.path.c_str());
    return false;
  }

  Bitmap bitmap(file, true);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    LOG_ERR("SLP", "Cycle failed to parse custom sleep BMP: %s", selection.path.c_str());
    return false;
  }

  LOG_INF("SLP", "Cycling sleep image to: %s", selection.path.c_str());
  renderBitmapSleepScreen(bitmap);
  return true;
}

void SleepActivity::renderDefaultSleepScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(Logo160, (pageWidth - 160) / 2, (pageHeight - 160) / 2, 160, 160);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, tr(STR_DUET), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_SLEEPING));

  // Make sleep screen dark unless light is selected in settings
  const bool lightSleepScreen = SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT;
  if (!lightSleepScreen) {
    renderer.invertScreen();
  }

#ifdef CROSSINK_SHOW_SLEEP_BUILD_INFO
  const std::string buildInfo = std::string(CROSSINK_BUILD_ENV) + " " + CROSSINK_VERSION;
  const std::string visibleBuildInfo =
      renderer.truncatedText(SMALL_FONT_ID, buildInfo.c_str(), pageWidth - sleepBuildInfoSideMargin * 2);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 118, visibleBuildInfo.c_str(), lightSleepScreen);
#endif

  displayFinalSleepFrame(TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);
}

void SleepActivity::renderBitmapSleepScreen(const Bitmap& bitmap) const {
  int x, y;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  float cropX = 0, cropY = 0;

  LOG_DBG("SLP", "bitmap %d x %d, screen %d x %d", bitmap.getWidth(), bitmap.getHeight(), pageWidth, pageHeight);
  if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
    // image will scale, make sure placement is right
    float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    LOG_DBG("SLP", "bitmap ratio: %f, screen ratio: %f", ratio, screenRatio);
    if (ratio > screenRatio) {
      // image wider than viewport ratio, scaled down image needs to be centered vertically
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropX = 1.0f - (screenRatio / ratio);
        LOG_DBG("SLP", "Cropping bitmap x: %f", cropX);
        ratio = (1.0f - cropX) * static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
      }
      x = 0;
      y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
      LOG_DBG("SLP", "Centering with ratio %f to y=%d", ratio, y);
    } else {
      // image taller than viewport ratio, scaled down image needs to be centered horizontally
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropY = 1.0f - (ratio / screenRatio);
        LOG_DBG("SLP", "Cropping bitmap y: %f", cropY);
        ratio = static_cast<float>(bitmap.getWidth()) / ((1.0f - cropY) * static_cast<float>(bitmap.getHeight()));
      }
      x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      y = 0;
      LOG_DBG("SLP", "Centering with ratio %f to x=%d", ratio, x);
    }
  } else {
    // center the image
    x = (pageWidth - bitmap.getWidth()) / 2;
    y = (pageHeight - bitmap.getHeight()) / 2;
  }

  LOG_DBG("SLP", "drawing to %d x %d", x, y);
  renderer.clearScreen();

  // The X4 factory grayscale pass can erase filled tones and leave only image
  // edges after the clean first paint. Its normal BW renderer already dithers
  // grayscale source pixels well, so keep that stable frame on X4. X3 retains
  // its working multi-plane grayscale sleep path.
  const bool hasGreyscale = !gpio.deviceIsX4() && bitmap.hasGreyscale() &&
                            SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;

  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);

  if (SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  if (hasGreyscale) {
    // A lock image must not diff against the reader page. Force a clean X3 base
    // and settle pass before applying the grayscale planes.
    renderer.displayCleanGrayscaleBase();
  } else {
    displayFinalSleepFrame(TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);
  }

  if (hasGreyscale) {
    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleLsbBuffers();

    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleMsbBuffers();

    // SSD1677 (X4) needs its absolute factory grayscale waveform for a
    // standalone lock image. Differential grayscale can preserve the prior UI
    // underneath the new frame; X3 keeps its separate clean-base path.
    renderer.displayGrayBuffer(TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH, gpio.deviceIsX4());
    renderer.setRenderMode(GfxRenderer::BW);
  }
}

void SleepActivity::renderCoverSleepScreen() const {
  void (SleepActivity::*renderNoCoverSleepScreen)() const;
  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      renderNoCoverSleepScreen = &SleepActivity::renderCustomSleepScreen;
      break;
    default:
      renderNoCoverSleepScreen = &SleepActivity::renderDefaultSleepScreen;
      break;
  }

  const std::string& path = currentBookPath.empty() ? APP_STATE.openEpubPath : currentBookPath;
  if (path.empty()) {
    return (this->*renderNoCoverSleepScreen)();
  }

  bool cropped = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;
  std::string coverBmpPath = SleepCoverAssets::cachedCoverPathFor(path, cropped);
  if (coverBmpPath.empty() && SleepCoverAssets::prepareFullCoverForPath(path, cropped, &renderer)) {
    coverBmpPath = SleepCoverAssets::cachedCoverPathFor(path, cropped);
  }
  if (coverBmpPath.empty()) {
    return (this->*renderNoCoverSleepScreen)();
  }

  FsFile file;
  if (Storage.openFileForRead("SLP", coverBmpPath, file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Rendering sleep cover: %s", coverBmpPath.c_str());
      renderBitmapSleepScreen(bitmap);
      return;
    }
  }

  return (this->*renderNoCoverSleepScreen)();
}

void SleepActivity::renderReadingStatsSleepScreen() const {
  BookReadingStats bookStats;
  std::string bookTitle = tr(STR_READING_STATS);
  float progressPercent = -1.0f;

  const std::string& path = currentBookPath.empty() ? APP_STATE.openEpubPath : currentBookPath;
  if (!path.empty()) {
    const std::string recentTitle = recentTitleForPath(path);
    bookTitle = recentTitle.empty() ? filenameFromPath(path) : recentTitle;

    bookStats = loadBookStatsForPath(path);
    progressPercent = RecentBookProgress::loadPercent(recentBookForPath(path));
  }

  if (!halClock.isAvailable()) {
    const GlobalReadingStats deviceStats = GlobalReadingStats::load();
    const bool hasSyncedStats = GlobalReadingStats::hasSyncedStats();
    const GlobalReadingStats allDevicesStats =
        hasSyncedStats ? GlobalReadingStats::loadAggregated(deviceStats) : GlobalReadingStats{};
    renderNoRtcCombinedStatsPage(renderer, nullptr, bookTitle, bookStats, progressPercent, false, 0, deviceStats,
                                 hasSyncedStats ? &allDevicesStats : nullptr, false);
  } else {
    renderPerBookStatsPage(renderer, nullptr, bookTitle, bookStats, progressPercent, false, 0, false, false, false);
  }
  if (!sleepCoverFilterInvertsGeneratedScreen()) {
    renderer.invertScreen();
  }
  displayFinalSleepFrame(TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);
}

void SleepActivity::renderMinimalSleepScreen() const {
  const std::string& path = currentBookPath.empty() ? APP_STATE.openEpubPath : currentBookPath;
  if (path.empty()) {
    return renderDefaultSleepScreen();
  }

  RecentBook book = recentBookForPath(path);
  book.coverBmpPath = SleepCoverAssets::cachedMinimalCoverPathFor(path);
  if (book.coverBmpPath.empty() && SleepCoverAssets::prepareMinimalCoverForPath(path, &renderer)) {
    book.coverBmpPath = SleepCoverAssets::cachedMinimalCoverPathFor(path);
  }

  const BookReadingStats bookStats = loadBookStatsForPath(path);
  const float progressPercent = RecentBookProgress::loadPercent(book);
  MinimalTheme theme;
  theme.drawSleepScreen(renderer, book, &bookStats, progressPercent);
  if (sleepCoverFilterInvertsGeneratedScreen()) {
    renderer.invertScreen();
  }
  displayFinalSleepFrame(TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);
}

void SleepActivity::renderMinimalStatsSleepScreen() const {
  const std::string& path = currentBookPath.empty() ? APP_STATE.openEpubPath : currentBookPath;
  if (path.empty()) {
    return renderDefaultSleepScreen();
  }

  RecentBook book = recentBookForPath(path);
  book.coverBmpPath = SleepCoverAssets::cachedMinimalCoverPathFor(path);
  if (book.coverBmpPath.empty() && SleepCoverAssets::prepareMinimalCoverForPath(path, &renderer)) {
    book.coverBmpPath = SleepCoverAssets::cachedMinimalCoverPathFor(path);
  }

  const BookReadingStats bookStats = loadBookStatsForPath(path);
  const GlobalReadingStats globalStats = GlobalReadingStats::load();
  const float progressPercent = RecentBookProgress::loadPercent(book);
  MinimalTheme theme;
  theme.drawStatsSleepScreen(renderer, book, &bookStats, &globalStats, progressPercent);
  if (sleepCoverFilterInvertsGeneratedScreen()) {
    renderer.invertScreen();
  }
  displayFinalSleepFrame(TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);
}

void SleepActivity::renderDashboardSleepScreen() const {
  const std::string& path = currentBookPath.empty() ? APP_STATE.openEpubPath : currentBookPath;
  if (path.empty()) {
    return renderDefaultSleepScreen();
  }

  RecentBook book = recentBookForPath(path);
  const std::string fallbackCoverPath = book.coverBmpPath;
  book.coverBmpPath = SleepCoverAssets::cachedDashboardCoverPathFor(path);
  if (book.coverBmpPath.empty() && SleepCoverAssets::prepareDashboardCoverForPath(path, &renderer)) {
    book.coverBmpPath = SleepCoverAssets::cachedDashboardCoverPathFor(path);
  }
  if (book.coverBmpPath.empty()) {
    book.coverBmpPath = fallbackCoverPath;
  }

  const BookReadingStats bookStats = loadBookStatsForPath(path);
  const GlobalReadingStats globalStats = GlobalReadingStats::load();
  const float progressPercent = RecentBookProgress::loadPercent(book);
  const std::string chapterTitle = loadChapterTitleForPath(path);
  DashboardTheme theme;
  theme.drawSleepScreen(renderer, book, &bookStats, &globalStats, progressPercent, chapterTitle.c_str());
  if (sleepCoverFilterInvertsGeneratedScreen()) {
    renderer.invertScreen();
  }
  displayFinalSleepFrame(TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);
}

void SleepActivity::renderLastScreenSleepScreen() const {
  const auto pageHeight = renderer.getScreenHeight();
  if (ReaderUtils::readerDarkModeEnabled()) {
    renderer.drawImageInverted(MoonIcon, 0, pageHeight - MOONICON_HEIGHT, MOONICON_WIDTH, MOONICON_HEIGHT);
  } else {
    renderer.drawImage(MoonIcon, 0, pageHeight - MOONICON_HEIGHT, MOONICON_WIDTH, MOONICON_HEIGHT);
  }
  displayFinalSleepFrame(false);
}

void SleepActivity::renderBlankSleepScreen() const {
  renderer.clearScreen();
  displayFinalSleepFrame(TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);
}

void SleepActivity::renderOverlaySleepScreen() const {
  // Overlay pictures always use portrait orientation regardless of the reader's orientation preference.
  const auto savedOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Portrait);
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const bool shouldUseReaderPageBackground = canSnapshotOverlayBackground;
  const std::string path = shouldUseReaderPageBackground
                               ? (currentBookPath.empty() ? APP_STATE.openEpubPath : currentBookPath)
                               : std::string{};

  auto renderSavedReaderPage = [&]() -> bool {
    if (path.empty()) {
      return false;
    }

    if (FsHelpers::checkFileExtension(path, ".xtc") || FsHelpers::checkFileExtension(path, ".xtch")) {
      return XtcReaderActivity::drawCurrentPageToBuffer(path, renderer);
    }
    if (FsHelpers::checkFileExtension(path, ".txt")) {
      return TxtReaderActivity::drawCurrentPageToBuffer(path, renderer);
    }
    if (FsHelpers::checkFileExtension(path, ".epub")) {
      return EpubReaderActivity::drawCurrentPageToBuffer(path, renderer);
    }
    return false;
  };
  const bool backgroundSupportsGrayscale =
      FsHelpers::checkFileExtension(path, ".txt") || FsHelpers::checkFileExtension(path, ".epub");
  bool backgroundWasRebuilt = false;
  bool backgroundAvailable = false;

  // Step 1: Restore the screen that was visible before the sleep popup. When
  // that snapshot is unavailable in the reader, rebuild from the saved position.
  if (overlayBackgroundBufferStored) {
    renderer.restoreBwBuffer();
    backgroundAvailable = true;
  } else if (shouldUseReaderPageBackground && !path.empty()) {
    backgroundWasRebuilt = renderSavedReaderPage();
    backgroundAvailable = backgroundWasRebuilt;

    if (!backgroundWasRebuilt) {
      LOG_DBG("SLP", "Page re-render failed, using white background");
      renderer.clearScreen();
    }
  } else {
    LOG_DBG("SLP", "No current screen snapshot available for overlay sleep screen");
    renderer.clearScreen();
  }

  // Remove the live battery strip from the preserved/reconstructed reader page so the
  // overlay sleep screen still shows chapter/progress details without the battery glance target.
  if (shouldUseReaderPageBackground && backgroundAvailable) {
    hideOverlayBatteryStrip(renderer);
  }

  // Step 2: Load the overlay image using the same selection logic as renderCustomSleepScreen.
  // BMP: white pixels are skipped (transparent via drawBitmap), black pixels composited on top.
  // PNG: pixels with alpha < 128 are skipped; opaque pixels are drawn with their grayscale value.
  auto tryDrawOverlay = [&](const std::string& filename) -> OverlayDrawResult {
    FsFile file;
    if (!Storage.openFileForRead("SLP", filename, file)) {
      if (Storage.exists(filename.c_str())) {
        LOG_ERR("SLP", "BMP overlay exists but could not be opened: %s", filename.c_str());
        return OverlayDrawResult::Failed;
      }
      LOG_DBG("SLP", "BMP overlay not found: %s", filename.c_str());
      return OverlayDrawResult::NotFound;
    }
    Bitmap bitmap(file, true);
    const BmpReaderError parseResult = bitmap.parseHeaders();
    if (parseResult != BmpReaderError::Ok) {
      LOG_ERR("SLP", "BMP overlay header parse failed for %s: %s", filename.c_str(),
              Bitmap::errorToString(parseResult));
      file.close();
      return OverlayDrawResult::Failed;
    }

    int x, y;
    float cropX = 0, cropY = 0;
    if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
      float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
      const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);
      if (ratio > screenRatio) {
        x = 0;
        y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
      } else {
        x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
        y = 0;
      }
    } else {
      x = (pageWidth - bitmap.getWidth()) / 2;
      y = (pageHeight - bitmap.getHeight()) / 2;
    }

    // Draw without clearScreen so the reader page remains in the frame buffer beneath
    LOG_INF("SLP", "Drawing BMP overlay: %s", filename.c_str());
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    file.close();
    return OverlayDrawResult::Drawn;
  };

  auto tryDrawPngOverlay = [&](const std::string& filename) -> OverlayDrawResult {
    if (!Storage.exists(filename.c_str())) {
      LOG_DBG("SLP", "PNG overlay not found: %s", filename.c_str());
      return OverlayDrawResult::NotFound;
    }

    constexpr size_t MIN_FREE_HEAP = 60 * 1024;  // PNG decoder ~42 KB + overhead
    if (ESP.getFreeHeap() < MIN_FREE_HEAP) {
      LOG_ERR("SLP", "Not enough heap for PNG overlay decoder: %u free, need %u for %s", ESP.getFreeHeap(),
              static_cast<unsigned>(MIN_FREE_HEAP), filename.c_str());
      return OverlayDrawResult::Failed;
    }
    PNG* png = new (std::nothrow) PNG();
    if (!png) {
      LOG_ERR("SLP", "Failed to allocate PNG overlay decoder for %s", filename.c_str());
      return OverlayDrawResult::Failed;
    }

    int rc = png->open(filename.c_str(), pngSleepOpen, pngSleepClose, pngSleepRead, pngSleepSeek, pngOverlayDraw);
    if (rc != PNG_SUCCESS) {
      delete png;
      LOG_ERR("SLP", "PNG overlay open failed for %s: %d", filename.c_str(), rc);
      return OverlayDrawResult::Failed;
    }

    const int srcW = png->getWidth(), srcH = png->getHeight();
    float yScale = 1.0f;
    int dstW = srcW, dstH = srcH;
    if (srcW > pageWidth || srcH > pageHeight) {
      const float scaleX = (float)pageWidth / srcW, scaleY = (float)pageHeight / srcH;
      const float scale = (scaleX < scaleY) ? scaleX : scaleY;
      dstW = (int)(srcW * scale);
      dstH = (int)(srcH * scale);
      yScale = (float)dstH / srcH;
    }

    PngOverlayCtx ctx;
    ctx.renderer = &renderer;
    ctx.screenW = pageWidth;
    ctx.screenH = pageHeight;
    ctx.srcWidth = srcW;
    ctx.dstWidth = dstW;
    ctx.dstX = (pageWidth - dstW) / 2;
    ctx.dstY = (pageHeight - dstH) / 2;
    ctx.yScale = yScale;
    ctx.lastDstY = -1;
    ctx.transparentColor = -2;  // will be resolved on first draw callback (after tRNS is parsed)
    ctx.pngObj = png;

    LOG_INF("SLP", "Drawing PNG overlay: %s", filename.c_str());
    rc = png->decode(&ctx, 0);
    png->close();
    delete png;
    if (rc != PNG_SUCCESS) {
      LOG_ERR("SLP", "PNG overlay decode failed for %s: %d", filename.c_str(), rc);
      return OverlayDrawResult::Failed;
    }
    return OverlayDrawResult::Drawn;
  };

  bool overlayDrawn = false;
  bool overlayCandidateFailed = false;
  SleepImageSelection selection;
  auto trySelectedOverlay = [&](const SleepImageSelection& image) {
    LOG_INF("SLP", "Selected overlay image: %s", image.path.c_str());
    const OverlayDrawResult result = image.isPng ? tryDrawPngOverlay(image.path) : tryDrawOverlay(image.path);
    overlayDrawn = result == OverlayDrawResult::Drawn;
    overlayCandidateFailed = overlayCandidateFailed || result == OverlayDrawResult::Failed;
  };

  if (selectPinnedSleepImage(SleepImageMode::Overlay, selection)) {
    trySelectedOverlay(selection);
  }
  if (!overlayDrawn && selectRandomSleepImage(SleepImageMode::Overlay, selection)) {
    trySelectedOverlay(selection);
  }

  if (!overlayDrawn) {
    const OverlayDrawResult result = tryDrawOverlay("/sleep.bmp");
    overlayDrawn = result == OverlayDrawResult::Drawn;
    overlayCandidateFailed = overlayCandidateFailed || result == OverlayDrawResult::Failed;
  }
  if (!overlayDrawn) {
    const OverlayDrawResult result = tryDrawPngOverlay("/sleep.png");
    overlayDrawn = result == OverlayDrawResult::Drawn;
    overlayCandidateFailed = overlayCandidateFailed || result == OverlayDrawResult::Failed;
  }

  if (!overlayDrawn) {
    if (overlayCandidateFailed) {
      LOG_ERR("SLP", "Overlay image was found but could not be drawn; falling back to default sleep screen");
      renderer.setOrientation(savedOrientation);
      return renderDefaultSleepScreen();
    }
    if (!backgroundAvailable) {
      LOG_DBG("SLP", "No overlay image or current screen snapshot available, falling back to default sleep screen");
      renderer.setOrientation(savedOrientation);
      return renderDefaultSleepScreen();
    }
    LOG_DBG("SLP", "No overlay image found, displaying background without overlay");
  }

  renderer.setOrientation(savedOrientation);
  // The grayscale re-render has no mask for the overlay image. If an overlay was
  // drawn, keep the composited BW frame intact instead of painting page glyphs
  // over the sleep image.
  const bool shouldRunGrayscalePass = !gpio.deviceIsX4() && shouldUseReaderPageBackground &&
                                      backgroundSupportsGrayscale && !overlayDrawn &&
                                      (backgroundWasRebuilt || (overlayBackgroundBufferStored && !path.empty()));
  displayFinalSleepFrame(!shouldRunGrayscalePass && TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);

  if (!shouldRunGrayscalePass) {
    return;
  }

  if (!renderer.storeBwBuffer()) {
    LOG_ERR("SLP", "Overlay: failed to store BW buffer for grayscale pass");
    return;
  }

  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  if (!renderSavedReaderPage()) {
    LOG_ERR("SLP", "Overlay: failed to rebuild page for grayscale LSB pass");
    renderer.setRenderMode(GfxRenderer::BW);
    renderer.restoreBwBuffer();
    return;
  }
  renderer.copyGrayscaleLsbBuffers();

  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  if (!renderSavedReaderPage()) {
    LOG_ERR("SLP", "Overlay: failed to rebuild page for grayscale MSB pass");
    renderer.setRenderMode(GfxRenderer::BW);
    renderer.restoreBwBuffer();
    return;
  }
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer(TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH, gpio.deviceIsX4());
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.restoreBwBuffer();
}
