#pragma once

#include <string>
#include <vector>

#include "FavoritesStore.h"
#include "activities/UiListActivity.h"

// Reorder / remove favorites. Buttons: Confirm toggles "move mode" (Up/Down
// then move the picked entry), a Confirm hold removes it. Touch: tap picks an
// entry (move mode), tapping another row swaps the picked entry into that
// slot, tapping it again drops it; long-press removes.
class FavoritesOrderActivity final : public UiListActivity {
 public:
  FavoritesOrderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("FavoritesOrder", renderer, mappedInput, /*wantsTouchLongPress=*/true) {}

  void onEnter() override;
  void onExit() override;

 private:
  std::vector<FavoriteBook> entries;
  // Row caches derived from entries (titles fall back to the file name).
  std::vector<std::string> rowTitles;
  std::vector<freeink::ui::ListItem> rowItems;
  bool moveMode = false;

  int listCount() const override { return static_cast<int>(entries.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  // Confirm toggles move mode (hold = remove); Back leaves move mode first.
  bool handleButtons() override;
  // In move mode Up/Down move the entry instead of the selection.
  void navigateButtons() override;
  void drawChrome() override;
  void drawFooter() override;

  void reloadEntries();
  void rebuildRowItems();
  void setMoveMode(bool enabled);
  void moveSelectedEntry(int delta);
  void confirmDeleteEntry(int index);
};
