#include "NearbyStatsSyncActivity.h"

#ifdef SIMULATOR

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

NearbyStatsSyncActivity::NearbyStatsSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("NearbyStatsSync", renderer, mappedInput) {}

NearbyStatsSyncActivity::~NearbyStatsSyncActivity() = default;

void NearbyStatsSyncActivity::onEnter() {
  Activity::onEnter();
  setState(State::ERROR);
}

void NearbyStatsSyncActivity::onExit() { Activity::onExit(); }

void NearbyStatsSyncActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) exitViaBack();
}

void NearbyStatsSyncActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_NEARBY_STATS_SYNC));
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_NEARBY_STATS_SIMULATOR_UNAVAILABLE), true,
                            EpdFontFamily::BOLD);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void NearbyStatsSyncActivity::enqueueEspNowPacket(const uint8_t*, const uint8_t*, int) {}

void NearbyStatsSyncActivity::exitViaBack() {
  mappedInput.suppressNextBackRelease();
  finish();
}

void NearbyStatsSyncActivity::setState(const State state) {
  state_ = state;
  requestUpdate();
}

#else

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <uzlib.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "activities/ActivityManager.h"
#include "activities/reader/GlobalReadingStats.h"
#include "activities/reader/LibraryInsights.h"
#include "activities/reader/ReadingJournal.h"
#include "activities/reader/ReadingStatsClock.h"
#include "AchievementStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr const char* LOG_TAG = "NSYNC";
constexpr const char* CROSSPOINT_ROOT = DUET_STATE_ROOT_PATH "";
constexpr const char* GLOBAL_STATS_PATH = DUET_STATE_ROOT_PATH "/global_stats.bin";
constexpr const char* SYNCED_STATS_DIR = DUET_STATE_ROOT_PATH "/synced_stats";
constexpr const char* BOOK_STATS_INDEX_PATH = DUET_STATE_ROOT_PATH "/library_book_stats_v1.bin";
constexpr const char* BOOK_STATS_DETAIL_PATH = DUET_STATE_ROOT_PATH "/library_book_details_v1.bin";
constexpr const char* JOURNAL_PATH = DUET_STATE_ROOT_PATH "/reading_journal.bin";
constexpr const char* LEDGER_PATH = DUET_STATE_ROOT_PATH "/reading_ledger_v1.bin";
constexpr const char* STATS_DATE_PATH = DUET_STATE_ROOT_PATH "/reading_stats_clock_v1.bin";
constexpr const char* SYNCED_LEDGER_DIR = DUET_STATE_ROOT_PATH "/synced_ledgers";
constexpr const char* SYNCED_STATS_DATE_DIR = DUET_STATE_ROOT_PATH "/synced_stats_dates";
constexpr const char* SYNCED_NAMES_DIR = DUET_STATE_ROOT_PATH "/synced_names";
constexpr const char* SYNCED_BOOK_STATS_DIR = DUET_STATE_ROOT_PATH "/synced_book_stats";
constexpr const char* SYNCED_BOOK_DETAILS_DIR = DUET_STATE_ROOT_PATH "/synced_book_details";
constexpr const char* SYNCED_JOURNAL_DIR = DUET_STATE_ROOT_PATH "/synced_journals";
constexpr uint8_t ESPNOW_CHANNEL = 1;
constexpr uint8_t PROTOCOL_VERSION = 5;
constexpr uint8_t MIN_STATS_BYTES = static_cast<uint8_t>(GlobalReadingStats::MIN_SUPPORTED_FILE_SIZE);
constexpr uint8_t MAX_STATS_BYTES = static_cast<uint8_t>(GlobalReadingStats::CURRENT_FILE_SIZE);
constexpr uint8_t PACKET_HEADER_BYTES = 14;
constexpr uint16_t MAX_PACKET_BYTES = 250;
constexpr uint16_t MAX_PAYLOAD_BYTES = MAX_PACKET_BYTES - PACKET_HEADER_BYTES;
// 5-byte chunk header + 228 data bytes fills the 236-byte ESP-NOW payload
// budget (192 wasted ~17% of every frame under the stop-and-wait ACK).
constexpr uint16_t FILE_CHUNK_BYTES = 228;
constexpr uint8_t MAX_DEVICE_NAME_BYTES = static_cast<uint8_t>(CrossPointSettings::MAX_DEVICE_NAME_LENGTH);
constexpr uint32_t HELLO_INTERVAL_MS = 750;
constexpr uint32_t STATS_RETRY_INTERVAL_MS = 1500;
constexpr uint32_t FILE_RETRY_INTERVAL_MS = 650;
constexpr uint32_t DISCOVERY_TIMEOUT_MS = 60000;
// Full-library stats sync transfers several detailed files over ESP-NOW. Use
// an idle timeout, not a wall-clock timeout, so a slow-but-moving transfer can
// finish while a truly stalled one still returns to the user.
constexpr uint32_t SYNC_IDLE_TIMEOUT_MS = 90000;
constexpr uint32_t ACHIEVEMENT_REFRESH_DELAY_MS = 900;
constexpr uint8_t BROADCAST_MAC[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

NearbyStatsSyncActivity* activeActivity = nullptr;

std::string bytesToHex(const uint8_t* data, const size_t length) {
  static constexpr char hex[] = "0123456789abcdef";
  std::string out;
  out.resize(length * 2);
  for (size_t i = 0; i < length; i++) {
    out[i * 2] = hex[data[i] >> 4];
    out[i * 2 + 1] = hex[data[i] & 0x0F];
  }
  return out;
}

std::string statsFileNameForDeviceMac(const std::array<uint8_t, 6>& mac) {
  char name[32];
  snprintf(name, sizeof(name), "device_%02x%02x%02x%02x%02x%02x.bin", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return name;
}

std::string syncedStatsPathForDeviceMac(const std::array<uint8_t, 6>& mac) {
  return std::string(SYNCED_STATS_DIR) + "/" + statsFileNameForDeviceMac(mac);
}

const char* localFilePathForKind(const NearbyStatsSyncActivity::SyncFileKind kind) {
  switch (kind) {
    case NearbyStatsSyncActivity::SyncFileKind::BookStats:
      return BOOK_STATS_INDEX_PATH;
    case NearbyStatsSyncActivity::SyncFileKind::BookDetails:
      return BOOK_STATS_DETAIL_PATH;
    case NearbyStatsSyncActivity::SyncFileKind::Journal:
      return JOURNAL_PATH;
    case NearbyStatsSyncActivity::SyncFileKind::Ledger:
      return LEDGER_PATH;
    case NearbyStatsSyncActivity::SyncFileKind::StatsDate:
      return STATS_DATE_PATH;
    default:
      return nullptr;
  }
}

std::string syncedDirectoryForKind(const NearbyStatsSyncActivity::SyncFileKind kind) {
  switch (kind) {
    case NearbyStatsSyncActivity::SyncFileKind::BookStats:
      return SYNCED_BOOK_STATS_DIR;
    case NearbyStatsSyncActivity::SyncFileKind::BookDetails:
      return SYNCED_BOOK_DETAILS_DIR;
    case NearbyStatsSyncActivity::SyncFileKind::Journal:
      return SYNCED_JOURNAL_DIR;
    case NearbyStatsSyncActivity::SyncFileKind::Ledger:
      return SYNCED_LEDGER_DIR;
    case NearbyStatsSyncActivity::SyncFileKind::StatsDate:
      return SYNCED_STATS_DATE_DIR;
    default:
      return {};
  }
}

std::string syncedFilePathForKindAndDeviceMac(const NearbyStatsSyncActivity::SyncFileKind kind,
                                              const std::array<uint8_t, 6>& mac) {
  const std::string dir = syncedDirectoryForKind(kind);
  return dir.empty() ? std::string{} : dir + "/" + statsFileNameForDeviceMac(mac);
}

std::string incomingTempPathForKind(const NearbyStatsSyncActivity::SyncFileKind kind) {
  const std::string dir = syncedDirectoryForKind(kind);
  if (dir.empty()) return {};
  char name[32];
  snprintf(name, sizeof(name), "/incoming_%u.part", static_cast<unsigned>(kind));
  return dir + name;
}

// The Device Split stats page shows peer devices by their human name; the
// handshake is the only moment the name is known, so persist it here.
void persistPeerDeviceName(const std::array<uint8_t, 6>& mac, const std::string& name) {
  if (name.empty()) return;
  const std::string binName = statsFileNameForDeviceMac(mac);
  std::string txtName = binName;
  const size_t dot = txtName.rfind(".bin");
  if (dot != std::string::npos) txtName.replace(dot, 4, ".txt");
  Storage.writeFile((std::string(SYNCED_NAMES_DIR) + "/" + txtName).c_str(), String(name.c_str()));
}

bool isZeroMac(const std::array<uint8_t, 6>& mac) { return mac == std::array<uint8_t, 6>{}; }

bool isValidStatsPayload(const uint8_t* data, const uint8_t size) {
  return (size == MIN_STATS_BYTES && data[0] == 1) || (size == 17 && data[0] == 2) ||
         (size == 159 && data[0] == 3) ||
         (size == MAX_STATS_BYTES && data[0] == GlobalReadingStats::CURRENT_FILE_VERSION);
}

bool ensureSyncedStatsDirectory() {
  return Storage.ensureDirectoryExists(CROSSPOINT_ROOT) && Storage.ensureDirectoryExists(SYNCED_STATS_DIR) &&
         Storage.ensureDirectoryExists(SYNCED_BOOK_STATS_DIR) &&
         Storage.ensureDirectoryExists(SYNCED_BOOK_DETAILS_DIR) && Storage.ensureDirectoryExists(SYNCED_JOURNAL_DIR) &&
         Storage.ensureDirectoryExists(SYNCED_LEDGER_DIR) && Storage.ensureDirectoryExists(SYNCED_STATS_DATE_DIR) &&
         Storage.ensureDirectoryExists(SYNCED_NAMES_DIR);
}

uint32_t crc32(const uint8_t* data, const size_t size, const uint32_t crc = 0) {
  return uzlib_crc32(data, static_cast<unsigned int>(size), crc);
}

bool computeFileInfo(const char* path, uint32_t& size, uint32_t& crc) {
  size = 0;
  crc = 0;
  if (!path) return false;
  FsFile file;
  if (!Storage.openFileForRead(LOG_TAG, path, file)) return false;
  uint8_t buffer[256];
  while (file.available() > 0) {
    const int read = file.read(buffer, sizeof(buffer));
    if (read <= 0) {
      file.close();
      return false;
    }
    crc = crc32(buffer, static_cast<size_t>(read), crc);
    size += static_cast<uint32_t>(read);
  }
  file.close();
  return true;
}

uint16_t readLe16(const uint8_t* data) { return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8); }

uint32_t readLe32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

void writeLe16(uint8_t* data, const uint16_t value) {
  data[0] = static_cast<uint8_t>(value & 0xffu);
  data[1] = static_cast<uint8_t>((value >> 8) & 0xffu);
}

void writeLe32(uint8_t* data, const uint32_t value) {
  data[0] = static_cast<uint8_t>(value & 0xffu);
  data[1] = static_cast<uint8_t>((value >> 8) & 0xffu);
  data[2] = static_cast<uint8_t>((value >> 16) & 0xffu);
  data[3] = static_cast<uint8_t>((value >> 24) & 0xffu);
}

bool readSmallFile(const char* path, std::array<uint8_t, MAX_STATS_BYTES>& out, uint8_t& outSize) {
  outSize = 0;
  FsFile file;
  if (!Storage.openFileForRead(LOG_TAG, path, file)) return false;
  const size_t fileSize = file.fileSize();
  if (fileSize < MIN_STATS_BYTES || fileSize > MAX_STATS_BYTES) {
    file.close();
    return false;
  }

  const int read = file.read(out.data(), fileSize);
  file.close();
  if (read != static_cast<int>(fileSize) || !isValidStatsPayload(out.data(), static_cast<uint8_t>(fileSize)))
    return false;
  outSize = static_cast<uint8_t>(fileSize);
  return true;
}

bool writeSyncedStatsFile(const std::string& path, const uint8_t* data, const uint8_t size) {
  if (!isValidStatsPayload(data, size) || !ensureSyncedStatsDirectory()) return false;

  std::array<uint8_t, MAX_STATS_BYTES> existing = {};
  uint8_t existingSize = 0;
  if (readSmallFile(path.c_str(), existing, existingSize) && existingSize == size &&
      memcmp(existing.data(), data, size) == 0) {
    return true;
  }

  const std::string tmpPath = path + ".part";
  if (Storage.exists(tmpPath.c_str())) Storage.remove(tmpPath.c_str());

  FsFile file;
  if (!Storage.openFileForWrite(LOG_TAG, tmpPath, file)) return false;
  const size_t written = file.write(data, size);
  if (written != size) {
    file.close();
    Storage.remove(tmpPath.c_str());
    return false;
  }
  file.flush();
  if (!file.sync()) {
    file.close();
    Storage.remove(tmpPath.c_str());
    return false;
  }
  if (!file.close()) {
    Storage.remove(tmpPath.c_str());
    return false;
  }

  if (Storage.exists(path.c_str()) && !Storage.remove(path.c_str())) {
    Storage.remove(tmpPath.c_str());
    return false;
  }
  if (!Storage.rename(tmpPath.c_str(), path.c_str())) {
    Storage.remove(tmpPath.c_str());
    return false;
  }
  return true;
}

void onEspNowReceive(const esp_now_recv_info_t* info, const uint8_t* data, int length) {
  if (!activeActivity || !info || !info->src_addr) return;
  activeActivity->enqueueEspNowPacket(info->src_addr, data, length);
}

}  // namespace

NearbyStatsSyncActivity::NearbyStatsSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("NearbyStatsSync", renderer, mappedInput), eventMutex_(xSemaphoreCreateMutex()) {}

NearbyStatsSyncActivity::~NearbyStatsSyncActivity() {
  if (eventMutex_) {
    vSemaphoreDelete(eventMutex_);
    eventMutex_ = nullptr;
  }
}

void NearbyStatsSyncActivity::onEnter() {
  Activity::onEnter();
  sdFontSystem.releaseLoadedFont(renderer);
  setState(State::STARTING);

  if (esp_efuse_mac_get_default(localDeviceMac_.data()) != ESP_OK) {
    setError("Could not read device id");
    return;
  }

  if (!beginEspNow()) {
    setError("Could not start nearby sync");
    return;
  }

  setState(State::READY);
}

void NearbyStatsSyncActivity::onExit() {
  Activity::onExit();
  closeTransferFiles();
  if (!syncTimingWritten_ && syncStartedMs_ != 0) {
    writeSyncTiming(state_ == State::SYNCED ? "synced" : (state_ == State::ERROR ? "error" : "cancelled"));
  }
  endEspNow();
}

void NearbyStatsSyncActivity::closeTransferFiles() {
  if (incomingFileHandle_.isOpen()) incomingFileHandle_.close();
  if (outgoingFileHandle_.isOpen()) outgoingFileHandle_.close();
}

void NearbyStatsSyncActivity::loop() {
  processEvents();

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    exitViaBack();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) &&
      (state_ == State::READY || state_ == State::SYNCED || state_ == State::ERROR)) {
    startSync();
    return;
  }

  updateSyncProgress();

  // Let the successful-sync frame paint before evaluating/queuing an
  // achievement alert. On real e-ink hardware the old synchronous sequence
  // made a completed exchange look frozen behind its own popup.
  if (state_ == State::SYNCED && !achievementsRefreshed_ && syncCompletedAtMs_ != 0 &&
      millis() - syncCompletedAtMs_ >= ACHIEVEMENT_REFRESH_DELAY_MS) {
    achievementsRefreshed_ = true;
    ACHIEVEMENT_STORE.refreshLightweight(false);
  }
}

bool NearbyStatsSyncActivity::beginEspNow() {
  // Belt-and-braces: a persisted STA config with auto-reconnect can re-join an
  // AP mid-discovery and drag the radio off the ESP-NOW channel.
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false);
  WiFi.setSleep(false);
  if (esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK) return false;
  esp_wifi_set_ps(WIFI_PS_NONE);

  if (esp_now_init() != ESP_OK) return false;
  espNowStarted_ = true;

  if (esp_now_register_recv_cb(onEspNowReceive) != ESP_OK) return false;
  if (!addPeer(BROADCAST_MAC)) return false;
  activeActivity = this;
  return true;
}

void NearbyStatsSyncActivity::endEspNow() {
  if (activeActivity == this) activeActivity = nullptr;
  if (espNowStarted_) {
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    espNowStarted_ = false;
  }
  WiFi.disconnect(false);
  WiFi.mode(WIFI_OFF);
}

bool NearbyStatsSyncActivity::prepProgressTrampoline(void* ctx, uint16_t scanned, uint16_t written) {
  return static_cast<NearbyStatsSyncActivity*>(ctx)->onPrepProgress(scanned, written);
}

bool NearbyStatsSyncActivity::onPrepProgress(const uint16_t scanned, const uint16_t written) {
  prepScannedDirs_ = scanned;
  prepWrittenRecords_ = written;
  // The loop task is blocked in the preparation walk; poll input here so Back
  // can abort instead of the screen appearing frozen.
  mappedInput.update();
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    LOG_INF(LOG_TAG, "Sync preparation aborted by user at %u dirs", scanned);
    prepAborted_ = true;
    return false;
  }
  const unsigned long now = millis();
  if (now - prepLastPaintMs_ >= 1500) {
    prepLastPaintMs_ = now;
    activityManager.requestUpdateAndWait();
  }
  return true;
}

bool NearbyStatsSyncActivity::prepareLocalStats() {
  localStatsReady_ = false;
  if (!ensureSyncedStatsDirectory()) {
    setError("could not create synced stats directory");
    return false;
  }

  // Ensure a valid local stats payload exists before exchanging stats.
  GlobalReadingStats::load().save();
  LibraryInsights::publishLocalBookStatsIndexForSync();
  prepScannedDirs_ = 0;
  prepWrittenRecords_ = 0;
  prepLastPaintMs_ = millis();
  prepAborted_ = false;
  if (!LibraryInsights::publishLocalDetailedBookStatsForSync(&NearbyStatsSyncActivity::prepProgressTrampoline, this)) {
    if (prepAborted_) {
      prepAborted_ = false;
      setState(State::READY);
      return false;
    }
    setError("could not prepare detailed book stats");
    return false;
  }
  ReadingJournal::publishLocalForSync();
  ReadingStatsDateTime statsDateTime;
  if (!getClocklessReadingStatsDateTime(statsDateTime) || !Storage.existsForRead(STATS_DATE_PATH)) {
    setError("local stats date unavailable");
    return false;
  }

  if (!readSmallFile(GLOBAL_STATS_PATH, localStats_, localStatsSize_)) {
    setError("local stats unavailable");
    return false;
  }

  // Nearby sync remains on the v3 wire shape so devices on the preceding
  // firmware can still exchange totals. The v4-only session-time field is
  // reconstructed from total reading time when a v3 payload is loaded.
  if (localStatsSize_ == GlobalReadingStats::CURRENT_FILE_SIZE &&
      localStats_[0] == GlobalReadingStats::CURRENT_FILE_VERSION) {
    localStats_[0] = 3;
    localStatsSize_ = 159;
  }

  localStatsReady_ = true;
  return true;
}

void NearbyStatsSyncActivity::startSync() {
  errorMessage_.clear();
  peerSeen_ = false;
  peerStatsSaved_ = false;
  localStatsSent_ = false;
  localStatsAcked_ = false;
  peerBookStatsSaved_ = false;
  peerBookDetailsSaved_ = false;
  peerJournalSaved_ = false;
  peerLedgerSaved_ = false;
  peerStatsDateSaved_ = false;
  localFilesSent_ = false;
  achievementsRefreshed_ = false;
  syncTimingWritten_ = false;
  lateStatsAckCount_ = 0;
  lateFileAckCount_ = 0;
  closeTransferFiles();
  outgoingFile_ = {};
  incomingFile_ = {};
  syncCompletedAtMs_ = 0;
  peerSourceMac_ = {};
  peerDeviceMac_ = {};
  peerId_.clear();
  peerName_.clear();
  syncStartedMs_ = millis();
  lastSyncProgressMs_ = syncStartedMs_;
  lastHelloMs_ = 0;
  lastStatsSendMs_ = 0;
  if (eventMutex_) {
    xSemaphoreTake(eventMutex_, portMAX_DELAY);
    eventOverflow_ = false;
    eventHead_ = 0;
    eventCount_ = 0;
    xSemaphoreGive(eventMutex_);
  }

  // Snapshot preparation walks the whole library cache tree on first run and
  // blocks this task for seconds-to-minutes on large libraries. Paint the
  // PREPARING state first (the render task draws it concurrently) so the Sync
  // press is never a dead button on a screen still saying "ready".
  setState(State::PREPARING);
  // Block until the PREPARING frame is physically on the panel. Relying on
  // the render task getting scheduled while this task grinds SD I/O proved
  // unreliable on hardware — the Sync press looked like a dead button.
  activityManager.requestUpdateAndWait();
  if (!prepareLocalStats()) return;

  // Restart the sync clock: preparation can take a long time on first run, and
  // it must not eat into the discovery/transfer timeout budget.
  syncStartedMs_ = millis();
  lastSyncProgressMs_ = syncStartedMs_;
  setState(State::DISCOVERING);
  sendHello();
}

void NearbyStatsSyncActivity::enqueueEspNowPacket(const uint8_t* sourceMac, const uint8_t* data, const int length) {
  if (!eventMutex_ || !sourceMac || !data || length < PACKET_HEADER_BYTES) return;
  if (data[0] != 'C' || data[1] != 'I' || data[2] != 'S' || data[3] != 'S') return;
  if (data[4] != PROTOCOL_VERSION) return;

  SyncEvent event;
  const PacketType packetType = static_cast<PacketType>(data[5]);
  event.type = packetType;
  event.payloadSize = readLe16(data + 6);
  event.statsSize = static_cast<uint8_t>(std::min<uint16_t>(event.payloadSize, event.stats.size()));
  std::copy(sourceMac, sourceMac + event.sourceMac.size(), event.sourceMac.begin());
  std::copy(data + 8, data + 14, event.deviceMac.begin());

  const uint16_t payloadLength = static_cast<uint16_t>(length - PACKET_HEADER_BYTES);
  if (length != static_cast<int>(PACKET_HEADER_BYTES + event.payloadSize) || event.payloadSize > event.payload.size())
    return;
  if (packetType != PacketType::HELLO && packetType != PacketType::STATS && packetType != PacketType::ACK &&
      packetType != PacketType::NAME && packetType != PacketType::FILE_META && packetType != PacketType::FILE_CHUNK &&
      packetType != PacketType::FILE_DONE && packetType != PacketType::FILE_ACK)
    return;
  if (event.deviceMac == localDeviceMac_) return;
  if (packetType == PacketType::STATS) {
    if (event.payloadSize > event.stats.size() ||
        !isValidStatsPayload(data + PACKET_HEADER_BYTES, static_cast<uint8_t>(event.payloadSize))) {
      event.type = PacketType::INVALID_STATS;
      event.statsSize = 0;
    } else {
      event.statsSize = static_cast<uint8_t>(event.payloadSize);
      std::copy(data + PACKET_HEADER_BYTES, data + PACKET_HEADER_BYTES + event.statsSize, event.stats.begin());
    }
  } else if (packetType == PacketType::NAME) {
    if (event.payloadSize < CrossPointSettings::MIN_DEVICE_NAME_LENGTH || event.payloadSize > MAX_DEVICE_NAME_BYTES ||
        payloadLength != event.payloadSize) {
      return;
    }
    memcpy(event.deviceName.data(), data + PACKET_HEADER_BYTES, event.payloadSize);
    event.deviceName[event.payloadSize] = '\0';
  } else if (event.payloadSize > 0) {
    std::copy(data + PACKET_HEADER_BYTES, data + PACKET_HEADER_BYTES + event.payloadSize, event.payload.begin());
  } else if (packetType == PacketType::HELLO || packetType == PacketType::ACK) {
    if (event.payloadSize != 0) return;
  }

  if (xSemaphoreTake(eventMutex_, 0) != pdTRUE) return;
  if (eventCount_ >= MAX_SYNC_EVENTS) {
    if (event.type == PacketType::HELLO || event.type == PacketType::NAME) {
      xSemaphoreGive(eventMutex_);
      return;
    }
    eventHead_ = static_cast<uint8_t>((eventHead_ + 1) % MAX_SYNC_EVENTS);
    eventCount_--;
  }
  const uint8_t eventTail = static_cast<uint8_t>((eventHead_ + eventCount_) % MAX_SYNC_EVENTS);
  events_[eventTail] = event;
  eventCount_++;
  xSemaphoreGive(eventMutex_);
}

void NearbyStatsSyncActivity::processEvents() {
  while (true) {
    SyncEvent event;
    bool hasEvent = false;
    if (eventMutex_) {
      xSemaphoreTake(eventMutex_, portMAX_DELAY);
      if (eventOverflow_) {
        eventOverflow_ = false;
        eventHead_ = 0;
        eventCount_ = 0;
      }
      if (eventCount_ > 0) {
        event = events_[eventHead_];
        eventHead_ = static_cast<uint8_t>((eventHead_ + 1) % MAX_SYNC_EVENTS);
        eventCount_--;
        hasEvent = true;
      }
      xSemaphoreGive(eventMutex_);
    }

    if (!hasEvent) return;
    handleEvent(event);
  }
}

void NearbyStatsSyncActivity::handleEvent(const SyncEvent& event) {
  if (state_ == State::ERROR) return;
  markSyncProgress();

  // A peer can still be retrying while this reader has already completed.
  // Acknowledge those late packets without resetting the successful reader
  // back into SYNCING, which previously produced false timeout errors.
  if (state_ == State::SYNCED) {
    if (event.deviceMac != peerDeviceMac_) return;
    peerSourceMac_ = event.sourceMac;
    addPeer(peerSourceMac_.data());
    if (event.type == PacketType::NAME) {
      peerName_ = event.deviceName.data();
      if (!peerNamePersisted_) {
        persistPeerDeviceName(peerDeviceMac_, peerName_);
        peerNamePersisted_ = true;
      }
      requestUpdate();
    } else if (event.type == PacketType::HELLO) {
      sendDeviceName(peerSourceMac_.data());
      sendLocalStats();
    } else if (event.type == PacketType::STATS) {
      if (sendAck(peerSourceMac_.data())) {
        lateStatsAckCount_++;
        syncTimingWritten_ = false;
      }
    } else {
      acknowledgeLateSyncedFilePacket(event);
    }
    return;
  }

  if (event.type == PacketType::NAME) {
    if (event.deviceMac == peerDeviceMac_ || isZeroMac(peerDeviceMac_)) {
      peerSourceMac_ = event.sourceMac;
      peerDeviceMac_ = event.deviceMac;
      peerId_ = bytesToHex(peerDeviceMac_.data(), peerDeviceMac_.size());
      peerName_ = event.deviceName.data();
      if (!peerNamePersisted_) {
        persistPeerDeviceName(peerDeviceMac_, peerName_);
        peerNamePersisted_ = true;
      }
      peerSeen_ = true;
      addPeer(peerSourceMac_.data());
      if (state_ == State::DISCOVERING && localStatsReady_) {
        setState(State::SYNCING);
        sendLocalStats();
      }
      requestUpdate();
    }
    return;
  }

  const bool startingPassiveSync = state_ != State::DISCOVERING && state_ != State::SYNCING;
  if (startingPassiveSync) {
    errorMessage_.clear();
    peerStatsSaved_ = false;
    localStatsSent_ = false;
    localStatsAcked_ = false;
    peerBookStatsSaved_ = false;
    peerBookDetailsSaved_ = false;
    peerJournalSaved_ = false;
    peerLedgerSaved_ = false;
    peerStatsDateSaved_ = false;
    localFilesSent_ = false;
    outgoingFile_ = {};
    incomingFile_ = {};
    localStatsReady_ = false;
    syncStartedMs_ = millis();
    lastHelloMs_ = syncStartedMs_;
    lastStatsSendMs_ = 0;
  }

  peerSeen_ = true;
  if (event.deviceMac != peerDeviceMac_) {
    peerName_.clear();
  }
  peerSourceMac_ = event.sourceMac;
  peerDeviceMac_ = event.deviceMac;
  peerId_ = bytesToHex(peerDeviceMac_.data(), peerDeviceMac_.size());
  addPeer(peerSourceMac_.data());

  if (!localStatsReady_ && !prepareLocalStats()) return;
  // ERROR included: a device parked on "no reader found" must still join when
  // the other reader finally starts broadcasting.
  if (state_ == State::READY || state_ == State::DISCOVERING || state_ == State::SYNCED ||
      state_ == State::ERROR) {
    setState(State::SYNCING);
  }

  if (event.type == PacketType::INVALID_STATS) {
    setError(tr(STR_NEARBY_STATS_VERSION_MISMATCH));
    return;
  }

  if (event.type == PacketType::HELLO) {
    sendDeviceName(peerSourceMac_.data());
    sendLocalStats();
    return;
  }

  if (event.type == PacketType::STATS) {
    if (!peerStatsSaved_) {
      if (!writeSyncedStatsFile(syncedStatsPathForDeviceMac(peerDeviceMac_), event.stats.data(), event.statsSize)) {
        setError("could not save stats");
        return;
      }
      peerStatsSaved_ = true;
      markSyncProgress();
    }
    sendAck(peerSourceMac_.data());
    if (!localStatsSent_ || !localStatsAcked_) sendLocalStats();
    return;
  }

  if (event.type == PacketType::ACK) {
    localStatsAcked_ = true;
    markSyncProgress();
    if (peerStatsSaved_ && !localFilesSent_ && outgoingFile_.kind == SyncFileKind::None) {
      beginNextOutgoingFile();
    }
    return;
  }

  if (event.type == PacketType::FILE_META) {
    handleFileMeta(event);
    return;
  }

  if (event.type == PacketType::FILE_CHUNK) {
    handleFileChunk(event);
    return;
  }

  if (event.type == PacketType::FILE_DONE) {
    handleFileDone(event);
    return;
  }

  if (event.type == PacketType::FILE_ACK) {
    handleFileAck(event);
    return;
  }
}

bool NearbyStatsSyncActivity::acknowledgeLateSyncedFilePacket(const SyncEvent& event) {
  if (event.payloadSize == 0) return false;
  const auto kind = static_cast<SyncFileKind>(event.payload[0]);
  if (kind != SyncFileKind::BookStats && kind != SyncFileKind::BookDetails && kind != SyncFileKind::Journal &&
      kind != SyncFileKind::Ledger && kind != SyncFileKind::StatsDate) {
    return false;
  }

  FileAckPhase phase = FileAckPhase::Meta;
  uint16_t chunkIndex = 0;
  if (event.type == PacketType::FILE_META) {
    if (event.payloadSize != 9) return false;
  } else if (event.type == PacketType::FILE_CHUNK) {
    if (event.payloadSize < 5) return false;
    chunkIndex = readLe16(event.payload.data() + 1);
    const uint16_t chunkSize = readLe16(event.payload.data() + 3);
    if (event.payloadSize != static_cast<uint16_t>(5 + chunkSize)) return false;
    phase = FileAckPhase::Chunk;
  } else if (event.type == PacketType::FILE_DONE) {
    if (event.payloadSize != 1) return false;
    phase = FileAckPhase::Done;
  } else {
    return false;
  }

  if (!sendFileAck(kind, phase, chunkIndex)) return false;
  lateFileAckCount_++;
  syncTimingWritten_ = false;
  return true;
}

bool NearbyStatsSyncActivity::addPeer(const uint8_t* peerMac) {
  if (!peerMac) return false;

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, peerMac, ESP_NOW_ETH_ALEN);
  peer.channel = ESPNOW_CHANNEL;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;

  const esp_err_t result = esp_now_add_peer(&peer);
  return result == ESP_OK || result == ESP_ERR_ESPNOW_EXIST;
}

void NearbyStatsSyncActivity::markSyncProgress() { lastSyncProgressMs_ = millis(); }

bool NearbyStatsSyncActivity::sendPacket(const PacketType type, const uint8_t* peerMac) {
  if (!peerMac || !espNowStarted_) return false;
  if (!addPeer(peerMac)) return false;

  std::array<uint8_t, MAX_PACKET_BYTES> packet = {};
  packet[0] = 'C';
  packet[1] = 'I';
  packet[2] = 'S';
  packet[3] = 'S';
  packet[4] = PROTOCOL_VERSION;
  packet[5] = static_cast<uint8_t>(type);
  std::copy(localDeviceMac_.begin(), localDeviceMac_.end(), packet.begin() + 8);

  size_t length = PACKET_HEADER_BYTES;
  if (type == PacketType::STATS) {
    if (!localStatsReady_ || !isValidStatsPayload(localStats_.data(), localStatsSize_)) return false;
    writeLe16(packet.data() + 6, localStatsSize_);
    std::copy(localStats_.begin(), localStats_.begin() + localStatsSize_, packet.begin() + PACKET_HEADER_BYTES);
    length += localStatsSize_;
  } else if (type == PacketType::NAME) {
    const char* name = SETTINGS.getEffectiveDeviceName();
    const size_t nameLength = std::min(std::strlen(name), static_cast<size_t>(MAX_DEVICE_NAME_BYTES));
    if (nameLength < CrossPointSettings::MIN_DEVICE_NAME_LENGTH) return false;
    writeLe16(packet.data() + 6, static_cast<uint16_t>(nameLength));
    memcpy(packet.data() + PACKET_HEADER_BYTES, name, nameLength);
    length += nameLength;
  } else {
    writeLe16(packet.data() + 6, 0);
  }

  const esp_err_t result = esp_now_send(peerMac, packet.data(), length);
  if (result != ESP_OK) {
    LOG_ERR(LOG_TAG, "esp_now_send failed: %d", static_cast<int>(result));
    return false;
  }
  return true;
}

bool NearbyStatsSyncActivity::sendHello() {
  lastHelloMs_ = millis();
  return sendPacket(PacketType::HELLO, BROADCAST_MAC);
}

bool NearbyStatsSyncActivity::sendDeviceName(const uint8_t*) {
  // Keep the pairing exchange on broadcast delivery. Both readers still
  // identify and filter each other by the immutable device MAC in the packet,
  // while avoiding the asymmetric X3/X4 failure where only one unicast peer
  // registration became usable.
  return sendPacket(PacketType::NAME, BROADCAST_MAC);
}

bool NearbyStatsSyncActivity::sendLocalStats() {
  if (!peerSeen_) return false;
  lastStatsSendMs_ = millis();
  localStatsSent_ = sendPacket(PacketType::STATS, BROADCAST_MAC);
  return localStatsSent_;
}

bool NearbyStatsSyncActivity::sendAck(const uint8_t*) { return sendPacket(PacketType::ACK, BROADCAST_MAC); }

bool NearbyStatsSyncActivity::sendFileMeta() {
  if (!peerSeen_ || outgoingFile_.kind == SyncFileKind::None) return false;

  std::array<uint8_t, MAX_PACKET_BYTES> packet = {};
  packet[0] = 'C';
  packet[1] = 'I';
  packet[2] = 'S';
  packet[3] = 'S';
  packet[4] = PROTOCOL_VERSION;
  packet[5] = static_cast<uint8_t>(PacketType::FILE_META);
  writeLe16(packet.data() + 6, 9);
  std::copy(localDeviceMac_.begin(), localDeviceMac_.end(), packet.begin() + 8);
  uint8_t* payload = packet.data() + PACKET_HEADER_BYTES;
  payload[0] = static_cast<uint8_t>(outgoingFile_.kind);
  writeLe32(payload + 1, outgoingFile_.size);
  writeLe32(payload + 5, outgoingFile_.crc);
  lastFileProgressMs_ = millis();
  lastSyncProgressMs_ = lastFileProgressMs_;
  outgoingFile_.lastSendMs = lastFileProgressMs_;
  return esp_now_send(BROADCAST_MAC, packet.data(), PACKET_HEADER_BYTES + 9) == ESP_OK;
}

bool NearbyStatsSyncActivity::sendFileChunk() {
  if (!peerSeen_ || outgoingFile_.kind == SyncFileKind::None || !outgoingFile_.metadataAcked ||
      outgoingFile_.path == nullptr || outgoingFile_.size == 0) {
    return false;
  }

  const uint32_t offset = static_cast<uint32_t>(outgoingFile_.chunkIndex) * FILE_CHUNK_BYTES;
  if (offset >= outgoingFile_.size) return sendFileDone();

  // Held open across the transfer; sequential sends skip the seek entirely
  // (a fresh open + seek per chunk walked the FAT cluster chain every time).
  if (!outgoingFileHandle_.isOpen() && !Storage.openFileForRead(LOG_TAG, outgoingFile_.path, outgoingFileHandle_)) {
    return false;
  }
  if (outgoingFileHandle_.position() != offset && !outgoingFileHandle_.seek(offset)) {
    outgoingFileHandle_.close();
    return false;
  }

  std::array<uint8_t, MAX_PACKET_BYTES> packet = {};
  packet[0] = 'C';
  packet[1] = 'I';
  packet[2] = 'S';
  packet[3] = 'S';
  packet[4] = PROTOCOL_VERSION;
  packet[5] = static_cast<uint8_t>(PacketType::FILE_CHUNK);
  std::copy(localDeviceMac_.begin(), localDeviceMac_.end(), packet.begin() + 8);
  uint8_t* payload = packet.data() + PACKET_HEADER_BYTES;
  payload[0] = static_cast<uint8_t>(outgoingFile_.kind);
  writeLe16(payload + 1, outgoingFile_.chunkIndex);
  const uint16_t bytesToRead = static_cast<uint16_t>(std::min<uint32_t>(FILE_CHUNK_BYTES, outgoingFile_.size - offset));
  writeLe16(payload + 3, bytesToRead);
  const int read = outgoingFileHandle_.read(payload + 5, bytesToRead);
  if (read != bytesToRead) {
    outgoingFileHandle_.close();
    return false;
  }

  const uint16_t payloadSize = static_cast<uint16_t>(5 + bytesToRead);
  writeLe16(packet.data() + 6, payloadSize);
  lastFileProgressMs_ = millis();
  lastSyncProgressMs_ = lastFileProgressMs_;
  outgoingFile_.lastSendMs = lastFileProgressMs_;
  return esp_now_send(BROADCAST_MAC, packet.data(), PACKET_HEADER_BYTES + payloadSize) == ESP_OK;
}

bool NearbyStatsSyncActivity::sendFileDone() {
  if (!peerSeen_ || outgoingFile_.kind == SyncFileKind::None) return false;

  std::array<uint8_t, MAX_PACKET_BYTES> packet = {};
  packet[0] = 'C';
  packet[1] = 'I';
  packet[2] = 'S';
  packet[3] = 'S';
  packet[4] = PROTOCOL_VERSION;
  packet[5] = static_cast<uint8_t>(PacketType::FILE_DONE);
  writeLe16(packet.data() + 6, 1);
  std::copy(localDeviceMac_.begin(), localDeviceMac_.end(), packet.begin() + 8);
  packet[PACKET_HEADER_BYTES] = static_cast<uint8_t>(outgoingFile_.kind);
  outgoingFile_.doneSent = true;
  lastFileProgressMs_ = millis();
  lastSyncProgressMs_ = lastFileProgressMs_;
  outgoingFile_.lastSendMs = lastFileProgressMs_;
  return esp_now_send(BROADCAST_MAC, packet.data(), PACKET_HEADER_BYTES + 1) == ESP_OK;
}

bool NearbyStatsSyncActivity::sendFileAck(const SyncFileKind kind, const FileAckPhase phase, const uint16_t chunkIndex) {
  std::array<uint8_t, MAX_PACKET_BYTES> packet = {};
  packet[0] = 'C';
  packet[1] = 'I';
  packet[2] = 'S';
  packet[3] = 'S';
  packet[4] = PROTOCOL_VERSION;
  packet[5] = static_cast<uint8_t>(PacketType::FILE_ACK);
  writeLe16(packet.data() + 6, 4);
  std::copy(localDeviceMac_.begin(), localDeviceMac_.end(), packet.begin() + 8);
  uint8_t* payload = packet.data() + PACKET_HEADER_BYTES;
  payload[0] = static_cast<uint8_t>(kind);
  payload[1] = static_cast<uint8_t>(phase);
  writeLe16(payload + 2, chunkIndex);
  return esp_now_send(BROADCAST_MAC, packet.data(), PACKET_HEADER_BYTES + 4) == ESP_OK;
}

bool NearbyStatsSyncActivity::beginNextOutgoingFile() {
  // computeFileInfo below re-opens the path for the CRC pass, and SdFat allows
  // only one open handle per file on hardware.
  if (outgoingFileHandle_.isOpen()) outgoingFileHandle_.close();
  if (outgoingFile_.kind == SyncFileKind::None) {
    outgoingFile_.kind = SyncFileKind::BookStats;
  } else if (outgoingFile_.kind == SyncFileKind::BookStats && outgoingFile_.doneAcked) {
    outgoingFile_ = {};
    outgoingFile_.kind = SyncFileKind::BookDetails;
  } else if (outgoingFile_.kind == SyncFileKind::BookDetails && outgoingFile_.doneAcked) {
    outgoingFile_ = {};
    outgoingFile_.kind = SyncFileKind::Journal;
  } else if (outgoingFile_.kind == SyncFileKind::Journal && outgoingFile_.doneAcked) {
    outgoingFile_ = {};
    outgoingFile_.kind = SyncFileKind::Ledger;
  } else if (outgoingFile_.kind == SyncFileKind::Ledger && outgoingFile_.doneAcked) {
    outgoingFile_ = {};
    outgoingFile_.kind = SyncFileKind::StatsDate;
  } else if (outgoingFile_.kind == SyncFileKind::StatsDate && outgoingFile_.doneAcked) {
    outgoingFile_ = {};
    localFilesSent_ = true;
    return true;
  }

  outgoingFile_.path = localFilePathForKind(outgoingFile_.kind);
  outgoingFile_.metadataAcked = false;
  outgoingFile_.doneSent = false;
  outgoingFile_.doneAcked = false;
  outgoingFile_.chunkIndex = 0;
  if (!computeFileInfo(outgoingFile_.path, outgoingFile_.size, outgoingFile_.crc)) {
    outgoingFile_.size = 0;
    outgoingFile_.crc = 0;
  }
  return sendFileMeta();
}

bool NearbyStatsSyncActivity::hasSavedPeerFile(const SyncFileKind kind) const {
  switch (kind) {
    case SyncFileKind::BookStats:
      return peerBookStatsSaved_;
    case SyncFileKind::BookDetails:
      return peerBookDetailsSaved_;
    case SyncFileKind::Journal:
      return peerJournalSaved_;
    case SyncFileKind::Ledger:
      return peerLedgerSaved_;
    case SyncFileKind::StatsDate:
      return peerStatsDateSaved_;
    default:
      return false;
  }
}

void NearbyStatsSyncActivity::handleFileMeta(const SyncEvent& event) {
  if (event.payloadSize != 9) return;
  const auto kind = static_cast<SyncFileKind>(event.payload[0]);
  if (kind != SyncFileKind::BookStats && kind != SyncFileKind::BookDetails && kind != SyncFileKind::Journal &&
      kind != SyncFileKind::Ledger && kind != SyncFileKind::StatsDate) {
    return;
  }
  const uint32_t expectedSize = readLe32(event.payload.data() + 1);
  const uint32_t expectedCrc = readLe32(event.payload.data() + 5);

  // If the sender missed our META ACK and retries the same metadata, do not
  // reset the already-open temp file. Just re-ACK so the transfer can continue.
  if (incomingFile_.active && incomingFile_.kind == kind && incomingFile_.expectedSize == expectedSize &&
      incomingFile_.expectedCrc == expectedCrc) {
    sendFileAck(kind, FileAckPhase::Meta, 0);
    return;
  }

  incomingFile_ = {};
  incomingFile_.kind = kind;
  incomingFile_.expectedSize = expectedSize;
  incomingFile_.expectedCrc = expectedCrc;
  incomingFile_.active = true;
  incomingFile_.nextChunkIndex = 0;

  const std::string dir = syncedDirectoryForKind(kind);
  const std::string tempPath = incomingTempPathForKind(kind);
  if (dir.empty() || tempPath.empty() || !Storage.ensureDirectoryExists(dir.c_str())) {
    setError("could not prepare stats file");
    return;
  }
  if (incomingFileHandle_.isOpen()) incomingFileHandle_.close();
  if (Storage.exists(tempPath.c_str())) Storage.remove(tempPath.c_str());
  if (incomingFile_.expectedSize > 0) {
    // Held open until FILE_DONE so each chunk is a plain append instead of an
    // open/flush/sync/close cycle of its own.
    if (!Storage.openFileForWrite(LOG_TAG, tempPath, incomingFileHandle_)) {
      setError("could not create stats temp file");
      return;
    }
  }
  sendFileAck(kind, FileAckPhase::Meta, 0);
}

void NearbyStatsSyncActivity::handleFileChunk(const SyncEvent& event) {
  if (event.payloadSize < 5) return;
  const auto kind = static_cast<SyncFileKind>(event.payload[0]);
  const uint16_t chunkIndex = readLe16(event.payload.data() + 1);
  const uint16_t chunkSize = readLe16(event.payload.data() + 3);
  if (event.payloadSize != static_cast<uint16_t>(5 + chunkSize)) {
    return;
  }
  if (!incomingFile_.active || kind != incomingFile_.kind) {
    return;
  }
  if (chunkIndex != incomingFile_.nextChunkIndex) {
    // Sender likely missed the previous chunk ACK and resent it. Re-ACK the
    // last accepted chunk instead of dropping the retry and forcing timeout.
    if (chunkIndex + 1 == incomingFile_.nextChunkIndex) {
      sendFileAck(kind, FileAckPhase::Chunk, chunkIndex);
    }
    return;
  }

  if (!incomingFileHandle_.isOpen()) {
    setError("could not append stats chunk");
    return;
  }
  // No per-chunk flush/sync: FILE_DONE syncs once and verifies the whole
  // file's CRC, and a failed transfer discards the temp file anyway.
  if (incomingFileHandle_.write(event.payload.data() + 5, chunkSize) != chunkSize) {
    setError("could not save stats chunk");
    return;
  }
  incomingFile_.receivedSize += chunkSize;
  incomingFile_.nextChunkIndex++;
  markSyncProgress();
  sendFileAck(kind, FileAckPhase::Chunk, chunkIndex);
}

void NearbyStatsSyncActivity::handleFileDone(const SyncEvent& event) {
  if (event.payloadSize != 1) return;
  const auto kind = static_cast<SyncFileKind>(event.payload[0]);
  if (!incomingFile_.active || kind != incomingFile_.kind) {
    // Sender may have missed our DONE ACK after we already verified and moved
    // the received file. Re-ACK the duplicate so the sender can advance.
    if (hasSavedPeerFile(kind)) {
      sendFileAck(kind, FileAckPhase::Done, 0);
    }
    return;
  }

  bool ok = incomingFile_.receivedSize == incomingFile_.expectedSize;
  if (incomingFileHandle_.isOpen()) {
    incomingFileHandle_.flush();
    const bool syncedOk = incomingFileHandle_.sync();
    const bool closedOk = incomingFileHandle_.close();
    ok = ok && syncedOk && closedOk;
  }
  const std::string tempPath = incomingTempPathForKind(kind);
  const std::string finalPath = syncedFilePathForKindAndDeviceMac(kind, peerDeviceMac_);
  if (ok && incomingFile_.expectedSize > 0) {
    uint32_t size = 0;
    uint32_t crc = 0;
    ok = computeFileInfo(tempPath.c_str(), size, crc) && size == incomingFile_.expectedSize &&
         crc == incomingFile_.expectedCrc && !finalPath.empty();
    if (ok) {
      if (Storage.exists(finalPath.c_str()) && !Storage.remove(finalPath.c_str())) ok = false;
      if (ok && !Storage.rename(tempPath.c_str(), finalPath.c_str())) ok = false;
    }
  } else if (ok && incomingFile_.expectedSize == 0 && Storage.exists(tempPath.c_str())) {
    Storage.remove(tempPath.c_str());
  }
  if (!ok) {
    setError("stats file transfer failed");
    return;
  }

  if (kind == SyncFileKind::BookStats) {
    peerBookStatsSaved_ = true;
    LibraryInsights::invalidateCache();
  } else if (kind == SyncFileKind::BookDetails) {
    peerBookDetailsSaved_ = true;
    LibraryInsights::invalidateCache();
  } else if (kind == SyncFileKind::Journal) {
    peerJournalSaved_ = true;
  } else if (kind == SyncFileKind::Ledger) {
    peerLedgerSaved_ = true;
  } else if (kind == SyncFileKind::StatsDate) {
    if (incomingFile_.expectedSize == 0 || finalPath.empty() ||
        !mergeClocklessReadingStatsDateFromFile(finalPath.c_str())) {
      setError("invalid peer stats date");
      return;
    }
    peerStatsDateSaved_ = true;
  }
  incomingFile_ = {};
  markSyncProgress();
  sendFileAck(kind, FileAckPhase::Done, 0);
}

void NearbyStatsSyncActivity::handleFileAck(const SyncEvent& event) {
  if (event.payloadSize != 4 || outgoingFile_.kind == SyncFileKind::None) return;
  const auto kind = static_cast<SyncFileKind>(event.payload[0]);
  const auto phase = static_cast<FileAckPhase>(event.payload[1]);
  const uint16_t chunkIndex = readLe16(event.payload.data() + 2);
  if (kind != outgoingFile_.kind) return;

  if (phase == FileAckPhase::Meta) {
    outgoingFile_.metadataAcked = true;
    markSyncProgress();
    if (outgoingFile_.size == 0) {
      sendFileDone();
    } else {
      sendFileChunk();
    }
  } else if (phase == FileAckPhase::Chunk && chunkIndex == outgoingFile_.chunkIndex) {
    outgoingFile_.chunkIndex++;
    markSyncProgress();
    const uint32_t offset = static_cast<uint32_t>(outgoingFile_.chunkIndex) * FILE_CHUNK_BYTES;
    if (offset >= outgoingFile_.size) {
      sendFileDone();
    } else {
      sendFileChunk();
    }
  } else if (phase == FileAckPhase::Done) {
    outgoingFile_.doneAcked = true;
    markSyncProgress();
    beginNextOutgoingFile();
  }
}

void NearbyStatsSyncActivity::updateFileTransferProgress() {
  if (!peerStatsSaved_ || !localStatsAcked_ || localFilesSent_) return;

  const uint32_t now = millis();
  if (outgoingFile_.kind == SyncFileKind::None) {
    beginNextOutgoingFile();
    return;
  }
  if (now - outgoingFile_.lastSendMs < FILE_RETRY_INTERVAL_MS) return;

  if (!outgoingFile_.metadataAcked) {
    sendFileMeta();
  } else if (outgoingFile_.doneSent && !outgoingFile_.doneAcked) {
    sendFileDone();
  } else if (!outgoingFile_.doneSent) {
    sendFileChunk();
  }
}

std::string NearbyStatsSyncActivity::syncProgressDetail() const {
  const auto fileLabel = [](const SyncFileKind kind) -> const char* {
    switch (kind) {
      case SyncFileKind::BookStats:
        return "Book index";
      case SyncFileKind::BookDetails:
        return "Book details";
      case SyncFileKind::Journal:
        return "Journal";
      case SyncFileKind::Ledger:
        return "Ledger";
      case SyncFileKind::StatsDate:
        return "Dates";
      default:
        return "Files";
    }
  };
  const auto chunkTotal = [](const uint32_t bytes) -> uint16_t {
    return static_cast<uint16_t>((bytes + FILE_CHUNK_BYTES - 1) / FILE_CHUNK_BYTES);
  };

  char line[72];
  if (outgoingFile_.kind != SyncFileKind::None && !localFilesSent_) {
    const uint16_t total = chunkTotal(outgoingFile_.size);
    if (!outgoingFile_.metadataAcked) {
      snprintf(line, sizeof(line), "Sending %s: preparing", fileLabel(outgoingFile_.kind));
    } else if (outgoingFile_.doneSent && !outgoingFile_.doneAcked) {
      snprintf(line, sizeof(line), "Sending %s: finishing", fileLabel(outgoingFile_.kind));
    } else if (total > 0) {
      snprintf(line, sizeof(line), "Sending %s: %u/%u", fileLabel(outgoingFile_.kind),
               static_cast<unsigned>(std::min<uint16_t>(outgoingFile_.chunkIndex + 1, total)),
               static_cast<unsigned>(total));
    } else {
      snprintf(line, sizeof(line), "Sending %s", fileLabel(outgoingFile_.kind));
    }
    return line;
  }

  if (incomingFile_.active) {
    const uint16_t total = chunkTotal(incomingFile_.expectedSize);
    if (total > 0) {
      snprintf(line, sizeof(line), "Receiving %s: %u/%u", fileLabel(incomingFile_.kind),
               static_cast<unsigned>(std::min<uint16_t>(incomingFile_.nextChunkIndex + 1, total)),
               static_cast<unsigned>(total));
    } else {
      snprintf(line, sizeof(line), "Receiving %s", fileLabel(incomingFile_.kind));
    }
    return line;
  }

  if (!peerStatsSaved_) return "Waiting for peer stats";
  if (!localStatsAcked_) return "Sending local totals";
  if (!localFilesSent_) return "Preparing file transfer";
  if (!peerBookStatsSaved_) return "Waiting for book index";
  if (!peerBookDetailsSaved_) return "Waiting for book details";
  if (!peerJournalSaved_) return "Waiting for journal";
  if (!peerLedgerSaved_) return "Waiting for ledger";
  if (!peerStatsDateSaved_) return "Waiting for dates";
  return "Finalizing";
}

void NearbyStatsSyncActivity::exitViaBack() {
  mappedInput.suppressNextBackRelease();
  finish();
}

void NearbyStatsSyncActivity::updateSyncProgress() {
  if (state_ != State::DISCOVERING && state_ != State::SYNCING) return;

  const uint32_t now = millis();
  const uint32_t timeoutBase = peerSeen_ ? lastSyncProgressMs_ : syncStartedMs_;
  const uint32_t timeoutMs = peerSeen_ ? SYNC_IDLE_TIMEOUT_MS : DISCOVERY_TIMEOUT_MS;
  if (now - timeoutBase > timeoutMs) {
    setError(peerSeen_ ? "stats sync stalled" : "no reader found");
    return;
  }

  updateFileTransferProgress();

  if (peerStatsSaved_ && localStatsAcked_ && localFilesSent_ && peerBookStatsSaved_ && peerBookDetailsSaved_ &&
      peerJournalSaved_ && peerLedgerSaved_ && peerStatsDateSaved_) {
    syncCompletedAtMs_ = now;
    setState(State::SYNCED);
    writeSyncTiming("synced");
    return;
  }

  if (!peerSeen_ && now - lastHelloMs_ >= HELLO_INTERVAL_MS) {
    sendHello();
    return;
  }

  if (peerSeen_ && localStatsReady_ && !localStatsAcked_ && now - lastStatsSendMs_ >= STATS_RETRY_INTERVAL_MS) {
    sendLocalStats();
  }
}

void NearbyStatsSyncActivity::setState(const State state) {
  if (state_ == state) return;
  state_ = state;
  lastStateChangeMs_ = millis();
  requestUpdate();
}

void NearbyStatsSyncActivity::setError(const std::string& error) {
  LOG_ERR(LOG_TAG, "%s", error.c_str());
  closeTransferFiles();
  errorMessage_ = error;
  setState(State::ERROR);
  writeSyncTiming("error");
}

void NearbyStatsSyncActivity::writeSyncTiming(const char* outcome) {
  if (syncTimingWritten_ || syncStartedMs_ == 0) return;

  FsFile timingFile;
  if (!Storage.openFileForWrite(LOG_TAG, DUET_STATE_ROOT_PATH "/nearby_sync_timing.txt", timingFile)) return;
  char buf[360];
  const int n = snprintf(
      buf, sizeof(buf),
      "outcome=%s elapsed=%lums peer=%u stats=%u/%u files=%u book=%u details=%u journal=%u ledger=%u date=%u "
      "lateStatsAck=%u lateFileAck=%u out=%u/%u/%u in=%u/%lu/%lu idle=%lums error=%s\n",
      outcome ? outcome : "unknown", millis() - syncStartedMs_, peerSeen_ ? 1u : 0u, peerStatsSaved_ ? 1u : 0u,
      localStatsAcked_ ? 1u : 0u, localFilesSent_ ? 1u : 0u, peerBookStatsSaved_ ? 1u : 0u,
      peerBookDetailsSaved_ ? 1u : 0u, peerJournalSaved_ ? 1u : 0u, peerLedgerSaved_ ? 1u : 0u,
      peerStatsDateSaved_ ? 1u : 0u, static_cast<unsigned>(lateStatsAckCount_),
      static_cast<unsigned>(lateFileAckCount_), static_cast<unsigned>(outgoingFile_.kind),
      static_cast<unsigned>(outgoingFile_.chunkIndex), static_cast<unsigned>(outgoingFile_.doneAcked ? 1 : 0),
      static_cast<unsigned>(incomingFile_.kind), static_cast<unsigned long>(incomingFile_.receivedSize),
      static_cast<unsigned long>(incomingFile_.expectedSize), millis() - lastSyncProgressMs_, errorMessage_.c_str());
  if (n > 0) timingFile.write(reinterpret_cast<const uint8_t*>(buf), std::min<int>(n, sizeof(buf) - 1));
  timingFile.close();
  syncTimingWritten_ = true;
}

void NearbyStatsSyncActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_NEARBY_STATS_SYNC));

  const int centerY = pageHeight / 2 - 20;
  std::string primary;
  std::string detailPrimary;
  std::string detailSecondary;

  switch (state_) {
    case State::STARTING:
      primary = tr(STR_LOADING_POPUP);
      break;
    case State::READY:
      primary = tr(STR_NEARBY_STATS_READY);
      detailPrimary = std::string(tr(STR_DEVICE_NAME)) + ": " + SETTINGS.getEffectiveDeviceName();
      break;
    case State::PREPARING:
      primary = tr(STR_NEARBY_STATS_PREPARING);
      if (prepScannedDirs_ > 0) {
        detailPrimary = std::string(tr(STR_NEARBY_STATS_PREP_SCANNED)) + ": " + std::to_string(prepScannedDirs_);
      }
      break;
    case State::DISCOVERING:
      primary = tr(STR_NEARBY_STATS_SCANNING);
      break;
    case State::SYNCING:
      primary = tr(STR_NEARBY_STATS_SYNCING);
      detailPrimary = std::string(I18N.get(peerName_.empty() ? StrId::STR_SYSTEM_DEVICE : StrId::STR_DEVICE_NAME)) +
                      ": " + (peerName_.empty() ? peerId_ : peerName_);
      detailSecondary = syncProgressDetail();
      break;
    case State::SYNCED:
      primary = tr(STR_NEARBY_STATS_SYNCED);
      detailPrimary = std::string(I18N.get(peerName_.empty() ? StrId::STR_SYSTEM_DEVICE : StrId::STR_DEVICE_NAME)) +
                      ": " + (peerName_.empty() ? peerId_ : peerName_);
      if (!isZeroMac(peerDeviceMac_)) {
        detailSecondary = std::string(tr(STR_FILENAME)) + ": " + statsFileNameForDeviceMac(peerDeviceMac_);
      }
      break;
    case State::ERROR:
      primary = tr(STR_ERROR_MSG);
      detailPrimary = errorMessage_;
      break;
  }

  if (state_ == State::READY || state_ == State::SYNCED || state_ == State::ERROR) {
    renderReady(primary, detailPrimary, detailSecondary);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_NEARBY_STATS_SYNC_BUTTON), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  renderer.drawCenteredText(UI_10_FONT_ID, centerY, primary.c_str(), true, EpdFontFamily::BOLD);
  if (!detailPrimary.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, centerY + renderer.getLineHeight(UI_10_FONT_ID) + 8,
                              detailPrimary.c_str());
  }
  if (!detailSecondary.empty()) {
    renderer.drawCenteredText(
        SMALL_FONT_ID, centerY + renderer.getLineHeight(UI_10_FONT_ID) + renderer.getLineHeight(SMALL_FONT_ID) + 14,
        detailSecondary.c_str());
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void NearbyStatsSyncActivity::renderReady(const std::string& primary, const std::string& detailPrimary,
                                          const std::string& detailSecondary) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  int y = contentTop + 70;

  renderer.drawCenteredText(UI_10_FONT_ID, y, primary.c_str(), true, EpdFontFamily::BOLD);
  y += lineHeight + metrics.verticalSpacing;
  if (!detailPrimary.empty()) {
    renderer.drawCenteredText(SMALL_FONT_ID, y, detailPrimary.c_str(), true);
    y += renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing;
  }
  if (!detailSecondary.empty()) {
    renderer.drawCenteredText(SMALL_FONT_ID, y, detailSecondary.c_str(), true);
    y += renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing;
  }
  if (state_ == State::READY) {
    renderer.drawCenteredText(SMALL_FONT_ID, y, tr(STR_NEARBY_STATS_READY_HINT), true);
  }
}

#endif
