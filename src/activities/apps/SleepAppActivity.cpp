#include "SleepAppActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "SleepPreviewActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "util/HeaderDateUtils.h"
#include "util/SleepImageUtils.h"

namespace fui = freeink::ui;

void SleepAppActivity::loadDirectories() {
  directories = SleepImageUtils::listSleepDirectories();
  const int itemCount = static_cast<int>(directories.size()) + 1;
  if (nav.selected >= itemCount) {
    nav.selected = std::max(0, itemCount - 1);
  }
  rebuildRows();
}

// Row 0: sleep order (value = current mode); rows 1..N: directories (value =
// "Selected" on the configured one). Rebuilt on load and after any change.
void SleepAppActivity::rebuildRows() {
  const std::string selectedDirectory = SleepImageUtils::resolveConfiguredSleepDirectory();
  rowLabels.clear();
  rowItems.clear();
  rowLabels.reserve(directories.size());
  rowItems.reserve(directories.size() + 1);

  fui::ListItem order;
  order.label = tr(STR_SLEEP_ORDER);
  order.value =
      SETTINGS.sleepImageOrder == CrossPointSettings::SLEEP_IMAGE_SHUFFLE ? tr(STR_SHUFFLE) : tr(STR_SEQUENTIAL);
  order.actionValue = 0;
  rowItems.push_back(order);

  for (size_t i = 0; i < directories.size(); ++i) {
    rowLabels.push_back(SleepImageUtils::getDirectoryLabel(directories[i]));
    fui::ListItem item;
    item.label = rowLabels.back().c_str();
    item.icon = listIconFor(UIIcon::Folder);
    if (directories[i] == selectedDirectory) item.value = tr(STR_SELECTED);
    item.actionValue = static_cast<int16_t>(i + 1);
    rowItems.push_back(item);
  }
}

void SleepAppActivity::onEnter() {
  UiListActivity::onEnter();
  loadDirectories();
}

void SleepAppActivity::onExit() {
  Activity::onExit();
  rowItems.clear();
  rowLabels.clear();
  directories.clear();
}

void SleepAppActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  nav.selected = index;
  if (index == 0) {
    SETTINGS.sleepImageOrder = (SETTINGS.sleepImageOrder + 1) % CrossPointSettings::SLEEP_IMAGE_ORDER_COUNT;
    SETTINGS.saveToFile();
    {
      RenderLock lock(*this);
      rebuildRows();
    }
    requestUpdate();
    return;
  }
  app.clearTapFlash();
  openDirectory(index);
}

void SleepAppActivity::openDirectory(const int index) {
  if (index <= 0 || index > static_cast<int>(directories.size())) {
    return;
  }

  startActivityForResult(std::make_unique<SleepPreviewActivity>(renderer, mappedInput, directories[index - 1]),
                         [this](const ActivityResult&) {
                           closeRouting();
                           {
                             RenderLock lock(*this);
                             loadDirectories();
                           }
                           requestUpdate();
                         });
}

void SleepAppActivity::drawChrome() { HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_SLEEP)); }

void SleepAppActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), nav.selected == 0 ? tr(STR_SELECT) : tr(STR_OPEN),
                                            tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void SleepAppActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (directories.empty()) {
    fui::TextStyle note = screen.theme().smallText;
    note.align = fui::TextAlign::Center;
    const int16_t lh = screen.target().lineHeight(note.font);
    screen.target().text(screen.takeBottom(lh, static_cast<int16_t>(metrics.verticalSpacing)),
                         tr(STR_NO_SLEEP_DIRECTORIES), note);
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
