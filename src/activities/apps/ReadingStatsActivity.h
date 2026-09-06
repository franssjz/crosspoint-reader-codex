#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// Reading statistics overview: summary metric cards (header chrome) above a
// list whose first row opens the extended stats and whose remaining rows are
// the started books (activate = detail page, Confirm hold / long-press =
// remove the book's stats).
class ReadingStatsActivity final : public UiListActivity {
  bool waitForConfirmRelease = false;
  bool waitForBackRelease = false;
  int bookCount = 0;
  // Row caches: row 0 is "More details", rows 1..N mirror READING_STATS.getBooks().
  std::vector<std::string> rowValues;
  std::vector<freeink::ui::ListItem> rowItems;

  void rebuildRows();
  void openEntry(int index);
  void confirmRemoveBook(int index);
  void guardBackReturn();
  void showTransientPopup(const char* message, int progress = -1, unsigned long delayMs = 0);
  void createDueAutoBackupWithFeedback();
  int listTop() const;

  int listCount() const override { return static_cast<int>(rowItems.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  void drawChrome() override;

 public:
  explicit ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("ReadingStats", renderer, mappedInput, /*wantsTouchLongPress=*/true) {
    chromeNeedsListLayout = true;  // sub-header shows "<page>/<pages>"
  }

  void onEnter() override;
  void onExit() override;
  uint8_t getUiTransitionRefreshWeight() const override { return UI_TRANSITION_REFRESH_WEIGHT_DENSE; }
};
