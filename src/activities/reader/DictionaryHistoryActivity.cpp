#include "DictionaryHistoryActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <optional>

#include "CrossPointSettings.h"
#include "DictionaryDefinitionActivity.h"
#include "DictionaryStore.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void DictionaryHistoryActivity::onEnter() {
  Activity::onEnter();
  history = DICTIONARIES.getHistory();
  selectedIndex = 0;
  requestUpdate();
}

void DictionaryHistoryActivity::lookupSelected() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(history.size())) return;
  const auto lookup = DICTIONARIES.lookup(history[selectedIndex], true);
  if (lookup.status != DictionaryLookupResult::Status::Found) {
    GUI.drawPopup(renderer, DictionaryStore::lookupErrorMessage(lookup.status));
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    delay(800);
    requestUpdate();
    return;
  }

  startActivityForResult(std::make_unique<DictionaryDefinitionActivity>(
                             renderer, mappedInput, page, lookup.headword, lookup.definition, lookup.truncated,
                             readerFontId, DICTIONARIES.getDefinitionFontId(readerFontId), marginLeft, marginTop),
                         [this](const ActivityResult&) {
                           history = DICTIONARIES.getHistory();
                           selectedIndex = std::min(selectedIndex, std::max(0, static_cast<int>(history.size()) - 1));
                           requestUpdate();
                         });
}

void DictionaryHistoryActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    lookupSelected();
    return;
  }

  buttonNavigator.onNext([this] {
    if (!history.empty()) {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(history.size()));
      requestUpdate();
    }
  });
  buttonNavigator.onPrevious([this] {
    if (!history.empty()) {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(history.size()));
      requestUpdate();
    }
  });
}

void DictionaryHistoryActivity::render(RenderLock&&) {
  renderer.clearScreen();
  std::optional<FontCacheManager::PrewarmScope> pageFontPrewarm;
  if (page) {
    if (auto* fcm = renderer.getFontCacheManager()) {
      pageFontPrewarm.emplace(*fcm);
      page->recordFontUsage(*fcm, readerFontId, SETTINGS.bionicReading);
      pageFontPrewarm->endScanAndPrewarm();
    }
    page->render(renderer, readerFontId, marginLeft, marginTop, SETTINGS.bionicReading);
  }

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int overlayHeight = std::max((screenHeight * 3) / 4, 170);
  const Rect rect{10, screenHeight - overlayHeight - 10, screenWidth - 20, overlayHeight};

  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height, 2, true);
  GUI.drawSubHeader(renderer, Rect{rect.x + 4, rect.y + 8, rect.width - 8, 40}, tr(STR_LOOKUP_HISTORY), nullptr);

  const int listTop = rect.y + 58;
  const int listHeight = rect.height - 68;
  if (history.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, listTop + 20, tr(STR_NO_ENTRIES));
  } else {
    GUI.drawList(renderer, Rect{rect.x, listTop, rect.width, listHeight}, static_cast<int>(history.size()),
                 selectedIndex, [this](int index) { return history[index]; });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
