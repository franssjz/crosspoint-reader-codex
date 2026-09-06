#include "FlashcardsAppActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "FlashcardBrowserActivity.h"
#include "FlashcardRecentsActivity.h"
#include "FlashcardSettingsActivity.h"
#include "FlashcardStatsActivity.h"
#include "FlashcardsStore.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "util/HeaderDateUtils.h"

namespace fui = freeink::ui;

namespace {
bool hasStatsToShow(const FlashcardDeckRecord& record) {
  return record.sessionCount > 0 || record.seenCards > 0 || record.totalReviewed > 0 || record.totalCorrect > 0 ||
         record.totalWrong > 0 || record.totalSkipped > 0 || record.lastReviewedAt > 0;
}

std::string getSettingsSubtitle() {
  std::string studyModeLabel;
  switch (SETTINGS.flashcardStudyMode) {
    case CrossPointSettings::FLASHCARD_STUDY_DUE:
      studyModeLabel = tr(STR_DUE);
      break;
    case CrossPointSettings::FLASHCARD_STUDY_INFINITE:
      studyModeLabel = tr(STR_RANDOM);
      break;
    case CrossPointSettings::FLASHCARD_STUDY_SEQUENTIAL:
      studyModeLabel = tr(STR_SEQUENTIAL);
      break;
    case CrossPointSettings::FLASHCARD_STUDY_SCHEDULED:
    default:
      studyModeLabel = tr(STR_SCHEDULED);
      break;
  }

  if (SETTINGS.flashcardStudyMode == CrossPointSettings::FLASHCARD_STUDY_INFINITE ||
      SETTINGS.flashcardStudyMode == CrossPointSettings::FLASHCARD_STUDY_SEQUENTIAL) {
    return studyModeLabel;
  }

  return studyModeLabel + " | " +
         (SETTINGS.flashcardSessionSize == CrossPointSettings::FLASHCARD_SESSION_ALL
              ? std::string(tr(STR_ALL))
              : std::to_string(SETTINGS.flashcardSessionSize == CrossPointSettings::FLASHCARD_SESSION_10   ? 10
                               : SETTINGS.flashcardSessionSize == CrossPointSettings::FLASHCARD_SESSION_20 ? 20
                               : SETTINGS.flashcardSessionSize == CrossPointSettings::FLASHCARD_SESSION_30 ? 30
                                                                                                           : 50));
}
}  // namespace

void FlashcardsAppActivity::refreshCounts() {
  recentCount = static_cast<int>(FLASHCARDS.getRecentDecks().size());
  deckCount = 0;
  for (const auto& record : FLASHCARDS.getKnownDecks()) {
    if (hasStatsToShow(record)) {
      deckCount++;
    }
  }
  // Assign into the existing strings; the rows keep pointing at them.
  rowSubtitles[0] = tr(STR_FLASHCARDS_OPEN_DESC);
  rowSubtitles[1] = std::to_string(recentCount);
  rowSubtitles[2] = std::to_string(deckCount);
  rowSubtitles[3] = getSettingsSubtitle();
  for (int i = 0; i < ACTION_COUNT; ++i) {
    rowItems[i].subtitle = rowSubtitles[i].c_str();
  }
}

void FlashcardsAppActivity::activateIndex(const int index) {
  app.clearTapFlash();  // every row opens a sub-screen
  nav.selected = index;
  std::unique_ptr<Activity> activity;
  switch (index) {
    case 0:
      activity = std::make_unique<FlashcardBrowserActivity>(renderer, mappedInput);
      break;
    case 1:
      activity = std::make_unique<FlashcardRecentsActivity>(renderer, mappedInput);
      break;
    case 2:
      activity = std::make_unique<FlashcardStatsActivity>(renderer, mappedInput);
      break;
    default:
      activity = std::make_unique<FlashcardSettingsActivity>(renderer, mappedInput);
      break;
  }

  startActivityForResult(std::move(activity), [this](const ActivityResult&) {
    RenderLock lock(*this);
    refreshCounts();
    requestUpdate();
  });
}

void FlashcardsAppActivity::onEnter() {
  UiListActivity::onEnter();
  renderer.requestNextRefresh(HalDisplay::HALF_REFRESH);

  const StrId labels[ACTION_COUNT] = {StrId::STR_OPEN, StrId::STR_RECENTS, StrId::STR_STATISTICS,
                                      StrId::STR_SETTINGS_TITLE};
  const UIIcon icons[ACTION_COUNT] = {UIIcon::Folder, UIIcon::Recent, UIIcon::Library, UIIcon::Settings};
  for (int i = 0; i < ACTION_COUNT; ++i) {
    rowItems[i] = fui::ListItem{};
    rowItems[i].label = I18N.get(labels[i]);
    rowItems[i].icon = listIconFor(icons[i], 32);
    rowItems[i].actionValue = static_cast<int16_t>(i);
  }
  refreshCounts();
}

void FlashcardsAppActivity::onExit() {
  renderer.requestNextRefresh(HalDisplay::HALF_REFRESH);
  Activity::onExit();
}

void FlashcardsAppActivity::drawChrome() {
  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_FLASHCARDS), std::to_string(deckCount).c_str());
}

void FlashcardsAppActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

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
