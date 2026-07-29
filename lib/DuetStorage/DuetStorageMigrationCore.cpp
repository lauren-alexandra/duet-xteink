#include "DuetStorageMigration.h"

#include "DuetStoragePaths.h"

#include <array>
#include <string>

namespace DuetStorage {
namespace {

constexpr const char* STATE_FILES[] = {
    "achievements.bin",
    "achievements.bin.bak",
    "boot_timing.txt",
    "crossink-settings.json",
    "dictionary_config.json",
    "dictionary_history.txt",
    "desktop_cover_prefill.json",
    "favorites.json",
    "font_catalog_v1.bin",
    "font_catalog_v1.tmp",
    "global_stats.bin",
    "global_stats.bin.bak",
    "home_carousel_cache.bin",
    "home_carousel_cache.tmp",
    "home_timing.txt",
    "installed_firmware.txt",
    "koreader.bin",
    "koreader.bin.bak",
    "koreader.json",
    "language.bin",
    "language.bin.bak",
    "last_active_book.txt",
    "launcher_layout.bin",
    "launcher_layout.bin.bak",
    "library_book_aliases_v1.bin",
    "library_book_aliases_v1.tmp",
    "library_book_details_v1.bin",
    "library_book_details_v1.clean",
    "library_book_details_v1.tmp",
    "library_book_stats_v1.bin",
    "library_book_stats_v1.tmp",
    "library_catalog.tsv",
    "library_insights_v1.bin",
    "library_insights_v1.tmp",
    "nearby_sync_timing.txt",
    "opds.json",
    "picker_hb.txt",
    "picker_timing.txt",
    "purge_hb.txt",
    "reader_timing.txt",
    "reading_journal.bin",
    "reading_journal.bin.bak",
    "reading_ledger_v1.bin",
    "reading_stats_clock_v1.bin",
    "reading_stats_clock_v1.bin.bak",
    "reading_stats_clock_v1.bin.tmp",
    "recent.bin",
    "recent.bin.bak",
    "recent.json",
    "session_log_v1.bin",
    "settings.bin",
    "settings.bin.bak",
    "settings.json",
    "sleep_frame.bin",
    "sleep_image_index_v1.bin",
    "sleep_image_index_v1.tmp",
    "state.bin",
    "state.bin.bak",
    "state.json",
    "sync_prep_timing.txt",
    "usb-upload.tmp",
    "wifi.bin",
    "wifi.bin.bak",
    "wifi.json",
};

constexpr const char* STATE_DIRECTORIES[] = {
    "bookmarks",          "clippings",       "synced_book_details",
    "synced_book_stats",  "synced_journals", "synced_ledgers",
    "synced_names",       "synced_stats",     "synced_stats_dates",
};

std::string joinPath(const char* root, const char* relative) {
  std::string path(root);
  path.push_back('/');
  path.append(relative);
  return path;
}

void importFile(MigrationBackend& backend, const char* relative, MigrationReport& report) {
  const std::string destination = joinPath(STATE_ROOT, relative);
  if (backend.exists(destination.c_str())) return;

  const std::array<const char*, 2> roots = {LEGACY_STATE_ROOT, LEGACY_BOOKS_ROOT};
  for (const char* root : roots) {
    const std::string source = joinPath(root, relative);
    if (!backend.exists(source.c_str())) continue;
    if (backend.copyFileAtomically(source.c_str(), destination.c_str())) {
      report.filesCopied++;
    } else {
      report.failures++;
    }
    return;
  }
}

void importDirectory(MigrationBackend& backend, const char* relative, MigrationReport& report) {
  const std::string destination = joinPath(STATE_ROOT, relative);
  bool ok = true;
  bool found = false;

  // Import the newer hot-state tree first. Missing entries from the older
  // book-cache tree may then be merged without replacing canonical data.
  const std::array<const char*, 2> roots = {LEGACY_STATE_ROOT, LEGACY_BOOKS_ROOT};
  for (const char* root : roots) {
    const std::string source = joinPath(root, relative);
    if (!backend.exists(source.c_str())) continue;
    found = true;
    if (!backend.mergeDirectory(source.c_str(), destination.c_str())) {
      report.failures++;
      ok = false;
    }
  }
  if (found && ok) report.directoriesMerged++;
}

}  // namespace

MigrationReport migrateLegacyNamespace(MigrationBackend& backend) {
  MigrationReport report;
  if (backend.exists(MIGRATION_MARKER)) {
    report.complete = true;
    report.alreadyComplete = true;
    return report;
  }

  constexpr const char* REQUIRED_DIRECTORIES[] = {
      ROOT, STATE_ROOT, BOOKS_ROOT, CACHE_ROOT, THUMBS_ROOT, LAYOUTS_ROOT, FILE_INDEX_ROOT,
      BACKUPS_ROOT, STATS_BACKUP_ROOT, MIGRATION_ROOT,
  };
  for (const char* directory : REQUIRED_DIRECTORIES) {
    if (!backend.ensureDirectory(directory)) report.failures++;
  }

  for (const char* relative : STATE_FILES) importFile(backend, relative, report);
  for (const char* relative : STATE_DIRECTORIES) importDirectory(backend, relative, report);

  // The file index is small and hot enough to import eagerly. Per-book caches,
  // layout shards, thumbnails, and dated backups remain lazy read fallbacks.
  bool importedFileIndex = true;
  for (const char* legacyRoot : {DUET_LEGACY_STATE_ROOT_PATH "/fileindex",
                                 DUET_LEGACY_BOOKS_ROOT_PATH "/fileindex"}) {
    if (backend.exists(legacyRoot) && !backend.mergeDirectory(legacyRoot, FILE_INDEX_ROOT)) {
      report.failures++;
      importedFileIndex = false;
    }
  }
  if (importedFileIndex &&
      (backend.exists(DUET_LEGACY_STATE_ROOT_PATH "/fileindex") ||
       backend.exists(DUET_LEGACY_BOOKS_ROOT_PATH "/fileindex"))) {
    report.directoriesMerged++;
  }

  if (report.failures == 0 && backend.writeCompletionMarker(MIGRATION_MARKER)) {
    report.complete = true;
  } else if (report.failures == 0) {
    report.failures++;
  }
  return report;
}

}  // namespace DuetStorage
