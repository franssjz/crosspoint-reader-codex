#pragma once
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
class KeyboardLayoutsActivity final : public Activity {
  uint8_t enabled = 1;
  int selected = 0;
  ButtonNavigator navigator;

 public:
  KeyboardLayoutsActivity(GfxRenderer& renderer, MappedInputManager& input)
      : Activity("KeyboardLayouts", renderer, input) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
