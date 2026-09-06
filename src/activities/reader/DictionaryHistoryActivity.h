#pragma once

#include <Epub/Page.h>

#include <memory>
#include <string>
#include <vector>

#include "../UiListActivity.h"

struct Rect;

// Fork: the reader's dictionary lookup history, drawn as an overlay panel over
// the current page. Tap (or Confirm) looks the word up again.
class DictionaryHistoryActivity final : public UiListActivity {
 public:
  DictionaryHistoryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::shared_ptr<Page> page,
                            int readerFontId, int marginLeft, int marginTop)
      : UiListActivity("DictionaryHistory", renderer, mappedInput),
        page(std::move(page)),
        readerFontId(readerFontId),
        marginLeft(marginLeft),
        marginTop(marginTop) {}

  void onEnter() override;
  void onExit() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }

 private:
  std::shared_ptr<Page> page;
  int readerFontId = 0;
  int marginLeft = 0;
  int marginTop = 0;
  std::vector<std::string> history;
  // Row cache aliasing history's strings; rebuilt only when history changes.
  std::vector<freeink::ui::ListItem> rowItems;
  void rebuildRowItems();

  // Overlay panel geometry (the page shows above it).
  Rect overlayRect() const;

  int listCount() const override { return static_cast<int>(history.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  // Page behind + the framed panel and its sub-header; the list renders inside.
  void drawChrome() override;
};
