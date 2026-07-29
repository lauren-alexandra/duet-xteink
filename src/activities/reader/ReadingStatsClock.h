#pragma once

#include "ReadingStatsUtils.h"

bool getClocklessReadingStatsDateTime(ReadingStatsDateTime& outDateTime);
bool setClocklessReadingStatsDate(const ReadingStatsDate& date);
bool mergeClocklessReadingStatsDateFromFile(const char* path);
void resetClocklessReadingStatsDateCache();
