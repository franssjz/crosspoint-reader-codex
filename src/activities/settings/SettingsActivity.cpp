#include "SettingsActivity.h"

#include <Arduino.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <HalTiltSensor.h>
#include <Logging.h>
#include <Utf8.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "AchievementsStore.h"
#include "ButtonRemapActivity.h"
#include "ClearCacheActivity.h"
#include "ClockSyncActivity.h"
#include "CrossPointSettings.h"
#include "FontDownloadActivity.h"
#include "FontSelectionActivity.h"
#include "KOReaderSettingsActivity.h"
#include "KeyboardLayoutsActivity.h"
#include "LanguageSelectActivity.h"
#include "MappedInputManager.h"
#include "OpdsServerListActivity.h"
#include "OtaUpdateActivity.h"
#include "ReadingStatsImportActivity.h"
#include "ReadingStatsStore.h"
#include "SdCardFontGlobals.h"
#include "SdFirmwareUpdateActivity.h"
#include "SettingsList.h"
#include "ShortcutLocationActivity.h"
#include "ShortcutOrderActivity.h"
#include "ShortcutVisibilityActivity.h"
#include "StatusBarSettingsActivity.h"
#include "TimeZoneSelectActivity.h"
#include "activities/apps/AchievementsActivity.h"
#include "activities/apps/BookmarksAppActivity.h"
#include "activities/apps/FavoritesAppActivity.h"
#include "activities/apps/FlashcardsAppActivity.h"
#include "activities/apps/IfFoundActivity.h"
#include "activities/apps/ReadingHeatmapActivity.h"
#include "activities/apps/ReadingProfileActivity.h"
#include "activities/apps/ReadingStatsActivity.h"
#include "activities/apps/ScreenCleanActivity.h"
#include "activities/apps/SleepAppActivity.h"
#include "activities/apps/SyncDayActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/ShortcutRegistry.h"
#include "util/ShortcutUiMetadata.h"
#include "util/SleepImageUtils.h"
#include "util/TimeUtils.h"
#include "version.h"

const StrId SettingsActivity::categoryNames[categoryCount] = {
    StrId::STR_CAT_DISPLAY, StrId::STR_CAT_READER, StrId::STR_CAT_CONTROLS, StrId::STR_CAT_SYSTEM, StrId::STR_APPS};

namespace {
constexpr size_t SETTINGS_TAB_MAX_CHARS = 10;

const std::vector<SettingInfo>& getDeviceDisplaySettings() {
  static const std::vector<SettingInfo> settings = {
      SettingInfo::Enum(StrId::STR_SLEEP_SCREEN, &CrossPointSettings::sleepScreen,
                        {StrId::STR_DARK, StrId::STR_LIGHT, StrId::STR_CUSTOM, StrId::STR_COVER, StrId::STR_NONE_OPT,
                         StrId::STR_COVER_CUSTOM, StrId::STR_READING_DASHBOARD, StrId::STR_COVER_STATS,
                         StrId::STR_COVER_STATS_V2, StrId::STR_CUSTOM_STATS, StrId::STR_CUSTOM_STATS_V2}),
      SettingInfo::Enum(StrId::STR_SLEEP_COVER_MODE, &CrossPointSettings::sleepScreenCoverMode,
                        {StrId::STR_FIT, StrId::STR_CROP}),
      SettingInfo::Enum(StrId::STR_SLEEP_COVER_FILTER, &CrossPointSettings::sleepScreenCoverFilter,
                        {StrId::STR_NONE_OPT, StrId::STR_FILTER_CONTRAST, StrId::STR_INVERTED}),
      SettingInfo::Toggle(StrId::STR_CLEAN_SLEEP_REFRESH, &CrossPointSettings::cleanSleepRefresh),
      SettingInfo::Enum(StrId::STR_HIDE_BATTERY, &CrossPointSettings::hideBatteryPercentage,
                        {StrId::STR_NEVER, StrId::STR_IN_READER, StrId::STR_ALWAYS}),
      SettingInfo::Enum(
          StrId::STR_REFRESH_FREQ, &CrossPointSettings::refreshFrequency,
          {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10, StrId::STR_PAGES_15, StrId::STR_PAGES_30}),
      SettingInfo::Enum(StrId::STR_UI_THEME, &CrossPointSettings::uiTheme,
                        {StrId::STR_THEME_LYRA, StrId::STR_THEME_LYRA_CUSTOM, StrId::STR_THEME_LYRA_CAROUSEL}),
      SettingInfo::Enum(StrId::STR_HOME_BOOK_SOURCE, &CrossPointSettings::homeBookSource,
                        {StrId::STR_RECENTS, StrId::STR_FAVORITES}),
      SettingInfo::Toggle(StrId::STR_ANTI_GHOSTING_EXPERIMENTAL, &CrossPointSettings::antiGhostingExperimental),
      SettingInfo::Toggle(StrId::STR_DARK_MODE, &CrossPointSettings::darkMode),
      SettingInfo::Toggle(StrId::STR_SUNLIGHT_FADING_FIX, &CrossPointSettings::fadingFix),
  };
  return settings;
}

const std::vector<SettingInfo>& getDeviceReaderSettings() {
  static const std::vector<SettingInfo> settings = {
      SettingInfo::Enum(StrId::STR_FONT_FAMILY, &CrossPointSettings::fontFamily,
                        {StrId::STR_BOOKERLY, StrId::STR_NOTO_SANS}),
      SettingInfo::Action(StrId::STR_MANAGE_FONTS, SettingAction::DownloadFonts),
      SettingInfo::Enum(
          StrId::STR_FONT_SIZE, &CrossPointSettings::fontSize,
          {StrId::STR_X_SMALL, StrId::STR_SMALL, StrId::STR_MEDIUM, StrId::STR_LARGE, StrId::STR_X_LARGE}),
      SettingInfo::Enum(StrId::STR_LINE_SPACING, &CrossPointSettings::lineSpacing,
                        {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE, StrId::STR_EXTRA_WIDE}),
      SettingInfo::Value(StrId::STR_SCREEN_MARGIN, &CrossPointSettings::screenMargin, {5, 40, 5}),
      SettingInfo::Enum(StrId::STR_PARA_ALIGNMENT, &CrossPointSettings::paragraphAlignment,
                        {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                         StrId::STR_BOOK_S_STYLE}),
      SettingInfo::Toggle(StrId::STR_EMBEDDED_STYLE, &CrossPointSettings::embeddedStyle),
      SettingInfo::Toggle(StrId::STR_HYPHENATION, &CrossPointSettings::hyphenationEnabled),
      SettingInfo::Enum(StrId::STR_BIONIC_READING, &CrossPointSettings::bionicReading,
                        {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_SUBTLE}),
      SettingInfo::Enum(StrId::STR_ORIENTATION, &CrossPointSettings::orientation,
                        {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED, StrId::STR_LANDSCAPE_CCW}),
      SettingInfo::Toggle(StrId::STR_EXTRA_SPACING, &CrossPointSettings::extraParagraphSpacing),
      SettingInfo::Toggle(StrId::STR_FORCE_PARAGRAPH_INDENTS, &CrossPointSettings::forceParagraphIndents),
      SettingInfo::Toggle(StrId::STR_TEXT_AA, &CrossPointSettings::textAntiAliasing),
      SettingInfo::Enum(StrId::STR_TEXT_DARKNESS, &CrossPointSettings::textDarkness,
                        {StrId::STR_NORMAL, StrId::STR_LEGACY_BW, StrId::STR_DARK, StrId::STR_EXTRA_DARK}),
      SettingInfo::Enum(StrId::STR_READER_REFRESH_MODE, &CrossPointSettings::readerRefreshMode,
                        {StrId::STR_REFRESH_MODE_AUTO, StrId::STR_REFRESH_MODE_FAST, StrId::STR_REFRESH_MODE_HALF,
                         StrId::STR_REFRESH_MODE_FULL}),
      SettingInfo::Enum(StrId::STR_IMAGES, &CrossPointSettings::imageRendering,
                        {StrId::STR_IMAGES_DISPLAY, StrId::STR_IMAGES_PLACEHOLDER, StrId::STR_IMAGES_SUPPRESS}),
      SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar),
  };
  return settings;
}

const std::vector<SettingInfo>& getDeviceControlsSettings() {
  static const std::vector<SettingInfo> settings = [] {
    std::vector<SettingInfo> result = {
        SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons),
        SettingInfo::Action(StrId::STR_KEYBOARD_LAYOUTS, SettingAction::KeyboardLayouts),
        SettingInfo::Enum(StrId::STR_SIDE_BTN_LAYOUT, &CrossPointSettings::sideButtonLayout,
                          {StrId::STR_PREV_NEXT, StrId::STR_NEXT_PREV}),
        SettingInfo::Toggle(StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION, &CrossPointSettings::frontButtonFollowOrientation),
        SettingInfo::Enum(StrId::STR_LONG_PRESS_BEHAVIOR, &CrossPointSettings::longPressButtonBehavior,
                          {StrId::STR_LONG_PRESS_BEHAVIOR_OFF, StrId::STR_LONG_PRESS_BEHAVIOR_SKIP,
                           StrId::STR_LONG_PRESS_BEHAVIOR_ORIENTATION}),
        SettingInfo::Enum(StrId::STR_SHORT_PWR_BTN, &CrossPointSettings::shortPwrBtn,
                          {StrId::STR_IGNORE, StrId::STR_SLEEP, StrId::STR_PAGE_TURN, StrId::STR_FORCE_REFRESH,
                           StrId::STR_TOGGLE_STATUS_BAR, StrId::STR_QUICK_LOCK}),
    };
    if (halTiltSensor.isAvailable()) {
      result.push_back(SettingInfo::Enum(StrId::STR_TILT_PAGE_TURN, &CrossPointSettings::tiltPageTurn,
                                         {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_INVERTED}));
    }
    return result;
  }();
  return settings;
}

const std::vector<SettingInfo>& getDeviceSystemSettings() {
  static const std::vector<SettingInfo> settings = {
      SettingInfo::Enum(StrId::STR_TIME_TO_SLEEP, &CrossPointSettings::sleepTimeout,
                        {StrId::STR_MIN_1, StrId::STR_MIN_5, StrId::STR_MIN_10, StrId::STR_MIN_15, StrId::STR_MIN_30}),
      SettingInfo::Toggle(StrId::STR_SHOW_HIDDEN_FILES, &CrossPointSettings::showHiddenFiles),
      SettingInfo::Toggle(StrId::STR_HIDE_FILE_EXTENSION, &CrossPointSettings::hideFileExtension),
      SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network),
      SettingInfo::Action(StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync),
      SettingInfo::Enum(StrId::STR_OPDS_FILENAME_FORMAT, &CrossPointSettings::opdsFilenameFormat,
                        {StrId::STR_AUTHOR_TITLE, StrId::STR_TITLE_AUTHOR}),
      SettingInfo::Action(StrId::STR_OPDS_SERVERS, SettingAction::OPDSBrowser),
      SettingInfo::Action(StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache),
      SettingInfo::Action(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates),
      SettingInfo::Action(StrId::STR_SD_FIRMWARE_UPDATE, SettingAction::SdFirmwareUpdate),
      SettingInfo::Action(StrId::STR_LANGUAGE, SettingAction::Language),
  };
  return settings;
}

const std::vector<SettingInfo>& getDeviceOnlyControlSettings() {
  static const std::vector<SettingInfo> settings = {
      SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons),
  };
  return settings;
}

const std::vector<SettingInfo>& getDeviceOnlySystemSettings() {
  static const std::vector<SettingInfo> settings = {
      SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network),
      SettingInfo::Action(StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync),
      SettingInfo::Action(StrId::STR_OPDS_SERVERS, SettingAction::OPDSBrowser),
      SettingInfo::Action(StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache),
      SettingInfo::Action(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates),
      SettingInfo::Action(StrId::STR_SD_FIRMWARE_UPDATE, SettingAction::SdFirmwareUpdate),
      SettingInfo::Action(StrId::STR_LANGUAGE, SettingAction::Language),
  };
  return settings;
}

const std::vector<SettingInfo>& getDeviceOnlyAppSettings() {
  static const std::vector<SettingInfo> settings = {
      SettingInfo::Section(StrId::STR_SYNC_DAY),
      SettingInfo::Action(StrId::STR_SYNC_DAY, SettingAction::SyncDay),
      SettingInfo::Action(StrId::STR_TIME_ZONE, SettingAction::TimeZone),
      SettingInfo::Toggle(StrId::STR_DISPLAY_DAY, &CrossPointSettings::displayDay),
      SettingInfo::Enum(StrId::STR_CHOOSE_WIFI, &CrossPointSettings::syncDayWifiChoice,
                        {StrId::STR_REFRESH_MODE_AUTO, StrId::STR_MANUAL}),
      SettingInfo::Enum(StrId::STR_SYNC_DAY_REMINDER_EVERY, &CrossPointSettings::syncDayReminderStarts,
                        {StrId::STR_STATE_OFF, StrId::STR_NUM_10, StrId::STR_NUM_20, StrId::STR_NUM_30,
                         StrId::STR_NUM_40, StrId::STR_NUM_50, StrId::STR_NUM_60}),
      SettingInfo::Enum(
          StrId::STR_DATE_FORMAT, &CrossPointSettings::dateFormat,
          {StrId::STR_DATE_FORMAT_DD_MM_YYYY, StrId::STR_DATE_FORMAT_MM_DD_YYYY, StrId::STR_DATE_FORMAT_YYYY_MM_DD}),
      SettingInfo::Section(StrId::STR_READING_STATS),
      SettingInfo::Action(StrId::STR_READING_STATS, SettingAction::ReadingStats),
      SettingInfo::Enum(StrId::STR_DAILY_GOAL, &CrossPointSettings::dailyGoalTarget,
                        {StrId::STR_MIN_15, StrId::STR_MIN_30, StrId::STR_MIN_45, StrId::STR_MIN_60}),
      SettingInfo::Enum(
          StrId::STR_READING_STATS_AUTOBACKUP, &CrossPointSettings::readingStatsAutoBackup,
          {StrId::STR_STATE_OFF, StrId::STR_NUM_1, StrId::STR_NUM_7, StrId::STR_NUM_14, StrId::STR_NUM_21}),
      SettingInfo::Action(StrId::STR_CLEAR_READING_STATS_BACKUPS, SettingAction::ClearReadingStatsBackups),
      SettingInfo::Toggle(StrId::STR_SHOW_AFTER_READING, &CrossPointSettings::showStatsAfterReading),
      SettingInfo::Toggle(StrId::STR_MOVE_COMPLETED_BOOKS, &CrossPointSettings::moveCompletedBooks),
      SettingInfo::Action(StrId::STR_RESET_READING_STATS, SettingAction::ResetReadingStats),
      SettingInfo::Action(StrId::STR_EXPORT_READING_STATS, SettingAction::ExportReadingStats),
      SettingInfo::Action(StrId::STR_IMPORT_READING_STATS, SettingAction::ImportReadingStats),
      SettingInfo::Action(StrId::STR_READING_HEATMAP, SettingAction::ReadingHeatmap),
      SettingInfo::Action(StrId::STR_READING_PROFILE, SettingAction::ReadingProfile),
      SettingInfo::Section(StrId::STR_ACHIEVEMENTS),
      SettingInfo::Action(StrId::STR_ACHIEVEMENTS, SettingAction::Achievements),
      SettingInfo::Toggle(StrId::STR_ENABLE_ACHIEVEMENTS, &CrossPointSettings::achievementsEnabled),
      SettingInfo::Toggle(StrId::STR_ACHIEVEMENT_POPUPS, &CrossPointSettings::achievementPopups),
      SettingInfo::Action(StrId::STR_RESET_ACHIEVEMENTS, SettingAction::ResetAchievements),
      SettingInfo::Action(StrId::STR_SYNC_WITH_PREV_STATS, SettingAction::SyncAchievementsFromStats),
      SettingInfo::Section(StrId::STR_APPS),
      SettingInfo::Action(StrId::STR_HIGHLIGHTS, SettingAction::Bookmarks),
      SettingInfo::Action(StrId::STR_FAVORITES, SettingAction::Favorites),
      SettingInfo::Action(StrId::STR_SCREEN_CLEAN, SettingAction::ScreenClean),
      SettingInfo::Action(StrId::STR_SLEEP, SettingAction::SleepApp),
      SettingInfo::Action(StrId::STR_IF_FOUND_RETURN_ME, SettingAction::IfFound),
      SettingInfo::Section(StrId::STR_FLASHCARDS),
      SettingInfo::Action(StrId::STR_FLASHCARDS, SettingAction::Flashcards),
      SettingInfo::Enum(StrId::STR_STUDY_MODE, &CrossPointSettings::flashcardStudyMode,
                        {StrId::STR_DUE, StrId::STR_SCHEDULED, StrId::STR_RANDOM_PRACTICE, StrId::STR_SEQUENTIAL}),
      SettingInfo::Enum(StrId::STR_SESSION_SIZE, &CrossPointSettings::flashcardSessionSize,
                        {StrId::STR_NUM_10, StrId::STR_NUM_20, StrId::STR_NUM_30, StrId::STR_NUM_50, StrId::STR_ALL}),
      SettingInfo::Section(StrId::STR_SHORTCUTS_SECTION),
      SettingInfo::Action(StrId::STR_SHORTCUT_LOCATION, SettingAction::ShortcutLocation),
      SettingInfo::Action(StrId::STR_SHORTCUT_VISIBILITY, SettingAction::ShortcutVisibility),
      SettingInfo::Action(StrId::STR_ORDER_HOME_SHORTCUTS, SettingAction::OrderHomeShortcuts),
      SettingInfo::Action(StrId::STR_ORDER_APPS_SHORTCUTS, SettingAction::OrderAppsShortcuts),
  };
  return settings;
}

const std::vector<SettingInfo>& getDeviceOnlyReaderSettings() {
  static const std::vector<SettingInfo> settings = {
      SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar),
  };
  return settings;
}

std::string getReadingStatsExportPath() { return "/exports/stats_exported"; }

std::string fileNameFromPath(const std::string& path) {
  const size_t pos = path.find_last_of('/');
  if (pos == std::string::npos) {
    return path;
  }
  return path.substr(pos + 1);
}

size_t utf8CodepointCount(const std::string& text) {
  size_t count = 0;
  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(text.c_str());
  while (*ptr != '\0') {
    utf8NextCodepoint(&ptr);
    ++count;
  }
  return count;
}

std::string utf8LimitChars(std::string text, const size_t maxChars) {
  const size_t count = utf8CodepointCount(text);
  if (count <= maxChars) {
    return text;
  }
  utf8TruncateChars(text, count - maxChars);
  return text;
}

std::string getLatestReadingStatsImportPath() {
  const auto paths = ReadingStatsImportActivity::getImportPaths();
  return paths.empty() ? std::string() : paths.front();
}

std::string getReadingStatsExportFileName() { return fileNameFromPath(getReadingStatsExportPath()); }

std::string getLatestReadingStatsImportFileName() {
  const std::string path = getLatestReadingStatsImportPath();
  return path.empty() ? std::string() : fileNameFromPath(path);
}

std::string getNetworkSettingValueText() {
  const wifi_mode_t wifiMode = WiFi.getMode();
  const bool isApMode = (wifiMode & WIFI_MODE_AP) != 0;
  const bool isStaConnected = (wifiMode & WIFI_MODE_STA) != 0 && WiFi.status() == WL_CONNECTED;
  if (isApMode) {
    return "AP";
  }
  if (isStaConnected) {
    const String ssid = WiFi.SSID();
    return ssid.length() > 0 ? std::string(ssid.c_str()) : "WiFi";
  }
  return std::string(tr(STR_STATE_OFF));
}

std::string getShortcutLocationSettingValueText() {
  int homeCount = 1;  // Apps hub is always in Home.
  int appsCount = 0;
  for (const auto& definition : getShortcutDefinitions()) {
    const auto location = static_cast<CrossPointSettings::SHORTCUT_LOCATION>(SETTINGS.*(definition.locationPtr));
    if (location == CrossPointSettings::SHORTCUT_HOME) {
      ++homeCount;
    } else {
      ++appsCount;
    }
  }
  return "H" + std::to_string(homeCount) + " A" + std::to_string(appsCount);
}

std::string getShortcutVisibilitySettingValueText() {
  int visibleCount = 0;
  for (const auto& definition : getShortcutDefinitions()) {
    if (getShortcutVisibility(definition)) {
      ++visibleCount;
    }
  }
  return std::to_string(visibleCount) + "/" + std::to_string(getShortcutDefinitions().size());
}

std::string getShortcutOrderSettingValueText(const ShortcutOrderGroup group) {
  return std::to_string(getShortcutOrderEntries(group).size());
}

std::string getSettingValueText(const SettingInfo& setting) {
  if (setting.nameId == StrId::STR_FONT_FAMILY && SETTINGS.sdFontFamilyName[0] != '\0') {
    return SETTINGS.sdFontFamilyName;
  }
  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    const bool value = SETTINGS.*(setting.valuePtr);
    return value ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  }
  if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    if (setting.enumValues.empty()) {
      return "";
    }
    const uint8_t value = SETTINGS.*(setting.valuePtr);
    const size_t safeIndex = std::min<size_t>(value, setting.enumValues.size() - 1);
    return I18N.get(setting.enumValues[safeIndex]);
  }
  if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    return std::to_string(SETTINGS.*(setting.valuePtr));
  }
  if (setting.type == SettingType::ACTION && setting.action == SettingAction::TimeZone) {
    return TimeUtils::getCurrentTimeZoneLabel();
  }
  if (setting.type == SettingType::ACTION) {
    switch (setting.action) {
      case SettingAction::Network:
        return getNetworkSettingValueText();
      case SettingAction::CheckForUpdates:
        return CROSSPOINT_VERSION;
      case SettingAction::Language:
        return I18N.getLanguageName(I18N.getLanguage());
      case SettingAction::ReadingStats: {
        const auto* definition = findShortcutDefinition(ShortcutId::ReadingStats);
        return definition ? ShortcutUiMetadata::getSubtitle(*definition) : "";
      }
      case SettingAction::Achievements: {
        const auto* definition = findShortcutDefinition(ShortcutId::Achievements);
        return definition ? ShortcutUiMetadata::getSubtitle(*definition) : "";
      }
      case SettingAction::Flashcards: {
        const auto* definition = findShortcutDefinition(ShortcutId::Flashcards);
        return definition ? ShortcutUiMetadata::getSubtitle(*definition) : "";
      }
      case SettingAction::ScreenClean: {
        const auto* definition = findShortcutDefinition(ShortcutId::ScreenClean);
        return definition ? ShortcutUiMetadata::getSubtitle(*definition) : "";
      }
      case SettingAction::SleepApp: {
        const auto* definition = findShortcutDefinition(ShortcutId::Sleep);
        return definition ? ShortcutUiMetadata::getSubtitle(*definition) : "";
      }
      case SettingAction::ShortcutLocation:
        return getShortcutLocationSettingValueText();
      case SettingAction::ShortcutVisibility:
        return getShortcutVisibilitySettingValueText();
      case SettingAction::OrderHomeShortcuts:
        return getShortcutOrderSettingValueText(ShortcutOrderGroup::Home);
      case SettingAction::OrderAppsShortcuts:
        return getShortcutOrderSettingValueText(ShortcutOrderGroup::Apps);
      default:
        break;
    }
  }
  return "";
}

const char* getSettingNameText(const SettingInfo& setting) { return I18N.get(setting.nameId); }

void appendPrewarmText(std::string& text, const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return;
  }
  text += value;
  text += '\n';
}

void appendPrewarmText(std::string& text, const std::string& value) { appendPrewarmText(text, value.c_str()); }
}  // namespace

void SettingsActivity::onEnter() {
  Activity::onEnter();

  settingsListsBuilt = false;
  buildSettingsLists();

  // Reset selection to first category
  selectedCategoryIndex = 0;
  selectedSettingIndex = 0;
  enterCategory(0);

  // Trigger first update
  requestUpdate();
}

void SettingsActivity::buildSettingsLists() {
  if (settingsListsBuilt) {
    return;
  }

  // Device settings intentionally avoid the shared web/API settings list.
  // That shared list carries dynamic/web metadata and is the wrong dependency
  // for the on-device settings screen.
  const auto& deviceDisplay = getDeviceDisplaySettings();
  const auto& deviceReader = getDeviceReaderSettings();
  const auto& deviceControls = getDeviceControlsSettings();
  const auto& deviceSystem = getDeviceSystemSettings();
  displaySettings.clear();
  readerSettings.clear();
  controlsSettings.clear();
  systemSettings.clear();

  displaySettings.reserve(deviceDisplay.size());
  readerSettings.reserve(deviceReader.size());
  controlsSettings.reserve(deviceControls.size());
  systemSettings.reserve(deviceSystem.size());

  for (const auto& setting : deviceDisplay) {
    displaySettings.push_back(&setting);
  }
  for (const auto& setting : deviceReader) {
    readerSettings.push_back(&setting);
  }
  for (const auto& setting : deviceControls) {
    controlsSettings.push_back(&setting);
  }
  for (const auto& setting : deviceSystem) {
    systemSettings.push_back(&setting);
  }
  rebuildAppSettingsList();
  settingsListsBuilt = true;
}

void SettingsActivity::rebuildAppSettingsList() {
  const auto& deviceApps = getDeviceOnlyAppSettings();
  appSettings.clear();
  appSettingOverrides.clear();
  appSettings.reserve(deviceApps.size());

  const bool rtcClockActive = SETTINGS.isHardwareRtcAutoDayClockActive();
  if (rtcClockActive) {
    // Two overrides replace Sync Day + Display Day entries; reserve so stored pointers stay valid.
    appSettingOverrides.reserve(2);
  }
  for (const auto& setting : deviceApps) {
    if (setting.nameId == StrId::STR_SYNC_DAY_REMINDER_EVERY && rtcClockActive) {
      continue;
    }
    if (rtcClockActive && setting.nameId == StrId::STR_SYNC_DAY && setting.type == SettingType::ACTION) {
      appSettingOverrides.push_back(SettingInfo::Action(StrId::STR_CLOCK_SYNC_NOW, SettingAction::ClockSync));
      appSettings.push_back(&appSettingOverrides.back());
      continue;
    }
    if (rtcClockActive && setting.nameId == StrId::STR_DISPLAY_DAY && setting.type == SettingType::TOGGLE) {
      appSettingOverrides.push_back(SettingInfo::Enum(StrId::STR_DISPLAY_DAY_TIME, &CrossPointSettings::displayDay,
                                                      {StrId::STR_STATE_OFF, StrId::STR_DISPLAY_DATE_ONLY,
                                                       StrId::STR_DISPLAY_TIME_ONLY, StrId::STR_DISPLAY_DAY_AND_TIME}));
      appSettings.push_back(&appSettingOverrides.back());
      continue;
    }
    appSettings.push_back(&setting);
  }
}

void SettingsActivity::onExit() {
  Activity::onExit();

  UITheme::getInstance().reload();  // Re-apply theme in case it was changed
}

void SettingsActivity::enterCategory(const int categoryIndex) {
  selectedCategoryIndex = categoryIndex;
  switch (selectedCategoryIndex) {
    case 0:
      currentSettings = &displaySettings;
      break;
    case 1:
      currentSettings = &readerSettings;
      break;
    case 2:
      currentSettings = &controlsSettings;
      break;
    case 3:
      currentSettings = &systemSettings;
      break;
    default:
      rebuildAppSettingsList();
      currentSettings = &appSettings;
      break;
  }
  settingsCount = static_cast<int>(currentSettings->size());
}

bool SettingsActivity::isSelectableSetting(const int settingIndex) const {
  if (currentSettings == nullptr || settingIndex < 0 || settingIndex >= settingsCount) {
    return false;
  }
  return (*currentSettings)[settingIndex]->type != SettingType::SECTION;
}

int SettingsActivity::firstSelectableSettingIndex() const {
  for (int index = 0; index < settingsCount; ++index) {
    if (isSelectableSetting(index)) {
      return index + 1;
    }
  }
  return 0;
}

int SettingsActivity::stepSettingSelection(const int direction) const {
  const int totalSlots = settingsCount + 1;
  if (totalSlots <= 1) {
    return 0;
  }

  int candidate = selectedSettingIndex;
  for (int guard = 0; guard < totalSlots; ++guard) {
    candidate = direction > 0 ? ButtonNavigator::nextIndex(candidate, totalSlots)
                              : ButtonNavigator::previousIndex(candidate, totalSlots);
    if (candidate == 0 || isSelectableSetting(candidate - 1)) {
      return candidate;
    }
  }

  return selectedSettingIndex;
}

void SettingsActivity::showTransientPopup(const char* message, const int progress, const unsigned long delayMs) {
  requestUpdateAndWait();

  {
    RenderLock lock(*this);
    const Rect popupRect = GUI.drawPopup(renderer, message);
    if (progress >= 0) {
      GUI.fillPopupProgress(renderer, popupRect, progress);
    }
  }

  if (delayMs > 0) {
    delay(delayMs);
  }
}

void SettingsActivity::loop() {
  bool hasChangedCategory = false;

  // Handle actions with early return
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedSettingIndex == 0) {
      selectedCategoryIndex = (selectedCategoryIndex < categoryCount - 1) ? (selectedCategoryIndex + 1) : 0;
      hasChangedCategory = true;
      requestUpdate();
    } else {
      toggleCurrentSetting();
      requestUpdate();
      return;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (selectedSettingIndex > 0) {
      selectedSettingIndex = 0;
      requestUpdate();
    } else {
      SETTINGS.saveToFile();
      onGoHome();
    }
    return;
  }

  // Handle navigation
  buttonNavigator.onNextRelease([this] {
    selectedSettingIndex = stepSettingSelection(1);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    selectedSettingIndex = stepSettingSelection(-1);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, &hasChangedCategory] {
    hasChangedCategory = true;
    selectedCategoryIndex = ButtonNavigator::nextIndex(selectedCategoryIndex, categoryCount);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, &hasChangedCategory] {
    hasChangedCategory = true;
    selectedCategoryIndex = ButtonNavigator::previousIndex(selectedCategoryIndex, categoryCount);
    requestUpdate();
  });

  if (hasChangedCategory) {
    selectedSettingIndex = (selectedSettingIndex == 0) ? 0 : firstSelectableSettingIndex();
    enterCategory(selectedCategoryIndex);
  }
}

void SettingsActivity::toggleCurrentSetting() {
  int selectedSetting = selectedSettingIndex - 1;
  if (selectedSetting < 0 || selectedSetting >= settingsCount) {
    return;
  }

  const auto& setting = *(*currentSettings)[selectedSetting];
  const uint8_t previousReadingStatsAutoBackup = SETTINGS.readingStatsAutoBackup;

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    // Toggle the boolean value using the member pointer
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !currentValue;
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    if (setting.nameId == StrId::STR_FONT_FAMILY) {
      sdFontSystem.refreshIfDirty();
      startActivityForResult(std::make_unique<FontSelectionActivity>(renderer, mappedInput, &sdFontSystem.registry()),
                             [this](const ActivityResult&) {
                               ensureSdFontLoaded();
                               SETTINGS.saveToFile();
                               requestUpdate(true);
                             });
      return;
    }
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = (currentValue + 1) % static_cast<uint8_t>(setting.enumValues.size());
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    const int8_t currentValue = SETTINGS.*(setting.valuePtr);
    if (currentValue + setting.valueRange.step > setting.valueRange.max) {
      SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
    } else {
      SETTINGS.*(setting.valuePtr) = currentValue + setting.valueRange.step;
    }
  } else if (setting.type == SettingType::ACTION) {
    auto resultHandler = [this](const ActivityResult&) { SETTINGS.saveToFile(); };

    switch (setting.action) {
      case SettingAction::KeyboardLayouts:
        startActivityForResult(std::make_unique<KeyboardLayoutsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::RemapFrontButtons:
        startActivityForResult(std::make_unique<ButtonRemapActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CustomiseStatusBar:
        startActivityForResult(std::make_unique<StatusBarSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::KOReaderSync:
        startActivityForResult(std::make_unique<KOReaderSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::OPDSBrowser:
        startActivityForResult(std::make_unique<OpdsServerListActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Network:
        startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, false), resultHandler);
        break;
      case SettingAction::ClearCache:
        startActivityForResult(std::make_unique<ClearCacheActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CheckForUpdates:
        startActivityForResult(std::make_unique<OtaUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::SdFirmwareUpdate:
        startActivityForResult(std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Language:
        startActivityForResult(std::make_unique<LanguageSelectActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::DownloadFonts:
        startActivityForResult(std::make_unique<FontDownloadActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) {
                                 ensureSdFontLoaded();
                                 SETTINGS.saveToFile();
                                 requestUpdate(true);
                               });
        break;
      case SettingAction::SyncDay:
        startActivityForResult(std::make_unique<SyncDayActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::ClockSync:
        startActivityForResult(std::make_unique<ClockSyncActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::TimeZone:
        startActivityForResult(std::make_unique<TimeZoneSelectActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::ReadingStats:
        startActivityForResult(std::make_unique<ReadingStatsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::ResetReadingStats:
        startActivityForResult(
            std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_RESET_READING_STATS_CONFIRM), ""),
            [this](const ActivityResult& result) {
              if (!result.isCancelled) {
                READING_STATS.reset();
              }
              requestUpdate(true);
            });
        break;
      case SettingAction::ExportReadingStats: {
        showTransientPopup(tr(STR_EXPORTING), 20, 120);
        Storage.mkdir("/exports");
        const std::string exportPath = getReadingStatsExportPath();
        if (Storage.exists(exportPath.c_str())) {
          Storage.remove(exportPath.c_str());
        }
        const bool exported = READING_STATS.exportToFile(exportPath);
        showTransientPopup(exported ? tr(STR_EXPORT_DONE) : tr(STR_EXPORT_FAILED), exported ? 100 : -1,
                           exported ? 350 : 700);
        requestUpdate(true);
        break;
      }
      case SettingAction::ImportReadingStats:
        if (ReadingStatsImportActivity::getImportPaths().empty()) {
          showTransientPopup(tr(STR_NO_READING_STATS_EXPORT), -1, 700);
          requestUpdate(true);
          break;
        }
        startActivityForResult(std::make_unique<ReadingStatsImportActivity>(renderer, mappedInput),
                               [this](const ActivityResult& result) {
                                 if (!result.isCancelled) {
                                   const auto* path = std::get_if<FilePathResult>(&result.data);
                                   if (path == nullptr || path->path.empty()) {
                                     showTransientPopup(tr(STR_IMPORT_FAILED), -1, 700);
                                   } else {
                                     showTransientPopup(tr(STR_IMPORTING), 20, 120);
                                     const bool imported = READING_STATS.importFromFile(path->path);
                                     if (imported) {
                                       ACHIEVEMENTS.rebuildProgressFromCurrentStats();
                                     }
                                     showTransientPopup(imported ? tr(STR_IMPORT_DONE) : tr(STR_IMPORT_FAILED),
                                                        imported ? 100 : -1, imported ? 350 : 700);
                                   }
                                 }
                                 requestUpdate(true);
                               });
        break;
      case SettingAction::ClearReadingStatsBackups:
        startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput,
                                                                      tr(STR_CLEAR_READING_STATS_BACKUPS_CONFIRM), ""),
                               [this](const ActivityResult& result) {
                                 if (!result.isCancelled) {
                                   showTransientPopup(tr(STR_CLEARING_READING_STATS_BACKUPS), 20, 120);
                                   const int removedCount = READING_STATS.clearAutoBackups();
                                   showTransientPopup(removedCount > 0 ? tr(STR_READING_STATS_BACKUPS_CLEARED)
                                                                       : tr(STR_NO_READING_STATS_BACKUPS),
                                                      removedCount > 0 ? 100 : -1, removedCount > 0 ? 350 : 700);
                                 }
                                 requestUpdate(true);
                               });
        break;
      case SettingAction::ReadingHeatmap:
        startActivityForResult(std::make_unique<ReadingHeatmapActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::ReadingProfile:
        startActivityForResult(std::make_unique<ReadingProfileActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Achievements:
        startActivityForResult(std::make_unique<AchievementsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::ShortcutLocation:
        startActivityForResult(std::make_unique<ShortcutLocationActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::ShortcutVisibility:
        startActivityForResult(std::make_unique<ShortcutVisibilityActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::OrderHomeShortcuts:
        startActivityForResult(std::make_unique<ShortcutOrderActivity>(renderer, mappedInput, ShortcutOrderGroup::Home),
                               resultHandler);
        break;
      case SettingAction::OrderAppsShortcuts:
        startActivityForResult(std::make_unique<ShortcutOrderActivity>(renderer, mappedInput, ShortcutOrderGroup::Apps),
                               resultHandler);
        break;
      case SettingAction::ResetAchievements:
        startActivityForResult(
            std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_RESET_ACHIEVEMENTS_CONFIRM), ""),
            [this](const ActivityResult& result) {
              if (!result.isCancelled) {
                ACHIEVEMENTS.reset();
              }
              requestUpdate(true);
            });
        break;
      case SettingAction::SyncAchievementsFromStats:
        showTransientPopup(tr(STR_SYNC_WITH_PREV_STATS), 20, 120);
        ACHIEVEMENTS.syncWithPreviousStats();
        showTransientPopup(tr(STR_DONE), 100, 350);
        requestUpdate(true);
        break;
      case SettingAction::Bookmarks:
        startActivityForResult(std::make_unique<BookmarksAppActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Favorites:
        startActivityForResult(std::make_unique<FavoritesAppActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Flashcards:
        startActivityForResult(std::make_unique<FlashcardsAppActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::ScreenClean:
        startActivityForResult(std::make_unique<ScreenCleanActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::SleepApp:
        startActivityForResult(std::make_unique<SleepAppActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::IfFound:
        startActivityForResult(std::make_unique<IfFoundActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::None:
        // Do nothing
        break;
    }
    return;  // Results will be handled in the result handler, so we can return early here
  } else if (setting.type == SettingType::SECTION) {
    return;
  } else {
    return;
  }

  if (setting.valuePtr == &CrossPointSettings::dailyGoalTarget) {
    ACHIEVEMENTS.syncWithPreviousStats();
  }

  if (setting.valuePtr == &CrossPointSettings::fontSize) {
    ensureSdFontLoaded();
  }

  if (setting.valuePtr == &CrossPointSettings::darkMode) {
    renderer.setDarkMode(SETTINGS.darkMode);
    renderer.requestNextFullRefresh();
    requestUpdate(true);
  }

  const bool createInitialReadingStatsBackup = setting.valuePtr == &CrossPointSettings::readingStatsAutoBackup &&
                                               SETTINGS.readingStatsAutoBackup != previousReadingStatsAutoBackup &&
                                               SETTINGS.getReadingStatsAutoBackupIntervalDays() > 0 &&
                                               !READING_STATS.hasAutoBackups();

  SETTINGS.saveToFile();
  if (createInitialReadingStatsBackup) {
    showTransientPopup(tr(STR_READING_STATS_BACKUP_RUNNING), 20, 120);
    const bool backupReady = READING_STATS.ensureAutoBackupForEnabledSetting();
    showTransientPopup(backupReady ? tr(STR_READING_STATS_BACKUP_DONE) : tr(STR_READING_STATS_BACKUP_PENDING),
                       backupReady ? 100 : -1, backupReady ? 350 : 700);
    requestUpdate(true);
  }
}

void SettingsActivity::renderAppSettingsList(const Rect& rect) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& settings = *currentSettings;
  if (settings.empty() || rect.height <= 0) {
    return;
  }

  const int rowHeight = metrics.listRowHeight;
  const int sectionHeight = 40;
  const int sidePadding = metrics.contentSidePadding;
  constexpr int scrollBarWidth = 4;
  constexpr int scrollBarGap = 6;
  const int rowX = rect.x + sidePadding;
  const int rowWidth = rect.width - sidePadding * 2 - scrollBarWidth - scrollBarGap;
  const int viewportHeight = rect.height;

  auto getItemHeight = [rowHeight, sectionHeight](const SettingInfo* setting) {
    return setting->type == SettingType::SECTION ? sectionHeight : rowHeight;
  };

  std::vector<int> itemOffsets(settingsCount, 0);
  int totalHeight = 0;
  for (int index = 0; index < settingsCount; ++index) {
    itemOffsets[index] = totalHeight;
    totalHeight += getItemHeight(settings[index]);
  }

  int firstVisibleIndex = 0;
  int visibleWindowHeight = 0;
  if (selectedSettingIndex > 0) {
    const int selectedIndex = std::clamp(selectedSettingIndex - 1, 0, settingsCount - 1);
    for (int index = 0; index <= selectedIndex; ++index) {
      visibleWindowHeight += getItemHeight(settings[index]);
      while (visibleWindowHeight > viewportHeight && firstVisibleIndex <= index) {
        visibleWindowHeight -= getItemHeight(settings[firstVisibleIndex]);
        ++firstVisibleIndex;
      }
    }

    if (firstVisibleIndex > 0 && settings[firstVisibleIndex - 1]->type == SettingType::SECTION) {
      const int headerHeight = getItemHeight(settings[firstVisibleIndex - 1]);
      if (visibleWindowHeight + headerHeight <= viewportHeight) {
        --firstVisibleIndex;
        visibleWindowHeight += headerHeight;
      }
    }
  }

  int currentY = rect.y;
  int renderedHeight = 0;
  for (int index = firstVisibleIndex; index < settingsCount; ++index) {
    const auto& setting = settings[index];
    const int itemHeight = getItemHeight(setting);
    if (renderedHeight + itemHeight > viewportHeight) {
      break;
    }

    if (setting->type == SettingType::SECTION) {
      renderer.drawText(UI_10_FONT_ID, rowX, currentY + 4, getSettingNameText(*setting), true, EpdFontFamily::BOLD);
      renderer.drawLine(rowX, currentY + itemHeight - 5, rowX + rowWidth, currentY + itemHeight - 5, true);
      currentY += itemHeight;
      renderedHeight += itemHeight;
      continue;
    }

    const bool selected = selectedSettingIndex == index + 1;
    const Rect rowRect{rowX, currentY, rowWidth, itemHeight - 4};
    if (selected) {
      renderer.fillRectDither(rowRect.x, rowRect.y, rowRect.width, rowRect.height, Color::LightGray);
      renderer.drawRect(rowRect.x, rowRect.y, rowRect.width, rowRect.height);
    }

    const std::string valueText = getSettingValueText(*setting);
    const bool showExportFileName =
        setting->type == SettingType::ACTION && setting->action == SettingAction::ExportReadingStats;
    const bool showImportFileName =
        setting->type == SettingType::ACTION && setting->action == SettingAction::ImportReadingStats;
    const std::string sideNote = showExportFileName
                                     ? getReadingStatsExportFileName()
                                     : (showImportFileName ? getLatestReadingStatsImportFileName() : std::string());
    const int valueWidth =
        valueText.empty() ? 0 : renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str(), EpdFontFamily::REGULAR);
    const int leftPadding = 12;
    const int rightPadding = 12;
    if (showExportFileName || showImportFileName) {
      const int sideNoteMaxWidth = rowRect.width / 2 - leftPadding - rightPadding;
      const std::string truncatedSideNote =
          sideNote.empty()
              ? std::string()
              : renderer.truncatedText(SMALL_FONT_ID, sideNote.c_str(), sideNoteMaxWidth, EpdFontFamily::REGULAR);
      const int sideNoteWidth =
          truncatedSideNote.empty()
              ? 0
              : renderer.getTextWidth(SMALL_FONT_ID, truncatedSideNote.c_str(), EpdFontFamily::REGULAR);

      const int labelWidth = rowRect.width - leftPadding - rightPadding - (sideNoteWidth > 0 ? sideNoteWidth + 12 : 0);
      const std::string titleText =
          renderer.truncatedText(UI_10_FONT_ID, getSettingNameText(*setting), labelWidth, EpdFontFamily::REGULAR);
      renderer.drawText(UI_10_FONT_ID, rowRect.x + leftPadding, rowRect.y + 9, titleText.c_str(), true,
                        EpdFontFamily::REGULAR);
      if (!truncatedSideNote.empty()) {
        renderer.drawText(SMALL_FONT_ID, rowRect.x + rowRect.width - rightPadding - sideNoteWidth, rowRect.y + 11,
                          truncatedSideNote.c_str(), true, EpdFontFamily::REGULAR);
      }
    } else {
      const int labelWidth = rowRect.width - leftPadding - rightPadding - (valueWidth > 0 ? valueWidth + 12 : 0);
      const std::string titleText =
          renderer.truncatedText(UI_10_FONT_ID, getSettingNameText(*setting), labelWidth, EpdFontFamily::REGULAR);

      renderer.drawText(UI_10_FONT_ID, rowRect.x + leftPadding, rowRect.y + 9, titleText.c_str(), true,
                        EpdFontFamily::REGULAR);
      if (!valueText.empty()) {
        renderer.drawText(UI_10_FONT_ID, rowRect.x + rowRect.width - rightPadding - valueWidth, rowRect.y + 9,
                          valueText.c_str(), true, EpdFontFamily::REGULAR);
      }
    }

    currentY += itemHeight;
    renderedHeight += itemHeight;
  }

  if (totalHeight > viewportHeight) {
    const int scrollTrackX = rect.x + rect.width - sidePadding;
    const int scrollOffset = itemOffsets[firstVisibleIndex];
    const int scrollBarHeight = std::max(18, (viewportHeight * viewportHeight) / totalHeight);
    const int maxScrollOffset = std::max(1, totalHeight - viewportHeight);
    const int scrollBarY =
        rect.y + ((viewportHeight - scrollBarHeight) * std::min(scrollOffset, maxScrollOffset)) / maxScrollOffset;

    renderer.drawLine(scrollTrackX, rect.y, scrollTrackX, rect.y + viewportHeight, true);
    renderer.fillRect(scrollTrackX - scrollBarWidth + 1, scrollBarY, scrollBarWidth, scrollBarHeight, true);
  }
}

bool SettingsActivity::prewarmSettingsRenderText(const char* settingsTitle, const char* selectedCategoryLabel,
                                                 const char* firmwareVersion, const char* confirmLabel) const {
  auto* fontCache = renderer.getFontCacheManager();
  if (fontCache == nullptr || currentSettings == nullptr) {
    return false;
  }

  std::string text;
  text.reserve(2048);
  appendPrewarmText(text, settingsTitle);
  appendPrewarmText(text, selectedCategoryLabel);
  appendPrewarmText(text, firmwareVersion);
  appendPrewarmText(text, confirmLabel);
  appendPrewarmText(text, tr(STR_BACK));
  appendPrewarmText(text, tr(STR_DIR_UP));
  appendPrewarmText(text, tr(STR_DIR_DOWN));

  for (int i = 0; i < categoryCount; ++i) {
    appendPrewarmText(text, I18N.get(categoryNames[i]));
  }

  for (const auto* setting : *currentSettings) {
    appendPrewarmText(text, getSettingNameText(*setting));
    appendPrewarmText(text, getSettingValueText(*setting));
  }

  if (text.empty()) {
    return false;
  }

  constexpr uint8_t regularAndBold =
      (1 << static_cast<uint8_t>(EpdFontFamily::REGULAR)) | (1 << static_cast<uint8_t>(EpdFontFamily::BOLD));
  fontCache->clearCache();
  fontCache->prewarmCache(UI_10_FONT_ID, text.c_str(), regularAndBold);
  fontCache->prewarmCache(UI_12_FONT_ID, text.c_str(), regularAndBold);
  return true;
}

void SettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const char* settingsTitle = tr(STR_SETTINGS_TITLE);
  const char* selectedCategoryLabel = I18N.get(categoryNames[selectedCategoryIndex]);
  const char* firmwareVersion = CROSSPOINT_VERSION;
  const char* confirmLabel = nullptr;
  if (selectedSettingIndex == 0) {
    confirmLabel = I18N.get(categoryNames[(selectedCategoryIndex + 1) % categoryCount]);
  } else {
    const auto& selectedSetting = *(*currentSettings)[selectedSettingIndex - 1];
    confirmLabel = (selectedSetting.type == SettingType::ACTION || selectedSetting.type == SettingType::SECTION)
                       ? tr(STR_SELECT)
                       : tr(STR_TOGGLE);
  }
  const bool prewarmedFonts =
      prewarmSettingsRenderText(settingsTitle, selectedCategoryLabel, firmwareVersion, confirmLabel);

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, settingsTitle, nullptr);
  HeaderDateUtils::drawTopLine(renderer, HeaderDateUtils::getDisplayDateText());

  const int titleX = metrics.contentSidePadding;
  const int titleY = metrics.topPadding + metrics.batteryBarHeight + 3;
  const int titleWidth = renderer.getTextWidth(UI_12_FONT_ID, settingsTitle, EpdFontFamily::BOLD);
  const int categoryGap = 10;
  const int categoryX = titleX + titleWidth + categoryGap;
  const int versionWidth = renderer.getTextWidth(SMALL_FONT_ID, firmwareVersion, EpdFontFamily::REGULAR);
  const int versionX = pageWidth - metrics.contentSidePadding - versionWidth;
  const int versionGap = 12;
  const int categoryMaxWidth = std::max(0, versionX - categoryX - versionGap);
  if (categoryMaxWidth > 24) {
    const std::string headerCategory =
        renderer.truncatedText(SMALL_FONT_ID, selectedCategoryLabel, categoryMaxWidth, EpdFontFamily::REGULAR);
    if (!headerCategory.empty()) {
      const std::string categoryPrefix = "/ ";
      renderer.drawText(SMALL_FONT_ID, categoryX, titleY + 4, categoryPrefix.c_str(), true, EpdFontFamily::REGULAR);
      renderer.drawText(
          SMALL_FONT_ID,
          categoryX + renderer.getTextWidth(SMALL_FONT_ID, categoryPrefix.c_str(), EpdFontFamily::REGULAR), titleY + 4,
          headerCategory.c_str(), true, EpdFontFamily::REGULAR);
    }
  }
  renderer.drawText(SMALL_FONT_ID, versionX, titleY + 4, firmwareVersion, true, EpdFontFamily::REGULAR);

  std::vector<std::string> tabLabels;
  tabLabels.reserve(categoryCount);
  std::vector<TabInfo> tabs;
  tabs.reserve(categoryCount);
  for (int i = 0; i < categoryCount; i++) {
    const char* fullLabel = I18N.get(categoryNames[i]);
    tabLabels.push_back(
        utf8LimitChars(fullLabel != nullptr ? std::string(fullLabel) : std::string(), SETTINGS_TAB_MAX_CHARS));
    const bool compact =
        utf8CodepointCount(fullLabel != nullptr ? std::string(fullLabel) : std::string()) > SETTINGS_TAB_MAX_CHARS;
    tabs.push_back({tabLabels.back().c_str(), selectedCategoryIndex == i, compact});
  }
  GUI.drawTabBar(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight}, tabs,
                 selectedSettingIndex == 0);

  constexpr int listBottomGap = 10;
  const Rect listRect{0, metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing,
                      pageWidth,
                      pageHeight - (metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight +
                                    metrics.buttonHintsHeight + metrics.verticalSpacing * 2 + listBottomGap)};
  const auto& settings = *currentSettings;
  if (selectedCategoryIndex == 4) {
    renderAppSettingsList(listRect);
  } else {
    GUI.drawList(
        renderer, listRect, settingsCount, selectedSettingIndex - 1,
        [&settings](int index) { return std::string(getSettingNameText(*settings[index])); }, nullptr, nullptr,
        [&settings](int i) { return getSettingValueText(*settings[i]); }, true);
  }

  // Draw help text
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Always use standard refresh for settings screen
  renderer.displayBuffer();
  if (prewarmedFonts) {
    renderer.getFontCacheManager()->clearCache();
  }
}
