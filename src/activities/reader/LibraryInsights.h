#pragma once

#include <DuetStoragePaths.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

struct BookReadingStats;

struct LibraryInsightItem {
  std::string name;
  uint32_t readingSeconds = 0;
  uint16_t books = 0;
  uint16_t reading = 0;
  uint16_t finished = 0;
};

struct LibraryBookStatus {
  uint32_t readingSeconds = 0;
  uint16_t sessions = 0;
  bool completed = false;
  bool hasProgress = false;
};

class LibraryInsights {
 public:
  static constexpr const char* CATALOG_PATH = DUET_STATE_ROOT_PATH "/library_catalog.tsv";
  static constexpr const char* CACHE_PATH = DUET_STATE_ROOT_PATH "/library_insights_v1.bin";
  static constexpr size_t TOP_GENRE_COUNT = 4;
  static constexpr size_t TOP_AUTHOR_COUNT = 4;
  static constexpr size_t SPICE_COUNT = 8;
  static constexpr size_t SERIES_COUNT = 48;

  bool available = false;
  uint16_t totalBooks = 0;
  uint16_t unreadBooks = 0;
  uint16_t readingBooks = 0;
  uint16_t finishedBooks = 0;
  uint16_t seriesStarted = 0;
  uint32_t totalReadingSeconds = 0;

  std::array<LibraryInsightItem, TOP_GENRE_COUNT> topGenres{};
  size_t topGenreCount = 0;
  std::array<LibraryInsightItem, TOP_AUTHOR_COUNT> topAuthors{};
  size_t topAuthorCount = 0;
  std::array<LibraryInsightItem, SPICE_COUNT> spiceLevels{};
  size_t spiceLevelCount = 0;
  std::array<LibraryInsightItem, SERIES_COUNT> seriesProgress{};
  size_t seriesProgressCount = 0;

  static std::unique_ptr<LibraryInsights> load();
  static bool lookupBookStatus(const std::string& cachePath, LibraryBookStatus& status);
  static void mergeSyncedBookStats(const std::string& cachePath, BookReadingStats& stats);
  // Visits every record in the local detailed snapshot plus each synced peer
  // snapshot, deduplicated by book key (local record wins). For date-based
  // library summaries such as started/finished-per-month.
  using DetailedBookStatsVisitor = void (*)(uint64_t key, const BookReadingStats& stats, void* ctx);
  static void forEachDetailedBookStats(DetailedBookStatsVisitor visitor, void* ctx);
  // Stable hash key for a book cache path (the key used in the stats index
  // and detailed snapshots).
  static uint64_t keyForCachePath(const std::string& cachePath);
  // Preserve the original stats identity when a finished EPUB moves to
  // /Read. Both the original and moved path then resolve to one sync record.
  static bool registerMovedBookStatsAlias(const std::string& oldCachePath, const std::string& newCachePath);
  static void updateBookStatsIndex(const std::string& cachePath, const BookReadingStats& stats);
  static bool publishLocalBookStatsIndexForSync();
  // Progress hook: called periodically during the publication walk with
  // (scannedDirs, writtenRecords); return false to abort cleanly.
  using SyncPrepProgress = bool (*)(void* ctx, uint16_t scanned, uint16_t written);
  static bool publishLocalDetailedBookStatsForSync(SyncPrepProgress progress = nullptr, void* progressCtx = nullptr);
  // Updates one book's record in the published detail snapshot in place (or
  // appends it), so ordinary reading never invalidates the snapshot and no
  // full library crawl is needed on the next sync. Returns false when the
  // snapshot is absent/corrupt — caller then invalidates the snapshot marker.
  static bool updateDetailedBookStatsInPlace(const std::string& cachePath, const BookReadingStats& stats);
  // Explicitly force the next sync to rebuild the detail snapshot (stats
  // restore/replace flows only).
  static void invalidateDetailedStatsSnapshot();
  static void invalidateCache();
};
