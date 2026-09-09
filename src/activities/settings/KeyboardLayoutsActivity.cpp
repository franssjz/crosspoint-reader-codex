#include "KeyboardLayoutsActivity.h"

#include <I18n.h>

#include "CrossPointSettings.h"
#include "activities/util/KeyboardLayoutSet.h"
#include "components/UITheme.h"
#include "fontIds.h"
void KeyboardLayoutsActivity::onEnter() {
  Activity::onEnter();
  RenderLock lock(*this);
  enabled = KeyboardLayoutSet::normalizeMask(SETTINGS.keyboardLayouts);
  selected = 0;
  requestUpdate();
}
void KeyboardLayoutsActivity::onExit() {
  Activity::onExit();
  if (SETTINGS.keyboardLayouts != enabled) {
    SETTINGS.keyboardLayouts = enabled;
    SETTINGS.saveToFile();
  }
}
void KeyboardLayoutsActivity::loop() {
  RenderLock lock(*this);
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const uint8_t candidate = enabled ^ (1u << selected);
    // Reject disabling the final Latin layout without changing another checkbox.
    if (candidate & 0x0F) enabled = KeyboardLayoutSet::normalizeMask(candidate);
    requestUpdate();
  }
  navigator.onNextRelease([this] {
    selected = ButtonNavigator::nextIndex(selected, KeyboardLayoutSet::COUNT);
    requestUpdate();
  });
  navigator.onPreviousRelease([this] {
    selected = ButtonNavigator::previousIndex(selected, KeyboardLayoutSet::COUNT);
    requestUpdate();
  });
}
void KeyboardLayoutsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_KEYBOARD_LAYOUTS));
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int hintHeight = renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing;
  renderer.drawCenteredText(SMALL_FONT_ID, top, tr(STR_KB_LATIN_REQUIRED));
  const auto getter = [](const void*, uint32_t row) { return KeyboardLayoutSet::name(row); };
  renderer.prewarmFallbackText(UI_10_FONT_ID, getter, nullptr, KeyboardLayoutSet::COUNT);
  GUI.drawList(
      renderer,
      Rect{0, top + hintHeight, width,
           renderer.getScreenHeight() - top - hintHeight - metrics.buttonHintsHeight - metrics.verticalSpacing},
      KeyboardLayoutSet::COUNT, selected, [](int row) { return std::string(KeyboardLayoutSet::name(row)); }, nullptr,
      nullptr,
      [this](int row) {
        return std::string(I18N.get(enabled & (1u << row) ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF));
      });
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
