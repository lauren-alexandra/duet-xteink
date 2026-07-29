#pragma once

#include <cstddef>
#include <cstring>

namespace DuetVersion {

constexpr size_t SEGMENT_COUNT = 4;

struct ParsedVersion {
  int segments[SEGMENT_COUNT] = {0, 0, 0, 0};
  bool valid = false;
  int prereleaseRank = 4;
  int prereleaseNumber = 0;
};

inline bool isDigit(const char c) { return c >= '0' && c <= '9'; }

inline bool equalsIgnoreCase(const char* value, const char* expected, const size_t length) {
  if (strlen(expected) != length) return false;
  for (size_t i = 0; i < length; ++i) {
    const char left =
        value[i] >= 'A' && value[i] <= 'Z' ? static_cast<char>(value[i] - 'A' + 'a') : value[i];
    if (left != expected[i]) return false;
  }
  return true;
}

inline bool isArtifactSuffix(const char* value, const size_t length) {
  return equalsIgnoreCase(value, "x3", length) || equalsIgnoreCase(value, "x4", length) ||
         equalsIgnoreCase(value, "tiny", length) || equalsIgnoreCase(value, "xlarge", length) ||
         equalsIgnoreCase(value, "debug", length) || equalsIgnoreCase(value, "simulator", length);
}

inline ParsedVersion parse(const char* version) {
  ParsedVersion parsed;
  if (version == nullptr) return parsed;

  const char* p = version;
  if (*p == 'v' || *p == 'V') ++p;
  if (!isDigit(*p)) return parsed;

  size_t segmentIndex = 0;
  while (segmentIndex < SEGMENT_COUNT) {
    if (!isDigit(*p)) return parsed;

    int value = 0;
    while (isDigit(*p)) {
      value = value * 10 + (*p - '0');
      ++p;
    }
    parsed.segments[segmentIndex++] = value;

    if (*p != '.') break;
    ++p;
  }

  parsed.valid = true;
  if (*p != '-') return parsed;

  ++p;
  const char* tagStart = p;
  while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) ++p;
  const size_t tagLength = static_cast<size_t>(p - tagStart);
  if (tagLength == 0 || isArtifactSuffix(tagStart, tagLength)) return parsed;

  if (equalsIgnoreCase(tagStart, "alpha", tagLength)) {
    parsed.prereleaseRank = 1;
  } else if (equalsIgnoreCase(tagStart, "beta", tagLength)) {
    parsed.prereleaseRank = 2;
  } else if (equalsIgnoreCase(tagStart, "rc", tagLength)) {
    parsed.prereleaseRank = 3;
  } else {
    parsed.prereleaseRank = 0;
  }

  if (*p == '.') {
    ++p;
    while (isDigit(*p)) {
      parsed.prereleaseNumber = parsed.prereleaseNumber * 10 + (*p - '0');
      ++p;
    }
  }
  return parsed;
}

inline int compare(const char* latestVersion, const char* currentVersion) {
  const ParsedVersion latest = parse(latestVersion);
  const ParsedVersion current = parse(currentVersion);
  if (!latest.valid || !current.valid) return 0;

  for (size_t i = 0; i < SEGMENT_COUNT; ++i) {
    if (latest.segments[i] != current.segments[i]) {
      return latest.segments[i] > current.segments[i] ? 1 : -1;
    }
  }

  if (latest.prereleaseRank != current.prereleaseRank) {
    return latest.prereleaseRank > current.prereleaseRank ? 1 : -1;
  }
  if (latest.prereleaseNumber != current.prereleaseNumber) {
    return latest.prereleaseNumber > current.prereleaseNumber ? 1 : -1;
  }
  return 0;
}

}  // namespace DuetVersion
