#include "ReadingDateSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>

#include "ReadingStatsStore.h"
#include "StepperFieldsUi.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/TimeUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr int FIELD_COUNT = 3;
constexpr int FIELD_DAY = 0;
constexpr int FIELD_MONTH = 1;
constexpr int FIELD_YEAR = 2;
constexpr int MIN_DAY = 1;
constexpr int MIN_MONTH = 1;
constexpr int MAX_MONTH = 12;
constexpr int MIN_YEAR = 2024;
constexpr int MAX_YEAR = 2099;

constexpr fui::ActionId ACTION_FIELD = 1;
constexpr fui::ActionId ACTION_DEC = 2;
constexpr fui::ActionId ACTION_INC = 3;
constexpr fui::ActionId ACTION_CANCEL = 4;
constexpr fui::ActionId ACTION_OK = 5;

bool isLeapYear(const int year) { return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0); }

unsigned getDaysInMonth(const int year, const unsigned month) {
  static constexpr unsigned DAYS_PER_MONTH[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2) {
    return isLeapYear(year) ? 29U : 28U;
  }
  if (month < 1 || month > 12) {
    return 31;
  }
  return DAYS_PER_MONTH[month - 1];
}

unsigned wrapValue(const unsigned value, const int delta, const unsigned minValue, const unsigned maxValue) {
  const int range = static_cast<int>(maxValue - minValue + 1);
  int offset = static_cast<int>(value - minValue) + delta;
  offset %= range;
  if (offset < 0) {
    offset += range;
  }
  return minValue + static_cast<unsigned>(offset);
}
}  // namespace

void ReadingDateSelectionActivity::onEnter() {
  Activity::onEnter();

  bool initialized = false;
  if (initialDayOrdinal != 0) {
    initialized = TimeUtils::getDateFromDayOrdinal(initialDayOrdinal, year, month, day);
  }

  if (!initialized) {
    bool usedFallback = false;
    const uint32_t referenceTimestamp = READING_STATS.getDisplayTimestamp(&usedFallback);
    if (TimeUtils::isClockValid(referenceTimestamp)) {
      time_t currentTime = static_cast<time_t>(referenceTimestamp);
      tm localTime = {};
      if (localtime_r(&currentTime, &localTime) != nullptr) {
        year = std::clamp(localTime.tm_year + 1900, MIN_YEAR, MAX_YEAR);
        month = static_cast<unsigned>(std::clamp(localTime.tm_mon + 1, MIN_MONTH, MAX_MONTH));
        day = static_cast<unsigned>(
            std::clamp(localTime.tm_mday, MIN_DAY, static_cast<int>(getDaysInMonth(year, month))));
      }
    }
  }

  selectedField = 0;
  resetUi();
  app.on(ACTION_FIELD, &ReadingDateSelectionActivity::onFieldEvent, this);
  app.on(ACTION_DEC, &ReadingDateSelectionActivity::onStepEvent, this);
  app.on(ACTION_INC, &ReadingDateSelectionActivity::onStepEvent, this);
  app.on(ACTION_CANCEL, &ReadingDateSelectionActivity::onCancelEvent, this);
  app.on(ACTION_OK, &ReadingDateSelectionActivity::onOkEvent, this);
  app.setScreen(&ReadingDateSelectionActivity::screenTrampoline, this);
  requestUpdate();
}

void ReadingDateSelectionActivity::selectField(const int field) {
  if (field < 0 || field >= FIELD_COUNT) return;
  selectedField = field;
  requestUpdate();
}

void ReadingDateSelectionActivity::adjustField(const int field, const int delta) {
  if (field == FIELD_DAY) {
    day = wrapValue(day, delta, MIN_DAY, getDaysInMonth(year, month));
  } else if (field == FIELD_MONTH) {
    month = wrapValue(month, delta, MIN_MONTH, MAX_MONTH);
    day = std::min(day, getDaysInMonth(year, month));
  } else {
    year = std::clamp(year + delta, MIN_YEAR, MAX_YEAR);
    day = std::min(day, getDaysInMonth(year, month));
  }
  requestUpdate();
}

uint32_t ReadingDateSelectionActivity::getSelectedDayOrdinal() const {
  return TimeUtils::getDayOrdinalForDate(year, month, day);
}

std::string ReadingDateSelectionActivity::getSelectedDateLabel() const {
  return TimeUtils::formatDateParts(year, month, day);
}

void ReadingDateSelectionActivity::finishWithDate() {
  setResult(ActivityResult{PageResult{getSelectedDayOrdinal()}});
  finish();
}

void ReadingDateSelectionActivity::cancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void ReadingDateSelectionActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<ReadingDateSelectionActivity*>(user)->buildScreen(screen);
}

void ReadingDateSelectionActivity::onFieldEvent(const fui::ActionEvent& event, void* user) {
  static_cast<ReadingDateSelectionActivity*>(user)->selectField(event.value);
}

void ReadingDateSelectionActivity::onStepEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<ReadingDateSelectionActivity*>(user);
  self->selectedField = std::clamp(static_cast<int>(event.value), 0, FIELD_COUNT - 1);
  self->adjustField(event.value, event.action == ACTION_INC ? 1 : -1);
}

void ReadingDateSelectionActivity::onCancelEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<ReadingDateSelectionActivity*>(user);
  self->app.clearTapFlash();  // the tap leaves this screen
  self->cancel();
}

void ReadingDateSelectionActivity::onOkEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<ReadingDateSelectionActivity*>(user);
  self->app.clearTapFlash();
  self->finishWithDate();
}

void ReadingDateSelectionActivity::loop() {
  const auto route = routeTouch(mappedInput);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) return;

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    cancel();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    finishWithDate();
    return;
  }

  buttonNavigator.onRelease({MappedInputManager::Button::Down},
                            [this] { selectField(ButtonNavigator::nextIndex(selectedField, FIELD_COUNT)); });
  buttonNavigator.onRelease({MappedInputManager::Button::Up},
                            [this] { selectField(ButtonNavigator::previousIndex(selectedField, FIELD_COUNT)); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { adjustField(selectedField, -1); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { adjustField(selectedField, 1); });
}

void ReadingDateSelectionActivity::buildScreen(UiScreen& screen) {
  snprintf(dayText, sizeof(dayText), "%02u", day);
  snprintf(monthText, sizeof(monthText), "%02u", month);
  snprintf(yearText, sizeof(yearText), "%d", year);

  const StepperField fields[FIELD_COUNT] = {
      {tr(STR_DAY), dayText, "00"},
      {tr(STR_MONTH), monthText, "00"},
      {tr(STR_YEAR), yearText, "0000"},
  };
  StepperFieldsSpec spec;
  spec.fields = fields;
  spec.count = FIELD_COUNT;
  spec.selectedField = selectedField;
  spec.fieldAction = ACTION_FIELD;
  spec.decrementAction = ACTION_DEC;
  spec.incrementAction = ACTION_INC;
  spec.cancelAction = ACTION_CANCEL;
  spec.okAction = ACTION_OK;
  buildStepperFieldsScreen(screen, renderer, mappedInput, spec);

  if (!mappedInput.hasTouch()) addStepperHint(screen, tr(STR_SET_DATE_HINT));
}

void ReadingDateSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();
  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_SET_DATE), getSelectedDateLabel().c_str());
  renderUi();
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CONFIRM), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
