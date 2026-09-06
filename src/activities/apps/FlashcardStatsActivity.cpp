#include "FlashcardStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "FlashcardDeckStatsActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "util/HeaderDateUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr unsigned long DELETE_FLASHCARD_STATS_HOLD_MS = 1000;

bool hasStatsToShow(const FlashcardDeckRecord& record) {
  return record.sessionCount > 0 || record.seenCards > 0 || record.totalReviewed > 0 || record.totalCorrect > 0 ||
         record.totalWrong > 0 || record.totalSkipped > 0 || record.lastReviewedAt > 0;
}

std::string buildDeckSubtitle(const FlashcardDeckRecord& record) {
  const int answered = static_cast<int>(record.totalCorrect + record.totalWrong);
  const int accuracy = answered > 0 ? static_cast<int>((record.totalCorrect * 100) / answered) : 0;
  return std::to_string(record.seenCards) + "/" + std::to_string(record.totalCards) + " | " + std::to_string(accuracy) +
         "%";
}
}  // namespace

void FlashcardStatsActivity::reloadDecks() {
  decks.clear();
  for (const auto& record : FLASHCARDS.getKnownDecks()) {
    if (hasStatsToShow(record)) {
      decks.push_back(record);
    }
  }
  std::sort(decks.begin(), decks.end(), [](const FlashcardDeckRecord& lhs, const FlashcardDeckRecord& rhs) {
    if (lhs.lastOpenedAt != rhs.lastOpenedAt) {
      return lhs.lastOpenedAt > rhs.lastOpenedAt;
    }
    return lhs.title < rhs.title;
  });

  if (decks.empty()) {
    nav.selected = 0;
  } else {
    nav.selected = std::clamp(nav.selected, 0, static_cast<int>(decks.size()) - 1);
  }
  rebuildRowItems();
}

void FlashcardStatsActivity::rebuildRowItems() {
  rowSubtitles.clear();
  rowItems.clear();
  rowSubtitles.reserve(decks.size());
  rowItems.reserve(decks.size());
  for (size_t i = 0; i < decks.size(); ++i) {
    rowSubtitles.push_back(buildDeckSubtitle(decks[i]));
    fui::ListItem item;
    item.label = decks[i].title.c_str();
    item.subtitle = rowSubtitles.back().c_str();
    item.icon = listIconFor(UIIcon::Library, 32);
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
}

void FlashcardStatsActivity::onEnter() {
  UiListActivity::onEnter();
  reloadDecks();
}

void FlashcardStatsActivity::onExit() {
  Activity::onExit();
  rowItems.clear();
  rowSubtitles.clear();
  decks.clear();
}

void FlashcardStatsActivity::confirmResetDeck(const int index) {
  if (index < 0 || index >= static_cast<int>(decks.size())) return;
  const auto selectedDeck = decks[index];
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_STATS_ENTRY), selectedDeck.title),
      [this, deckId = selectedDeck.deckId](const ActivityResult& result) {
        if (!result.isCancelled) {
          FLASHCARDS.resetDeckStats(deckId);
        }
        closeRouting();
        {
          RenderLock lock(*this);
          reloadDecks();
          nav.follow(listCount());
        }
        requestUpdate();
      });
}

bool FlashcardStatsActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (nav.selected >= 0 && nav.selected < listCount()) {
      if (mappedInput.getHeldTime() >= DELETE_FLASHCARD_STATS_HOLD_MS) {
        confirmResetDeck(nav.selected);
      } else {
        activateIndex(nav.selected);
      }
    }
    return true;
  }
  return UiListActivity::handleButtons();
}

void FlashcardStatsActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  startActivityForResult(std::make_unique<FlashcardDeckStatsActivity>(renderer, mappedInput, decks[index].path),
                         [this](const ActivityResult&) {
                           {
                             RenderLock lock(*this);
                             reloadDecks();
                           }
                           requestUpdate();
                         });
}

void FlashcardStatsActivity::onRowLongPress(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  confirmResetDeck(index);
}

void FlashcardStatsActivity::drawChrome() {
  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_FLASHCARDS), tr(STR_STATISTICS));
}

void FlashcardStatsActivity::drawFooter() {
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), decks.empty() ? "" : tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void FlashcardStatsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (decks.empty()) {
    screen.centeredText(tr(STR_NO_ENTRIES), screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;  // tap opens, long-press resets
  fui::TextStyle label = screen.theme().smallText;
  label.bold = true;
  props.labelText = label;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}
