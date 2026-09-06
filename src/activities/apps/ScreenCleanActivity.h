#pragma once

#include <cstdint>

#include "activities/UiListActivity.h"

// Two-row picker (Quick / Deep clean) that runs a full-refresh pattern cycle.
// While cleaning, Back or a tap anywhere cancels the cycle.
class ScreenCleanActivity final : public UiListActivity {
  enum class Mode : uint8_t { Quick, Deep };
  enum class Pattern : uint8_t { White, Black, LightGray, DarkGray, Checker, InverseChecker };

  static constexpr int ACTION_COUNT = 2;

  bool cleaning = false;
  bool completed = false;
  bool darkModeSaved = false;
  bool savedDarkMode = false;
  Mode mode = Mode::Quick;
  uint8_t stageIndex = 0;
  unsigned long lastStageRenderedAt = 0;
  // Row activated by tap/Confirm; the cycle starts on the next loop pass so
  // its blocking requestUpdateAndWait() never runs inside the app's dispatch.
  int pendingStart = -1;
  // Fixed two-row cache (static tr() strings), built in onEnter().
  freeink::ui::ListItem rowItems[ACTION_COUNT]{};

  void startCleaning(Mode cleanMode);
  void finishCleaning(bool markCompleted);
  void restoreDarkMode();
  int stageCount() const;
  Pattern patternForStage(uint8_t index) const;
  void drawPattern(Pattern pattern) const;

  int listCount() const override { return ACTION_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // The cleaning cycle owns input while it runs (stage timing, cancel).
  bool handleCustomInput() override;
  // Moving the selection clears the "complete" popup like the legacy screen.
  void navigateButtons() override;
  void drawChrome() override;

 public:
  explicit ScreenCleanActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("ScreenClean", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return cleaning; }
  uint8_t getUiTransitionRefreshWeight() const override { return UI_TRANSITION_REFRESH_WEIGHT_DENSE; }
};
