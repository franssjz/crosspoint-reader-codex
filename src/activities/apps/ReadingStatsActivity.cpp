#include "ReadingStatsActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>

#include "AppMetricCard.h"
#include "ReadingStatsDetailActivity.h"
#include "ReadingStatsExtendedActivity.h"
#include "ReadingStatsStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/ReadingStatsAnalytics.h"

namespace fui = freeink::ui;

namespace {
constexpr unsigned long BOOK_LONG_PRESS_MS = 1000;
constexpr int SUMMARY_CARD_HEIGHT = 76;
constexpr int SUMMARY_GAP = 10;
constexpr int LIST_HEADER_HEIGHT = 34;
constexpr int LIST_HEADER_BOTTOM_GAP = 10;

std::string getBookTitle(const ReadingBookStats& book) { return book.title.empty() ? book.path : book.title; }

const char* getBookSubtitle(const ReadingBookStats& book) {
  if (!book.author.empty()) {
    return book.author.c_str();
  }
  return book.completed ? tr(STR_DONE) : tr(STR_IN_PROGRESS);
}

void drawMetricCard(GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value,
                    const bool showCheck = false) {
  AppMetricCard::Options options;
  options.showCheck = showCheck;
  AppMetricCard::draw(renderer, rect, label, value, options);
}
}  // namespace

void ReadingStatsActivity::onEnter() {
  UiListActivity::onEnter();
  renderer.requestNextRefresh(HalDisplay::HALF_REFRESH);
  rebuildRows();
  nav.selected = READING_STATS.getBooks().empty() ? 0 : 1;
  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  waitForBackRelease = false;
  createDueAutoBackupWithFeedback();
}

void ReadingStatsActivity::onExit() {
  renderer.requestNextRefresh(HalDisplay::HALF_REFRESH);
  Activity::onExit();
  rowItems.clear();
  rowValues.clear();
}

// Row 0: "More details"; rows 1..N: the started books with "progress | time"
// in the value slot. Rebuilt whenever the stats store may have changed.
void ReadingStatsActivity::rebuildRows() {
  const auto& books = READING_STATS.getBooks();
  bookCount = static_cast<int>(books.size());
  rowValues.clear();
  rowItems.clear();
  rowValues.reserve(books.size() + 1);
  rowItems.reserve(books.size() + 1);

  fui::ListItem details;
  details.label = tr(STR_MORE_DETAILS);
  details.icon = listIconFor(UIIcon::Library, 32);
  details.actionValue = 0;
  rowItems.push_back(details);
  rowValues.emplace_back();

  for (size_t i = 0; i < books.size(); ++i) {
    const auto& book = books[i];
    rowValues.push_back(std::to_string(book.lastProgressPercent) + "% | " +
                        ReadingStatsAnalytics::formatDurationHm(book.totalReadingMs));
    fui::ListItem item;
    item.label = book.title.empty() ? book.path.c_str() : book.title.c_str();
    item.subtitle = getBookSubtitle(book);
    item.value = rowValues.back().c_str();
    item.icon = listIconFor(UIIcon::Book, 32);
    item.actionValue = static_cast<int16_t>(i + 1);
    rowItems.push_back(item);
  }
}

// Y where the list band starts: below the metric cards and the sub-header.
int ReadingStatsActivity::listTop() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int summaryTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listHeaderTop = summaryTop + SUMMARY_CARD_HEIGHT * 3 + SUMMARY_GAP * 2 + metrics.verticalSpacing;
  return listHeaderTop + LIST_HEADER_HEIGHT + LIST_HEADER_BOTTOM_GAP;
}

bool ReadingStatsActivity::handleCustomInput() {
  if (waitForBackRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Back) &&
        !mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      waitForBackRelease = false;
    }
    return true;
  }

  if (waitForConfirmRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
      return true;
    }
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      waitForConfirmRelease = false;
    }
    return true;
  }
  return false;
}

bool ReadingStatsActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (nav.selected > 0 && mappedInput.getHeldTime() >= BOOK_LONG_PRESS_MS) {
      confirmRemoveBook(nav.selected);
    } else {
      activateIndex(nav.selected);
    }
    return true;
  }
  return UiListActivity::handleButtons();
}

void ReadingStatsActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  openEntry(index);
}

void ReadingStatsActivity::onRowLongPress(const int index) {
  if (index <= 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  confirmRemoveBook(index);
}

void ReadingStatsActivity::openEntry(const int index) {
  const auto& books = READING_STATS.getBooks();
  auto onReturn = [this](const ActivityResult&) {
    closeRouting();
    {
      RenderLock lock(*this);
      rebuildRows();
      nav.selected = std::min(nav.selected, std::max(0, listCount() - 1));
      nav.follow(listCount());
    }
    guardBackReturn();
    requestUpdate();
  };
  if (index == 0) {
    startActivityForResult(std::make_unique<ReadingStatsExtendedActivity>(renderer, mappedInput), onReturn);
    return;
  }
  const int bookIndex = index - 1;
  if (bookIndex < 0 || bookIndex >= static_cast<int>(books.size())) {
    return;
  }

  startActivityForResult(std::make_unique<ReadingStatsDetailActivity>(renderer, mappedInput, books[bookIndex].path),
                         onReturn);
}

void ReadingStatsActivity::confirmRemoveBook(const int index) {
  const auto& books = READING_STATS.getBooks();
  const int bookIndex = index - 1;
  if (bookIndex < 0 || bookIndex >= static_cast<int>(books.size())) {
    return;
  }

  const ReadingBookStats selectedBook = books[bookIndex];
  const int currentSelection = index;
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_STATS_ENTRY),
                                                                getBookTitle(selectedBook)),
                         [this, selectedBook, currentSelection](const ActivityResult& result) {
                           if (!result.isCancelled && READING_STATS.removeBook(selectedBook.path)) {
                             closeRouting();
                             RenderLock lock(*this);
                             rebuildRows();
                             if (bookCount == 0) {
                               nav.selected = 0;
                             } else if (currentSelection > bookCount) {
                               nav.selected = bookCount;
                             } else {
                               nav.selected = currentSelection;
                             }
                             nav.follow(listCount());
                           }

                           guardBackReturn();
                           requestUpdate(true);
                         });
}

void ReadingStatsActivity::guardBackReturn() { waitForBackRelease = true; }

void ReadingStatsActivity::showTransientPopup(const char* message, const int progress, const unsigned long delayMs) {
  requestUpdateAndWait();

  {
    RenderLock lock(*this);
    const Rect popupRect = GUI.drawPopup(renderer, message);
    if (progress >= 0) {
      GUI.fillPopupProgress(renderer, popupRect, progress);
    }
  }

  if (delayMs > 0) {
    delay(delayMs);
  }
}

void ReadingStatsActivity::createDueAutoBackupWithFeedback() {
  if (!READING_STATS.isAutoBackupDue()) {
    return;
  }

  showTransientPopup(tr(STR_READING_STATS_BACKUP_RUNNING), 20, 120);
  const bool backupReady = READING_STATS.createDueAutoBackup();
  showTransientPopup(backupReady ? tr(STR_READING_STATS_BACKUP_DONE) : tr(STR_READING_STATS_BACKUP_PENDING),
                     backupReady ? 100 : -1, backupReady ? 350 : 700);
  requestUpdate(true);
}

void ReadingStatsActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int sidePadding = metrics.contentSidePadding;
  const int cardWidth = (pageWidth - sidePadding * 2 - SUMMARY_GAP) / 2;
  const int rightX = sidePadding + cardWidth + SUMMARY_GAP;
  const int summaryTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int row2 = summaryTop + SUMMARY_CARD_HEIGHT + SUMMARY_GAP;
  const int row3 = summaryTop + (SUMMARY_CARD_HEIGHT + SUMMARY_GAP) * 2;
  const uint64_t todayReadingMs = READING_STATS.getTodayReadingMs();
  const std::string dailyGoalValue = ReadingStatsAnalytics::formatDurationHm(todayReadingMs) + " / " +
                                     ReadingStatsAnalytics::formatDurationHm(getDailyReadingGoalMs());

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_READING_STATS));

  drawMetricCard(renderer, Rect{sidePadding, summaryTop, cardWidth, SUMMARY_CARD_HEIGHT}, tr(STR_STREAK),
                 std::to_string(READING_STATS.getCurrentStreakDays()));
  drawMetricCard(renderer, Rect{rightX, summaryTop, cardWidth, SUMMARY_CARD_HEIGHT}, tr(STR_MAX_STREAK),
                 std::to_string(READING_STATS.getMaxStreakDays()));
  drawMetricCard(renderer, Rect{sidePadding, row2, cardWidth, SUMMARY_CARD_HEIGHT}, tr(STR_DAILY_GOAL), dailyGoalValue,
                 todayReadingMs >= getDailyReadingGoalMs());
  drawMetricCard(renderer, Rect{rightX, row2, cardWidth, SUMMARY_CARD_HEIGHT}, tr(STR_READING_TIME),
                 ReadingStatsAnalytics::formatDurationHm(READING_STATS.getTotalReadingMs()));
  drawMetricCard(renderer, Rect{sidePadding, row3, cardWidth, SUMMARY_CARD_HEIGHT}, tr(STR_BOOKS_FINISHED),
                 std::to_string(READING_STATS.getBooksFinishedCount()));
  drawMetricCard(renderer, Rect{rightX, row3, cardWidth, SUMMARY_CARD_HEIGHT}, tr(STR_BOOKS_STARTED),
                 std::to_string(READING_STATS.getBooksStartedCount()));

  // Sub-header: started-book count on the left, list page on the right.
  const int listHeaderTop = row3 + SUMMARY_CARD_HEIGHT + metrics.verticalSpacing;
  const int count = listCount();
  const int pageRows = std::max(1, nav.pageRowsFor(count));
  const int totalPages = std::max(1, (count + pageRows - 1) / pageRows);
  const int currentPage = std::clamp(nav.selected, 0, std::max(0, count - 1)) / pageRows + 1;
  const std::string bookCountLabel = std::to_string(currentPage) + "/" + std::to_string(totalPages);
  const std::string startedBooksLabel =
      std::string(tr(STR_STARTED_BOOKS)) + " (" + std::to_string(READING_STATS.getBooksStartedCount()) + ")";
  GUI.drawSubHeader(renderer, Rect{0, listHeaderTop, pageWidth, LIST_HEADER_HEIGHT}, startedBooksLabel.c_str(),
                    bookCountLabel.c_str());
}

void ReadingStatsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // The list occupies the band below the cards and sub-header (chrome).
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(listTop()), 0, static_cast<int16_t>(metrics.buttonHintsHeight), 0});

  if (bookCount == 0) {
    fui::TextStyle note = screen.theme().smallText;
    note.align = fui::TextAlign::Center;
    const int16_t lh = screen.target().lineHeight(note.font);
    screen.target().text(screen.takeBottom(lh, static_cast<int16_t>(metrics.verticalSpacing)), tr(STR_NO_READING_STATS),
                         note);
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;  // tap opens, long-press removes a book
  props.valueInset = 8;
  fui::TextStyle label = screen.theme().smallText;
  label.bold = true;
  props.labelText = label;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}
