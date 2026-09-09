#include "SettingsList.h"

#include <HalClock.h>
#include <HalTiltSensor.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "KOReaderCredentialStore.h"
#include "util/ShortcutRegistry.h"

const std::vector<SettingInfo>& getSettingsList() {
  static const std::vector<SettingInfo> list = [] {
    std::vector<SettingInfo> settings = {
        // --- Display ---
        SettingInfo::Enum(StrId::STR_SLEEP_SCREEN, &CrossPointSettings::sleepScreen,
                          {StrId::STR_DARK, StrId::STR_LIGHT, StrId::STR_CUSTOM, StrId::STR_COVER, StrId::STR_NONE_OPT,
                           StrId::STR_COVER_CUSTOM, StrId::STR_READING_DASHBOARD, StrId::STR_COVER_STATS,
                           StrId::STR_COVER_STATS_V2, StrId::STR_CUSTOM_STATS, StrId::STR_CUSTOM_STATS_V2},
                          "sleepScreen", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_SLEEP_COVER_MODE, &CrossPointSettings::sleepScreenCoverMode,
                          {StrId::STR_FIT, StrId::STR_CROP}, "sleepScreenCoverMode", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_SLEEP_COVER_FILTER, &CrossPointSettings::sleepScreenCoverFilter,
                          {StrId::STR_NONE_OPT, StrId::STR_FILTER_CONTRAST, StrId::STR_INVERTED},
                          "sleepScreenCoverFilter", StrId::STR_CAT_DISPLAY),
        SettingInfo::Toggle(StrId::STR_CLEAN_SLEEP_REFRESH, &CrossPointSettings::cleanSleepRefresh, "cleanSleepRefresh",
                            StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_HIDE_BATTERY, &CrossPointSettings::hideBatteryPercentage,
                          {StrId::STR_NEVER, StrId::STR_IN_READER, StrId::STR_ALWAYS}, "hideBatteryPercentage",
                          StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(
            StrId::STR_REFRESH_FREQ, &CrossPointSettings::refreshFrequency,
            {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10, StrId::STR_PAGES_15, StrId::STR_PAGES_30},
            "refreshFrequency", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_UI_THEME, &CrossPointSettings::uiTheme,
                          {StrId::STR_THEME_LYRA, StrId::STR_THEME_LYRA_CUSTOM, StrId::STR_THEME_LYRA_CAROUSEL},
                          "uiTheme", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_HOME_BOOK_SOURCE, &CrossPointSettings::homeBookSource,
                          {StrId::STR_RECENTS, StrId::STR_FAVORITES}, "homeBookSource", StrId::STR_CAT_DISPLAY),
        SettingInfo::Toggle(StrId::STR_ANTI_GHOSTING_EXPERIMENTAL, &CrossPointSettings::antiGhostingExperimental,
                            "antiGhostingExperimental", StrId::STR_CAT_DISPLAY),
        SettingInfo::Toggle(StrId::STR_DARK_MODE, &CrossPointSettings::darkMode, "darkMode", StrId::STR_CAT_DISPLAY),
        SettingInfo::Toggle(StrId::STR_SUNLIGHT_FADING_FIX, &CrossPointSettings::fadingFix, "fadingFix",
                            StrId::STR_CAT_DISPLAY),
        // --- Reader ---
        SettingInfo::Enum(StrId::STR_FONT_FAMILY, &CrossPointSettings::fontFamily,
                          {StrId::STR_BOOKERLY, StrId::STR_NOTO_SANS}, "fontFamily", StrId::STR_CAT_READER),
        SettingInfo::Enum(
            StrId::STR_FONT_SIZE, &CrossPointSettings::fontSize,
            {StrId::STR_X_SMALL, StrId::STR_SMALL, StrId::STR_MEDIUM, StrId::STR_LARGE, StrId::STR_X_LARGE}, "fontSize",
            StrId::STR_CAT_READER),
        SettingInfo::Enum(StrId::STR_LINE_SPACING, &CrossPointSettings::lineSpacing,
                          {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE, StrId::STR_EXTRA_WIDE}, "lineSpacing",
                          StrId::STR_CAT_READER),
        SettingInfo::Enum(StrId::STR_WORD_SPACING, &CrossPointSettings::wordSpacing,
                          {StrId::STR_NORMAL, StrId::STR_NUM_1, StrId::STR_NUM_2, StrId::STR_NUM_3, StrId::STR_NUM_4},
                          "wordSpacing", StrId::STR_CAT_READER),
        SettingInfo::Value(StrId::STR_SCREEN_MARGIN, &CrossPointSettings::screenMargin, {5, 40, 5}, "screenMargin",
                           StrId::STR_CAT_READER),
        SettingInfo::Enum(StrId::STR_PARA_ALIGNMENT, &CrossPointSettings::paragraphAlignment,
                          {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                           StrId::STR_BOOK_S_STYLE},
                          "paragraphAlignment", StrId::STR_CAT_READER),
        SettingInfo::Toggle(StrId::STR_EMBEDDED_STYLE, &CrossPointSettings::embeddedStyle, "embeddedStyle",
                            StrId::STR_CAT_READER),
        SettingInfo::Toggle(StrId::STR_HYPHENATION, &CrossPointSettings::hyphenationEnabled, "hyphenationEnabled",
                            StrId::STR_CAT_READER),
        SettingInfo::Enum(StrId::STR_BIONIC_READING, &CrossPointSettings::bionicReading,
                          {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_SUBTLE}, "bionicReading",
                          StrId::STR_CAT_READER),
        SettingInfo::Enum(StrId::STR_ORIENTATION, &CrossPointSettings::orientation,
                          {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED, StrId::STR_LANDSCAPE_CCW},
                          "orientation", StrId::STR_CAT_READER),
        SettingInfo::Toggle(StrId::STR_EXTRA_SPACING, &CrossPointSettings::extraParagraphSpacing,
                            "extraParagraphSpacing", StrId::STR_CAT_READER),
        SettingInfo::Toggle(StrId::STR_FORCE_PARAGRAPH_INDENTS, &CrossPointSettings::forceParagraphIndents,
                            "forceParagraphIndents", StrId::STR_CAT_READER),
        SettingInfo::Toggle(StrId::STR_TEXT_AA, &CrossPointSettings::textAntiAliasing, "textAntiAliasing",
                            StrId::STR_CAT_READER),
        SettingInfo::Enum(StrId::STR_TEXT_DARKNESS, &CrossPointSettings::textDarkness,
                          {StrId::STR_NORMAL, StrId::STR_LEGACY_BW, StrId::STR_DARK, StrId::STR_EXTRA_DARK},
                          "textDarkness", StrId::STR_CAT_READER),
        SettingInfo::Enum(StrId::STR_READER_REFRESH_MODE, &CrossPointSettings::readerRefreshMode,
                          {StrId::STR_REFRESH_MODE_AUTO, StrId::STR_REFRESH_MODE_FAST, StrId::STR_REFRESH_MODE_HALF,
                           StrId::STR_REFRESH_MODE_FULL},
                          "readerRefreshMode", StrId::STR_CAT_READER),
        SettingInfo::Enum(StrId::STR_IMAGES, &CrossPointSettings::imageRendering,
                          {StrId::STR_IMAGES_DISPLAY, StrId::STR_IMAGES_PLACEHOLDER, StrId::STR_IMAGES_SUPPRESS},
                          "imageRendering", StrId::STR_CAT_READER),
        // --- Controls ---
        SettingInfo::Enum(StrId::STR_SIDE_BTN_LAYOUT, &CrossPointSettings::sideButtonLayout,
                          {StrId::STR_PREV_NEXT, StrId::STR_NEXT_PREV}, "sideButtonLayout", StrId::STR_CAT_CONTROLS),
        SettingInfo::Toggle(StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION, &CrossPointSettings::frontButtonFollowOrientation,
                            "frontButtonFollowOrientation", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(StrId::STR_LONG_PRESS_BEHAVIOR, &CrossPointSettings::longPressButtonBehavior,
                          {StrId::STR_LONG_PRESS_BEHAVIOR_OFF, StrId::STR_LONG_PRESS_BEHAVIOR_SKIP,
                           StrId::STR_LONG_PRESS_BEHAVIOR_ORIENTATION},
                          "longPressButtonBehavior", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(StrId::STR_SHORT_PWR_BTN, &CrossPointSettings::shortPwrBtn,
                          {StrId::STR_IGNORE, StrId::STR_SLEEP, StrId::STR_PAGE_TURN, StrId::STR_FORCE_REFRESH,
                           StrId::STR_TOGGLE_STATUS_BAR, StrId::STR_QUICK_LOCK},
                          "shortPwrBtn", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(StrId::STR_TILT_PAGE_TURN, &CrossPointSettings::tiltPageTurn,
                          {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_INVERTED}, "tiltPageTurn",
                          StrId::STR_CAT_CONTROLS),

        // --- System ---
        SettingInfo::Enum(StrId::STR_TIME_TO_SLEEP, &CrossPointSettings::sleepTimeout,
                          {StrId::STR_MIN_1, StrId::STR_MIN_5, StrId::STR_MIN_10, StrId::STR_MIN_15, StrId::STR_MIN_30},
                          "sleepTimeout", StrId::STR_CAT_SYSTEM),
        SettingInfo::Toggle(StrId::STR_SHOW_HIDDEN_FILES, &CrossPointSettings::showHiddenFiles, "showHiddenFiles",
                            StrId::STR_CAT_SYSTEM),
        SettingInfo::Toggle(StrId::STR_HIDE_FILE_EXTENSION, &CrossPointSettings::hideFileExtension, "hideFileExtension",
                            StrId::STR_CAT_SYSTEM),

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
        // Clock entries (status bar uses the shared Sync Day timezone preset).
        SettingInfo::Enum(StrId::STR_CLOCK, &CrossPointSettings::statusBarClock,
                          {StrId::STR_HIDE, StrId::STR_DIR_RIGHT, StrId::STR_DIR_LEFT}, "statusBarClock",
                          StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Enum(StrId::STR_CLOCK_FORMAT, &CrossPointSettings::clockFormat,
                          {StrId::STR_CLOCK_FORMAT_24H, StrId::STR_CLOCK_FORMAT_12H}, "clockFormat",
                          StrId::STR_CUSTOMISE_STATUS_BAR),
        // Persistence flag for NTP debounce. Resetting from the web UI forces a re-sync
        // on next WiFi connect, which is useful when crossing time zones.
        SettingInfo::Toggle(StrId::STR_CLOCK_SYNCED, &CrossPointSettings::clockHasBeenSynced, "clockHasBeenSynced",
                            StrId::STR_CUSTOMISE_STATUS_BAR),

    };

    if (!halTiltSensor.isAvailable()) {
      settings.erase(
          std::remove_if(settings.begin(), settings.end(),
                         [](const SettingInfo& setting) { return setting.nameId == StrId::STR_TILT_PAGE_TURN; }),
          settings.end());
    }

    return settings;
  }();

  return list;
}
