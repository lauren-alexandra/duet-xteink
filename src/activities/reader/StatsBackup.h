#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Copies Duet's global stats to its reading-stats backup directory using a dated or
// incrementing filename. Returns true on success. When outFileName is provided,
// it receives the written filename without the directory prefix.
bool backupGlobalStats(bool manual, char* outFileName = nullptr, size_t outFileNameLen = 0);

// Deletes oldest backup files beyond the keep count. Returns the number removed.
int pruneBackups(int keep = 7);

// Exports every active reading-statistics file into one CRC-checked archive
// under /exports. This includes global totals, journal/ledger history, synced
// device totals, and per-book stats files.
bool exportAllReadingStats(char* outFileName = nullptr, size_t outFileNameLen = 0,
                           uint16_t* outFileCount = nullptr, uint32_t* outDataBytes = nullptr);

// Returns full paths to validly named .cstats archives, newest name first.
std::vector<std::string> listReadingStatsArchives();

// Validates and restores a full archive. Current files are first protected by a
// fresh safety export; replacements are staged before any live file is swapped.
bool importAllReadingStats(const std::string& archivePath, char* outSafetyFileName = nullptr,
                           size_t outSafetyFileNameLen = 0, uint16_t* outRestoredFileCount = nullptr);

// Performs the complete structural/path/CRC validation without changing data.
bool validateReadingStatsArchive(const std::string& archivePath, uint16_t* outFileCount = nullptr,
                                 uint32_t* outDataBytes = nullptr);
