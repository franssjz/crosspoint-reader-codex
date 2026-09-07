#include "BookmarksAppActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <utility>

#include "FavoritesStore.h"
#include "RecentBooksStore.h"
#include "../reader/BookmarksActivity.h"
#include "ReadingStatsStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/BookIdentity.h"

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

  if (selectedIndex >= static_cast<int>(entries.size())) {
    selectedIndex = std::max(0, static_cast<int>(entries.size()) - 1);
  }
}

void BookmarksAppActivity::openSelectedBook() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size())) {
    return;
  }

  const BookEntry entry = entries[selectedIndex];
  startActivityForResult(
      std::make_unique<BookmarksActivity>(
          renderer, mappedInput, entry.bookmarks, nullptr, entry.title,
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
        refreshEntries();
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

void BookmarksAppActivity::confirmDeleteSelectedBook() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size())) {
    return;
  }

  const BookEntry entry = entries[selectedIndex];
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_ALL_HIGHLIGHTS), entry.title),
      [this, bookId = entry.bookId](const ActivityResult& result) {
        if (!result.isCancelled) {
          clearBookmarksForBook(bookId);
          refreshEntries();
        }
        requestUpdate();
      });
}

void BookmarksAppActivity::onEnter() {
  Activity::onEnter();
  refreshEntries();
  requestUpdate();
}

void BookmarksAppActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (mappedInput.getHeldTime() >= DELETE_BOOKMARKS_HOLD_MS) {
      confirmDeleteSelectedBook();
      return;
    }

    openSelectedBook();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    if (entries.empty()) {
      return;
    }
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(entries.size()));
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    if (entries.empty()) {
      return;
    }
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(entries.size()));
    requestUpdate();
  });
}

void BookmarksAppActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_HIGHLIGHTS), tr(STR_HIGHLIGHTS_APP_DESC));

  if (entries.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_HIGHLIGHTS));
  } else {
    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, listHeight}, static_cast<int>(entries.size()), selectedIndex,
                 [this](const int index) { return entries[index].title; },
                 [this](const int index) {
                   if (!entries[index].author.empty()) {
                     return entries[index].author;
                   }
                   return entries[index].path;
                 },
                 [](const int) { return UIIcon::Book; },
                 [this](const int index) { return std::to_string(entries[index].bookmarks.size()); });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), entries.empty() ? "" : tr(STR_OPEN), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
