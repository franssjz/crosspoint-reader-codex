#pragma once

#include <string>
#include <vector>

#include "FlashcardsStore.h"
#include "activities/UiListActivity.h"

// Decks with recorded statistics. Activate opens the deck's stats page; a
// Confirm hold or touch long-press resets that deck's statistics.
class FlashcardStatsActivity final : public UiListActivity {
  std::vector<FlashcardDeckRecord> decks;
  std::vector<std::string> rowSubtitles;
  std::vector<freeink::ui::ListItem> rowItems;

  void reloadDecks();
  void rebuildRowItems();
  void confirmResetDeck(int index);

  int listCount() const override { return static_cast<int>(decks.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleButtons() override;
  void drawChrome() override;
  void drawFooter() override;

 public:
  explicit FlashcardStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("FlashcardStats", renderer, mappedInput, /*wantsTouchLongPress=*/true) {}

  void onEnter() override;
  void onExit() override;
};
