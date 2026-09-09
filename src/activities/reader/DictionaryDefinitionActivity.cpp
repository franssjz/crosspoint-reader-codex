#include "DictionaryDefinitionActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <optional>
#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr size_t MAX_WRAPPED_DEFINITION_LINES = 180;
}

void DictionaryDefinitionActivity::onEnter() {
  Activity::onEnter();
  trail.push(headword.c_str());
  prepareDefinitionFontMetrics();
  wrapText();
  requestUpdate();
}

void DictionaryDefinitionActivity::onExit() {
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->clearCache();
  }
  Activity::onExit();
}

Rect DictionaryDefinitionActivity::overlayRect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int margin = std::max(8, metrics.contentSidePadding / 2);
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int height = std::max((screenHeight * 3) / 4, 160);
  return Rect{margin, screenHeight - height - margin, screenWidth - margin * 2, height};
}

void DictionaryDefinitionActivity::prepareDefinitionFontMetrics() {
  if (!renderer.isSdCardFont(definitionFontId)) return;

  if (!definition.empty()) {
    renderer.ensureSdCardFontReady(definitionFontId, definition.c_str(), 0x01);
  }

  if (truncated) {
    const std::string marker = std::string("[") + tr(STR_DEFINITION_TRUNCATED) + "]";
    renderer.ensureSdCardFontReady(definitionFontId, marker.c_str(), 0x01);
  }
}

int DictionaryDefinitionActivity::measureDefinitionText(const char* text) const {
  return renderer.getTextAdvanceX(definitionFontId, text, EpdFontFamily::REGULAR);
}

void DictionaryDefinitionActivity::wrapText() {
  wrappedLines.clear();
  bool wrapLimitReached = false;
  const Rect rect = overlayRect();
  const int padding = 10;
  const int maxWidth = std::max(80, rect.width - padding * 2);
  const int lineHeight = std::max(1, renderer.getLineHeight(definitionFontId));
  const int headerHeight = renderer.getLineHeight(UI_10_FONT_ID) + 20;
  const int footerHeight = renderer.getLineHeight(SMALL_FONT_ID) + 8;
  linesPerPage = std::max(1, (rect.height - headerHeight - footerHeight - padding) / lineHeight);

  auto utf8UnitLength = [](const unsigned char c) -> size_t {
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
  };

  auto trimTrailingSpaces = [](std::string& line) {
    while (!line.empty() && line.back() == ' ') line.pop_back();
  };

  auto appendWrappedLine = [&](std::string line) {
    if (wrappedLines.size() >= MAX_WRAPPED_DEFINITION_LINES) {
      wrapLimitReached = true;
      return false;
    }
    wrappedLines.push_back(std::move(line));
    return true;
  };

  auto expandTabs = [](const std::string& line) {
    std::string expanded;
    expanded.reserve(line.size());
    for (const char c : line) {
      if (c == '\t') {
        expanded += "  ";
      } else {
        expanded.push_back(c);
      }
    }
    return expanded;
  };

  auto continuationPrefixFor = [](const std::string& line, const std::string& prefix) {
    size_t pos = prefix.size();
    if (pos + 1 < line.size() && (line[pos] == '-' || line[pos] == '*' || line[pos] == '+') && line[pos + 1] == ' ') {
      return prefix + "  ";
    }

    const size_t numberStart = pos;
    while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos]))) ++pos;
    if (pos > numberStart && pos + 1 < line.size() && (line[pos] == '.' || line[pos] == ')') && line[pos + 1] == ' ') {
      return prefix + std::string(pos + 2 - prefix.size(), ' ');
    }
    return prefix;
  };

  auto appendLongToken = [&](const std::string& token, std::string& currentLine, std::string activePrefix,
                             const std::string& continuationPrefix, bool& hasContent) {
    for (size_t pos = 0; pos < token.size();) {
      const size_t unitLen = std::min(utf8UnitLength(static_cast<unsigned char>(token[pos])), token.size() - pos);
      const std::string unit = token.substr(pos, unitLen);
      const std::string test = currentLine + unit;
      if (currentLine.size() > activePrefix.size() && measureDefinitionText(test.c_str()) > maxWidth) {
        trimTrailingSpaces(currentLine);
        if (!appendWrappedLine(currentLine)) return;
        activePrefix = continuationPrefix;
        currentLine = activePrefix;
        hasContent = false;
        continue;
      }
      currentLine = test;
      hasContent = true;
      pos += unitLen;
    }
  };

  auto wrapSourceLine = [&](std::string sourceLine) {
    if (wrapLimitReached) return;
    sourceLine = expandTabs(sourceLine);
    trimTrailingSpaces(sourceLine);
    if (sourceLine.empty()) {
      appendWrappedLine(std::string());
      return;
    }

    size_t firstText = 0;
    while (firstText < sourceLine.size() && sourceLine[firstText] == ' ') ++firstText;
    const std::string firstPrefix = sourceLine.substr(0, std::min<size_t>(firstText, 4));
    const std::string continuationPrefix = continuationPrefixFor(sourceLine, firstPrefix);

    std::string currentLine = firstPrefix;
    bool hasContent = false;
    size_t pos = firstText;
    while (pos < sourceLine.size()) {
      size_t spaces = 0;
      while (pos < sourceLine.size() && sourceLine[pos] == ' ') {
        ++spaces;
        ++pos;
      }
      if (pos >= sourceLine.size()) break;

      const size_t tokenStart = pos;
      while (pos < sourceLine.size() && sourceLine[pos] != ' ') ++pos;
      const std::string token = sourceLine.substr(tokenStart, pos - tokenStart);
      const std::string separator = hasContent ? (spaces > 1 ? "  " : " ") : "";
      const std::string test = currentLine + separator + token;
      if (measureDefinitionText(test.c_str()) <= maxWidth) {
        currentLine = test;
        hasContent = true;
        continue;
      }

      if (hasContent) {
        trimTrailingSpaces(currentLine);
        if (!appendWrappedLine(currentLine)) return;
        currentLine = continuationPrefix;
        hasContent = false;
      }

      const std::string prefixedToken = currentLine + token;
      if (measureDefinitionText(prefixedToken.c_str()) <= maxWidth) {
        currentLine = prefixedToken;
        hasContent = true;
      } else {
        appendLongToken(token, currentLine, currentLine, continuationPrefix, hasContent);
      }
    }

    trimTrailingSpaces(currentLine);
    if (hasContent || currentLine.size() > firstPrefix.size()) {
      appendWrappedLine(currentLine);
    } else {
      appendWrappedLine(std::string());
    }
  };

  size_t lineStart = 0;
  for (size_t i = 0; i <= definition.size(); ++i) {
    if (i == definition.size() || definition[i] == '\n') {
      wrapSourceLine(definition.substr(lineStart, i - lineStart));
      if (wrapLimitReached) break;
      lineStart = i + 1;
    }
  }

  if (truncated || wrapLimitReached) {
    const std::string marker = std::string("[") + tr(STR_DEFINITION_TRUNCATED) + "]";
    if (wrappedLines.size() < MAX_WRAPPED_DEFINITION_LINES) {
      wrappedLines.push_back(marker);
    } else if (!wrappedLines.empty()) {
      wrappedLines.back() = marker;
    }
  }
  totalPages = std::max(1, (static_cast<int>(wrappedLines.size()) + linesPerPage - 1) / linesPerPage);
  currentPage = std::clamp(currentPage, 0, totalPages - 1);
}

void DictionaryDefinitionActivity::updateWordSelection() {
  selectableWordCount = 0;
  selectedWord.clear();
  DictionaryNavigation::WordSpan selectedSpan;
  const int startLine = currentPage * linesPerPage;
  for (int line = startLine; line < startLine + linesPerPage && line < static_cast<int>(wrappedLines.size()); ++line) {
    size_t cursor = 0;
    DictionaryNavigation::WordSpan span;
    while (DictionaryNavigation::nextWord(wrappedLines[line], cursor, span)) {
      // When the requested ordinal is beyond this page, retain the last word.
      if (selectableWordCount <= selectedWordOrdinal) {
        selectedSpan = span;
        selectedWordLine = line;
      }
      ++selectableWordCount;
    }
  }
  if (selectableWordCount == 0) return;
  selectedWordOrdinal = std::clamp(selectedWordOrdinal, 0, selectableWordCount - 1);
  const auto& line = wrappedLines[selectedWordLine];
  selectedWord = line.substr(selectedSpan.offset, selectedSpan.length);
  const std::string prefix = line.substr(0, selectedSpan.offset);
  selectedWordX = measureDefinitionText(prefix.c_str());
  selectedWordWidth = std::max(1, measureDefinitionText(selectedWord.c_str()));
}

void DictionaryDefinitionActivity::openDictionaryPicker() {
  DICTIONARIES.ensureScanned();
  if (DICTIONARIES.getEntries().empty()) {
    lookupStatus = DictionaryLookupResult::Status::NoDictionary;
    view = View::LookupError;
    return;
  }
  menuIndex = std::max(0, DICTIONARIES.getActiveIndex());
  view = View::Dictionaries;
}

bool DictionaryDefinitionActivity::lookupWord(const std::string& query, const bool append, const int restorePage) {
  pendingQuery = query;
  pendingAppend = append;
  chainLimitReached = append && !trail.canPush();
  if (chainLimitReached) {
    view = View::LookupError;
    requestUpdate();
    return false;
  }
  if (auto* fcm = renderer.getFontCacheManager()) fcm->clearCache();
  auto lookup = DICTIONARIES.lookup(pendingQuery, false);
  lookupStatus = lookup.status;
  if (lookup.status != DictionaryLookupResult::Status::Found) {
    view = View::LookupError;
    requestUpdate();
    return false;
  }
  if (append) {
    trail.setPage(currentPage);
    if (!trail.push(lookup.headword.c_str())) {
      chainLimitReached = true;
      view = View::LookupError;
      requestUpdate();
      return false;
    }
  }
  headword = std::move(lookup.headword);
  definition = std::move(lookup.definition);
  truncated = lookup.truncated;
  currentPage = restorePage;
  definitionFontId = DICTIONARIES.getDefinitionFontId(readerFontId);
  prepareDefinitionFontMetrics();
  wrapText();
  selectedWord.clear();
  view = View::Definition;
  requestUpdate();
  return true;
}

void DictionaryDefinitionActivity::returnToPreviousLookup() {
  const auto* previous = trail.previous();
  if (!previous) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }
  // Leave the trail and current definition intact if reloading fails.
  if (lookupWord(previous->query, false, previous->page)) {
    trail.pop();
    trail.setPage(currentPage);
  }
}

void DictionaryDefinitionActivity::finishDefinition() {
  setResult(ActivityResult{});
  finish();
}

void DictionaryDefinitionActivity::loop() {
  if (!mappedInput.wasAnyReleased()) return;
  // The render task reads definition, wrapped lines, selection and font caches.
  // Keep a lookup/reflow and its resulting view transition under the same lock.
  RenderLock lock(*this);
  const bool back = mappedInput.wasReleased(MappedInputManager::Button::Back);
  const bool confirm = mappedInput.wasReleased(MappedInputManager::Button::Confirm);
  const bool previous = mappedInput.wasReleased(MappedInputManager::Button::Left) ||
                        mappedInput.wasReleased(MappedInputManager::Button::Up);
  const bool next = mappedInput.wasReleased(MappedInputManager::Button::Right) ||
                    mappedInput.wasReleased(MappedInputManager::Button::Down);
  const bool previousPage = mappedInput.wasReleased(MappedInputManager::Button::PageBack);
  const bool nextPage = mappedInput.wasReleased(MappedInputManager::Button::PageForward);

  if (back) {
    if (view == View::Definition)
      returnToPreviousLookup();
    else {
      view = View::Definition;
      selectedWord.clear();
      chainLimitReached = false;
      requestUpdate();
    }
    return;
  }

  if (view == View::Definition) {
    if (confirm) {
      menuIndex = 0;
      view = View::Actions;
    } else if ((previous || previousPage) && currentPage > 0)
      --currentPage;
    else if ((next || nextPage) && currentPage + 1 < totalPages)
      ++currentPage;
  } else if (view == View::Actions) {
    if (previous || previousPage) menuIndex = (menuIndex + 2) % 3;
    if (next || nextPage) menuIndex = (menuIndex + 1) % 3;
    if (confirm) {
      if (menuIndex == 0) {
        selectedWordOrdinal = 0;
        updateWordSelection();
        view = View::WordSelection;
      } else if (menuIndex == 1) {
        pendingQuery = headword;
        pendingAppend = false;
        openDictionaryPicker();
      } else {
        finishDefinition();
        return;
      }
    }
  } else if (view == View::WordSelection) {
    if (confirm && !selectedWord.empty()) {
      const std::string query = DictionaryStore::cleanWord(selectedWord);
      if (!query.empty()) lookupWord(query, true);
    } else {
      if (previousPage && currentPage > 0) {
        --currentPage;
        selectedWordOrdinal = INT_MAX;
      } else if (nextPage && currentPage + 1 < totalPages) {
        ++currentPage;
        selectedWordOrdinal = 0;
      } else if (previous) {
        if (selectedWordOrdinal > 0)
          --selectedWordOrdinal;
        else if (currentPage > 0) {
          --currentPage;
          selectedWordOrdinal = INT_MAX;
        }
      } else if (next) {
        if (selectedWordOrdinal + 1 < selectableWordCount)
          ++selectedWordOrdinal;
        else if (currentPage + 1 < totalPages) {
          ++currentPage;
          selectedWordOrdinal = 0;
        }
      }
      updateWordSelection();
    }
  } else if (view == View::Dictionaries) {
    const auto& entries = DICTIONARIES.getEntries();
    const int count = static_cast<int>(entries.size());
    if (count > 0) {
      if (previous || previousPage) menuIndex = (menuIndex + count - 1) % count;
      if (next || nextPage) menuIndex = (menuIndex + 1) % count;
      if (confirm) {
        if (DICTIONARIES.setActiveIndex(menuIndex))
          lookupWord(pendingQuery, pendingAppend);
        else {
          lookupStatus = entries[menuIndex].compressed ? DictionaryLookupResult::Status::NotReady
                                                       : DictionaryLookupResult::Status::IoError;
          view = View::LookupError;
        }
      }
    }
  } else if (view == View::LookupError && confirm && !chainLimitReached) {
    openDictionaryPicker();
  }
  requestUpdate();
}

void DictionaryDefinitionActivity::renderMenu(const Rect& rect, const int bodyY) {
  constexpr int padding = 10;
  const int rowHeight = renderer.getLineHeight(UI_10_FONT_ID) + 12;
  if (view == View::LookupError) {
    const char* message =
        chainLimitReached ? tr(STR_DICTIONARY_CHAIN_LIMIT) : DictionaryStore::lookupErrorMessage(lookupStatus);
    const auto lines = renderer.wrappedText(UI_10_FONT_ID, message, rect.width - padding * 2, 4);
    for (size_t i = 0; i < lines.size(); ++i) {
      renderer.drawText(UI_10_FONT_ID, rect.x + padding, bodyY + static_cast<int>(i) * rowHeight, lines[i].c_str());
    }
    return;
  }
  const int rows = std::max(1, (rect.y + rect.height - bodyY - 16) / rowHeight);
  const int first = (menuIndex / rows) * rows;
  const auto& entries = DICTIONARIES.getEntries();
  const int count = view == View::Actions ? 3 : static_cast<int>(entries.size());
  for (int i = first; i < std::min(count, first + rows); ++i) {
    std::string label;
    if (view == View::Actions) {
      label = i == 0 ? tr(STR_LOOK_UP_WORD) : i == 1 ? tr(STR_CHANGE_DICTIONARY) : tr(STR_DONE);
    } else
      label = entries[i].languageId + " - " + entries[i].name;
    const std::string fitted = renderer.truncatedText(UI_10_FONT_ID, label.c_str(), rect.width - padding * 2 - 4);
    const int y = bodyY + (i - first) * rowHeight;
    if (i == menuIndex)
      renderer.fillRect(rect.x + padding - 2, y - 2, rect.width - padding * 2 + 4, rowHeight - 2, true);
    renderer.drawText(UI_10_FONT_ID, rect.x + padding, y, fitted.c_str(), i != menuIndex);
  }
}

void DictionaryDefinitionActivity::render(RenderLock&&) {
  if (renderPageBackground) {
    renderer.clearScreen();
    std::optional<FontCacheManager::PrewarmScope> pageFontPrewarm;
    if (page) {
      if (auto* fcm = renderer.getFontCacheManager()) {
        pageFontPrewarm.emplace(*fcm);
        page->recordFontUsage(*fcm, readerFontId, SETTINGS.bionicReading);
        pageFontPrewarm->endScanAndPrewarm();
      }
      page->render(renderer, readerFontId, marginLeft, marginTop, SETTINGS.bionicReading);
    }
  }

  const Rect rect = overlayRect();
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height, 2, true);

  const int padding = 10;
  const int titleY = rect.y + padding;
  const int titleMaxWidth = rect.width - padding * 2 - 54;
  const char* rawTitle = view == View::Dictionaries  ? tr(STR_CHANGE_DICTIONARY)
                         : view == View::LookupError ? pendingQuery.c_str()
                                                     : headword.c_str();
  const std::string title = renderer.truncatedText(UI_10_FONT_ID, rawTitle, titleMaxWidth, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, rect.x + padding, titleY, title.c_str(), true, EpdFontFamily::BOLD);

  if (totalPages > 1 && (view == View::Definition || view == View::WordSelection)) {
    const std::string pageText = std::to_string(currentPage + 1) + "/" + std::to_string(totalPages);
    const int pageWidth = renderer.getTextWidth(SMALL_FONT_ID, pageText.c_str());
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - padding - pageWidth, titleY + 2, pageText.c_str());
  }

  const int lineHeight = renderer.getLineHeight(definitionFontId);
  const int separatorY = titleY + renderer.getLineHeight(UI_10_FONT_ID) + 12;
  renderer.drawLine(rect.x + padding, separatorY, rect.x + rect.width - padding - 1, separatorY, true);

  const int bodyY = separatorY + 10;
  const int startLine = currentPage * linesPerPage;
  std::optional<FontCacheManager::PrewarmScope> definitionPrewarm;
  if (view == View::Definition || view == View::WordSelection) {
    if (auto* fcm = renderer.getFontCacheManager()) {
      definitionPrewarm.emplace(*fcm);
      for (int i = 0; i < linesPerPage && startLine + i < static_cast<int>(wrappedLines.size()); ++i) {
        fcm->recordText(wrappedLines[startLine + i].c_str(), definitionFontId, EpdFontFamily::REGULAR);
      }
      definitionPrewarm->endScanAndPrewarm();
    }
    for (int i = 0; i < linesPerPage && startLine + i < static_cast<int>(wrappedLines.size()); ++i) {
      renderer.drawText(definitionFontId, rect.x + padding, bodyY + i * lineHeight,
                        wrappedLines[startLine + i].c_str());
    }
    if (view == View::WordSelection && !selectedWord.empty()) {
      const int y = bodyY + (selectedWordLine - startLine) * lineHeight;
      const int x = rect.x + padding + selectedWordX;
      renderer.fillRect(x, y, std::min(selectedWordWidth + 2, rect.x + rect.width - padding - x), lineHeight, true);
      renderer.drawText(definitionFontId, x, y, selectedWord.c_str(), false);
    }
  } else {
    renderMenu(rect, bodyY);
  }

  const char* confirmLabel = view == View::Definition      ? tr(STR_DICTIONARY)
                             : view == View::WordSelection ? tr(STR_LOOK_UP_WORD)
                             : view == View::LookupError   ? (chainLimitReached ? "" : tr(STR_CHANGE_DICTIONARY))
                                                           : tr(STR_SELECT);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
