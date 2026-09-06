#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// Picker for a reading-stats export/backup file under /exports. Activating a
// row returns its path as a FilePathResult; Back returns a cancelled result.
class ReadingStatsImportActivity final : public UiListActivity {
  std::vector<std::string> importPaths;
  // Row cache: display names (file name part of each path) + rows, built when
  // importPaths is loaded in onEnter().
  std::vector<std::string> rowNames;
  std::vector<freeink::ui::ListItem> rowItems;

  void rebuildRowItems();

  int listCount() const override { return static_cast<int>(importPaths.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  void drawChrome() override;
  void drawFooter() override;

 public:
  explicit ReadingStatsImportActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("ReadingStatsImport", renderer, mappedInput) {}

  static std::vector<std::string> getImportPaths();

  void onEnter() override;
  uint8_t getUiTransitionRefreshWeight() const override { return UI_TRANSITION_REFRESH_WEIGHT_DENSE; }
};
