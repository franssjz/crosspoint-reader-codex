#include "ShortcutVisibilityActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "util/ShortcutUiMetadata.h"

namespace fui = freeink::ui;

namespace {
const char* getVisibilityLabel(const ShortcutDefinition& definition) {
  return getShortcutVisibility(definition) ? tr(STR_SHOW) : tr(STR_HIDDEN);
}
}  // namespace

void ShortcutVisibilityActivity::reloadEntries() {
  entries.clear();
  entries.reserve(getShortcutDefinitions().size());
  for (const auto& definition : getShortcutDefinitions()) {
    if (isShortcutAlwaysVisible(definition)) {
      continue;
    }
    entries.push_back(&definition);
  }

  std::stable_sort(entries.begin(), entries.end(), [](const ShortcutDefinition* lhs, const ShortcutDefinition* rhs) {
    return getShortcutOrder(*lhs) < getShortcutOrder(*rhs);
  });

  if (entries.empty()) {
    nav.selected = 0;
  } else {
    nav.selected = std::clamp(nav.selected, 0, static_cast<int>(entries.size()) - 1);
  }
  rebuildRowItems();
}

// Derives the row cache from entries. Called when entries changes (onEnter),
// never from buildScreen().
void ShortcutVisibilityActivity::rebuildRowItems() {
  rowLabels.clear();
  rowItems.clear();
  rowLabels.reserve(entries.size());
  rowItems.reserve(entries.size());
  for (const auto* definition : entries) {
    rowLabels.push_back(ShortcutUiMetadata::getName(*definition));
  }
  for (size_t i = 0; i < entries.size(); ++i) {
    fui::ListItem item;
    item.label = rowLabels[i].c_str();
    item.value = getVisibilityLabel(*entries[i]);
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
}

void ShortcutVisibilityActivity::onEnter() {
  UiListActivity::onEnter();
  reloadEntries();
}

const char* ShortcutVisibilityActivity::headerTitle() const { return tr(STR_SHORTCUT_VISIBILITY); }

void ShortcutVisibilityActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(entries.size())) {
    return;
  }

  const auto* definition = entries[static_cast<size_t>(index)];
  {
    // The render task reads rowItems mid-build; swap the value under the lock.
    RenderLock lock(*this);
    auto& visible = getShortcutVisibilityRef(SETTINGS, *definition);
    visible = visible == 0 ? 1 : 0;
    rowItems[static_cast<size_t>(index)].value = getVisibilityLabel(*definition);
  }
  requestUpdate();
}

void ShortcutVisibilityActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (entries.empty()) {
    screen.centeredText(tr(STR_NO_ENTRIES), screen.theme().bodyText);
    return;
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

void ShortcutVisibilityActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
