#pragma once

#include <string>
#include <vector>

#include "FlashcardsStore.h"
#include "activities/UiListActivity.h"

// Recently opened decks. Activate opens a review session; a Confirm hold or
// touch long-press removes the deck from the recents list.
class FlashcardRecentsActivity final : public UiListActivity {
  std::vector<FlashcardDeckRecord> decks;
  // Row caches derived from decks (progress | accuracy subtitles).
  std::vector<std::string> rowSubtitles;
  std::vector<freeink::ui::ListItem> rowItems;

  void reloadDecks();
  void rebuildRowItems();
  void openDeck(int index);
  void confirmRemoveDeck(int index);

  int listCount() const override { return static_cast<int>(decks.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  // Confirm activates on release; a hold prompts removal.
  bool handleButtons() override;
  void drawChrome() override;
  void drawFooter() override;

 public:
  explicit FlashcardRecentsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("FlashcardRecents", renderer, mappedInput, /*wantsTouchLongPress=*/true) {}

  void onEnter() override;
  void onExit() override;
};
