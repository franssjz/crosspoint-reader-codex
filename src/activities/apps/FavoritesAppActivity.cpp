#include "FavoritesAppActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "FavoritesBrowserActivity.h"
#include "FavoritesOrderActivity.h"
#include "FavoritesStore.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "util/HeaderDateUtils.h"

namespace fui = freeink::ui;

void FavoritesAppActivity::refreshEntries() { favoriteCount = static_cast<int>(FAVORITES.getBooks().size()); }

void FavoritesAppActivity::onEnter() {
  UiListActivity::onEnter();
  refreshEntries();

  rowItems[0] = fui::ListItem{};
  rowItems[0].label = tr(STR_BROWSE_FILES);
  rowItems[0].subtitle = tr(STR_FAVORITES_BROWSER_DESC);
  rowItems[0].icon = listIconFor(UIIcon::Folder, 32);
  rowItems[0].actionValue = 0;
  rowItems[1] = fui::ListItem{};
  rowItems[1].label = tr(STR_ORDER_FAVORITES);
  rowItems[1].subtitle = tr(STR_FAVORITES_SORT_DESC);
  rowItems[1].actionValue = 1;
}

void FavoritesAppActivity::drawChrome() {
  const std::string headerSubtitle = std::to_string(favoriteCount);
  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_FAVORITES), headerSubtitle.c_str());
}

void FavoritesAppActivity::activateIndex(const int index) {
  // Both rows open a sub-screen; a lingering flash would gray a row on return.
  app.clearTapFlash();
  nav.selected = index;
  auto onReturn = [this](const ActivityResult&) {
    refreshEntries();
    requestUpdate();
  };
  if (index == 0) {
    startActivityForResult(std::make_unique<FavoritesBrowserActivity>(renderer, mappedInput), onReturn);
  } else if (index == 1) {
    startActivityForResult(std::make_unique<FavoritesOrderActivity>(renderer, mappedInput), onReturn);
  }
}

void FavoritesAppActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (favoriteCount == 0) {
    // Empty-state note pinned above the button hints, below the two rows.
    fui::TextStyle note = screen.theme().smallText;
    note.align = fui::TextAlign::Center;
    const int16_t lh = screen.target().lineHeight(note.font);
    screen.target().text(screen.takeBottom(lh, static_cast<int16_t>(metrics.verticalSpacing)), tr(STR_NO_FAVORITES),
                         note);
  }

  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(ACTION_COUNT);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  fui::TextStyle label = screen.theme().smallText;
  label.bold = true;
  props.labelText = label;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}
