#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// SD-card browser whose file rows carry a favorite toggle: activating a
// directory descends, activating a book toggles it in the favorites store.
class FavoritesBrowserActivity final : public UiListActivity {
  bool lockLongPressBack = false;
  std::string basepath = "/";
  std::vector<std::string> files;
  std::vector<uint8_t> favoriteStates;
  // Per-row caches derived from `files`, rebuilt only in loadFiles().
  std::vector<std::string> rowNames;
  std::vector<freeink::ui::ListItem> rowItems;

  void loadFiles();
  void rebuildRowItems();
  size_t findEntry(const std::string& name) const;

  int listCount() const override { return static_cast<int>(files.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // Long-press Back goes to root; short Back goes up a directory (finish at root).
  bool handleCustomInput() override;
  bool handleButtons() override;
  void drawChrome() override;
  void drawFooter() override;

 public:
  explicit FavoritesBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                    std::string initialPath = "/")
      : UiListActivity("FavoritesBrowser", renderer, mappedInput),
        basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}

  void onEnter() override;
  void onExit() override;
};
