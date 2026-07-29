#include "DuetStorageMigration.h"

#include "DuetStoragePaths.h"

#ifndef SIMULATOR
#include <SdFat.h>
#define HAL_STORAGE_IMPL
#endif
#include <HalStorage.h>
#ifndef SIMULATOR
#undef HAL_STORAGE_IMPL
#endif
#include <Logging.h>

#include <cstring>
#include <string>

namespace DuetStorage {
namespace {

class DeviceMigrationBackend final : public MigrationBackend {
 public:
  bool exists(const char* path) override { return Storage.exists(path); }

  bool ensureDirectory(const char* path) override { return Storage.ensureDirectoryExists(path); }

  bool copyFileAtomically(const char* sourcePath, const char* destinationPath) override {
    if (Storage.exists(destinationPath)) return true;

    const std::string temporaryPath = std::string(destinationPath) + ".duet-import.tmp";
    if (Storage.exists(temporaryPath.c_str()) && !Storage.remove(temporaryPath.c_str())) return false;

    HalFile source;
    HalFile destination;
    if (!Storage.openFileForRead("DUETMIG", sourcePath, source)) return false;
    if (!Storage.openFileForWrite("DUETMIG", temporaryPath, destination)) {
      source.close();
      return false;
    }

    const uint64_t expectedSize = source.fileSize64();
    uint8_t buffer[512];
    uint64_t copied = 0;
    bool ok = true;
    while (source.available()) {
      const int bytesRead = source.read(buffer, sizeof(buffer));
      if (bytesRead <= 0) {
        ok = false;
        break;
      }
      const size_t bytesWritten = destination.write(buffer, static_cast<size_t>(bytesRead));
      if (bytesWritten != static_cast<size_t>(bytesRead)) {
        ok = false;
        break;
      }
      copied += bytesWritten;
    }
    source.close();
    if (ok) {
      destination.flush();
      ok = destination.sync();
    }
    ok = destination.close() && ok;

    if (!ok || copied != expectedSize) {
      Storage.remove(temporaryPath.c_str());
      return false;
    }

    HalFile verification;
    if (!Storage.openFileForRead("DUETMIG", temporaryPath, verification)) {
      Storage.remove(temporaryPath.c_str());
      return false;
    }
    const uint64_t actualSize = verification.fileSize64();
    verification.close();
    if (actualSize != expectedSize || !Storage.rename(temporaryPath.c_str(), destinationPath)) {
      Storage.remove(temporaryPath.c_str());
      return false;
    }
    return true;
  }

  bool mergeDirectory(const char* sourcePath, const char* destinationPath) override {
    if (Storage.exists(destinationPath)) {
      return mergeDirectoryAtDepth(sourcePath, destinationPath, 0);
    }

    // Keep an interrupted first import invisible. Until this temporary tree is
    // complete, normal reads continue falling back to the untouched legacy
    // directory. A reboot resumes the same bounded merge and promotes it with
    // one metadata rename.
    const std::string temporaryPath = std::string(destinationPath) + ".duet-import.tmp";
    if (!mergeDirectoryAtDepth(sourcePath, temporaryPath, 0)) return false;
    if (Storage.exists(destinationPath)) {
      return mergeDirectoryAtDepth(sourcePath, destinationPath, 0);
    }
    return Storage.rename(temporaryPath.c_str(), destinationPath);
  }

  bool writeCompletionMarker(const char* path) override {
    const std::string temporaryPath = std::string(path) + ".tmp";
    if (Storage.exists(temporaryPath.c_str()) && !Storage.remove(temporaryPath.c_str())) return false;
    if (!Storage.writeFile(temporaryPath.c_str(), String("legacy-import-v1\n"))) return false;
    return Storage.rename(temporaryPath.c_str(), path);
  }

 private:
  bool mergeDirectoryAtDepth(const std::string& sourcePath, const std::string& destinationPath, const uint8_t depth) {
    constexpr uint8_t MAX_DEPTH = 6;
    if (depth > MAX_DEPTH || !Storage.ensureDirectoryExists(destinationPath.c_str())) return false;

    HalFile directory = Storage.open(sourcePath.c_str());
    if (!directory || !directory.isDirectory()) {
      directory.close();
      return false;
    }

    bool ok = true;
    char name[160];
    for (HalFile entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
      const bool isDirectory = entry.isDirectory();
      const size_t nameLength = entry.getName(name, sizeof(name));
      entry.close();
      if (nameLength == 0 || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

      const std::string sourceChild = sourcePath + "/" + name;
      const std::string destinationChild = destinationPath + "/" + name;
      if (isDirectory) {
        if (!mergeDirectoryAtDepth(sourceChild, destinationChild, depth + 1)) ok = false;
      } else if (!Storage.exists(destinationChild.c_str()) &&
                 !copyFileAtomically(sourceChild.c_str(), destinationChild.c_str())) {
        ok = false;
      }
    }
    directory.close();
    return ok;
  }
};

}  // namespace

MigrationReport migrateLegacyNamespaceOnDevice() {
  DeviceMigrationBackend backend;
  const MigrationReport report = migrateLegacyNamespace(backend);
  if (report.complete) {
    LOG_INF("DUETMIG", "Namespace migration %s: files=%u dirs=%u",
            report.alreadyComplete ? "already complete" : "complete", static_cast<unsigned>(report.filesCopied),
            static_cast<unsigned>(report.directoriesMerged));
  } else {
    LOG_ERR("DUETMIG", "Namespace migration incomplete: files=%u dirs=%u failures=%u; legacy fallback remains active",
            static_cast<unsigned>(report.filesCopied), static_cast<unsigned>(report.directoriesMerged),
            static_cast<unsigned>(report.failures));
  }
  return report;
}

bool copyLegacyFileToCanonical(const char* sourcePath, const char* destinationPath) {
  DeviceMigrationBackend backend;
  return backend.copyFileAtomically(sourcePath, destinationPath);
}

bool migrateLegacyBookStateOnDevice(const char* canonicalCachePath) {
  if (canonicalCachePath == nullptr) return false;
  const std::string legacyCachePath = legacyBookPathForCanonical(canonicalCachePath);
  if (legacyCachePath.empty() || !Storage.exists(legacyCachePath.c_str())) return true;

  static constexpr const char* DURABLE_BOOK_FILES[] = {
      "progress.bin", "progress.bin.bak", "progress_pct.bin", "reader_settings.bin",
      "stats.bin",    "stats_v1.bin",     "stats_v2.bin",     "stats_v3.bin",
      "stats_v4.bin", "stats_v5.bin",     "stats_v6.bin",     "stats_v7.bin",
  };

  if (!Storage.ensureDirectoryExists(ROOT) || !Storage.ensureDirectoryExists(BOOKS_ROOT) ||
      !Storage.ensureDirectoryExists(canonicalCachePath)) {
    return false;
  }

  DeviceMigrationBackend backend;
  bool ok = true;
  for (const char* fileName : DURABLE_BOOK_FILES) {
    const std::string source = legacyCachePath + "/" + fileName;
    if (!Storage.exists(source.c_str())) continue;
    const std::string destination = std::string(canonicalCachePath) + "/" + fileName;
    if (!backend.copyFileAtomically(source.c_str(), destination.c_str())) ok = false;
  }
  return ok;
}

}  // namespace DuetStorage
