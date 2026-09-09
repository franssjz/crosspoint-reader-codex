#include "DictionaryWordSelectActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>
#include <Utf8.h>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <optional>

#include "CrossPointSettings.h"
#include "DictionaryDefinitionActivity.h"
#include "DictionaryStore.h"
#include "DictionarySuggestionsActivity.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int HIGHLIGHT_PADDING_X = 2;
constexpr int HIGHLIGHT_PADDING_Y = 1;
constexpr int HIGHLIGHT_RADIUS = 3;

std::string visibleHighlightWord(const std::string& word) {
  if (word.size() >= 3 && static_cast<unsigned char>(word[0]) == 0xE2 && static_cast<unsigned char>(word[1]) == 0x80 &&
      static_cast<unsigned char>(word[2]) == 0x83) {
    return word.substr(3);
  }
  return word;
}
}  // namespace

void DictionaryWordSelectActivity::onEnter() {
  Activity::onEnter();
  invalidateSelectionRegionCache();
  extractWords();
  mergeHyphenatedWords();
  if (!rows.empty()) {
    currentRow = std::min<int>(static_cast<int>(rows.size()) / 3, static_cast<int>(rows.size()) - 1);
    currentWordInRow = 0;
  }
  requestUpdate();
}

void DictionaryWordSelectActivity::onExit() {
  freeSelectionRegionCache();
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->clearCache();
  }
  Activity::onExit();
}

void DictionaryWordSelectActivity::extractWords() {
  words.clear();
  rows.clear();
  if (!page) return;

  prepareReaderFontMetrics();

  page->forEachTextLine([&](const TextBlock& block, const int lineX, const int lineY, const uint16_t flow) {
    const int rubyShift = block.getRubyShift(renderer.getFontAscenderSize(readerFontId));
    for (size_t i = 0; i < block.wordCount(); ++i) {
      const std::string word = block.wordText(i);
      const std::string cleaned = highlightPhraseMode ? visibleHighlightWord(word) : DictionaryStore::cleanWord(word);
      if (cleaned.find_first_not_of(" \t\r\n") == std::string::npos) continue;
      const int16_t x = static_cast<int16_t>(lineX + block.wordXpos(i) + marginLeft);
      const int16_t y = static_cast<int16_t>(lineY + marginTop + rubyShift);
      const auto style = block.wordStyle(i);
      const int16_t width =
          static_cast<int16_t>(std::max(1, renderer.getTextAdvanceX(readerFontId, word.c_str(), style)));
      words.push_back(WordInfo{word, cleaned, x, y, width, 0, -1, -1, flow});
    }
    return true;
  });

  if (words.empty()) return;
  int16_t currentY = words[0].screenY;
  rows.push_back(Row{currentY, {}});
  for (size_t i = 0; i < words.size(); ++i) {
    if (std::abs(words[i].screenY - currentY) > 2 || (i > 0 && words[i].flow != words[i - 1].flow)) {
      currentY = words[i].screenY;
      rows.push_back(Row{currentY, {}});
    }
    words[i].row = static_cast<int16_t>(rows.size() - 1);
    rows.back().wordIndices.push_back(static_cast<int>(i));
  }
}

void DictionaryWordSelectActivity::prepareReaderFontMetrics() {
  if (!page || !renderer.isSdCardFont(readerFontId)) return;

  if (auto* manager = renderer.getFontCacheManager()) {
    auto scope = manager->createPrewarmScope();
    page->recordFontUsage(*manager, readerFontId, SETTINGS.bionicReading);
    scope.endScanAndPrewarm();
  }
}

int DictionaryWordSelectActivity::measureWordWidth(const char* text) const {
  return renderer.getTextAdvanceX(readerFontId, text, EpdFontFamily::REGULAR);
}

void DictionaryWordSelectActivity::mergeHyphenatedWords() {
  for (size_t rowIndex = 0; rowIndex + 1 < rows.size(); ++rowIndex) {
    if (rows[rowIndex].wordIndices.empty() || rows[rowIndex + 1].wordIndices.empty()) continue;

    const int lastIndex = rows[rowIndex].wordIndices.back();
    const int nextIndex = rows[rowIndex + 1].wordIndices.front();
    if (words[lastIndex].flow != words[nextIndex].flow) continue;
    const std::string& raw = words[lastIndex].text;
    if (raw.empty()) continue;

    bool hyphenated = raw.back() == '-';
    if (!hyphenated && raw.size() >= 2 && static_cast<unsigned char>(raw[raw.size() - 2]) == 0xC2 &&
        static_cast<unsigned char>(raw[raw.size() - 1]) == 0xAD) {
      hyphenated = true;
    }
    if (!hyphenated) continue;

    std::string first = raw;
    if (!first.empty() && first.back() == '-') {
      first.pop_back();
    } else if (first.size() >= 2) {
      first.erase(first.size() - 2);
    }

    const std::string merged = DictionaryStore::cleanWord(first + words[nextIndex].text);
    if (merged.empty()) continue;
    words[lastIndex].lookupText = merged;
    words[nextIndex].lookupText = merged;
    words[lastIndex].continuationIndex = nextIndex;
    words[nextIndex].continuationOf = lastIndex;
  }
}

void DictionaryWordSelectActivity::moveRow(const int delta) {
  if (rows.empty()) return;
  const int oldWordIndex = rows[currentRow].wordIndices[currentWordInRow];
  const int oldCenter = words[oldWordIndex].screenX + words[oldWordIndex].width / 2;

  currentRow = (currentRow + delta + static_cast<int>(rows.size())) % static_cast<int>(rows.size());
  int bestIndex = 0;
  int bestDistance = INT_MAX;
  for (int i = 0; i < static_cast<int>(rows[currentRow].wordIndices.size()); ++i) {
    const int wordIndex = rows[currentRow].wordIndices[i];
    const int center = words[wordIndex].screenX + words[wordIndex].width / 2;
    const int distance = std::abs(center - oldCenter);
    if (distance < bestDistance) {
      bestDistance = distance;
      bestIndex = i;
    }
  }
  currentWordInRow = bestIndex;
  updateSelectionHighlight();
}

void DictionaryWordSelectActivity::moveWord(const int delta) {
  if (rows.empty()) return;
  const int rowCount = static_cast<int>(rows.size());
  const int wordCount = static_cast<int>(rows[currentRow].wordIndices.size());
  if (wordCount <= 0) return;

  if (delta < 0 && currentWordInRow > 0) {
    --currentWordInRow;
  } else if (delta > 0 && currentWordInRow + 1 < wordCount) {
    ++currentWordInRow;
  } else if (delta < 0) {
    currentRow = (currentRow + rowCount - 1) % rowCount;
    currentWordInRow = static_cast<int>(rows[currentRow].wordIndices.size()) - 1;
  } else {
    currentRow = (currentRow + 1) % rowCount;
    currentWordInRow = 0;
  }
  updateSelectionHighlight();
}

void DictionaryWordSelectActivity::updateSelectionHighlight() {
  if (highlightPhraseMode) {
    requestUpdate();
    return;
  }
  if (redrawSelectionFast()) return;
  requestUpdate();
}

bool DictionaryWordSelectActivity::redrawSelectionFast() {
  if (selectionRegionCount == 0) return false;

  RenderLock lock(*this);
  if (!restoreSelectionBaseRegions()) return false;
  if (!storeSelectionBaseRegions()) return false;

  prewarmCurrentSelectionText();
  drawSelectionHighlight();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  return true;
}

void DictionaryWordSelectActivity::prewarmCurrentSelectionText() const {
  if (rows.empty() || currentRow < 0 || currentRow >= static_cast<int>(rows.size()) || currentWordInRow < 0 ||
      currentWordInRow >= static_cast<int>(rows[currentRow].wordIndices.size())) {
    return;
  }

  auto* fcm = renderer.getFontCacheManager();
  if (!fcm) return;

  const int wordIndex = rows[currentRow].wordIndices[currentWordInRow];
  std::string text = words[wordIndex].text;
  const int linkedIndex =
      words[wordIndex].continuationOf >= 0 ? words[wordIndex].continuationOf : words[wordIndex].continuationIndex;
  if (linkedIndex >= 0 && linkedIndex != wordIndex && linkedIndex < static_cast<int>(words.size())) {
    text.push_back(' ');
    text += words[linkedIndex].text;
  }

  if (!text.empty()) {
    fcm->prewarmCache(readerFontId, text.c_str(), 0x01);
  }
}

size_t DictionaryWordSelectActivity::collectSelectionRects(SelectionRect* rects, const size_t maxRects) const {
  if (!rects || maxRects == 0 || rows.empty() || currentRow < 0 || currentRow >= static_cast<int>(rows.size()) ||
      currentWordInRow < 0 || currentWordInRow >= static_cast<int>(rows[currentRow].wordIndices.size())) {
    return 0;
  }

  auto addRect = [&](const WordInfo& selectedWord, size_t& count) {
    if (count >= maxRects) return;
    const int lineHeight = renderer.getLineHeight(readerFontId);
    rects[count++] =
        SelectionRect{selectedWord.screenX - HIGHLIGHT_PADDING_X, selectedWord.screenY - HIGHLIGHT_PADDING_Y,
                      selectedWord.width + HIGHLIGHT_PADDING_X * 2, lineHeight + HIGHLIGHT_PADDING_Y * 2};
  };

  size_t count = 0;
  const int wordIndex = rows[currentRow].wordIndices[currentWordInRow];
  addRect(words[wordIndex], count);

  const int linkedIndex =
      words[wordIndex].continuationOf >= 0 ? words[wordIndex].continuationOf : words[wordIndex].continuationIndex;
  if (linkedIndex >= 0 && linkedIndex != wordIndex && linkedIndex < static_cast<int>(words.size())) {
    addRect(words[linkedIndex], count);
  }

  return count;
}

bool DictionaryWordSelectActivity::storeSelectionBaseRegions() {
  SelectionRect rects[MAX_SELECTION_REGIONS];
  const size_t rectCount = collectSelectionRects(rects, MAX_SELECTION_REGIONS);
  invalidateSelectionRegionCache();
  if (rectCount == 0) return false;

  for (size_t i = 0; i < rectCount; ++i) {
    const size_t required = renderer.getRegionByteSize(rects[i].x, rects[i].y, rects[i].width, rects[i].height);
    if (required == 0) {
      invalidateSelectionRegionCache();
      return false;
    }

    SelectionRegionCache& region = selectionRegions[i];
    if (region.capacity < required) {
      uint8_t* replacement = static_cast<uint8_t*>(malloc(required));
      if (!replacement) {
        invalidateSelectionRegionCache();
        return false;
      }
      free(region.buffer);
      region.buffer = replacement;
      region.capacity = required;
    }

    if (!renderer.copyRegionToBuffer(rects[i].x, rects[i].y, rects[i].width, rects[i].height, region.buffer,
                                     region.capacity)) {
      invalidateSelectionRegionCache();
      return false;
    }

    region.rect = rects[i];
    region.size = required;
    region.stored = true;
  }

  selectionRegionCount = rectCount;
  return true;
}

bool DictionaryWordSelectActivity::restoreSelectionBaseRegions() const {
  if (selectionRegionCount == 0) return false;

  for (size_t i = 0; i < selectionRegionCount; ++i) {
    const SelectionRegionCache& region = selectionRegions[i];
    if (!region.stored || !region.buffer || region.size == 0) return false;
    if (!renderer.copyBufferToRegion(region.rect.x, region.rect.y, region.rect.width, region.rect.height, region.buffer,
                                     region.size)) {
      return false;
    }
  }
  return true;
}

void DictionaryWordSelectActivity::invalidateSelectionRegionCache() {
  selectionRegionCount = 0;
  for (auto& region : selectionRegions) {
    region.stored = false;
    region.size = 0;
  }
}

void DictionaryWordSelectActivity::freeSelectionRegionCache() {
  for (auto& region : selectionRegions) {
    free(region.buffer);
    region.buffer = nullptr;
    region.capacity = 0;
    region.size = 0;
    region.stored = false;
  }
  selectionRegionCount = 0;
}

void DictionaryWordSelectActivity::drawSelectionHighlight() {
  if (rows.empty() || currentRow < 0 || currentRow >= static_cast<int>(rows.size()) || currentWordInRow < 0 ||
      currentWordInRow >= static_cast<int>(rows[currentRow].wordIndices.size())) {
    return;
  }

  const int wordIndex = selectedWordIndex();
  const auto& word = words[wordIndex];
  const int lineHeight = renderer.getLineHeight(readerFontId);

  auto drawSelectedWord = [&](const WordInfo& selectedWord) {
    renderer.fillRoundedRect(selectedWord.screenX - HIGHLIGHT_PADDING_X, selectedWord.screenY - HIGHLIGHT_PADDING_Y,
                             selectedWord.width + HIGHLIGHT_PADDING_X * 2, lineHeight + HIGHLIGHT_PADDING_Y * 2,
                             HIGHLIGHT_RADIUS, Color::Black);
    renderer.drawText(readerFontId, selectedWord.screenX, selectedWord.screenY, selectedWord.text.c_str(), false);
  };

  if (highlightPhraseMode && anchorWordIndex >= 0) {
    const int from = std::min(anchorWordIndex, wordIndex);
    const int to = std::max(anchorWordIndex, wordIndex);
    for (int index = from; index <= to; ++index) {
      drawSelectedWord(words[index]);
    }
    return;
  }

  drawSelectedWord(word);

  const int linkedIndex = word.continuationOf >= 0 ? word.continuationOf : word.continuationIndex;
  if (linkedIndex >= 0 && linkedIndex != wordIndex && linkedIndex < static_cast<int>(words.size())) {
    drawSelectedWord(words[linkedIndex]);
  }
}

int DictionaryWordSelectActivity::selectedWordIndex() const {
  if (rows.empty() || currentRow < 0 || currentRow >= static_cast<int>(rows.size()) || currentWordInRow < 0 ||
      currentWordInRow >= static_cast<int>(rows[currentRow].wordIndices.size())) {
    return -1;
  }
  return rows[currentRow].wordIndices[currentWordInRow];
}

std::string DictionaryWordSelectActivity::buildSelectedText(const int from, const int to) const {
  std::string text;
  text.reserve(256);
  for (int index = from; index <= to && text.size() < 512; ++index) {
    std::string word = visibleHighlightWord(words[index].text);
    const size_t first = word.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) continue;
    const size_t last = word.find_last_not_of(" \t\r\n");
    word = word.substr(first, last - first + 1);
    if (!text.empty()) text.push_back(' ');
    const size_t remaining = 512 - text.size();
    text.append(word, 0, remaining);
  }
  return text;
}

HighlightFragment DictionaryWordSelectActivity::makeSelectionFragment(const int from, const int to) const {
  HighlightFragment fragment;
  fragment.text = buildSelectedText(from, to);
  fragment.text.resize(utf8SafeTruncateBuffer(fragment.text.data(), static_cast<int>(fragment.text.size())));
  fragment.spineIndex = selectionSpine;
  fragment.pageNumber = selectionPage;
  fragment.startWordIndex = static_cast<uint16_t>(from);
  fragment.endWordIndex = static_cast<uint16_t>(to);
  fragment.visibleTextOffset = page ? page->visibleTextOffset : 0;
  return fragment;
}

bool DictionaryWordSelectActivity::selectionFits(int from, int to) const {
  size_t bytes = 0;
  for (int i = from; i <= to; ++i) {
    const auto word = visibleHighlightWord(words[i].text);
    const auto first = word.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) continue;
    const auto last = word.find_last_not_of(" \t\r\n");
    bytes += last - first + 1 + (bytes ? 1 : 0);
    if (bytes > 512) return false;
  }
  return true;
}

void DictionaryWordSelectActivity::stepSelectionPage(const int direction) {
  if (!pageLoader || pendingPageDirection != 0) return;
  RenderLock lock(*this);
  if (anchorWordIndex >= 0 && selectedFragments.size() + 1 >= MAX_SELECTION_PAGES && direction == selectionDirection) {
    GUI.drawPopup(renderer, tr(STR_HIGHLIGHT_FAILED));
    renderer.displayBuffer();
    return;
  }
  if (anchorWordIndex >= 0 && !words.empty() && (selectedFragments.empty() || direction == selectionDirection) &&
      !selectionFits(direction > 0 ? anchorWordIndex : 0,
                     direction > 0 ? static_cast<int>(words.size()) - 1 : anchorWordIndex)) {
    GUI.drawPopup(renderer, tr(STR_HIGHLIGHT_FAILED));
    renderer.displayBuffer();
    return;
  }
  selectionPageError = false;
  pendingPageDirection = direction;
  requestUpdate();
  acceptSelectionPage(pageLoader(direction > 0 ? SelectionPageRequest::Next : SelectionPageRequest::Previous));
}

void DictionaryWordSelectActivity::acceptSelectionPage(SelectionPageResult result) {
  using Status = SelectionPageResult::Status;
  if (result.status == Status::Pending) return;
  const int direction = pendingPageDirection;
  pendingPageDirection = 0;
  if (result.status != Status::Ready || !result.page) {
    selectionPageError = result.status == Status::Error;
    requestUpdate();
    return;
  }
  int restoredAnchor = -1;
  if (anchorWordIndex >= 0 && !words.empty()) {
    const bool returning = !selectedFragments.empty() && selectedFragments.back().fragment.spineIndex == result.spine &&
                           selectedFragments.back().fragment.pageNumber == result.pageIndex;
    if (returning) {
      restoredAnchor = selectedFragments.back().anchor;
      selectedFragments.pop_back();
      if (selectedFragments.empty()) selectionDirection = 0;
    } else {
      if (selectedFragments.empty()) selectionDirection = direction;
      const int from = direction > 0 ? anchorWordIndex : 0;
      const int to = direction > 0 ? static_cast<int>(words.size()) - 1 : anchorWordIndex;
      selectedFragments.push_back({makeSelectionFragment(from, to), anchorWordIndex});
    }
  }
  const bool continuing = anchorWordIndex >= 0;
  freeSelectionRegionCache();
  page = std::move(result.page);
  selectionSpine = result.spine;
  selectionPage = result.pageIndex;
  extractWords();
  mergeHyphenatedWords();
  currentRow = direction > 0 || rows.empty() ? 0 : static_cast<int>(rows.size()) - 1;
  currentWordInRow = direction > 0 || rows.empty() ? 0 : static_cast<int>(rows.back().wordIndices.size()) - 1;
  anchorWordIndex = continuing ? (restoredAnchor >= 0 ? restoredAnchor
                                  : direction > 0     ? 0
                                                      : std::max(0, static_cast<int>(words.size()) - 1))
                               : -1;
  if (restoredAnchor >= 0 && restoredAnchor < static_cast<int>(words.size())) {
    currentRow = words[restoredAnchor].row;
    const auto& indices = rows[currentRow].wordIndices;
    currentWordInRow = static_cast<int>(std::find(indices.begin(), indices.end(), restoredAnchor) - indices.begin());
  }
  requestUpdate();
}

void DictionaryWordSelectActivity::confirmHighlightSelection() {
  const int wordIndex = selectedWordIndex();
  if (wordIndex < 0) return;
  if (anchorWordIndex < 0) {
    anchorWordIndex = wordIndex;
    requestUpdate();
    return;
  }

  const int from = std::min(anchorWordIndex, wordIndex);
  const int to = std::max(anchorWordIndex, wordIndex);
  if (!selectionFits(from, to)) {
    RenderLock lock(*this);
    GUI.drawPopup(renderer, tr(STR_HIGHLIGHT_FAILED));
    renderer.displayBuffer();
    return;
  }
  HighlightResult result{buildSelectedText(from, to), static_cast<uint16_t>(from), static_cast<uint16_t>(to)};
  if (pageLoader) {
    result.fragments.reserve(selectedFragments.size() + 1);
    for (auto& saved : selectedFragments) result.fragments.push_back(std::move(saved.fragment));
    result.fragments.push_back(makeSelectionFragment(from, to));
    std::sort(result.fragments.begin(), result.fragments.end(),
              [](const HighlightFragment& a, const HighlightFragment& b) {
                return a.spineIndex != b.spineIndex ? a.spineIndex < b.spineIndex : a.pageNumber < b.pageNumber;
              });
  }
  setResult(std::move(result));
  finish();
}

void DictionaryWordSelectActivity::lookupSelectedWord() {
  if (rows.empty()) return;
  const int wordIndex = rows[currentRow].wordIndices[currentWordInRow];
  const std::string query = words[wordIndex].lookupText.empty() ? DictionaryStore::cleanWord(words[wordIndex].text)
                                                                : words[wordIndex].lookupText;
  freeSelectionRegionCache();
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->clearCache();
  }
  if (query.empty()) {
    GUI.drawPopup(renderer, tr(STR_LOOKUP_EMPTY_PAGE));
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    delay(700);
    requestUpdate();
    return;
  }

  Rect popup;
  {
    RenderLock lock(*this);
    popup = GUI.drawPopup(renderer, tr(STR_DICTIONARY_PREPARING));
  }
  if (!DICTIONARIES.prepareActive([this, &popup](int percent) {
        RenderLock lock(*this);
        GUI.fillPopupProgress(renderer, popup, percent);
      })) {
    GUI.drawPopup(renderer, tr(STR_DICTIONARY_NOT_READY));
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    delay(900);
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    popup = GUI.drawPopup(renderer, tr(STR_DICTIONARY_LOOKUP));
  }
  const auto lookup = DICTIONARIES.lookup(query, true);
  if (lookup.status == DictionaryLookupResult::Status::Found) {
    startActivityForResult(std::make_unique<DictionaryDefinitionActivity>(
                               renderer, mappedInput, page, lookup.headword, lookup.definition, lookup.truncated,
                               readerFontId, DICTIONARIES.getDefinitionFontId(readerFontId), marginLeft, marginTop),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               setResult(ActivityResult{});
                               finish();
                               return;
                             }
                             requestUpdate();
                           });
    return;
  }

  if (!lookup.suggestions.empty()) {
    startActivityForResult(
        std::make_unique<DictionarySuggestionsActivity>(renderer, mappedInput, page, query, lookup.suggestions,
                                                        readerFontId, marginLeft, marginTop),
        [this](const ActivityResult& result) {
          if (!result.isCancelled) {
            setResult(ActivityResult{});
            finish();
            return;
          }
          requestUpdate();
        });
    return;
  }

  GUI.drawPopup(renderer, DictionaryStore::lookupErrorMessage(lookup.status));
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  delay(900);
  requestUpdate();
}

void DictionaryWordSelectActivity::loop() {
  if (pendingPageDirection != 0) {
    RenderLock lock(*this);
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      pageLoader(SelectionPageRequest::Cancel);
      pendingPageDirection = 0;
      requestUpdate();
      return;
    }
    acceptSelectionPage(pageLoader(SelectionPageRequest::Poll));
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (highlightPhraseMode && anchorWordIndex >= 0) {
      anchorWordIndex = -1;
      selectedFragments.clear();
      selectionDirection = 0;
      requestUpdate();
      return;
    }
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (highlightPhraseMode) {
      confirmHighlightSelection();
    } else {
      lookupSelectedWord();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
    if (highlightPhraseMode && pageLoader && (mappedInput.getHeldTime() >= 450 || rows.empty())) {
      stepSelectionPage(-1);
      return;
    }
    moveRow(-1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
    if (highlightPhraseMode && pageLoader && (mappedInput.getHeldTime() >= 450 || rows.empty())) {
      stepSelectionPage(1);
      return;
    }
    moveRow(1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    moveWord(-1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    moveWord(1);
  }
}

void DictionaryWordSelectActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (pendingPageDirection != 0) {
    renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_LOADING));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }
  std::optional<FontCacheManager::PrewarmScope> fontPrewarm;
  if (page) {
    if (auto* fcm = renderer.getFontCacheManager()) {
      fontPrewarm.emplace(*fcm);
      page->recordFontUsage(*fcm, readerFontId, SETTINGS.bionicReading);
      fontPrewarm->endScanAndPrewarm();
    }
    page->render(renderer, readerFontId, marginLeft, marginTop, SETTINGS.bionicReading);
  }

  if (rows.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_LOOKUP_EMPTY_PAGE));
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sideBackgroundWidth = metrics.sideButtonHintsWidth + 8;
  const int sideBackgroundHeight = 168;
  if (gpio.deviceIsX3()) {
    constexpr int sideY = 151;
    renderer.fillRect(0, sideY, sideBackgroundWidth, sideBackgroundHeight / 2, false);
    renderer.fillRect(renderer.getScreenWidth() - sideBackgroundWidth, sideY, sideBackgroundWidth,
                      sideBackgroundHeight / 2, false);
  } else {
    const int sideY = std::min(341, std::max(0, renderer.getScreenHeight() - sideBackgroundHeight - 4));
    renderer.fillRect(renderer.getScreenWidth() - sideBackgroundWidth, sideY, sideBackgroundWidth, sideBackgroundHeight,
                      false);
  }

  const char* confirmLabel =
      highlightPhraseMode ? I18N.get(anchorWordIndex < 0 ? StrId::STR_HIGHLIGHT_START : StrId::STR_SAVE_HIGHLIGHT)
                          : tr(STR_SELECT);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  if (highlightPhraseMode && pageLoader) {
    renderer.drawCenteredText(SMALL_FONT_ID, renderer.getScreenHeight() - metrics.buttonHintsHeight - 16,
                              tr(STR_SELECTION_PAGE_HINT));
  }

  if (!highlightPhraseMode) {
    storeSelectionBaseRegions();
  }
  prewarmCurrentSelectionText();
  drawSelectionHighlight();
  if (selectionPageError) GUI.drawPopup(renderer, tr(STR_PAGE_LOAD_ERROR));
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
