#pragma once

#include "activities/UiListActivity.h"

// Favorites hub: two fixed rows (browse files to toggle favorites, reorder
// the favorites list) with the favorite count in the header.
class FavoritesAppActivity final : public UiListActivity {
  static constexpr int ACTION_COUNT = 2;

  int favoriteCount = 0;
  // Fixed-capacity row storage; labels are static, built once in onEnter().
  freeink::ui::ListItem rowItems[ACTION_COUNT]{};

  void refreshEntries();

  int listCount() const override { return ACTION_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void drawChrome() override;

 public:
  explicit FavoritesAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("FavoritesApp", renderer, mappedInput) {}

  void onEnter() override;
};
