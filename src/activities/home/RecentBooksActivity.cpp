#include "RecentBooksActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long GO_HOME_MS = 1000;
constexpr unsigned long RECENT_BOOK_LONG_PRESS_MS = 1000;

std::string getRecentBookConfirmationLabel(const RecentBook& book) {
  return !book.title.empty() ? book.title : book.path;
}
}  // namespace

void RecentBooksActivity::loadRecentBooks() {
  recentBooks.clear();
  recentBookCompletedStates.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(books.size());
  recentBookCompletedStates.reserve(books.size());

  for (const auto& book : books) {
    // Skip if file no longer exists
    if (!Storage.exists(book.path.c_str())) {
      continue;
    }
    recentBooks.push_back(book);
    const auto* statsBook = READING_STATS.findBook(!book.bookId.empty() ? book.bookId : book.path);
    recentBookCompletedStates.push_back((statsBook != nullptr && statsBook->completed) ? 1 : 0);
  }
}

void RecentBooksActivity::onEnter() {
  Activity::onEnter();

  if (RECENT_BOOKS.pruneMissing()) {
    RECENT_BOOKS.saveToFile();
  }

  // Load data
  loadRecentBooks();

  selectorIndex = 0;
  requestUpdate();
}

void RecentBooksActivity::onExit() {
  Activity::onExit();
  recentBooks.clear();
  recentBookCompletedStates.clear();
}

void RecentBooksActivity::loop() {
  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, true);

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!recentBooks.empty() && selectorIndex < static_cast<int>(recentBooks.size())) {
      if (mappedInput.getHeldTime() >= RECENT_BOOK_LONG_PRESS_MS) {
        const RecentBook selectedBook = recentBooks[selectorIndex];
        const size_t currentSelection = selectorIndex;
        startActivityForResult(
            std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_FROM_RECENTS),
                                                   getRecentBookConfirmationLabel(selectedBook)),
            [this, selectedBook, currentSelection](const ActivityResult& result) {
              if (result.isCancelled) {
                requestUpdate();
                return;
              }

              if (RECENT_BOOKS.removeBook(selectedBook.path)) {
                loadRecentBooks();
                if (recentBooks.empty()) {
                  selectorIndex = 0;
                } else if (currentSelection >= recentBooks.size()) {
                  selectorIndex = recentBooks.size() - 1;
                } else {
                  selectorIndex = currentSelection;
                }
              }
              requestUpdate(true);
            });
        return;
      }

      LOG_DBG("RBA", "Selected recent book: %s", recentBooks[selectorIndex].path.c_str());
      onSelectBook(recentBooks[selectorIndex].path);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
  }

  int listSize = static_cast<int>(recentBooks.size());

  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });
}

void RecentBooksActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_MENU_RECENT_BOOKS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  // Recent tab
  if (recentBooks.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_RECENT_BOOKS));
  } else {
    const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, true);
    const size_t first = (selectorIndex / pageItems) * pageItems;
    const size_t count = std::min(recentBooks.size() - first, static_cast<size_t>(pageItems));
    struct PrewarmCtx {
      const std::vector<RecentBook>* books;
      size_t first;
      bool title;
    };
    const auto getter = [](const void* raw, uint32_t i) -> const char* {
      const auto* context = static_cast<const PrewarmCtx*>(raw);
      const auto& book = (*context->books)[context->first + i];
      return context->title ? book.title.c_str() : book.author.c_str();
    };
    PrewarmCtx titleCtx{&recentBooks, first, true};
    PrewarmCtx authorCtx{&recentBooks, first, false};
    renderer.prewarmFallbackText(UI_12_FONT_ID, getter, &titleCtx, count);
    renderer.prewarmFallbackText(UI_10_FONT_ID, getter, &authorCtx, count);
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, recentBooks.size(), selectorIndex,
        [this](int index) { return recentBooks[index].title; }, [this](int index) { return recentBooks[index].author; },
        [this](int index) { return UITheme::getFileIcon(recentBooks[index].path); }, nullptr, false,
        [this](int index) { return index >= 0 && index < static_cast<int>(recentBookCompletedStates.size()) &&
                                   recentBookCompletedStates[index] != 0; });
  }

  // Help text
  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
