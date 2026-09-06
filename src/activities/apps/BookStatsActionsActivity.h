#pragma once

#include <string>

#include "activities/UiListActivity.h"

// Per-book stats actions: adjust reading time, modify the start date, reset
// the book's statistics. Three rows; a failed start-date change shows a hint.
class BookStatsActionsActivity final : public UiListActivity {
  static constexpr int ACTION_COUNT = 3;

  std::string bookPath;
  std::string bookTitle;
  bool waitForConfirmRelease = false;
  bool startDateApplyFailed = false;
  freeink::ui::ListItem rowItems[ACTION_COUNT]{};

  void openAdjustment();
  void openStartDateSelection();
  void confirmResetBookStats();
  void guardConfirmReturn();

  int listCount() const override { return ACTION_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // Swallows the Confirm release that opened this screen / a sub-screen.
  bool handleCustomInput() override;
  void drawChrome() override;

 public:
  static constexpr int RESULT_RESET_BOOK_STATS = 1;

  explicit BookStatsActionsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                                    std::string bookTitle)
      : UiListActivity("BookStatsActions", renderer, mappedInput),
        bookPath(std::move(bookPath)),
        bookTitle(std::move(bookTitle)) {}

  void onEnter() override;
  uint8_t getUiTransitionRefreshWeight() const override { return UI_TRANSITION_REFRESH_WEIGHT_DENSE; }
};
