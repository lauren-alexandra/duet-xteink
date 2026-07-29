#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

// Keep the canonical namespace literals in one place. The *_PATH macros are
// available for compile-time string concatenation in persisted-path constants.
#define DUET_STORAGE_ROOT_PATH "/.duet"
#define DUET_STATE_ROOT_PATH DUET_STORAGE_ROOT_PATH "/state"
#define DUET_BOOKS_ROOT_PATH DUET_STORAGE_ROOT_PATH "/books"
#define DUET_CACHE_ROOT_PATH DUET_STORAGE_ROOT_PATH "/cache"
#define DUET_THUMBS_ROOT_PATH DUET_CACHE_ROOT_PATH "/thumbs"
#define DUET_LAYOUTS_ROOT_PATH DUET_CACHE_ROOT_PATH "/layouts"
#define DUET_FILE_INDEX_ROOT_PATH DUET_CACHE_ROOT_PATH "/fileindex"
#define DUET_BACKUPS_ROOT_PATH DUET_STORAGE_ROOT_PATH "/backups"
#define DUET_STATS_BACKUP_ROOT_PATH DUET_BACKUPS_ROOT_PATH "/reading-stats"
#define DUET_MIGRATION_ROOT_PATH DUET_STORAGE_ROOT_PATH "/migration"

#define DUET_LEGACY_STATE_ROOT_PATH "/.crossink"
#define DUET_LEGACY_BOOKS_ROOT_PATH "/.crosspoint"
#define DUET_LEGACY_STATS_BACKUP_ROOT_PATH "/.crossink-stats-backup"

namespace DuetStorage {

inline constexpr char ROOT[] = DUET_STORAGE_ROOT_PATH;
inline constexpr char STATE_ROOT[] = DUET_STATE_ROOT_PATH;
inline constexpr char BOOKS_ROOT[] = DUET_BOOKS_ROOT_PATH;
inline constexpr char CACHE_ROOT[] = DUET_CACHE_ROOT_PATH;
inline constexpr char THUMBS_ROOT[] = DUET_THUMBS_ROOT_PATH;
inline constexpr char LAYOUTS_ROOT[] = DUET_LAYOUTS_ROOT_PATH;
inline constexpr char FILE_INDEX_ROOT[] = DUET_FILE_INDEX_ROOT_PATH;
inline constexpr char BACKUPS_ROOT[] = DUET_BACKUPS_ROOT_PATH;
inline constexpr char STATS_BACKUP_ROOT[] = DUET_STATS_BACKUP_ROOT_PATH;
inline constexpr char MIGRATION_ROOT[] = DUET_MIGRATION_ROOT_PATH;
inline constexpr char MIGRATION_MARKER[] = DUET_MIGRATION_ROOT_PATH "/legacy-import-v1.complete";

inline constexpr char LEGACY_STATE_ROOT[] = DUET_LEGACY_STATE_ROOT_PATH;
inline constexpr char LEGACY_BOOKS_ROOT[] = DUET_LEGACY_BOOKS_ROOT_PATH;
inline constexpr char LEGACY_STATS_BACKUP_ROOT[] = DUET_LEGACY_STATS_BACKUP_ROOT_PATH;

struct LegacyReadCandidates {
  std::array<std::string, 2> paths{};
  size_t count = 0;
};

bool isCanonicalPath(std::string_view path);
LegacyReadCandidates legacyReadCandidates(std::string_view canonicalPath);

std::string statePath(std::string_view relativePath);
std::string bookPath(std::string_view relativePath);
std::string thumbPath(std::string_view relativePath);
std::string layoutPath(std::string_view relativePath);
std::string fileIndexPath(std::string_view relativePath);
std::string statsBackupPath(std::string_view relativePath);
std::string legacyBookPathForCanonical(std::string_view canonicalPath);
std::string stableBookCacheIdentity(std::string_view cachePath);
bool sameBookCacheIdentity(std::string_view lhs, std::string_view rhs);

}  // namespace DuetStorage
