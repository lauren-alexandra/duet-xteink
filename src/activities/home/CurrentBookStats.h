#pragma once

#include <cstdint>
#include <string>

#include "activities/reader/BookReadingStats.h"

// The Home and Apps launchers use this to show the last readable book's
// stats even when the reader is not currently open.
struct CurrentBookStatsTarget {
  std::string path;
  std::string title;
  std::string cachePath;
  BookReadingStats stats;
  float progressPercent = -1.0f;
  uint32_t wordCount = 0;
};

namespace CurrentBookStats {

void markLastActive(const std::string& path);
bool loadLastActivePath(std::string& path);

// Resolves the active reader path first, then the newest still-present book in
// Recent Books. Returns false only when no EPUB/XTC book can be opened.
bool loadLastActive(CurrentBookStatsTarget& target);

}  // namespace CurrentBookStats
