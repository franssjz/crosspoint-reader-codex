#pragma once

#include <vector>

#include "activities/UiListActivity.h"
#include "util/ShortcutRegistry.h"

// Per-shortcut Show/Hidden switch: one row per hideable shortcut, the value
// column shows the current state and activating a row (tap or Confirm)
// toggles it in place.
class ShortcutVisibilityActivity final : public UiListActivity {
 public:
  explicit ShortcutVisibilityActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("ShortcutVisibility", renderer, mappedInput) {}

  void onEnter() override;

 private:
  int listCount() const override { return static_cast<int>(entries.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void drawFooter() override;

  std::vector<const ShortcutDefinition*> entries;
  // Row cache; rebuilt in reloadEntries(), values refreshed by activateIndex().
  std::vector<std::string> rowLabels;
  std::vector<freeink::ui::ListItem> rowItems;

  void reloadEntries();
  void rebuildRowItems();
};
