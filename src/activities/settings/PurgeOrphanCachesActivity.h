#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"

// Moves book cache directories whose book no longer exists into
// Duet's per-book attic. Nothing is deleted: orphaned stats and bookmarks stay
// recoverable. A crowded cache root makes every FAT path resolution a linear
// scan, so shrinking it speeds up the whole firmware.
class PurgeOrphanCachesActivity final : public Activity {
 public:
  explicit PurgeOrphanCachesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("PurgeOrphanCaches", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum State { WARNING, SCANNING, PURGING, SUCCESS, FAILED };
  State state = WARNING;
  // Hashes of live cache paths, not strings: 300 strings cost ~20 KB of
  // heap; 300 hashes cost 2.4 KB. The purge itself works in small batches —
  // holding 2,400 orphan names at once needed a 64 KB allocation and
  // aborted both devices.
  std::vector<uint64_t> liveCacheNames;
  uint16_t scannedBooks = 0;
  uint32_t visitedEntries = 0;
  bool scanIncomplete = false;
  uint16_t movedCount = 0;
  uint16_t keptCount = 0;
  uint16_t failedCount = 0;

  void goBack() { finish(); }
  void collectLiveCacheNames(const std::string& dirPath, int depth);
  void runPurge();
};
