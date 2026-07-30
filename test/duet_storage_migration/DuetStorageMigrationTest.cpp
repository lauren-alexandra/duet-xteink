#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "DuetStorageMigration.h"
#include "DuetStoragePaths.h"

namespace {

class FakeMigrationBackend final : public DuetStorage::MigrationBackend {
 public:
  bool exists(const char* path) override { return files.count(path) != 0 || directories.count(path) != 0; }

  bool ensureDirectory(const char* path) override {
    directories.insert(path);
    return true;
  }

  bool copyFileAtomically(const char* sourcePath, const char* destinationPath) override {
    if (exists(destinationPath)) return true;
    copyAttempts++;
    if (failCopyAttempt != 0 && copyAttempts == failCopyAttempt) return false;
    const auto source = files.find(sourcePath);
    if (source == files.end()) return false;
    files[destinationPath] = source->second;
    addParentDirectories(destinationPath);
    return true;
  }

  bool mergeDirectory(const char* sourcePath, const char* destinationPath) override {
    if (!exists(sourcePath)) return false;
    if (failDirectoryMerge) return false;

    directories.insert(destinationPath);
    std::vector<std::pair<std::string, std::string>> pendingFiles;
    const std::string prefix = std::string(sourcePath) + "/";
    for (const auto& [path, contents] : files) {
      if (path.rfind(prefix, 0) != 0) continue;
      pendingFiles.emplace_back(std::string(destinationPath) + path.substr(std::string(sourcePath).size()), contents);
    }
    for (const auto& [path, contents] : pendingFiles) {
      files.emplace(path, contents);
      addParentDirectories(path);
    }
    return true;
  }

  bool writeCompletionMarker(const char* path) override {
    if (failMarkerWrite) return false;
    files[path] = "legacy-import-v1\n";
    addParentDirectories(path);
    return true;
  }

  void addFile(const std::string& path, const std::string& contents) {
    files[path] = contents;
    addParentDirectories(path);
  }

  std::string read(const std::string& path) const {
    const auto found = files.find(path);
    return found == files.end() ? std::string{} : found->second;
  }

  std::string readCompatible(const std::string& canonicalPath) const {
    const auto canonical = files.find(canonicalPath);
    if (canonical != files.end()) return canonical->second;
    const auto candidates = DuetStorage::legacyReadCandidates(canonicalPath);
    for (size_t i = 0; i < candidates.count; ++i) {
      const auto legacy = files.find(candidates.paths[i]);
      if (legacy != files.end()) return legacy->second;
    }
    return {};
  }

  std::map<std::string, std::string> files;
  std::set<std::string> directories;
  size_t copyAttempts = 0;
  size_t failCopyAttempt = 0;
  bool failDirectoryMerge = false;
  bool failMarkerWrite = false;

 private:
  void addParentDirectories(const std::string& path) {
    size_t separator = path.find('/', 1);
    while (separator != std::string::npos) {
      directories.insert(path.substr(0, separator));
      separator = path.find('/', separator + 1);
    }
  }
};

TEST(DuetStorageMigration, FreshCardCreatesCanonicalTree) {
  FakeMigrationBackend backend;
  const auto report = DuetStorage::migrateLegacyNamespace(backend);

  EXPECT_TRUE(report.complete);
  EXPECT_FALSE(report.alreadyComplete);
  EXPECT_EQ(report.failures, 0);
  EXPECT_TRUE(backend.exists(DuetStorage::STATE_ROOT));
  EXPECT_TRUE(backend.exists(DuetStorage::BOOKS_ROOT));
  EXPECT_TRUE(backend.exists(DuetStorage::THUMBS_ROOT));
  EXPECT_TRUE(backend.exists(DuetStorage::MIGRATION_MARKER));
}

TEST(DuetStorageMigration, CrossinkOnlyImportsGlobalStateAndKeepsSource) {
  FakeMigrationBackend backend;
  backend.addFile("/.crossink/settings.bin", "settings");
  backend.addFile("/.crossink/global_stats.bin", "stats");
  backend.addFile("/.crossink/achievements.bin", "achievements");
  backend.addFile("/.crossink/last_active_book.txt", "/Books/Current.epub");
  backend.addFile("/.crossink/koreader.bin", "sync");
  backend.addFile("/.crossink/library_catalog.tsv", "catalog");
  backend.addFile("/.crossink/synced_stats/device_a.bin", "peer");
  backend.addFile("/.crossink/synced_achievements/device_a.bin", "peer-achievements");

  const auto report = DuetStorage::migrateLegacyNamespace(backend);

  ASSERT_TRUE(report.complete);
  EXPECT_EQ(backend.read("/.duet/state/settings.bin"), "settings");
  EXPECT_EQ(backend.read("/.duet/state/global_stats.bin"), "stats");
  EXPECT_EQ(backend.read("/.duet/state/achievements.bin"), "achievements");
  EXPECT_EQ(backend.read("/.duet/state/last_active_book.txt"), "/Books/Current.epub");
  EXPECT_EQ(backend.read("/.duet/state/koreader.bin"), "sync");
  EXPECT_EQ(backend.read("/.duet/state/library_catalog.tsv"), "catalog");
  EXPECT_EQ(backend.read("/.duet/state/synced_stats/device_a.bin"), "peer");
  EXPECT_EQ(backend.read("/.duet/state/synced_achievements/device_a.bin"), "peer-achievements");
  EXPECT_EQ(backend.read("/.crossink/settings.bin"), "settings");
}

TEST(DuetStorageMigration, CrosspointOnlyImportsOldGlobalStateAndReadsPerBookStateLazily) {
  FakeMigrationBackend backend;
  backend.addFile("/.crosspoint/state.bin", "current-book-state");
  backend.addFile("/.crosspoint/reading_journal.bin", "journal");
  backend.addFile("/.crosspoint/epub_123/progress.bin", "position");
  backend.addFile("/.crosspoint/epub_123/reader_settings.bin", "book-settings");

  const auto report = DuetStorage::migrateLegacyNamespace(backend);

  ASSERT_TRUE(report.complete);
  EXPECT_EQ(backend.read("/.duet/state/state.bin"), "current-book-state");
  EXPECT_EQ(backend.read("/.duet/state/reading_journal.bin"), "journal");
  EXPECT_EQ(backend.readCompatible("/.duet/books/epub_123/progress.bin"), "position");
  EXPECT_EQ(backend.readCompatible("/.duet/books/epub_123/reader_settings.bin"), "book-settings");
  EXPECT_EQ(backend.read("/.crosspoint/epub_123/progress.bin"), "position");
}

TEST(DuetStorageMigration, BothLegacyFoldersUseNewerHotStateThenFillMissingOldState) {
  FakeMigrationBackend backend;
  backend.addFile("/.crossink/settings.bin", "newer-settings");
  backend.addFile("/.crosspoint/settings.bin", "older-settings");
  backend.addFile("/.crosspoint/recent.bin", "older-recents");

  const auto report = DuetStorage::migrateLegacyNamespace(backend);

  ASSERT_TRUE(report.complete);
  EXPECT_EQ(backend.read("/.duet/state/settings.bin"), "newer-settings");
  EXPECT_EQ(backend.read("/.duet/state/recent.bin"), "older-recents");
}

TEST(DuetStorageMigration, PartiallyCompletedMigrationFillsOnlyMissingFiles) {
  FakeMigrationBackend backend;
  backend.addFile("/.duet/state/settings.bin", "canonical-settings");
  backend.addFile("/.crossink/settings.bin", "legacy-settings");
  backend.addFile("/.crossink/achievements.bin", "legacy-achievements");

  const auto report = DuetStorage::migrateLegacyNamespace(backend);

  ASSERT_TRUE(report.complete);
  EXPECT_EQ(backend.read("/.duet/state/settings.bin"), "canonical-settings");
  EXPECT_EQ(backend.read("/.duet/state/achievements.bin"), "legacy-achievements");
}

TEST(DuetStorageMigration, ExistingDuetStateWinsWhileLegacyCachesRemainReadable) {
  FakeMigrationBackend backend;
  backend.addFile("/.duet/state/global_stats.bin", "canonical-stats");
  backend.addFile("/.crossink/global_stats.bin", "legacy-stats");
  backend.addFile("/.crossink/thumbs/a/abc_123x180.bmp", "cover");
  backend.addFile("/.crossink/layouts/a/epub_abc/page.bin", "layout");

  const auto report = DuetStorage::migrateLegacyNamespace(backend);

  ASSERT_TRUE(report.complete);
  EXPECT_EQ(backend.read("/.duet/state/global_stats.bin"), "canonical-stats");
  EXPECT_EQ(backend.readCompatible("/.duet/cache/thumbs/a/abc_123x180.bmp"), "cover");
  EXPECT_EQ(backend.readCompatible("/.duet/cache/layouts/a/epub_abc/page.bin"), "layout");
}

TEST(DuetStorageMigration, CompletedMigrationIsIdempotentAcrossReboot) {
  FakeMigrationBackend backend;
  backend.addFile("/.crossink/settings.bin", "settings");
  ASSERT_TRUE(DuetStorage::migrateLegacyNamespace(backend).complete);
  const size_t copiesAfterFirstBoot = backend.copyAttempts;

  const auto secondBoot = DuetStorage::migrateLegacyNamespace(backend);

  EXPECT_TRUE(secondBoot.complete);
  EXPECT_TRUE(secondBoot.alreadyComplete);
  EXPECT_EQ(backend.copyAttempts, copiesAfterFirstBoot);
  EXPECT_EQ(backend.read("/.duet/state/settings.bin"), "settings");
}

TEST(DuetStorageMigration, InterruptedMigrationRetriesWithoutOverwritingCopiedData) {
  FakeMigrationBackend backend;
  backend.addFile("/.crossink/settings.bin", "settings");
  backend.addFile("/.crossink/global_stats.bin", "stats");
  backend.failCopyAttempt = 2;

  const auto interrupted = DuetStorage::migrateLegacyNamespace(backend);
  EXPECT_FALSE(interrupted.complete);
  EXPECT_FALSE(backend.exists(DuetStorage::MIGRATION_MARKER));
  EXPECT_EQ(backend.read("/.duet/state/global_stats.bin"), "stats");

  backend.failCopyAttempt = 0;
  const auto retry = DuetStorage::migrateLegacyNamespace(backend);

  EXPECT_TRUE(retry.complete);
  EXPECT_EQ(backend.read("/.duet/state/settings.bin"), "settings");
  EXPECT_EQ(backend.read("/.duet/state/global_stats.bin"), "stats");
  EXPECT_EQ(backend.read("/.crossink/global_stats.bin"), "stats");
}

TEST(DuetStorageMigration, BookIdentityDoesNotChangeWithNamespace) {
  EXPECT_EQ(DuetStorage::stableBookCacheIdentity("/.duet/books/epub_123"), "/.crosspoint/epub_123");
  EXPECT_TRUE(DuetStorage::sameBookCacheIdentity("/.duet/books/epub_123", "/.crosspoint/epub_123"));
  EXPECT_FALSE(DuetStorage::sameBookCacheIdentity("/.duet/books/epub_123", "/.crosspoint/epub_456"));
}

}  // namespace
