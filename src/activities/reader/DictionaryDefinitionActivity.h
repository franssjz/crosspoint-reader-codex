#pragma once

#include <Epub/Page.h>

#include <memory>
#include <string>
#include <vector>

#include "../Activity.h"
#include "DictionaryNavigation.h"
#include "DictionaryStore.h"

struct Rect;

class DictionaryDefinitionActivity final : public Activity {
 public:
  DictionaryDefinitionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::shared_ptr<Page> page,
                               std::string headword, std::string definition, bool truncated, int readerFontId,
                               int definitionFontId, int marginLeft, int marginTop, bool renderPageBackground = true)
      : Activity("DictionaryDefinition", renderer, mappedInput),
        page(std::move(page)),
        headword(std::move(headword)),
        definition(std::move(definition)),
        truncated(truncated),
        readerFontId(readerFontId),
        definitionFontId(definitionFontId),
        marginLeft(marginLeft),
        marginTop(marginTop),
        renderPageBackground(renderPageBackground) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }

 private:
  std::shared_ptr<Page> page;
  std::string headword;
  std::string definition;
  bool truncated = false;
  int readerFontId = 0;
  int definitionFontId = 0;
  int marginLeft = 0;
  int marginTop = 0;
  bool renderPageBackground = true;
  std::vector<std::string> wrappedLines;
  int currentPage = 0;
  int linesPerPage = 1;
  int totalPages = 1;

  enum class View : uint8_t { Definition, Actions, WordSelection, Dictionaries, LookupError };
  View view = View::Definition;
  int menuIndex = 0;
  int selectedWordOrdinal = 0;
  int selectableWordCount = 0;
  int selectedWordLine = 0;
  int selectedWordX = 0;
  int selectedWordWidth = 0;
  std::string selectedWord;
  std::string pendingQuery;
  bool pendingAppend = false;
  DictionaryLookupResult::Status lookupStatus = DictionaryLookupResult::Status::NotFound;
  bool chainLimitReached = false;
  DictionaryNavigation::Trail trail;

  Rect overlayRect() const;
  void wrapText();
  void prepareDefinitionFontMetrics();
  int measureDefinitionText(const char* text) const;
  void updateWordSelection();
  void openDictionaryPicker();
  bool lookupWord(const std::string& query, bool append, int restorePage = 0);
  void returnToPreviousLookup();
  void finishDefinition();
  void renderMenu(const Rect& rect, int bodyY);
};
