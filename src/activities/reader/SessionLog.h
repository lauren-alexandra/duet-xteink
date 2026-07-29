#pragma once

#include <DuetStoragePaths.h>

#include <cstdint>
#include <string>

#include "ReadingStatsUtils.h"

// Append-only full session history: one record per reading session with its
// exact local start time. Pure recorder for now — future analytics pages
// (hour-of-day heatmaps, session-start patterns, reading-rhythm charts) will
// read it once enough history has accumulated. Deliberately separate from
// ReadingJournal/ReadingLedger so a bug here can never damage existing stats.
class SessionLog {
 public:
  static constexpr const char* PATH = DUET_STATE_ROOT_PATH "/session_log_v1.bin";

  static bool append(const ReadingStatsDateTime& localStart, uint32_t readingSeconds, uint16_t screenPages,
                     const std::string& cachePath, const std::string& title);
};
