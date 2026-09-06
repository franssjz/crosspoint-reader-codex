#include "FlashcardDeckStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "AppMetricCard.h"
#include "FlashcardReviewActivity.h"
#include "FlashcardSessionSummaryActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/TimeUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr int METRIC_CARD_HEIGHT = 74;
constexpr int METRIC_CARD_GAP = 8;
constexpr int METRIC_CARD_ROWS = 4;

std::string formatDateOrFallback(const uint32_t timestamp) {
  const std::string date = TimeUtils::formatDate(timestamp);
  return date.empty() ? std::string(tr(STR_NOT_SET)) : date;
}

void drawMetricCard(GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value) {
  AppMetricCard::Options options;
  options.paddingX = 10;
  options.contentInset = 20;
  options.valueLargeY = 12;
  options.labelY = 48;
  options.shrinkValue = false;
  options.labelMode = AppMetricCard::LabelMode::Simple;
  AppMetricCard::draw(renderer, rect, label, value, options);
}
}  // namespace

void FlashcardDeckStatsActivity::loadDeckData() {
  errorMessage.clear();
  if (!FLASHCARDS.loadDeck(deckPath, deck, &errorMessage)) {
    loaded = false;
    return;
  }
  if (!FLASHCARDS.loadDeckProgress(deck, progress, &errorMessage)) {
    loaded = false;
    deck.cards.clear();
    return;
  }
  metrics = FLASHCARDS.buildMetrics(deck, progress);
  loaded = true;
}

void FlashcardDeckStatsActivity::onEnter() {
  UiListActivity::onEnter();
  loadDeckData();
  openRow = fui::ListItem{};
  openRow.label = tr(STR_OPEN);
  openRow.icon = listIconFor(UIIcon::Text);
  openRow.actionValue = 0;
}

// Y where the metric-card grid ends (the list sits below it).
int FlashcardDeckStatsActivity::cardsBottom() const {
  const auto& metricsUi = UITheme::getInstance().getMetrics();
  const int contentTop = metricsUi.topPadding + metricsUi.headerHeight + metricsUi.verticalSpacing;
  return contentTop + METRIC_CARD_ROWS * METRIC_CARD_HEIGHT + (METRIC_CARD_ROWS - 1) * METRIC_CARD_GAP;
}

void FlashcardDeckStatsActivity::activateIndex(const int index) {
  if (!loaded || index != 0) return;
  app.clearTapFlash();
  startActivityForResult(
      std::make_unique<FlashcardReviewActivity>(renderer, mappedInput, deckPath), [this](const ActivityResult& result) {
        {
          RenderLock lock(*this);
          loadDeckData();
        }
        if (const auto* session = std::get_if<FlashcardSessionResult>(&result.data)) {
          startActivityForResult(std::make_unique<FlashcardSessionSummaryActivity>(renderer, mappedInput, *session),
                                 [this](const ActivityResult&) {
                                   {
                                     RenderLock lock(*this);
                                     loadDeckData();
                                   }
                                   requestUpdate();
                                 });
          return;
        }
        requestUpdate();
      });
}

void FlashcardDeckStatsActivity::drawChrome() {
  const auto& metricsUi = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int sidePadding = metricsUi.contentSidePadding;
  const int contentTop = metricsUi.topPadding + metricsUi.headerHeight + metricsUi.verticalSpacing;

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_FLASHCARDS), loaded ? deck.title.c_str() : tr(STR_STATISTICS));
  if (!loaded) return;

  const int cardWidth = (pageWidth - sidePadding * 2 - METRIC_CARD_GAP) / 2;
  const int rightX = sidePadding + cardWidth + METRIC_CARD_GAP;
  int currentY = contentTop;
  drawMetricCard(renderer, Rect{sidePadding, currentY, cardWidth, METRIC_CARD_HEIGHT}, tr(STR_TOTAL_CARDS),
                 std::to_string(metrics.totalCards));
  drawMetricCard(renderer, Rect{rightX, currentY, cardWidth, METRIC_CARD_HEIGHT}, tr(STR_SEEN),
                 std::to_string(metrics.seenCards));
  currentY += METRIC_CARD_HEIGHT + METRIC_CARD_GAP;
  drawMetricCard(renderer, Rect{sidePadding, currentY, cardWidth, METRIC_CARD_HEIGHT}, tr(STR_UNSEEN),
                 std::to_string(metrics.unseenCards));
  drawMetricCard(renderer, Rect{rightX, currentY, cardWidth, METRIC_CARD_HEIGHT}, tr(STR_DUE),
                 std::to_string(metrics.dueCards));
  currentY += METRIC_CARD_HEIGHT + METRIC_CARD_GAP;
  drawMetricCard(renderer, Rect{sidePadding, currentY, cardWidth, METRIC_CARD_HEIGHT}, tr(STR_MASTERED),
                 std::to_string(metrics.masteredCards));
  drawMetricCard(renderer, Rect{rightX, currentY, cardWidth, METRIC_CARD_HEIGHT}, tr(STR_SUCCESS_RATE),
                 std::to_string(metrics.successRatePercent) + "%");
  currentY += METRIC_CARD_HEIGHT + METRIC_CARD_GAP;
  drawMetricCard(renderer, Rect{sidePadding, currentY, cardWidth, METRIC_CARD_HEIGHT}, tr(STR_SESSIONS),
                 std::to_string(metrics.sessionCount));
  drawMetricCard(renderer, Rect{rightX, currentY, cardWidth, METRIC_CARD_HEIGHT}, tr(STR_LAST_REVIEW),
                 formatDateOrFallback(metrics.lastReviewedAt));
}

void FlashcardDeckStatsActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), loaded ? tr(STR_OPEN) : "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void FlashcardDeckStatsActivity::buildScreen(UiScreen& screen) {
  const auto& metricsUi = UITheme::getInstance().getMetrics();
  if (!loaded) {
    screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metricsUi.topPadding + metricsUi.headerHeight),
                                                  0, static_cast<int16_t>(metricsUi.buttonHintsHeight), 0});
    fui::TextStyle message = screen.theme().bodyText;
    message.maxLines = 3;
    screen.centeredText(errorMessage.empty() ? tr(STR_FLASHCARDS_INVALID_DECK) : errorMessage.c_str(), message);
    return;
  }

  // The list occupies the band below the metric cards (drawn as chrome).
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(cardsBottom()), 0, static_cast<int16_t>(metricsUi.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metricsUi.verticalSpacing));

  fui::ListProps props;
  props.items = &openRow;
  props.count = 1;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // Confirm stays in loop()
  props.labelText = screen.theme().smallText;
  props.labelText.bold = true;
  syncListViewport(screen, props);
  screen.list(props);
}
