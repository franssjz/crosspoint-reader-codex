#pragma once

#include <FileIndex.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../Activity.h"
#include "RecentBooksStore.h"
#include "util/ButtonNavigator.h"

class FileBrowserActivity final : public Activity {
 public:
  // PickFile returns a supported book/image; PickFirmware only returns .bin.
  // Both pickers preserve navigation and disable the delete action.
  enum class Mode { Books, PickFirmware, PickFile };

 private:
  // Deletion
  bool removeDirFile(const std::string& fullPath);

  ButtonNavigator buttonNavigator;

  size_t selectorIndex = 0;

  bool lockLongPressBack = false;
  // True when this activity was entered while Confirm was already held; we must swallow the next
  // release so we don't immediately auto-open the first entry.
  bool lockNextConfirmRelease = false;

  Mode mode = Mode::Books;

  // Files state
  std::string basepath = "/";
  FileIndex fileIndex;
  struct VisibleEntry {
    char name[FileIndex::MAX_NAME + 2] = {};
    bool completed = false;
  };
  std::unique_ptr<VisibleEntry[]> visibleEntries;
  std::unique_ptr<FileIndex::Entry> indexEntry;
  size_t windowCapacity = 0;
  size_t windowFirst = SIZE_MAX;
  size_t windowCount = 0;
  size_t fileCount = 0;
  enum class ListingError { None, Directory, Index, Memory };
  ListingError listingError = ListingError::None;
  std::unique_ptr<char[]> fileNameBuffer;

  // Data loading
  void loadFiles();
  size_t findEntry(const std::string& name);
  bool loadVisibleWindow();
  void setSelection(size_t row);
  const char* visibleName(size_t row) const;
  bool visibleCompleted(size_t row) const;

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/",
                               Mode mode = Mode::Books)
      : Activity("FileBrowser", renderer, mappedInput),
        mode(mode),
        basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
