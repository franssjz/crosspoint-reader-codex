#include "BookmarksActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
constexpr const char* PAGE_LABEL = "Page ";
constexpr unsigned long DELETE_BOOKMARK_HOLD_MS = 1000;
}  // namespace

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

// Derives rowLabels/rowItems from bookmarks. Called whenever bookmarks
// changes (enter, deletion) so buildScreen() reuses the cached rows.
void BookmarksActivity::rebuildRowItems() {
  rowItems.clear();
  rowLabels.clear();
  rowLabels.reserve(bookmarks.size());
  rowItems.reserve(bookmarks.size());
  for (size_t i = 0; i < bookmarks.size(); ++i) {
    rowLabels.push_back(getItemLabel(static_cast<int>(i)));
    fui::ListItem item;
    item.label = rowLabels.back().c_str();
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
}

const char* BookmarksActivity::headerTitle() const {
  return headerTitleText.empty() ? tr(STR_HIGHLIGHTS) : headerTitleText.c_str();
}

void BookmarksActivity::onEnter() {
  UiListActivity::onEnter();
  rebuildRowItems();
}

void BookmarksActivity::onExit() {
  Activity::onExit();
  // rowItems' labels alias rowLabels; drop both together.
  rowItems.clear();
  rowLabels.clear();
}

void BookmarksActivity::finishCancelled() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void BookmarksActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  // Opening the bookmark leaves this screen; a lingering flash would gray an
  // unrelated row when the list next appears.
  app.clearTapFlash();
  const auto& bookmark = bookmarks[index];
  setResult(BookmarkResult{static_cast<int>(bookmark.spineIndex), bookmark.pageNumber, bookmark.hasVisibleTextOffset,
                           bookmark.visibleTextOffset});
  finish();
}

void BookmarksActivity::onRowLongPress(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  confirmDeleteBookmark(index);
}

bool BookmarksActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!bookmarks.empty() && nav.selected >= 0 && nav.selected < listCount()) {
      if (mappedInput.getHeldTime() >= DELETE_BOOKMARK_HOLD_MS) {
        confirmDeleteBookmark(nav.selected);
      } else {
        activateIndex(nav.selected);
      }
    }
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finishCancelled();
    return true;
  }

  return false;
}

void BookmarksActivity::confirmDeleteBookmark(const int index) {
  if (!onDeleteBookmark || index < 0 || index >= listCount()) {
    return;
  }

  const auto bookmark = bookmarks[index];
  const std::string body = getItemLabel(index);
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_HIGHLIGHT), body),
      [this, bookmark](const ActivityResult& result) {
        if (result.isCancelled) {
          requestUpdate();
          return;
        }

        if (onDeleteBookmark(bookmark)) {
          // The interaction table still indexes the pre-removal rows; stop
          // routing touches against it until the next render republishes.
          closeRouting();
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
            finishCancelled();
            return;
          }

          rebuildRowItems();
          if (nav.selected >= listCount()) {
            nav.selected = listCount() - 1;
          }
          nav.follow(listCount());
        }

        requestUpdate(true);
      });
}

void BookmarksActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band GUI.drawHeader paints.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (bookmarks.empty()) {
    screen.centeredText(tr(STR_NO_HIGHLIGHTS), screen.theme().bodyText);
    return;
  }

  // rowItems is built in rebuildRowItems() and reused here on every repaint.
  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  // Tap opens; long-press prompts deletion (physical buttons stay in loop()).
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  // Small font so more of a snippet fits; maxLines=2 wraps long highlights and
  // marks the style caller-owned (see textStyleUnset).
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
