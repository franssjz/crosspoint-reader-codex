#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// Dictionary manager: two setting rows (definition text size, clear history)
// followed by one row per installed dictionary; activating a dictionary makes
// it the active one.
class DictionaryActivity final : public UiListActivity {
 public:
  explicit DictionaryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("Dictionary", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;

 private:
  int entryCount = 0;
  std::vector<std::string> rowValues;
  std::vector<freeink::ui::ListItem> rowItems;

  void rebuildRows();
  void selectIndex(int index);

  int listCount() const override { return static_cast<int>(rowItems.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void drawChrome() override;
};
