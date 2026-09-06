#pragma once

#include <vector>

#include "activities/UiListActivity.h"

// Time zone preset picker: one FreeInkUI list row per registry preset, the
// active preset marked "Selected". Activating a row saves it and finishes.
class TimeZoneSelectActivity final : public UiListActivity {
 public:
  explicit TimeZoneSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("TimeZoneSelect", renderer, mappedInput) {}

  void onEnter() override;

 private:
  int listCount() const override { return static_cast<int>(rowItems.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  // Row cache: labels are static registry strings, built once in onEnter()
  // (activateIndex() finishes immediately, so the "Selected" marker cannot go
  // stale within one visit).
  std::vector<freeink::ui::ListItem> rowItems;
};
