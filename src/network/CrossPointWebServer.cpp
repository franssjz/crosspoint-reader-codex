#include "CrossPointWebServer.h"

#include <ArduinoJson.h>
#include <FsHelpers.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "AchievementsStore.h"
#include "CrossPointSettings.h"
#include "FontInstaller.h"
#include "KOReaderCredentialStore.h"
#include "OpdsServerStore.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "SdCardFontGlobals.h"
#include "WebDAVHandler.h"
#include "WebPathUtils.h"
#include "WebUploadSize.h"
#include "html/FilesPageHtml.generated.h"
#include "html/FontsPageHtml.generated.h"
#include "html/HomePageHtml.generated.h"
#include "html/IfFoundPageHtml.generated.h"
#include "html/SettingsPageHtml.generated.h"
#include "html/js/jszip_minJs.generated.h"
#include "util/AtomicFileReplace.h"
#include "util/BookCacheUtils.h"
#include "util/IfFoundFile.h"
#include "util/WebUploadRecovery.h"
#include "version.h"

namespace {
// Folders/files to hide from the web interface file browser
// Note: Items starting with "." are automatically hidden
constexpr const char* HIDDEN_ITEMS[] = {"System Volume Information", "XTCache"};
constexpr uint16_t UDP_PORTS[] = {54982, 48123, 39001, 44044, 59678};
constexpr uint16_t LOCAL_UDP_PORT = 8134;

bool isTrackedReadingFile(const String& fileName) {
  const std::string_view fileView{fileName.c_str(), fileName.length()};
  return FsHelpers::hasEpubExtension(fileView) || FsHelpers::hasXtcExtension(fileView) ||
         FsHelpers::hasTxtExtension(fileView) || FsHelpers::hasMarkdownExtension(fileView);
}

bool isCompletedReadingFilePath(const String& filePath) {
  if (!isTrackedReadingFile(filePath)) {
    return false;
  }

  const auto* statsBook = READING_STATS.findBook(std::string{filePath.c_str(), filePath.length()});
  return statsBook != nullptr && statsBook->completed;
}

uint32_t getUnlockedAchievementCount() {
  uint32_t unlocked = 0;
  for (const auto& achievement : ACHIEVEMENTS.buildViews()) {
    if (achievement.state.unlocked) {
      ++unlocked;
    }
  }
  return unlocked;
}

// Static pointer for WebSocket callback (WebSocketsServer requires C-style callback)
CrossPointWebServer* wsInstance = nullptr;

// WebSocket upload state
FsFile wsUploadFile;
WebUploadTransaction wsUploadTransaction;
String wsUploadFileName;
String wsUploadPath;
size_t wsUploadSize = 0;
size_t wsUploadReceived = 0;
unsigned long wsUploadStartTime = 0;
bool wsUploadInProgress = false;
uint8_t wsUploadClientNum = 255;  // 255 = no active upload client
size_t wsLastProgressSent = 0;
String wsLastCompleteName;
size_t wsLastCompleteSize = 0;
unsigned long wsLastCompleteAt = 0;

String normalizeWebPath(const String& inputPath) { return WebPathUtils::normalize(inputPath); }

bool isProtectedPath(const String& path) { return WebPathUtils::isProtected(path); }

void sendRaw(WebServer* server, const char* data) { server->sendContent(data, strlen(data)); }

void sendJsonEscaped(WebServer* server, const char* value) {
  server->sendContent("\"", 1);
  if (!value) {
    server->sendContent("\"", 1);
    return;
  }

  char buffer[96];
  size_t pos = 0;
  auto flush = [&] {
    if (pos > 0) {
      server->sendContent(buffer, pos);
      pos = 0;
    }
  };
  auto append = [&](const char* text) {
    while (*text) {
      if (pos >= sizeof(buffer)) flush();
      buffer[pos++] = *text++;
    }
  };

  for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(value); *cursor; ++cursor) {
    switch (*cursor) {
      case '"':
        append("\\\"");
        break;
      case '\\':
        append("\\\\");
        break;
      case '\b':
        append("\\b");
        break;
      case '\f':
        append("\\f");
        break;
      case '\n':
        append("\\n");
        break;
      case '\r':
        append("\\r");
        break;
      case '\t':
        append("\\t");
        break;
      default:
        if (*cursor < 0x20) {
          char escaped[7];
          snprintf(escaped, sizeof(escaped), "\\u%04x", *cursor);
          append(escaped);
        } else {
          if (pos >= sizeof(buffer)) flush();
          buffer[pos++] = static_cast<char>(*cursor);
        }
        break;
    }
  }
  flush();
  server->sendContent("\"", 1);
}

void sendJsonIntField(WebServer* server, const char* key, int value) {
  char buffer[48];
  const int len = snprintf(buffer, sizeof(buffer), "\"%s\":%d", key, value);
  if (len > 0) {
    server->sendContent(buffer, static_cast<size_t>(len));
  }
}

void sendJsonStringField(WebServer* server, const char* key, const char* value) {
  server->sendContent("\"", 1);
  sendRaw(server, key);
  sendRaw(server, "\":");
  sendJsonEscaped(server, value);
}

int webSettingsCategoryIndex(StrId category) {
  switch (category) {
    case StrId::STR_CAT_DISPLAY:
      return 0;
    case StrId::STR_CAT_READER:
      return 1;
    case StrId::STR_CAT_CONTROLS:
      return 2;
    case StrId::STR_CAT_SYSTEM:
      return 3;
    case StrId::STR_APPS:
      return 4;
    case StrId::STR_SHORTCUTS_SECTION:
      return 5;
    case StrId::STR_KOREADER_SYNC:
      return 6;
    case StrId::STR_CUSTOMISE_STATUS_BAR:
      return 7;
    default:
      return -1;
  }
}

enum class WebSettingType : uint8_t { Toggle, Enum, Value, String };
enum class WebDynamicSetting : uint8_t { None, KoUsername, KoPassword, KoServerUrl, KoMatchMethod };

struct WebSettingDef {
  StrId nameId;
  StrId category;
  WebSettingType type;
  uint8_t CrossPointSettings::* valuePtr;
  const StrId* options;
  uint8_t optionCount;
  uint8_t min;
  uint8_t max;
  uint8_t step;
  WebDynamicSetting dynamic;
  const char* key;
};

constexpr StrId OPT_SLEEP_SCREEN[] = {StrId::STR_DARK,
                                      StrId::STR_LIGHT,
                                      StrId::STR_CUSTOM,
                                      StrId::STR_COVER,
                                      StrId::STR_NONE_OPT,
                                      StrId::STR_COVER_CUSTOM,
                                      StrId::STR_READING_DASHBOARD,
                                      StrId::STR_COVER_STATS,
                                      StrId::STR_COVER_STATS_V2,
                                      StrId::STR_CUSTOM_STATS,
                                      StrId::STR_CUSTOM_STATS_V2};
constexpr StrId OPT_FIT_CROP[] = {StrId::STR_FIT, StrId::STR_CROP};
constexpr StrId OPT_SLEEP_FILTER[] = {StrId::STR_NONE_OPT, StrId::STR_FILTER_CONTRAST, StrId::STR_INVERTED};
constexpr StrId OPT_HIDE_BATTERY[] = {StrId::STR_NEVER, StrId::STR_IN_READER, StrId::STR_ALWAYS};
constexpr StrId OPT_REFRESH_FREQ[] = {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10, StrId::STR_PAGES_15,
                                      StrId::STR_PAGES_30};
constexpr StrId OPT_UI_THEME[] = {StrId::STR_THEME_LYRA, StrId::STR_THEME_LYRA_CUSTOM, StrId::STR_THEME_LYRA_CAROUSEL};
constexpr StrId OPT_FONT_FAMILY[] = {StrId::STR_BOOKERLY, StrId::STR_NOTO_SANS};
constexpr StrId OPT_FONT_SIZE[] = {StrId::STR_X_SMALL, StrId::STR_SMALL, StrId::STR_MEDIUM, StrId::STR_LARGE,
                                   StrId::STR_X_LARGE};
constexpr StrId OPT_LINE_SPACING[] = {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE, StrId::STR_EXTRA_WIDE};
constexpr StrId OPT_ALIGNMENT[] = {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                                   StrId::STR_BOOK_S_STYLE};
constexpr StrId OPT_BIONIC[] = {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_SUBTLE};
constexpr StrId OPT_ORIENTATION[] = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED,
                                     StrId::STR_LANDSCAPE_CCW};
constexpr StrId OPT_TEXT_DARKNESS[] = {StrId::STR_NORMAL, StrId::STR_LEGACY_BW, StrId::STR_DARK, StrId::STR_EXTRA_DARK};
constexpr StrId OPT_READER_REFRESH[] = {StrId::STR_REFRESH_MODE_AUTO, StrId::STR_REFRESH_MODE_FAST,
                                        StrId::STR_REFRESH_MODE_HALF, StrId::STR_REFRESH_MODE_FULL};
constexpr StrId OPT_IMAGES[] = {StrId::STR_IMAGES_DISPLAY, StrId::STR_IMAGES_PLACEHOLDER, StrId::STR_IMAGES_SUPPRESS};
constexpr StrId OPT_SIDE_BUTTONS[] = {StrId::STR_PREV_NEXT, StrId::STR_NEXT_PREV};
constexpr StrId OPT_LONG_PRESS_BEHAVIOR[] = {StrId::STR_LONG_PRESS_BEHAVIOR_OFF, StrId::STR_LONG_PRESS_BEHAVIOR_SKIP,
                                             StrId::STR_LONG_PRESS_BEHAVIOR_ORIENTATION};
constexpr StrId OPT_SHORT_PWR[] = {StrId::STR_IGNORE, StrId::STR_SLEEP, StrId::STR_PAGE_TURN, StrId::STR_FORCE_REFRESH,
                                   StrId::STR_TOGGLE_STATUS_BAR};
constexpr StrId OPT_TILT_PAGE_TURN[] = {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_INVERTED};
constexpr StrId OPT_SLEEP_TIMEOUT[] = {StrId::STR_MIN_1, StrId::STR_MIN_5, StrId::STR_MIN_10, StrId::STR_MIN_15,
                                       StrId::STR_MIN_30};
constexpr StrId OPT_DISPLAY_HEADER[] = {StrId::STR_STATE_OFF, StrId::STR_DISPLAY_DATE_ONLY,
                                        StrId::STR_DISPLAY_TIME_ONLY, StrId::STR_DISPLAY_DAY_AND_TIME};
constexpr StrId OPT_AUTO_MANUAL[] = {StrId::STR_REFRESH_MODE_AUTO, StrId::STR_MANUAL};
constexpr StrId OPT_REMINDER_STARTS[] = {StrId::STR_STATE_OFF, StrId::STR_NUM_10, StrId::STR_NUM_20, StrId::STR_NUM_30,
                                         StrId::STR_NUM_40,    StrId::STR_NUM_50, StrId::STR_NUM_60};
constexpr StrId OPT_DATE_FORMAT[] = {StrId::STR_DATE_FORMAT_DD_MM_YYYY, StrId::STR_DATE_FORMAT_MM_DD_YYYY,
                                     StrId::STR_DATE_FORMAT_YYYY_MM_DD};
constexpr StrId OPT_DAILY_GOAL[] = {StrId::STR_MIN_15, StrId::STR_MIN_30, StrId::STR_MIN_45, StrId::STR_MIN_60};
constexpr StrId OPT_STATS_AUTOBACKUP[] = {StrId::STR_STATE_OFF, StrId::STR_NUM_1, StrId::STR_NUM_7, StrId::STR_NUM_14,
                                          StrId::STR_NUM_21};
constexpr StrId OPT_STUDY_MODE[] = {StrId::STR_DUE, StrId::STR_SCHEDULED, StrId::STR_RANDOM_PRACTICE,
                                    StrId::STR_SEQUENTIAL};
constexpr StrId OPT_SESSION_SIZE[] = {StrId::STR_NUM_10, StrId::STR_NUM_20, StrId::STR_NUM_30, StrId::STR_NUM_50,
                                      StrId::STR_ALL};
constexpr StrId OPT_HOME_BOOK_SOURCE[] = {StrId::STR_RECENTS, StrId::STR_FAVORITES};
constexpr StrId OPT_SHORTCUT_LOCATION[] = {StrId::STR_HOME_LOCATION, StrId::STR_APPS};
constexpr StrId OPT_KO_MATCH[] = {StrId::STR_FILENAME, StrId::STR_BINARY};
constexpr StrId OPT_OPDS_FILENAME_FORMAT[] = {StrId::STR_AUTHOR_TITLE, StrId::STR_TITLE_AUTHOR};
constexpr StrId OPT_BOOK_CHAPTER_HIDE[] = {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE};
constexpr StrId OPT_BAR_THICKNESS[] = {StrId::STR_PROGRESS_BAR_THIN, StrId::STR_PROGRESS_BAR_MEDIUM,
                                       StrId::STR_PROGRESS_BAR_THICK};
constexpr StrId OPT_XTC_STATUS_BAR[] = {StrId::STR_HIDE, StrId::STR_BOTTOM, StrId::STR_TOP};
constexpr StrId OPT_STATUS_BAR_CLOCK[] = {StrId::STR_HIDE, StrId::STR_DIR_RIGHT, StrId::STR_DIR_LEFT};
constexpr StrId OPT_CLOCK_FORMAT[] = {StrId::STR_CLOCK_FORMAT_24H, StrId::STR_CLOCK_FORMAT_12H};

#define WEB_TOGGLE(name, member, key, category)                                                                       \
  {name, category, WebSettingType::Toggle, &CrossPointSettings::member, nullptr, 0, 0, 0, 0, WebDynamicSetting::None, \
   key}
#define WEB_ENUM(name, member, opts, key, category)      \
  {name,                                                 \
   category,                                             \
   WebSettingType::Enum,                                 \
   &CrossPointSettings::member,                          \
   opts,                                                 \
   static_cast<uint8_t>(sizeof(opts) / sizeof(opts[0])), \
   0,                                                    \
   0,                                                    \
   0,                                                    \
   WebDynamicSetting::None,                              \
   key}
#define WEB_VALUE(name, member, lo, hi, inc, key, category) \
  {                                                         \
      name,                                                 \
      category,                                             \
      WebSettingType::Value,                                \
      &CrossPointSettings::member,                          \
      nullptr,                                              \
      0,                                                    \
      lo,                                                   \
      hi,                                                   \
      inc,                                                  \
      WebDynamicSetting::None,                              \
      key}
#define WEB_DYNAMIC(name, kind, type, opts, key, category) \
  {name, category, type, nullptr, opts, static_cast<uint8_t>(sizeof(opts) / sizeof(opts[0])), 0, 0, 0, kind, key}
#define WEB_DYNAMIC_STRING(name, kind, key, category) \
  {name, category, WebSettingType::String, nullptr, nullptr, 0, 0, 0, 0, kind, key}

constexpr WebSettingDef WEB_SETTINGS[] = {
    WEB_ENUM(StrId::STR_SLEEP_SCREEN, sleepScreen, OPT_SLEEP_SCREEN, "sleepScreen", StrId::STR_CAT_DISPLAY),
    WEB_ENUM(StrId::STR_SLEEP_COVER_MODE, sleepScreenCoverMode, OPT_FIT_CROP, "sleepScreenCoverMode",
             StrId::STR_CAT_DISPLAY),
    WEB_ENUM(StrId::STR_SLEEP_COVER_FILTER, sleepScreenCoverFilter, OPT_SLEEP_FILTER, "sleepScreenCoverFilter",
             StrId::STR_CAT_DISPLAY),
    WEB_TOGGLE(StrId::STR_CLEAN_SLEEP_REFRESH, cleanSleepRefresh, "cleanSleepRefresh", StrId::STR_CAT_DISPLAY),
    WEB_ENUM(StrId::STR_HIDE_BATTERY, hideBatteryPercentage, OPT_HIDE_BATTERY, "hideBatteryPercentage",
             StrId::STR_CAT_DISPLAY),
    WEB_ENUM(StrId::STR_REFRESH_FREQ, refreshFrequency, OPT_REFRESH_FREQ, "refreshFrequency", StrId::STR_CAT_DISPLAY),
    WEB_ENUM(StrId::STR_UI_THEME, uiTheme, OPT_UI_THEME, "uiTheme", StrId::STR_CAT_DISPLAY),
    WEB_ENUM(StrId::STR_HOME_BOOK_SOURCE, homeBookSource, OPT_HOME_BOOK_SOURCE, "homeBookSource",
             StrId::STR_CAT_DISPLAY),
    WEB_TOGGLE(StrId::STR_ANTI_GHOSTING_EXPERIMENTAL, antiGhostingExperimental, "antiGhostingExperimental",
               StrId::STR_CAT_DISPLAY),
    WEB_TOGGLE(StrId::STR_DARK_MODE, darkMode, "darkMode", StrId::STR_CAT_DISPLAY),
    WEB_TOGGLE(StrId::STR_SUNLIGHT_FADING_FIX, fadingFix, "fadingFix", StrId::STR_CAT_DISPLAY),

    WEB_ENUM(StrId::STR_FONT_FAMILY, fontFamily, OPT_FONT_FAMILY, "fontFamily", StrId::STR_CAT_READER),
    WEB_ENUM(StrId::STR_FONT_SIZE, fontSize, OPT_FONT_SIZE, "fontSize", StrId::STR_CAT_READER),
    WEB_ENUM(StrId::STR_LINE_SPACING, lineSpacing, OPT_LINE_SPACING, "lineSpacing", StrId::STR_CAT_READER),
    WEB_VALUE(StrId::STR_SCREEN_MARGIN, screenMargin, 5, 40, 5, "screenMargin", StrId::STR_CAT_READER),
    WEB_ENUM(StrId::STR_PARA_ALIGNMENT, paragraphAlignment, OPT_ALIGNMENT, "paragraphAlignment", StrId::STR_CAT_READER),
    WEB_TOGGLE(StrId::STR_EMBEDDED_STYLE, embeddedStyle, "embeddedStyle", StrId::STR_CAT_READER),
    WEB_TOGGLE(StrId::STR_HYPHENATION, hyphenationEnabled, "hyphenationEnabled", StrId::STR_CAT_READER),
    WEB_ENUM(StrId::STR_BIONIC_READING, bionicReading, OPT_BIONIC, "bionicReading", StrId::STR_CAT_READER),
    WEB_ENUM(StrId::STR_ORIENTATION, orientation, OPT_ORIENTATION, "orientation", StrId::STR_CAT_READER),
    WEB_TOGGLE(StrId::STR_EXTRA_SPACING, extraParagraphSpacing, "extraParagraphSpacing", StrId::STR_CAT_READER),
    WEB_TOGGLE(StrId::STR_FORCE_PARAGRAPH_INDENTS, forceParagraphIndents, "forceParagraphIndents",
               StrId::STR_CAT_READER),
    WEB_TOGGLE(StrId::STR_TEXT_AA, textAntiAliasing, "textAntiAliasing", StrId::STR_CAT_READER),
    WEB_ENUM(StrId::STR_TEXT_DARKNESS, textDarkness, OPT_TEXT_DARKNESS, "textDarkness", StrId::STR_CAT_READER),
    WEB_ENUM(StrId::STR_READER_REFRESH_MODE, readerRefreshMode, OPT_READER_REFRESH, "readerRefreshMode",
             StrId::STR_CAT_READER),
    WEB_ENUM(StrId::STR_IMAGES, imageRendering, OPT_IMAGES, "imageRendering", StrId::STR_CAT_READER),

    WEB_ENUM(StrId::STR_SIDE_BTN_LAYOUT, sideButtonLayout, OPT_SIDE_BUTTONS, "sideButtonLayout",
             StrId::STR_CAT_CONTROLS),
    WEB_TOGGLE(StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION, frontButtonFollowOrientation, "frontButtonFollowOrientation",
               StrId::STR_CAT_CONTROLS),
    WEB_ENUM(StrId::STR_LONG_PRESS_BEHAVIOR, longPressButtonBehavior, OPT_LONG_PRESS_BEHAVIOR,
             "longPressButtonBehavior", StrId::STR_CAT_CONTROLS),
    WEB_ENUM(StrId::STR_SHORT_PWR_BTN, shortPwrBtn, OPT_SHORT_PWR, "shortPwrBtn", StrId::STR_CAT_CONTROLS),
    WEB_ENUM(StrId::STR_TILT_PAGE_TURN, tiltPageTurn, OPT_TILT_PAGE_TURN, "tiltPageTurn", StrId::STR_CAT_CONTROLS),

    WEB_ENUM(StrId::STR_TIME_TO_SLEEP, sleepTimeout, OPT_SLEEP_TIMEOUT, "sleepTimeout", StrId::STR_CAT_SYSTEM),
    WEB_TOGGLE(StrId::STR_SHOW_HIDDEN_FILES, showHiddenFiles, "showHiddenFiles", StrId::STR_CAT_SYSTEM),
    WEB_TOGGLE(StrId::STR_HIDE_FILE_EXTENSION, hideFileExtension, "hideFileExtension", StrId::STR_CAT_SYSTEM),

    WEB_TOGGLE(StrId::STR_DISPLAY_DAY, displayDay, "displayDay", StrId::STR_APPS),
    WEB_ENUM(StrId::STR_DISPLAY_DAY_TIME, displayDay, OPT_DISPLAY_HEADER, "displayDay", StrId::STR_APPS),
    WEB_ENUM(StrId::STR_CHOOSE_WIFI, syncDayWifiChoice, OPT_AUTO_MANUAL, "syncDayWifiChoice", StrId::STR_APPS),
    WEB_ENUM(StrId::STR_SYNC_DAY_REMINDER_EVERY, syncDayReminderStarts, OPT_REMINDER_STARTS, "syncDayReminderStarts",
             StrId::STR_APPS),
    WEB_ENUM(StrId::STR_DATE_FORMAT, dateFormat, OPT_DATE_FORMAT, "dateFormat", StrId::STR_APPS),
    WEB_ENUM(StrId::STR_DAILY_GOAL, dailyGoalTarget, OPT_DAILY_GOAL, "dailyGoalTarget", StrId::STR_APPS),
    WEB_ENUM(StrId::STR_READING_STATS_AUTOBACKUP, readingStatsAutoBackup, OPT_STATS_AUTOBACKUP,
             "readingStatsAutoBackup", StrId::STR_APPS),
    WEB_ENUM(StrId::STR_STUDY_MODE, flashcardStudyMode, OPT_STUDY_MODE, "flashcardStudyMode", StrId::STR_APPS),
    WEB_ENUM(StrId::STR_SESSION_SIZE, flashcardSessionSize, OPT_SESSION_SIZE, "flashcardSessionSize", StrId::STR_APPS),
    WEB_TOGGLE(StrId::STR_SHOW_AFTER_READING, showStatsAfterReading, "showStatsAfterReading", StrId::STR_APPS),
    WEB_TOGGLE(StrId::STR_MOVE_COMPLETED_BOOKS, moveCompletedBooks, "moveCompletedBooks", StrId::STR_APPS),
    WEB_TOGGLE(StrId::STR_ENABLE_ACHIEVEMENTS, achievementsEnabled, "achievementsEnabled", StrId::STR_APPS),
    WEB_TOGGLE(StrId::STR_ACHIEVEMENT_POPUPS, achievementPopups, "achievementPopups", StrId::STR_APPS),

    WEB_ENUM(StrId::STR_BROWSE_FILES, browseFilesShortcut, OPT_SHORTCUT_LOCATION, "browseFilesShortcut",
             StrId::STR_SHORTCUTS_SECTION),
    WEB_ENUM(StrId::STR_SYNC_DAY, syncDayShortcut, OPT_SHORTCUT_LOCATION, "syncDayShortcut",
             StrId::STR_SHORTCUTS_SECTION),
    WEB_ENUM(StrId::STR_SETTINGS_TITLE, settingsShortcut, OPT_SHORTCUT_LOCATION, "settingsShortcut",
             StrId::STR_SHORTCUTS_SECTION),
    WEB_ENUM(StrId::STR_READING_STATS, readingStatsShortcut, OPT_SHORTCUT_LOCATION, "readingStatsShortcut",
             StrId::STR_SHORTCUTS_SECTION),
    WEB_ENUM(StrId::STR_READING_HEATMAP, readingHeatmapShortcut, OPT_SHORTCUT_LOCATION, "readingHeatmapShortcut",
             StrId::STR_SHORTCUTS_SECTION),
    WEB_ENUM(StrId::STR_READING_PROFILE, readingProfileShortcut, OPT_SHORTCUT_LOCATION, "readingProfileShortcut",
             StrId::STR_SHORTCUTS_SECTION),
    WEB_ENUM(StrId::STR_ACHIEVEMENTS, achievementsShortcut, OPT_SHORTCUT_LOCATION, "achievementsShortcut",
             StrId::STR_SHORTCUTS_SECTION),
    WEB_ENUM(StrId::STR_IF_FOUND_RETURN_ME, ifFoundShortcut, OPT_SHORTCUT_LOCATION, "ifFoundShortcut",
             StrId::STR_SHORTCUTS_SECTION),
    WEB_ENUM(StrId::STR_MENU_RECENT_BOOKS, recentBooksShortcut, OPT_SHORTCUT_LOCATION, "recentBooksShortcut",
             StrId::STR_SHORTCUTS_SECTION),
    WEB_ENUM(StrId::STR_HIGHLIGHTS, bookmarksShortcut, OPT_SHORTCUT_LOCATION, "bookmarksShortcut",
             StrId::STR_SHORTCUTS_SECTION),
    WEB_ENUM(StrId::STR_FAVORITES, favoritesShortcut, OPT_SHORTCUT_LOCATION, "favoritesShortcut",
             StrId::STR_SHORTCUTS_SECTION),
    WEB_ENUM(StrId::STR_FLASHCARDS, flashcardsShortcut, OPT_SHORTCUT_LOCATION, "flashcardsShortcut",
             StrId::STR_SHORTCUTS_SECTION),
    WEB_ENUM(StrId::STR_FILE_TRANSFER, fileTransferShortcut, OPT_SHORTCUT_LOCATION, "fileTransferShortcut",
             StrId::STR_SHORTCUTS_SECTION),
    WEB_ENUM(StrId::STR_SLEEP, sleepShortcut, OPT_SHORTCUT_LOCATION, "sleepShortcut", StrId::STR_SHORTCUTS_SECTION),

    WEB_DYNAMIC_STRING(StrId::STR_KOREADER_USERNAME, WebDynamicSetting::KoUsername, "koUsername",
                       StrId::STR_KOREADER_SYNC),
    WEB_DYNAMIC_STRING(StrId::STR_KOREADER_PASSWORD, WebDynamicSetting::KoPassword, "koPassword",
                       StrId::STR_KOREADER_SYNC),
    WEB_DYNAMIC_STRING(StrId::STR_SYNC_SERVER_URL, WebDynamicSetting::KoServerUrl, "koServerUrl",
                       StrId::STR_KOREADER_SYNC),
    WEB_DYNAMIC(StrId::STR_DOCUMENT_MATCHING, WebDynamicSetting::KoMatchMethod, WebSettingType::Enum, OPT_KO_MATCH,
                "koMatchMethod", StrId::STR_KOREADER_SYNC),
    WEB_TOGGLE(StrId::STR_KO_AUTO_PULL_ON_OPEN, koSyncAutoPullOnOpen, "koSyncAutoPullOnOpen", StrId::STR_KOREADER_SYNC),
    WEB_TOGGLE(StrId::STR_KO_AUTO_PUSH_ON_CLOSE, koSyncAutoPushOnClose, "koSyncAutoPushOnClose",
               StrId::STR_KOREADER_SYNC),
    WEB_ENUM(StrId::STR_OPDS_FILENAME_FORMAT, opdsFilenameFormat, OPT_OPDS_FILENAME_FORMAT, "opdsFilenameFormat",
             StrId::STR_KOREADER_SYNC),

    WEB_TOGGLE(StrId::STR_CHAPTER_PAGE_COUNT, statusBarChapterPageCount, "statusBarChapterPageCount",
               StrId::STR_CUSTOMISE_STATUS_BAR),
    WEB_TOGGLE(StrId::STR_BOOK_PROGRESS_PERCENTAGE, statusBarBookProgressPercentage, "statusBarBookProgressPercentage",
               StrId::STR_CUSTOMISE_STATUS_BAR),
    WEB_ENUM(StrId::STR_PROGRESS_BAR, statusBarProgressBar, OPT_BOOK_CHAPTER_HIDE, "statusBarProgressBar",
             StrId::STR_CUSTOMISE_STATUS_BAR),
    WEB_ENUM(StrId::STR_PROGRESS_BAR_THICKNESS, statusBarProgressBarThickness, OPT_BAR_THICKNESS,
             "statusBarProgressBarThickness", StrId::STR_CUSTOMISE_STATUS_BAR),
    WEB_ENUM(StrId::STR_TITLE, statusBarTitle, OPT_BOOK_CHAPTER_HIDE, "statusBarTitle",
             StrId::STR_CUSTOMISE_STATUS_BAR),
    WEB_TOGGLE(StrId::STR_BATTERY, statusBarBattery, "statusBarBattery", StrId::STR_CUSTOMISE_STATUS_BAR),
    WEB_ENUM(StrId::STR_XTC_STATUS_BAR, xtcStatusBarMode, OPT_XTC_STATUS_BAR, "xtcStatusBarMode",
             StrId::STR_CUSTOMISE_STATUS_BAR),
    WEB_ENUM(StrId::STR_CLOCK, statusBarClock, OPT_STATUS_BAR_CLOCK, "statusBarClock", StrId::STR_CUSTOMISE_STATUS_BAR),
    WEB_ENUM(StrId::STR_CLOCK_FORMAT, clockFormat, OPT_CLOCK_FORMAT, "clockFormat", StrId::STR_CUSTOMISE_STATUS_BAR),
    WEB_TOGGLE(StrId::STR_CLOCK_SYNCED, clockHasBeenSynced, "clockHasBeenSynced", StrId::STR_CUSTOMISE_STATUS_BAR),
};

#undef WEB_DYNAMIC_STRING
#undef WEB_DYNAMIC
#undef WEB_VALUE
#undef WEB_ENUM
#undef WEB_TOGGLE

const WebSettingDef* findWebSetting(const char* key) {
  for (const auto& setting : WEB_SETTINGS) {
    if (strcmp(setting.key, key) == 0) {
      return &setting;
    }
  }
  return nullptr;
}

bool isWebSettingVisible(const WebSettingDef& setting) {
  if (setting.nameId == StrId::STR_TILT_PAGE_TURN && !halTiltSensor.isAvailable()) {
    return false;
  }
  if ((setting.nameId == StrId::STR_CLOCK || setting.nameId == StrId::STR_CLOCK_FORMAT ||
       setting.nameId == StrId::STR_CLOCK_SYNCED) &&
      !halClock.isAvailable()) {
    return false;
  }
  if (setting.nameId == StrId::STR_SYNC_DAY_REMINDER_EVERY && SETTINGS.isHardwareRtcAutoDayClockActive()) {
    return false;
  }
  if (setting.nameId == StrId::STR_DISPLAY_DAY && SETTINGS.isHardwareRtcAutoDayClockActive()) {
    return false;
  }
  if (setting.nameId == StrId::STR_DISPLAY_DAY_TIME && !SETTINGS.isHardwareRtcAutoDayClockActive()) {
    return false;
  }
  return true;
}
}  // namespace

// File listing page template - now using generated headers:
// - HomePageHtml (from html/HomePage.html)
// - FilesPageHeaderHtml (from html/FilesPageHeader.html)
// - FilesPageFooterHtml (from html/FilesPageFooter.html)
CrossPointWebServer::CrossPointWebServer() {}

CrossPointWebServer::~CrossPointWebServer() { stop(); }

void CrossPointWebServer::begin() {
  if (Storage.ready() && !WebUploadRecovery::recover(Storage)) {
    LOG_ERR("WEB", "Upload recovery needs attention; file uploads will remain blocked");
  }
  if (running) {
    LOG_DBG("WEB", "Web server already running");
    return;
  }

  // Check if we have a valid network connection (either STA connected or AP mode)
  const wifi_mode_t wifiMode = WiFi.getMode();
  const bool isStaConnected = (wifiMode & WIFI_MODE_STA) && (WiFi.status() == WL_CONNECTED);
  const bool isInApMode = (wifiMode & WIFI_MODE_AP) && (WiFi.softAPgetStationNum() >= 0);  // AP is running

  if (!isStaConnected && !isInApMode) {
    LOG_DBG("WEB", "Cannot start webserver - no valid network (mode=%d, status=%d)", wifiMode, WiFi.status());
    return;
  }

  // Store AP mode flag for later use (e.g., in handleStatus)
  apMode = isInApMode;

  LOG_DBG("WEB", "[MEM] Free heap before begin: %d bytes", ESP.getFreeHeap());
  LOG_DBG("WEB", "Network mode: %s", apMode ? "AP" : "STA");

  LOG_DBG("WEB", "Creating web server on port %d...", port);
  server = makeUniqueNoThrow<WebServer>(port);

  if (!server) {
    LOG_ERR("WEB", "Failed to allocate WebServer");
    return;
  }

  // Disable WiFi sleep to improve responsiveness and prevent 'unreachable' errors.
  // This is critical for reliable web server operation on ESP32.
  WiFi.setSleep(false);
  // Default varies by ESP32 core version. The activity's loss-recovery loop
  // relies on driver retries during transient disconnects.
  WiFi.setAutoReconnect(true);

  // Note: WebServer class doesn't have setNoDelay() in the standard ESP32 library.
  // We rely on disabling WiFi sleep for responsiveness.

  LOG_DBG("WEB", "[MEM] Free heap after WebServer allocation: %d bytes", ESP.getFreeHeap());

  // Setup routes
  LOG_DBG("WEB", "Setting up routes...");
  server->on("/", HTTP_GET, [this] { handleRoot(); });
  server->on("/files", HTTP_GET, [this] { handleFileList(); });
  server->on("/js/jszip.min.js", HTTP_GET, [this] { handleJszip(); });

  server->on("/api/status", HTTP_GET, [this] { handleStatus(); });
  server->on("/api/files", HTTP_GET, [this] { handleFileListData(); });
  server->on("/download", HTTP_GET, [this] { handleDownload(); });

  // Upload endpoint with special handling for multipart form data
  server->on("/upload", HTTP_POST, [this] { handleUploadPost(upload); }, [this] { handleUpload(upload); });

  // Create folder endpoint
  server->on("/mkdir", HTTP_POST, [this] { handleCreateFolder(); });

  // Rename file endpoint
  server->on("/rename", HTTP_POST, [this] { handleRename(); });

  // Move file endpoint
  server->on("/move", HTTP_POST, [this] { handleMove(); });

  // Delete file/folder endpoint
  server->on("/delete", HTTP_POST, [this] { handleDelete(); });

  // Settings endpoints
  server->on("/settings", HTTP_GET, [this] { handleSettingsPage(); });
  server->on("/api/settings", HTTP_GET, [this] { handleGetSettings(); });
  server->on("/api/settings", HTTP_POST, [this] { handlePostSettings(); });

  // Font management endpoints
  server->on("/fonts", HTTP_GET, [this] { handleFontsPage(); });
  server->on("/api/fonts", HTTP_GET, [this] { handleFontList(); });
  server->on("/api/fonts/upload", HTTP_POST, [this] { handleFontUpload(); }, [this] { handleFontUploadData(); });
  server->on("/api/fonts/delete", HTTP_POST, [this] { handleFontDelete(); });

  // If Found contact-card endpoints
  server->on("/if-found", HTTP_GET, [this] { handleIfFoundPage(); });
  server->on("/api/if-found", HTTP_GET, [this] { handleGetIfFound(); });
  server->on("/api/if-found", HTTP_POST, [this] { handlePostIfFound(); });

  // OPDS server endpoints
  server->on("/api/opds", HTTP_GET, [this] { handleGetOpdsServers(); });
  server->on("/api/opds", HTTP_POST, [this] { handlePostOpdsServer(); });
  server->on("/api/opds/delete", HTTP_POST, [this] { handleDeleteOpdsServer(); });

  server->onNotFound([this] { handleNotFound(); });
  LOG_DBG("WEB", "[MEM] Free heap after route setup: %d bytes", ESP.getFreeHeap());

  // Collect WebDAV headers and register handler
  const char* davHeaders[] = {"Depth", "Destination", "Overwrite", "If", "Lock-Token", "Timeout"};
  server->collectHeaders(davHeaders, 6);
  if (auto* davHandler = new (std::nothrow) WebDAVHandler()) {
    server->addHandler(davHandler);  // WebServer owns and deletes the handler.
    LOG_DBG("WEB", "WebDAV handler initialized");
  } else {
    LOG_ERR("WEB", "Insufficient heap for WebDAV; continuing without it");
  }

  server->begin();

  // Start WebSocket server for fast binary uploads
  LOG_DBG("WEB", "Starting WebSocket server on port %d...", wsPort);
  wsServer = makeUniqueNoThrow<WebSocketsServer>(wsPort);
  if (wsServer) {
    wsInstance = this;
    wsServer->begin();
    wsServer->onEvent(wsEventCallback);
    LOG_DBG("WEB", "WebSocket server started");
  } else {
    LOG_ERR("WEB", "Insufficient heap for WebSocket; HTTP upload fallback remains available");
  }

  udpActive = udp.begin(LOCAL_UDP_PORT);
  LOG_DBG("WEB", "Discovery UDP %s on port %d", udpActive ? "enabled" : "failed", LOCAL_UDP_PORT);

  running = true;

  LOG_DBG("WEB", "Web server started on port %d", port);
  // Show the correct IP based on network mode
  const String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  LOG_DBG("WEB", "Access at http://%s/", ipAddr.c_str());
  if (wsServer) {
    LOG_DBG("WEB", "WebSocket at ws://%s:%d/", ipAddr.c_str(), wsPort);
  }
  LOG_DBG("WEB", "[MEM] Free heap after server.begin(): %d bytes", ESP.getFreeHeap());
}

void CrossPointWebServer::abortWsUpload(const char* tag) {
  wsUploadTransaction.cancel(wsUploadFile);
  LOG_DBG(tag, "Cancelled incomplete upload; previous book preserved");
  wsUploadInProgress = false;
  wsUploadClientNum = 255;
  wsLastProgressSent = 0;
}

void CrossPointWebServer::stop() {
  if (!running || !server) {
    LOG_DBG("WEB", "stop() called but already stopped (running=%d, server=%p)", running, server.get());
    return;
  }

  LOG_DBG("WEB", "STOP INITIATED - setting running=false first");
  running = false;  // Set this FIRST to prevent handleClient from using server
  upload.transaction.cancel(upload.file);

  LOG_DBG("WEB", "[MEM] Free heap before stop: %d bytes", ESP.getFreeHeap());

  // Close any in-progress WebSocket upload and remove partial file
  if (wsUploadInProgress && wsUploadFile) {
    abortWsUpload("WEB");
  }

  // Stop WebSocket server
  if (wsServer) {
    LOG_DBG("WEB", "Stopping WebSocket server...");
    wsServer->close();
    wsServer.reset();
    wsInstance = nullptr;
    LOG_DBG("WEB", "WebSocket server stopped");
  }

  if (udpActive) {
    udp.stop();
    udpActive = false;
  }

  // Brief delay to allow any in-flight handleClient() calls to complete
  delay(20);

  server->stop();
  LOG_DBG("WEB", "[MEM] Free heap after server->stop(): %d bytes", ESP.getFreeHeap());

  // Brief delay before deletion
  delay(10);

  server.reset();
  LOG_DBG("WEB", "Web server stopped and deleted");
  LOG_DBG("WEB", "[MEM] Free heap after delete server: %d bytes", ESP.getFreeHeap());

  // Note: Static upload variables (uploadFileName, uploadPath, uploadError) are declared
  // later in the file and will be cleared when they go out of scope or on next upload
  LOG_DBG("WEB", "[MEM] Free heap final: %d bytes", ESP.getFreeHeap());
}

void CrossPointWebServer::handleClient() {
  static unsigned long lastDebugPrint = 0;

  // Check running flag FIRST before accessing server
  if (!running) {
    return;
  }

  // Double-check server pointer is valid
  if (!server) {
    LOG_DBG("WEB", "WARNING: handleClient called with null server!");
    return;
  }

  // Print debug every 10 seconds to confirm handleClient is being called
  if (millis() - lastDebugPrint > 10000) {
    LOG_DBG("WEB", "handleClient active, server running on port %d", port);
    lastDebugPrint = millis();
  }

  server->handleClient();

  // Handle WebSocket events
  if (wsServer) {
    wsServer->loop();
  }

  // Respond to discovery broadcasts
  if (udpActive) {
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
      char buffer[16];
      int len = udp.read(buffer, sizeof(buffer) - 1);
      if (len > 0) {
        buffer[len] = '\0';
        if (strcmp(buffer, "hello") == 0) {
          String hostname = WiFi.getHostname();
          if (hostname.isEmpty()) {
            hostname = "crosspoint";
          }
          String message = "crosspoint (on " + hostname + ");" + String(wsPort);
          udp.beginPacket(udp.remoteIP(), udp.remotePort());
          udp.write(reinterpret_cast<const uint8_t*>(message.c_str()), message.length());
          udp.endPacket();
        }
      }
    }
  }
}

CrossPointWebServer::WsUploadStatus CrossPointWebServer::getWsUploadStatus() const {
  WsUploadStatus status;
  status.inProgress = wsUploadInProgress;
  status.received = wsUploadReceived;
  status.total = wsUploadSize;
  status.filename = wsUploadFileName.c_str();
  status.lastCompleteName = wsLastCompleteName.c_str();
  status.lastCompleteSize = wsLastCompleteSize;
  status.lastCompleteAt = wsLastCompleteAt;
  return status;
}

static void sendHtmlContent(WebServer* server, const char* data, size_t len) {
  server->sendHeader("Content-Encoding", "gzip");
  server->send_P(200, "text/html", data, len);
}

void CrossPointWebServer::handleRoot() const {
  sendHtmlContent(server.get(), HomePageHtml, sizeof(HomePageHtml));
  LOG_DBG("WEB", "Served root page");
}

void CrossPointWebServer::handleJszip() const {
  server->sendHeader("Content-Encoding", "gzip");
  server->send_P(200, "application/javascript", jszip_minJs, jszip_minJsCompressedSize);
  LOG_DBG("WEB", "Served jszip.min.js");
}

void CrossPointWebServer::handleNotFound() const {
  // in AP mode, redirect unmatched browser/captive-portal requests to "/" so the OS auto-opens the browser
  // API requests (/api/*) still return 404 so XHR errors surface correctly
  // see https://en.wikipedia.org/wiki/Captive_portal#Detection
  if (apMode && !server->uri().startsWith("/api/")) {
    server->sendHeader("Location", "/", true);
    server->send(302, "text/plain", "");
    return;
  }

  String message = "404 Not Found\n\n";
  message += "URI: " + server->uri() + "\n";
  server->send(404, "text/plain", message);
}

void CrossPointWebServer::handleStatus() const {
  // Get correct IP based on AP vs STA mode
  const String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  const uint32_t unlockedAchievements = getUnlockedAchievementCount();
  const uint32_t totalAchievements = static_cast<uint32_t>(AchievementId::_COUNT);

  JsonDocument doc;
  doc["version"] = CROSSPOINT_VERSION;
  doc["ip"] = ipAddr;
  doc["mode"] = apMode ? "AP" : "STA";
  doc["rssi"] = apMode ? 0 : WiFi.RSSI();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["uptime"] = millis() / 1000;
  doc["recentCount"] = RECENT_BOOKS.getCount();
  doc["booksStarted"] = READING_STATS.getBooksStartedCount();
  doc["booksCompleted"] = READING_STATS.getBooksFinishedCount();
  doc["todayReadingMs"] = READING_STATS.getTodayReadingMs();
  doc["dailyGoalMs"] = getDailyReadingGoalMs();
  doc["streakDays"] = READING_STATS.getCurrentStreakDays();
  doc["achievementsUnlocked"] = unlockedAchievements;
  doc["achievementsTotal"] = totalAchievements;

  char serialNumber[33] = {};
  bool validSerial = false;
  if (esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, serialNumber, 256) == ESP_OK) {
    validSerial = serialNumber[0] != '\0' && serialNumber[0] != static_cast<char>(0xFF);
    for (size_t index = 0; validSerial && index < 32 && serialNumber[index] != '\0'; ++index) {
      validSerial = std::isprint(static_cast<unsigned char>(serialNumber[index])) != 0;
    }
  }
  doc["serial"] = validSerial ? serialNumber : "Not found";

  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
}

void CrossPointWebServer::scanFiles(const char* path, const FileVisitor visitor, void* context) const {
  FsFile root = Storage.open(path);
  if (!root) {
    LOG_DBG("WEB", "Failed to open directory: %s", path);
    return;
  }

  if (!root.isDirectory()) {
    LOG_DBG("WEB", "Not a directory: %s", path);
    root.close();
    return;
  }

  LOG_DBG("WEB", "Scanning files in: %s", path);

  FsFile file = root.openNextFile();
  char name[500];
  while (file) {
    file.getName(name, sizeof(name));
    auto fileName = String(name);

    // Skip hidden items (starting with ".")
    bool shouldHide = !SETTINGS.showHiddenFiles && fileName.startsWith(".");

    // Check against explicitly hidden items list
    if (!shouldHide) {
      for (const auto* item : HIDDEN_ITEMS) {
        if (fileName.equals(item)) {
          shouldHide = true;
          break;
        }
      }
    }

    if (!shouldHide) {
      FileInfo info;
      info.name = fileName;
      info.isDirectory = file.isDirectory();

      if (info.isDirectory) {
        info.size = 0;
        info.isEpub = false;
        info.completed = false;
      } else {
        info.size = file.size();
        info.isEpub = isEpubFile(info.name);
        String fullPath = path;
        if (!fullPath.endsWith("/")) {
          fullPath += "/";
        }
        fullPath += info.name;
        info.completed = isCompletedReadingFilePath(fullPath);
      }

      visitor(info, context);
    }

    file.close();
    yield();               // Yield to allow WiFi and other tasks to process during long scans
    esp_task_wdt_reset();  // Reset watchdog to prevent timeout on large directories
    file = root.openNextFile();
  }
  root.close();
}

bool CrossPointWebServer::isEpubFile(const String& filename) const { return FsHelpers::hasEpubExtension(filename); }

void CrossPointWebServer::handleFileList() const {
  sendHtmlContent(server.get(), FilesPageHtml, sizeof(FilesPageHtml));
}

void CrossPointWebServer::handleFontsPage() const {
  sendHtmlContent(server.get(), FontsPageHtml, sizeof(FontsPageHtml));
  LOG_DBG("WEB", "Served fonts page");
}

void CrossPointWebServer::handleIfFoundPage() const {
  sendHtmlContent(server.get(), IfFoundPageHtml, sizeof(IfFoundPageHtml));
  LOG_DBG("WEB", "Served if_found page");
}

void CrossPointWebServer::handleFontList() const {
  const_cast<SdCardFontSystem&>(sdFontSystem).refreshIfDirty();
  const auto& families = sdFontSystem.registry().getFamilies();

  JsonDocument doc;
  JsonArray arr = doc["families"].to<JsonArray>();
  doc["maxFamilies"] = SdCardFontRegistry::MAX_SD_FAMILIES;

  for (const auto& family : families) {
    JsonObject fObj = arr.add<JsonObject>();
    fObj["name"] = family.name;

    JsonArray sizes = fObj["sizes"].to<JsonArray>();
    for (uint8_t s : family.availableSizes()) {
      sizes.add(s);
    }

    JsonArray files = fObj["files"].to<JsonArray>();
    for (const auto& file : family.files) {
      JsonObject fileObj = files.add<JsonObject>();
      const char* name = strrchr(file.path.c_str(), '/');
      fileObj["name"] = name ? name + 1 : file.path.c_str();

      FsFile f;
      if (Storage.openFileForRead("WEB", file.path.c_str(), f)) {
        fileObj["size"] = static_cast<unsigned long>(f.size());
        f.close();
      } else {
        fileObj["size"] = 0;
      }
    }
  }

  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
}

void CrossPointWebServer::handleFontUploadData() {
  HTTPUpload& upload = server->upload();

  switch (upload.status) {
    case UPLOAD_FILE_START: {
      esp_task_wdt_reset();
      String family = server->arg("family");
      fontUpload.file = HalFile();
      fontUpload.valid = false;
      fontUpload.magicChecked = false;
      fontUpload.bytesWritten = 0;
      fontUpload.bufferPos = 0;
      fontUpload.filePath.clear();
      fontUpload.temporaryPath.clear();
      fontUpload.backupPath.clear();
      fontUpload.familyName.clear();

      if (!FontInstaller::isValidFamilyName(family.c_str())) {
        LOG_ERR("WEB", "Invalid font family name: %s", family.c_str());
        break;
      }

      String filename = upload.filename;
      filename.replace(' ', '_');
      if (!FontInstaller::isValidCpfontFilename(filename.c_str())) {
        LOG_ERR("WEB", "Invalid font filename: %s", filename.c_str());
        break;
      }

      fontUpload.familyName = family.c_str();

      FontInstaller installer(sdFontSystem.registry());
      if (!installer.ensureFamilyDir(family.c_str())) {
        LOG_ERR("WEB", "Failed to create font family dir");
        break;
      }

      char path[128];
      FontInstaller::buildFontPath(family.c_str(), filename.c_str(), path, sizeof(path));
      fontUpload.filePath = path;
      fontUpload.temporaryPath = fontUpload.filePath + ".upload.tmp";
      fontUpload.backupPath = fontUpload.filePath + ".previous";

      if (!AtomicFileReplace::recover(Storage, fontUpload.filePath.c_str(), fontUpload.backupPath.c_str())) {
        LOG_ERR("WEB", "Failed to recover previous font file: %s", path);
        break;
      }
      if (Storage.exists(fontUpload.temporaryPath.c_str())) Storage.remove(fontUpload.temporaryPath.c_str());

      if (!Storage.openFileForWrite("WEB", fontUpload.temporaryPath, fontUpload.file)) {
        LOG_ERR("WEB", "Failed to open font staging file: %s", fontUpload.temporaryPath.c_str());
        break;
      }

      fontUpload.valid = true;
      LOG_DBG("WEB", "Font upload started: %s -> %s", filename.c_str(), path);
      break;
    }

    case UPLOAD_FILE_WRITE: {
      if (!fontUpload.valid) break;
      esp_task_wdt_reset();

      size_t remaining = upload.currentSize;
      const uint8_t* src = upload.buf;
      while (remaining > 0) {
        const size_t space = FontUploadState::BUFFER_SIZE - fontUpload.bufferPos;
        const size_t chunk = (remaining < space) ? remaining : space;
        memcpy(fontUpload.buffer.data() + fontUpload.bufferPos, src, chunk);
        fontUpload.bufferPos += chunk;
        src += chunk;
        remaining -= chunk;

        if (!fontUpload.magicChecked && fontUpload.bytesWritten == 0 && fontUpload.bufferPos >= 8) {
          if (memcmp(fontUpload.buffer.data(), "CPFONT\0\0", 8) != 0) {
            LOG_ERR("WEB", "Invalid .cpfont magic bytes");
            fontUpload.valid = false;
            break;
          }
          fontUpload.magicChecked = true;
        }

        if (fontUpload.bufferPos >= FontUploadState::BUFFER_SIZE) {
          const size_t written = fontUpload.file.write(fontUpload.buffer.data(), fontUpload.bufferPos);
          fontUpload.bytesWritten += written;
          if (written != fontUpload.bufferPos) {
            LOG_ERR("WEB", "Short write while staging font");
            fontUpload.valid = false;
            break;
          }
          fontUpload.bufferPos = 0;
          esp_task_wdt_reset();
        }
      }
      break;
    }

    case UPLOAD_FILE_END: {
      if (fontUpload.valid && !fontUpload.magicChecked && fontUpload.bufferPos >= 8) {
        fontUpload.magicChecked = memcmp(fontUpload.buffer.data(), "CPFONT\0\0", 8) == 0;
      }
      if (!fontUpload.magicChecked) fontUpload.valid = false;
      if (fontUpload.valid && fontUpload.bufferPos > 0) {
        const size_t written = fontUpload.file.write(fontUpload.buffer.data(), fontUpload.bufferPos);
        fontUpload.bytesWritten += written;
        fontUpload.valid = written == fontUpload.bufferPos;
        fontUpload.bufferPos = 0;
      }
      if (fontUpload.file.isOpen()) {
        fontUpload.valid = fontUpload.file.close() && fontUpload.valid;
      }

      if (fontUpload.valid) {
        fontUpload.valid = AtomicFileReplace::promote(Storage, fontUpload.temporaryPath.c_str(),
                                                      fontUpload.filePath.c_str(), fontUpload.backupPath.c_str());
      } else if (!fontUpload.temporaryPath.empty()) {
        Storage.remove(fontUpload.temporaryPath.c_str());
      }

      LOG_DBG("WEB", "Font upload end: valid=%d, %zu bytes", fontUpload.valid, fontUpload.bytesWritten);
      break;
    }

    case UPLOAD_FILE_ABORTED: {
      if (fontUpload.file.isOpen()) {
        fontUpload.file.close();
      }
      if (!fontUpload.temporaryPath.empty()) {
        Storage.remove(fontUpload.temporaryPath.c_str());
      }
      fontUpload.valid = false;
      LOG_DBG("WEB", "Font upload aborted");
      break;
    }
  }
}

void CrossPointWebServer::handleFontUpload() {
  if (fontUpload.valid) {
    sdFontSystem.markRegistryDirty();
    server->send(200, "application/json", "{\"ok\":true}");
    LOG_DBG("WEB", "Font upload complete: %s", fontUpload.filePath.c_str());
  } else {
    server->send(400, "application/json", "{\"error\":\"Invalid .cpfont file\"}");
  }
}

void CrossPointWebServer::handleFontDelete() {
  String body = server->arg("plain");
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);

  if (err || !doc["family"].is<const char*>()) {
    server->send(400, "application/json", "{\"error\":\"Invalid request\"}");
    return;
  }

  const char* familyName = doc["family"];
  FontInstaller installer(sdFontSystem.registry());
  auto result = installer.deleteFamily(familyName);

  if (result == FontInstaller::Error::OK) {
    sdFontSystem.markRegistryDirty();
    server->send(200, "application/json", "{\"ok\":true}");
    LOG_DBG("WEB", "Deleted font family: %s", familyName);
  } else {
    server->send(500, "application/json", "{\"error\":\"Delete failed\"}");
    LOG_ERR("WEB", "Failed to delete font family: %s", familyName);
  }
}

void CrossPointWebServer::handleGetIfFound() const {
  std::string path = IfFoundFile::findPath();
  const bool exists = !path.empty();
  if (!exists) {
    path = IfFoundFile::DEFAULT_PATH;
  }
  const std::string content = exists ? IfFoundFile::readNormalized(path) : "";

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("{\"exists\":");
  server->sendContent(exists ? "true" : "false");
  server->sendContent(",\"path\":");
  sendJsonEscaped(server.get(), path.c_str());
  server->sendContent(",\"maxBytes\":");
  char buffer[24];
  snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned>(IfFoundFile::MAX_BYTES));
  server->sendContent(buffer);
  server->sendContent(",\"content\":");
  sendJsonEscaped(server.get(), content.c_str());
  server->sendContent("}");
  server->sendContent("");
  LOG_DBG("WEB", "Served if_found content path=%s exists=%d bytes=%u", path.c_str(), exists,
          static_cast<unsigned>(content.size()));
}

void CrossPointWebServer::handlePostIfFound() {
  const String content = server->arg("plain");
  if (static_cast<size_t>(content.length()) > IfFoundFile::MAX_BYTES) {
    server->send(413, "application/json", "{\"error\":\"Content is too large\"}");
    return;
  }

  std::string path = IfFoundFile::findPath();
  if (path.empty()) {
    path = IfFoundFile::DEFAULT_PATH;
  }

  FsFile file;
  if (!Storage.openFileForWrite("IFF", path, file)) {
    server->send(500, "application/json", "{\"error\":\"Could not open if_found.txt for writing\"}");
    return;
  }

  const size_t expected = static_cast<size_t>(content.length());
  const size_t written = expected == 0 ? 0 : file.write(reinterpret_cast<const uint8_t*>(content.c_str()), expected);
  file.close();

  if (written != expected) {
    server->send(500, "application/json", "{\"error\":\"Could not write the complete file\"}");
    return;
  }

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("{\"ok\":true,\"path\":");
  sendJsonEscaped(server.get(), path.c_str());
  server->sendContent(",\"bytes\":");
  char buffer[24];
  snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned>(written));
  server->sendContent(buffer);
  server->sendContent("}");
  server->sendContent("");
  LOG_DBG("WEB", "Saved if_found content path=%s bytes=%u", path.c_str(), static_cast<unsigned>(written));
}

void CrossPointWebServer::handleFileListData() const {
  // Get current path from query string (default to root)
  String currentPath = "/";
  if (server->hasArg("path")) {
    currentPath = normalizeWebPath(server->arg("path"));
  }
  if (isProtectedPath(currentPath)) {
    server->send(403, "application/json", "[]");
    return;
  }

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  // Keep writes near a TCP segment without adding 1.4KB to this task's stack.
  // Allocation is fallible; low-memory devices retain the per-entry path.
  constexpr size_t BATCH_CAPACITY = 1400;
  constexpr size_t OUTPUT_CAPACITY = 640;
  constexpr size_t FALLBACK_OUTPUT_CAPACITY = 240;
  auto scratch = makeUniqueNoThrow<char[]>(BATCH_CAPACITY + OUTPUT_CAPACITY);
  char fallbackOutput[FALLBACK_OUTPUT_CAPACITY];
  JsonDocument doc;

  struct FileListContext {
    WebServer* server;
    char* batch;
    size_t batchLength;
    char* output;
    size_t outputCapacity;
    JsonDocument* doc;
    bool seenFirst;
  } context{server.get(),
            scratch ? scratch.get() : nullptr,
            0,
            scratch ? scratch.get() + BATCH_CAPACITY : fallbackOutput,
            scratch ? OUTPUT_CAPACITY : FALLBACK_OUTPUT_CAPACITY,
            &doc,
            false};

  if (context.batch) {
    context.batch[context.batchLength++] = '[';
  } else {
    LOG_ERR("WEB", "OOM: file list scratch buffer; using limited per-entry sends");
    server->sendContent("[");
  }

  scanFiles(
      currentPath.c_str(),
      [](const FileInfo& info, void* rawContext) {
        auto& context = *static_cast<FileListContext*>(rawContext);
        context.doc->clear();
        (*context.doc)["name"] = info.name;
        (*context.doc)["size"] = info.size;
        (*context.doc)["isDirectory"] = info.isDirectory;
        (*context.doc)["isEpub"] = info.isEpub;
        (*context.doc)["completed"] = info.completed;

        const size_t written = serializeJson(*context.doc, context.output, context.outputCapacity);
        if (written >= context.outputCapacity) {
          LOG_DBG("WEB", "Skipping file entry with oversized JSON for name: %s", info.name.c_str());
          return;
        }

        const size_t required = written + (context.seenFirst ? 1 : 0);
        if (context.batch) {
          if (context.batchLength + required > BATCH_CAPACITY) {
            context.server->sendContent(context.batch, context.batchLength);
            context.batchLength = 0;
          }
          if (context.seenFirst) context.batch[context.batchLength++] = ',';
          memcpy(context.batch + context.batchLength, context.output, written);
          context.batchLength += written;
        } else {
          if (context.seenFirst) context.server->sendContent(",");
          context.server->sendContent(context.output);
        }
        context.seenFirst = true;
      },
      &context);

  if (context.batch) {
    if (context.batchLength + 1 > BATCH_CAPACITY) {
      server->sendContent(context.batch, context.batchLength);
      context.batchLength = 0;
    }
    context.batch[context.batchLength++] = ']';
    server->sendContent(context.batch, context.batchLength);
  } else {
    server->sendContent("]");
  }
  // End of streamed response, empty chunk to signal client
  server->sendContent("");
  LOG_DBG("WEB", "Served file listing page for path: %s", currentPath.c_str());
}

void CrossPointWebServer::handleDownload() const {
  if (!server->hasArg("path")) {
    server->send(400, "text/plain", "Missing path");
    return;
  }

  String itemPath = normalizeWebPath(server->arg("path"));
  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Invalid path");
    return;
  }
  if (isProtectedPath(itemPath)) {
    server->send(403, "text/plain", "Cannot access protected path");
    return;
  }

  if (!Storage.exists(itemPath.c_str())) {
    server->send(404, "text/plain", "Item not found");
    return;
  }

  FsFile file = Storage.open(itemPath.c_str());
  if (!file) {
    server->send(500, "text/plain", "Failed to open file");
    return;
  }
  if (file.isDirectory()) {
    file.close();
    server->send(400, "text/plain", "Path is a directory");
    return;
  }

  String contentType = "application/octet-stream";
  if (isEpubFile(itemPath)) {
    contentType = "application/epub+zip";
  }

  char nameBuf[128] = {0};
  String filename = "download";
  if (file.getName(nameBuf, sizeof(nameBuf))) {
    filename = nameBuf;
  }

  const size_t fileSize = file.size();
  server->sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  server->setContentLength(fileSize);
  server->send(200, contentType.c_str(), "");

  NetworkClient client = server->client();
  constexpr size_t chunkSize = 2048;
  uint8_t buffer[chunkSize];
  size_t sent = 0;

  file.seekSet(0);
  while (sent < fileSize && client.connected()) {
    esp_task_wdt_reset();
    const size_t remaining = fileSize - sent;
    const size_t toRead = std::min(chunkSize, remaining);
    const int bytesRead = file.read(buffer, toRead);
    if (bytesRead <= 0) {
      break;
    }

    size_t writtenForChunk = 0;
    uint8_t zeroWriteRetries = 0;
    while (writtenForChunk < static_cast<size_t>(bytesRead) && client.connected()) {
      esp_task_wdt_reset();
      const size_t wrote = client.write(buffer + writtenForChunk, static_cast<size_t>(bytesRead) - writtenForChunk);
      if (wrote == 0) {
        if (++zeroWriteRetries >= 5) {
          break;
        }
        delay(2);
        continue;
      }
      zeroWriteRetries = 0;
      writtenForChunk += wrote;
    }

    sent += writtenForChunk;
    if (writtenForChunk != static_cast<size_t>(bytesRead)) {
      break;
    }
    yield();
  }

  if (sent != fileSize) {
    LOG_ERR("WEB", "Download size mismatch for %s: sent %u of %u bytes", itemPath.c_str(), (unsigned)sent,
            (unsigned)fileSize);
  }
  file.close();
}

// Diagnostic counters for upload performance analysis
static unsigned long uploadStartTime = 0;
static unsigned long totalWriteTime = 0;
static size_t writeCount = 0;

static bool flushUploadBuffer(CrossPointWebServer::UploadState& state) {
  if (state.bufferPos > 0 && state.file) {
    esp_task_wdt_reset();  // Reset watchdog before potentially slow SD write
    const unsigned long writeStart = millis();
    const size_t written = state.file.write(state.buffer.data(), state.bufferPos);
    totalWriteTime += millis() - writeStart;
    writeCount++;
    esp_task_wdt_reset();  // Reset watchdog after SD write

    if (written != state.bufferPos) {
      LOG_DBG("WEB", "[UPLOAD] Buffer flush failed: expected %d, wrote %d", state.bufferPos, written);
      state.bufferPos = 0;
      return false;
    }
    state.bufferPos = 0;
  }
  return true;
}

void CrossPointWebServer::handleUpload(UploadState& state) const {
  static size_t lastLoggedSize = 0;

  // Reset watchdog at start of every upload callback - HTTP parsing can be slow
  esp_task_wdt_reset();

  // Safety check: ensure server is still valid
  if (!running || !server) {
    LOG_DBG("WEB", "[UPLOAD] ERROR: handleUpload called but server not running!");
    return;
  }

  const HTTPUpload& upload = server->upload();

  if (upload.status == UPLOAD_FILE_START) {
    // Reset watchdog - this is the critical 1% crash point
    esp_task_wdt_reset();

    state.transaction.cancel(state.file);
    state.fileName = WebPathUtils::sanitizeFilename(upload.filename);
    state.size = 0;
    state.success = false;
    state.error = "";
    uploadStartTime = millis();
    lastLoggedSize = 0;
    state.bufferPos = 0;
    totalWriteTime = 0;
    writeCount = 0;

    // Get upload path from query parameter (defaults to root if not specified)
    // Note: We use query parameter instead of form data because multipart form
    // fields aren't available until after file upload completes
    state.path = normalizeWebPath(server->hasArg("path") ? server->arg("path") : String("/"));
    if (state.fileName.isEmpty() || isProtectedPath(state.path)) {
      state.error = "Invalid or protected upload path";
      LOG_ERR("WEB", "Rejected upload path or filename");
      return;
    }

    LOG_DBG("WEB", "[UPLOAD] START: %s to path: %s", state.fileName.c_str(), state.path.c_str());
    LOG_DBG("WEB", "[UPLOAD] Free heap: %d bytes", ESP.getFreeHeap());

    // Create file path
    String filePath = state.path;
    if (!filePath.endsWith("/")) filePath += "/";
    filePath += state.fileName;
    if (isProtectedPath(filePath)) {
      state.error = "Cannot upload to protected path";
      return;
    }

    // Stage the body separately; keep the old book until the complete upload
    // has been written and validated.
    esp_task_wdt_reset();
    if (!state.transaction.begin(filePath, state.file)) {
      state.error = "Failed to create file on SD card";
      LOG_DBG("WEB", "[UPLOAD] FAILED to create file: %s", filePath.c_str());
      return;
    }
    esp_task_wdt_reset();

    LOG_DBG("WEB", "[UPLOAD] File created successfully: %s", filePath.c_str());
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (state.file && state.error.isEmpty()) {
      // Buffer incoming data and flush when buffer is full
      // This reduces SD card write operations and improves throughput
      const uint8_t* data = upload.buf;
      size_t remaining = upload.currentSize;

      while (remaining > 0) {
        const size_t space = UploadState::UPLOAD_BUFFER_SIZE - state.bufferPos;
        const size_t toCopy = (remaining < space) ? remaining : space;

        memcpy(state.buffer.data() + state.bufferPos, data, toCopy);
        state.bufferPos += toCopy;
        data += toCopy;
        remaining -= toCopy;

        // Flush buffer when full
        if (state.bufferPos >= UploadState::UPLOAD_BUFFER_SIZE) {
          if (!flushUploadBuffer(state)) {
            state.error = "Failed to write to SD card - disk may be full";
            state.transaction.cancel(state.file);
            return;
          }
        }
      }

      state.size += upload.currentSize;

      // Log progress every 100KB
      if (state.size - lastLoggedSize >= 102400) {
        const unsigned long elapsed = millis() - uploadStartTime;
        const float kbps = (elapsed > 0) ? (state.size / 1024.0) / (elapsed / 1000.0) : 0;
        LOG_DBG("WEB", "[UPLOAD] %d bytes (%.1f KB), %.1f KB/s, %d writes", state.size, state.size / 1024.0, kbps,
                writeCount);
        lastLoggedSize = state.size;
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (state.file) {
      // Flush any remaining buffered data
      if (!flushUploadBuffer(state)) {
        state.error = "Failed to write final data to SD card";
      }
      if (state.error.isEmpty() &&
          (state.size != upload.totalSize || !state.transaction.finish(state.file, state.size))) {
        state.error = "Failed to finish upload; previous file preserved";
      }
      if (!state.error.isEmpty()) state.transaction.cancel(state.file);

      if (state.error.isEmpty()) {
        state.success = true;
        const unsigned long elapsed = millis() - uploadStartTime;
        const float avgKbps = (elapsed > 0) ? (state.size / 1024.0) / (elapsed / 1000.0) : 0;
        const float writePercent = (elapsed > 0) ? (totalWriteTime * 100.0 / elapsed) : 0;
        LOG_DBG("WEB", "[UPLOAD] Complete: %s (%d bytes in %lu ms, avg %.1f KB/s)", state.fileName.c_str(), state.size,
                elapsed, avgKbps);
        LOG_DBG("WEB", "[UPLOAD] Diagnostics: %d writes, total write time: %lu ms (%.1f%%)", writeCount, totalWriteTime,
                writePercent);

        // Clear epub cache to prevent stale metadata issues when overwriting files
        String filePath = state.path;
        if (!filePath.endsWith("/")) filePath += "/";
        filePath += state.fileName;
        clearBookCache(filePath.c_str());
      }
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    state.bufferPos = 0;  // Discard buffered data
    state.transaction.cancel(state.file);
    state.error = "Upload aborted";
    LOG_DBG("WEB", "Upload aborted");
  }
}

void CrossPointWebServer::handleUploadPost(UploadState& state) const {
  if (state.success) {
    server->send(200, "text/plain", "File uploaded successfully: " + state.fileName);
  } else {
    const String error = state.error.isEmpty() ? "Unknown error during upload" : state.error;
    server->send(400, "text/plain", error);
  }
}

void CrossPointWebServer::handleCreateFolder() const {
  // Get folder name from form data
  if (!server->hasArg("name")) {
    server->send(400, "text/plain", "Missing folder name");
    return;
  }

  const String folderName = WebPathUtils::sanitizeFilename(server->arg("name"));

  // Validate folder name
  if (folderName.isEmpty()) {
    server->send(400, "text/plain", "Folder name cannot be empty");
    return;
  }

  // Get parent path
  String parentPath = "/";
  if (server->hasArg("path")) {
    parentPath = normalizeWebPath(server->arg("path"));
  }
  if (isProtectedPath(parentPath)) {
    server->send(403, "text/plain", "Cannot create folder in protected path");
    return;
  }

  // Build full folder path
  String folderPath = parentPath;
  if (!folderPath.endsWith("/")) folderPath += "/";
  folderPath += folderName;
  if (isProtectedPath(folderPath)) {
    server->send(403, "text/plain", "Cannot create protected folder");
    return;
  }

  LOG_DBG("WEB", "Creating folder: %s", folderPath.c_str());

  // Check if already exists
  if (Storage.exists(folderPath.c_str())) {
    server->send(400, "text/plain", "Folder already exists");
    return;
  }

  // Create the folder
  if (Storage.mkdir(folderPath.c_str())) {
    LOG_DBG("WEB", "Folder created successfully: %s", folderPath.c_str());
    server->send(200, "text/plain", "Folder created: " + folderName);
  } else {
    LOG_DBG("WEB", "Failed to create folder: %s", folderPath.c_str());
    server->send(500, "text/plain", "Failed to create folder");
  }
}

void CrossPointWebServer::handleRename() const {
  if (!server->hasArg("path") || !server->hasArg("name")) {
    server->send(400, "text/plain", "Missing path or new name");
    return;
  }

  String itemPath = normalizeWebPath(server->arg("path"));
  String newName = server->arg("name");
  newName.trim();

  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Invalid path");
    return;
  }
  if (newName.isEmpty()) {
    server->send(400, "text/plain", "New name cannot be empty");
    return;
  }
  if (newName.indexOf('/') >= 0 || newName.indexOf('\\') >= 0) {
    server->send(400, "text/plain", "Invalid file name");
    return;
  }
  if (isProtectedPath("/" + newName)) {
    server->send(403, "text/plain", "Cannot rename to protected name");
    return;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (isProtectedPath(itemPath)) {
    server->send(403, "text/plain", "Cannot rename protected item");
    return;
  }
  if (newName == itemName) {
    server->send(200, "text/plain", "Name unchanged");
    return;
  }

  if (!Storage.exists(itemPath.c_str())) {
    server->send(404, "text/plain", "Item not found");
    return;
  }

  FsFile file = Storage.open(itemPath.c_str());
  if (!file) {
    server->send(500, "text/plain", "Failed to open file");
    return;
  }
  if (file.isDirectory()) {
    file.close();
    server->send(400, "text/plain", "Only files can be renamed");
    return;
  }

  String parentPath = itemPath.substring(0, itemPath.lastIndexOf('/'));
  if (parentPath.isEmpty()) {
    parentPath = "/";
  }
  String newPath = parentPath;
  if (!newPath.endsWith("/")) {
    newPath += "/";
  }
  newPath += newName;

  if (Storage.exists(newPath.c_str())) {
    file.close();
    server->send(409, "text/plain", "Target already exists");
    return;
  }

  clearBookCache(itemPath.c_str());
  const bool success = file.rename(newPath.c_str());
  file.close();

  if (success) {
    LOG_DBG("WEB", "Renamed file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(200, "text/plain", "Renamed successfully");
  } else {
    LOG_ERR("WEB", "Failed to rename file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(500, "text/plain", "Failed to rename file");
  }
}

void CrossPointWebServer::handleMove() const {
  if (!server->hasArg("path") || !server->hasArg("dest")) {
    server->send(400, "text/plain", "Missing path or destination");
    return;
  }

  String itemPath = normalizeWebPath(server->arg("path"));
  String destPath = normalizeWebPath(server->arg("dest"));

  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Invalid path");
    return;
  }
  if (destPath.isEmpty()) {
    server->send(400, "text/plain", "Invalid destination");
    return;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (isProtectedPath(itemPath)) {
    server->send(403, "text/plain", "Cannot move protected item");
    return;
  }
  if (isProtectedPath(destPath)) {
    server->send(403, "text/plain", "Cannot move into protected folder");
    return;
  }

  if (!Storage.exists(itemPath.c_str())) {
    server->send(404, "text/plain", "Item not found");
    return;
  }

  FsFile file = Storage.open(itemPath.c_str());
  if (!file) {
    server->send(500, "text/plain", "Failed to open file");
    return;
  }
  if (file.isDirectory()) {
    file.close();
    server->send(400, "text/plain", "Only files can be moved");
    return;
  }

  if (!Storage.exists(destPath.c_str())) {
    file.close();
    server->send(404, "text/plain", "Destination not found");
    return;
  }
  FsFile destDir = Storage.open(destPath.c_str());
  if (!destDir || !destDir.isDirectory()) {
    if (destDir) {
      destDir.close();
    }
    file.close();
    server->send(400, "text/plain", "Destination is not a folder");
    return;
  }
  destDir.close();

  String newPath = destPath;
  if (!newPath.endsWith("/")) {
    newPath += "/";
  }
  newPath += itemName;

  if (newPath == itemPath) {
    file.close();
    server->send(200, "text/plain", "Already in destination");
    return;
  }
  if (Storage.exists(newPath.c_str())) {
    file.close();
    server->send(409, "text/plain", "Target already exists");
    return;
  }

  clearBookCache(itemPath.c_str());
  const bool success = file.rename(newPath.c_str());
  file.close();

  if (success) {
    LOG_DBG("WEB", "Moved file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(200, "text/plain", "Moved successfully");
  } else {
    LOG_ERR("WEB", "Failed to move file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(500, "text/plain", "Failed to move file");
  }
}

void CrossPointWebServer::handleDelete() const {
  // To ensure backwards compatibility, plain `path` is mapped
  // to a single element JSON array.
  bool hasPathArg = server->hasArg("path");
  bool hasPathsArg = server->hasArg("paths");
  // Check 'paths' or `path` argument is provided
  if (!(hasPathArg || hasPathsArg)) {
    server->send(400, "text/plain", "Missing `path` or `paths` argument");
    return;
  }
  if (hasPathArg && hasPathsArg) {
    server->send(400, "text/plain", "Provide either 'path' or 'paths', not both");
    return;
  }

  // Parse paths
  String pathsArg;
  JsonDocument doc;
  DeserializationError error = DeserializationError(DeserializationError::Code::Ok);
  if (hasPathsArg) {
    pathsArg = server->arg("paths");
    error = deserializeJson(doc, pathsArg);
  } else {
    pathsArg = server->arg("path");
    doc.add(pathsArg);
  }
  if (error) {
    server->send(400, "text/plain", "Invalid paths format");
    return;
  }

  auto paths = doc.as<JsonArray>();
  if (paths.isNull() || paths.size() == 0) {
    server->send(400, "text/plain", "No paths provided");
    return;
  }

  // Iterate over paths and delete each item
  bool allSuccess = true;
  String failedItems;

  for (const auto& p : paths) {
    auto itemPath = normalizeWebPath(p.as<String>());

    // Validate path
    if (itemPath.isEmpty() || itemPath == "/") {
      failedItems += itemPath + " (cannot delete root); ";
      allSuccess = false;
      continue;
    }

    if (isProtectedPath(itemPath)) {
      failedItems += itemPath + " (protected path); ";
      allSuccess = false;
      continue;
    }

    // Check if item exists
    if (!Storage.exists(itemPath.c_str())) {
      failedItems += itemPath + " (not found); ";
      allSuccess = false;
      continue;
    }

    // Decide whether it's a directory or file by opening it
    bool success = false;
    FsFile f = Storage.open(itemPath.c_str());
    if (f && f.isDirectory()) {
      // For folders, ensure empty before removing
      FsFile entry = f.openNextFile();
      if (entry) {
        entry.close();
        f.close();
        failedItems += itemPath + " (folder not empty); ";
        allSuccess = false;
        continue;
      }
      f.close();
      success = Storage.rmdir(itemPath.c_str());
    } else {
      // It's a file (or couldn't open as dir) — remove file
      if (f) f.close();
      success = Storage.remove(itemPath.c_str());
      clearBookCache(itemPath.c_str());
    }

    if (!success) {
      failedItems += itemPath + " (deletion failed); ";
      allSuccess = false;
    }
  }

  if (allSuccess) {
    server->send(200, "text/plain", "All items deleted successfully");
  } else {
    server->send(500, "text/plain", "Failed to delete some items: " + failedItems);
  }
}

void CrossPointWebServer::handleSettingsPage() const {
  sendHtmlContent(server.get(), SettingsPageHtml, sizeof(SettingsPageHtml));
  LOG_DBG("WEB", "Served settings page");
}

void CrossPointWebServer::handleGetSettings() const {
  int requestedCategory = -1;
  if (server->hasArg("category")) {
    const String categoryArg = server->arg("category");
    requestedCategory = categoryArg.toInt();
    if (requestedCategory < 0 || requestedCategory > 7) {
      server->send(400, "text/plain", "Invalid category");
      return;
    }
  }

  LOG_DBG("WEB", "[MEM] /api/settings start category=%d free=%u min=%u", requestedCategory, ESP.getFreeHeap(),
          ESP.getMinFreeHeap());

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  bool seenFirst = false;

  for (const auto& s : WEB_SETTINGS) {
    if (!isWebSettingVisible(s)) continue;
    if (requestedCategory >= 0 && webSettingsCategoryIndex(s.category) != requestedCategory) continue;

    if (seenFirst) {
      server->sendContent(",", 1);
    } else {
      seenFirst = true;
    }

    server->sendContent("{", 1);
    sendJsonStringField(server.get(), "key", s.key);
    server->sendContent(",", 1);
    sendJsonStringField(server.get(), "name", I18N.get(s.nameId));
    server->sendContent(",", 1);
    sendJsonStringField(server.get(), "category", I18N.get(s.category));
    server->sendContent(",", 1);

    bool handled = true;
    switch (s.type) {
      case WebSettingType::Toggle: {
        sendJsonStringField(server.get(), "type", "toggle");
        if (s.valuePtr) {
          server->sendContent(",", 1);
          sendJsonIntField(server.get(), "value", static_cast<int>(SETTINGS.*(s.valuePtr)));
        }
        break;
      }
      case WebSettingType::Enum: {
        sendJsonStringField(server.get(), "type", "enum");
        server->sendContent(",", 1);
        if (s.valuePtr) {
          sendJsonIntField(server.get(), "value", static_cast<int>(SETTINGS.*(s.valuePtr)));
        } else if (s.dynamic == WebDynamicSetting::KoMatchMethod) {
          sendJsonIntField(server.get(), "value", static_cast<int>(KOREADER_STORE.getMatchMethod()));
        } else {
          sendJsonIntField(server.get(), "value", 0);
        }
        sendRaw(server.get(), ",\"options\":[");
        bool seenOption = false;
        for (uint8_t i = 0; i < s.optionCount; i++) {
          if (seenOption) {
            server->sendContent(",", 1);
          } else {
            seenOption = true;
          }
          sendJsonEscaped(server.get(), I18N.get(s.options[i]));
        }
        server->sendContent("]", 1);
        break;
      }
      case WebSettingType::Value: {
        sendJsonStringField(server.get(), "type", "value");
        if (s.valuePtr) {
          server->sendContent(",", 1);
          sendJsonIntField(server.get(), "value", static_cast<int>(SETTINGS.*(s.valuePtr)));
        }
        server->sendContent(",", 1);
        sendJsonIntField(server.get(), "min", s.min);
        server->sendContent(",", 1);
        sendJsonIntField(server.get(), "max", s.max);
        server->sendContent(",", 1);
        sendJsonIntField(server.get(), "step", s.step);
        break;
      }
      case WebSettingType::String: {
        sendJsonStringField(server.get(), "type", "string");
        server->sendContent(",", 1);
        std::string value;
        switch (s.dynamic) {
          case WebDynamicSetting::KoUsername:
            value = KOREADER_STORE.getUsername();
            break;
          case WebDynamicSetting::KoPassword:
            // Credentials are write-only in the browser. An empty field is
            // ignored by the diff-based settings form unless the user enters
            // a replacement, so the stored password remains unchanged.
            value.clear();
            break;
          case WebDynamicSetting::KoServerUrl:
            value = KOREADER_STORE.getServerUrl();
            break;
          default:
            break;
        }
        sendJsonStringField(server.get(), "value", value.c_str());
        if (s.dynamic == WebDynamicSetting::KoPassword) {
          server->sendContent(",\"configured\":", 14);
          server->sendContent(KOREADER_STORE.getPassword().empty() ? "false" : "true");
        }
        break;
      }
      default:
        handled = false;
        break;
    }

    if (!handled) {
      server->sendContent("}", 1);
      continue;
    }

    server->sendContent("}", 1);
    yield();
    esp_task_wdt_reset();
  }

  server->sendContent("]");
  server->sendContent("");
  LOG_DBG("WEB", "[MEM] /api/settings end category=%d free=%u min=%u", requestedCategory, ESP.getFreeHeap(),
          ESP.getMinFreeHeap());
}

void CrossPointWebServer::handlePostSettings() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  int applied = 0;
  bool saveSettings = false;
  bool saveKOReader = false;
  bool createInitialReadingStatsBackup = false;

  for (const auto& s : WEB_SETTINGS) {
    if (!isWebSettingVisible(s)) continue;
    if (!doc[s.key].is<JsonVariant>()) continue;

    switch (s.type) {
      case WebSettingType::Toggle: {
        const int val = doc[s.key].as<int>() ? 1 : 0;
        if (s.valuePtr) {
          SETTINGS.*(s.valuePtr) = val;
          saveSettings = true;
        }
        applied++;
        break;
      }
      case WebSettingType::Enum: {
        const int val = doc[s.key].as<int>();
        if (val >= 0 && val < static_cast<int>(s.optionCount)) {
          if (s.valuePtr) {
            const uint8_t previousValue = SETTINGS.*(s.valuePtr);
            SETTINGS.*(s.valuePtr) = static_cast<uint8_t>(val);
            if (s.valuePtr == &CrossPointSettings::fontFamily) {
              SETTINGS.sdFontFamilyName[0] = '\0';
            }
            if (s.valuePtr == &CrossPointSettings::readingStatsAutoBackup &&
                SETTINGS.readingStatsAutoBackup != previousValue &&
                SETTINGS.getReadingStatsAutoBackupIntervalDays() > 0 && !READING_STATS.hasAutoBackups()) {
              createInitialReadingStatsBackup = true;
            }
            saveSettings = true;
          } else if (s.dynamic == WebDynamicSetting::KoMatchMethod) {
            KOREADER_STORE.setMatchMethod(static_cast<DocumentMatchMethod>(val));
            saveKOReader = true;
          }
          applied++;
        }
        break;
      }
      case WebSettingType::Value: {
        const int val = doc[s.key].as<int>();
        if (val >= s.min && val <= s.max) {
          if (s.valuePtr) {
            SETTINGS.*(s.valuePtr) = static_cast<uint8_t>(val);
            saveSettings = true;
          }
          applied++;
        }
        break;
      }
      case WebSettingType::String: {
        const std::string val = doc[s.key].as<std::string>();
        switch (s.dynamic) {
          case WebDynamicSetting::KoUsername:
            KOREADER_STORE.setCredentials(val, KOREADER_STORE.getPassword());
            saveKOReader = true;
            break;
          case WebDynamicSetting::KoPassword:
            KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), val);
            saveKOReader = true;
            break;
          case WebDynamicSetting::KoServerUrl:
            KOREADER_STORE.setServerUrl(val);
            saveKOReader = true;
            break;
          default:
            break;
        }
        applied++;
        break;
      }
      default:
        break;
    }
  }

  if (saveSettings) {
    SETTINGS.saveToFile();
    if (createInitialReadingStatsBackup) {
      READING_STATS.ensureAutoBackupForEnabledSetting();
    }
  }
  if (saveKOReader) {
    KOREADER_STORE.saveToFile();
  }

  LOG_DBG("WEB", "Applied %d setting(s)", applied);
  server->send(200, "text/plain", String("Applied ") + String(applied) + " setting(s)");
}

// ---- OPDS Server API ----

void CrossPointWebServer::handleGetOpdsServers() const {
  const auto& servers = OPDS_STORE.getServers();

  // Stream JSON array incrementally to avoid allocating the full response in memory
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  for (size_t i = 0; i < servers.size(); i++) {
    if (i > 0) server->sendContent(",", 1);
    server->sendContent("{", 1);
    sendJsonIntField(server.get(), "index", static_cast<int>(i));
    server->sendContent(",", 1);
    sendJsonStringField(server.get(), "name", servers[i].name.c_str());
    server->sendContent(",", 1);
    sendJsonStringField(server.get(), "url", servers[i].url.c_str());
    server->sendContent(",", 1);
    sendJsonStringField(server.get(), "username", servers[i].username.c_str());
    sendRaw(server.get(), ",\"hasPassword\":");
    sendRaw(server.get(), servers[i].password.empty() ? "false" : "true");
    server->sendContent("}", 1);
    yield();
    esp_task_wdt_reset();
  }

  server->sendContent("]");
  server->sendContent("");
  LOG_DBG("WEB", "Served OPDS servers API (%zu servers)", servers.size());
}

void CrossPointWebServer::handlePostOpdsServer() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  OpdsServer opdsServer;
  opdsServer.name = doc["name"] | std::string("");
  opdsServer.url = doc["url"] | std::string("");
  opdsServer.username = doc["username"] | std::string("");

  // The password field is optional in the JSON payload. When absent (vs. present but empty),
  // we preserve the existing password — the web UI omits it when the user hasn't changed it.
  bool hasPasswordField = doc["password"].is<const char*>() || doc["password"].is<std::string>();
  std::string password = doc["password"] | std::string("");

  if (doc["index"].is<int>()) {
    int idx = doc["index"].as<int>();
    if (idx < 0 || idx >= static_cast<int>(OPDS_STORE.getCount())) {
      server->send(400, "text/plain", "Invalid server index");
      return;
    }
    // Preserve existing password if not explicitly provided
    if (!hasPasswordField) {
      const auto* existing = OPDS_STORE.getServer(static_cast<size_t>(idx));
      if (existing) password = existing->password;
    }
    opdsServer.password = password;
    OPDS_STORE.updateServer(static_cast<size_t>(idx), opdsServer);
    LOG_DBG("WEB", "Updated OPDS server at index %d", idx);
  } else {
    opdsServer.password = password;
    if (!OPDS_STORE.addServer(opdsServer)) {
      server->send(400, "text/plain", "Cannot add server (limit reached)");
      return;
    }
    LOG_DBG("WEB", "Added new OPDS server: %s", opdsServer.name.c_str());
  }

  server->send(200, "text/plain", "OK");
}

// Uses POST (not HTTP DELETE) because ESP32 WebServer doesn't support DELETE with body.
void CrossPointWebServer::handleDeleteOpdsServer() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  if (!doc["index"].is<int>()) {
    server->send(400, "text/plain", "Missing index");
    return;
  }

  int idx = doc["index"].as<int>();
  if (idx < 0 || idx >= static_cast<int>(OPDS_STORE.getCount())) {
    server->send(400, "text/plain", "Invalid server index");
    return;
  }

  OPDS_STORE.removeServer(static_cast<size_t>(idx));
  LOG_DBG("WEB", "Deleted OPDS server at index %d", idx);
  server->send(200, "text/plain", "OK");
}

// WebSocket callback trampoline
void CrossPointWebServer::wsEventCallback(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (wsInstance) {
    wsInstance->onWebSocketEvent(num, type, payload, length);
  }
}

// WebSocket event handler for fast binary uploads
// Protocol:
//   1. Client sends TEXT message: "START:<filename>:<size>:<path>"
//   2. Client sends BINARY messages with file data chunks
//   3. Server sends TEXT "PROGRESS:<received>:<total>" after each chunk
//   4. Server sends TEXT "DONE" or "ERROR:<message>" when complete
void CrossPointWebServer::onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      LOG_DBG("WS", "Client %u disconnected", num);
      // Only clean up if this is the client that owns the active upload.
      // A new client may have already started a fresh upload before this
      // DISCONNECTED event fires (race condition on quick cancel + retry).
      if (num == wsUploadClientNum && wsUploadInProgress && wsUploadFile) {
        abortWsUpload("WS");
      }
      break;

    case WStype_CONNECTED: {
      LOG_DBG("WS", "Client %u connected", num);
      break;
    }

    case WStype_TEXT: {
      // Parse control messages
      String msg;
      if (!payload || length == 0 || !msg.reserve(length) ||
          !msg.concat(reinterpret_cast<const char*>(payload), length) || msg.length() != length ||
          memchr(payload, '\0', length)) {
        wsServer->sendTXT(num, "ERROR:Invalid control message");
        break;
      }
      LOG_DBG("WS", "Text from client %u: %s", num, msg.c_str());

      if (msg.startsWith("START:")) {
        // Reject any START while an upload is already active to prevent
        // leaking the open wsUploadFile handle (owning client re-START included)
        if (wsUploadInProgress) {
          wsServer->sendTXT(num, "ERROR:Upload already in progress");
          break;
        }

        // Parse: START:<filename>:<size>:<path>
        int firstColon = msg.indexOf(':', 6);
        int secondColon = msg.indexOf(':', firstColon + 1);

        if (firstColon > 0 && secondColon > 0) {
          wsUploadFileName = WebPathUtils::sanitizeFilename(msg.substring(6, firstColon));
          String sizeToken = msg.substring(firstColon + 1, secondColon);
          if (!WebUploadSize::parse(std::string_view(sizeToken.c_str(), sizeToken.length()), wsUploadSize)) {
            LOG_DBG("WS", "START rejected: invalid size token '%s'", sizeToken.c_str());
            wsServer->sendTXT(num, "ERROR:Invalid START format");
            return;
          }
          wsUploadPath = normalizeWebPath(msg.substring(secondColon + 1));
          wsUploadReceived = 0;
          wsLastProgressSent = 0;
          wsUploadStartTime = millis();

          if (wsUploadFileName.isEmpty() || isProtectedPath(wsUploadPath)) {
            wsServer->sendTXT(num, "ERROR:Invalid or protected upload path");
            return;
          }

          // Build file path
          String filePath = wsUploadPath;
          if (!filePath.endsWith("/")) filePath += "/";
          filePath += wsUploadFileName;
          if (isProtectedPath(filePath)) {
            wsServer->sendTXT(num, "ERROR:Cannot upload to protected path");
            return;
          }

          LOG_DBG("WS", "Starting upload: %s (%d bytes) to %s", wsUploadFileName.c_str(), wsUploadSize,
                  filePath.c_str());

          // Stage first; cancellation cannot remove the original file.
          esp_task_wdt_reset();
          if (!wsUploadTransaction.begin(filePath, wsUploadFile)) {
            wsServer->sendTXT(num, "ERROR:Failed to create file");
            wsUploadInProgress = false;
            wsUploadClientNum = 255;
            return;
          }
          esp_task_wdt_reset();

          // Zero-byte upload: complete immediately without waiting for BIN frames
          if (wsUploadSize == 0) {
            // Explicit close() required: file-scope global persists beyond function scope
            if (!wsUploadTransaction.finish(wsUploadFile, 0)) {
              wsServer->sendTXT(num, "ERROR:Failed to finish upload; previous file preserved");
              return;
            }
            wsLastCompleteName = wsUploadFileName;
            wsLastCompleteSize = 0;
            wsLastCompleteAt = millis();
            LOG_DBG("WS", "Zero-byte upload complete: %s", filePath.c_str());
            clearBookCache(filePath.c_str());
            wsServer->sendTXT(num, "DONE");
            wsLastProgressSent = 0;
            break;
          }

          wsUploadClientNum = num;
          wsUploadInProgress = true;
          wsServer->sendTXT(num, "READY");
        } else {
          wsServer->sendTXT(num, "ERROR:Invalid START format");
        }
      }
      break;
    }

    case WStype_BIN: {
      if (!wsUploadInProgress || !wsUploadFile || num != wsUploadClientNum) {
        wsServer->sendTXT(num, "ERROR:No upload in progress");
        return;
      }

      // Write binary data directly to file
      size_t remaining = wsUploadSize - wsUploadReceived;
      if (length > remaining) {
        abortWsUpload("WS");
        wsServer->sendTXT(num, "ERROR:Upload overflow");
        return;
      }
      esp_task_wdt_reset();
      size_t written = wsUploadFile.write(payload, length);
      esp_task_wdt_reset();

      if (written != length) {
        abortWsUpload("WS");
        wsServer->sendTXT(num, "ERROR:Write failed - disk full?");
        return;
      }

      wsUploadReceived += written;

      // Send progress update (every 64KB or at end)
      if (wsUploadReceived - wsLastProgressSent >= 65536 || wsUploadReceived >= wsUploadSize) {
        String progress = "PROGRESS:" + String(wsUploadReceived) + ":" + String(wsUploadSize);
        wsServer->sendTXT(num, progress);
        wsLastProgressSent = wsUploadReceived;
      }

      // Check if upload complete
      if (wsUploadReceived >= wsUploadSize) {
        // Explicit close() required: file-scope global persists beyond function scope
        const bool committed = wsUploadTransaction.finish(wsUploadFile, wsUploadSize);
        wsUploadInProgress = false;
        wsUploadClientNum = 255;
        if (!committed) {
          wsServer->sendTXT(num, "ERROR:Failed to finish upload; previous file preserved");
          return;
        }

        wsLastCompleteName = wsUploadFileName;
        wsLastCompleteSize = wsUploadSize;
        wsLastCompleteAt = millis();

        unsigned long elapsed = millis() - wsUploadStartTime;
        float kbps = (elapsed > 0) ? (wsUploadSize / 1024.0) / (elapsed / 1000.0) : 0;

        LOG_DBG("WS", "Upload complete: %s (%d bytes in %lu ms, %.1f KB/s)", wsUploadFileName.c_str(), wsUploadSize,
                elapsed, kbps);

        // Clear epub cache to prevent stale metadata issues when overwriting files
        String filePath = wsUploadPath;
        if (!filePath.endsWith("/")) filePath += "/";
        filePath += wsUploadFileName;
        clearBookCache(filePath.c_str());

        wsServer->sendTXT(num, "DONE");
        wsLastProgressSent = 0;
      }
      break;
    }

    default:
      break;
  }
}
