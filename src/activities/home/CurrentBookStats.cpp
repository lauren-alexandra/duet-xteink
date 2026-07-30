#include "CurrentBookStats.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Xtc.h>

#include <mutex>
#include <string>

#include "CrossPointState.h"
#include "RecentBookProgress.h"
#include "RecentBooksStore.h"

namespace {

constexpr char LAST_ACTIVE_BOOK_PATH[] = DUET_STATE_ROOT_PATH "/last_active_book.txt";

bool supportsBookStats(const std::string& path) {
  return FsHelpers::hasEpubExtension(path) || FsHelpers::hasXtcExtension(path);
}

std::string cachePathFor(const std::string& path) {
  if (FsHelpers::hasEpubExtension(path)) {
    return Epub::cachePathForFilePath(path, DUET_BOOKS_ROOT_PATH "");
  }
  if (FsHelpers::hasXtcExtension(path)) {
    return Xtc(path, DUET_BOOKS_ROOT_PATH "").getCachePath();
  }
  return {};
}

std::string titleFromPath(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  const size_t start = slash == std::string::npos ? 0 : slash + 1;
  const size_t dot = path.find_last_of('.');
  const size_t end = dot != std::string::npos && dot > start ? dot : path.size();
  return path.substr(start, end - start);
}

bool loadTarget(const RecentBook& book, CurrentBookStatsTarget& target) {
  if (!supportsBookStats(book.path) || !Storage.exists(book.path.c_str())) {
    return false;
  }

  const std::string cachePath = cachePathFor(book.path);
  if (cachePath.empty()) {
    return false;
  }

  target.path = book.path;
  target.title = book.title.empty() ? titleFromPath(book.path) : book.title;
  target.cachePath = cachePath;
  target.stats = BookReadingStats::load(cachePath);
  target.progressPercent = RecentBookProgress::loadPercent(book);
  target.wordCount = target.stats.totalWordCount;
  if (target.wordCount == 0 && FsHelpers::hasEpubExtension(book.path)) {
    Epub epub(book.path, DUET_BOOKS_ROOT_PATH "");
    if (epub.load(false, true)) {
      target.wordCount = epub.getTotalWords();
    }
  }
  return true;
}

}  // namespace

namespace CurrentBookStats {

void markLastActive(const std::string& path) {
  if (!supportsBookStats(path)) return;
  // Runs on every book open; skip the rewrite when the same book is reopened.
  // RAM memo only — a fresh boot writes once even if the file already matches.
  // Memoized only on success so a failed write is retried on the next open.
  static std::string lastWrittenPath;
  if (lastWrittenPath == path) return;
  Storage.mkdir(DUET_STATE_ROOT_PATH "");
  if (!Storage.writeFile(LAST_ACTIVE_BOOK_PATH, String(path.c_str()))) {
    LOG_ERR("CBS", "Failed to write last-active book file");
    return;
  }
  lastWrittenPath = path;
}

bool loadLastActivePath(std::string& path) {
  path.clear();
  if (!Storage.existsForRead(LAST_ACTIVE_BOOK_PATH)) return false;
  String savedPath = Storage.readFile(LAST_ACTIVE_BOOK_PATH);
  savedPath.trim();
  if (savedPath.isEmpty()) return false;
  path = savedPath.c_str();
  return supportsBookStats(path) && Storage.exists(path.c_str());
}

bool loadLastActive(CurrentBookStatsTarget& target) {
  target = CurrentBookStatsTarget{};

  std::string activePath;
  loadLastActivePath(activePath);
  {
    std::lock_guard<std::mutex> lock(APP_STATE.getMutex());
    if (activePath.empty()) activePath = APP_STATE.openEpubPath;
  }

  const auto& recentBooks = RECENT_BOOKS.getBooks();
  if (!activePath.empty()) {
    for (const RecentBook& book : recentBooks) {
      if (book.path == activePath && loadTarget(book, target)) {
        return true;
      }
    }

    if (supportsBookStats(activePath)) {
      const RecentBook activeBook{activePath, titleFromPath(activePath), "", ""};
      if (loadTarget(activeBook, target)) {
        return true;
      }
    }
  }

  for (const RecentBook& book : recentBooks) {
    if (loadTarget(book, target)) {
      return true;
    }
  }

  return false;
}

}  // namespace CurrentBookStats
