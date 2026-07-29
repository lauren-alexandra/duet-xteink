#pragma once

#include <DuetStoragePaths.h>

#include <string>

namespace ThumbnailCache {

// Keep the decimal FNV cache key stable, but bucket by its final two digits.
// The old first-digit bucket put most uint64 hashes in shard "1", creating
// multi-thousand-entry FAT directories and turning exact cache hits into long
// linear scans.
inline std::string shardKeyForHash(const std::string& hash) {
  if (hash.empty()) return "00";
  if (hash.size() == 1) return std::string("0") + hash;
  return hash.substr(hash.size() - 2);
}

inline std::string shardDirForHash(const std::string& hash) {
  return std::string(DUET_THUMBS_ROOT_PATH "/") + shardKeyForHash(hash);
}

inline std::string legacyShardDirForHash(const std::string& hash) {
  return std::string(DUET_LEGACY_STATE_ROOT_PATH "/thumbs/") + (hash.empty() ? '0' : hash[0]);
}

inline std::string pathForHash(const std::string& hash, const int width, const int height,
                               const bool adaptiveContain = false) {
  return shardDirForHash(hash) + "/" + hash + "_" + std::to_string(width) + "x" +
         std::to_string(height) + (adaptiveContain ? "_fit.bmp" : ".bmp");
}

inline std::string legacyPathForHash(const std::string& hash, const int width, const int height,
                                     const bool adaptiveContain = false) {
  return legacyShardDirForHash(hash) + "/" + hash + "_" + std::to_string(width) + "x" +
         std::to_string(height) + (adaptiveContain ? "_fit.bmp" : ".bmp");
}

}  // namespace ThumbnailCache
