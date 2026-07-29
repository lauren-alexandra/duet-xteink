#include "DuetStoragePaths.h"

namespace DuetStorage {
namespace {

bool hasPathPrefix(const std::string_view path, const std::string_view prefix) {
  return path == prefix ||
         (path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0 && path[prefix.size()] == '/');
}

std::string appendRelative(const std::string_view root, std::string_view relativePath) {
  while (!relativePath.empty() && relativePath.front() == '/') relativePath.remove_prefix(1);
  std::string path(root);
  if (!relativePath.empty()) {
    path.push_back('/');
    path.append(relativePath);
  }
  return path;
}

void addMappedCandidate(LegacyReadCandidates& candidates, const std::string_view canonicalPath,
                        const std::string_view canonicalRoot, const std::string_view legacyRoot) {
  if (candidates.count >= candidates.paths.size() || !hasPathPrefix(canonicalPath, canonicalRoot)) return;
  const std::string_view suffix = canonicalPath.substr(canonicalRoot.size());
  candidates.paths[candidates.count++] = std::string(legacyRoot) + std::string(suffix);
}

}  // namespace

bool isCanonicalPath(const std::string_view path) { return hasPathPrefix(path, ROOT); }

LegacyReadCandidates legacyReadCandidates(const std::string_view canonicalPath) {
  LegacyReadCandidates candidates;

  if (hasPathPrefix(canonicalPath, STATE_ROOT)) {
    addMappedCandidate(candidates, canonicalPath, STATE_ROOT, LEGACY_STATE_ROOT);
    addMappedCandidate(candidates, canonicalPath, STATE_ROOT, LEGACY_BOOKS_ROOT);
    return candidates;
  }
  if (hasPathPrefix(canonicalPath, BOOKS_ROOT)) {
    addMappedCandidate(candidates, canonicalPath, BOOKS_ROOT, LEGACY_BOOKS_ROOT);
    return candidates;
  }
  if (hasPathPrefix(canonicalPath, THUMBS_ROOT)) {
    addMappedCandidate(candidates, canonicalPath, THUMBS_ROOT, DUET_LEGACY_STATE_ROOT_PATH "/thumbs");
    return candidates;
  }
  if (hasPathPrefix(canonicalPath, LAYOUTS_ROOT)) {
    addMappedCandidate(candidates, canonicalPath, LAYOUTS_ROOT, DUET_LEGACY_STATE_ROOT_PATH "/layouts");
    return candidates;
  }
  if (hasPathPrefix(canonicalPath, FILE_INDEX_ROOT)) {
    addMappedCandidate(candidates, canonicalPath, FILE_INDEX_ROOT, DUET_LEGACY_STATE_ROOT_PATH "/fileindex");
    addMappedCandidate(candidates, canonicalPath, FILE_INDEX_ROOT, DUET_LEGACY_BOOKS_ROOT_PATH "/fileindex");
    return candidates;
  }
  if (hasPathPrefix(canonicalPath, STATS_BACKUP_ROOT)) {
    addMappedCandidate(candidates, canonicalPath, STATS_BACKUP_ROOT, LEGACY_STATS_BACKUP_ROOT);
  }
  return candidates;
}

std::string statePath(const std::string_view relativePath) { return appendRelative(STATE_ROOT, relativePath); }
std::string bookPath(const std::string_view relativePath) { return appendRelative(BOOKS_ROOT, relativePath); }
std::string thumbPath(const std::string_view relativePath) { return appendRelative(THUMBS_ROOT, relativePath); }
std::string layoutPath(const std::string_view relativePath) { return appendRelative(LAYOUTS_ROOT, relativePath); }
std::string fileIndexPath(const std::string_view relativePath) {
  return appendRelative(FILE_INDEX_ROOT, relativePath);
}
std::string statsBackupPath(const std::string_view relativePath) {
  return appendRelative(STATS_BACKUP_ROOT, relativePath);
}

std::string legacyBookPathForCanonical(const std::string_view canonicalPath) {
  if (!hasPathPrefix(canonicalPath, BOOKS_ROOT)) return {};
  return std::string(LEGACY_BOOKS_ROOT) + std::string(canonicalPath.substr(std::string_view(BOOKS_ROOT).size()));
}

std::string stableBookCacheIdentity(const std::string_view cachePath) {
  if (hasPathPrefix(cachePath, BOOKS_ROOT)) {
    return std::string(LEGACY_BOOKS_ROOT) + std::string(cachePath.substr(std::string_view(BOOKS_ROOT).size()));
  }
  return std::string(cachePath);
}

bool sameBookCacheIdentity(const std::string_view lhs, const std::string_view rhs) {
  if (lhs == rhs) return true;
  const auto relative = [](const std::string_view path) {
    if (hasPathPrefix(path, BOOKS_ROOT)) return path.substr(std::string_view(BOOKS_ROOT).size());
    if (hasPathPrefix(path, LEGACY_BOOKS_ROOT)) return path.substr(std::string_view(LEGACY_BOOKS_ROOT).size());
    return path;
  };
  return relative(lhs) == relative(rhs);
}

}  // namespace DuetStorage
