#include "BackupStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "activities/reader/StatsBackup.h"
#include "components/UITheme.h"
#include "fontIds.h"

void BackupStatsActivity::onEnter() {
  Activity::onEnter();
  state = WARNING;
  backupFileName[0] = '\0';
  backupFileCount = 0;
  backupDataBytes = 0;
  requestUpdate();
}

void BackupStatsActivity::onExit() { Activity::onExit(); }

void BackupStatsActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 tr(STR_EXPORT_ALL_READING_STATS));

  if (state == WARNING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_EXPORT_READING_STATS_CONFIRM), true);

    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_CONFIRM), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == SUCCESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 40, tr(STR_EXPORT_READING_STATS_DONE), true,
                              EpdFontFamily::BOLD);
    const std::string visibleFileName = renderer.truncatedText(
        SMALL_FONT_ID, backupFileName[0] != '\0' ? backupFileName : "-", pageWidth - 32);
    renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 10, visibleFileName.c_str());
    char detail[64];
    snprintf(detail, sizeof(detail), "%u files | %lu bytes", static_cast<unsigned>(backupFileCount),
             static_cast<unsigned long>(backupDataBytes));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 40, detail);

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_BACKUP_STATS_FAILED), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, tr(STR_CHECK_SERIAL_OUTPUT));

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void BackupStatsActivity::runBackup() {
  LOG_DBG("BACKUP_STATS", "Creating complete reading-stats export");
  state = exportAllReadingStats(backupFileName, sizeof(backupFileName), &backupFileCount, &backupDataBytes)
              ? SUCCESS
              : FAILED;
  requestUpdate();
}

void BackupStatsActivity::loop() {
  if (state == WARNING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      runBackup();
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    goBack();
  }
}
