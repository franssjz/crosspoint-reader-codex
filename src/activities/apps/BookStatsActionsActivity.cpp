#include "BookStatsActionsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>
#include <variant>

#include "AchievementsStore.h"
#include "BookReadingAdjustmentActivity.h"
#include "ReadingDateSelectionActivity.h"
#include "ReadingStatsStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/TimeUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr int ACTION_ADJUST_READING_TIME = 0;
constexpr int ACTION_MODIFY_START_DATE = 1;
constexpr int ACTION_RESET_BOOK_STATS = 2;

uint32_t getInitialStartDateDayOrdinal(const std::string& bookPath) {
  const auto* book = READING_STATS.findBook(bookPath);
  if (book == nullptr) {
    return 0;
  }
  if (TimeUtils::isClockValid(book->firstReadAt)) {
    return TimeUtils::getLocalDayOrdinal(book->firstReadAt);
  }
  return book->readingDays.empty() ? 0 : book->readingDays.front().dayOrdinal;
}
}  // namespace

void BookStatsActionsActivity::onEnter() {
  UiListActivity::onEnter();
  nav.selected = ACTION_ADJUST_READING_TIME;
  startDateApplyFailed = false;
  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);

  const StrId labels[ACTION_COUNT] = {StrId::STR_ADJUST_READING_TIME, StrId::STR_MODIFY_START_DATE,
                                      StrId::STR_RESET_THIS_BOOK_STATS};
  for (int i = 0; i < ACTION_COUNT; ++i) {
    rowItems[i] = fui::ListItem{};
    rowItems[i].label = I18N.get(labels[i]);
    rowItems[i].actionValue = static_cast<int16_t>(i);
  }
}

void BookStatsActionsActivity::openAdjustment() {
  startActivityForResult(std::make_unique<BookReadingAdjustmentActivity>(renderer, mappedInput, bookPath, bookTitle),
                         [this](const ActivityResult&) {
                           ActivityResult result;
                           setResult(std::move(result));
                           finish();
                         });
}

void BookStatsActionsActivity::openStartDateSelection() {
  startDateApplyFailed = false;
  startActivityForResult(
      std::make_unique<ReadingDateSelectionActivity>(renderer, mappedInput, getInitialStartDateDayOrdinal(bookPath)),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          if (const auto* page = std::get_if<PageResult>(&result.data);
              page != nullptr && READING_STATS.setBookFirstReadDate(bookPath, page->page)) {
            ActivityResult updatedResult;
            setResult(std::move(updatedResult));
            finish();
            return;
          }
          startDateApplyFailed = true;
        }

        guardConfirmReturn();
        requestUpdate(true);
      });
}

void BookStatsActionsActivity::confirmResetBookStats() {
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_RESET_THIS_BOOK_STATS_CONFIRM), bookTitle),
      [this](const ActivityResult& result) {
        if (!result.isCancelled && READING_STATS.removeBook(bookPath)) {
          ACHIEVEMENTS.rebuildProgressFromCurrentStats();
          ActivityResult resetResult;
          resetResult.data = MenuResult{RESULT_RESET_BOOK_STATS};
          setResult(std::move(resetResult));
          finish();
          return;
        }

        guardConfirmReturn();
        requestUpdate(true);
      });
}

void BookStatsActionsActivity::guardConfirmReturn() {
  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm) ||
                          mappedInput.wasReleased(MappedInputManager::Button::Confirm);
}

bool BookStatsActionsActivity::handleCustomInput() {
  if (waitForConfirmRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
      return true;
    }
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
        !mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      waitForConfirmRelease = false;
    }
    return true;
  }
  // Moving the selection clears the start-date failure hint (not consumed).
  if (startDateApplyFailed && (mappedInput.wasReleased(MappedInputManager::Button::NavNext) ||
                               mappedInput.wasReleased(MappedInputManager::Button::NavPrevious))) {
    startDateApplyFailed = false;
  }
  return false;
}

void BookStatsActionsActivity::activateIndex(const int index) {
  if (index < 0 || index >= ACTION_COUNT) return;
  app.clearTapFlash();  // every row opens a sub-screen
  nav.selected = index;
  if (index == ACTION_ADJUST_READING_TIME) {
    openAdjustment();
  } else if (index == ACTION_MODIFY_START_DATE) {
    openStartDateSelection();
  } else {
    confirmResetBookStats();
  }
}

void BookStatsActionsActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const std::string subtitle = renderer.truncatedText(UI_10_FONT_ID, bookTitle.c_str(),
                                                      renderer.getScreenWidth() - metrics.contentSidePadding * 2);
  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_BOOK_STATS_ACTIONS), subtitle.c_str());
}

void BookStatsActionsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(ACTION_COUNT);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  const int16_t rowHeight = props.rowHeight > 0 ? props.rowHeight : screen.theme().rowHeight;
  const int16_t rowGap = props.rowGap >= 0 ? props.rowGap : screen.theme().listRowGap;
  const int16_t listHeight = static_cast<int16_t>(rowHeight * ACTION_COUNT + rowGap * (ACTION_COUNT - 1));
  screen.list(props, listHeight);

  if (startDateApplyFailed) {
    screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
    const int16_t sidePadding = static_cast<int16_t>(metrics.contentSidePadding);
    screen.insetContent(fui::Insets{0, sidePadding, 0, sidePadding});
    fui::TextStyle hint = screen.theme().smallText;
    hint.maxLines = 3;
    const fui::Size size =
        fui::measureWrappedText(screen.target(), tr(STR_CHOOSE_EARLIER_START_DATE), hint, screen.body().width);
    screen.target().text(screen.takeTop(size.height), tr(STR_CHOOSE_EARLIER_START_DATE), hint);
  }
}
