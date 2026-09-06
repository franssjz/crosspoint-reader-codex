#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

/**
 * Activity showing the list of configured KOReader sync profiles.
 * Allows adding new profiles and editing/deleting/activating existing ones.
 * Selecting a profile opens KOReaderProfileEditActivity, which is where a
 * profile is actually made the active one (KOReaderSettingsActivity reads
 * whichever profile is active).
 */
class KOReaderProfileListActivity final : public UiListActivity {
 public:
  explicit KOReaderProfileListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("KOReaderProfileList", renderer, mappedInput) {}

  void onEnter() override;

 private:
  int listCount() const override { return static_cast<int>(rowItems.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  // Row cache: profile name/username copies (the store reloads from disk after
  // every edit, so rows never alias its strings) plus the trailing
  // "Add Profile" row. Rebuilt by rebuildRowItems() after each store reload.
  std::vector<std::string> rowLabels;
  std::vector<std::string> rowSubtitles;
  std::vector<freeink::ui::ListItem> rowItems;
  int profileCount = 0;

  void rebuildRowItems();
};
