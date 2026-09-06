#pragma once

#include <FreeInkUICore.h>

#include <cstdint>
#include <string>

#include "../Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

struct ReadingStatsDetailContext {
  bool showSessionSummary = false;
};

// Per-book reading stats page. The page stays hand-drawn (with its frame
// buffer cache); the FreeInkApp only registers touch targets over it: the
// cover/title band opens the book, the gear button opens the stats actions.
// Swipe up/down scrolls. Buttons are unchanged.
class ReadingStatsDetailActivity final : public Activity, private UiAppHost {
  ButtonNavigator buttonNavigator;
  std::string bookPath;
  std::string resolvedCoverBmpPath;
  ReadingStatsDetailContext context;
  bool coverLoadPending = false;
  int selectedStatsItem = 0;
  bool waitForConfirmRelease = false;
  bool waitForBackRelease = false;
  bool baseScreenBufferStored = false;
  uint8_t* baseScreenBuffer = nullptr;
  std::string baseScreenBookPath;
  std::string baseScreenCoverPath;
  int baseScreenScrollOffset = -1;
  int scrollOffset = 0;
  int maxScrollOffset = 0;
  // Touch targets measured by render() (they follow the scroll offset) and
  // registered by buildDetailScreen() on the same pass. Empty when no book.
  freeink::ui::Rect hitOpenRect{};
  freeink::ui::Rect hitActionsRect{};

  static void detailScreen(UiScreen& screen, void* user);
  static void onOpenEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onActionsEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildDetailScreen(UiScreen& screen);
  // Clamp-scroll the page by delta and drop the cached base frame.
  bool scrollContent(int delta);

  void openStatsActions();
  void guardChildReturn();
  bool storeBaseScreenBuffer();
  bool restoreBaseScreenBuffer();
  void invalidateBaseScreenBuffer();
  void freeBaseScreenBuffer();

 public:
  explicit ReadingStatsDetailActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                                      ReadingStatsDetailContext context = {})
      : Activity("ReadingStatsDetail", renderer, mappedInput),
        UiAppHost(renderer),
        bookPath(std::move(bookPath)),
        context(std::move(context)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  uint8_t getUiTransitionRefreshWeight() const override { return UI_TRANSITION_REFRESH_WEIGHT_DENSE; }
};
