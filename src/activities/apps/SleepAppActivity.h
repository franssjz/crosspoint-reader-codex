#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// Sleep-image app: a sleep-order row (activate cycles shuffle/sequential)
// followed by one row per sleep-image directory (activate opens its preview).
class SleepAppActivity final : public UiListActivity {
  std::vector<std::string> directories;
  std::vector<std::string> rowLabels;
  std::vector<freeink::ui::ListItem> rowItems;

  void loadDirectories();
  void rebuildRows();
  void openDirectory(int index);

  int listCount() const override { return static_cast<int>(rowItems.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void drawChrome() override;
  void drawFooter() override;

 public:
  explicit SleepAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("SleepApp", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
};
