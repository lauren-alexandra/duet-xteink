#pragma once

#include <Arduino.h>
#include <Logging.h>

#include <cstdint>
#include <cstring>

namespace MemoryBudget {

struct HeapSnapshot {
  uint32_t freeHeap;
  uint32_t maxAllocHeap;
};

struct HeapRequirement {
  uint32_t minFree;
  uint32_t minMaxAlloc;
};

constexpr uint32_t EPUB_INLINE_IMAGE_MIN_FREE = 72U * 1024U;
constexpr uint32_t EPUB_INLINE_IMAGE_MIN_MAX_ALLOC = 48U * 1024U;
constexpr uint32_t EPUB_INLINE_IMAGE_SD_FONT_RELEASE_MIN_FREE = 120U * 1024U;
constexpr uint32_t EPUB_INLINE_IMAGE_SD_FONT_RELEASE_MIN_MAX_ALLOC = 80U * 1024U;
constexpr uint32_t OPTIONAL_EPUB_REBUILD_MIN_FREE = 96U * 1024U;
// Cover-shelf thumbnail generation: no book is resident, so the floor is far
// lower than the reader's optional-rebuild budget. On-card telemetry showed
// the shelf idling at ~72-77 KB free / 55-57 KB max-alloc while the 96 KB
// reader floor blocked every generation.
constexpr uint32_t COVER_THUMB_GEN_MIN_FREE = 64U * 1024U;
constexpr uint32_t COVER_THUMB_GEN_MIN_MAX_ALLOC = 44U * 1024U;
constexpr uint32_t OPTIONAL_EPUB_REBUILD_MIN_MAX_ALLOC = 48U * 1024U;
constexpr uint32_t BACKGROUND_EPUB_BUILD_ABORT_MIN_FREE = 56U * 1024U;
constexpr uint32_t BACKGROUND_EPUB_BUILD_ABORT_MIN_MAX_ALLOC = 36U * 1024U;
constexpr uint32_t IMAGE_DECODER_HEADROOM = 16U * 1024U;
constexpr uint32_t JPEG_DECODER_APPROX_BYTES = 20U * 1024U;
constexpr uint32_t EPUB_INLINE_JPEG_MIN_FREE = JPEG_DECODER_APPROX_BYTES + IMAGE_DECODER_HEADROOM;
constexpr uint32_t EPUB_INLINE_JPEG_MIN_MAX_ALLOC = JPEG_DECODER_APPROX_BYTES;

inline HeapSnapshot snapshot() { return {ESP.getFreeHeap(), ESP.getMaxAllocHeap()}; }

inline bool hasHeap(const HeapSnapshot heap, const uint32_t minFree, const uint32_t minMaxAlloc) {
  return heap.freeHeap >= minFree && heap.maxAllocHeap >= minMaxAlloc;
}

inline char asciiLower(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

inline bool endsWithIgnoreCase(const char* value, const char* suffix) {
  if (!value || !suffix) return false;
  const size_t valueLen = strlen(value);
  const size_t suffixLen = strlen(suffix);
  if (suffixLen > valueLen) return false;

  const char* valueTail = value + valueLen - suffixLen;
  for (size_t i = 0; i < suffixLen; ++i) {
    if (asciiLower(valueTail[i]) != asciiLower(suffix[i])) return false;
  }
  return true;
}

inline bool isJpegSource(const char* source) {
  return endsWithIgnoreCase(source, ".jpg") || endsWithIgnoreCase(source, ".jpeg");
}

inline HeapRequirement epubInlineImageRequirementForSource(const char* source) {
  if (isJpegSource(source)) {
    return {EPUB_INLINE_JPEG_MIN_FREE, EPUB_INLINE_JPEG_MIN_MAX_ALLOC};
  }
  return {EPUB_INLINE_IMAGE_MIN_FREE, EPUB_INLINE_IMAGE_MIN_MAX_ALLOC};
}

inline bool shouldReleaseSdFontCachesForEpubInlineImage(const HeapSnapshot heap) {
  return !hasHeap(heap, EPUB_INLINE_IMAGE_SD_FONT_RELEASE_MIN_FREE, EPUB_INLINE_IMAGE_SD_FONT_RELEASE_MIN_MAX_ALLOC);
}

inline bool hasHeapForEpubInlineImage(const char* tag, const char* source) {
  const auto heap = snapshot();
  const auto requirement = epubInlineImageRequirementForSource(source);
  if (hasHeap(heap, requirement.minFree, requirement.minMaxAlloc)) {
    return true;
  }

  LOG_ERR(tag, "Low heap for inline image (%u free, %u max alloc, need %u/%u); suppressing %s", heap.freeHeap,
          heap.maxAllocHeap, requirement.minFree, requirement.minMaxAlloc, source ? source : "");
  return false;
}

inline bool hasHeapForCoverThumbGeneration(const char* tag) {
  const auto heap = snapshot();
  if (hasHeap(heap, COVER_THUMB_GEN_MIN_FREE, COVER_THUMB_GEN_MIN_MAX_ALLOC)) {
    return true;
  }
  LOG_DBG(tag, "Skipping cover thumb generation: low heap (free=%u, maxAlloc=%u, need free>=%u maxAlloc>=%u)",
          heap.freeHeap, heap.maxAllocHeap, COVER_THUMB_GEN_MIN_FREE, COVER_THUMB_GEN_MIN_MAX_ALLOC);
  return false;
}

inline bool hasHeapForOptionalEpubRebuild(const char* tag, const char* action, const int spineIndex) {
  const auto heap = snapshot();
  if (hasHeap(heap, OPTIONAL_EPUB_REBUILD_MIN_FREE, OPTIONAL_EPUB_REBUILD_MIN_MAX_ALLOC)) {
    return true;
  }

  LOG_DBG(tag, "Skipping %s for spine %d: low heap (free=%u, maxAlloc=%u, need free>=%u maxAlloc>=%u)", action,
          spineIndex, heap.freeHeap, heap.maxAllocHeap, OPTIONAL_EPUB_REBUILD_MIN_FREE,
          OPTIONAL_EPUB_REBUILD_MIN_MAX_ALLOC);
  return false;
}

inline bool hasHeapForImageDecoder(const char* tag, const char* decoderName, const uint32_t decoderApproxBytes) {
  const auto heap = snapshot();
  const uint32_t minFree = decoderApproxBytes + IMAGE_DECODER_HEADROOM;
  if (hasHeap(heap, minFree, decoderApproxBytes)) {
    return true;
  }

  LOG_ERR(tag, "Not enough heap for %s decoder (%u free, %u max alloc, need %u/%u)", decoderName, heap.freeHeap,
          heap.maxAllocHeap, minFree, decoderApproxBytes);
  return false;
}

}  // namespace MemoryBudget
