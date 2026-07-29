#pragma once

#include <HalStorage.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <array>
#include <cstdint>
#include <string>

#include "activities/Activity.h"
#include "activities/reader/GlobalReadingStats.h"

class NearbyStatsSyncActivity final : public Activity {
 public:
  enum class State { STARTING, READY, PREPARING, DISCOVERING, SYNCING, SYNCED, ERROR };

  explicit NearbyStatsSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~NearbyStatsSyncActivity() override;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  bool skipLoopDelay() override { return state_ == State::DISCOVERING || state_ == State::SYNCING; }

  void enqueueEspNowPacket(const uint8_t* sourceMac, const uint8_t* data, int length);

  enum class PacketType : uint8_t {
    HELLO = 1,
    STATS = 2,
    ACK = 3,
    NAME = 4,
    FILE_META = 5,
    FILE_CHUNK = 6,
    FILE_DONE = 7,
    FILE_ACK = 8,
    INVALID_STATS = 0xFF
  };
  enum class SyncFileKind : uint8_t {
    None = 0,
    BookStats = 1,
    Journal = 2,
    BookDetails = 3,
    Ledger = 4,
    StatsDate = 5
  };
  enum class FileAckPhase : uint8_t { Meta = 1, Chunk = 2, Done = 3 };

 private:
  struct SyncEvent {
    PacketType type = PacketType::HELLO;
    std::array<uint8_t, 6> sourceMac = {};
    std::array<uint8_t, 6> deviceMac = {};
    std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE> stats = {};
    std::array<uint8_t, 236> payload = {};
    std::array<char, 21> deviceName = {};
    uint16_t payloadSize = 0;
    uint8_t statsSize = 0;
  };
  static constexpr size_t MAX_SYNC_EVENTS = 8;

  struct OutgoingFileTransfer {
    SyncFileKind kind = SyncFileKind::None;
    const char* path = nullptr;
    uint32_t size = 0;
    uint32_t crc = 0;
    uint16_t chunkIndex = 0;
    bool metadataAcked = false;
    bool doneSent = false;
    bool doneAcked = false;
    uint32_t lastSendMs = 0;
  };

  struct IncomingFileTransfer {
    SyncFileKind kind = SyncFileKind::None;
    uint32_t expectedSize = 0;
    uint32_t expectedCrc = 0;
    uint32_t receivedSize = 0;
    uint16_t nextChunkIndex = 0;
    bool active = false;
  };

  State state_ = State::STARTING;
  SemaphoreHandle_t eventMutex_ = nullptr;
  std::array<SyncEvent, MAX_SYNC_EVENTS> events_ = {};
  uint8_t eventHead_ = 0;
  uint8_t eventCount_ = 0;
  bool eventOverflow_ = false;
  bool espNowStarted_ = false;
  bool localStatsReady_ = false;
  bool peerSeen_ = false;
  bool peerStatsSaved_ = false;
  bool localStatsSent_ = false;
  bool localStatsAcked_ = false;
  bool peerBookStatsSaved_ = false;
  bool peerBookDetailsSaved_ = false;
  bool peerNamePersisted_ = false;
  bool peerJournalSaved_ = false;
  bool peerLedgerSaved_ = false;
  bool peerStatsDateSaved_ = false;
  bool localFilesSent_ = false;
  bool achievementsRefreshed_ = false;
  bool syncTimingWritten_ = false;
  uint16_t lateStatsAckCount_ = 0;
  uint16_t lateFileAckCount_ = 0;
  OutgoingFileTransfer outgoingFile_;
  IncomingFileTransfer incomingFile_;
  // Held open across a whole transfer instead of an open/sync/close cycle per
  // 200-odd chunk. Closed before computeFileInfo re-opens the same path (SdFat
  // allows one open handle per file on hardware) and in onExit/setError.
  FsFile incomingFileHandle_;
  FsFile outgoingFileHandle_;

  std::array<uint8_t, 6> localDeviceMac_ = {};
  std::array<uint8_t, 6> peerSourceMac_ = {};
  std::array<uint8_t, 6> peerDeviceMac_ = {};
  std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE> localStats_ = {};
  uint8_t localStatsSize_ = 0;

  uint32_t syncStartedMs_ = 0;
  uint32_t syncCompletedAtMs_ = 0;
  uint32_t lastSyncProgressMs_ = 0;
  // Last state change; idle end-states auto-exit so the radio (WIFI_PS_NONE,
  // auto-sleep inhibited) cannot drain the battery indefinitely.
  uint32_t lastStateChangeMs_ = 0;
  // Preparation progress (painted on the PREPARING screen; Back aborts).
  uint16_t prepScannedDirs_ = 0;
  uint16_t prepWrittenRecords_ = 0;
  unsigned long prepLastPaintMs_ = 0;
  bool prepAborted_ = false;
  uint32_t lastHelloMs_ = 0;
  uint32_t lastStatsSendMs_ = 0;
  uint32_t lastFileProgressMs_ = 0;
  std::string peerId_;
  std::string peerName_;
  std::string errorMessage_;

  bool beginEspNow();
  void endEspNow();
  bool prepareLocalStats();
  void startSync();
  void processEvents();
  void handleEvent(const SyncEvent& event);
  bool acknowledgeLateSyncedFilePacket(const SyncEvent& event);
  bool sendPacket(PacketType type, const uint8_t* peerMac);
  bool sendHello();
  bool sendDeviceName(const uint8_t* peerMac);
  bool sendLocalStats();
  bool sendAck(const uint8_t* peerMac);
  bool sendFileMeta();
  bool sendFileChunk();
  void closeTransferFiles();
  static bool prepProgressTrampoline(void* ctx, uint16_t scanned, uint16_t written);
  bool onPrepProgress(uint16_t scanned, uint16_t written);
  bool sendFileDone();
  bool sendFileAck(SyncFileKind kind, FileAckPhase phase, uint16_t chunkIndex);
  bool beginNextOutgoingFile();
  bool hasSavedPeerFile(SyncFileKind kind) const;
  void handleFileMeta(const SyncEvent& event);
  void handleFileChunk(const SyncEvent& event);
  void handleFileDone(const SyncEvent& event);
  void handleFileAck(const SyncEvent& event);
  void updateFileTransferProgress();
  void writeSyncTiming(const char* outcome);
  bool addPeer(const uint8_t* peerMac);
  void markSyncProgress();
  std::string syncProgressDetail() const;
  void exitViaBack();
  void updateSyncProgress();
  void setState(State state);
  void setError(const std::string& error);
  void renderReady(const std::string& primary, const std::string& detailPrimary,
                   const std::string& detailSecondary) const;
};
