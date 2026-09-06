#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// SD-card browser filtered to flashcard decks (.csv): directories descend,
// decks open a review session.
class FlashcardBrowserActivity final : public UiListActivity {
  std::string basepath = "/";
  std::vector<std::string> files;
  bool lockLongPressBack = false;
  // Per-row caches derived from `files`, rebuilt only in loadFiles().
  std::vector<std::string> rowNames;
  std::vector<freeink::ui::ListItem> rowItems;

  void loadFiles();
  void rebuildRowItems();
  size_t findEntry(const std::string& name) const;
  bool openDeckPath(const std::string& path);

  int listCount() const override { return static_cast<int>(files.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // Long-press Back goes to root; short Back goes up a directory (finish at root).
  bool handleCustomInput() override;
  bool handleButtons() override;
  void drawChrome() override;
  void drawFooter() override;

 public:
  explicit FlashcardBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("FlashcardBrowser", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
};
