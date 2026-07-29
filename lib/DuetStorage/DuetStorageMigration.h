#pragma once

#include <cstddef>
#include <cstdint>

namespace DuetStorage {

struct MigrationReport {
  bool complete = false;
  bool alreadyComplete = false;
  uint16_t filesCopied = 0;
  uint16_t directoriesMerged = 0;
  uint16_t failures = 0;
};

class MigrationBackend {
 public:
  virtual ~MigrationBackend() = default;
  virtual bool exists(const char* path) = 0;
  virtual bool ensureDirectory(const char* path) = 0;
  virtual bool copyFileAtomically(const char* sourcePath, const char* destinationPath) = 0;
  virtual bool mergeDirectory(const char* sourcePath, const char* destinationPath) = 0;
  virtual bool writeCompletionMarker(const char* path) = 0;
};

MigrationReport migrateLegacyNamespace(MigrationBackend& backend);
MigrationReport migrateLegacyNamespaceOnDevice();
bool copyLegacyFileToCanonical(const char* sourcePath, const char* destinationPath);
bool migrateLegacyBookStateOnDevice(const char* canonicalCachePath);

}  // namespace DuetStorage
