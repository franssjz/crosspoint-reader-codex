#include "ReaderQuickSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SdCardFontGlobals.h"
#include "activities/settings/FontSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"

namespace {

std::string enumValueText(const uint8_t value, const std::vector<StrId>& labels) {
  if (labels.empty()) {
    return "";
  }
  const size_t safeIndex = std::min<size_t>(value, labels.size() - 1);
  return I18N.get(labels[safeIndex]);
}

std::string fontFamilyText() {
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
      {StrId::STR_FONT_SIZE,
       QuickSettingType::Enum,
       &CrossPointSettings::fontSize,
       {StrId::STR_X_SMALL, StrId::STR_SMALL, StrId::STR_MEDIUM, StrId::STR_LARGE, StrId::STR_X_LARGE}},
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

std::string ReaderQuickSettingsActivity::getSettingName(const int index) { return I18N.get(settings()[index].nameId); }

std::string ReaderQuickSettingsActivity::getSettingValue(const int index) {
  const auto& setting = settings()[index];
  if (setting.type == QuickSettingType::FontFamily) {
    return fontFamilyText();
  }

  if (setting.valuePtr == nullptr) {
    return "";
  }

  const uint8_t value = SETTINGS.*(setting.valuePtr);
  if (setting.type == QuickSettingType::Toggle) {
    return value ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  }

  if (setting.type == QuickSettingType::Value) {
    return std::to_string(value);
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

void ReaderQuickSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void ReaderQuickSettingsActivity::toggleSelectedSetting() {
  const auto& setting = settings()[selectedIndex];

  if (setting.type == QuickSettingType::FontFamily) {
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

  if (setting.valuePtr == &CrossPointSettings::fontSize) {
    ensureSdFontLoaded();
  }

  applyImmediateRendererSetting(setting);
  SETTINGS.saveToFile();
}

void ReaderQuickSettingsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    toggleSelectedSetting();
    requestUpdate(true);
    return;
  }

  const int settingCount = static_cast<int>(settings().size());
  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false);

  buttonNavigator.onNextRelease([this, settingCount] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, settingCount);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, settingCount] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, settingCount);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, settingCount, pageItems] {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, settingCount, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, settingCount, pageItems] {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, settingCount, pageItems);
    requestUpdate();
  });
}

void ReaderQuickSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_CAT_READER), tr(STR_SETTINGS_TITLE));

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(settings().size()), selectedIndex,
      [](const int index) { return getSettingName(index); }, nullptr, [](const int) { return UIIcon::Settings; },
      [](const int index) { return getSettingValue(index); }, true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
