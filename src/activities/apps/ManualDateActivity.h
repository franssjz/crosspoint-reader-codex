#pragma once

#include <string>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

// Manually set the device date: day / month / year stepper fields. Buttons:
// Up/Down pick the field, Left/Right step it, Confirm saves, Back cancels.
// Touch: tap a row to pick it, [-]/[+] step, OK saves, Cancel closes.
class ManualDateActivity final : public Activity, private UiAppHost {
  ButtonNavigator buttonNavigator;
  int selectedField = 0;
  int year = 2026;
  unsigned month = 6;
  unsigned day = 15;
  // Row value text (refreshed on the render task in buildScreen()).
  char dayText[4] = {};
  char monthText[4] = {};
  char yearText[8] = {};

  static void screenTrampoline(UiScreen& screen, void* user);
  static void onFieldEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onStepEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onCancelEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onOkEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildScreen(UiScreen& screen);

  void selectField(int field);
  void adjustField(int field, int delta);
  void saveDate();
  std::string getSelectedDateLabel() const;

 public:
  explicit ManualDateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ManualDate", renderer, mappedInput), UiAppHost(renderer) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
