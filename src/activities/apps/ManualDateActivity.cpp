#include "ManualDateActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>

#include "CrossPointState.h"
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
constexpr int MAX_DAY = 31;
constexpr int MIN_MONTH = 1;
constexpr int MAX_MONTH = 12;
constexpr int MIN_YEAR = 2024;
constexpr int MAX_YEAR = 2099;

constexpr fui::ActionId ACTION_FIELD = 1;
constexpr fui::ActionId ACTION_DEC = 2;
constexpr fui::ActionId ACTION_INC = 3;
constexpr fui::ActionId ACTION_CANCEL = 4;
constexpr fui::ActionId ACTION_OK = 5;

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

void ManualDateActivity::onEnter() {
  Activity::onEnter();
  TimeUtils::configureTimezone();

  year = 2026;
  month = 6;
  day = 15;

  const auto displayDateInfo = HeaderDateUtils::getDisplayDateInfo();
  const uint32_t referenceTimestamp = displayDateInfo.timestamp;

  if (TimeUtils::isClockValid(referenceTimestamp)) {
    time_t currentTime = static_cast<time_t>(referenceTimestamp);
    tm localTime = {};
    if (localtime_r(&currentTime, &localTime) != nullptr) {
      year = std::clamp(localTime.tm_year + 1900, MIN_YEAR, MAX_YEAR);
      month = static_cast<unsigned>(std::clamp(localTime.tm_mon + 1, MIN_MONTH, MAX_MONTH));
      day = static_cast<unsigned>(std::clamp(localTime.tm_mday, MIN_DAY, MAX_DAY));
    }
  }

  selectedField = 0;
  resetUi();
  app.on(ACTION_FIELD, &ManualDateActivity::onFieldEvent, this);
  app.on(ACTION_DEC, &ManualDateActivity::onStepEvent, this);
  app.on(ACTION_INC, &ManualDateActivity::onStepEvent, this);
  app.on(ACTION_CANCEL, &ManualDateActivity::onCancelEvent, this);
  app.on(ACTION_OK, &ManualDateActivity::onOkEvent, this);
  app.setScreen(&ManualDateActivity::screenTrampoline, this);
  requestUpdate();
}

void ManualDateActivity::selectField(const int field) {
  if (field < 0 || field >= FIELD_COUNT) return;
  selectedField = field;
  requestUpdate();
}

void ManualDateActivity::adjustField(const int field, const int delta) {
  if (field == FIELD_DAY) {
    day = wrapValue(day, delta, MIN_DAY, MAX_DAY);
  } else if (field == FIELD_MONTH) {
    month = wrapValue(month, delta, MIN_MONTH, MAX_MONTH);
  } else {
    year = std::clamp(year + delta, MIN_YEAR, MAX_YEAR);
  }
  requestUpdate();
}

std::string ManualDateActivity::getSelectedDateLabel() const { return TimeUtils::formatDateParts(year, month, day); }

void ManualDateActivity::saveDate() {
  uint32_t epoch = 0;
  if (!TimeUtils::setCurrentDate(year, month, day, &epoch)) {
    return;
  }

  APP_STATE.registerValidTimeSync(epoch);
  APP_STATE.saveToFile();
  finish();
}

void ManualDateActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<ManualDateActivity*>(user)->buildScreen(screen);
}

void ManualDateActivity::onFieldEvent(const fui::ActionEvent& event, void* user) {
  static_cast<ManualDateActivity*>(user)->selectField(event.value);
}

void ManualDateActivity::onStepEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<ManualDateActivity*>(user);
  self->selectedField = std::clamp(static_cast<int>(event.value), 0, FIELD_COUNT - 1);
  self->adjustField(event.value, event.action == ACTION_INC ? 1 : -1);
}

void ManualDateActivity::onCancelEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<ManualDateActivity*>(user);
  self->app.clearTapFlash();  // the tap leaves this screen
  self->finish();
}

void ManualDateActivity::onOkEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<ManualDateActivity*>(user);
  self->app.clearTapFlash();
  self->saveDate();
}

void ManualDateActivity::loop() {
  // Touch goes through the FreeInkApp: render() registered the rows, the
  // [-]/[+] controls and the Cancel/OK pair.
  const auto route = routeTouch(mappedInput);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) return;

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    saveDate();
    return;
  }

  buttonNavigator.onRelease({MappedInputManager::Button::Down},
                            [this] { selectField(ButtonNavigator::nextIndex(selectedField, FIELD_COUNT)); });
  buttonNavigator.onRelease({MappedInputManager::Button::Up},
                            [this] { selectField(ButtonNavigator::previousIndex(selectedField, FIELD_COUNT)); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { adjustField(selectedField, -1); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { adjustField(selectedField, 1); });
}

void ManualDateActivity::buildScreen(UiScreen& screen) {
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

  // Button-board hint; touch users have the visible [-]/[+] controls.
  if (!mappedInput.hasTouch()) addStepperHint(screen, tr(STR_SET_DATE_HINT));
}

void ManualDateActivity::render(RenderLock&&) {
  renderer.clearScreen();
  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_SET_DATE), getSelectedDateLabel().c_str());
  renderUi();
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CONFIRM), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
