#include "SettingsList.h"

#include <BoardConfig.h>
#include <HalClock.h>
#include <HalTiltSensor.h>
#include <I18n.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>

#include "CrossPointSettings.h"
#include "KOReaderCredentialStore.h"
#include "ReaderFontSizes.h"
#include "util/ShortcutRegistry.h"

SettingInfo buildFontFamilySetting(const SdCardFontRegistry* registry) {
  // Built-in font labels (StrId), in CrossPointSettings::FONT_FAMILY order.
  std::vector<StrId> enumValues = {StrId::STR_BOOKERLY, StrId::STR_NOTO_SANS};
  // Runtime string labels for SD card fonts
  std::vector<std::string> enumStringValues;

  // First CrossPointSettings::BUILTIN_FONT_COUNT entries use StrId, rest use strings
  if (registry) {
    const auto& families = registry->getFamilies();
    enumStringValues.reserve(families.size());
    std::transform(families.begin(), families.end(), std::back_inserter(enumStringValues),
                   [](const SdCardFontFamilyInfo& f) { return f.name; });
  }

  const int sdFontCount = static_cast<int>(enumStringValues.size());

  // The render code checks enumStringValues first, then enumValues, so when SD
  // fonts are present every option (built-in + SD) is provided as a string.
  std::vector<std::string> allStringValues;
  if (sdFontCount > 0) {
    allStringValues.push_back(I18N.get(StrId::STR_BOOKERLY));
    allStringValues.push_back(I18N.get(StrId::STR_NOTO_SANS));
    allStringValues.insert(allStringValues.end(), enumStringValues.begin(), enumStringValues.end());
  }

  SettingInfo s;
  s.nameId = StrId::STR_FONT_FAMILY;
  s.type = SettingType::ENUM;
  s.enumValues = std::move(enumValues);
  s.enumStringValues = std::move(allStringValues);
  s.key = "fontFamily";
  s.category = StrId::STR_CAT_READER;
  s.inTextSettings = true;  // matches the static font-family entry it replaces

  // Capture registry families by copy for the lambdas
  std::vector<std::string> sdFamilyNames;
  if (registry) {
    const auto& families = registry->getFamilies();
    sdFamilyNames.reserve(families.size());
    std::transform(families.begin(), families.end(), std::back_inserter(sdFamilyNames),
                   [](const SdCardFontFamilyInfo& f) { return f.name; });
  }

  s.valueGetter = [sdFamilyNames]() -> uint8_t {
    // If an SD card font is selected, find its index
    if (SETTINGS.sdFontFamilyName[0] != '\0') {
      for (int i = 0; i < static_cast<int>(sdFamilyNames.size()); i++) {
        if (sdFamilyNames[i] == SETTINGS.sdFontFamilyName) {
          return static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i);
        }
      }
      // SD font name not found in registry: fall through to built-in
    }
    return SETTINGS.fontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? SETTINGS.fontFamily : 0;
  };

  s.valueSetter = [sdFamilyNames](uint8_t v) {
    if (v < CrossPointSettings::BUILTIN_FONT_COUNT) {
      SETTINGS.fontFamily = v;
      SETTINGS.sdFontFamilyName[0] = '\0';
    } else {
      const int sdIdx = v - CrossPointSettings::BUILTIN_FONT_COUNT;
      if (sdIdx < static_cast<int>(sdFamilyNames.size())) {
        strncpy(SETTINGS.sdFontFamilyName, sdFamilyNames[sdIdx].c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
        SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
      }
    }
  };

  return s;
}

SettingInfo buildFontSizeSetting(const SdCardFontRegistry* registry) {
  // Captured by copy: getSettingsList() returns by value and the lambdas outlive
  // this call, so they must not reference the registry.
  const std::vector<uint8_t> sizes = readerFontPointSizes(registry, SETTINGS.sdFontFamilyName);

  // "pt" is deliberately not translated (unit symbol, matches upstream).
  std::vector<std::string> labels;
  labels.reserve(sizes.size());
  for (const uint8_t pt : sizes) {
    labels.push_back(std::to_string(pt) + " pt");
  }

  SettingInfo s;
  s.nameId = StrId::STR_FONT_SIZE;
  s.type = SettingType::ENUM;
  s.enumStringValues = std::move(labels);
  s.key = "fontSize";
  s.category = StrId::STR_CAT_READER;
  s.inTextSettings = true;  // matches the static font-size entry it replaces

  s.valueGetter = [sizes]() -> uint8_t {
    const uint8_t pt = snapToNearestPointSize(sizes, SETTINGS.fontPointSize);
    for (int i = 0; i < static_cast<int>(sizes.size()); i++) {
      if (sizes[i] == pt) return static_cast<uint8_t>(i);
    }
    return 0;
  };

  s.valueSetter = [sizes](uint8_t v) {
    if (v < sizes.size()) SETTINGS.fontPointSize = sizes[v];
  };

  return s;
}

std::vector<StrId> buildLongPressMenuValues() {
  static constexpr StrId VALUES[] = {StrId::STR_KOSYNC, StrId::STR_DISABLED, StrId::STR_BOOKMARK_OPTION,
                                     StrId::STR_DICTIONARY, StrId::STR_READER_MENU};
  const size_t count = BoardConfig::hasHomeKey() ? std::size(VALUES) : std::size(VALUES) - 1;
  return {VALUES, VALUES + count};
}

std::vector<SettingInfo> getSettingsList(const SdCardFontRegistry* registry) {
  static const std::vector<SettingInfo> baseList = [] {
    // Enum settings are persisted as numeric values. Assign these labels by enum
    // value so a reordered menu or enum cannot silently swap their behavior.
    std::vector<StrId> sleepScreenValues(CrossPointSettings::SLEEP_SCREEN_MODE_COUNT);
    sleepScreenValues[CrossPointSettings::DARK] = StrId::STR_DARK;
    sleepScreenValues[CrossPointSettings::LIGHT] = StrId::STR_LIGHT;
    sleepScreenValues[CrossPointSettings::CUSTOM] = StrId::STR_CUSTOM;
    sleepScreenValues[CrossPointSettings::COVER] = StrId::STR_COVER;
    sleepScreenValues[CrossPointSettings::BLANK] = StrId::STR_NONE_OPT;
    sleepScreenValues[CrossPointSettings::COVER_CUSTOM] = StrId::STR_COVER_CUSTOM;
    sleepScreenValues[CrossPointSettings::READING_DASHBOARD] = StrId::STR_READING_DASHBOARD;
    sleepScreenValues[CrossPointSettings::COVER_STATS] = StrId::STR_COVER_STATS;
    sleepScreenValues[CrossPointSettings::COVER_STATS_V2] = StrId::STR_COVER_STATS_V2;
    sleepScreenValues[CrossPointSettings::CUSTOM_STATS] = StrId::STR_CUSTOM_STATS;
    sleepScreenValues[CrossPointSettings::CUSTOM_STATS_V2] = StrId::STR_CUSTOM_STATS_V2;
    sleepScreenValues[CrossPointSettings::QUICK_RESUME] = StrId::STR_QUICK_RESUME;
    sleepScreenValues[CrossPointSettings::TRANSPARENT_CUSTOM] = StrId::STR_TRANSPARENT;

    std::vector<StrId> uiThemeValues(CrossPointSettings::UI_THEME_COUNT);
    uiThemeValues[CrossPointSettings::LYRA] = StrId::STR_THEME_LYRA;
    uiThemeValues[CrossPointSettings::LYRA_CUSTOM] = StrId::STR_THEME_LYRA_CUSTOM;
    uiThemeValues[CrossPointSettings::LYRA_CAROUSEL] = StrId::STR_THEME_LYRA_CAROUSEL;
    uiThemeValues[CrossPointSettings::CLASSIC] = StrId::STR_THEME_CLASSIC;
    uiThemeValues[CrossPointSettings::ROUNDEDRAFF] = StrId::STR_THEME_ROUNDEDRAFF;
    uiThemeValues[CrossPointSettings::LYRA_3_COVERS] = StrId::STR_THEME_LYRA_EXTENDED;

    std::vector<StrId> shortPwrBtnValues(CrossPointSettings::SHORT_PWRBTN_COUNT);
    shortPwrBtnValues[CrossPointSettings::IGNORE] = StrId::STR_IGNORE;
    shortPwrBtnValues[CrossPointSettings::SLEEP] = StrId::STR_SLEEP;
    shortPwrBtnValues[CrossPointSettings::PAGE_TURN] = StrId::STR_PAGE_TURN;
    shortPwrBtnValues[CrossPointSettings::FORCE_REFRESH] = StrId::STR_FORCE_REFRESH;
    shortPwrBtnValues[CrossPointSettings::TOGGLE_STATUS_BAR] = StrId::STR_TOGGLE_STATUS_BAR;
    shortPwrBtnValues[CrossPointSettings::FOOTNOTES] = StrId::STR_FOOTNOTES;
    shortPwrBtnValues[CrossPointSettings::PWR_CONFIRM] = StrId::STR_CONFIRM;
#if !FREEINK_CAP_TOUCH
    // Power-as-Confirm is touch-board chrome (upstream offers it only there).
    shortPwrBtnValues.pop_back();
#endif

    std::vector<StrId> statusBarClockValues(CrossPointSettings::STATUS_BAR_CLOCK_COUNT);
    statusBarClockValues[CrossPointSettings::STATUS_BAR_CLOCK_HIDE] = StrId::STR_HIDE;
    statusBarClockValues[CrossPointSettings::STATUS_BAR_CLOCK_RIGHT] = StrId::STR_DIR_RIGHT;
    statusBarClockValues[CrossPointSettings::STATUS_BAR_CLOCK_LEFT] = StrId::STR_DIR_LEFT;

    std::vector<SettingInfo> settings = {
        // --- Display ---
        SettingInfo::Enum(StrId::STR_SLEEP_SCREEN, &CrossPointSettings::sleepScreen, std::move(sleepScreenValues),
                          "sleepScreen", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_SLEEP_COVER_MODE, &CrossPointSettings::sleepScreenCoverMode,
                          {StrId::STR_FIT, StrId::STR_CROP}, "sleepScreenCoverMode", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_SLEEP_COVER_FILTER, &CrossPointSettings::sleepScreenCoverFilter,
                          {StrId::STR_NONE_OPT, StrId::STR_FILTER_CONTRAST, StrId::STR_INVERTED},
                          "sleepScreenCoverFilter", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_QUICK_RESUME_TIMEOUT, &CrossPointSettings::quickResumeSleepScreen,
                          {StrId::STR_STATE_OFF, StrId::STR_STATE_ON}, "quickResumeSleepScreen",
                          StrId::STR_CAT_DISPLAY),
        SettingInfo::Toggle(StrId::STR_CLEAN_SLEEP_REFRESH, &CrossPointSettings::cleanSleepRefresh, "cleanSleepRefresh",
                            StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_HIDE_BATTERY, &CrossPointSettings::hideBatteryPercentage,
                          {StrId::STR_NEVER, StrId::STR_IN_READER, StrId::STR_ALWAYS}, "hideBatteryPercentage",
                          StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_REFRESH_FREQ, &CrossPointSettings::refreshFrequency,
                          {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10, StrId::STR_PAGES_15,
                           StrId::STR_PAGES_30, StrId::STR_NEVER},
                          "refreshFrequency", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_UI_THEME, &CrossPointSettings::uiTheme, std::move(uiThemeValues), "uiTheme",
                          StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_HOME_BOOK_SOURCE, &CrossPointSettings::homeBookSource,
                          {StrId::STR_RECENTS, StrId::STR_FAVORITES}, "homeBookSource", StrId::STR_CAT_DISPLAY),
        SettingInfo::Toggle(StrId::STR_ANTI_GHOSTING_EXPERIMENTAL, &CrossPointSettings::antiGhostingExperimental,
                            "antiGhostingExperimental", StrId::STR_CAT_DISPLAY),
        // Night mode / dark mode: one persisted flag ("darkMode"); upstream reads it as screenInverted.
        SettingInfo::Toggle(StrId::STR_DARK_MODE, &CrossPointSettings::darkMode, "darkMode", StrId::STR_CAT_DISPLAY),
        SettingInfo::Toggle(StrId::STR_SUNLIGHT_FADING_FIX, &CrossPointSettings::fadingFix, "fadingFix",
                            StrId::STR_CAT_DISPLAY),
#if FREEINK_CAP_FRONTLIGHT
        SettingInfo::Toggle(StrId::STR_RESTORE_LIGHT_ON_WAKE, &CrossPointSettings::frontlightRestoreOnWake,
                            "frontlightRestoreOnWake", StrId::STR_CAT_DISPLAY),
#endif

        // --- Reader ---
        // Built-in font-family entry. Replaced per-call with a registry-aware
        // version when SD fonts are installed.
        SettingInfo::Enum(StrId::STR_FONT_FAMILY, &CrossPointSettings::fontFamily,
                          {StrId::STR_BOOKERLY, StrId::STR_NOTO_SANS}, "fontFamily", StrId::STR_CAT_READER)
            .withTextSettings(),
        // Placeholder: the selectable sizes depend on the active font family, so
        // this entry is always replaced by buildFontSizeSetting() below. It only
        // fixes the setting's position in the Reader category.
        SettingInfo::Enum(StrId::STR_FONT_SIZE, nullptr, {}, "fontSize", StrId::STR_CAT_READER).withTextSettings(),
        SettingInfo::Enum(StrId::STR_LINE_SPACING, &CrossPointSettings::lineSpacing,
                          {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE, StrId::STR_EXTRA_WIDE}, "lineSpacing",
                          StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Value(StrId::STR_SCREEN_MARGIN, &CrossPointSettings::screenMargin,
                           {CrossPointSettings::SCREEN_MARGIN_MIN, CrossPointSettings::SCREEN_MARGIN_MAX,
                            CrossPointSettings::SCREEN_MARGIN_STEP},
                           "screenMargin", StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Enum(StrId::STR_PARA_ALIGNMENT, &CrossPointSettings::paragraphAlignment,
                          {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                           StrId::STR_BOOK_S_STYLE},
                          "paragraphAlignment", StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Toggle(StrId::STR_EMBEDDED_STYLE, &CrossPointSettings::embeddedStyle, "embeddedStyle",
                            StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Toggle(StrId::STR_HYPHENATION, &CrossPointSettings::hyphenationEnabled, "hyphenationEnabled",
                            StrId::STR_CAT_READER)
            .withTextSettings(),
        // Fork tri-state; upstream's focusReadingEnabled toggle aliases this field.
        SettingInfo::Enum(StrId::STR_BIONIC_READING, &CrossPointSettings::bionicReading,
                          {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_SUBTLE}, "bionicReading",
                          StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Enum(StrId::STR_ORIENTATION, &CrossPointSettings::orientation,
                          {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED, StrId::STR_LANDSCAPE_CCW},
                          "orientation", StrId::STR_CAT_READER),
        SettingInfo::Toggle(StrId::STR_EXTRA_SPACING, &CrossPointSettings::extraParagraphSpacing,
                            "extraParagraphSpacing", StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Toggle(StrId::STR_FORCE_PARAGRAPH_INDENTS, &CrossPointSettings::forceParagraphIndents,
                            "forceParagraphIndents", StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Toggle(StrId::STR_TEXT_AA, &CrossPointSettings::textAntiAliasing, "textAntiAliasing",
                            StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Enum(StrId::STR_TEXT_DARKNESS, &CrossPointSettings::textDarkness,
                          {StrId::STR_NORMAL, StrId::STR_LEGACY_BW, StrId::STR_DARK, StrId::STR_EXTRA_DARK},
                          "textDarkness", StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Enum(StrId::STR_READER_REFRESH_MODE, &CrossPointSettings::readerRefreshMode,
                          {StrId::STR_REFRESH_MODE_AUTO, StrId::STR_REFRESH_MODE_FAST, StrId::STR_REFRESH_MODE_HALF,
                           StrId::STR_REFRESH_MODE_FULL},
                          "readerRefreshMode", StrId::STR_CAT_READER),
        SettingInfo::Enum(StrId::STR_IMAGES, &CrossPointSettings::imageRendering,
                          {StrId::STR_IMAGES_DISPLAY, StrId::STR_IMAGES_PLACEHOLDER, StrId::STR_IMAGES_SUPPRESS},
                          "imageRendering", StrId::STR_CAT_READER),
        SettingInfo::Enum(StrId::STR_READER_MENU_STYLE, &CrossPointSettings::readerMenuStyle,
                          {StrId::STR_MENU_STYLE_LIST, StrId::STR_MENU_STYLE_TOOLBAR}, "readerMenuStyle",
                          StrId::STR_CAT_READER),

        // --- Controls ---
        SettingInfo::Enum(StrId::STR_SIDE_BTN_LAYOUT, &CrossPointSettings::sideButtonLayout,
                          {StrId::STR_PREV_NEXT, StrId::STR_NEXT_PREV, StrId::STR_DISABLED}, "sideButtonLayout",
                          StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(
            StrId::STR_TOUCH_READER_CONTROLS, &CrossPointSettings::touchReaderControls,
            {StrId::STR_STATE_OFF, StrId::STR_STATE_TAP, StrId::STR_STATE_SWIPE, StrId::STR_STATE_INVERTED_TAP},
            "touchReaderControls", StrId::STR_CAT_CONTROLS),
        // Persisted under the legacy "tapForReaderMenu" key: old saves map
        // 0 = Off, 1 = Tap.
        SettingInfo::Enum(StrId::STR_SHOW_READER_MENU, &CrossPointSettings::showReaderMenu,
                          {StrId::STR_STATE_OFF, StrId::STR_STATE_TAP, StrId::STR_STATE_SWIPE_UP}, "tapForReaderMenu",
                          StrId::STR_CAT_CONTROLS),
        SettingInfo::Toggle(StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION, &CrossPointSettings::frontButtonFollowOrientation,
                            "frontButtonFollowOrientation", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(StrId::STR_LONG_PRESS_BEHAVIOR, &CrossPointSettings::longPressButtonBehavior,
                          {StrId::STR_LONG_PRESS_BEHAVIOR_OFF, StrId::STR_LONG_PRESS_BEHAVIOR_SKIP,
                           StrId::STR_LONG_PRESS_BEHAVIOR_ORIENTATION},
                          "longPressButtonBehavior", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(StrId::STR_LONG_PRESS_MENU, &CrossPointSettings::longPressMenuFunction,
                          buildLongPressMenuValues(), "longPressMenuFunction", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(StrId::STR_SHORT_PWR_BTN, &CrossPointSettings::shortPwrBtn, std::move(shortPwrBtnValues),
                          "shortPwrBtn", StrId::STR_CAT_CONTROLS),
        SettingInfo::Toggle(StrId::STR_PWR_BTN_FOOTNOTE_BACK, &CrossPointSettings::pwrBtnFootnoteBack,
                            "pwrBtnFootnoteBack", StrId::STR_CAT_CONTROLS),
        SettingInfo::Toggle(StrId::STR_BACK_SHORT_TO_FILE_BROWSER, &CrossPointSettings::backShortToFileBrowser,
                            "backShortToFileBrowser", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(StrId::STR_TILT_PAGE_TURN, &CrossPointSettings::tiltPageTurn,
                          {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_INVERTED}, "tiltPageTurn",
                          StrId::STR_CAT_CONTROLS),

        // --- System ---
        SettingInfo::Value(
            StrId::STR_TIME_TO_SLEEP, &CrossPointSettings::sleepTimeoutMinutes,
            {CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES, CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1},
            "sleepTimeoutMinutes", StrId::STR_CAT_SYSTEM),
        SettingInfo::Toggle(StrId::STR_SHOW_HIDDEN_FILES, &CrossPointSettings::showHiddenFiles, "showHiddenFiles",
                            StrId::STR_CAT_SYSTEM),
        SettingInfo::Toggle(StrId::STR_HIDE_FILE_EXTENSION, &CrossPointSettings::hideFileExtension, "hideFileExtension",
                            StrId::STR_CAT_SYSTEM),
        SettingInfo::Toggle(StrId::STR_REMOVE_READ_FROM_RECENTS, &CrossPointSettings::removeReadBooksFromRecents,
                            "removeReadBooksFromRecents", StrId::STR_CAT_SYSTEM),

        // OPDS download folder: persisted + web-exposed, but category-less so it
        // is hidden from the on-device Settings screen (edited via OPDS UI).
        SettingInfo::String(StrId::STR_OPDS_DOWNLOAD_FOLDER, &SETTINGS.opdsDownloadFolder[0],
                            sizeof(SETTINGS.opdsDownloadFolder), "opdsDownloadFolder"),

        // Frontlight quick-panel state: persisted and web-exposed, but hidden
        // from the on-device Settings screen because the swipe panel owns it.
        SettingInfo::Value(StrId::STR_BRIGHTNESS, &CrossPointSettings::frontlightBrightness, {0, 100, 5},
                           "frontlightBrightness"),
#if FREEINK_CAP_WARMLIGHT
        SettingInfo::Value(StrId::STR_WARMTH, &CrossPointSettings::frontlightWarmth, {0, 100, 5}, "frontlightWarmth"),
#endif
        SettingInfo::Toggle(StrId::STR_FRONTLIGHT, &CrossPointSettings::frontlightOn, "frontlightOn"),

        // --- Apps ---
        SettingInfo::Toggle(StrId::STR_DISPLAY_DAY, &CrossPointSettings::displayDay, "displayDay", StrId::STR_APPS),
        SettingInfo::Enum(StrId::STR_CHOOSE_WIFI, &CrossPointSettings::syncDayWifiChoice,
                          {StrId::STR_REFRESH_MODE_AUTO, StrId::STR_MANUAL}, "syncDayWifiChoice", StrId::STR_APPS),
        SettingInfo::Enum(StrId::STR_SYNC_DAY_REMINDER_EVERY, &CrossPointSettings::syncDayReminderStarts,
                          {StrId::STR_STATE_OFF, StrId::STR_NUM_10, StrId::STR_NUM_20, StrId::STR_NUM_30,
                           StrId::STR_NUM_40, StrId::STR_NUM_50, StrId::STR_NUM_60},
                          "syncDayReminderStarts", StrId::STR_APPS),
        SettingInfo::Enum(
            StrId::STR_DATE_FORMAT, &CrossPointSettings::dateFormat,
            {StrId::STR_DATE_FORMAT_DD_MM_YYYY, StrId::STR_DATE_FORMAT_MM_DD_YYYY, StrId::STR_DATE_FORMAT_YYYY_MM_DD},
            "dateFormat", StrId::STR_APPS),
        SettingInfo::Enum(StrId::STR_DAILY_GOAL, &CrossPointSettings::dailyGoalTarget,
                          {StrId::STR_MIN_15, StrId::STR_MIN_30, StrId::STR_MIN_45, StrId::STR_MIN_60},
                          "dailyGoalTarget", StrId::STR_APPS),
        SettingInfo::Enum(
            StrId::STR_READING_STATS_AUTOBACKUP, &CrossPointSettings::readingStatsAutoBackup,
            {StrId::STR_STATE_OFF, StrId::STR_NUM_1, StrId::STR_NUM_7, StrId::STR_NUM_14, StrId::STR_NUM_21},
            "readingStatsAutoBackup", StrId::STR_APPS),
        SettingInfo::Enum(StrId::STR_STUDY_MODE, &CrossPointSettings::flashcardStudyMode,
                          {StrId::STR_DUE, StrId::STR_SCHEDULED, StrId::STR_RANDOM_PRACTICE, StrId::STR_SEQUENTIAL},
                          "flashcardStudyMode", StrId::STR_APPS),
        SettingInfo::Enum(StrId::STR_SESSION_SIZE, &CrossPointSettings::flashcardSessionSize,
                          {StrId::STR_NUM_10, StrId::STR_NUM_20, StrId::STR_NUM_30, StrId::STR_NUM_50, StrId::STR_ALL},
                          "flashcardSessionSize", StrId::STR_APPS),
        SettingInfo::Toggle(StrId::STR_SHOW_AFTER_READING, &CrossPointSettings::showStatsAfterReading,
                            "showStatsAfterReading", StrId::STR_APPS),
        // Upstream's moveFinishedToReadFolder aliases this field.
        SettingInfo::Toggle(StrId::STR_MOVE_COMPLETED_BOOKS, &CrossPointSettings::moveCompletedBooks,
                            "moveCompletedBooks", StrId::STR_APPS),
        SettingInfo::Toggle(StrId::STR_ENABLE_ACHIEVEMENTS, &CrossPointSettings::achievementsEnabled,
                            "achievementsEnabled", StrId::STR_APPS),
        SettingInfo::Toggle(StrId::STR_ACHIEVEMENT_POPUPS, &CrossPointSettings::achievementPopups, "achievementPopups",
                            StrId::STR_APPS),

        // --- Shortcuts (web-only launcher placement) ---
        SettingInfo::Enum(StrId::STR_BROWSE_FILES, &CrossPointSettings::browseFilesShortcut,
                          {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "browseFilesShortcut",
                          StrId::STR_SHORTCUTS_SECTION),
        SettingInfo::Enum(StrId::STR_SYNC_DAY, &CrossPointSettings::syncDayShortcut,
                          {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "syncDayShortcut", StrId::STR_SHORTCUTS_SECTION),
        SettingInfo::Enum(StrId::STR_SETTINGS_TITLE, &CrossPointSettings::settingsShortcut,
                          {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "settingsShortcut",
                          StrId::STR_SHORTCUTS_SECTION),
        SettingInfo::Enum(StrId::STR_READING_STATS, &CrossPointSettings::readingStatsShortcut,
                          {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "readingStatsShortcut",
                          StrId::STR_SHORTCUTS_SECTION),
        SettingInfo::Enum(StrId::STR_READING_HEATMAP, &CrossPointSettings::readingHeatmapShortcut,
                          {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "readingHeatmapShortcut",
                          StrId::STR_SHORTCUTS_SECTION),
        SettingInfo::Enum(StrId::STR_READING_PROFILE, &CrossPointSettings::readingProfileShortcut,
                          {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "readingProfileShortcut",
                          StrId::STR_SHORTCUTS_SECTION),
        SettingInfo::Enum(StrId::STR_ACHIEVEMENTS, &CrossPointSettings::achievementsShortcut,
                          {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "achievementsShortcut",
                          StrId::STR_SHORTCUTS_SECTION),
        SettingInfo::Enum(StrId::STR_IF_FOUND_RETURN_ME, &CrossPointSettings::ifFoundShortcut,
                          {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "ifFoundShortcut", StrId::STR_SHORTCUTS_SECTION),
        SettingInfo::Enum(StrId::STR_MENU_RECENT_BOOKS, &CrossPointSettings::recentBooksShortcut,
                          {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "recentBooksShortcut",
                          StrId::STR_SHORTCUTS_SECTION),
        SettingInfo::Enum(StrId::STR_HIGHLIGHTS, &CrossPointSettings::bookmarksShortcut,
                          {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "bookmarksShortcut",
                          StrId::STR_SHORTCUTS_SECTION),
        SettingInfo::Enum(StrId::STR_FAVORITES, &CrossPointSettings::favoritesShortcut,
                          {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "favoritesShortcut",
                          StrId::STR_SHORTCUTS_SECTION),
        SettingInfo::Enum(StrId::STR_FLASHCARDS, &CrossPointSettings::flashcardsShortcut,
                          {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "flashcardsShortcut",
                          StrId::STR_SHORTCUTS_SECTION),
        SettingInfo::Enum(StrId::STR_DICTIONARY, &CrossPointSettings::dictionaryShortcut,
                          {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "dictionaryShortcut",
                          StrId::STR_SHORTCUTS_SECTION),
        SettingInfo::Enum(StrId::STR_FILE_TRANSFER, &CrossPointSettings::fileTransferShortcut,
                          {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "fileTransferShortcut",
                          StrId::STR_SHORTCUTS_SECTION),
        SettingInfo::Enum(StrId::STR_SCREEN_CLEAN, &CrossPointSettings::screenCleanShortcut,
                          {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "screenCleanShortcut",
                          StrId::STR_SHORTCUTS_SECTION),
        SettingInfo::Enum(StrId::STR_SLEEP, &CrossPointSettings::sleepShortcut,
                          {StrId::STR_HOME_LOCATION, StrId::STR_APPS}, "sleepShortcut", StrId::STR_SHORTCUTS_SECTION),

        // --- KOReader Sync (web-only, uses KOReaderCredentialStore) ---
        SettingInfo::DynamicString(
            StrId::STR_KOREADER_USERNAME, [] { return KOREADER_STORE.getUsername(); },
            [](const std::string& v) {
              KOREADER_STORE.setCredentials(v, KOREADER_STORE.getPassword());
              KOREADER_STORE.saveToFile();
            },
            "koUsername", StrId::STR_KOREADER_SYNC),
        SettingInfo::DynamicString(
            StrId::STR_KOREADER_PASSWORD, [] { return KOREADER_STORE.getPassword(); },
            [](const std::string& v) {
              KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), v);
              KOREADER_STORE.saveToFile();
            },
            "koPassword", StrId::STR_KOREADER_SYNC),
        SettingInfo::DynamicString(
            StrId::STR_SYNC_SERVER_URL, [] { return KOREADER_STORE.getServerUrl(); },
            [](const std::string& v) {
              KOREADER_STORE.setServerUrl(v);
              KOREADER_STORE.saveToFile();
            },
            "koServerUrl", StrId::STR_KOREADER_SYNC),
        SettingInfo::DynamicEnum(
            StrId::STR_DOCUMENT_MATCHING, {StrId::STR_FILENAME, StrId::STR_BINARY},
            [] { return static_cast<uint8_t>(KOREADER_STORE.getMatchMethod()); },
            [](uint8_t v) {
              KOREADER_STORE.setMatchMethod(static_cast<DocumentMatchMethod>(v));
              KOREADER_STORE.saveToFile();
            },
            "koMatchMethod", StrId::STR_KOREADER_SYNC),
        SettingInfo::DynamicEnum(
            StrId::STR_SEND_METADATA, {StrId::STR_STATE_OFF, StrId::STR_STATE_ON},
            [] { return static_cast<uint8_t>(KOREADER_STORE.getSendMetadata()); },
            [](uint8_t v) {
              KOREADER_STORE.setSendMetadata(v != 0);
              KOREADER_STORE.saveToFile();
            },
            "koSendMetadata", StrId::STR_KOREADER_SYNC),
        SettingInfo::DynamicEnum(
            StrId::STR_SYNC_BEHAVIOR, {StrId::STR_ASK_EVERY_TIME, StrId::STR_SMART_SYNC},
            [] { return static_cast<uint8_t>(KOREADER_STORE.getSyncBehavior()); },
            [](uint8_t v) {
              KOREADER_STORE.setSyncBehavior(static_cast<KOReaderSyncBehavior>(v));
              KOREADER_STORE.saveToFile();
            },
            "koSyncBehavior", StrId::STR_KOREADER_SYNC),
        SettingInfo::Toggle(StrId::STR_KO_AUTO_PULL_ON_OPEN, &CrossPointSettings::koSyncAutoPullOnOpen,
                            "koSyncAutoPullOnOpen", StrId::STR_KOREADER_SYNC),
        SettingInfo::Toggle(StrId::STR_KO_AUTO_PUSH_ON_CLOSE, &CrossPointSettings::koSyncAutoPushOnClose,
                            "koSyncAutoPushOnClose", StrId::STR_KOREADER_SYNC),
        SettingInfo::Enum(StrId::STR_OPDS_FILENAME_FORMAT, &CrossPointSettings::opdsFilenameFormat,
                          {StrId::STR_AUTHOR_TITLE, StrId::STR_TITLE_AUTHOR}, "opdsFilenameFormat",
                          StrId::STR_KOREADER_SYNC),

        // --- Status Bar Settings (web-only, uses StatusBarSettingsActivity) ---
        SettingInfo::Toggle(StrId::STR_CHAPTER_PAGE_COUNT, &CrossPointSettings::statusBarChapterPageCount,
                            "statusBarChapterPageCount", StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Toggle(StrId::STR_BOOK_PROGRESS_PERCENTAGE, &CrossPointSettings::statusBarBookProgressPercentage,
                            "statusBarBookProgressPercentage", StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Enum(StrId::STR_PROGRESS_BAR, &CrossPointSettings::statusBarProgressBar,
                          {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE}, "statusBarProgressBar",
                          StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Enum(StrId::STR_PROGRESS_BAR_THICKNESS, &CrossPointSettings::statusBarProgressBarThickness,
                          {StrId::STR_PROGRESS_BAR_THIN, StrId::STR_PROGRESS_BAR_MEDIUM, StrId::STR_PROGRESS_BAR_THICK},
                          "statusBarProgressBarThickness", StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Enum(StrId::STR_TITLE, &CrossPointSettings::statusBarTitle,
                          {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE}, "statusBarTitle",
                          StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Toggle(StrId::STR_BATTERY, &CrossPointSettings::statusBarBattery, "statusBarBattery",
                            StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Enum(StrId::STR_XTC_STATUS_BAR, &CrossPointSettings::xtcStatusBarMode,
                          {StrId::STR_HIDE, StrId::STR_BOTTOM, StrId::STR_TOP}, "xtcStatusBarMode",
                          StrId::STR_CUSTOMISE_STATUS_BAR),
        // Clock entries (the on-device status bar clock follows the shared Sync Day
        // timezone preset; clockUtcOffsetQ is kept for upstream's ClockOffsetActivity).
        // Range 0..104 = quarter-hour steps from UTC-12:00 to UTC+14:00, biased by 48.
        SettingInfo::Enum(StrId::STR_CLOCK, &CrossPointSettings::statusBarClock, std::move(statusBarClockValues),
                          "statusBarClock", StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Value(StrId::STR_CLOCK_UTC_OFFSET, &CrossPointSettings::clockUtcOffsetQ, {0, 104, 1},
                           "clockUtcOffsetQ", StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Enum(StrId::STR_CLOCK_FORMAT, &CrossPointSettings::clockFormat,
                          {StrId::STR_CLOCK_FORMAT_24H, StrId::STR_CLOCK_FORMAT_12H}, "clockFormat",
                          StrId::STR_CUSTOMISE_STATUS_BAR),
        // Persistence flag for NTP debounce. Resetting from the web UI forces a re-sync
        // on next WiFi connect, which is useful when crossing time zones.
        SettingInfo::Toggle(StrId::STR_CLOCK_SYNCED, &CrossPointSettings::clockHasBeenSynced, "clockHasBeenSynced",
                            StrId::STR_CUSTOMISE_STATUS_BAR),
    };

    // Only show tilt page turn setting when the QMI8658 IMU is present (X3)
    if (!halTiltSensor.isAvailable()) {
      settings.erase(
          std::remove_if(settings.begin(), settings.end(),
                         [](const SettingInfo& setting) { return setting.nameId == StrId::STR_TILT_PAGE_TURN; }),
          settings.end());
    }

    return settings;
  }();

  std::vector<SettingInfo> v = baseList;
  if (!BoardConfig::hasTouch()) {
    // The toolbar reader menu is touch-first chrome: button boards keep the
    // classic list menu, so the style choice is hidden along with the touch
    // controls.
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const SettingInfo& s) {
                             return s.nameId == StrId::STR_TOUCH_READER_CONTROLS ||
                                    s.nameId == StrId::STR_READER_MENU_STYLE;
                           }),
            v.end());
  }
  // The reader-menu gesture choice only makes sense where the menu stays
  // reachable without the tap and the bottom edge is free (the capacitive
  // Home key); everywhere else the setting stays at its Tap default.
  if (!BoardConfig::hasHomeKey()) {
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const SettingInfo& s) { return s.nameId == StrId::STR_SHOW_READER_MENU; }),
            v.end());
  }
  if (BoardConfig::hasTouch()) {
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const SettingInfo& s) {
                             return s.nameId == StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION ||
                                    s.nameId == StrId::STR_SUNLIGHT_FADING_FIX ||
                                    s.nameId == StrId::STR_BACK_SHORT_TO_FILE_BROWSER;
                           }),
            v.end());
  }
  if (registry && registry->getFamilyCount() > 0) {
    auto it = std::find_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.nameId == StrId::STR_FONT_FAMILY; });
    if (it != v.end()) {
      *it = buildFontFamilySetting(registry);
    }
  }
  {
    // Unconditional: even with no SD fonts installed the sizes come from the
    // built-in family rather than a fixed slot enum.
    auto it = std::find_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.nameId == StrId::STR_FONT_SIZE; });
    if (it != v.end()) {
      *it = buildFontSizeSetting(registry);
    }
  }
  return v;
}
