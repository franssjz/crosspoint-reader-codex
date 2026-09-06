#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "util/ShortcutRegistry.h"

// Reorder the Home or Apps shortcuts. Buttons: Confirm toggles move mode, in
// which Up/Down move the selected entry instead of the selection. Touch: a
// tap picks a row up (move mode), tapping another row drops it at that
// position, tapping the picked row again puts it down.
class ShortcutOrderActivity final : public UiListActivity {
 public:
  ShortcutOrderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, ShortcutOrderGroup group)
      : UiListActivity("ShortcutOrder", renderer, mappedInput), group(group) {}

  void onEnter() override;

 private:
  int listCount() const override { return static_cast<int>(entries.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // Confirm toggles move mode; Back leaves move mode before it leaves the screen.
  bool handleButtons() override;
  // Move mode redirects Up/Down to moving the entry.
  void navigateButtons() override;
  const char* headerTitle() const override;
  void drawFooter() override;

  ShortcutOrderGroup group;
  std::vector<ShortcutOrderEntry> entries;
  bool moveMode = false;
  // Row the picked-up entry currently sits on while moveMode is set. Button
  // moves keep it equal to the selection; a touch drop tap moves the selection
  // to the target row first (onRowAction), so it is tracked separately.
  int pickedIndex = 0;
  // Row cache: titles are copies (getEntryTitle builds strings), rebuilt only
  // when entries changes; a move swaps the two affected titles in place.
  std::vector<std::string> rowTitles;
  std::vector<freeink::ui::ListItem> rowItems;

  void reloadEntries();
  void rebuildRowItems();
  // Swap the selected entry with its neighbour `delta` rows away (persisting
  // the order) and follow it.
  void moveSelectedEntry(int delta);
  // Walk the selected entry to targetIndex one swap at a time.
  void moveSelectedEntryTo(int targetIndex);
};
