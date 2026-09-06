#pragma once

#include <FreeInkUICore.h>

#include "../Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

// Monthly reading calendar. The page stays hand-drawn; the FreeInkApp only
// registers touch targets over it: the month header (tap left/right half =
// previous/next month), and the day grid (tap a day = select + open it).
// Swipe left/right also pages months. Buttons are unchanged.
class ReadingHeatmapActivity final : public Activity, private UiAppHost {
  ButtonNavigator buttonNavigator;
  int viewedYear = 0;
  unsigned viewedMonth = 0;
  uint32_t selectedDayOrdinal = 0;
  bool waitForConfirmRelease = false;

  static void heatmapScreen(UiScreen& screen, void* user);
  static void onMonthEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildHeatmapScreen(UiScreen& screen);
  void handleGridTap(int x, int y);
  void openSelectedDay();

  void goToAdjacentMonth(int delta);
  void goToReferenceMonth();
  void resetSelectedDay();
  void moveSelection(int delta);

 public:
  explicit ReadingHeatmapActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadingHeatmap", renderer, mappedInput), UiAppHost(renderer) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  uint8_t getUiTransitionRefreshWeight() const override { return UI_TRANSITION_REFRESH_WEIGHT_DENSE; }
};
