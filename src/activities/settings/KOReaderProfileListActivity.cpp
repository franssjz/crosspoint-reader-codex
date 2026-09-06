#include "KOReaderProfileListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "KOReaderCredentialStore.h"
#include "KOReaderProfileEditActivity.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

// Derives the row cache from the credential store. Called after every
// loadFromFile(), never from buildScreen().
void KOReaderProfileListActivity::rebuildRowItems() {
  const auto& profiles = KOREADER_STORE.getProfiles();
  profileCount = static_cast<int>(profiles.size());
  const int activeIndex = KOREADER_STORE.getActiveIndex();
  const bool canAdd = KOREADER_STORE.canAddProfile();

  rowLabels.clear();
  rowSubtitles.clear();
  rowItems.clear();
  const size_t total = static_cast<size_t>(profileCount) + (canAdd ? 1U : 0U);
  rowLabels.reserve(total);
  rowSubtitles.reserve(total);
  rowItems.reserve(total);

  // Primary label: profile name (falling back to username if unnamed).
  // Subtitle: username (when a name is set). Value: "Active" tag.
  for (const auto& profile : profiles) {
    rowLabels.push_back(profile.name.empty() ? profile.username : profile.name);
    rowSubtitles.push_back(profile.name.empty() ? std::string() : profile.username);
  }
  if (canAdd) {
    rowLabels.emplace_back(tr(STR_ADD_PROFILE));
    rowSubtitles.emplace_back();
  }
  for (size_t i = 0; i < rowLabels.size(); ++i) {
    fui::ListItem item;
    item.label = rowLabels[i].c_str();
    if (!rowSubtitles[i].empty()) item.subtitle = rowSubtitles[i].c_str();
    if (static_cast<int>(i) < profileCount && static_cast<int>(i) == activeIndex) {
      item.value = tr(STR_ACTIVE_PROFILE_TAG);
    }
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
}

void KOReaderProfileListActivity::onEnter() {
  UiListActivity::onEnter();

  // Reload from disk in case profiles were added/removed by a subactivity or the web UI
  KOREADER_STORE.loadFromFile();
  rebuildRowItems();
}

const char* KOReaderProfileListActivity::headerTitle() const { return tr(STR_KOREADER_PROFILES); }

void KOReaderProfileListActivity::activateIndex(const int index) {
  app.clearTapFlash();  // the tap opens the editor

  auto resultHandler = [this](const ActivityResult&) {
    // Reload profile list when returning from the editor (covers add/edit/delete/set-active)
    KOREADER_STORE.loadFromFile();
    RenderLock lock(*this);
    rebuildRowItems();
    nav.selected = 0;
  };

  if (index < profileCount) {
    startActivityForResult(std::make_unique<KOReaderProfileEditActivity>(renderer, mappedInput, index), resultHandler);
  } else if (KOREADER_STORE.canAddProfile()) {
    startActivityForResult(std::make_unique<KOReaderProfileEditActivity>(renderer, mappedInput, -1), resultHandler);
  }
}

void KOReaderProfileListActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (rowItems.empty()) {
    screen.centeredText(tr(STR_NO_ENTRIES), screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  // Small bold titles keep the name/username hierarchy (bold also marks the
  // style caller-owned so the list keeps the small font).
  fui::TextStyle label = screen.theme().smallText;
  label.bold = true;
  props.labelText = label;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}
