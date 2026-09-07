#include "BookmarksActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr const char* PAGE_LABEL = "Page ";
constexpr unsigned long DELETE_BOOKMARK_HOLD_MS = 1000;
}

int BookmarksActivity::getPageItems() const {
  constexpr int lineHeight = 30;
  const int screenHeight = renderer.getScreenHeight();
  const auto orientation = renderer.getOrientation();
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int startY = 60 + hintGutterHeight;
  const int availableHeight = screenHeight - startY - lineHeight;
  return std::max(1, availableHeight / lineHeight);
}

std::string BookmarksActivity::getItemLabel(const int index) const {
  const auto& bookmark = bookmarks[index];
  char buffer[64];

  if (bookmark.isTextHighlight) {
    snprintf(buffer, sizeof(buffer), "%d. %s", index + 1, tr(STR_TEXT_HIGHLIGHT_PREFIX));
    return std::string(buffer) + bookmark.snippet;
  }

  if (!bookmark.snippet.empty()) {
    snprintf(buffer, sizeof(buffer), "%d. %s", index + 1, tr(STR_PAGE_MARK_PREFIX));
    return std::string(buffer) + bookmark.snippet;
  }

  if (epub) {
    const int tocIndex = epub->getTocIndexForSpineIndex(bookmark.spineIndex);
    if (tocIndex != -1) {
      const auto tocItem = epub->getTocItem(tocIndex);
      snprintf(buffer, sizeof(buffer), "%d. ", index + 1);
      return std::string(buffer) + tocItem.title + " - " + PAGE_LABEL + std::to_string(bookmark.pageNumber + 1);
    }

    snprintf(buffer, sizeof(buffer), "%d. %s%d, %s%d", index + 1, tr(STR_SECTION_PREFIX), bookmark.spineIndex + 1,
             PAGE_LABEL, bookmark.pageNumber + 1);
    return buffer;
  }

  snprintf(buffer, sizeof(buffer), "%d. %s%d", index + 1, PAGE_LABEL, bookmark.pageNumber + 1);
  return buffer;
}

void BookmarksActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void BookmarksActivity::onExit() { Activity::onExit(); }

void BookmarksActivity::confirmDeleteSelectedBookmark() {
  if (!onDeleteBookmark || selectorIndex < 0 || selectorIndex >= static_cast<int>(bookmarks.size())) {
    return;
  }

  const auto bookmark = bookmarks[selectorIndex];
  const std::string body = getItemLabel(selectorIndex);
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_HIGHLIGHT), body),
      [this, bookmark](const ActivityResult& result) {
        if (result.isCancelled) {
          requestUpdate();
          return;
        }

        if (onDeleteBookmark(bookmark)) {
          bookmarks.erase(std::remove_if(bookmarks.begin(), bookmarks.end(),
                                         [&](const BookmarkStore::Bookmark& current) {
                                           return current.isTextHighlight == bookmark.isTextHighlight &&
                                                  current.spineIndex == bookmark.spineIndex &&
                                                  current.pageNumber == bookmark.pageNumber &&
                                                  current.endPageNumber == bookmark.endPageNumber &&
                                                  current.startWordIndex == bookmark.startWordIndex &&
                                                  current.endWordIndex == bookmark.endWordIndex &&
                                                  current.snippet == bookmark.snippet &&
                                                  current.hasVisibleTextOffset == bookmark.hasVisibleTextOffset &&
                                                  (!current.hasVisibleTextOffset ||
                                                   current.visibleTextOffset == bookmark.visibleTextOffset);
                                         }),
                          bookmarks.end());

          if (bookmarks.empty()) {
            ActivityResult cancelResult;
            cancelResult.isCancelled = true;
            setResult(std::move(cancelResult));
            finish();
            return;
          }

          if (selectorIndex >= static_cast<int>(bookmarks.size())) {
            selectorIndex = static_cast<int>(bookmarks.size()) - 1;
          }
        }

        requestUpdate();
      });
}

void BookmarksActivity::loop() {
  const int totalItems = static_cast<int>(bookmarks.size());
  const int pageItems = getPageItems();

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (mappedInput.getHeldTime() >= DELETE_BOOKMARK_HOLD_MS) {
      confirmDeleteSelectedBookmark();
      return;
    }

    if (!bookmarks.empty()) {
      const auto& bookmark = bookmarks[selectorIndex];
      setResult(BookmarkResult{static_cast<int>(bookmark.spineIndex), bookmark.pageNumber,
                               bookmark.hasVisibleTextOffset, bookmark.visibleTextOffset});
      finish();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  buttonNavigator.onNextRelease([this, totalItems] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, totalItems] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });
}

void BookmarksActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const int totalItems = static_cast<int>(bookmarks.size());
  if (totalItems == 0) {
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_NO_HIGHLIGHTS), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 30 : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int contentY = hintGutterHeight;
  const int pageItems = getPageItems();

  const char* rawTitle = headerTitle.empty() ? tr(STR_HIGHLIGHTS) : headerTitle.c_str();
  const std::string title = renderer.truncatedText(UI_12_FONT_ID, rawTitle, contentWidth - 20);
  const int titleX =
      contentX + (contentWidth - renderer.getTextWidth(UI_12_FONT_ID, title.c_str(), EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, title.c_str(), true, EpdFontFamily::BOLD);

  const auto pageStartIndex = selectorIndex / pageItems * pageItems;
  renderer.fillRect(contentX, 60 + contentY + (selectorIndex % pageItems) * 30 - 2, contentWidth - 1, 30);

  for (int i = 0; i < pageItems; ++i) {
    const int itemIndex = pageStartIndex + i;
    if (itemIndex >= totalItems) {
      break;
    }

    const int displayY = 60 + contentY + i * 30;
    const bool isSelected = itemIndex == selectorIndex;
    const std::string label =
        renderer.truncatedText(UI_10_FONT_ID, getItemLabel(itemIndex).c_str(), contentWidth - 40);
    renderer.drawText(UI_10_FONT_ID, contentX + 20, displayY, label.c_str(), !isSelected);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
