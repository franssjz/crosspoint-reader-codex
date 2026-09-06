#include "FlashcardRecentsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "FlashcardReviewActivity.h"
#include "FlashcardSessionSummaryActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "util/HeaderDateUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr unsigned long DELETE_RECENT_FLASHCARD_HOLD_MS = 1000;

std::string buildDeckSubtitle(const FlashcardDeckRecord& record) {
  const std::string progress = std::to_string(record.seenCards) + "/" + std::to_string(record.totalCards);
  const int answered = static_cast<int>(record.totalCorrect + record.totalWrong);
  const int accuracy = answered > 0 ? static_cast<int>((record.totalCorrect * 100) / answered) : 0;
  return progress + " | " + std::to_string(accuracy) + "%";
}
}  // namespace

void FlashcardRecentsActivity::reloadDecks() {
  decks = FLASHCARDS.getRecentDecks();
  if (decks.empty()) {
    nav.selected = 0;
  } else {
    nav.selected = std::clamp(nav.selected, 0, static_cast<int>(decks.size()) - 1);
  }
  rebuildRowItems();
}

void FlashcardRecentsActivity::rebuildRowItems() {
  rowSubtitles.clear();
  rowItems.clear();
  rowSubtitles.reserve(decks.size());
  rowItems.reserve(decks.size());
  for (size_t i = 0; i < decks.size(); ++i) {
    rowSubtitles.push_back(buildDeckSubtitle(decks[i]));
    fui::ListItem item;
    item.label = decks[i].title.c_str();
    item.subtitle = rowSubtitles.back().c_str();
    item.icon = listIconFor(UIIcon::Text, 32);
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
}

void FlashcardRecentsActivity::openDeck(const int index) {
  if (index < 0 || index >= static_cast<int>(decks.size())) return;
  const auto selectedDeck = decks[index];
  startActivityForResult(std::make_unique<FlashcardReviewActivity>(renderer, mappedInput, selectedDeck.path),
                         [this](const ActivityResult& result) {
                           {
                             RenderLock lock(*this);
                             reloadDecks();
                           }
                           if (const auto* session = std::get_if<FlashcardSessionResult>(&result.data)) {
                             startActivityForResult(
                                 std::make_unique<FlashcardSessionSummaryActivity>(renderer, mappedInput, *session),
                                 [this](const ActivityResult&) {
                                   {
                                     RenderLock lock(*this);
                                     reloadDecks();
                                   }
                                   requestUpdate();
                                 });
                             return;
                           }
                           requestUpdate();
                         });
}

void FlashcardRecentsActivity::confirmRemoveDeck(const int index) {
  if (index < 0 || index >= static_cast<int>(decks.size())) return;
  const FlashcardDeckRecord selectedDeck = decks[index];
  const size_t currentSelection = static_cast<size_t>(index);
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_FROM_RECENTS), selectedDeck.title),
      [this, selectedDeck, currentSelection](const ActivityResult& result) {
        if (!result.isCancelled) {
          FLASHCARDS.removeRecentDeck(selectedDeck.deckId);
          closeRouting();
          RenderLock lock(*this);
          reloadDecks();
          if (decks.empty()) {
            nav.selected = 0;
          } else if (currentSelection >= decks.size()) {
            nav.selected = static_cast<int>(decks.size()) - 1;
          } else {
            nav.selected = static_cast<int>(currentSelection);
          }
          nav.follow(listCount());
        }
        requestUpdate(true);
      });
}

void FlashcardRecentsActivity::onEnter() {
  UiListActivity::onEnter();
  reloadDecks();
}

void FlashcardRecentsActivity::onExit() {
  Activity::onExit();
  rowItems.clear();
  rowSubtitles.clear();
  decks.clear();
}

bool FlashcardRecentsActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (nav.selected >= 0 && nav.selected < listCount()) {
      if (mappedInput.getHeldTime() >= DELETE_RECENT_FLASHCARD_HOLD_MS) {
        confirmRemoveDeck(nav.selected);
      } else {
        activateIndex(nav.selected);
      }
    }
    return true;
  }
  return UiListActivity::handleButtons();
}

void FlashcardRecentsActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  openDeck(index);
}

void FlashcardRecentsActivity::onRowLongPress(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  confirmRemoveDeck(index);
}

void FlashcardRecentsActivity::drawChrome() {
  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_FLASHCARDS), tr(STR_RECENTS));
}

void FlashcardRecentsActivity::drawFooter() {
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), decks.empty() ? "" : tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void FlashcardRecentsActivity::buildScreen(UiScreen& screen) {
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
  props.inputMask = fui::InputTouch | fui::InputLongPress;  // tap opens, long-press removes
  fui::TextStyle label = screen.theme().smallText;
  label.bold = true;
  props.labelText = label;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}
