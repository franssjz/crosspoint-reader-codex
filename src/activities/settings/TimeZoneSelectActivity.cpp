#include "TimeZoneSelectActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "util/TimeUtils.h"
#include "util/TimeZoneRegistry.h"

namespace fui = freeink::ui;

void TimeZoneSelectActivity::onEnter() {
  UiListActivity::onEnter();

  const int totalItems = static_cast<int>(TimeZoneRegistry::getPresetCount());
  const int currentIndex = TimeZoneRegistry::clampPresetIndex(SETTINGS.timeZonePreset);
  // Open on the current preset; the first build pulls the viewport to it.
  nav.selected = currentIndex;

  rowItems.clear();
  rowItems.reserve(static_cast<size_t>(totalItems));
  for (int i = 0; i < totalItems; ++i) {
    fui::ListItem item;
    item.label = TimeZoneRegistry::getPresetLabel(static_cast<uint8_t>(i));
    if (i == currentIndex) {
      item.value = tr(STR_SELECTED);
    }
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
}

const char* TimeZoneSelectActivity::headerTitle() const { return tr(STR_TIME_ZONE); }

void TimeZoneSelectActivity::activateIndex(const int index) {
  // The activated row leaves this screen; a lingering flash would gray an
  // unrelated element on the next render.
  app.clearTapFlash();
  {
    RenderLock lock(*this);
    SETTINGS.timeZonePreset = TimeZoneRegistry::clampPresetIndex(static_cast<uint8_t>(index));
    SETTINGS.saveToFile();
    TimeUtils::configureTimezone();
  }
  finish();
}

void TimeZoneSelectActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  // Label at the value's font size; long preset names wrap to a second line.
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
