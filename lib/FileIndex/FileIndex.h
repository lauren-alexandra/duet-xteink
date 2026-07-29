#pragma once

#include <HalStorage.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

class FileIndex {
 public:
  static constexpr size_t MAX_NAME = 511;

  struct Entry {
    bool isDir;
    char name[MAX_NAME + 1];
  };

  using AcceptFn = bool (*)(const char* name, bool isDir);
  enum class SortMode : uint8_t { Filename = 0, BookTitle = 1 };

  FileIndex() = default;
  ~FileIndex() { close(); }
  FileIndex(const FileIndex&) = delete;
  FileIndex& operator=(const FileIndex&) = delete;

  bool open(const char* dirPath, AcceptFn accept, SortMode sortMode = SortMode::Filename);
  void close();
  bool isOpen() const { return opened; }
  // Navigation within one FileBrowser activity cannot change the SD directory
  // behind our back. Reuse the verified on-card index for folders we have
  // already scanned, rather than re-walking a large directory on every Back.
  bool hasTrustedIndex(const char* dirPath, AcceptFn accept, SortMode sortMode = SortMode::Filename) const;
  void forgetTrustedIndex(const char* dirPath);

  size_t totalCount() const { return hdr.dirCount + hdr.fileCount; }
  size_t directoryCount() const { return hdr.dirCount; }
  size_t fileCount() const { return hdr.fileCount; }

  bool entryAt(size_t row, Entry& out);
  size_t findRowByName(const char* name);

 private:
#pragma pack(push, 1)
  struct IndexHeader {
    char magic[4];
    uint8_t version;
    uint16_t pathLen;
    uint32_t dirSignature;
    uint32_t dirCount;
    uint32_t fileCount;
    uint32_t blobStart;
    uint32_t blobLen;
    uint32_t offsetsStart;
    uint8_t sortMode;
  };

  struct RecordHeader {
    uint8_t flags;  // bit0 = directory
    uint8_t reserved;
    uint16_t nameLen;
  };

  struct RunRecord {
    uint8_t key[28];
    uint32_t blobOffset;
  };
#pragma pack(pop)
  static_assert(sizeof(RunRecord) == 32, "run record packing");

  struct BuildState;

  struct TrustedIndex {
    char path[96] = {0};
    AcceptFn accept = nullptr;
    uint32_t signature = 0;
    uint32_t dirs = 0;
    uint32_t files = 0;
    SortMode sortMode = SortMode::Filename;
    uint32_t useStamp = 0;
  };

  SortMode sortMode = SortMode::Filename;
  bool scanDirectory(const char* dirPath, AcceptFn accept, uint32_t& signature, uint32_t& dirs, uint32_t& files);
  bool loadExisting(const char* dirPath, uint32_t signature, uint32_t dirs, uint32_t files);
  bool build(const char* dirPath, AcceptFn accept, uint32_t signature, uint32_t dirs, uint32_t files);
  void rememberTrustedIndex(const char* dirPath, AcceptFn accept, uint32_t signature, uint32_t dirs, uint32_t files);
  TrustedIndex* findTrustedIndex(const char* dirPath, AcceptFn accept);
  const TrustedIndex* findTrustedIndex(const char* dirPath, AcceptFn accept) const;
  bool flushChunk(BuildState& bs);
  bool mergeRuns(BuildState& bs, uint32_t recordCount);
  bool writeOffsets(BuildState& bs, uint32_t recordCount);
  void makeKey(RunRecord& rec, bool isDir, const char* name) const;
  bool readNameAt(HalFile& file, uint32_t recordOffset, char* out, size_t cap);
  bool readOffsetForPhysIndex(size_t physIndex, uint32_t& recordOffset);

  HalFile idxFile;
  IndexHeader hdr{};
  bool opened = false;
  char idxPath[64] = {0};
  std::unique_ptr<char[]> nameBuf;

  static constexpr size_t OFFSETS_CACHE_ENTRIES = 64;
  uint32_t offsetsCache[OFFSETS_CACHE_ENTRIES] = {0};
  size_t offsetsCacheFirst = SIZE_MAX;
  std::array<TrustedIndex, 4> trustedIndexes{};
  uint32_t trustedIndexUseStamp = 0;
};
