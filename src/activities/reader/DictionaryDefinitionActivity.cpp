#include "DictionaryDefinitionActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cctype>
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

void DictionaryDefinitionActivity::prewarmVisibleDefinitionText() const {
  auto* fcm = renderer.getFontCacheManager();
  if (!fcm) return;

  const int startLine = currentPage * linesPerPage;
  std::string visibleText;
  visibleText.reserve(512);
  for (int i = 0; i < linesPerPage && startLine + i < static_cast<int>(wrappedLines.size()); ++i) {
    if (!visibleText.empty()) visibleText.push_back('\n');
    visibleText += wrappedLines[startLine + i];
  }

  if (!visibleText.empty()) {
    fcm->prewarmCache(definitionFontId, visibleText.c_str(), 0x01);
  }
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

void DictionaryDefinitionActivity::loop() {
  bool prevPage = mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
                  mappedInput.wasReleased(MappedInputManager::Button::Left);
  bool nextPage = mappedInput.wasReleased(MappedInputManager::Button::PageForward) ||
                  mappedInput.wasReleased(MappedInputManager::Button::Right);

  // Touch (upstream): same zones as the reader page turns inside the overlay --
  // left third = previous page, the rest = next. A tap outside the overlay box
  // closes it like Back; the left-edge back swipe cancels as well.
  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    const Rect rect = overlayRect();
    const bool insideOverlay = tx >= rect.x && tx < rect.x + rect.width && ty >= rect.y && ty < rect.y + rect.height;
    if (!insideOverlay) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
      return;
    }
    if (tx < rect.x + rect.width / 3) {
      prevPage = true;
    } else {
      nextPage = true;
    }
  } else if (mappedInput.wasBackGesture()) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (prevPage && currentPage > 0) {
    --currentPage;
    requestUpdate();
    return;
  }
  if (nextPage && currentPage + 1 < totalPages) {
    ++currentPage;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    setResult(ActivityResult{});
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
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
  const std::string title = renderer.truncatedText(UI_10_FONT_ID, headword.c_str(), titleMaxWidth, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, rect.x + padding, titleY, title.c_str(), true, EpdFontFamily::BOLD);

  if (totalPages > 1) {
    const std::string pageText = std::to_string(currentPage + 1) + "/" + std::to_string(totalPages);
    const int pageWidth = renderer.getTextWidth(SMALL_FONT_ID, pageText.c_str());
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - padding - pageWidth, titleY + 2, pageText.c_str());
  }

  const int lineHeight = renderer.getLineHeight(definitionFontId);
  const int separatorY = titleY + renderer.getLineHeight(UI_10_FONT_ID) + 12;
  renderer.drawLine(rect.x + padding, separatorY, rect.x + rect.width - padding - 1, separatorY, true);

  const int bodyY = separatorY + 10;
  const int startLine = currentPage * linesPerPage;
  prewarmVisibleDefinitionText();
  for (int i = 0; i < linesPerPage && startLine + i < static_cast<int>(wrappedLines.size()); ++i) {
    renderer.drawText(definitionFontId, rect.x + padding, bodyY + i * lineHeight, wrappedLines[startLine + i].c_str());
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DONE), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
