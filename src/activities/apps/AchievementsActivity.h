#pragma once

#include <string>
#include <vector>

#include "AchievementsStore.h"
#include "activities/UiTabListActivity.h"

// Achievements browser: Pending / Completed tabs over a FreeInkUI list. Rows
// are informational (title, description, progress); Confirm toggles the tab
// like the legacy screen, tabs are also tappable.
class AchievementsActivity final : public UiTabListActivity {
  enum class FilterTab : uint8_t { Pending = 0, Completed };

  FilterTab selectedTab = FilterTab::Pending;
  std::vector<AchievementView> achievements;
  std::vector<int> visibleIndexes;

  // Row cache for the visible achievements (title/description/progress are
  // built strings) and the "Pending (n)" / "Completed (n)" tab labels.
  // Rebuilt by rebuildVisibleIndexes(), never from buildScreen().
  struct RowText {
    std::string title;
    std::string description;
    std::string progress;
  };
  std::vector<RowText> rowTexts;
  std::vector<freeink::ui::ListItem> rowItems;
  std::string tabLabels[2];

  void refreshEntries();
  void rebuildVisibleIndexes();
  void rebuildRowItems();
  void switchTab(FilterTab tab);

  // --- UiTabListActivity contract ---
  int listCount() const override { return static_cast<int>(visibleIndexes.size()); }
  int tabCount() const override { return 2; }
  int activeTab() const override { return static_cast<int>(selectedTab); }
  const char* tabLabel(int index) const override { return tabLabels[index].c_str(); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int /*index*/) override {}  // rows are informational
  void onTabAction(int index) override;
  void stepTab(int direction) override;
  bool handleButtons() override;
  // Rows only (wrapping), as the legacy screen: the tab band is reached by
  // Confirm / tap, not by walking past the first row.
  void navigateButtons() override;
  void drawChrome() override;
  void drawFooter() override;

 public:
  explicit AchievementsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiTabListActivity("Achievements", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  uint8_t getUiTransitionRefreshWeight() const override { return UI_TRANSITION_REFRESH_WEIGHT_DENSE; }
};
