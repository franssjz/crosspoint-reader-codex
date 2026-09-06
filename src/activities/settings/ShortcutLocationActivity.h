#pragma once

#include <vector>

#include "activities/UiListActivity.h"
#include "util/ShortcutRegistry.h"

// Per-shortcut Home/Apps placement: one row per shortcut, the value column
// shows the current location and activating a row (tap or Confirm) toggles it
// in place.
class ShortcutLocationActivity final : public UiListActivity {
 public:
  explicit ShortcutLocationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("ShortcutLocation", renderer, mappedInput) {}

  void onEnter() override;

 private:
  int listCount() const override { return static_cast<int>(entries.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void drawFooter() override;

  std::vector<const ShortcutDefinition*> entries;
  // Row cache (labels alias the registry's metadata strings, values are
  // static tr() strings); rebuilt in reloadEntries(), values refreshed by
  // activateIndex().
  std::vector<std::string> rowLabels;
  std::vector<freeink::ui::ListItem> rowItems;

  void reloadEntries();
  void rebuildRowItems();
};
