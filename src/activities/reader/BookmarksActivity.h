#pragma once

#include <Epub.h>

#include <functional>
#include <memory>
#include <vector>

#include "../Activity.h"
#include "BookmarkStore.h"
#include "util/ButtonNavigator.h"

class BookmarksActivity final : public Activity {
  std::shared_ptr<Epub> epub;
  std::vector<BookmarkStore::Bookmark> bookmarks;
  std::string headerTitle;
  std::function<bool(const BookmarkStore::Bookmark&)> onDeleteBookmark;
  BookmarkStore* sourceStore = nullptr;
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;

  int getPageItems() const;
  std::string getItemLabel(int index) const;
  void confirmDeleteSelectedBookmark();

 public:
  explicit BookmarksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             const std::vector<BookmarkStore::Bookmark>& bookmarks,
                             std::shared_ptr<Epub> epub = nullptr, std::string headerTitle = {},
                             std::function<bool(const BookmarkStore::Bookmark&)> onDeleteBookmark = nullptr,
                             BookmarkStore* sourceStore = nullptr)
      : Activity("Bookmarks", renderer, mappedInput),
        epub(std::move(epub)),
        bookmarks(bookmarks),
        headerTitle(std::move(headerTitle)),
        onDeleteBookmark(std::move(onDeleteBookmark)),
        sourceStore(sourceStore) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
