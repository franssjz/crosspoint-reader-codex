#pragma once

#include <cstdint>
#include <string>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

// Add or subtract reading time for one book on one day: Action / Date /
// Amount stepper fields plus a preview line. Buttons: Up/Down pick the field,
// Left/Right step it, Confirm applies (on the Date field: opens the date
// picker), Back cancels. Touch: tap a row to pick it (Date row opens the
// picker), [-]/[+] step, OK applies, Cancel closes.
class BookReadingAdjustmentActivity final : public Activity, private UiAppHost {
  ButtonNavigator buttonNavigator;
  std::string bookPath;
  std::string bookTitle;
  int selectedField = 0;
  int selectedOperation = 0;
  int selectedDuration = 1;
  uint32_t selectedDayOrdinal = 0;
  bool lastApplyFailed = false;
  // Row / hint text storage, refreshed on the render task in buildScreen().
  std::string dateLabel;
  std::string previewInfo;
  char durationText[12] = {};

  static void screenTrampoline(UiScreen& screen, void* user);
  static void onFieldEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onStepEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onCancelEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onOkEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildScreen(UiScreen& screen);

  void selectField(int field);
  void adjustValue(int field, int delta);
  void openDateSelection();
  void initializeSelectedDate();
  int32_t getSelectedDeltaMs() const;
  uint64_t getSelectedDayReadingMs() const;
  bool canApplySelectedAdjustment() const;
  std::string getAdjustmentPreviewInfo() const;
  const char* getOperationLabel() const;
  std::string getDateLabel() const;
  bool applyAdjustment();

 public:
  explicit BookReadingAdjustmentActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                                         std::string bookTitle)
      : Activity("BookReadingAdjustment", renderer, mappedInput),
        UiAppHost(renderer),
        bookPath(std::move(bookPath)),
        bookTitle(std::move(bookTitle)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
