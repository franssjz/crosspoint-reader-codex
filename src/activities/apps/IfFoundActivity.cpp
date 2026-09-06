#include "IfFoundActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>
#include <vector>

#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/IfFoundFile.h"

namespace {
std::string trim(const std::string& value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::vector<std::string> splitLinesPreserveEmpty(const std::string& value) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start <= value.size()) {
    const size_t end = value.find('\n', start);
    if (end == std::string::npos) {
      lines.push_back(value.substr(start));
      break;
    }
    lines.push_back(value.substr(start, end - start));
    start = end + 1;
  }
  return lines;
}
}  // namespace

void IfFoundActivity::loadContent() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int textWidth = renderer.getScreenWidth() - metrics.contentSidePadding * 2 - metrics.scrollBarWidth -
                        metrics.scrollBarRightOffset - 8;

  introLines = renderer.wrappedText(UI_10_FONT_ID, tr(STR_IF_FOUND_MESSAGE), textWidth, 8, EpdFontFamily::REGULAR);

  std::string body = IfFoundFile::readNormalized(IfFoundFile::findPath());
  if (trim(body).empty()) {
    body = tr(STR_IF_FOUND_FILE_HINT);
  }

  bodyLines.clear();
  for (const auto& rawLine : splitLinesPreserveEmpty(body)) {
    if (trim(rawLine).empty()) {
      bodyLines.emplace_back("");
      continue;
    }

    const auto wrapped = renderer.wrappedText(UI_10_FONT_ID, rawLine.c_str(), textWidth, 64, EpdFontFamily::BOLD);
    if (wrapped.empty()) {
      bodyLines.push_back(rawLine);
      continue;
    }
    bodyLines.insert(bodyLines.end(), wrapped.begin(), wrapped.end());
  }

  while (!bodyLines.empty() && bodyLines.back().empty()) {
    bodyLines.pop_back();
  }

  scrollOffset = std::clamp(scrollOffset, 0, getMaxScrollOffset());
}

int IfFoundActivity::getVisibleBodyLineCount() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int introHeight = static_cast<int>(introLines.size()) * lineHeight;
  const int bodyTop = contentTop + introHeight + 16;
  const int viewportHeight = pageHeight - bodyTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  return std::max(1, viewportHeight / lineHeight);
}

int IfFoundActivity::getMaxScrollOffset() const {
  return std::max(0, static_cast<int>(bodyLines.size()) - getVisibleBodyLineCount());
}

void IfFoundActivity::onEnter() {
  Activity::onEnter();
  loadContent();
  requestUpdate();
}

void IfFoundActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  // Touch: vertical swipes page through the body text (up = forward).
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    const int maxScrollOffset = getMaxScrollOffset();
    const int page = getVisibleBodyLineCount();
    const int next =
        std::clamp(scrollOffset + (swipe == MappedInputManager::SwipeDir::Up ? page : -page), 0, maxScrollOffset);
    if (next != scrollOffset) {
      scrollOffset = next;
      requestUpdate();
    }
    return;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right, MappedInputManager::Button::Down}, [this] {
    const int maxScrollOffset = getMaxScrollOffset();
    if (maxScrollOffset <= 0) {
      return;
    }
    scrollOffset = std::min(scrollOffset + 1, maxScrollOffset);
    requestUpdate();
  });

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left, MappedInputManager::Button::Up}, [this] {
    if (scrollOffset <= 0) {
      return;
    }
    scrollOffset = std::max(0, scrollOffset - 1);
    requestUpdate();
  });
}

void IfFoundActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int sidePadding = metrics.contentSidePadding;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_IF_FOUND_RETURN_ME));

  int currentY = contentTop;
  for (const auto& line : introLines) {
    renderer.drawText(UI_10_FONT_ID, sidePadding, currentY, line.c_str(), true, EpdFontFamily::REGULAR);
    currentY += lineHeight;
  }

  currentY += 8;
  renderer.drawLine(sidePadding, currentY, pageWidth - sidePadding, currentY);
  currentY += 8;

  const int bodyTop = currentY;
  const int viewportHeight = pageHeight - bodyTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int visibleLines = std::max(1, viewportHeight / lineHeight);
  const int endLine = std::min(static_cast<int>(bodyLines.size()), scrollOffset + visibleLines);

  int textY = bodyTop;
  for (int i = scrollOffset; i < endLine; ++i) {
    if (!bodyLines[i].empty()) {
      renderer.drawText(UI_10_FONT_ID, sidePadding, textY, bodyLines[i].c_str(), true, EpdFontFamily::BOLD);
    }
    textY += lineHeight;
  }

  if (static_cast<int>(bodyLines.size()) > visibleLines) {
    const int scrollTrackX = pageWidth - metrics.scrollBarRightOffset;
    const int maxScrollOffset = getMaxScrollOffset();
    const int scrollBarHeight = std::max(18, (viewportHeight * visibleLines) / static_cast<int>(bodyLines.size()));
    const int scrollBarY =
        bodyTop + ((viewportHeight - scrollBarHeight) * std::min(scrollOffset, maxScrollOffset)) / maxScrollOffset;
    renderer.drawLine(scrollTrackX, bodyTop, scrollTrackX, bodyTop + viewportHeight, true);
    renderer.fillRect(scrollTrackX - metrics.scrollBarWidth + 1, scrollBarY, metrics.scrollBarWidth, scrollBarHeight,
                      true);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
