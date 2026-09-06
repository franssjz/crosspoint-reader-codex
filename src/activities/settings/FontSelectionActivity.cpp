#include "FontSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReaderFontSizes.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

FontSelectionActivity::FontSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const SdCardFontRegistry* registry)
    : UiTabListActivity("FontSelect", renderer, mappedInput), registry_(registry) {}

int FontSelectionActivity::listCount() const {
  return static_cast<int>(tab_ == TAB_SIZE ? sizeRows_.size() : familyRows_.size());
}

const char* FontSelectionActivity::tabLabel(const int index) const {
  return index == TAB_SIZE ? tr(STR_SIZE) : tr(STR_FONT);
}

const char* FontSelectionActivity::headerTitle() const {
  return tab_ == TAB_SIZE ? tr(STR_FONT_SIZE) : tr(STR_FONT_FAMILY);
}

int FontSelectionActivity::currentFamilyIndex() const {
  if (SETTINGS.sdFontFamilyName[0] != '\0' && registry_) {
    const auto& families = registry_->getFamilies();
    for (int i = 0; i < static_cast<int>(families.size()); i++) {
      if (families[i].name == SETTINGS.sdFontFamilyName) {
        return CrossPointSettings::BUILTIN_FONT_COUNT + i;
      }
    }
    return 0;
  }
  return SETTINGS.fontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? SETTINGS.fontFamily : 0;
}

int FontSelectionActivity::currentSizeIndex() const {
  if (sizes_.empty()) return 0;
  const uint8_t pt = snapToNearestPointSize(sizes_, SETTINGS.fontPointSize);
  for (int i = 0; i < static_cast<int>(sizes_.size()); i++) {
    if (sizes_[i] == pt) return i;
  }
  return 0;
}

void FontSelectionActivity::onEnter() {
  tab_ = TAB_FAMILY;  // before the base resets activeNav(), which indexes by tab
  UiTabListActivity::onEnter();

  // Build combined font list: built-in + SD card fonts
  fonts_.clear();
  fonts_.reserve(CrossPointSettings::BUILTIN_FONT_COUNT + (registry_ ? registry_->getFamilyCount() : 0));

  fonts_.push_back({I18N.get(StrId::STR_BOOKERLY), true, CrossPointSettings::BOOKERLY});
  fonts_.push_back({I18N.get(StrId::STR_NOTO_SANS), true, CrossPointSettings::NOTOSANS});

  if (registry_) {
    const auto& families = registry_->getFamilies();
    for (int i = 0; i < static_cast<int>(families.size()); i++) {
      fonts_.push_back({families[i].name, false, static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i)});
    }
  }

  // Sizes the active family ships (built-in set or the SD family's .cpfont
  // files). "pt" is deliberately not translated (matches TextSettingsActivity).
  sizes_ = readerFontPointSizes(registry_, SETTINGS.sdFontFamilyName);
  sizeLabels_.clear();
  sizeLabels_.reserve(sizes_.size());
  for (const uint8_t pt : sizes_) {
    sizeLabels_.push_back(std::to_string(pt) + " pt");
  }

  const int familyIndex = currentFamilyIndex();
  const int sizeIndex = currentSizeIndex();

  familyRows_.clear();
  familyRows_.reserve(fonts_.size());
  for (size_t i = 0; i < fonts_.size(); ++i) {
    fui::ListItem item;
    item.label = fonts_[i].name.c_str();
    if (static_cast<int>(i) == familyIndex) item.value = tr(STR_SELECTED);
    item.actionValue = static_cast<int16_t>(i);
    familyRows_.push_back(item);
  }

  sizeRows_.clear();
  sizeRows_.reserve(sizeLabels_.size());
  for (size_t i = 0; i < sizeLabels_.size(); ++i) {
    fui::ListItem item;
    item.label = sizeLabels_[i].c_str();
    if (static_cast<int>(i) == sizeIndex) item.value = tr(STR_SELECTED);
    item.actionValue = static_cast<int16_t>(i);
    sizeRows_.push_back(item);
  }

  // Open each tab on its current value (ring: 0 = tab bar, rows from 1); the
  // first build of a tab pulls the viewport to it.
  tabNavs[TAB_FAMILY].selected = familyRows_.empty() ? 0 : familyIndex + 1;
  tabNavs[TAB_SIZE].selected = sizeRows_.empty() ? 0 : sizeIndex + 1;
}

void FontSelectionActivity::applyFamily(const int index) {
  if (index < 0 || index >= static_cast<int>(fonts_.size())) return;
  const auto& font = fonts_[static_cast<size_t>(index)];
  if (font.settingIndex < CrossPointSettings::BUILTIN_FONT_COUNT) {
    SETTINGS.fontFamily = font.settingIndex;
    SETTINGS.sdFontFamilyName[0] = '\0';
  } else if (registry_) {
    const int sdIdx = font.settingIndex - CrossPointSettings::BUILTIN_FONT_COUNT;
    const auto& families = registry_->getFamilies();
    if (sdIdx < static_cast<int>(families.size())) {
      strncpy(SETTINGS.sdFontFamilyName, families[sdIdx].name.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
      SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
    }
  }
}

void FontSelectionActivity::applySize(const int index) {
  if (index < 0 || index >= static_cast<int>(sizes_.size())) return;
  SETTINGS.fontPointSize = sizes_[static_cast<size_t>(index)];
}

void FontSelectionActivity::activateIndex(const int index) {
  app.clearTapFlash();  // the activated row leaves this screen
  if (tab_ == TAB_SIZE) {
    applySize(index);
  } else {
    applyFamily(index);
  }
  finish();
}

void FontSelectionActivity::onTabAction(const int index) {
  tab_ = index;
  activeNav().selected = 0;  // tab taps land with the tab bar focused
  activeNav().top = 0;
  // The switched-to tab repaints as the selected pill; a flash overlay on top
  // of it just repaints the pill in the focused style.
  app.clearTapFlash();
  requestUpdate();
}

void FontSelectionActivity::stepTab(int /*direction*/) {
  // Two tabs: either direction is the other tab. A row selection carries over
  // to the new tab's current value; the tab bar stays focused otherwise.
  const bool onTabBar = ringPos() == 0;
  tab_ = tab_ == TAB_FAMILY ? TAB_SIZE : TAB_FAMILY;
  activeNav().top = 0;
  if (onTabBar) {
    activeNav().selected = 0;
  } else {
    const int current = tab_ == TAB_SIZE ? currentSizeIndex() : currentFamilyIndex();
    activeNav().selected = listCount() > 0 ? current + 1 : 0;
  }
  requestUpdate();
}

bool FontSelectionActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (ringPos() == 0) {
      stepTab(1);
    } else {
      activateIndex(ringPos() - 1);
    }
    return true;
  }

  return false;
}

void FontSelectionActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});

  buildTabBar(screen);

  const auto& rows = tab_ == TAB_SIZE ? sizeRows_ : familyRows_;
  fui::ListProps props;
  props.items = rows.data();
  props.count = static_cast<uint16_t>(rows.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncTabListViewport(screen, props);
  screen.list(props);
}
