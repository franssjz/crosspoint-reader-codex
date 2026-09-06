#include "DictionarySuggestionsActivity.h"

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

namespace fui = freeink::ui;

namespace {
constexpr int OVERLAY_MARGIN = 10;
constexpr int OVERLAY_MIN_HEIGHT = 160;
constexpr int OVERLAY_HEADER_HEIGHT = 40;
constexpr int OVERLAY_HEADER_TOP = 8;
constexpr int OVERLAY_LIST_TOP = 58;
constexpr int OVERLAY_LIST_BOTTOM_PAD = 10;
}  // namespace

void DictionarySuggestionsActivity::onEnter() {
  UiListActivity::onEnter();
  rebuildRowItems();
}

void DictionarySuggestionsActivity::onExit() {
  Activity::onExit();
  rowItems.clear();
}

void DictionarySuggestionsActivity::rebuildRowItems() {
  rowItems.clear();
  rowItems.reserve(suggestions.size());
  for (size_t i = 0; i < suggestions.size(); ++i) {
    fui::ListItem item;
    item.label = suggestions[i].c_str();
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
}

Rect DictionarySuggestionsActivity::overlayRect() const {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int overlayHeight = std::max((screenHeight * 3) / 4, OVERLAY_MIN_HEIGHT);
  return Rect{OVERLAY_MARGIN, screenHeight - overlayHeight - OVERLAY_MARGIN, screenWidth - 2 * OVERLAY_MARGIN,
              overlayHeight};
}

void DictionarySuggestionsActivity::onBackButton() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void DictionarySuggestionsActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  const auto lookup = DICTIONARIES.lookup(suggestions[index], false);
  if (lookup.status != DictionaryLookupResult::Status::Found) {
    GUI.drawPopup(renderer, tr(STR_DEFINITION_NOT_FOUND));
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    delay(700);
    requestUpdate();
    return;
  }

  // The definition screen covers this one; a lingering flash would gray an
  // unrelated row when the list next appears.
  app.clearTapFlash();
  startActivityForResult(std::make_unique<DictionaryDefinitionActivity>(
                             renderer, mappedInput, page, lookup.headword, lookup.definition, lookup.truncated,
                             readerFontId, DICTIONARIES.getDefinitionFontId(readerFontId), marginLeft, marginTop),
                         [this](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             setResult(ActivityResult{});
                             finish();
                             return;
                           }
                           requestUpdate();
                         });
}

void DictionarySuggestionsActivity::drawChrome() {
  std::optional<FontCacheManager::PrewarmScope> pageFontPrewarm;
  if (page) {
    if (auto* fcm = renderer.getFontCacheManager()) {
      pageFontPrewarm.emplace(*fcm);
      page->recordFontUsage(*fcm, readerFontId, SETTINGS.bionicReading);
      pageFontPrewarm->endScanAndPrewarm();
    }
    page->render(renderer, readerFontId, marginLeft, marginTop, SETTINGS.bionicReading);
  }

  const Rect rect = overlayRect();
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height, 2, true);
  GUI.drawSubHeader(renderer, Rect{rect.x + 4, rect.y + OVERLAY_HEADER_TOP, rect.width - 8, OVERLAY_HEADER_HEIGHT},
                    tr(STR_DID_YOU_MEAN), originalWord.c_str());
}

void DictionarySuggestionsActivity::buildScreen(UiScreen& screen) {
  // The list lives inside the overlay panel drawChrome() framed, below its
  // sub-header and above the panel's bottom edge.
  const Rect rect = overlayRect();
  const int listTop = rect.y + OVERLAY_LIST_TOP;
  const int listBottom = rect.y + rect.height - OVERLAY_LIST_BOTTOM_PAD;
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(listTop), static_cast<int16_t>(renderer.getScreenWidth() - (rect.x + rect.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - listBottom), static_cast<int16_t>(rect.x)});

  if (suggestions.empty()) {
    screen.centeredText(tr(STR_NO_SUGGESTIONS), screen.theme().bodyText);
    return;
  }

  // rowItems was built once in onEnter() and is reused here on every repaint.
  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}

void DictionarySuggestionsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawChrome();
  renderUi();
  // Same follow-on-build correction as UiListActivity::render(): rebuild while
  // the nav reports the selection landed past the drawn rows (bounded).
  for (int pass = 0; nav.consumeRebuildNeeded() && pass < 8; ++pass) {
    renderer.clearScreen();
    drawChrome();
    renderUi();
  }
  drawFooter();
  // Overlay over a book page: fast refresh, as before the FreeInkUI conversion.
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
