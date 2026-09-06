#pragma once

#include <Epub/Page.h>

#include <memory>
#include <string>
#include <vector>

#include "../UiListActivity.h"

struct Rect;

// Fork: "Did you mean" spelling suggestions for a failed dictionary lookup,
// drawn as an overlay panel over the current page. Tap (or Confirm) looks the
// suggestion up.
class DictionarySuggestionsActivity final : public UiListActivity {
 public:
  DictionarySuggestionsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::shared_ptr<Page> page,
                                std::string originalWord, std::vector<std::string> suggestions, int readerFontId,
                                int marginLeft, int marginTop)
      : UiListActivity("DictionarySuggestions", renderer, mappedInput),
        page(std::move(page)),
        originalWord(std::move(originalWord)),
        suggestions(std::move(suggestions)),
        readerFontId(readerFontId),
        marginLeft(marginLeft),
        marginTop(marginTop) {}

  void onEnter() override;
  void onExit() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }

 private:
  std::shared_ptr<Page> page;
  std::string originalWord;
  std::vector<std::string> suggestions;
  int readerFontId = 0;
  int marginLeft = 0;
  int marginTop = 0;
  // Row cache aliasing suggestions' strings; built once on enter (the list
  // never changes while the screen is up).
  std::vector<freeink::ui::ListItem> rowItems;
  void rebuildRowItems();

  // Overlay panel geometry (the page shows above it).
  Rect overlayRect() const;

  int listCount() const override { return static_cast<int>(suggestions.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  // Page behind + the framed panel and its sub-header; the list renders inside.
  void drawChrome() override;
};
