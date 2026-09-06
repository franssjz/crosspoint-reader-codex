#include "FavoritesOrderActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "util/HeaderDateUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr unsigned long DELETE_FAVORITE_HOLD_MS = 1000;

std::string getFavoriteTitle(const FavoriteBook& book) {
  if (!book.title.empty()) {
    return book.title;
  }

  const auto slashPos = book.path.find_last_of('/');
  const std::string filename = slashPos == std::string::npos ? book.path : book.path.substr(slashPos + 1);
  const auto dotPos = filename.rfind('.');
  return dotPos == std::string::npos ? filename : filename.substr(0, dotPos);
}
}  // namespace

void FavoritesOrderActivity::onEnter() {
  UiListActivity::onEnter();
  moveMode = false;
  reloadEntries();
}

void FavoritesOrderActivity::onExit() {
  Activity::onExit();
  rowItems.clear();
  rowTitles.clear();
  entries.clear();
}

void FavoritesOrderActivity::reloadEntries() {
  entries = FAVORITES.getBooks();
  moveMode = !entries.empty() && moveMode;
  if (entries.empty()) {
    nav.selected = 0;
  } else {
    nav.selected = std::clamp(nav.selected, 0, static_cast<int>(entries.size()) - 1);
  }
  rebuildRowItems();
}

// Derives the row caches from entries; called from reloadEntries() and after
// a swap, never from buildScreen().
void FavoritesOrderActivity::rebuildRowItems() {
  rowTitles.clear();
  rowItems.clear();
  rowTitles.reserve(entries.size());
  rowItems.reserve(entries.size());
  for (size_t i = 0; i < entries.size(); ++i) {
    rowTitles.push_back(getFavoriteTitle(entries[i]));
    fui::ListItem item;
    item.label = rowTitles.back().c_str();
    item.subtitle = !entries[i].author.empty() ? entries[i].author.c_str() : entries[i].path.c_str();
    // The picked entry (move mode) is marked in the value slot.
    if (moveMode && static_cast<int>(i) == nav.selected) item.value = tr(STR_SELECTED);
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
}

void FavoritesOrderActivity::setMoveMode(const bool enabled) {
  {
    RenderLock lock(*this);
    moveMode = enabled && !entries.empty();
    rebuildRowItems();
  }
  requestUpdate();
}

void FavoritesOrderActivity::moveSelectedEntry(const int delta) {
  const int targetIndex = nav.selected + delta;
  if (targetIndex < 0 || targetIndex >= static_cast<int>(entries.size()) || targetIndex == nav.selected) {
    return;
  }

  if (!FAVORITES.moveBook(nav.selected, targetIndex)) {
    return;
  }

  {
    RenderLock lock(*this);
    std::swap(entries[nav.selected], entries[targetIndex]);
    nav.selected = targetIndex;
    nav.follow(listCount());
    rebuildRowItems();
  }
  requestUpdate();
}

void FavoritesOrderActivity::confirmDeleteEntry(const int index) {
  if (index < 0 || index >= static_cast<int>(entries.size())) {
    return;
  }

  const FavoriteBook selectedEntry = entries[index];
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_FROM_FAVORITES),
                                                                getFavoriteTitle(selectedEntry)),
                         [this, entryPath = selectedEntry.path](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             FAVORITES.removeBook(entryPath);
                             // The interaction table still indexes the pre-removal rows.
                             closeRouting();
                             RenderLock lock(*this);
                             reloadEntries();
                             nav.follow(listCount());
                           }
                           requestUpdate(true);
                         });
}

bool FavoritesOrderActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (moveMode) {
      setMoveMode(false);
    } else {
      finish();
    }
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!entries.empty()) {
      if (!moveMode && mappedInput.getHeldTime() >= DELETE_FAVORITE_HOLD_MS) {
        confirmDeleteEntry(nav.selected);
        return true;
      }
      setMoveMode(!moveMode);
    }
    return true;
  }
  return false;
}

void FavoritesOrderActivity::navigateButtons() {
  if (!moveMode) {
    UiListActivity::navigateButtons();
    return;
  }
  buttonNavigator.onNextRelease([this] { moveSelectedEntry(1); });
  buttonNavigator.onPreviousRelease([this] { moveSelectedEntry(-1); });
}

void FavoritesOrderActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  if (!moveMode) {
    // Pick this entry: move mode on, marked in the value slot.
    nav.selected = index;
    setMoveMode(true);
    return;
  }
  if (index == nav.selected) {
    setMoveMode(false);
    return;
  }
  // Move the picked entry into the tapped slot (same swap as Up/Down).
  moveSelectedEntry(index - nav.selected);
}

void FavoritesOrderActivity::onRowLongPress(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  confirmDeleteEntry(index);
}

void FavoritesOrderActivity::drawChrome() {
  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_ORDER_FAVORITES), tr(STR_FAVORITES_SORT_DESC));
}

void FavoritesOrderActivity::drawFooter() {
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), moveMode ? tr(STR_DONE) : tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void FavoritesOrderActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (entries.empty()) {
    screen.centeredText(tr(STR_NO_FAVORITES), screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  // Tap picks/moves; long-press prompts removal (physical buttons stay in loop()).
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  props.valueInset = 8;
  fui::TextStyle label = screen.theme().smallText;
  label.bold = true;
  props.labelText = label;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}
