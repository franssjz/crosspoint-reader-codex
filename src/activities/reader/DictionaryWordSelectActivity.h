#pragma once

#include <Epub/Page.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../Activity.h"
#include "SelectionPageStepper.h"

class DictionaryWordSelectActivity final : public Activity {
 public:
  using PageLoader = std::function<SelectionPageResult(SelectionPageRequest)>;
  DictionaryWordSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::shared_ptr<Page> page,
                               int readerFontId, int marginLeft, int marginTop, bool highlightPhraseMode = false,
                               PageLoader pageLoader = {}, uint16_t spine = 0, uint16_t pageIndex = 0)
      : Activity("DictionaryWordSelect", renderer, mappedInput),
        page(std::move(page)),
        readerFontId(readerFontId),
        marginLeft(marginLeft),
        marginTop(marginTop),
        highlightPhraseMode(highlightPhraseMode),
        pageLoader(std::move(pageLoader)),
        selectionSpine(spine),
        selectionPage(pageIndex) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }

 private:
  struct WordInfo {
    std::string text;
    std::string lookupText;
    int16_t screenX = 0;
    int16_t screenY = 0;
    int16_t width = 0;
    int16_t row = 0;
    int continuationIndex = -1;
    int continuationOf = -1;
    uint16_t flow = 0;
  };

  struct Row {
    int16_t y = 0;
    std::vector<int> wordIndices;
  };

  struct SelectionRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
  };

  struct SelectionRegionCache {
    SelectionRect rect;
    uint8_t* buffer = nullptr;
    size_t capacity = 0;
    size_t size = 0;
    bool stored = false;
  };

  static constexpr size_t MAX_SELECTION_REGIONS = 2;

  std::shared_ptr<Page> page;
  int readerFontId = 0;
  int marginLeft = 0;
  int marginTop = 0;
  std::vector<WordInfo> words;
  std::vector<Row> rows;
  int currentRow = 0;
  int currentWordInRow = 0;
  int anchorWordIndex = -1;
  bool highlightPhraseMode = false;
  PageLoader pageLoader;
  uint16_t selectionSpine = 0;
  uint16_t selectionPage = 0;
  int pendingPageDirection = 0;
  int selectionDirection = 0;
  bool selectionPageError = false;
  struct SavedFragment {
    HighlightFragment fragment;
    int anchor;
  };
  std::vector<SavedFragment> selectedFragments;
  static constexpr size_t MAX_SELECTION_PAGES = 4;
  void stepSelectionPage(int direction);
  void acceptSelectionPage(SelectionPageResult result);
  HighlightFragment makeSelectionFragment(int from, int to) const;
  bool selectionFits(int from, int to) const;
  SelectionRegionCache selectionRegions[MAX_SELECTION_REGIONS];
  size_t selectionRegionCount = 0;

  void extractWords();
  void prepareReaderFontMetrics();
  int measureWordWidth(const char* text) const;
  void mergeHyphenatedWords();
  void moveRow(int delta);
  void moveWord(int delta);
  void lookupSelectedWord();
  void confirmHighlightSelection();
  std::string buildSelectedText(int from, int to) const;
  int selectedWordIndex() const;
  void updateSelectionHighlight();
  bool redrawSelectionFast();
  void prewarmCurrentSelectionText() const;
  size_t collectSelectionRects(SelectionRect* rects, size_t maxRects) const;
  bool storeSelectionBaseRegions();
  bool restoreSelectionBaseRegions() const;
  void invalidateSelectionRegionCache();
  void freeSelectionRegionCache();
  void drawSelectionHighlight();
};
