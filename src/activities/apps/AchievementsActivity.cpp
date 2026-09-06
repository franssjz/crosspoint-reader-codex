#include "AchievementsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>
#include <vector>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "util/HeaderDateUtils.h"

namespace fui = freeink::ui;

namespace {
std::string formatDurationCompact(const uint64_t totalMs) {
  const uint64_t totalMinutes = totalMs / 60000ULL;
  const uint64_t hours = totalMinutes / 60ULL;
  const uint64_t minutes = totalMinutes % 60ULL;
  if (hours == 0) {
    return std::to_string(minutes) + "m";
  }
  return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
}

std::string getProgressLabel(const AchievementView& view) {
  if (view.state.unlocked) {
    return tr(STR_DONE);
  }

  switch (view.definition->metric) {
    case AchievementMetric::TotalReadingMs:
    case AchievementMetric::MaxSessionMs:
      return formatDurationCompact(view.progress) + " / " + formatDurationCompact(view.target);
    default:
      return std::to_string(view.progress) + " / " + std::to_string(view.target);
  }
}

const char* tabLabelFor(const bool completed) { return completed ? tr(STR_COMPLETED) : tr(STR_PENDING); }

std::string tabLabelWithCount(const bool completed, const int count) {
  return std::string(tabLabelFor(completed)) + " (" + std::to_string(count) + ")";
}

const char* emptyStateLabel(const bool completed) {
  return completed ? tr(STR_NO_COMPLETED_ACHIEVEMENTS) : tr(STR_NO_PENDING_ACHIEVEMENTS);
}
}  // namespace

void AchievementsActivity::rebuildVisibleIndexes() {
  visibleIndexes.clear();
  const bool showCompleted = selectedTab == FilterTab::Completed;

  for (int i = 0; i < static_cast<int>(achievements.size()); ++i) {
    if (achievements[i].state.unlocked == showCompleted) {
      visibleIndexes.push_back(i);
    }
  }

  if (visibleIndexes.empty() && !achievements.empty()) {
    selectedTab = showCompleted ? FilterTab::Pending : FilterTab::Completed;
    const bool fallbackCompleted = selectedTab == FilterTab::Completed;
    for (int i = 0; i < static_cast<int>(achievements.size()); ++i) {
      if (achievements[i].state.unlocked == fallbackCompleted) {
        visibleIndexes.push_back(i);
      }
    }
  }

  int pendingCount = 0;
  int completedCount = 0;
  for (const auto& achievement : achievements) {
    if (achievement.state.unlocked) {
      ++completedCount;
    } else {
      ++pendingCount;
    }
  }
  tabLabels[0] = tabLabelWithCount(false, pendingCount);
  tabLabels[1] = tabLabelWithCount(true, completedCount);

  rebuildRowItems();
}

// Derives the row cache from visibleIndexes. Called from
// rebuildVisibleIndexes(), never from buildScreen().
void AchievementsActivity::rebuildRowItems() {
  rowTexts.clear();
  rowItems.clear();
  rowTexts.reserve(visibleIndexes.size());
  rowItems.reserve(visibleIndexes.size());
  for (const int index : visibleIndexes) {
    const auto& entry = achievements[static_cast<size_t>(index)];
    RowText text;
    text.title = ACHIEVEMENTS.getTitle(entry.definition->id);
    text.description = ACHIEVEMENTS.getDescription(entry.definition->id);
    text.progress = getProgressLabel(entry);
    rowTexts.push_back(std::move(text));
  }
  for (size_t i = 0; i < rowTexts.size(); ++i) {
    fui::ListItem item;
    item.label = rowTexts[i].title.c_str();
    item.subtitle = rowTexts[i].description.c_str();
    item.value = rowTexts[i].progress.c_str();
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
}

void AchievementsActivity::refreshEntries() {
  ACHIEVEMENTS.reconcileFromCurrentStats();
  achievements = ACHIEVEMENTS.buildViews();
  rebuildVisibleIndexes();
}

void AchievementsActivity::onEnter() {
  selectedTab = FilterTab::Pending;  // before the base resets activeNav(), which indexes by tab
  UiTabListActivity::onEnter();
  renderer.requestNextRefresh(HalDisplay::HALF_REFRESH);
  refreshEntries();
  // Legacy screen opened with the first row highlighted (ring 1); the tab band
  // (ring 0) is still reachable by Confirm / tap.
  activeNav().selected = visibleIndexes.empty() ? 0 : 1;
  activeNav().top = 0;
}

void AchievementsActivity::onExit() {
  renderer.requestNextRefresh(HalDisplay::HALF_REFRESH);
  Activity::onExit();
}

void AchievementsActivity::switchTab(const FilterTab tab) {
  RenderLock lock(*this);
  selectedTab = tab;
  rebuildVisibleIndexes();  // may fall back to the other tab when this one is empty
  activeNav().top = 0;
  activeNav().selected = visibleIndexes.empty() ? 0 : 1;
}

void AchievementsActivity::onTabAction(const int index) {
  switchTab(static_cast<FilterTab>(index));
  // The switched-to tab repaints as the selected pill; a flash overlay on top
  // of it just repaints the pill in the focused style.
  app.clearTapFlash();
  requestUpdate();
}

void AchievementsActivity::stepTab(int /*direction*/) {
  switchTab(selectedTab == FilterTab::Pending ? FilterTab::Completed : FilterTab::Pending);
  requestUpdate();
}

bool AchievementsActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return true;
  }

  // Confirm always toggles the tab (legacy semantics), wherever the ring is.
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    stepTab(1);
    return true;
  }
  return false;
}

void AchievementsActivity::navigateButtons() {
  const int count = listCount();
  if (count <= 0) {
    return;
  }
  // Ring 1..count with wrap (rows only), press + hold-repeat like the legacy
  // screen; a ring at 0 (tab band) steps onto the first/last row.
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right, MappedInputManager::Button::Down},
                                       [this, count] {
                                         const int row = ringPos() - 1;
                                         moveRingTo(row < 0 ? 1 : ButtonNavigator::nextIndex(row, count) + 1);
                                       });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left, MappedInputManager::Button::Up},
                                       [this, count] {
                                         const int row = ringPos() - 1;
                                         moveRingTo(row < 0 ? count : ButtonNavigator::previousIndex(row, count) + 1);
                                       });
}

void AchievementsActivity::drawChrome() { HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_ACHIEVEMENTS)); }

void AchievementsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the header band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});

  buildTabBar(screen);

  if (visibleIndexes.empty()) {
    screen.centeredText(emptyStateLabel(selectedTab == FilterTab::Completed), screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  fui::TextStyle label = screen.theme().smallText;
  label.bold = true;
  props.labelText = label;
  syncTabListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}

void AchievementsActivity::drawFooter() {
  const char* nextTabLabel = tabLabelFor(selectedTab == FilterTab::Pending);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), nextTabLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
