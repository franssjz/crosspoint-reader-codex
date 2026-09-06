#include "FlashcardSessionSummaryActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "AppMetricCard.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"

namespace {
constexpr int METRIC_CARD_HEIGHT = 72;
constexpr int METRIC_CARD_GAP = 8;

void drawMetricCard(GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value) {
  AppMetricCard::Options options;
  options.paddingX = 10;
  options.contentInset = 20;
  options.valueLargeY = 12;
  options.labelY = 46;
  options.shrinkValue = false;
  options.labelMode = AppMetricCard::LabelMode::Simple;
  AppMetricCard::draw(renderer, rect, label, value, options);
}
}  // namespace

void FlashcardSessionSummaryActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void FlashcardSessionSummaryActivity::loop() {
  // Read-only summary: Back, Done, or a tap anywhere on the page dismiss it
  // (the X4 Pro has no Back/Confirm front buttons).
  int tapX = 0;
  int tapY = 0;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(tapX, tapY)) {
    finish();
  }
}

void FlashcardSessionSummaryActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int sidePadding = metrics.contentSidePadding;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int cardWidth = (pageWidth - sidePadding * 2 - METRIC_CARD_GAP) / 2;

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_FLASHCARDS), summary.deckTitle.c_str());

  renderer.drawText(UI_10_FONT_ID, sidePadding, contentTop, tr(STR_SESSION_SUMMARY), true, EpdFontFamily::BOLD);

  int currentY = contentTop + 24;
  drawMetricCard(renderer, Rect{sidePadding, currentY, cardWidth, METRIC_CARD_HEIGHT}, tr(STR_REVIEWED),
                 std::to_string(summary.reviewed));
  drawMetricCard(renderer, Rect{sidePadding + cardWidth + METRIC_CARD_GAP, currentY, cardWidth, METRIC_CARD_HEIGHT},
                 tr(STR_CORRECT), std::to_string(summary.correct));
  currentY += METRIC_CARD_HEIGHT + METRIC_CARD_GAP;
  drawMetricCard(renderer, Rect{sidePadding, currentY, cardWidth, METRIC_CARD_HEIGHT}, tr(STR_FAILED),
                 std::to_string(summary.failed));
  drawMetricCard(renderer, Rect{sidePadding + cardWidth + METRIC_CARD_GAP, currentY, cardWidth, METRIC_CARD_HEIGHT},
                 tr(STR_SKIPPED), std::to_string(summary.skipped));
  currentY += METRIC_CARD_HEIGHT + METRIC_CARD_GAP;
  drawMetricCard(renderer, Rect{sidePadding, currentY, cardWidth, METRIC_CARD_HEIGHT}, tr(STR_NEW_SEEN),
                 std::to_string(summary.newSeen));
  drawMetricCard(renderer, Rect{sidePadding + cardWidth + METRIC_CARD_GAP, currentY, cardWidth, METRIC_CARD_HEIGHT},
                 tr(STR_SUCCESS_RATE), std::to_string(summary.successRatePercent) + "%");
  currentY += METRIC_CARD_HEIGHT + METRIC_CARD_GAP + 6;
  drawMetricCard(renderer, Rect{sidePadding, currentY, cardWidth, METRIC_CARD_HEIGHT}, tr(STR_SEEN),
                 std::to_string(summary.seenCards) + "/" + std::to_string(summary.totalCards));
  drawMetricCard(renderer, Rect{sidePadding + cardWidth + METRIC_CARD_GAP, currentY, cardWidth, METRIC_CARD_HEIGHT},
                 tr(STR_DUE_LEFT), std::to_string(summary.dueRemaining));
  currentY += METRIC_CARD_HEIGHT + METRIC_CARD_GAP;
  drawMetricCard(renderer, Rect{sidePadding, currentY, cardWidth, METRIC_CARD_HEIGHT}, tr(STR_MASTERED),
                 std::to_string(summary.masteredCards));
  drawMetricCard(renderer, Rect{sidePadding + cardWidth + METRIC_CARD_GAP, currentY, cardWidth, METRIC_CARD_HEIGHT},
                 tr(STR_SESSIONS), std::to_string(summary.sessionCount));

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DONE), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
