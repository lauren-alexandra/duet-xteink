#pragma once

#include <Epub.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace EpubReaderUtils {

struct Progress {
  int spineIndex = 0;
  int pageNumber = 0;
  int pageCount = 0;
  bool hasPageCount = false;
};

constexpr uint8_t PROGRESS_PERCENT_VERSION = 1;
constexpr char PROGRESS_PERCENT_FILE[] = "/progress_pct.bin";

inline bool readProgressFile(const char* moduleName, const std::string& path, Progress& progress) {
  if (!Storage.existsForRead(path)) {
    return false;
  }

  FsFile f;
  if (!Storage.openFileForRead(moduleName, path, f)) {
    return false;
  }

  uint8_t data[6];
  const int dataSize = f.read(data, sizeof(data));
  f.close();
  if (dataSize != 4 && dataSize != 6) {
    LOG_ERR(moduleName, "Progress file has unexpected size: %d", dataSize);
    return false;
  }

  progress.spineIndex = static_cast<int>(static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8));
  progress.pageNumber = static_cast<int>(static_cast<uint16_t>(data[2]) | (static_cast<uint16_t>(data[3]) << 8));
  if (progress.pageNumber == UINT16_MAX) {
    progress.pageNumber = 0;
  }
  if (dataSize == 6) {
    progress.pageCount = static_cast<int>(static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8));
    progress.hasPageCount = true;
  } else {
    progress.pageCount = 0;
    progress.hasPageCount = false;
  }
  return true;
}

inline bool loadProgress(const std::string& cachePath, Progress& progress, const char* moduleName = "ERS") {
  const std::string progressPath = cachePath + "/progress.bin";
  if (readProgressFile(moduleName, progressPath, progress)) {
    return true;
  }

  const std::string backupPath = progressPath + ".bak";
  if (readProgressFile(moduleName, backupPath, progress)) {
    LOG_DBG("ERS", "Recovered progress from backup");
    return true;
  }
  return false;
}

inline bool loadProgress(const Epub& epub, Progress& progress, const char* moduleName = "ERS") {
  return loadProgress(epub.getCachePath(), progress, moduleName);
}

inline bool loadProgressPercent(const std::string& cachePath, float& progressPercent,
                                const char* moduleName = "ERS") {
  FsFile file;
  if (!Storage.openFileForRead(moduleName, cachePath + PROGRESS_PERCENT_FILE, file)) return false;

  uint8_t data[3] = {};
  const int bytesRead = file.read(data, sizeof(data));
  file.close();
  if (bytesRead != sizeof(data) || data[0] != PROGRESS_PERCENT_VERSION) return false;

  const uint16_t basisPoints = static_cast<uint16_t>(data[1]) | (static_cast<uint16_t>(data[2]) << 8);
  if (basisPoints > 10000) return false;
  progressPercent = static_cast<float>(basisPoints) / 100.0f;
  return true;
}

inline bool saveProgressPercent(const std::string& cachePath, float progressPercent,
                                const char* moduleName = "ERS") {
  progressPercent = progressPercent < 0.0f ? 0.0f : (progressPercent > 100.0f ? 100.0f : progressPercent);
  const uint16_t basisPoints = static_cast<uint16_t>(progressPercent * 100.0f + 0.5f);
  const uint8_t data[3] = {PROGRESS_PERCENT_VERSION, static_cast<uint8_t>(basisPoints & 0xFF),
                           static_cast<uint8_t>((basisPoints >> 8) & 0xFF)};
  const std::string path = cachePath + PROGRESS_PERCENT_FILE;
  const std::string tmpPath = path + ".tmp";

  if (Storage.exists(tmpPath.c_str())) Storage.remove(tmpPath.c_str());
  FsFile file;
  if (!Storage.openFileForWrite(moduleName, tmpPath, file)) return false;
  const bool wrote = file.write(data, sizeof(data)) == sizeof(data);
  if (wrote) file.flush();
  const bool synced = wrote && file.sync();
  const bool closed = file.close();
  if (!synced || !closed) {
    Storage.remove(tmpPath.c_str());
    return false;
  }

  if (Storage.exists(path.c_str()) && !Storage.remove(path.c_str())) {
    Storage.remove(tmpPath.c_str());
    return false;
  }
  if (!Storage.rename(tmpPath.c_str(), path.c_str())) {
    Storage.remove(tmpPath.c_str());
    return false;
  }
  return true;
}

// Persists reader progress for an EPUB to its cache directory. Returns true on success.
inline bool saveProgress(Epub& epub, int spineIndex, int pageNumber, int pageCount) {
  if (spineIndex < 0 || spineIndex > 0xFFFF || pageNumber < 0 || pageNumber > 0xFFFF || pageCount < 0 ||
      pageCount > 0xFFFF) {
    LOG_ERR("ERS", "Progress values out of range: spine=%d page=%d count=%d", spineIndex, pageNumber, pageCount);
    return false;
  }
  const std::string progressPath = epub.getCachePath() + "/progress.bin";
  const std::string tmpPath = progressPath + ".tmp";
  const std::string backupPath = progressPath + ".bak";

  if (Storage.exists(tmpPath.c_str()) && !Storage.remove(tmpPath.c_str())) {
    LOG_ERR("ERS", "Could not remove stale progress temp file");
    return false;
  }

  FsFile f;
  if (!Storage.openFileForWrite("ERS", tmpPath, f)) {
    LOG_ERR("ERS", "Could not open progress temp file for write!");
    return false;
  }
  uint8_t data[6];
  data[0] = spineIndex & 0xFF;
  data[1] = (spineIndex >> 8) & 0xFF;
  data[2] = pageNumber & 0xFF;
  data[3] = (pageNumber >> 8) & 0xFF;
  data[4] = pageCount & 0xFF;
  data[5] = (pageCount >> 8) & 0xFF;
  const size_t written = f.write(data, sizeof(data));
  if (written != sizeof(data)) {
    LOG_ERR("ERS", "Short write saving progress: %u/%u bytes", (unsigned)written, (unsigned)sizeof(data));
    f.close();
    Storage.remove(tmpPath.c_str());
    return false;
  }
  f.flush();
  if (!f.sync()) {
    LOG_ERR("ERS", "Failed to sync progress temp file");
    f.close();
    Storage.remove(tmpPath.c_str());
    return false;
  }
  if (!f.close()) {
    LOG_ERR("ERS", "Failed to close progress temp file");
    Storage.remove(tmpPath.c_str());
    return false;
  }

  if (Storage.exists(backupPath.c_str()) && !Storage.remove(backupPath.c_str())) {
    LOG_ERR("ERS", "Could not remove old progress backup");
    Storage.remove(tmpPath.c_str());
    return false;
  }
  if (Storage.exists(progressPath.c_str()) && !Storage.rename(progressPath.c_str(), backupPath.c_str())) {
    LOG_ERR("ERS", "Could not rotate progress backup");
    Storage.remove(tmpPath.c_str());
    return false;
  }
  if (!Storage.rename(tmpPath.c_str(), progressPath.c_str())) {
    LOG_ERR("ERS", "Could not replace progress file");
    if (Storage.exists(backupPath.c_str()) && !Storage.exists(progressPath.c_str())) {
      Storage.rename(backupPath.c_str(), progressPath.c_str());
    }
    Storage.remove(tmpPath.c_str());
    return false;
  }

  if (pageCount > 0 && epub.getBookSize() > 0) {
    const float chapterProgress = std::min(1.0f, static_cast<float>(pageNumber + 1) / static_cast<float>(pageCount));
    const float progressPercent = epub.calculateProgress(spineIndex, chapterProgress) * 100.0f;
    if (!saveProgressPercent(epub.getCachePath(), progressPercent)) {
      LOG_ERR("ERS", "Could not save lightweight progress percentage");
    }
  }
  LOG_DBG("ERS", "Progress saved: spine=%d page=%d", spineIndex, pageNumber);
  return true;
}

}  // namespace EpubReaderUtils
