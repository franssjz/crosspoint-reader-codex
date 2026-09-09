#include "KeyboardEntryActivity.h"

#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "KeyboardText.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

const char* const KeyboardEntryActivity::shiftString[2] = {"shift", "SHIFT"};

void KeyboardEntryActivity::onEnter() {
  Activity::onEnter();
  RenderLock lock(*this);
  enabledLayouts = KeyboardLayoutSet::normalizeMask(SETTINGS.keyboardLayouts);
  layoutId = inputType == InputType::Url ? KeyboardLayoutSet::Qwerty : KeyboardLayoutSet::first(enabledLayouts);
  cursorPos = text.length();
  symMode = false;
  urlMode = false;
  cursorMode = false;
  togglePos = false;
  passwordVisible = false;
  shiftState = 0;
  selectedRow = 0;
  selectedCol = 0;
  delPressCount = 0;
  hintVisible = false;
  hintShowTime = 0;
  rightHeld = false;
  rightLongHandled = false;
  savedCursorPos = 0;
  rightStartCursorPos = 0;
  requestUpdate();
}

void KeyboardEntryActivity::onExit() { Activity::onExit(); }

int KeyboardEntryActivity::getContentRowCount() const {
  if (urlMode) return 3;
  return ABC_ROWS;
}

int KeyboardEntryActivity::getContentColCount() const {
  if (urlMode) return 3;
  return symMode || inputType == InputType::Url ? COLS : KeyboardLayoutSet::columns(layoutId);
}

int KeyboardEntryActivity::getTotalRowCount() const { return getContentRowCount() + 1; }

bool KeyboardEntryActivity::isBottomRow(const int row) const { return row == getContentRowCount(); }

KeyDef KeyboardEntryActivity::getKey(int row, int column) const {
  if (row < 0 || row >= ABC_ROWS || column < 0 || column >= getContentColCount()) return {};
  if (symMode) return symLayout[row][column];
  if (inputType == InputType::Url) return urlLayout[row][column];
  return KeyboardLayoutSet::key(layoutId, row, column);
}

uint32_t KeyboardEntryActivity::getSelectedChar() const {
  const KeyDef key = getKey(selectedRow, selectedCol);
  return shiftState > 0 && key.secondary ? key.secondary : key.primary;
}

uint32_t KeyboardEntryActivity::getAlternativeChar() const {
  if (symMode || urlMode || (inputType == InputType::Url && selectedRow > 0)) return 0;
  const KeyDef key = getKey(selectedRow, selectedCol);
  return shiftState > 0 ? key.primary : key.secondary;
}

bool KeyboardEntryActivity::insertChar(uint32_t c) {
  KeyboardText::insert(text, cursorPos, c, maxLength);
  return true;
}

void KeyboardEntryActivity::insertString(const std::string& str) {
  if (str.empty()) return;
  if (maxLength != 0 && text.length() + str.length() > maxLength) return;
  if (cursorPos > text.length()) cursorPos = text.length();

  text.insert(cursorPos, str);
  cursorPos += str.length();
}

bool KeyboardEntryActivity::handleKeyPress() {
  if (isBottomRow(selectedRow)) {
    switch (static_cast<SpecialKeyType>(selectedCol)) {
      case SpecialKeyType::Shift:
        delPressCount = 0;
        hintVisible = false;
        if (urlMode || inputType == InputType::Url) return true;
        if (symMode) return true;
        shiftState = (shiftState + 1) % 2;
        return true;
      case SpecialKeyType::Mode: {
        delPressCount = 0;
        hintVisible = false;
        if (urlMode) {
          urlMode = false;
          symMode = false;
          selectedRow = getTotalRowCount() - 1;
          selectedCol = static_cast<int>(SpecialKeyType::Mode);
          requestUpdate();
          return true;
        }
        symMode = !symMode;
        int maxRow = getTotalRowCount() - 1;
        if (selectedRow > maxRow) selectedRow = maxRow;
        if (isBottomRow(selectedRow)) {
          if (selectedCol >= BOTTOM_KEY_COUNT) selectedCol = BOTTOM_KEY_COUNT - 1;
        } else {
          if (selectedCol >= getContentColCount()) selectedCol = getContentColCount() - 1;
        }
        return true;
      }
      case SpecialKeyType::Space:
        delPressCount = 0;
        hintVisible = false;
        if (inputType == InputType::Url) {
          urlMode = !urlMode;
          if (urlMode) {
            symMode = false;
          }
          selectedRow = getTotalRowCount() - 1;
          selectedCol = static_cast<int>(SpecialKeyType::Space);
          requestUpdate();
        } else {
          return insertChar(' ');
        }
        return true;
      case SpecialKeyType::Del:
        delPressCount++;
        if (delPressCount >= 2) {
          hintVisible = true;
          hintShowTime = millis();
        }
        if (cursorPos > 0 && !text.empty()) {
          KeyboardText::erasePrevious(text, cursorPos);
        }
        return true;
      case SpecialKeyType::Ok:
        delPressCount = 0;
        hintVisible = false;
        onComplete(text);
        return false;
      default:
        return true;
    }
  }

  if (urlMode) {
    delPressCount = 0;
    hintVisible = false;
    const int idx = selectedCol + selectedRow * 3;
    if (idx < URL_SNIPPET_COUNT) {
      insertString(urlSnippets[idx]);
    }
    return true;
  }

  delPressCount = 0;
  hintVisible = false;

  return insertChar(getSelectedChar());
}

void KeyboardEntryActivity::mapColContentBottom(int& col, bool goingUp) const {
  if (urlMode) {
    col = goingUp ? col - 1 : col + 1;
    if (col < 0) col = 0;
    if (col >= 3) col = 2;
  } else {
    const int columns = getContentColCount();
    col = goingUp ? col * columns / BOTTOM_KEY_COUNT : col * BOTTOM_KEY_COUNT / columns;
  }
}

void KeyboardEntryActivity::loop() {
  RenderLock lock(*this);
  const int totalRows = getTotalRowCount();

  if (!cursorMode && mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    upHeld = true;
    upLongHandled = false;
  }

  if (upHeld && !upLongHandled && mappedInput.isPressed(MappedInputManager::Button::Up) &&
      mappedInput.getHeldTime() > LONG_PRESS_MS) {
    cursorMode = true;
    upLongHandled = true;
    hintVisible = true;
    hintShowTime = millis();
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (upHeld && !upLongHandled && !cursorMode) {
      bool wasBottom = isBottomRow(selectedRow);
      const int contentCols = getContentColCount();
      selectedRow = ButtonNavigator::previousIndex(selectedRow, totalRows);
      if (wasBottom && !isBottomRow(selectedRow)) {
        mapColContentBottom(selectedCol, true);
      } else if (!wasBottom && isBottomRow(selectedRow)) {
        mapColContentBottom(selectedCol, false);
      }
      int maxCol = isBottomRow(selectedRow) ? BOTTOM_KEY_COUNT - 1 : contentCols - 1;
      if (selectedCol > maxCol) selectedCol = maxCol;
      requestUpdate();
    }
    upHeld = false;
    upLongHandled = false;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    downHeld = true;
    if (cursorMode) {
      togglePos = false;
      passwordVisible = false;
      cursorMode = false;
      hintVisible = false;
      downLongHandled = true;
      requestUpdate();
    } else {
      downLongHandled = false;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (downHeld && !downLongHandled && !cursorMode) {
      bool wasBottom = isBottomRow(selectedRow);
      const int contentCols = getContentColCount();
      selectedRow = ButtonNavigator::nextIndex(selectedRow, totalRows);
      if (wasBottom && !isBottomRow(selectedRow)) {
        mapColContentBottom(selectedCol, true);
      } else if (!wasBottom && isBottomRow(selectedRow)) {
        mapColContentBottom(selectedCol, false);
      }
      int maxCol = isBottomRow(selectedRow) ? BOTTOM_KEY_COUNT - 1 : contentCols - 1;
      if (selectedCol > maxCol) selectedCol = maxCol;
      requestUpdate();
    }
    downHeld = false;
    downLongHandled = false;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] {
    if (cursorMode) return;
    int maxCol = isBottomRow(selectedRow) ? BOTTOM_KEY_COUNT - 1 : getContentColCount() - 1;
    selectedCol = ButtonNavigator::previousIndex(selectedCol, maxCol + 1);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    if (cursorMode) {
      if (togglePos) {
        cursorPos = savedCursorPos;
        togglePos = false;
        requestUpdate();
      } else if (cursorPos > 0) {
        cursorPos = KeyboardText::previous(text, cursorPos);
        requestUpdate();
      }
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    if (cursorMode && inputType == InputType::Password && !togglePos) {
      rightHeld = true;
      rightLongHandled = false;
      rightStartCursorPos = cursorPos;
    }
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] {
    if (cursorMode) return;
    int maxCol = isBottomRow(selectedRow) ? BOTTOM_KEY_COUNT - 1 : getContentColCount() - 1;
    selectedCol = ButtonNavigator::nextIndex(selectedCol, maxCol + 1);
    requestUpdate();
  });

  if (rightHeld && !rightLongHandled && mappedInput.isPressed(MappedInputManager::Button::Right) &&
      mappedInput.getHeldTime() > LONG_PRESS_MS) {
    if (cursorMode && inputType == InputType::Password && !togglePos) {
      savedCursorPos = rightStartCursorPos;
      togglePos = true;
      rightLongHandled = true;
      requestUpdate();
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    if (cursorMode && inputType == InputType::Password) {
      rightHeld = false;
      rightLongHandled = false;
    }
    if (cursorMode && !togglePos && cursorPos < text.length()) {
      cursorPos = KeyboardText::next(text, cursorPos);
      requestUpdate();
    }
    if (cursorMode) return;
    rightHeld = false;
    rightLongHandled = false;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    confirmHeld = true;
    confirmLongHandled = false;
  }

  if (confirmHeld && !confirmLongHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() > DEL_LONG_PRESS_MS && isBottomRow(selectedRow) &&
      selectedCol == static_cast<int>(SpecialKeyType::Del)) {
    text.clear();
    cursorPos = 0;
    confirmLongHandled = true;
    requestUpdate();
  }

  if (confirmHeld && !confirmLongHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() > LONG_PRESS_MS) {
    if (!cursorMode && !symMode && !urlMode && inputType != InputType::Url && isBottomRow(selectedRow) &&
        selectedCol == static_cast<int>(SpecialKeyType::Mode) && KeyboardLayoutSet::multiple(enabledLayouts)) {
      layoutId = KeyboardLayoutSet::next(enabledLayouts, layoutId);
      shiftState = 0;
      confirmLongHandled = true;
      requestUpdate();
    }
    const uint32_t alt = confirmLongHandled ? 0 : getAlternativeChar();
    if (alt != '\0') {
      insertChar(alt);
      requestUpdate();
      confirmLongHandled = true;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (confirmHeld && !confirmLongHandled && !cursorMode) {
      if (handleKeyPress()) {
        requestUpdate();
      }
    } else if (confirmHeld && !confirmLongHandled && cursorMode && inputType == InputType::Password && togglePos) {
      passwordVisible = !passwordVisible;
      requestUpdate();
    }
    confirmHeld = false;
    confirmLongHandled = false;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onCancel();
  }

  if (hintVisible && !cursorMode && millis() - hintShowTime > 4000) {
    hintVisible = false;
    requestUpdate();
  }
}

void KeyboardEntryActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title.c_str());

  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int inputStartY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing +
                          metrics.verticalSpacing * 4 + metrics.keyboardVerticalOffset;
  int inputHeight = 0;

  const auto displayed =
      KeyboardText::display(text, cursorPos, inputType == InputType::Password && !passwordVisible, !cursorMode);
  const std::string& displayText = displayed.text;
  const size_t displayCursorPos = displayed.cursor;
  renderer.prewarmFallbackText(
      UI_12_FONT_ID, [](const void* raw, uint32_t) { return static_cast<const std::string*>(raw)->c_str(); },
      &displayText, 1);
  if (!symMode && !urlMode && inputType != InputType::Url) {
    const auto keyRows = [](const void* raw, uint32_t row) {
      return KeyboardLayoutSet::rowText(*static_cast<const uint8_t*>(raw), row / 2, row % 2 != 0);
    };
    renderer.prewarmFallbackText(UI_10_FONT_ID, keyRows, &layoutId, 8);
    renderer.prewarmFallbackText(UI_12_FONT_ID, keyRows, &layoutId, 8);
  }
  const std::string cursorGlyph = text.substr(cursorPos, KeyboardText::next(text, cursorPos) - cursorPos);

  const bool isPassword = (inputType == InputType::Password);
  int availableWidth = pageWidth;
  if (gpio.deviceIsX3()) {
    availableWidth -= 2 * metrics.sideButtonHintsWidth;
  }
  const int effectiveMargin = (pageWidth - availableWidth * metrics.keyboardTextFieldWidthPercent / 100) / 2;
  const int toggleGap = isPassword ? 4 : 0;
  const int toggleReserve = isPassword ? std::max(renderer.getTextWidth(UI_12_FONT_ID, "[abc]"),
                                                  renderer.getTextWidth(UI_12_FONT_ID, "[***]")) +
                                             toggleGap
                                       : 0;
  const int textAreaWidth = pageWidth - 2 * effectiveMargin - toggleReserve;
  const int maxLineWidth = textAreaWidth;
  const bool centerText = metrics.keyboardCenteredText;

  int cursorCharWidth = 6;
  if (displayCursorPos < displayText.length()) {
    int w = renderer.getTextWidth(UI_12_FONT_ID, cursorGlyph.c_str());
    if (w > cursorCharWidth) cursorCharWidth = w;
  }

  int lineStartIdx = 0;
  int lineEndIdx = displayText.length();
  int textWidth = 0;
  int cursorPixelX = effectiveMargin;
  int cursorLineY = inputStartY;
  bool cursorDrawn = false;

  while (true) {
    std::string lineText = displayText.substr(lineStartIdx, lineEndIdx - lineStartIdx);
    textWidth = renderer.getTextAdvanceX(UI_12_FONT_ID, lineText.c_str(), EpdFontFamily::REGULAR);
    if (textWidth <= maxLineWidth || static_cast<size_t>(lineEndIdx) <= KeyboardText::next(displayText, lineStartIdx)) {
      const bool isLastLine = (lineEndIdx == static_cast<int>(displayText.length()));
      bool isCursorLine = false;
      if (!cursorDrawn && displayCursorPos >= static_cast<size_t>(lineStartIdx) &&
          (isLastLine ? displayCursorPos <= static_cast<size_t>(lineEndIdx)
                      : displayCursorPos < static_cast<size_t>(lineEndIdx))) {
        std::string beforeCursor;
        if (isPassword && !passwordVisible && cursorMode) {
          beforeCursor = std::string(displayCursorPos - lineStartIdx, '*');
        } else {
          beforeCursor = displayText.substr(lineStartIdx, displayCursorPos - lineStartIdx);
        }
        int beforeWidth = renderer.getTextAdvanceX(UI_12_FONT_ID, beforeCursor.c_str(), EpdFontFamily::REGULAR);
        int kernOffset = 0;
        if (displayCursorPos < displayText.length()) {
          std::string beforeAndCursor =
              beforeCursor + displayText.substr(displayCursorPos,
                                                KeyboardText::next(displayText, displayCursorPos) - displayCursorPos);
          int beforeAndCursorWidth =
              renderer.getTextAdvanceX(UI_12_FONT_ID, beforeAndCursor.c_str(), EpdFontFamily::REGULAR);
          int charAdvance = renderer.getTextAdvanceX(
              UI_12_FONT_ID,
              displayText.substr(displayCursorPos, KeyboardText::next(displayText, displayCursorPos) - displayCursorPos)
                  .c_str(),
              EpdFontFamily::REGULAR);
          kernOffset = beforeAndCursorWidth - beforeWidth - charAdvance;
        }
        if (centerText) {
          cursorPixelX = effectiveMargin + (maxLineWidth - textWidth) / 2 + beforeWidth + kernOffset;
        } else {
          cursorPixelX = effectiveMargin + beforeWidth + kernOffset;
        }
        cursorLineY = inputStartY + inputHeight;
        cursorDrawn = true;
        isCursorLine = true;
      }

      const int lineStartX = centerText ? effectiveMargin + (maxLineWidth - textWidth) / 2 : effectiveMargin;
      if (isCursorLine && cursorMode && isPassword && !passwordVisible && !togglePos) {
        const std::string part1 = displayText.substr(lineStartIdx, displayCursorPos - lineStartIdx);
        renderer.drawText(UI_12_FONT_ID, lineStartX, inputStartY + inputHeight, part1.c_str());
        const int afterStart = static_cast<int>(KeyboardText::next(displayText, displayCursorPos));
        const int afterEnd = lineEndIdx;
        if (afterStart < afterEnd) {
          const std::string part3 = displayText.substr(afterStart, afterEnd - afterStart);
          renderer.drawText(UI_12_FONT_ID, cursorPixelX + cursorCharWidth, inputStartY + inputHeight, part3.c_str());
        }
      } else {
        renderer.drawText(UI_12_FONT_ID, lineStartX, inputStartY + inputHeight, lineText.c_str());
      }
      if (lineEndIdx == static_cast<int>(displayText.length())) {
        break;
      }

      inputHeight += lineHeight;
      lineStartIdx = lineEndIdx;
      lineEndIdx = displayText.length();
    } else {
      lineEndIdx = static_cast<int>(KeyboardText::previous(displayText, lineEndIdx));
    }
  }

  const int fieldWidth = (inputHeight > 0) ? maxLineWidth : textWidth;
  const int lineMargin = effectiveMargin;
  GUI.drawTextField(renderer, Rect{0, inputStartY, pageWidth, inputHeight}, fieldWidth, cursorMode, lineMargin,
                    pageWidth - 2 * lineMargin);

  if (cursorMode && !togglePos && displayCursorPos <= displayText.length()) {
    static constexpr int blockPadding = 1;
    renderer.fillRect(cursorPixelX - blockPadding, cursorLineY, cursorCharWidth + blockPadding * 2, lineHeight, true);
    if (displayCursorPos < displayText.length()) {
      renderer.drawText(UI_12_FONT_ID, cursorPixelX, cursorLineY, cursorGlyph.c_str(), false);
    }
  } else if (displayCursorPos <= displayText.length()) {
    static constexpr int serifW = 3;
    const int cX = cursorPixelX;
    const int cY = cursorLineY;
    const int cBottom = cursorLineY + lineHeight - 1;
    renderer.fillRect(cX, cY, 2, lineHeight, true);
    renderer.drawLine(cX - serifW, cY, cX - 1, cY, 2, true);
    renderer.drawLine(cX + 1, cY, cX + serifW, cY, 2, true);
    renderer.drawLine(cX - serifW, cBottom, cX - 1, cBottom, 2, true);
    renderer.drawLine(cX + 1, cBottom, cX + serifW, cBottom, 2, true);
  }

  if (isPassword) {
    const char* toggleLabel = passwordVisible ? "[***]" : "[abc]";
    const int toggleWidth = renderer.getTextWidth(UI_12_FONT_ID, toggleLabel);
    const int toggleX = pageWidth - effectiveMargin - toggleWidth;
    const int toggleY = inputStartY + inputHeight;
    const bool toggleSelected = cursorMode && togglePos;

    if (toggleSelected) {
      renderer.fillRect(toggleX - 2, toggleY, toggleWidth + 5, lineHeight + 3, true);
      renderer.drawText(UI_12_FONT_ID, toggleX, toggleY, toggleLabel, false);
    } else {
      renderer.drawText(UI_12_FONT_ID, toggleX, toggleY, toggleLabel, true);
    }
  }

  if (hintVisible && !text.empty()) {
    const int hintLh = renderer.getLineHeight(SMALL_FONT_ID);
    const int underlineY = inputStartY + inputHeight + lineHeight + metrics.verticalSpacing;
    const int hintY = underlineY + 4;
    if (cursorMode) {
      int hintLineY = hintY;
      if (inputType == InputType::Password && togglePos) {
        renderer.drawCenteredText(
            SMALL_FONT_ID, hintLineY,
            passwordVisible ? tr(STR_KB_HINT_TOGGLE_HIDE_PASSWORD) : tr(STR_KB_HINT_TOGGLE_SHOW_PASSWORD), true);
      } else {
        renderer.drawCenteredText(SMALL_FONT_ID, hintLineY, tr(STR_KB_HINT_MOVE_CURSOR), true);
      }
      hintLineY += hintLh;
      if (inputType == InputType::Password) {
        const char* passTip;
        if (togglePos) {
          passTip = tr(STR_KB_HINT_RETURN_CURSOR);
        } else {
          passTip = passwordVisible ? tr(STR_KB_HINT_HIDE_PASSWORD) : tr(STR_KB_HINT_SHOW_PASSWORD);
        }
        renderer.drawCenteredText(SMALL_FONT_ID, hintLineY, passTip, true);
      }
    } else {
      renderer.drawCenteredText(SMALL_FONT_ID, hintY, tr(STR_KB_HINT_EDIT_ENTRY), true);
    }
  }

  const int keyHeight = metrics.keyboardKeyHeight;
  const int bottomKeyHeight = metrics.keyboardBottomKeyHeight;
  const int keySpacing = metrics.keyboardKeySpacing;
  const int contentCols = getContentColCount();
  const int keyboardWidth = pageWidth * metrics.keyboardWidthPercent / 100;
  const int keyWidth = (keyboardWidth - (contentCols - 1) * keySpacing) / contentCols;
  const int leftMargin = (pageWidth - (contentCols * keyWidth + (contentCols - 1) * keySpacing)) / 2;

  const int bottomRowGap = metrics.keyboardBottomKeySpacing > 0 ? 4 : 0;
  const int keyboardStartY = metrics.keyboardBottomAligned
                                 ? pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing -
                                       (keyHeight + keySpacing) * getContentRowCount() - bottomKeyHeight -
                                       bottomRowGap + metrics.keyboardVerticalOffset
                                 : inputStartY + inputHeight + lineHeight + metrics.verticalSpacing;

  const int tipsLh = renderer.getLineHeight(SMALL_FONT_ID);
  const int underlineBottom = inputStartY + inputHeight + lineHeight + metrics.verticalSpacing + 4;
  auto drawTip = [&](const char* tip, int y) { renderer.drawCenteredText(SMALL_FONT_ID, y, tip, true); };

  int tipCount = 0;
  if (cursorMode) {
    tipCount = 1;
  } else if (urlMode) {
    tipCount = 1 + (!text.empty() ? 1 : 0);
  } else if (symMode) {
    tipCount = !text.empty() ? 1 : 0;
  } else {
    tipCount = 1 + (inputType == InputType::Url ? 1 : 0) + (!text.empty() ? 1 : 0);
  }

  const bool showLayoutHint =
      !cursorMode && !urlMode && !symMode && inputType != InputType::Url && KeyboardLayoutSet::multiple(enabledLayouts);
  if (showLayoutHint) ++tipCount;
  if (tipCount > 0) {
    int y = (underlineBottom + keyboardStartY) / 2 - (tipCount + 1) * tipsLh / 2;
    drawTip(tr(STR_KB_TIPS), y);
    y += tipsLh;
    if (showLayoutHint) {
      drawTip(tr(STR_KB_HINT_SWITCH_LAYOUT), y);
      y += tipsLh;
    }
    if (cursorMode) {
      drawTip(tr(STR_KB_HINT_RETURN_KEYBOARD), y);
    } else if (urlMode) {
      drawTip(tr(STR_KB_HINT_EXIT_URL_MODE), y);
      y += tipsLh;
      if (!text.empty()) {
        drawTip(tr(STR_KB_HINT_CLEAR_TEXT), y);
      }
    } else if (symMode) {
      if (!text.empty()) {
        drawTip(tr(STR_KB_HINT_CLEAR_TEXT), y);
      }
    } else {
      const char* altCharTip;
      if (inputType == InputType::Url) {
        altCharTip = tr(STR_KB_HINT_SECONDARY_CHAR);
      } else if (shiftState > 0) {
        altCharTip = tr(STR_KB_HINT_LOWER_SECONDARY);
      } else {
        altCharTip = tr(STR_KB_HINT_UPPER_SECONDARY);
      }
      drawTip(altCharTip, y);
      y += tipsLh;
      if (inputType == InputType::Url) {
        drawTip(tr(STR_KB_HINT_URL_SNIPPETS), y);
        y += tipsLh;
      }
      if (!text.empty()) {
        drawTip(tr(STR_KB_HINT_CLEAR_TEXT), y);
      }
    }
  }

  const int bkSpacing = metrics.keyboardBottomKeySpacing;
  const int abcKeyWidth = (keyboardWidth - (COLS - 1) * keySpacing) / COLS;
  const int contentTotalWidth = COLS * abcKeyWidth + (COLS - 1) * keySpacing;
  const int bottomKeyWidth = (contentTotalWidth - (BOTTOM_KEY_COUNT - 1) * bkSpacing) / BOTTOM_KEY_COUNT;
  const int bottomLeftMargin =
      (pageWidth - (BOTTOM_KEY_COUNT * bottomKeyWidth + (BOTTOM_KEY_COUNT - 1) * bkSpacing)) / 2;

  int urlLeftMargin = leftMargin;
  if (urlMode) {
    const int urlTotalWidth = 3 * keyWidth + 2 * keySpacing;
    const int urlCenterX =
        bottomLeftMargin + static_cast<int>(SpecialKeyType::Space) * (bottomKeyWidth + bkSpacing) + bottomKeyWidth / 2;
    urlLeftMargin = urlCenterX - urlTotalWidth / 2;
  }

  const int contentRows = getContentRowCount();

  for (int row = 0; row < contentRows; row++) {
    const int rowY = keyboardStartY + row * (keyHeight + keySpacing);
    const int rowLeftMargin = urlMode ? urlLeftMargin : leftMargin;

    for (int col = 0; col < contentCols; col++) {
      const int keyX = rowLeftMargin + col * (keyWidth + keySpacing);
      const bool isSelected = row == selectedRow && col == selectedCol;
      const bool activeKeySelected = isSelected && !cursorMode;

      if (urlMode) {
        const int snippetIdx = col + row * 3;
        if (snippetIdx < URL_SNIPPET_COUNT) {
          GUI.drawKeyboardKey(renderer, Rect{keyX, rowY, keyWidth, keyHeight}, urlSnippets[snippetIdx],
                              activeKeySelected, nullptr);
        }
      } else {
        const KeyDef key = getKey(row, col);

        uint32_t primaryChar = key.primary;
        uint32_t secondaryChar = key.secondary;

        if (!symMode && shiftState > 0 && key.secondary != '\0') {
          primaryChar = key.secondary;
          secondaryChar = key.primary;
        }

        char primaryBuf[5], secondaryBuf[5];
        KeyboardText::encode(primaryChar, primaryBuf);
        KeyboardText::encode(secondaryChar, secondaryBuf);
        const bool showSecondary = !symMode && row == 0 && secondaryChar != '\0';
        GUI.drawKeyboardKey(renderer, Rect{keyX, rowY, keyWidth, keyHeight}, primaryBuf, activeKeySelected,
                            showSecondary ? secondaryBuf : nullptr);
      }
    }
  }

  const int bottomRowY = keyboardStartY + contentRows * (keyHeight + keySpacing) + bottomRowGap;
  const bool bottomSelected = isBottomRow(selectedRow);

  struct BottomKeyInfo {
    KeyboardKeyType themeType;
    const char* label;
  };
  const BottomKeyInfo bottomKeys[BOTTOM_KEY_COUNT] = {
      {(symMode || urlMode || inputType == InputType::Url) ? KeyboardKeyType::Disabled : KeyboardKeyType::Shift,
       (symMode || urlMode || inputType == InputType::Url) ? shiftString[0] : shiftString[shiftState]},
      {KeyboardKeyType::Mode, urlMode || symMode ? "abc"
                              : inputType != InputType::Url && KeyboardLayoutSet::multiple(enabledLayouts)
                                  ? KeyboardLayoutSet::name(layoutId)
                                  : "#@!"},
      {inputType == InputType::Url ? KeyboardKeyType::Mode : KeyboardKeyType::Space,
       inputType == InputType::Url ? "URL" : nullptr},
      {KeyboardKeyType::Del, nullptr},
      {KeyboardKeyType::Ok, tr(STR_OK_BUTTON)},
  };

  for (int i = 0; i < BOTTOM_KEY_COUNT; i++) {
    const int keyX = bottomLeftMargin + i * (bottomKeyWidth + bkSpacing);
    const bool isSelected = bottomSelected && i == selectedCol;

    const bool activeKeySelected = isSelected && !cursorMode;
    GUI.drawKeyboardKey(renderer, Rect{keyX, bottomRowY, bottomKeyWidth, bottomKeyHeight}, bottomKeys[i].label,
                        activeKeySelected, nullptr, bottomKeys[i].themeType);
  }

  if (cursorMode) {
    int selKeyX, selKeyY, selKeyW, selKeyH;
    if (isBottomRow(selectedRow)) {
      selKeyX = bottomLeftMargin + selectedCol * (bottomKeyWidth + bkSpacing);
      selKeyY = bottomRowY;
      selKeyW = bottomKeyWidth;
      selKeyH = bottomKeyHeight;
    } else {
      const int rowLM = urlMode ? urlLeftMargin : leftMargin;
      selKeyX = rowLM + selectedCol * (keyWidth + keySpacing);
      selKeyY = keyboardStartY + selectedRow * (keyHeight + keySpacing);
      selKeyW = keyWidth;
      selKeyH = keyHeight;
    }
    if (isBottomRow(selectedRow)) {
      GUI.drawKeyboardKey(renderer, Rect{selKeyX, selKeyY, selKeyW, selKeyH}, bottomKeys[selectedCol].label, true,
                          nullptr, bottomKeys[selectedCol].themeType, true);
    } else if (urlMode) {
      const int idx = selectedCol + selectedRow * 3;
      if (idx < URL_SNIPPET_COUNT) {
        GUI.drawKeyboardKey(renderer, Rect{selKeyX, selKeyY, selKeyW, selKeyH}, urlSnippets[idx], true, nullptr,
                            KeyboardKeyType::Normal, true);
      }
    } else {
      const KeyDef selKey = getKey(selectedRow, selectedCol);
      uint32_t selPrimary = selKey.primary;
      uint32_t selSecondary = selKey.secondary;
      if (!symMode && shiftState > 0 && selKey.secondary != '\0') {
        selPrimary = selKey.secondary;
        selSecondary = selKey.primary;
      }
      char selPrimaryBuf[5], selSecondaryBuf[5];
      KeyboardText::encode(selPrimary, selPrimaryBuf);
      KeyboardText::encode(selSecondary, selSecondaryBuf);
      const bool selShowSecondary = !symMode && selectedRow == 0 && selSecondary != '\0';
      GUI.drawKeyboardKey(renderer, Rect{selKeyX, selKeyY, selKeyW, selKeyH}, selPrimaryBuf, true,
                          selShowSecondary ? selSecondaryBuf : nullptr, KeyboardKeyType::Normal, true);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  GUI.drawSideButtonHints(renderer, ">", "<");

  renderer.displayBuffer();
}

void KeyboardEntryActivity::onComplete(std::string text) {
  setResult(KeyboardResult{std::move(text)});
  finish();
}

void KeyboardEntryActivity::onCancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}
