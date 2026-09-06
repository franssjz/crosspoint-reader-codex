#include "BookReadingAdjustmentActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

#include "AchievementsStore.h"
#include "ReadingDateSelectionActivity.h"
#include "ReadingStatsStore.h"
#include "StepperFieldsUi.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/ReadingStatsAnalytics.h"
#include "util/TimeUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr int FIELD_COUNT = 3;
constexpr int FIELD_ACTION = 0;
constexpr int FIELD_DATE = 1;
constexpr int FIELD_AMOUNT = 2;
constexpr int OPERATION_COUNT = 2;
constexpr int DURATION_COUNT = 4;
constexpr int64_t MINUTES_TO_MS = 60LL * 1000LL;
constexpr int DURATION_MINUTES[DURATION_COUNT] = {15, 30, 45, 60};

constexpr fui::ActionId ACTION_FIELD = 1;
constexpr fui::ActionId ACTION_DEC = 2;
constexpr fui::ActionId ACTION_INC = 3;
constexpr fui::ActionId ACTION_CANCEL = 4;
constexpr fui::ActionId ACTION_OK = 5;

int wrapIndex(const int value, const int delta, const int count) {
  int next = value + delta;
  next %= count;
  if (next < 0) {
    next += count;
  }
  return next;
}
}  // namespace

void BookReadingAdjustmentActivity::onEnter() {
  Activity::onEnter();
  selectedField = 0;
  selectedOperation = 0;
  selectedDuration = 1;
  lastApplyFailed = false;
  initializeSelectedDate();
  resetUi();
  app.on(ACTION_FIELD, &BookReadingAdjustmentActivity::onFieldEvent, this);
  app.on(ACTION_DEC, &BookReadingAdjustmentActivity::onStepEvent, this);
  app.on(ACTION_INC, &BookReadingAdjustmentActivity::onStepEvent, this);
  app.on(ACTION_CANCEL, &BookReadingAdjustmentActivity::onCancelEvent, this);
  app.on(ACTION_OK, &BookReadingAdjustmentActivity::onOkEvent, this);
  app.setScreen(&BookReadingAdjustmentActivity::screenTrampoline, this);
  requestUpdate();
}

void BookReadingAdjustmentActivity::selectField(const int field) {
  if (field < 0 || field >= FIELD_COUNT) return;
  selectedField = field;
  lastApplyFailed = false;
  requestUpdate();
}

void BookReadingAdjustmentActivity::adjustValue(const int field, const int delta) {
  lastApplyFailed = false;
  if (field == FIELD_ACTION) {
    selectedOperation = wrapIndex(selectedOperation, delta, OPERATION_COUNT);
  } else if (field == FIELD_DATE) {
    if (selectedDayOrdinal != 0 || delta > 0) {
      selectedDayOrdinal = static_cast<uint32_t>(static_cast<int32_t>(selectedDayOrdinal) + delta);
    }
  } else {
    selectedDuration = wrapIndex(selectedDuration, delta, DURATION_COUNT);
  }
  requestUpdate();
}

void BookReadingAdjustmentActivity::initializeSelectedDate() {
  bool usedFallback = false;
  const uint32_t referenceTimestamp = READING_STATS.getDisplayTimestamp(&usedFallback);
  if (!TimeUtils::isClockValid(referenceTimestamp)) {
    selectedDayOrdinal = 0;
    return;
  }

  selectedDayOrdinal = TimeUtils::getLocalDayOrdinal(referenceTimestamp);
}

void BookReadingAdjustmentActivity::openDateSelection() {
  startActivityForResult(std::make_unique<ReadingDateSelectionActivity>(renderer, mappedInput, selectedDayOrdinal),
                         [this](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             if (const auto* page = std::get_if<PageResult>(&result.data)) {
                               selectedDayOrdinal = page->page;
                               lastApplyFailed = false;
                             }
                           }
                           requestUpdate();
                         });
}

int32_t BookReadingAdjustmentActivity::getSelectedDeltaMs() const {
  int32_t deltaMs = static_cast<int32_t>(DURATION_MINUTES[selectedDuration] * MINUTES_TO_MS);
  if (selectedOperation == 1) {
    deltaMs = -deltaMs;
  }
  return deltaMs;
}

uint64_t BookReadingAdjustmentActivity::getSelectedDayReadingMs() const {
  if (selectedDayOrdinal == 0) {
    return 0;
  }

  const auto* book = READING_STATS.findBook(bookPath);
  if (book == nullptr) {
    return 0;
  }

  for (const auto& day : book->readingDays) {
    if (day.dayOrdinal == selectedDayOrdinal) {
      return day.readingMs;
    }
    if (day.dayOrdinal > selectedDayOrdinal) {
      break;
    }
  }
  return 0;
}

bool BookReadingAdjustmentActivity::canApplySelectedAdjustment() const {
  if (selectedDayOrdinal == 0 || READING_STATS.findBook(bookPath) == nullptr) {
    return false;
  }

  const int32_t deltaMs = getSelectedDeltaMs();
  if (deltaMs >= 0) {
    return true;
  }

  return getSelectedDayReadingMs() >= static_cast<uint64_t>(-deltaMs);
}

std::string BookReadingAdjustmentActivity::getAdjustmentPreviewInfo() const {
  const auto* book = READING_STATS.findBook(bookPath);
  if (book == nullptr) {
    return tr(STR_BOOK_NOT_FOUND);
  }
  if (selectedDayOrdinal == 0) {
    return tr(STR_SET_DATE_BEFORE_APPLYING);
  }

  const uint64_t currentMs = getSelectedDayReadingMs();
  const int32_t deltaMs = getSelectedDeltaMs();
  const std::string dayTotal = std::string(tr(STR_DAY_TOTAL)) + ": ";
  if (deltaMs < 0) {
    const uint64_t removeMs = static_cast<uint64_t>(-deltaMs);
    if (currentMs < removeMs) {
      return dayTotal + ReadingStatsAnalytics::formatDurationHm(currentMs) + " (" + tr(STR_NOT_ENOUGH) + ")";
    }
    return dayTotal + ReadingStatsAnalytics::formatDurationHm(currentMs) + " -> " +
           ReadingStatsAnalytics::formatDurationHm(currentMs - removeMs);
  }

  return dayTotal + ReadingStatsAnalytics::formatDurationHm(currentMs) + " -> " +
         ReadingStatsAnalytics::formatDurationHm(currentMs + static_cast<uint64_t>(deltaMs));
}

const char* BookReadingAdjustmentActivity::getOperationLabel() const {
  return selectedOperation == 0 ? tr(STR_ADD) : tr(STR_SUBTRACT);
}

std::string BookReadingAdjustmentActivity::getDateLabel() const {
  int selectedYear = 0;
  unsigned selectedMonth = 0;
  unsigned selectedDay = 0;
  if (selectedDayOrdinal == 0 ||
      !TimeUtils::getDateFromDayOrdinal(selectedDayOrdinal, selectedYear, selectedMonth, selectedDay)) {
    return tr(STR_NOT_SET);
  }
  return TimeUtils::formatDateParts(selectedYear, selectedMonth, selectedDay);
}

bool BookReadingAdjustmentActivity::applyAdjustment() {
  const uint32_t dayOrdinal = selectedDayOrdinal;
  const int32_t deltaMs = getSelectedDeltaMs();

  if (!READING_STATS.adjustBookReadingTime(bookPath, dayOrdinal, deltaMs)) {
    lastApplyFailed = true;
    requestUpdate();
    return false;
  }

  ACHIEVEMENTS.rebuildProgressFromCurrentStats();
  finish();
  return true;
}

void BookReadingAdjustmentActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<BookReadingAdjustmentActivity*>(user)->buildScreen(screen);
}

void BookReadingAdjustmentActivity::onFieldEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<BookReadingAdjustmentActivity*>(user);
  self->selectField(event.value);
  if (event.value == FIELD_DATE) {
    // Same as Confirm on the Date field: open the date picker.
    self->app.clearTapFlash();
    self->openDateSelection();
  }
}

void BookReadingAdjustmentActivity::onStepEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<BookReadingAdjustmentActivity*>(user);
  self->selectedField = std::clamp(static_cast<int>(event.value), 0, FIELD_COUNT - 1);
  self->adjustValue(event.value, event.action == ACTION_INC ? 1 : -1);
}

void BookReadingAdjustmentActivity::onCancelEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<BookReadingAdjustmentActivity*>(user);
  self->app.clearTapFlash();  // the tap leaves this screen
  self->finish();
}

void BookReadingAdjustmentActivity::onOkEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<BookReadingAdjustmentActivity*>(user);
  self->app.clearTapFlash();
  self->applyAdjustment();
}

void BookReadingAdjustmentActivity::loop() {
  const auto route = routeTouch(mappedInput);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) return;

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedField == FIELD_DATE) {
      openDateSelection();
      return;
    }
    applyAdjustment();
    return;
  }

  buttonNavigator.onRelease({MappedInputManager::Button::Down},
                            [this] { selectField(ButtonNavigator::nextIndex(selectedField, FIELD_COUNT)); });
  buttonNavigator.onRelease({MappedInputManager::Button::Up},
                            [this] { selectField(ButtonNavigator::previousIndex(selectedField, FIELD_COUNT)); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { adjustValue(selectedField, -1); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { adjustValue(selectedField, 1); });
}

void BookReadingAdjustmentActivity::buildScreen(UiScreen& screen) {
  dateLabel = getDateLabel();
  previewInfo = getAdjustmentPreviewInfo();
  snprintf(durationText, sizeof(durationText), "%d min", DURATION_MINUTES[selectedDuration]);

  const StepperField fields[FIELD_COUNT] = {
      {tr(STR_ACTION), getOperationLabel(), nullptr},
      {tr(STR_DATE), dateLabel.c_str(), "00/00/0000"},
      {tr(STR_AMOUNT), durationText, "00 min"},
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

  addStepperHint(screen, previewInfo.c_str());
  const char* hint = selectedField == FIELD_DATE ? tr(STR_SELECT_OPENS_DATE_PICKER) : tr(STR_SELECT_APPLIES_CORRECTION);
  if (lastApplyFailed) {
    hint = tr(STR_COULD_NOT_APPLY_CORRECTION);
  } else if (!canApplySelectedAdjustment()) {
    hint = tr(STR_CHOOSE_ADD_OR_REDUCE_AMOUNT);
  }
  addStepperHint(screen, hint);
}

void BookReadingAdjustmentActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const std::string subtitle = renderer.truncatedText(UI_10_FONT_ID, bookTitle.c_str(),
                                                      renderer.getScreenWidth() - metrics.contentSidePadding * 2);
  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_ADJUST_READING_TIME), subtitle.c_str());
  renderUi();
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), selectedField == FIELD_DATE ? tr(STR_SELECT) : tr(STR_CONFIRM),
                            tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
