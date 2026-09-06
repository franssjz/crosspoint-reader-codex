#include "ShortcutLocationActivity.h"

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
const char* getLocationLabel(const ShortcutDefinition& definition) {
  return static_cast<CrossPointSettings::SHORTCUT_LOCATION>(SETTINGS.*(definition.locationPtr)) ==
                 CrossPointSettings::SHORTCUT_HOME
             ? tr(STR_HOME_LOCATION)
             : tr(STR_APPS);
}
}  // namespace

void ShortcutLocationActivity::reloadEntries() {
  entries.clear();
  entries.reserve(getShortcutDefinitions().size());
  for (const auto& definition : getShortcutDefinitions()) {
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
void ShortcutLocationActivity::rebuildRowItems() {
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
    item.value = getLocationLabel(*entries[i]);
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
}

void ShortcutLocationActivity::onEnter() {
  UiListActivity::onEnter();
  reloadEntries();
}

const char* ShortcutLocationActivity::headerTitle() const { return tr(STR_SHORTCUT_LOCATION); }

void ShortcutLocationActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(entries.size())) {
    return;
  }

  auto* definition = entries[static_cast<size_t>(index)];
  {
    // The render task reads rowItems mid-build; swap the value under the lock.
    RenderLock lock(*this);
    auto& location = SETTINGS.*(definition->locationPtr);
    location = location == CrossPointSettings::SHORTCUT_HOME ? CrossPointSettings::SHORTCUT_APPS
                                                             : CrossPointSettings::SHORTCUT_HOME;
    rowItems[static_cast<size_t>(index)].value = getLocationLabel(*definition);
  }
  requestUpdate();
}

void ShortcutLocationActivity::buildScreen(UiScreen& screen) {
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

void ShortcutLocationActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
