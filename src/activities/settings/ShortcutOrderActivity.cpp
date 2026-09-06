#include "ShortcutOrderActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "util/ShortcutUiMetadata.h"

namespace fui = freeink::ui;

namespace {
std::string getEntryTitle(const ShortcutOrderEntry& entry) {
  return entry.isAppsHub ? std::string(tr(STR_APPS)) : ShortcutUiMetadata::getName(*entry.definition);
}
}  // namespace

void ShortcutOrderActivity::onEnter() {
  UiListActivity::onEnter();
  moveMode = false;
  reloadEntries();
}

void ShortcutOrderActivity::reloadEntries() {
  entries = getShortcutOrderEntries(group);
  if (entries.empty()) {
    nav.selected = 0;
  } else {
    nav.selected = std::clamp(nav.selected, 0, static_cast<int>(entries.size()) - 1);
  }
  rebuildRowItems();
}

// Derives the row cache from entries. Called when entries changes (onEnter),
// never from buildScreen().
void ShortcutOrderActivity::rebuildRowItems() {
  rowTitles.clear();
  rowItems.clear();
  rowTitles.reserve(entries.size());
  rowItems.reserve(entries.size());
  for (const auto& entry : entries) {
    rowTitles.push_back(getEntryTitle(entry));
  }
  for (size_t i = 0; i < entries.size(); ++i) {
    fui::ListItem item;
    item.label = rowTitles[i].c_str();
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
}

void ShortcutOrderActivity::moveSelectedEntry(const int delta) {
  const int selectedIndex = nav.selected;
  const int targetIndex = selectedIndex + delta;
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size()) || targetIndex < 0 ||
      targetIndex >= static_cast<int>(entries.size()) || targetIndex == selectedIndex) {
    return;
  }

  {
    // The render task reads entries/rowTitles/nav mid-build: swap under the lock.
    RenderLock lock(*this);
    auto& selectedOrder = getShortcutOrderRef(SETTINGS, entries[static_cast<size_t>(selectedIndex)]);
    auto& targetOrder = getShortcutOrderRef(SETTINGS, entries[static_cast<size_t>(targetIndex)]);
    std::swap(selectedOrder, targetOrder);
    normalizeShortcutOrderSettings(SETTINGS);

    std::swap(entries[static_cast<size_t>(selectedIndex)], entries[static_cast<size_t>(targetIndex)]);
    std::swap(rowTitles[static_cast<size_t>(selectedIndex)], rowTitles[static_cast<size_t>(targetIndex)]);
    // std::string swap exchanges buffers; re-point the rows at their titles.
    rowItems[static_cast<size_t>(selectedIndex)].label = rowTitles[static_cast<size_t>(selectedIndex)].c_str();
    rowItems[static_cast<size_t>(targetIndex)].label = rowTitles[static_cast<size_t>(targetIndex)].c_str();
    nav.selected = targetIndex;
    pickedIndex = targetIndex;
    nav.follow(listCount());
  }
  requestUpdate();
}

void ShortcutOrderActivity::moveSelectedEntryTo(const int targetIndex) {
  const int step = targetIndex > nav.selected ? 1 : -1;
  while (nav.selected != targetIndex) {
    const int before = nav.selected;
    moveSelectedEntry(step);
    if (nav.selected == before) break;  // bounds hit: never spin
  }
}

const char* ShortcutOrderActivity::headerTitle() const {
  return group == ShortcutOrderGroup::Home ? tr(STR_ORDER_HOME_SHORTCUTS) : tr(STR_ORDER_APPS_SHORTCUTS);
}

bool ShortcutOrderActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (moveMode) {
      moveMode = false;
      requestUpdate();
    } else {
      finish();
    }
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!entries.empty()) {
      moveMode = !moveMode;
      pickedIndex = nav.selected;
      requestUpdate();
    }
    return true;
  }
  return false;
}

void ShortcutOrderActivity::navigateButtons() {
  if (!moveMode) {
    UiListActivity::navigateButtons();
    return;
  }
  buttonNavigator.onNextRelease([this] { moveSelectedEntry(1); });
  buttonNavigator.onPreviousRelease([this] { moveSelectedEntry(-1); });
}

void ShortcutOrderActivity::activateIndex(const int index) {
  // Touch path only: Confirm is consumed by handleButtons(). onRowAction has
  // already moved the selection to the tapped row.
  if (entries.empty()) return;
  if (!moveMode) {
    moveMode = true;
    pickedIndex = index;
    requestUpdate();
    return;
  }
  if (pickedIndex == index) {
    moveMode = false;  // tapping the picked row puts it down
    requestUpdate();
    return;
  }
  nav.selected = pickedIndex;
  moveSelectedEntryTo(index);
}

void ShortcutOrderActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (entries.empty()) {
    screen.centeredText(tr(STR_NO_ENTRIES), screen.theme().bodyText);
    return;
  }

  // Move-mode marker on the picked-up row (pointer assignment only; the row
  // structure itself was built by rebuildRowItems()).
  for (size_t i = 0; i < rowItems.size(); ++i) {
    rowItems[i].value = (moveMode && static_cast<int>(i) == pickedIndex) ? tr(STR_SELECTED) : nullptr;
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void ShortcutOrderActivity::drawFooter() {
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), moveMode ? tr(STR_DONE) : tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
