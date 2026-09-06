#include "BookmarksAppActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <utility>

#include "../reader/BookmarksActivity.h"
#include "FavoritesStore.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "util/BookIdentity.h"
#include "util/HeaderDateUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr unsigned long DELETE_BOOKMARKS_HOLD_MS = 1000;

struct BookmarkBookCandidate {
  std::string bookId;
  std::string path;
  std::string title;
  std::string author;
};

std::string getDisplayTitle(const std::string& title, const std::string& path) {
  if (!title.empty()) {
    return title;
  }

  const auto slashPos = path.find_last_of('/');
  if (slashPos == std::string::npos || slashPos + 1 >= path.size()) {
    return path;
  }
  return path.substr(slashPos + 1);
}

bool shouldReplaceBookId(const std::string& current, const std::string& candidate) {
  if (candidate.empty()) {
    return false;
  }
  return current.empty() || (BookIdentity::isLegacyBookId(current) && !BookIdentity::isLegacyBookId(candidate));
}

void addCandidate(std::vector<BookmarkBookCandidate>& candidates, BookmarkBookCandidate candidate) {
  candidate.path = BookIdentity::normalizePath(candidate.path);
  if (candidate.path.empty() || !FsHelpers::hasEpubExtension(candidate.path) ||
      !Storage.exists(candidate.path.c_str())) {
    return;
  }

  auto it = std::find_if(candidates.begin(), candidates.end(), [&candidate](const BookmarkBookCandidate& existing) {
    if (!candidate.bookId.empty() && !existing.bookId.empty() && candidate.bookId == existing.bookId) {
      return true;
    }
    return existing.path == candidate.path;
  });

  if (it == candidates.end()) {
    candidates.push_back(std::move(candidate));
    return;
  }

  if (shouldReplaceBookId(it->bookId, candidate.bookId)) {
    it->bookId = std::move(candidate.bookId);
  }
  if (it->title.empty() && !candidate.title.empty()) {
    it->title = std::move(candidate.title);
  }
  if (it->author.empty() && !candidate.author.empty()) {
    it->author = std::move(candidate.author);
  }
}

std::vector<std::string> getBookIdLoadOrder(const std::string& path, const std::string& preferredBookId) {
  std::vector<std::string> ids;
  auto addId = [&ids](const std::string& id) {
    if (!id.empty() && std::find(ids.begin(), ids.end(), id) == ids.end()) {
      ids.push_back(id);
    }
  };

  const std::string resolvedBookId = BookIdentity::resolveStableBookId(path);
  if (!resolvedBookId.empty() && !BookIdentity::isLegacyBookId(resolvedBookId)) {
    addId(resolvedBookId);
  }
  addId(preferredBookId);
  addId(resolvedBookId);
  addId("legacy:" + path);
  return ids;
}

bool loadBookmarksForBook(const std::string& path, const std::string& preferredBookId, BookmarkStore& store,
                          std::string& loadedBookId) {
  const Epub epub(path, "/.crosspoint");
  const std::string cachePath = epub.getCachePath();

  for (const auto& bookId : getBookIdLoadOrder(path, preferredBookId)) {
    BookmarkStore candidateStore;
    candidateStore.load(cachePath, bookId);
    if (candidateStore.isEmpty()) {
      continue;
    }

    store = std::move(candidateStore);
    loadedBookId = bookId;
    return true;
  }

  return false;
}
}  // namespace

void BookmarksAppActivity::refreshEntries() {
  entries.clear();

  std::vector<BookmarkBookCandidate> candidates;
  for (const auto& book : READING_STATS.getBooks()) {
    addCandidate(candidates, BookmarkBookCandidate{book.bookId, book.path, book.title, book.author});
  }
  for (const auto& book : RECENT_BOOKS.getBooks()) {
    addCandidate(candidates, BookmarkBookCandidate{book.bookId, book.path, book.title, book.author});
  }
  for (const auto& book : FAVORITES.getBooks()) {
    addCandidate(candidates, BookmarkBookCandidate{book.bookId, book.path, book.title, book.author});
  }

  for (const auto& candidate : candidates) {
    BookmarkStore store;
    std::string loadedBookId;
    if (!loadBookmarksForBook(candidate.path, candidate.bookId, store, loadedBookId)) {
      continue;
    }

    entries.push_back(BookEntry{
        .bookId = loadedBookId,
        .path = candidate.path,
        .title = getDisplayTitle(candidate.title, candidate.path),
        .author = candidate.author,
        .bookmarks = store.getAll(),
    });
  }

  if (nav.selected >= static_cast<int>(entries.size())) {
    nav.selected = std::max(0, static_cast<int>(entries.size()) - 1);
  }
  rebuildRowItems();
}

void BookmarksAppActivity::rebuildRowItems() {
  rowCounts.clear();
  rowItems.clear();
  rowCounts.reserve(entries.size());
  rowItems.reserve(entries.size());
  for (size_t i = 0; i < entries.size(); ++i) {
    rowCounts.push_back(std::to_string(entries[i].bookmarks.size()));
    fui::ListItem item;
    item.label = entries[i].title.c_str();
    item.subtitle = !entries[i].author.empty() ? entries[i].author.c_str() : entries[i].path.c_str();
    item.value = rowCounts.back().c_str();
    item.icon = listIconFor(UIIcon::Book, 32);
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
}

void BookmarksAppActivity::openBook(const int index) {
  if (index < 0 || index >= static_cast<int>(entries.size())) {
    return;
  }

  const BookEntry entry = entries[index];
  startActivityForResult(
      std::make_unique<BookmarksActivity>(renderer, mappedInput, entry.bookmarks, nullptr, entry.title,
                                          [bookId = entry.bookId](const BookmarkStore::Bookmark& bookmark) {
                                            BookmarkStore store;
                                            store.load("", bookId);
                                            const bool removed = store.removeItem(bookmark);
                                            if (removed) {
                                              store.save();
                                            }
                                            return removed;
                                          }),
      [this, path = entry.path](const ActivityResult& result) {
        if (!result.isCancelled) {
          const auto& bookmark = std::get<BookmarkResult>(result.data);
          activityManager.goToEpubBookmark(path, bookmark.spineIndex, bookmark.page, bookmark.hasVisibleTextOffset,
                                           bookmark.visibleTextOffset);
          return;
        }
        closeRouting();
        {
          RenderLock lock(*this);
          refreshEntries();
        }
        requestUpdate();
      });
}

bool BookmarksAppActivity::clearBookmarksForBook(const std::string& bookId) const {
  BookmarkStore store;
  store.load("", bookId);
  if (store.isEmpty()) {
    return true;
  }

  store.clear();
  store.save();
  return true;
}

void BookmarksAppActivity::confirmDeleteBook(const int index) {
  if (index < 0 || index >= static_cast<int>(entries.size())) {
    return;
  }

  const BookEntry entry = entries[index];
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_ALL_HIGHLIGHTS), entry.title),
      [this, bookId = entry.bookId](const ActivityResult& result) {
        if (!result.isCancelled) {
          clearBookmarksForBook(bookId);
          closeRouting();
          RenderLock lock(*this);
          refreshEntries();
          nav.follow(listCount());
        }
        requestUpdate();
      });
}

void BookmarksAppActivity::onEnter() {
  UiListActivity::onEnter();
  refreshEntries();
}

void BookmarksAppActivity::onExit() {
  Activity::onExit();
  rowItems.clear();
  rowCounts.clear();
  entries.clear();
}

bool BookmarksAppActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (mappedInput.getHeldTime() >= DELETE_BOOKMARKS_HOLD_MS) {
      confirmDeleteBook(nav.selected);
    } else {
      activateIndex(nav.selected);
    }
    return true;
  }
  return UiListActivity::handleButtons();
}

void BookmarksAppActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  openBook(index);
}

void BookmarksAppActivity::onRowLongPress(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  confirmDeleteBook(index);
}

void BookmarksAppActivity::drawChrome() {
  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_HIGHLIGHTS), tr(STR_HIGHLIGHTS_APP_DESC));
}

void BookmarksAppActivity::drawFooter() {
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), entries.empty() ? "" : tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void BookmarksAppActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (entries.empty()) {
    screen.centeredText(tr(STR_NO_HIGHLIGHTS), screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;  // tap opens, long-press clears
  props.valueInset = 8;
  fui::TextStyle label = screen.theme().smallText;
  label.bold = true;
  props.labelText = label;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}
