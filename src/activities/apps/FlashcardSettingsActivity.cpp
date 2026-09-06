#include "FlashcardSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <utility>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "util/HeaderDateUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr int ROW_STUDY_MODE = 0;
constexpr int ROW_SESSION_SIZE = 1;

// Indexed by CrossPointSettings::FLASHCARD_STUDY_MODE.
const StrId studyModeNames[CrossPointSettings::FLASHCARD_STUDY_MODE_COUNT] = {
    StrId::STR_DUE, StrId::STR_SCHEDULED, StrId::STR_RANDOM_PRACTICE, StrId::STR_SEQUENTIAL};
// Indexed by CrossPointSettings::FLASHCARD_SESSION_SIZE.
const char* const sessionSizeNames[CrossPointSettings::FLASHCARD_SESSION_SIZE_COUNT] = {"10", "20", "30", "50",
                                                                                        nullptr /* All */};

const char* getStudyModeValue() {
  const uint8_t mode = SETTINGS.flashcardStudyMode < CrossPointSettings::FLASHCARD_STUDY_MODE_COUNT
                           ? SETTINGS.flashcardStudyMode
                           : CrossPointSettings::FLASHCARD_STUDY_SCHEDULED;
  return I18N.get(studyModeNames[mode]);
}

const char* getSessionSizeValue() {
  const uint8_t size = SETTINGS.flashcardSessionSize;
  if (size < CrossPointSettings::FLASHCARD_SESSION_ALL && sessionSizeNames[size] != nullptr) {
    return sessionSizeNames[size];
  }
  return tr(STR_ALL);
}
}  // namespace

void FlashcardSettingsActivity::onEnter() {
  UiListActivity::onEnter();
  rowItems[ROW_STUDY_MODE] = fui::ListItem{};
  rowItems[ROW_STUDY_MODE].label = tr(STR_STUDY_MODE);
  rowItems[ROW_STUDY_MODE].actionValue = ROW_STUDY_MODE;
  rowItems[ROW_SESSION_SIZE] = fui::ListItem{};
  rowItems[ROW_SESSION_SIZE].label = tr(STR_SESSION_SIZE);
  rowItems[ROW_SESSION_SIZE].actionValue = ROW_SESSION_SIZE;
  refreshRowValues();
}

void FlashcardSettingsActivity::refreshRowValues() {
  rowValues[ROW_STUDY_MODE] = getStudyModeValue();
  rowValues[ROW_SESSION_SIZE] = getSessionSizeValue();
  for (int i = 0; i < SETTING_COUNT; ++i) {
    rowItems[i].value = rowValues[i].c_str();
  }
}

bool FlashcardSettingsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void FlashcardSettingsActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  nav.selected = index;
  app.clearTapFlash();  // a popup opens over the row
  if (index == ROW_STUDY_MODE) {
    optionPopup.show(StrId::STR_STUDY_MODE, studyModeNames, CrossPointSettings::FLASHCARD_STUDY_MODE_COUNT,
                     SETTINGS.flashcardStudyMode, [this](const int idx) {
                       SETTINGS.flashcardStudyMode = static_cast<uint8_t>(idx);
                       SETTINGS.saveToFile();
                       RenderLock lock(*this);
                       refreshRowValues();
                     });
  } else if (index == ROW_SESSION_SIZE) {
    const char* options[CrossPointSettings::FLASHCARD_SESSION_SIZE_COUNT];
    for (int i = 0; i < CrossPointSettings::FLASHCARD_SESSION_SIZE_COUNT; ++i) {
      options[i] = sessionSizeNames[i] != nullptr ? sessionSizeNames[i] : tr(STR_ALL);
    }
    optionPopup.show(tr(STR_SESSION_SIZE), options, CrossPointSettings::FLASHCARD_SESSION_SIZE_COUNT,
                     SETTINGS.flashcardSessionSize, [this](const int idx) {
                       SETTINGS.flashcardSessionSize = static_cast<uint8_t>(idx);
                       SETTINGS.saveToFile();
                       RenderLock lock(*this);
                       refreshRowValues();
                     });
  }
  requestUpdate();
}

void FlashcardSettingsActivity::drawChrome() {
  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_FLASHCARDS), tr(STR_SETTINGS_TITLE));
}

void FlashcardSettingsActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void FlashcardSettingsActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

void FlashcardSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(SETTING_COUNT);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
