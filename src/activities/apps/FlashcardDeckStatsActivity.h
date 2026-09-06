#pragma once

#include <string>
#include <vector>

#include "FlashcardsStore.h"
#include "activities/UiListActivity.h"

// Per-deck statistics: a grid of metric cards (header chrome) above a single
// "Open" row that starts a review session for the deck.
class FlashcardDeckStatsActivity final : public UiListActivity {
  std::string deckPath;
  FlashcardDeck deck;
  std::vector<FlashcardCardProgress> progress;
  FlashcardDeckMetrics metrics;
  bool loaded = false;
  std::string errorMessage;
  freeink::ui::ListItem openRow{};

  void loadDeckData();
  int cardsBottom() const;

  int listCount() const override { return loaded ? 1 : 0; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void drawChrome() override;
  void drawFooter() override;

 public:
  FlashcardDeckStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string deckPath)
      : UiListActivity("FlashcardDeckStats", renderer, mappedInput), deckPath(std::move(deckPath)) {}

  void onEnter() override;
};
