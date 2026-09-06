#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "util/ReadingStatsAnalytics.h"

// One reading day: summary cards + top book (drawn as chrome above the list)
// and the FreeInkUI list of books read that day. Activating a row opens the
// book's stats detail.
class ReadingDayDetailActivity final : public UiListActivity {
  uint32_t dayOrdinal = 0;
  std::vector<ReadingStatsAnalytics::DayBookEntry> entries;
  // Row cache: title/author/duration copies + rows, rebuilt by
  // rebuildRowItems() whenever entries is refreshed.
  std::vector<std::string> rowTitles;
  std::vector<std::string> rowSubtitles;
  std::vector<std::string> rowValues;
  std::vector<freeink::ui::ListItem> rowItems;
  // Top of the list band, measured by drawChrome() (cards + sub-header) on the
  // same render pass buildScreen() reads it.
  int listTop = 0;

  void refreshEntries();
  void rebuildRowItems();

  int listCount() const override { return static_cast<int>(entries.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void drawChrome() override;
  void drawFooter() override;

 public:
  explicit ReadingDayDetailActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, uint32_t dayOrdinal)
      : UiListActivity("ReadingDayDetail", renderer, mappedInput), dayOrdinal(dayOrdinal) {}

  void onEnter() override;
};
