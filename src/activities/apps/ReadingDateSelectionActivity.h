#pragma once

#include <string>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

// Pick a calendar day (returned as a PageResult day ordinal): day / month /
// year stepper fields. Buttons: Up/Down pick the field, Left/Right step it,
// Confirm returns the date, Back cancels. Touch: tap a row to pick it,
// [-]/[+] step, OK returns, Cancel closes.
class ReadingDateSelectionActivity final : public Activity, private UiAppHost {
  ButtonNavigator buttonNavigator;
  uint32_t initialDayOrdinal = 0;
  int selectedField = 0;
  int year = 2026;
  unsigned month = 6;
  unsigned day = 15;
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
  uint32_t getSelectedDayOrdinal() const;
  std::string getSelectedDateLabel() const;
  void finishWithDate();
  void cancel();

 public:
  explicit ReadingDateSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                        uint32_t initialDayOrdinal)
      : Activity("ReadingDateSelection", renderer, mappedInput),
        UiAppHost(renderer),
        initialDayOrdinal(initialDayOrdinal) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
