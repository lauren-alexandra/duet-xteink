#pragma once

#include <cstdint>

namespace BionicReadingMode {

enum Mode : uint8_t {
  Off = 0,
  Normal = 1,
  Subtle = 2,
  Count,
};

constexpr uint8_t normalize(const uint8_t mode) { return mode < Count ? mode : Off; }

constexpr uint8_t next(const uint8_t mode) {
  return static_cast<uint8_t>((normalize(mode) + 1U) % static_cast<uint8_t>(Count));
}

constexpr uint8_t boldPrefixPercent(const uint8_t mode) {
  return normalize(mode) == Subtle ? 30U : 43U;
}

}  // namespace BionicReadingMode
