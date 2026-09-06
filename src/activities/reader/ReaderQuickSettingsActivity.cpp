#include "ReaderQuickSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReaderFontSizes.h"
#include "SdCardFontGlobals.h"
#include "activities/settings/FontSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"

namespace fui = freeink::ui;

namespace {

const char* enumValueText(const uint8_t value, const std::vector<StrId>& labels) {
  if (labels.empty()) {
    return "";
  }
  const size_t safeIndex = std::min<size_t>(value, labels.size() - 1);
  return I18N.get(labels[safeIndex]);
}

const char* fontFamilyText() {
  if (SETTINGS.sdFontFamilyName[0] != '\0') {
    return SETTINGS.sdFontFamilyName;
  }
  static const std::vector<StrId> builtInLabels = {StrId::STR_BOOKERLY, StrId::STR_NOTO_SANS};
  return enumValueText(SETTINGS.fontFamily, builtInLabels);
}

}  // namespace

const std::vector<ReaderQuickSettingsActivity::QuickSetting>& ReaderQuickSettingsActivity::settings() {
  static const std::vector<QuickSetting> quickSettings = {
      {StrId::STR_DARK_MODE, QuickSettingType::Toggle, &CrossPointSettings::darkMode},
      {StrId::STR_REFRESH_FREQ,
       QuickSettingType::Enum,
       &CrossPointSettings::refreshFrequency,
       {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10, StrId::STR_PAGES_15, StrId::STR_PAGES_30}},
      {StrId::STR_SUNLIGHT_FADING_FIX, QuickSettingType::Toggle, &CrossPointSettings::fadingFix},
      {StrId::STR_FONT_FAMILY, QuickSettingType::FontFamily},
      {StrId::STR_FONT_SIZE, QuickSettingType::FontSize, &CrossPointSettings::fontPointSize},
      {StrId::STR_LINE_SPACING,
       QuickSettingType::Enum,
       &CrossPointSettings::lineSpacing,
       {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE, StrId::STR_EXTRA_WIDE}},
      {StrId::STR_SCREEN_MARGIN, QuickSettingType::Value, &CrossPointSettings::screenMargin, {}, {5, 40, 5}},
      {StrId::STR_PARA_ALIGNMENT,
       QuickSettingType::Enum,
       &CrossPointSettings::paragraphAlignment,
       {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT, StrId::STR_BOOK_S_STYLE}},
      {StrId::STR_EMBEDDED_STYLE, QuickSettingType::Toggle, &CrossPointSettings::embeddedStyle},
      {StrId::STR_HYPHENATION, QuickSettingType::Toggle, &CrossPointSettings::hyphenationEnabled},
      {StrId::STR_BIONIC_READING,
       QuickSettingType::Enum,
       &CrossPointSettings::bionicReading,
       {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_SUBTLE}},
      {StrId::STR_ORIENTATION,
       QuickSettingType::Enum,
       &CrossPointSettings::orientation,
       {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED, StrId::STR_LANDSCAPE_CCW}},
      {StrId::STR_EXTRA_SPACING, QuickSettingType::Toggle, &CrossPointSettings::extraParagraphSpacing},
      {StrId::STR_FORCE_PARAGRAPH_INDENTS, QuickSettingType::Toggle, &CrossPointSettings::forceParagraphIndents},
      {StrId::STR_TEXT_AA, QuickSettingType::Toggle, &CrossPointSettings::textAntiAliasing},
      {StrId::STR_TEXT_DARKNESS,
       QuickSettingType::Enum,
       &CrossPointSettings::textDarkness,
       {StrId::STR_NORMAL, StrId::STR_LEGACY_BW, StrId::STR_DARK, StrId::STR_EXTRA_DARK}},
      {StrId::STR_READER_REFRESH_MODE,
       QuickSettingType::Enum,
       &CrossPointSettings::readerRefreshMode,
       {StrId::STR_REFRESH_MODE_AUTO, StrId::STR_REFRESH_MODE_FAST, StrId::STR_REFRESH_MODE_HALF,
        StrId::STR_REFRESH_MODE_FULL}},
      {StrId::STR_IMAGES,
       QuickSettingType::Enum,
       &CrossPointSettings::imageRendering,
       {StrId::STR_IMAGES_DISPLAY, StrId::STR_IMAGES_PLACEHOLDER, StrId::STR_IMAGES_SUPPRESS}},
  };
  return quickSettings;
}

size_t ReaderQuickSettingsActivity::settingCount() { return std::min(settings().size(), MAX_QUICK_SETTINGS); }

const char* ReaderQuickSettingsActivity::settingValueText(const size_t index, char* scratch,
                                                          const size_t scratchLen) const {
  const auto& setting = settings()[index];
  if (setting.type == QuickSettingType::FontFamily) {
    return fontFamilyText();
  }

  if (setting.valuePtr == nullptr) {
    return "";
  }

  const uint8_t value = SETTINGS.*(setting.valuePtr);
  if (setting.type == QuickSettingType::FontSize) {
    snprintf(scratch, scratchLen, "%u pt", static_cast<unsigned>(value));
    return scratch;
  }

  if (setting.type == QuickSettingType::Toggle) {
    return value ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  }

  if (setting.type == QuickSettingType::Value) {
    snprintf(scratch, scratchLen, "%u", static_cast<unsigned>(value));
    return scratch;
  }

  return enumValueText(value, setting.enumValues);
}

bool ReaderQuickSettingsActivity::isImmediateRendererSetting(const QuickSetting& setting) {
  return setting.valuePtr == &CrossPointSettings::darkMode || setting.valuePtr == &CrossPointSettings::fadingFix ||
         setting.valuePtr == &CrossPointSettings::textDarkness;
}

bool ReaderQuickSettingsActivity::needsImmediateRendererFullRefresh(const QuickSetting& setting) {
  return setting.valuePtr == &CrossPointSettings::darkMode;
}

void ReaderQuickSettingsActivity::applyImmediateRendererSetting(const QuickSetting& setting) {
  if (!isImmediateRendererSetting(setting)) {
    return;
  }

  renderer.setDarkMode(SETTINGS.darkMode);
  renderer.setFadingFix(SETTINGS.fadingFix);
  renderer.setTextDarkness(SETTINGS.textDarkness);
  if (needsImmediateRendererFullRefresh(setting)) {
    renderer.requestNextFullRefresh();
  }
}

// Populates rowItems' labels/actionValue from settings(). Called once on
// entry since the row set never changes; buildScreen() only refreshes values.
void ReaderQuickSettingsActivity::buildRowItems() {
  const size_t count = settingCount();
  for (size_t i = 0; i < count; ++i) {
    fui::ListItem item;
    item.label = I18N.get(settings()[i].nameId);
    item.actionValue = static_cast<int16_t>(i);
    rowItems[i] = item;
  }
}

void ReaderQuickSettingsActivity::refreshRowValues() {
  const size_t count = settingCount();
  for (size_t i = 0; i < count; ++i) {
    rowItems[i].value = settingValueText(i, valueScratch[i], VALUE_SCRATCH_LEN);
  }
}

void ReaderQuickSettingsActivity::onEnter() {
  UiListActivity::onEnter();
  buildRowItems();
}

void ReaderQuickSettingsActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  nav.selected = index;
  toggleSetting(index);
  requestUpdate(true);
}

void ReaderQuickSettingsActivity::toggleSetting(const int index) {
  const auto& setting = settings()[index];

  if (setting.type == QuickSettingType::FontFamily) {
    // The font picker covers this screen; a lingering flash would gray an
    // unrelated row when the list next appears.
    app.clearTapFlash();
    sdFontSystem.refreshIfDirty();
    startActivityForResult(std::make_unique<FontSelectionActivity>(renderer, mappedInput, &sdFontSystem.registry()),
                           [this](const ActivityResult&) {
                             ensureSdFontLoaded();
                             SETTINGS.saveToFile();
                             requestUpdate(true);
                           });
    return;
  }

  if (setting.valuePtr == nullptr) {
    return;
  }

  if (setting.type == QuickSettingType::FontSize) {
    // Cycle through the point sizes the active family actually ships (built-in
    // or SD card), wrapping back to the smallest after the largest.
    const std::vector<uint8_t> sizes = readerFontPointSizes(&sdFontSystem.registry(), SETTINGS.sdFontFamilyName);
    if (!sizes.empty()) {
      const uint8_t current = snapToNearestPointSize(sizes, SETTINGS.fontPointSize);
      uint8_t next = sizes.front();
      for (size_t i = 0; i < sizes.size(); ++i) {
        if (sizes[i] == current && i + 1 < sizes.size()) {
          next = sizes[i + 1];
          break;
        }
      }
      SETTINGS.fontPointSize = next;
    }
    ensureSdFontLoaded();
    applyImmediateRendererSetting(setting);
    SETTINGS.saveToFile();
    return;
  }

  if (setting.type == QuickSettingType::Toggle) {
    SETTINGS.*(setting.valuePtr) = !(SETTINGS.*(setting.valuePtr));
  } else if (setting.type == QuickSettingType::Enum) {
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = (currentValue + 1) % static_cast<uint8_t>(setting.enumValues.size());
  } else if (setting.type == QuickSettingType::Value) {
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    if (currentValue + setting.valueRange.step > setting.valueRange.max) {
      SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
    } else {
      SETTINGS.*(setting.valuePtr) = currentValue + setting.valueRange.step;
    }
  }

  applyImmediateRendererSetting(setting);
  SETTINGS.saveToFile();
}

void ReaderQuickSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band drawChrome() paints.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // rowItems' labels were set once in onEnter() (buildRowItems()); the values
  // reflect live SETTINGS state, so refresh them on every build.
  refreshRowValues();

  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(settingCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  // Label at the value's font size: both sides of the row read as one unit.
  // maxLines=2 also marks the style caller-owned (see textStyleUnset).
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void ReaderQuickSettingsActivity::drawChrome() {
  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_CAT_READER), tr(STR_SETTINGS_TITLE));
}

void ReaderQuickSettingsActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
