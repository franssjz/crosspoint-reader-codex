#include "DictionaryActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "DictionaryStore.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr int DICTIONARY_ACTION_COUNT = 2;
constexpr int ACTION_DEFINITION_TEXT_SIZE = 0;
constexpr int ACTION_CLEAR_HISTORY = 1;

const char* textSizeLabel() {
  switch (DICTIONARIES.getDefinitionTextSize()) {
    case DictionaryStore::DEF_TEXT_LARGE:
      return tr(STR_LARGE);
    case DictionaryStore::DEF_TEXT_SMALL:
    default:
      return tr(STR_SMALL);
  }
}
}  // namespace

void DictionaryActivity::onEnter() {
  UiListActivity::onEnter();
  DICTIONARIES.ensureScanned();
  const int activeIndex = DICTIONARIES.getActiveIndex();
  nav.selected = activeIndex >= 0 ? activeIndex + DICTIONARY_ACTION_COUNT : 0;
  rebuildRows();
}

void DictionaryActivity::onExit() {
  Activity::onExit();
  rowItems.clear();
  rowValues.clear();
}

// Rows: the two settings, then one per dictionary entry. Rebuilt whenever a
// value changes (text size, active dictionary), never from buildScreen().
void DictionaryActivity::rebuildRows() {
  const auto& entries = DICTIONARIES.getEntries();
  const int activeIndex = DICTIONARIES.getActiveIndex();
  entryCount = static_cast<int>(entries.size());

  rowValues.clear();
  rowItems.clear();
  const size_t total = DICTIONARY_ACTION_COUNT + entries.size();
  rowValues.reserve(total);
  rowItems.reserve(total);

  rowValues.emplace_back(textSizeLabel());
  fui::ListItem textSize;
  textSize.label = tr(STR_DEFINITION_TEXT_SIZE);
  textSize.value = rowValues.back().c_str();
  textSize.actionValue = ACTION_DEFINITION_TEXT_SIZE;
  rowItems.push_back(textSize);

  rowValues.emplace_back();
  fui::ListItem clearHistory;
  clearHistory.label = tr(STR_CLEAR_HISTORY);
  clearHistory.actionValue = ACTION_CLEAR_HISTORY;
  rowItems.push_back(clearHistory);

  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    if (entry.compressed) {
      rowValues.emplace_back("ZIP");
    } else if (entry.missingFiles) {
      rowValues.emplace_back("!");
    } else if (static_cast<int>(i) == activeIndex) {
      rowValues.emplace_back(tr(STR_DICTIONARY_ACTIVE));
    } else {
      rowValues.emplace_back();
    }
    fui::ListItem item;
    item.label = entry.languageId.c_str();
    item.subtitle = entry.name.c_str();
    item.value = rowValues.back().empty() ? nullptr : rowValues.back().c_str();
    item.actionValue = static_cast<int16_t>(DICTIONARY_ACTION_COUNT + i);
    rowItems.push_back(item);
  }
}

void DictionaryActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  selectIndex(index);
}

void DictionaryActivity::selectIndex(const int index) {
  if (index == ACTION_DEFINITION_TEXT_SIZE) {
    const uint8_t nextSize =
        static_cast<uint8_t>((DICTIONARIES.getDefinitionTextSize() + 1) % DictionaryStore::DEF_TEXT_SIZE_COUNT);
    DICTIONARIES.setDefinitionTextSize(nextSize);
    {
      RenderLock lock(*this);
      rebuildRows();
    }
    requestUpdate();
    return;
  }
  if (index == ACTION_CLEAR_HISTORY) {
    DICTIONARIES.clearHistory();
    GUI.drawPopup(renderer, tr(STR_CLEAR_HISTORY));
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    delay(650);
    requestUpdate();
    return;
  }

  const auto& entries = DICTIONARIES.getEntries();
  const int dictionaryIndex = index - DICTIONARY_ACTION_COUNT;
  if (dictionaryIndex < 0 || dictionaryIndex >= static_cast<int>(entries.size())) return;
  const auto& entry = entries[dictionaryIndex];
  if (entry.compressed) {
    GUI.drawPopup(renderer, tr(STR_DICTIONARY_COMPRESSED_UNSUPPORTED));
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    delay(1100);
    requestUpdate();
    return;
  }
  if (entry.missingFiles) {
    GUI.drawPopup(renderer, tr(STR_DICTIONARY_MISSING_FILES));
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    delay(1100);
    requestUpdate();
    return;
  }

  DICTIONARIES.setActiveIndex(dictionaryIndex);
  Rect popup;
  {
    RenderLock lock(*this);
    popup = GUI.drawPopup(renderer, tr(STR_DICTIONARY_PREPARING));
  }
  const bool ready = DICTIONARIES.prepareActive([this, &popup](int percent) {
    RenderLock lock(*this);
    GUI.fillPopupProgress(renderer, popup, percent);
  });
  GUI.drawPopup(renderer, ready ? tr(STR_DICTIONARY_READY) : tr(STR_DICTIONARY_PREPARE_FAILED));
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  delay(900);
  {
    RenderLock lock(*this);
    rebuildRows();
  }
  requestUpdate();
}

void DictionaryActivity::drawChrome() {
  const std::string activeLabel = DICTIONARIES.getActiveLabel();
  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_DICTIONARY),
                                      activeLabel.empty() ? nullptr : activeLabel.c_str());
}

void DictionaryActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (entryCount == 0) {
    // No dictionaries: the two setting rows stay, the hint sits underneath.
    fui::TextStyle note = screen.theme().smallText;
    note.align = fui::TextAlign::Center;
    const int16_t lh = screen.target().lineHeight(note.font);
    fui::Rect band = screen.takeBottom(static_cast<int16_t>(lh * 2), static_cast<int16_t>(metrics.verticalSpacing));
    screen.target().text(fui::Rect{band.x, band.y, band.width, lh}, tr(STR_NO_DICTIONARIES), note);
    screen.target().text(fui::Rect{band.x, static_cast<int16_t>(band.y + lh), band.width, lh},
                         "/dictionaries/<language>/", note);
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  fui::TextStyle label = screen.theme().smallText;
  label.bold = true;
  props.labelText = label;
  syncListViewport(screen, props, /*hasSubtitle=*/entryCount > 0);
  screen.list(props);
}
