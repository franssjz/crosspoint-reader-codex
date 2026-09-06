#include "ScreenCleanActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "util/HeaderDateUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr uint32_t STAGE_HOLD_MS = 450;
constexpr int CHECKER_TILE_SIZE = 32;

const char* titleForAction(const int index) {
  return index == 0 ? tr(STR_SCREEN_CLEAN_QUICK) : tr(STR_SCREEN_CLEAN_DEEP);
}

const char* subtitleForAction(const int index) {
  return index == 0 ? tr(STR_SCREEN_CLEAN_QUICK_DESC) : tr(STR_SCREEN_CLEAN_DEEP_DESC);
}
}  // namespace

void ScreenCleanActivity::onEnter() {
  UiListActivity::onEnter();
  cleaning = false;
  completed = false;
  pendingStart = -1;
  for (int i = 0; i < ACTION_COUNT; ++i) {
    fui::ListItem item;
    item.label = titleForAction(i);
    item.subtitle = subtitleForAction(i);
    item.icon = listIconFor(UIIcon::Image, 32);  // subtitle rows carry the larger icon
    item.actionValue = static_cast<int16_t>(i);
    rowItems[i] = item;
  }
}

void ScreenCleanActivity::onExit() {
  restoreDarkMode();
  Activity::onExit();
}

void ScreenCleanActivity::restoreDarkMode() {
  if (!darkModeSaved) {
    return;
  }
  renderer.setDarkMode(savedDarkMode);
  darkModeSaved = false;
}

void ScreenCleanActivity::startCleaning(const Mode cleanMode) {
  completed = false;
  mode = cleanMode;
  stageIndex = 0;
  cleaning = true;
  savedDarkMode = renderer.isDarkMode();
  darkModeSaved = true;
  renderer.setDarkMode(false);
  requestUpdateAndWait();
}

void ScreenCleanActivity::finishCleaning(const bool markCompleted) {
  cleaning = false;
  completed = markCompleted;
  restoreDarkMode();
  renderer.requestNextFullRefresh();
  requestUpdateAndWait();
}

int ScreenCleanActivity::stageCount() const { return mode == Mode::Quick ? 5 : 11; }

ScreenCleanActivity::Pattern ScreenCleanActivity::patternForStage(const uint8_t index) const {
  static constexpr Pattern QUICK_SEQUENCE[] = {
      Pattern::White, Pattern::Black, Pattern::White, Pattern::Black, Pattern::White,
  };
  static constexpr Pattern DEEP_SEQUENCE[] = {
      Pattern::White,    Pattern::Black, Pattern::White, Pattern::Checker, Pattern::InverseChecker, Pattern::LightGray,
      Pattern::DarkGray, Pattern::Black, Pattern::White, Pattern::Black,   Pattern::White,
  };

  if (mode == Mode::Quick) {
    return QUICK_SEQUENCE[index % (sizeof(QUICK_SEQUENCE) / sizeof(QUICK_SEQUENCE[0]))];
  }
  return DEEP_SEQUENCE[index % (sizeof(DEEP_SEQUENCE) / sizeof(DEEP_SEQUENCE[0]))];
}

void ScreenCleanActivity::drawPattern(const Pattern pattern) const {
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();

  switch (pattern) {
    case Pattern::White:
      renderer.clearScreen(0xFF);
      break;
    case Pattern::Black:
      renderer.clearScreen(0x00);
      break;
    case Pattern::LightGray:
      renderer.clearScreen(0xFF);
      renderer.fillRectDither(0, 0, width, height, Color::LightGray);
      break;
    case Pattern::DarkGray:
      renderer.clearScreen(0xFF);
      renderer.fillRectDither(0, 0, width, height, Color::DarkGray);
      break;
    case Pattern::Checker:
    case Pattern::InverseChecker: {
      renderer.clearScreen(0xFF);
      const bool inverse = pattern == Pattern::InverseChecker;
      for (int y = 0; y < height; y += CHECKER_TILE_SIZE) {
        for (int x = 0; x < width; x += CHECKER_TILE_SIZE) {
          const bool fillBlack = (((x / CHECKER_TILE_SIZE) + (y / CHECKER_TILE_SIZE)) & 1) != (inverse ? 0 : 1);
          if (!fillBlack) {
            continue;
          }
          const int tileWidth = std::min(CHECKER_TILE_SIZE, width - x);
          const int tileHeight = std::min(CHECKER_TILE_SIZE, height - y);
          renderer.fillRect(x, y, tileWidth, tileHeight);
        }
      }
      break;
    }
  }
}

bool ScreenCleanActivity::handleCustomInput() {
  if (pendingStart >= 0) {
    const int index = pendingStart;
    pendingStart = -1;
    startCleaning(index == 0 ? Mode::Quick : Mode::Deep);
    return true;
  }
  if (!cleaning) {
    return false;
  }

  // Back (button or edge swipe) or a tap anywhere stops the cycle early.
  int tapX = 0;
  int tapY = 0;
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(tapX, tapY)) {
    finishCleaning(false);
    return true;
  }

  if (millis() - lastStageRenderedAt < STAGE_HOLD_MS) {
    return true;
  }

  stageIndex++;
  if (stageIndex >= stageCount()) {
    finishCleaning(true);
    return true;
  }

  requestUpdateAndWait();
  return true;
}

void ScreenCleanActivity::navigateButtons() {
  buttonNavigator.onNextRelease([this] {
    completed = false;
    moveSelectionTo(ButtonNavigator::nextIndex(nav.selected, ACTION_COUNT));
  });
  buttonNavigator.onPreviousRelease([this] {
    completed = false;
    moveSelectionTo(ButtonNavigator::previousIndex(nav.selected, ACTION_COUNT));
  });
}

void ScreenCleanActivity::activateIndex(const int index) {
  // The whole frame becomes a cleaning pattern; drop the tap flash. The
  // cycle itself starts from handleCustomInput() on the next pass.
  app.clearTapFlash();
  pendingStart = index;
}

void ScreenCleanActivity::drawChrome() { HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_SCREEN_CLEAN)); }

void ScreenCleanActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the header band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(ACTION_COUNT);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  fui::TextStyle label = screen.theme().smallText;
  label.bold = true;
  props.labelText = label;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}

void ScreenCleanActivity::render(RenderLock&&) {
  if (cleaning) {
    drawPattern(patternForStage(stageIndex));
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    lastStageRenderedAt = millis();
    return;
  }

  // Same skeleton as UiListActivity::render, plus the completion popup drawn
  // over the list (GUI.drawPopup pushes the frame itself).
  renderer.clearScreen();
  drawChrome();
  renderUi();
  for (int pass = 0; nav.consumeRebuildNeeded() && pass < 8; ++pass) {
    renderer.clearScreen();
    drawChrome();
    renderUi();
  }
  drawFooter();
  if (completed) {
    GUI.drawPopup(renderer, tr(STR_SCREEN_CLEAN_DONE));
    return;
  }
  renderer.displayBuffer();
}
