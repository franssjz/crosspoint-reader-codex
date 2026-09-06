#include "SettingsActivity.h"

#include <Arduino.h>
#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalFrontlight.h>
#include <HalStorage.h>
#include <HalTiltSensor.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>

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
#include "ReaderFontSizes.h"
#include "ReadingStatsImportActivity.h"
#include "ReadingStatsStore.h"
#include "SdCardFontGlobals.h"
#include "SdFirmwareUpdateActivity.h"
#include "ShortcutLocationActivity.h"
#include "ShortcutOrderActivity.h"
#include "ShortcutVisibilityActivity.h"
#include "StatusBarSettingsActivity.h"
#include "TextSettingsActivity.h"
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
#include "activities/util/IntervalSelectionActivity.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/ShortcutRegistry.h"
#include "util/ShortcutUiMetadata.h"
#include "util/TimeUtils.h"
#include "version.h"

namespace fui = freeink::ui;

namespace {
// ---------------------------------------------------------------------------
// On-device setting lists.
//
// Device settings intentionally avoid the shared web/API settings list
// (SettingsList.h): that list carries dynamic/web metadata and is the wrong
// dependency for the on-device screen. Board-capability filters (touch / home
// key / frontlight / tilt) are applied here, matching upstream's behaviour.
// ---------------------------------------------------------------------------

std::vector<StrId> buildSleepScreenValues() {
  // Enum settings are persisted as numeric values. Assign these labels by enum
  // value so a reordered menu or enum cannot silently swap their behavior.
  std::vector<StrId> values(CrossPointSettings::SLEEP_SCREEN_MODE_COUNT, StrId::STR_NONE_OPT);
  values[CrossPointSettings::DARK] = StrId::STR_DARK;
  values[CrossPointSettings::LIGHT] = StrId::STR_LIGHT;
  values[CrossPointSettings::CUSTOM] = StrId::STR_CUSTOM;
  values[CrossPointSettings::COVER] = StrId::STR_COVER;
  values[CrossPointSettings::BLANK] = StrId::STR_NONE_OPT;
  values[CrossPointSettings::COVER_CUSTOM] = StrId::STR_COVER_CUSTOM;
  values[CrossPointSettings::READING_DASHBOARD] = StrId::STR_READING_DASHBOARD;
  values[CrossPointSettings::COVER_STATS] = StrId::STR_COVER_STATS;
  values[CrossPointSettings::COVER_STATS_V2] = StrId::STR_COVER_STATS_V2;
  values[CrossPointSettings::CUSTOM_STATS] = StrId::STR_CUSTOM_STATS;
  values[CrossPointSettings::CUSTOM_STATS_V2] = StrId::STR_CUSTOM_STATS_V2;
  values[CrossPointSettings::QUICK_RESUME] = StrId::STR_QUICK_RESUME;
  values[CrossPointSettings::TRANSPARENT_CUSTOM] = StrId::STR_TRANSPARENT;
  return values;
}

std::vector<StrId> buildUiThemeValues() {
  std::vector<StrId> values(CrossPointSettings::UI_THEME_COUNT, StrId::STR_THEME_LYRA);
  values[CrossPointSettings::LYRA] = StrId::STR_THEME_LYRA;
  values[CrossPointSettings::LYRA_CUSTOM] = StrId::STR_THEME_LYRA_CUSTOM;
  values[CrossPointSettings::LYRA_CAROUSEL] = StrId::STR_THEME_LYRA_CAROUSEL;
  values[CrossPointSettings::CLASSIC] = StrId::STR_THEME_CLASSIC;
  values[CrossPointSettings::ROUNDEDRAFF] = StrId::STR_THEME_ROUNDEDRAFF;
  values[CrossPointSettings::LYRA_3_COVERS] = StrId::STR_THEME_LYRA_EXTENDED;
  return values;
}

std::vector<StrId> buildShortPwrBtnValues() {
  std::vector<StrId> values(CrossPointSettings::SHORT_PWRBTN_COUNT, StrId::STR_IGNORE);
  values[CrossPointSettings::IGNORE] = StrId::STR_IGNORE;
  values[CrossPointSettings::SLEEP] = StrId::STR_SLEEP;
  values[CrossPointSettings::PAGE_TURN] = StrId::STR_PAGE_TURN;
  values[CrossPointSettings::FORCE_REFRESH] = StrId::STR_FORCE_REFRESH;
  values[CrossPointSettings::TOGGLE_STATUS_BAR] = StrId::STR_TOGGLE_STATUS_BAR;
  values[CrossPointSettings::FOOTNOTES] = StrId::STR_FOOTNOTES;
  values[CrossPointSettings::PWR_CONFIRM] = StrId::STR_CONFIRM;
  // "Power = Confirm" only makes sense on touch boards (upstream gates it the
  // same way); it is the last enum value, so trimming keeps the indices stable.
  if (!BoardConfig::hasTouch()) values.resize(CrossPointSettings::PWR_CONFIRM);
  return values;
}

std::vector<StrId> buildLongPressMenuValues() {
  static constexpr StrId VALUES[] = {StrId::STR_KOSYNC, StrId::STR_DISABLED, StrId::STR_BOOKMARK_OPTION,
                                     StrId::STR_DICTIONARY, StrId::STR_READER_MENU};
  // The Reader Menu option is only offered on boards with a Home key.
  const size_t count = BoardConfig::hasHomeKey() ? std::size(VALUES) : std::size(VALUES) - 1;
  return {VALUES, VALUES + count};
}

// Reader font size: the options are the point sizes the active family actually
// ships (upstream's fontPointSize model, see ReaderFontSizes.h), so this row is
// rebuilt whenever the lists are rebuilt (family changes included).
SettingInfo buildReaderFontSizeSetting(const SdCardFontRegistry* registry) {
  const std::vector<uint8_t> sizes = readerFontPointSizes(registry, SETTINGS.sdFontFamilyName);

  // "pt" is deliberately not translated (matches TextSettingsActivity).
  std::vector<std::string> labels;
  labels.reserve(sizes.size());
  for (const uint8_t pt : sizes) {
    labels.push_back(std::to_string(pt) + " pt");
  }

  SettingInfo s;
  s.nameId = StrId::STR_FONT_SIZE;
  s.type = SettingType::ENUM;
  s.enumStringValues = std::move(labels);
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

std::vector<SettingInfo> buildDisplaySettings() {
  std::vector<SettingInfo> v = {
      SettingInfo::Enum(StrId::STR_SLEEP_SCREEN, &CrossPointSettings::sleepScreen, buildSleepScreenValues()),
      SettingInfo::Enum(StrId::STR_SLEEP_COVER_MODE, &CrossPointSettings::sleepScreenCoverMode,
                        {StrId::STR_FIT, StrId::STR_CROP}),
      SettingInfo::Enum(StrId::STR_SLEEP_COVER_FILTER, &CrossPointSettings::sleepScreenCoverFilter,
                        {StrId::STR_NONE_OPT, StrId::STR_FILTER_CONTRAST, StrId::STR_INVERTED}),
      SettingInfo::Toggle(StrId::STR_CLEAN_SLEEP_REFRESH, &CrossPointSettings::cleanSleepRefresh),
      SettingInfo::Enum(StrId::STR_QUICK_RESUME_TIMEOUT, &CrossPointSettings::quickResumeSleepScreen,
                        {StrId::STR_STATE_OFF, StrId::STR_STATE_ON}),
      SettingInfo::Enum(StrId::STR_HIDE_BATTERY, &CrossPointSettings::hideBatteryPercentage,
                        {StrId::STR_NEVER, StrId::STR_IN_READER, StrId::STR_ALWAYS}),
      SettingInfo::Enum(StrId::STR_REFRESH_FREQ, &CrossPointSettings::refreshFrequency,
                        {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10, StrId::STR_PAGES_15,
                         StrId::STR_PAGES_30, StrId::STR_NEVER}),
      SettingInfo::Enum(StrId::STR_UI_THEME, &CrossPointSettings::uiTheme, buildUiThemeValues()),
      SettingInfo::Enum(StrId::STR_HOME_BOOK_SOURCE, &CrossPointSettings::homeBookSource,
                        {StrId::STR_RECENTS, StrId::STR_FAVORITES}),
      SettingInfo::Toggle(StrId::STR_ANTI_GHOSTING_EXPERIMENTAL, &CrossPointSettings::antiGhostingExperimental),
      // Dark mode = inverted output polarity everywhere (upstream's "night
      // mode"; screenInverted aliases this field).
      SettingInfo::Toggle(StrId::STR_DARK_MODE, &CrossPointSettings::darkMode),
  };
  // The sunlight fading fix is a grayscale-waveform compensation that does not
  // apply on touch boards / the X4 Pro / X4 Classic (plain OTP waveform).
  if (!BoardConfig::hasTouch() && !BoardConfig::isX4Pro() && !BoardConfig::isX4Classic()) {
    v.push_back(SettingInfo::Toggle(StrId::STR_SUNLIGHT_FADING_FIX, &CrossPointSettings::fadingFix));
  }
#if FREEINK_CAP_FRONTLIGHT
  if (Frontlight.present()) {
    v.push_back(SettingInfo::Toggle(StrId::STR_RESTORE_LIGHT_ON_WAKE, &CrossPointSettings::frontlightRestoreOnWake));
  }
#endif
  return v;
}

std::vector<SettingInfo> buildReaderSettings(const SdCardFontRegistry* registry) {
  std::vector<SettingInfo> v = {
      // Upstream's tabbed Font / Size / Layout / Style screen with live preview.
      SettingInfo::Action(StrId::STR_TEXT_SETTINGS, SettingAction::TextSettings),
      SettingInfo::Action(StrId::STR_MANAGE_FONTS, SettingAction::DownloadFonts),
      // Font family opens the fork's picker (built-in + SD families), see runAction.
      SettingInfo::Enum(StrId::STR_FONT_FAMILY, &CrossPointSettings::fontFamily,
                        {StrId::STR_BOOKERLY, StrId::STR_NOTO_SANS}),
      buildReaderFontSizeSetting(registry),
      SettingInfo::Enum(StrId::STR_LINE_SPACING, &CrossPointSettings::lineSpacing,
                        {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE, StrId::STR_EXTRA_WIDE}),
      SettingInfo::Value(StrId::STR_SCREEN_MARGIN, &CrossPointSettings::screenMargin,
                         {CrossPointSettings::SCREEN_MARGIN_MIN, CrossPointSettings::SCREEN_MARGIN_MAX,
                          CrossPointSettings::SCREEN_MARGIN_STEP}),
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
  };
  // No dictionary row here: the fork's dictionary picker lives in the
  // Dictionary app (DICTIONARIES is authoritative, not SETTINGS.dictionaryName).
  if (BoardConfig::hasTouch()) {
    // The toolbar reader menu is touch-first chrome: button boards keep the
    // classic list menu, so the style choice is hidden there.
    v.push_back(SettingInfo::Enum(StrId::STR_READER_MENU_STYLE, &CrossPointSettings::readerMenuStyle,
                                  {StrId::STR_MENU_STYLE_LIST, StrId::STR_MENU_STYLE_TOOLBAR}));
  }
  v.push_back(SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar));
  return v;
}

std::vector<SettingInfo> buildControlsSettings() {
  std::vector<SettingInfo> v;
  if (!BoardConfig::hasTouch()) {
    v.push_back(SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons));
  }
  v.push_back(SettingInfo::Enum(StrId::STR_SIDE_BTN_LAYOUT, &CrossPointSettings::sideButtonLayout,
                                {StrId::STR_PREV_NEXT, StrId::STR_NEXT_PREV, StrId::STR_DISABLED}));
  if (BoardConfig::hasTouch()) {
    v.push_back(SettingInfo::Enum(
        StrId::STR_TOUCH_READER_CONTROLS, &CrossPointSettings::touchReaderControls,
        {StrId::STR_STATE_OFF, StrId::STR_STATE_TAP, StrId::STR_STATE_SWIPE, StrId::STR_STATE_INVERTED_TAP}));
  }
  // The reader-menu gesture choice only makes sense where the menu stays
  // reachable without the tap and the bottom edge is free (the capacitive
  // Home key); everywhere else the setting stays at its Tap default.
  if (BoardConfig::hasHomeKey()) {
    v.push_back(SettingInfo::Enum(StrId::STR_SHOW_READER_MENU, &CrossPointSettings::showReaderMenu,
                                  {StrId::STR_STATE_OFF, StrId::STR_STATE_TAP, StrId::STR_STATE_SWIPE_UP}));
  }
  if (!BoardConfig::hasTouch()) {
    v.push_back(SettingInfo::Toggle(StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION,
                                    &CrossPointSettings::frontButtonFollowOrientation));
  }
  v.push_back(SettingInfo::Enum(StrId::STR_LONG_PRESS_BEHAVIOR, &CrossPointSettings::longPressButtonBehavior,
                                {StrId::STR_LONG_PRESS_BEHAVIOR_OFF, StrId::STR_LONG_PRESS_BEHAVIOR_SKIP,
                                 StrId::STR_LONG_PRESS_BEHAVIOR_ORIENTATION}));
  v.push_back(SettingInfo::Enum(StrId::STR_LONG_PRESS_MENU, &CrossPointSettings::longPressMenuFunction,
                                buildLongPressMenuValues()));
  v.push_back(SettingInfo::Enum(StrId::STR_SHORT_PWR_BTN, &CrossPointSettings::shortPwrBtn, buildShortPwrBtnValues()));
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::FOOTNOTES) {
    v.push_back(SettingInfo::Toggle(StrId::STR_PWR_BTN_FOOTNOTE_BACK, &CrossPointSettings::pwrBtnFootnoteBack));
  }
  if (!BoardConfig::hasTouch()) {
    v.push_back(
        SettingInfo::Toggle(StrId::STR_BACK_SHORT_TO_FILE_BROWSER, &CrossPointSettings::backShortToFileBrowser));
  }
  if (halTiltSensor.isAvailable()) {
    v.push_back(SettingInfo::Enum(StrId::STR_TILT_PAGE_TURN, &CrossPointSettings::tiltPageTurn,
                                  {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_INVERTED}));
  }
  return v;
}

std::vector<SettingInfo> buildSystemSettings() {
  return {
      // Minutes picker (IntervalSelectionActivity), see openSleepTimeoutPicker.
      SettingInfo::Value(
          StrId::STR_TIME_TO_SLEEP, &CrossPointSettings::sleepTimeoutMinutes,
          {CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES, CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1}),
      SettingInfo::Toggle(StrId::STR_SHOW_HIDDEN_FILES, &CrossPointSettings::showHiddenFiles),
      SettingInfo::Toggle(StrId::STR_HIDE_FILE_EXTENSION, &CrossPointSettings::hideFileExtension),
      SettingInfo::Toggle(StrId::STR_REMOVE_READ_FROM_RECENTS, &CrossPointSettings::removeReadBooksFromRecents),
      SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network),
      SettingInfo::Action(StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync),
      SettingInfo::Enum(StrId::STR_OPDS_FILENAME_FORMAT, &CrossPointSettings::opdsFilenameFormat,
                        {StrId::STR_AUTHOR_TITLE, StrId::STR_TITLE_AUTHOR}),
      SettingInfo::Action(StrId::STR_OPDS_SERVERS, SettingAction::OPDSBrowser),
      SettingInfo::Action(StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache),
      // OTA fetches this board's own release asset (see OtaUpdater); boards whose
      // asset isn't published yet just report no update available.
      SettingInfo::Action(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates),
      SettingInfo::Action(StrId::STR_SD_FIRMWARE_UPDATE, SettingAction::SdFirmwareUpdate),
      SettingInfo::Action(StrId::STR_LANGUAGE, SettingAction::Language),
      SettingInfo::Action(StrId::STR_KEYBOARD_LAYOUTS, SettingAction::KeyboardLayouts),
  };
}

// Fork: the Apps tab. On X3 with a synced RTC, the Sync Day action becomes a
// clock sync and Display Day becomes a date/time mode picker.
std::vector<SettingInfo> buildAppSettings() {
  const bool rtcClockActive = SETTINGS.isHardwareRtcAutoDayClockActive();
  std::vector<SettingInfo> v;
  v.reserve(48);
  v.push_back(SettingInfo::Section(StrId::STR_SYNC_DAY));
  if (rtcClockActive) {
    v.push_back(SettingInfo::Action(StrId::STR_CLOCK_SYNC_NOW, SettingAction::ClockSync));
  } else {
    v.push_back(SettingInfo::Action(StrId::STR_SYNC_DAY, SettingAction::SyncDay));
  }
  v.push_back(SettingInfo::Action(StrId::STR_TIME_ZONE, SettingAction::TimeZone));
  if (rtcClockActive) {
    v.push_back(SettingInfo::Enum(StrId::STR_DISPLAY_DAY_TIME, &CrossPointSettings::displayDay,
                                  {StrId::STR_STATE_OFF, StrId::STR_DISPLAY_DATE_ONLY, StrId::STR_DISPLAY_TIME_ONLY,
                                   StrId::STR_DISPLAY_DAY_AND_TIME}));
  } else {
    v.push_back(SettingInfo::Toggle(StrId::STR_DISPLAY_DAY, &CrossPointSettings::displayDay));
  }
  v.push_back(SettingInfo::Enum(StrId::STR_CHOOSE_WIFI, &CrossPointSettings::syncDayWifiChoice,
                                {StrId::STR_REFRESH_MODE_AUTO, StrId::STR_MANUAL}));
  if (!rtcClockActive) {
    v.push_back(SettingInfo::Enum(StrId::STR_SYNC_DAY_REMINDER_EVERY, &CrossPointSettings::syncDayReminderStarts,
                                  {StrId::STR_STATE_OFF, StrId::STR_NUM_10, StrId::STR_NUM_20, StrId::STR_NUM_30,
                                   StrId::STR_NUM_40, StrId::STR_NUM_50, StrId::STR_NUM_60}));
  }
  v.push_back(SettingInfo::Enum(
      StrId::STR_DATE_FORMAT, &CrossPointSettings::dateFormat,
      {StrId::STR_DATE_FORMAT_DD_MM_YYYY, StrId::STR_DATE_FORMAT_MM_DD_YYYY, StrId::STR_DATE_FORMAT_YYYY_MM_DD}));

  v.push_back(SettingInfo::Section(StrId::STR_READING_STATS));
  v.push_back(SettingInfo::Action(StrId::STR_READING_STATS, SettingAction::ReadingStats));
  v.push_back(SettingInfo::Enum(StrId::STR_DAILY_GOAL, &CrossPointSettings::dailyGoalTarget,
                                {StrId::STR_MIN_15, StrId::STR_MIN_30, StrId::STR_MIN_45, StrId::STR_MIN_60}));
  v.push_back(SettingInfo::Enum(
      StrId::STR_READING_STATS_AUTOBACKUP, &CrossPointSettings::readingStatsAutoBackup,
      {StrId::STR_STATE_OFF, StrId::STR_NUM_1, StrId::STR_NUM_7, StrId::STR_NUM_14, StrId::STR_NUM_21}));
  v.push_back(SettingInfo::Action(StrId::STR_CLEAR_READING_STATS_BACKUPS, SettingAction::ClearReadingStatsBackups));
  v.push_back(SettingInfo::Toggle(StrId::STR_SHOW_AFTER_READING, &CrossPointSettings::showStatsAfterReading));
  v.push_back(SettingInfo::Toggle(StrId::STR_MOVE_COMPLETED_BOOKS, &CrossPointSettings::moveCompletedBooks));
  v.push_back(SettingInfo::Action(StrId::STR_RESET_READING_STATS, SettingAction::ResetReadingStats));
  v.push_back(SettingInfo::Action(StrId::STR_EXPORT_READING_STATS, SettingAction::ExportReadingStats));
  v.push_back(SettingInfo::Action(StrId::STR_IMPORT_READING_STATS, SettingAction::ImportReadingStats));
  v.push_back(SettingInfo::Action(StrId::STR_READING_HEATMAP, SettingAction::ReadingHeatmap));
  v.push_back(SettingInfo::Action(StrId::STR_READING_PROFILE, SettingAction::ReadingProfile));

  v.push_back(SettingInfo::Section(StrId::STR_ACHIEVEMENTS));
  v.push_back(SettingInfo::Action(StrId::STR_ACHIEVEMENTS, SettingAction::Achievements));
  v.push_back(SettingInfo::Toggle(StrId::STR_ENABLE_ACHIEVEMENTS, &CrossPointSettings::achievementsEnabled));
  v.push_back(SettingInfo::Toggle(StrId::STR_ACHIEVEMENT_POPUPS, &CrossPointSettings::achievementPopups));
  v.push_back(SettingInfo::Action(StrId::STR_RESET_ACHIEVEMENTS, SettingAction::ResetAchievements));
  v.push_back(SettingInfo::Action(StrId::STR_SYNC_WITH_PREV_STATS, SettingAction::SyncAchievementsFromStats));

  v.push_back(SettingInfo::Section(StrId::STR_APPS));
  v.push_back(SettingInfo::Action(StrId::STR_HIGHLIGHTS, SettingAction::Bookmarks));
  v.push_back(SettingInfo::Action(StrId::STR_FAVORITES, SettingAction::Favorites));
  v.push_back(SettingInfo::Action(StrId::STR_SCREEN_CLEAN, SettingAction::ScreenClean));
  v.push_back(SettingInfo::Action(StrId::STR_SLEEP, SettingAction::SleepApp));
  v.push_back(SettingInfo::Action(StrId::STR_IF_FOUND_RETURN_ME, SettingAction::IfFound));

  v.push_back(SettingInfo::Section(StrId::STR_FLASHCARDS));
  v.push_back(SettingInfo::Action(StrId::STR_FLASHCARDS, SettingAction::Flashcards));
  v.push_back(
      SettingInfo::Enum(StrId::STR_STUDY_MODE, &CrossPointSettings::flashcardStudyMode,
                        {StrId::STR_DUE, StrId::STR_SCHEDULED, StrId::STR_RANDOM_PRACTICE, StrId::STR_SEQUENTIAL}));
  v.push_back(
      SettingInfo::Enum(StrId::STR_SESSION_SIZE, &CrossPointSettings::flashcardSessionSize,
                        {StrId::STR_NUM_10, StrId::STR_NUM_20, StrId::STR_NUM_30, StrId::STR_NUM_50, StrId::STR_ALL}));

  v.push_back(SettingInfo::Section(StrId::STR_SHORTCUTS_SECTION));
  v.push_back(SettingInfo::Action(StrId::STR_SHORTCUT_LOCATION, SettingAction::ShortcutLocation));
  v.push_back(SettingInfo::Action(StrId::STR_SHORTCUT_VISIBILITY, SettingAction::ShortcutVisibility));
  v.push_back(SettingInfo::Action(StrId::STR_ORDER_HOME_SHORTCUTS, SettingAction::OrderHomeShortcuts));
  v.push_back(SettingInfo::Action(StrId::STR_ORDER_APPS_SHORTCUTS, SettingAction::OrderAppsShortcuts));
  return v;
}

// ---------------------------------------------------------------------------
// Value-text helpers (fork).
// ---------------------------------------------------------------------------

std::string getReadingStatsExportPath() { return "/exports/stats_exported"; }

std::string fileNameFromPath(const std::string& path) {
  const size_t pos = path.find_last_of('/');
  if (pos == std::string::npos) {
    return path;
  }
  return path.substr(pos + 1);
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

std::string getActionValueText(const SettingInfo& setting) {
  switch (setting.action) {
    case SettingAction::Network:
      return getNetworkSettingValueText();
    case SettingAction::CheckForUpdates:
      return CROSSPOINT_VERSION;
    case SettingAction::Language:
      return I18N.getLanguageName(I18N.getLanguage());
    case SettingAction::TimeZone:
      return TimeUtils::getCurrentTimeZoneLabel();
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
    // The export/import rows show the file name they act on.
    case SettingAction::ExportReadingStats:
      return getReadingStatsExportFileName();
    case SettingAction::ImportReadingStats:
      return getLatestReadingStatsImportFileName();
    default:
      return "";
  }
}
}  // namespace

SettingsActivity::SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiTabListActivity("Settings", renderer, mappedInput) {}

void SettingsActivity::rebuildSettingsLists() {
  // Pick up any fonts uploaded/deleted over the web server since the last
  // reader activity ran — otherwise the font-family picker shows a stale list.
  sdFontSystem.refreshIfDirty();

  displaySettings = buildDisplaySettings();
  readerSettings = buildReaderSettings(&sdFontSystem.registry());
  controlsSettings = buildControlsSettings();
  systemSettings = buildSystemSettings();
  appSettings = buildAppSettings();

  selectCategory(selectedCategoryIndex);
}

void SettingsActivity::onEnter() {
  UiTabListActivity::onEnter();

  // Reset selection to first category (ring position 0, the tab bar, comes
  // from the base's per-tab nav reset)
  selectedCategoryIndex = 0;
  preserveQuickResumeTimeoutOn =
      SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
  quickResumeTimeoutAutoEnabled = false;
  syncQuickResumeTimeoutForSleepScreen(/*sleepScreenChanged=*/true, /*quickResumeTimeoutChanged=*/false);

  rebuildSettingsLists();
}

void SettingsActivity::onExit() {
  Activity::onExit();

  UITheme::getInstance().reload();  // Re-apply theme in case it was changed
}

void SettingsActivity::selectCategory(const int categoryIndex) {
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
      currentSettings = &appSettings;
      break;
  }
  settingsCount = static_cast<int>(currentSettings->size());
  // Keep a remembered ring position inside the (possibly shorter) new list.
  if (activeNav().selected > settingsCount) activeNav().selected = settingsCount;
  rebuildRowItems();
}

// Rebuilds rowValues_/rowItems_ (label + actionValue) for *currentSettings.
// Structural — call only when the active category or a category's setting
// list changes, never from buildScreen(), which only refreshes rowValues_
// content and rowItems_[].value pointers in place.
void SettingsActivity::rebuildRowItems() {
  const auto& settings = *currentSettings;
  rowValues_.assign(settings.size(), std::string());
  rowItems_.clear();
  rowItems_.reserve(settings.size());
  for (size_t i = 0; i < settings.size(); i++) {
    fui::ListItem item;
    item.label = I18N.get(settings[i].nameId);
    item.actionValue = static_cast<int16_t>(i);
    // Section headings are non-interactive rows (never selected or focused);
    // navigateButtons() steps over them.
    item.isHeader = settings[i].type == SettingType::SECTION;
    rowItems_.push_back(item);
  }
}

bool SettingsActivity::isSelectableSetting(const int settingIndex) const {
  if (currentSettings == nullptr || settingIndex < 0 || settingIndex >= settingsCount) {
    return false;
  }
  return (*currentSettings)[settingIndex].type != SettingType::SECTION;
}

int SettingsActivity::firstSelectableRing() const {
  for (int index = 0; index < settingsCount; ++index) {
    if (isSelectableSetting(index)) {
      return index + 1;
    }
  }
  return 0;
}

int SettingsActivity::stepRing(const int direction) const {
  const int ringSize = settingsCount + 1;
  if (ringSize <= 1) {
    return 0;
  }
  int candidate = ringPos();
  for (int guard = 0; guard < ringSize; ++guard) {
    candidate = direction > 0 ? ButtonNavigator::nextIndex(candidate, ringSize)
                              : ButtonNavigator::previousIndex(candidate, ringSize);
    if (candidate == 0 || isSelectableSetting(candidate - 1)) {
      return candidate;
    }
  }
  return ringPos();
}

void SettingsActivity::navigateButtons() {
  // Same ring walk as the base, but section headings are skipped.
  buttonNavigator.onNextRelease([this] { moveRingTo(stepRing(1)); });
  buttonNavigator.onPreviousRelease([this] { moveRingTo(stepRing(-1)); });
  buttonNavigator.onNextContinuous([this] { stepTab(1); });
  buttonNavigator.onPreviousContinuous([this] { stepTab(-1); });
}

void SettingsActivity::onTabAction(const int index) {
  if (optionPopup.isActive()) return;
  selectCategory(index);
  activeNav().selected = 0;  // tab taps land with the tab bar focused
  activeNav().top = 0;
  // The switched-to tab repaints as the selected pill; a flash overlay on top
  // of it just repaints the pill in the focused style.
  app.clearTapFlash();
}

void SettingsActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  if (!isSelectableSetting(index)) return;  // section heading
  // Most rows repaint a different surface (popup, sub-activity, new value);
  // a lingering tap flash would gray an unrelated element.
  app.clearTapFlash();
  toggleCurrentSetting();  // reads the ring position set by onRowAction
  // Tap-first: a tapped row is not a cursor position. Leaving it focused
  // (inverted) after the tap meant the row stayed black once its sub-screen or
  // popup closed, and Back then had to clear that focus before a second Back
  // left Settings. Hand the focus back to the tab band; the viewport stays put.
  if (mappedInput.hasTouch()) {
    activeNav().selected = 0;
  }
}

void SettingsActivity::applyUiSettingChange(uint8_t CrossPointSettings::* valuePtr) {
  // Theme changes take effect immediately, on this screen — reload the theme
  // and re-derive the app's tokens so the very next repaint is in the new look.
  if (valuePtr != &CrossPointSettings::uiTheme) {
    return;
  }
  UITheme::getInstance().reload();
  // Re-derive the shared tokens for the new look; the gate stays closed until
  // the repaint that rebuilds the interaction table in the new layout.
  resetUi();
}

bool SettingsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void SettingsActivity::stepTab(const int direction) {
  // Ring position 0 stays on the tab bar; a row selection collapses to the
  // new category's first selectable row (per-tab memory is deliberately not
  // kept here).
  const bool onTabBar = ringPos() == 0;
  selectedCategoryIndex = direction > 0 ? ButtonNavigator::nextIndex(selectedCategoryIndex, categoryCount)
                                        : ButtonNavigator::previousIndex(selectedCategoryIndex, categoryCount);
  selectCategory(selectedCategoryIndex);
  activeNav().top = 0;  // category switches start the list at the top
  activeNav().selected = onTabBar ? 0 : firstSelectableRing();
  requestUpdate();
}

bool SettingsActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (ringPos() == 0) {
      stepTab(1);
    } else {
      toggleCurrentSetting();
      requestUpdate();
    }
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (ringPos() > 0) {
      activeNav().selected = 0;
      requestUpdate();
    } else {
      SETTINGS.saveToFile();
      onGoHome();
    }
    return true;
  }

  return false;
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

void SettingsActivity::toggleCurrentSetting() {
  const int selectedSetting = ringPos() - 1;
  if (selectedSetting < 0 || selectedSetting >= settingsCount) {
    return;
  }

  // Copy: the popup callbacks below outlive this call, and rebuildSettingsLists()
  // reallocates the category vectors.
  const SettingInfo setting = (*currentSettings)[selectedSetting];
  const uint8_t previousReadingStatsAutoBackup = SETTINGS.readingStatsAutoBackup;
  const bool sleepScreenChanged = setting.valuePtr == &CrossPointSettings::sleepScreen;
  const bool quickResumeTimeoutChanged = setting.valuePtr == &CrossPointSettings::quickResumeSleepScreen;

  if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
    openSleepTimeoutPicker();
    return;
  }

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    // Toggle the boolean value using the member pointer
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !currentValue;
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    if (setting.nameId == StrId::STR_FONT_FAMILY) {
      // Fork: the family picker lists built-in + SD families on its own screen.
      sdFontSystem.refreshIfDirty();
      startActivityForResult(std::make_unique<FontSelectionActivity>(renderer, mappedInput, &sdFontSystem.registry()),
                             [this](const ActivityResult&) {
                               ensureSdFontLoaded();
                               SETTINGS.saveToFile();
                               rebuildSettingsLists();  // the size row tracks the family
                               requestUpdate(true);
                             });
      return;
    }
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    if (setting.enumValues.size() > 2) {
      const auto valuePtr = setting.valuePtr;
      optionPopup.show(
          setting.nameId, setting.enumValues.data(), static_cast<int>(setting.enumValues.size()), currentValue,
          [this, setting, valuePtr, previousReadingStatsAutoBackup, sleepScreenChanged,
           quickResumeTimeoutChanged](int idx) {
            SETTINGS.*valuePtr = idx;
            afterSettingChanged(setting, previousReadingStatsAutoBackup, sleepScreenChanged, quickResumeTimeoutChanged);
          });
      requestUpdate();
      return;
    }
    SETTINGS.*(setting.valuePtr) = (currentValue + 1) % static_cast<uint8_t>(setting.enumValues.size());
  } else if (setting.type == SettingType::ENUM && setting.valueGetter && setting.valueSetter) {
    const uint8_t totalValues = setting.enumStringValues.empty()
                                    ? static_cast<uint8_t>(setting.enumValues.size())
                                    : static_cast<uint8_t>(setting.enumStringValues.size());
    if (totalValues == 0) return;
    const uint8_t cur = setting.valueGetter();
    if (totalValues > 2) {
      auto onSelect = [this, setting, previousReadingStatsAutoBackup, sleepScreenChanged,
                       quickResumeTimeoutChanged](int idx) {
        setting.valueSetter(static_cast<uint8_t>(idx));
        afterSettingChanged(setting, previousReadingStatsAutoBackup, sleepScreenChanged, quickResumeTimeoutChanged);
      };
      if (!setting.enumStringValues.empty()) {
        optionPopup.show(setting.nameId, setting.enumStringValues, cur, std::move(onSelect));
      } else {
        optionPopup.show(setting.nameId, setting.enumValues.data(), static_cast<int>(setting.enumValues.size()), cur,
                         std::move(onSelect));
      }
      requestUpdate();
      return;
    }
    setting.valueSetter((cur + 1) % totalValues);
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    const int currentValue = SETTINGS.*(setting.valuePtr);
    if (currentValue + setting.valueRange.step > setting.valueRange.max) {
      SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
    } else {
      SETTINGS.*(setting.valuePtr) = static_cast<uint8_t>(currentValue + setting.valueRange.step);
    }
  } else if (setting.type == SettingType::ACTION) {
    runAction(setting);
    return;  // Results will be handled in the result handler, so we can return early here
  } else {
    return;  // SECTION / STRING rows are not toggled on device
  }

  afterSettingChanged(setting, previousReadingStatsAutoBackup, sleepScreenChanged, quickResumeTimeoutChanged);
  activeNav().selected = std::min(ringPos(), settingsCount);
}

void SettingsActivity::afterSettingChanged(const SettingInfo& setting, const uint8_t previousReadingStatsAutoBackup,
                                           const bool sleepScreenChanged, const bool quickResumeTimeoutChanged) {
  syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);

  // Fork hooks: achievements follow the daily goal; the reader font reloads
  // when the size changes; dark mode repaints immediately.
  if (setting.valuePtr == &CrossPointSettings::dailyGoalTarget) {
    ACHIEVEMENTS.syncWithPreviousStats();
  }
  if (setting.nameId == StrId::STR_FONT_SIZE || setting.nameId == StrId::STR_FONT_FAMILY) {
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
  // Lists depend on live state (font sizes per family, footnote-back row,
  // RTC clock rows), so rebuild after every change.
  rebuildSettingsLists();
  applyUiSettingChange(setting.valuePtr);

  if (createInitialReadingStatsBackup) {
    showTransientPopup(tr(STR_READING_STATS_BACKUP_RUNNING), 20, 120);
    const bool backupReady = READING_STATS.ensureAutoBackupForEnabledSetting();
    showTransientPopup(backupReady ? tr(STR_READING_STATS_BACKUP_DONE) : tr(STR_READING_STATS_BACKUP_PENDING),
                       backupReady ? 100 : -1, backupReady ? 350 : 700);
    requestUpdate(true);
  }
}

void SettingsActivity::runAction(const SettingInfo& setting) {
  auto resultHandler = [this](const ActivityResult&) { SETTINGS.saveToFile(); };
  // Rows whose value text or presence depends on the sub-screen's outcome
  // need the lists rebuilt on return (labels are translated once in
  // rebuildRowItems() and don't re-run on Pop).
  auto rebuildingHandler = [this](const ActivityResult&) {
    SETTINGS.saveToFile();
    rebuildSettingsLists();
    requestUpdate(true);
  };

  switch (setting.action) {
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
      startActivityForResult(std::make_unique<LanguageSelectActivity>(renderer, mappedInput), rebuildingHandler);
      break;
    case SettingAction::DownloadFonts:
      startActivityForResult(std::make_unique<FontDownloadActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) {
                               ensureSdFontLoaded();
                               SETTINGS.saveToFile();
                               rebuildSettingsLists();
                               requestUpdate(true);
                             });
      break;
    case SettingAction::TextSettings:
      startActivityForResult(std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                                    TextSettingsActivity::Tab::Family),
                             [this](const ActivityResult&) {
                               // TextSettingsActivity saves on each change; the
                               // reader font follows the (possibly new) family/size.
                               ensureSdFontLoaded();
                               rebuildSettingsLists();
                               requestUpdate(true);
                             });
      break;
    case SettingAction::KeyboardLayouts:
      if (auto activity = makeUniqueNoThrow<KeyboardLayoutsActivity>(renderer, mappedInput)) {
        startActivityForResult(std::move(activity), nullptr);
      } else {
        LOG_ERR("SETTINGS", "OOM: KeyboardLayoutsActivity");
      }
      break;
    case SettingAction::SyncDay:
      startActivityForResult(std::make_unique<SyncDayActivity>(renderer, mappedInput), rebuildingHandler);
      break;
    case SettingAction::ClockSync:
      startActivityForResult(std::make_unique<ClockSyncActivity>(renderer, mappedInput), rebuildingHandler);
      break;
    case SettingAction::TimeZone:
      startActivityForResult(std::make_unique<TimeZoneSelectActivity>(renderer, mappedInput), rebuildingHandler);
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
}

void SettingsActivity::syncQuickResumeTimeoutForSleepScreen(bool sleepScreenChanged, bool quickResumeTimeoutChanged) {
  if (quickResumeTimeoutChanged) {
    preserveQuickResumeTimeoutOn =
        SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
    quickResumeTimeoutAutoEnabled = false;
  }

  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME) {
    if (SETTINGS.quickResumeSleepScreen != CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT) {
      SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
      quickResumeTimeoutAutoEnabled = !preserveQuickResumeTimeoutOn;
    } else if (sleepScreenChanged && !preserveQuickResumeTimeoutOn) {
      quickResumeTimeoutAutoEnabled = true;
    }
    return;
  }

  if (sleepScreenChanged && quickResumeTimeoutAutoEnabled && !preserveQuickResumeTimeoutOn) {
    SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_NEVER;
    quickResumeTimeoutAutoEnabled = false;
  }
}

void SettingsActivity::openSleepTimeoutPicker() {
  startActivityForResult(
      std::make_unique<IntervalSelectionActivity>(
          renderer, mappedInput, "SleepTimeoutInterval", StrId::STR_TIME_TO_SLEEP, SETTINGS.sleepTimeoutMinutes,
          CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES, CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1, 5,
          StrId::STR_SLEEP_TIMER_VALUE_FORMAT, false, StrId::STR_SLEEP_NEVER),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          SETTINGS.sleepTimeoutMinutes = static_cast<uint8_t>(std::get<IntervalResult>(result.data).value);
          SETTINGS.saveToFile();
        }
        requestUpdate();
      });
}

std::string SettingsActivity::settingValueText(const SettingInfo& setting) const {
  if (setting.nameId == StrId::STR_FONT_FAMILY && SETTINGS.sdFontFamilyName[0] != '\0') {
    return SETTINGS.sdFontFamilyName;
  }
  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    return SETTINGS.*(setting.valuePtr) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  }
  if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    // Guard like the valueGetter branch below: a corrupt/migrated settings
    // byte must not index past the enum table.
    const uint8_t value = SETTINGS.*(setting.valuePtr);
    if (value >= setting.enumValues.size()) return "";
    return I18N.get(setting.enumValues[value]);
  }
  if (setting.type == SettingType::ENUM && setting.valueGetter) {
    const uint8_t value = setting.valueGetter();
    if (!setting.enumStringValues.empty() && value < setting.enumStringValues.size()) {
      return setting.enumStringValues[value];
    }
    if (value < setting.enumValues.size()) {
      return I18N.get(setting.enumValues[value]);
    }
    return "";
  }
  if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
      if (SETTINGS.sleepTimeoutMinutes >= CrossPointSettings::SLEEP_TIMEOUT_NEVER_MINUTES) {
        return tr(STR_SLEEP_NEVER);
      }
      char valueBuffer[32];
      snprintf(valueBuffer, sizeof(valueBuffer), tr(STR_SLEEP_TIMER_VALUE_FORMAT),
               static_cast<unsigned int>(SETTINGS.*(setting.valuePtr)));
      return valueBuffer;
    }
    return std::to_string(SETTINGS.*(setting.valuePtr));
  }
  if (setting.type == SettingType::ACTION) {
    return getActionValueText(setting);
  }
  return "";
}

void SettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});

  buildTabBar(screen);

  // rowItems_ (label/actionValue) was built by rebuildRowItems() when the
  // category was last selected/rebuilt; only the live value text needs
  // refreshing here, by assigning into the existing rowValues_ strings (no
  // vector growth) rather than building a new items/values vector on every
  // render.
  const auto& settings = *currentSettings;
  for (size_t i = 0; i < settings.size(); i++) {
    rowValues_[i] = settings[i].type == SettingType::SECTION ? std::string() : settingValueText(settings[i]);
    rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
  }

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  // Titles match the value's font size (smallText) so both sides of a row
  // read as one unit; labels that still don't fit wrap onto a second line.
  // maxLines=2 also marks the style explicitly set (an all-default smallText
  // fails textStyleUnset and the list would substitute bodyText back); the
  // common fits-on-one-line case takes the renderer's fast path anyway.
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncTabListViewport(screen, props);
  screen.list(props);
}

void SettingsActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the rest of the screen renders through the app.
  // Version rides in the header's trailing label slot: the footer position
  // conflicts with button hints on non-touch devices.
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SETTINGS_TITLE),
                 CROSSPOINT_VERSION);
  // Fork: date/time in the top line when the user enabled it.
  HeaderDateUtils::drawTopLine(renderer, HeaderDateUtils::getDisplayDateText());

  renderUi();

  const int ring = ringPos();
  const char* confirmLabel = I18N.get(categoryNames[(selectedCategoryIndex + 1) % categoryCount]);
  if (ring > 0 && ring <= settingsCount) {
    const auto& selectedSetting = (*currentSettings)[ring - 1];
    const bool selectStyle =
        selectedSetting.type == SettingType::ACTION || selectedSetting.type == SettingType::SECTION ||
        selectedSetting.nameId == StrId::STR_TIME_TO_SLEEP || selectedSetting.nameId == StrId::STR_FONT_FAMILY;
    confirmLabel = selectStyle ? tr(STR_SELECT) : tr(STR_TOGGLE);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Always use standard refresh for settings screen
  renderer.displayBuffer();
}
