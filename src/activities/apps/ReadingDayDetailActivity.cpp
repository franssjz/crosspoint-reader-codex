#include "ReadingDayDetailActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <string>

#include "AppMetricCard.h"
#include "MappedInputManager.h"
#include "ReadingStatsDetailActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "util/HeaderDateUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr int SUMMARY_CARD_HEIGHT = 70;
constexpr int SUMMARY_GAP = 8;
constexpr int SUB_HEADER_HEIGHT = 34;
constexpr int LIST_TOP_GAP = 10;

std::string getBookTitle(const ReadingBookStats& book) { return book.title.empty() ? book.path : book.title; }

void drawMetricCard(GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value) {
  AppMetricCard::Options options;
  options.labelY = 40;
  AppMetricCard::draw(renderer, rect, label, value, options);
}
}  // namespace

void ReadingDayDetailActivity::refreshEntries() {
  entries = ReadingStatsAnalytics::getBooksReadOnDay(dayOrdinal);
  if (nav.selected >= static_cast<int>(entries.size())) {
    nav.selected = std::max(0, static_cast<int>(entries.size()) - 1);
  }
  rebuildRowItems();
}

// Derives the row cache from entries. Called from refreshEntries(), never
// from buildScreen().
void ReadingDayDetailActivity::rebuildRowItems() {
  rowTitles.clear();
  rowSubtitles.clear();
  rowValues.clear();
  rowItems.clear();
  rowTitles.reserve(entries.size());
  rowSubtitles.reserve(entries.size());
  rowValues.reserve(entries.size());
  rowItems.reserve(entries.size());
  for (const auto& entry : entries) {
    if (entry.book) {
      rowTitles.push_back(getBookTitle(*entry.book));
      rowSubtitles.push_back(entry.book->author.empty() ? std::string(tr(STR_IN_PROGRESS)) : entry.book->author);
    } else {
      rowTitles.emplace_back(tr(STR_NOT_SET));
      rowSubtitles.emplace_back(tr(STR_NOT_SET));
    }
    rowValues.push_back(ReadingStatsAnalytics::formatDurationHm(entry.readingMs));
  }
  for (size_t i = 0; i < entries.size(); ++i) {
    fui::ListItem item;
    item.label = rowTitles[i].c_str();
    item.subtitle = rowSubtitles[i].c_str();
    item.value = rowValues[i].c_str();
    item.icon = listIconFor(UIIcon::Book, 32);  // subtitle rows carry the larger icon
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
}

void ReadingDayDetailActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(entries.size()) || entries[static_cast<size_t>(index)].book == nullptr) {
    return;
  }
  app.clearTapFlash();  // the tap opens the detail screen

  startActivityForResult(std::make_unique<ReadingStatsDetailActivity>(renderer, mappedInput,
                                                                      entries[static_cast<size_t>(index)].book->path),
                         [this](const ActivityResult&) {
                           RenderLock lock(*this);
                           refreshEntries();
                         });
}

void ReadingDayDetailActivity::onEnter() {
  UiListActivity::onEnter();
  refreshEntries();
}

void ReadingDayDetailActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int sidePadding = metrics.contentSidePadding;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int cardWidth = (pageWidth - sidePadding * 2 - SUMMARY_GAP) / 2;
  const std::string dateLabel = ReadingStatsAnalytics::formatDayOrdinalLabel(dayOrdinal);
  const uint64_t totalReadingMs = ReadingStatsAnalytics::buildTimelineDayEntry(dayOrdinal).totalReadingMs;

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_READING_DAY), dateLabel.c_str());

  drawMetricCard(renderer, Rect{sidePadding, contentTop, cardWidth, SUMMARY_CARD_HEIGHT}, tr(STR_TOTAL_TIME),
                 ReadingStatsAnalytics::formatDurationHm(totalReadingMs));
  drawMetricCard(renderer, Rect{sidePadding + cardWidth + SUMMARY_GAP, contentTop, cardWidth, SUMMARY_CARD_HEIGHT},
                 tr(STR_BOOKS_READ), std::to_string(entries.size()));

  const char* topBookLabel = tr(STR_TOP_BOOK);
  const std::string topBookTitle = !entries.empty() && entries.front().book != nullptr
                                       ? getBookTitle(*entries.front().book)
                                       : std::string(tr(STR_NOT_SET));
  const int subHeaderTop = contentTop + SUMMARY_CARD_HEIGHT + metrics.verticalSpacing;
  GUI.drawSubHeader(renderer, Rect{0, subHeaderTop, pageWidth, SUB_HEADER_HEIGHT}, topBookLabel, topBookTitle.c_str());

  listTop = subHeaderTop + SUB_HEADER_HEIGHT + LIST_TOP_GAP;
}

void ReadingDayDetailActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // The list sits under the cards + sub-header drawChrome() painted, above
  // the button hints.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(listTop), 0, static_cast<int16_t>(metrics.buttonHintsHeight + metrics.verticalSpacing), 0});

  if (entries.empty()) {
    screen.centeredText(tr(STR_NO_READING_DAY), screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  fui::TextStyle label = screen.theme().smallText;
  label.bold = true;
  props.labelText = label;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}

void ReadingDayDetailActivity::drawFooter() {
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), entries.empty() ? "" : tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
