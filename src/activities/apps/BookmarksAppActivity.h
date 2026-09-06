#pragma once

#include <string>
#include <vector>

#include "../reader/BookmarkStore.h"
#include "activities/UiListActivity.h"

// Books with saved highlights/bookmarks. Activate opens the book's bookmark
// list; a Confirm hold or touch long-press clears all of that book's marks.
class BookmarksAppActivity final : public UiListActivity {
  struct BookEntry {
    std::string bookId;
    std::string path;
    std::string title;
    std::string author;
    std::vector<BookmarkStore::Bookmark> bookmarks;
  };

  std::vector<BookEntry> entries;
  // Row caches derived from entries (bookmark counts in the value slot).
  std::vector<std::string> rowCounts;
  std::vector<freeink::ui::ListItem> rowItems;

  void refreshEntries();
  void rebuildRowItems();
  void openBook(int index);
  bool clearBookmarksForBook(const std::string& bookId) const;
  void confirmDeleteBook(int index);

  int listCount() const override { return static_cast<int>(entries.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleButtons() override;
  void drawChrome() override;
  void drawFooter() override;

 public:
  explicit BookmarksAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("HighlightsApp", renderer, mappedInput, /*wantsTouchLongPress=*/true) {}

  void onEnter() override;
  void onExit() override;
};
