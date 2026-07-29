#include "RestoreStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "activities/reader/StatsBackup.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
std::string fileNameFromPath(const std::string& path) {
  const size_t separator = path.rfind('/');
  return separator == std::string::npos ? path : path.substr(separator + 1);
}
}

void RestoreStatsActivity::onEnter() {
  Activity::onEnter();
  archivePaths = listReadingStatsArchives();
  archiveLabels.clear();
  archiveLabels.reserve(archivePaths.size());
  for (const std::string& path : archivePaths) archiveLabels.push_back(fileNameFromPath(path));
  state = archivePaths.empty() ? State::Empty : State::Select;
  selectedIndex = 0;
  safetyFileName[0] = '\0';
  restoredFileCount = 0;
  requestUpdate();
}

const char* RestoreStatsActivity::selectedLabel() const {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(archiveLabels.size())) return "-";
  return archiveLabels[static_cast<size_t>(selectedIndex)].c_str();
}

void RestoreStatsActivity::runRestore() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(archivePaths.size())) {
    state = State::Failed;
  } else {
    LOG_DBG("RESTORE_STATS", "Restoring reading stats from %s",
            archivePaths[static_cast<size_t>(selectedIndex)].c_str());
    state = importAllReadingStats(archivePaths[static_cast<size_t>(selectedIndex)], safetyFileName,
                                  sizeof(safetyFileName), &restoredFileCount)
                ? State::Success
                : State::Failed;
  }
  requestUpdate();
}

void RestoreStatsActivity::loop() {
  if (state == State::Restoring) {
    runRestore();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    mappedInput.suppressNextBackRelease();
    if (state == State::Confirm) {
      state = State::Select;
      requestUpdate();
    } else {
      finish();
    }
    return;
  }
  if (state == State::Select) {
    const int count = static_cast<int>(archivePaths.size());
    navigator.onNextRelease([this, count] {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, count);
      requestUpdate();
    });
    navigator.onPreviousRelease([this, count] {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, count);
      requestUpdate();
    });
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      state = State::Confirm;
      requestUpdate();
    }
    return;
  }
  if (state == State::Confirm && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    state = State::Restoring;
    requestUpdate();
    return;
  }
  if ((state == State::Success || state == State::Failed || state == State::Empty) &&
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    finish();
  }
}

void RestoreStatsActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 tr(STR_RESTORE_READING_STATS));

  if (state == State::Select) {
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(archiveLabels.size()),
                 selectedIndex, [this](const int index) { return archiveLabels[static_cast<size_t>(index)]; },
                 nullptr, nullptr, nullptr, true);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == State::Confirm) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 55, tr(STR_RESTORE_READING_STATS_CONFIRM), true,
                              EpdFontFamily::BOLD);
    const std::string visibleFileName =
        renderer.truncatedText(SMALL_FONT_ID, selectedLabel(), pageWidth - 32);
    renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2, visibleFileName.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_CONFIRM), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == State::Restoring) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, tr(STR_RESTORE_READING_STATS_WORKING), true,
                              EpdFontFamily::BOLD);
  } else if (state == State::Success) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 55, tr(STR_RESTORE_READING_STATS_DONE), true,
                              EpdFontFamily::BOLD);
    char detail[48];
    snprintf(detail, sizeof(detail), "%u files", static_cast<unsigned>(restoredFileCount));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 15, detail);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 20, tr(STR_RESTORE_SAFETY_COPY));
    const std::string visibleSafetyFile = renderer.truncatedText(
        SMALL_FONT_ID, safetyFileName[0] != '\0' ? safetyFileName : "-", pageWidth - 32);
    renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 50, visibleSafetyFile.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    const char* message = state == State::Empty ? tr(STR_RESTORE_READING_STATS_EMPTY)
                                                : tr(STR_RESTORE_READING_STATS_FAILED);
    const std::string visibleMessage =
        renderer.truncatedText(UI_10_FONT_ID, message, pageWidth - 32, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, visibleMessage.c_str(), true,
                              EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer();
}
