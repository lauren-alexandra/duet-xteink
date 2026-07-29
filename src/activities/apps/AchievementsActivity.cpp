#include "AchievementsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>
#include <vector>

#include "AchievementStore.h"
#include "MappedInputManager.h"
#include "components/CompactHeader.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
const char* metricDescription(const AchievementMetric metric) {
  switch (metric) {
    case AchievementMetric::BooksStarted:
      return tr(STR_ACHIEVEMENT_BOOKS_STARTED_DESC);
    case AchievementMetric::BooksFinished:
      return tr(STR_ACHIEVEMENT_BOOKS_FINISHED_DESC);
    case AchievementMetric::Sessions:
      return tr(STR_ACHIEVEMENT_SESSIONS_DESC);
    case AchievementMetric::TotalReadingSeconds:
      return tr(STR_ACHIEVEMENT_READING_TIME_DESC);
    case AchievementMetric::GoalDays:
      return tr(STR_ACHIEVEMENT_GOAL_DAYS_DESC);
    case AchievementMetric::LongestGoalStreak:
      return tr(STR_ACHIEVEMENT_GOAL_STREAK_DESC);
    case AchievementMetric::Bookmarks:
      return tr(STR_ACHIEVEMENT_BOOKMARKS_DESC);
    case AchievementMetric::LongestSessionSeconds:
      return tr(STR_ACHIEVEMENT_LONGEST_SESSION_DESC);
    case AchievementMetric::ReadingDays:
      return tr(STR_ACHIEVEMENT_READING_DAYS_DESC);
    case AchievementMetric::LongestReadingStreak:
      return tr(STR_ACHIEVEMENT_READING_STREAK_DESC);
    case AchievementMetric::ScreenPagesTurned:
      return tr(STR_ACHIEVEMENT_SCREEN_PAGES_DESC);
    case AchievementMetric::SeriesStarted:
      return tr(STR_ACHIEVEMENT_SERIES_STARTED_DESC);
    case AchievementMetric::SeriesCompleted:
      return tr(STR_ACHIEVEMENT_SERIES_COMPLETED_DESC);
    case AchievementMetric::SpiceLevelsExplored:
      return tr(STR_ACHIEVEMENT_SPICE_LEVELS_DESC);
    case AchievementMetric::MorningReadingSeconds:
      return tr(STR_ACHIEVEMENT_EARLY_BIRD_DESC);
    case AchievementMetric::NightReadingSeconds:
      return tr(STR_ACHIEVEMENT_NIGHT_READER_DESC);
    case AchievementMetric::WeekendReadingSeconds:
      return tr(STR_ACHIEVEMENT_WEEKEND_READER_DESC);
    case AchievementMetric::CrossDeviceSync:
      return tr(STR_ACHIEVEMENT_CROSS_DEVICE_DESC);
    case AchievementMetric::Count:
      break;
  }
  return "";
}

std::string formatDuration(const uint64_t seconds) {
  const uint64_t minutes = seconds / 60u;
  if (minutes < 60) return std::to_string(minutes) + "m";
  const uint64_t hours = minutes / 60u;
  const uint64_t remainingMinutes = minutes % 60u;
  if (remainingMinutes == 0) return std::to_string(hours) + "h";
  return std::to_string(hours) + "h " + std::to_string(remainingMinutes) + "m";
}

bool durationMetric(const AchievementMetric metric) {
  return metric == AchievementMetric::TotalReadingSeconds || metric == AchievementMetric::LongestSessionSeconds ||
         metric == AchievementMetric::MorningReadingSeconds || metric == AchievementMetric::NightReadingSeconds ||
         metric == AchievementMetric::WeekendReadingSeconds;
}

std::string progressLabel(const AchievementView& achievement) {
  if (achievement.unlocked) return tr(STR_DONE);
  if (durationMetric(achievement.metric)) {
    return formatDuration(achievement.progress) + " / " + formatDuration(achievement.target);
  }
  return std::to_string(achievement.progress) + " / " + std::to_string(achievement.target);
}
}  // namespace

void AchievementsActivity::rebuildVisibleIndexes(const bool allowFallback) {
  visibleIndexes.clear();
  const bool completed = selectedTab == Tab::Completed;
  for (int i = 0; i < static_cast<int>(achievements.size()); ++i) {
    if (achievements[i].unlocked == completed) visibleIndexes.push_back(i);
  }

  if (allowFallback && visibleIndexes.empty() && !achievements.empty()) {
    selectedTab = completed ? Tab::Pending : Tab::Completed;
    rebuildVisibleIndexes(false);
  }
  selectedIndex = std::clamp(selectedIndex, 0, std::max(0, static_cast<int>(visibleIndexes.size()) - 1));
}

void AchievementsActivity::toggleTab() {
  selectedTab = selectedTab == Tab::Pending ? Tab::Completed : Tab::Pending;
  selectedIndex = 0;
  rebuildVisibleIndexes(false);
  requestUpdate();
}

bool AchievementsActivity::hasActiveInput() const {
  return mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased() ||
         mappedInput.isPressed(MappedInputManager::Button::Back) ||
         mappedInput.isPressed(MappedInputManager::Button::Confirm) ||
         mappedInput.isPressed(MappedInputManager::Button::Left) ||
         mappedInput.isPressed(MappedInputManager::Button::Right) ||
         mappedInput.isPressed(MappedInputManager::Button::Up) ||
         mappedInput.isPressed(MappedInputManager::Button::Down);
}

void AchievementsActivity::onEnter() {
  Activity::onEnter();
  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  // Paint the compact counter-backed list first. Catalogue and bookmark
  // enrichment follows after the screen is visible, avoiding a blank/frozen
  // transition from the unlock popup on the smaller X3 heap.
  snapshot = AchievementCatalog::loadLightweightSnapshot();
  achievements = AchievementCatalog::buildViews(snapshot);
  ACHIEVEMENT_STORE.applyPersistedUnlocks(achievements);
  rebuildVisibleIndexes();
  fullSnapshotPending = true;
  lightweightFramePainted = false;
  fullSnapshotNotBefore = millis() + 1800;
  requestUpdate();
}

void AchievementsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (waitForConfirmRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) waitForConfirmRelease = false;
    return;
  }

  if (fullSnapshotPending && lightweightFramePainted && millis() >= fullSnapshotNotBefore) {
    if (hasActiveInput()) {
      fullSnapshotNotBefore = millis() + 900;
      return;
    }
    fullSnapshotPending = false;
    snapshot = AchievementCatalog::loadSnapshot();
    achievements = AchievementCatalog::buildViews(snapshot);
    ACHIEVEMENT_STORE.reconcile(achievements, false);
    rebuildVisibleIndexes();
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    toggleTab();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    if (visibleIndexes.empty()) return;
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(visibleIndexes.size()));
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    if (visibleIndexes.empty()) return;
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(visibleIndexes.size()));
    requestUpdate();
  });
}

void AchievementsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int tabTop = CompactHeader::headerBottomY(metrics);

  CompactHeader::drawTitle(renderer, tr(STR_ACHIEVEMENTS), true);

  int completedCount = 0;
  for (const auto& achievement : achievements) {
    if (achievement.unlocked) ++completedCount;
  }
  const int pendingCount = static_cast<int>(achievements.size()) - completedCount;
  const std::string pendingLabel = std::string(tr(STR_PENDING)) + " (" + std::to_string(pendingCount) + ")";
  const std::string completedLabel = std::string(tr(STR_COMPLETED)) + " (" + std::to_string(completedCount) + ")";
  const std::vector<TabInfo> tabs = {
      {pendingLabel.c_str(), selectedTab == Tab::Pending},
      {completedLabel.c_str(), selectedTab == Tab::Completed},
  };
  GUI.drawTabBar(renderer, Rect{0, tabTop, pageWidth, metrics.tabBarHeight}, tabs, false);

  const int contentTop = tabTop + metrics.tabBarHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  if (visibleIndexes.empty()) {
    const char* emptyLabel =
        selectedTab == Tab::Completed ? tr(STR_NO_COMPLETED_ACHIEVEMENTS) : tr(STR_NO_PENDING_ACHIEVEMENTS);
    renderer.drawCenteredText(UI_12_FONT_ID, contentTop + contentHeight / 2, emptyLabel);
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(visibleIndexes.size()), selectedIndex,
        [this](const int index) { return achievementTargetLabel(achievements[visibleIndexes[index]]); },
        [this](const int index) { return std::string(metricDescription(achievements[visibleIndexes[index]].metric)); },
        nullptr, [this](const int index) { return progressLabel(achievements[visibleIndexes[index]]); });
  }

  const char* nextTab = selectedTab == Tab::Pending ? tr(STR_COMPLETED) : tr(STR_PENDING);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), nextTab, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
  lightweightFramePainted = true;
}
