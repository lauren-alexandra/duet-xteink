#include "UtilitiesActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "AchievementsActivity.h"
#include "DictionaryActivity.h"
#include "FavoritesActivity.h"
#include "GlobalActions.h"
#include "IfFoundActivity.h"
#include "LauncherCatalog.h"
#include "LauncherCustomizeActivity.h"
#include "MappedInputManager.h"
#include "ReadMeActivity.h"
#include "ScreenCleanActivity.h"
#include "TetrisActivity.h"
#include "activities/home/LibrarySearchActivity.h"
#include "activities/home/CurrentBookStats.h"
#include "activities/home/SavedItemsHomeActivity.h"
#include "activities/reader/BookReadingStats.h"
#include "activities/reader/GlobalReadingStats.h"
#include "activities/settings/KOReaderSettingsActivity.h"
#include "components/CompactHeader.h"
#include "components/UITheme.h"

int UtilitiesActivity::itemCount() const {
  return static_cast<int>(LAUNCHER_LAYOUT.count(LauncherSurface::Apps));
}

LauncherItem UtilitiesActivity::selectedItem() const {
  return LAUNCHER_LAYOUT.itemAt(LauncherSurface::Apps, static_cast<size_t>(selectedIndex));
}

void UtilitiesActivity::onEnter() {
  Activity::onEnter();
  if (selectedIndex >= itemCount()) selectedIndex = std::max(0, itemCount() - 1);
  requestUpdate();
}

void UtilitiesActivity::openStats(const BookStatsActivity::InitialPage initialPage) {
  const GlobalReadingStats localStats = GlobalReadingStats::load();
  CurrentBookStatsTarget lastActiveBook;
  const bool hasLastActiveBook = CurrentBookStats::loadLastActive(lastActiveBook);
  const std::string title = hasLastActiveBook ? lastActiveBook.title : std::string(tr(STR_READING_STATS));
  const std::string cachePath = hasLastActiveBook ? lastActiveBook.cachePath : std::string{};
  const BookReadingStats stats = hasLastActiveBook ? lastActiveBook.stats : BookReadingStats{};
  const float progressPercent = hasLastActiveBook ? lastActiveBook.progressPercent : -1.0f;
  const uint32_t wordCount = hasLastActiveBook ? lastActiveBook.wordCount : 0;
  if (GlobalReadingStats::hasSyncedStats()) {
    startActivityForResult(
        std::make_unique<BookStatsActivity>(renderer, mappedInput, title, cachePath, stats, progressPercent, false, 0,
                                            localStats,
                                            GlobalReadingStats::loadAggregated(localStats), false,
                                            ReadingSessionSnapshot{}, wordCount, initialPage),
        [this](const ActivityResult&) { requestUpdate(); });
  } else {
    startActivityForResult(
        std::make_unique<BookStatsActivity>(renderer, mappedInput, title, cachePath, stats, progressPercent, false, 0,
                                            localStats, false,
                                            ReadingSessionSnapshot{}, wordCount, initialPage),
        [this](const ActivityResult&) { requestUpdate(); });
  }
}

void UtilitiesActivity::openSelected(const LauncherItem item) {
  switch (item) {
    case LauncherItem::BrowseFiles:
      activityManager.goToFileBrowser();
      break;
    case LauncherItem::SearchLibrary:
      startActivityForResult(std::make_unique<LibrarySearchActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;
    case LauncherItem::RecentBooks:
      activityManager.goToRecentBooks();
      break;
    case LauncherItem::ReadingStats:
      openStats(BookStatsActivity::InitialPage::CurrentBook);
      break;
    case LauncherItem::ReadingHeatmap:
      openStats(BookStatsActivity::InitialPage::Heatmap);
      break;
    case LauncherItem::ReadingProfile:
      openStats(BookStatsActivity::InitialPage::ReadingProfile);
      break;
    case LauncherItem::SavedItems:
      startActivityForResult(std::make_unique<SavedItemsHomeActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;
    case LauncherItem::Achievements:
      startActivityForResult(std::make_unique<AchievementsActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;
    case LauncherItem::Favorites:
      startActivityForResult(std::make_unique<FavoritesActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;
    case LauncherItem::Dictionary:
      startActivityForResult(std::make_unique<DictionaryActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;
    case LauncherItem::Tetris:
      startActivityForResult(std::make_unique<TetrisActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;
    case LauncherItem::IfFound:
      startActivityForResult(std::make_unique<IfFoundActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;
    case LauncherItem::ScreenClean:
      startActivityForResult(std::make_unique<ScreenCleanActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;
    case LauncherItem::NearbyStatsSync:
      activityManager.goToNearbyStatsSync();
      break;
    case LauncherItem::FileTransfer:
      activityManager.goToFileTransfer();
      break;
    case LauncherItem::OpdsBrowser:
      activityManager.goToBrowser();
      break;
    case LauncherItem::KOReaderSync:
      startActivityForResult(std::make_unique<KOReaderSettingsActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;
    case LauncherItem::Sleep:
      enterDeepSleep();
      break;
    case LauncherItem::ReadMe:
      startActivityForResult(std::make_unique<ReadMeActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;
    case LauncherItem::CustomizeHomeApps:
      startActivityForResult(std::make_unique<LauncherCustomizeActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) {
                               selectedIndex = std::min(selectedIndex, std::max(0, itemCount() - 1));
                               requestUpdate();
                             });
      break;
    case LauncherItem::Settings:
      activityManager.goToSettings();
      break;
    case LauncherItem::Apps:
    case LauncherItem::Count:
      break;
  }
}

void UtilitiesActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelected(selectedItem());
    return;
  }

  const int count = itemCount();
  buttonNavigator.onNextRelease([this, count] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, count);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, count] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, count);
    requestUpdate();
  });
}

void UtilitiesActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = CompactHeader::contentTop(metrics) + metrics.verticalSpacing;
  const int contentHeight = renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  CompactHeader::drawTitle(renderer, tr(STR_APPS), true);
  GUI.drawList(
      renderer, Rect{0, contentTop, renderer.getScreenWidth(), contentHeight}, itemCount(), selectedIndex,
      [](const int index) {
        const LauncherItem item = LAUNCHER_LAYOUT.itemAt(LauncherSurface::Apps, static_cast<size_t>(index));
        return std::string(I18N.get(launcherItemLabel(item)));
      },
      [](const int index) {
        const LauncherItem item = LAUNCHER_LAYOUT.itemAt(LauncherSurface::Apps, static_cast<size_t>(index));
        return std::string(I18N.get(launcherItemDescription(item)));
      });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
