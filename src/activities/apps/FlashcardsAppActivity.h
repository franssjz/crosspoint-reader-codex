#pragma once

#include <string>

#include "activities/UiListActivity.h"

// Flashcards hub: four fixed rows (open a deck, recents, statistics,
// settings) whose subtitles reflect live counts / settings.
class FlashcardsAppActivity final : public UiListActivity {
  static constexpr int ACTION_COUNT = 4;

  int recentCount = 0;
  int deckCount = 0;
  // Row storage: labels/icons are static, subtitles are refreshed into these
  // strings by refreshCounts() (no per-render allocation).
  std::string rowSubtitles[ACTION_COUNT];
  freeink::ui::ListItem rowItems[ACTION_COUNT]{};

  void refreshCounts();

  int listCount() const override { return ACTION_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void drawChrome() override;

 public:
  explicit FlashcardsAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("FlashcardsApp", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
};
