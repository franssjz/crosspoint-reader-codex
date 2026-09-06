#include "CrossPointSettings.h"

#include <HalClock.h>
#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>
#include <Serialization.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>

#include "ReaderFontSizes.h"
#include "fontIds.h"

// Initialize the static instance
CrossPointSettings CrossPointSettings::instance;

void readAndValidate(HalFile& file, uint8_t& member, const uint8_t maxValue) {
  uint8_t tempValue;
  serialization::readPod(file, tempValue);
  if (tempValue < maxValue) {
    member = tempValue;
  }
}

namespace {
constexpr uint8_t SETTINGS_FILE_VERSION = 1;
constexpr char SETTINGS_FILE_BIN[] = "/.crosspoint/settings.bin";
constexpr char SETTINGS_FILE_JSON[] = "/.crosspoint/settings.json";
constexpr char SETTINGS_FILE_BAK[] = "/.crosspoint/settings.bin.bak";
constexpr uint8_t LEGACY_FONT_SIZE_COUNT = 4;
constexpr uint8_t LEGACY_LEXEND_FONT_FAMILY = 2;
constexpr char LEXEND_SD_FAMILY_NAME[] = "Lexend";

uint8_t migrateLegacyUiTheme(const uint8_t legacyUiTheme) {
  switch (legacyUiTheme) {
    case 0:  // Classic
    case 1:  // Lyra
    default:
      return CrossPointSettings::LYRA;
    case 2:  // Lyra Extended
    case 3:  // Lyra Custom
      return CrossPointSettings::LYRA_CUSTOM;
  }
}

// settings.bin stored the 0..3 SMALL..EXTRA_LARGE slot; the fork later inserted
// X_SMALL at 0, so the stored slot shifts up by one before it is turned into a
// point size.
uint8_t migrateLegacyFontSize(const uint8_t legacyFontSize) {
  return legacyFontSize < LEGACY_FONT_SIZE_COUNT ? static_cast<uint8_t>(legacyFontSize + 1)
                                                 : static_cast<uint8_t>(CrossPointSettings::MEDIUM);
}

// Convert legacy front button layout into explicit logical->hardware mapping.
void applyLegacyFrontButtonLayout(CrossPointSettings& settings) {
  switch (static_cast<CrossPointSettings::FRONT_BUTTON_LAYOUT>(settings.frontButtonLayout)) {
    case CrossPointSettings::LEFT_RIGHT_BACK_CONFIRM:
      settings.frontButtonBack = CrossPointSettings::FRONT_HW_LEFT;
      settings.frontButtonConfirm = CrossPointSettings::FRONT_HW_RIGHT;
      settings.frontButtonLeft = CrossPointSettings::FRONT_HW_BACK;
      settings.frontButtonRight = CrossPointSettings::FRONT_HW_CONFIRM;
      break;
    case CrossPointSettings::LEFT_BACK_CONFIRM_RIGHT:
      settings.frontButtonBack = CrossPointSettings::FRONT_HW_CONFIRM;
      settings.frontButtonConfirm = CrossPointSettings::FRONT_HW_LEFT;
      settings.frontButtonLeft = CrossPointSettings::FRONT_HW_BACK;
      settings.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
      break;
    case CrossPointSettings::BACK_CONFIRM_RIGHT_LEFT:
      settings.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
      settings.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
      settings.frontButtonLeft = CrossPointSettings::FRONT_HW_RIGHT;
      settings.frontButtonRight = CrossPointSettings::FRONT_HW_LEFT;
      break;
    case CrossPointSettings::BACK_CONFIRM_LEFT_RIGHT:
    default:
      settings.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
      settings.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
      settings.frontButtonLeft = CrossPointSettings::FRONT_HW_LEFT;
      settings.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
      break;
  }
}

}  // namespace

void CrossPointSettings::validateFrontButtonMapping(CrossPointSettings& settings) {
  const uint8_t mapping[] = {settings.frontButtonBack, settings.frontButtonConfirm, settings.frontButtonLeft,
                             settings.frontButtonRight};
  for (size_t i = 0; i < 4; i++) {
    for (size_t j = i + 1; j < 4; j++) {
      if (mapping[i] == mapping[j]) {
        settings.frontButtonBack = FRONT_HW_BACK;
        settings.frontButtonConfirm = FRONT_HW_CONFIRM;
        settings.frontButtonLeft = FRONT_HW_LEFT;
        settings.frontButtonRight = FRONT_HW_RIGHT;
        return;
      }
    }
  }
}

uint8_t CrossPointSettings::sleepTimeoutEnumToMinutes(const uint8_t legacyValue) {
  switch (legacyValue) {
    case SLEEP_1_MIN:
      return 1;
    case SLEEP_5_MIN:
      return 5;
    case SLEEP_15_MIN:
      return 15;
    case SLEEP_30_MIN:
      return 30;
    case SLEEP_10_MIN:
    default:
      return 10;
  }
}

bool CrossPointSettings::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveSettings(*this, SETTINGS_FILE_JSON);
}

bool CrossPointSettings::loadFromFile() {
  const std::string tempPath = std::string(SETTINGS_FILE_JSON) + ".tmp";
  if (!Storage.exists(SETTINGS_FILE_JSON) && Storage.exists(tempPath.c_str())) {
    if (Storage.rename(tempPath.c_str(), SETTINGS_FILE_JSON)) {
      LOG_DBG("CPS", "Recovered settings.json from interrupted temp file");
    }
  }

  // Try JSON first
  if (Storage.exists(SETTINGS_FILE_JSON)) {
    String json = Storage.readFile(SETTINGS_FILE_JSON);
    if (!json.isEmpty()) {
      bool resave = false;
      bool result = JsonSettingsIO::loadSettings(*this, json.c_str(), &resave);
      if (result && resave) {
        if (saveToFile()) {
          LOG_DBG("CPS", "Resaved settings to update format");
        } else {
          LOG_ERR("CPS", "Failed to resave settings after format update");
        }
      }
      return result;
    }
  }

  // Fall back to binary migration
  if (Storage.exists(SETTINGS_FILE_BIN)) {
    if (loadFromBinaryFile()) {
      if (saveToFile()) {
        Storage.rename(SETTINGS_FILE_BIN, SETTINGS_FILE_BAK);
        LOG_DBG("CPS", "Migrated settings.bin to settings.json");
        return true;
      } else {
        LOG_ERR("CPS", "Failed to save migrated settings to JSON");
        return false;
      }
    }
  }

  return false;
}

bool CrossPointSettings::loadFromBinaryFile() {
  HalFile inputFile;
  if (!Storage.openFileForRead("CPS", SETTINGS_FILE_BIN, inputFile)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);
  if (version != SETTINGS_FILE_VERSION) {
    LOG_ERR("CPS", "Deserialization failed: Unknown version %u", version);
    return false;
  }

  uint8_t fileSettingsCount = 0;
  serialization::readPod(inputFile, fileSettingsCount);

  uint8_t settingsRead = 0;
  bool frontButtonMappingRead = false;
  do {
    readAndValidate(inputFile, sleepScreen, SLEEP_SCREEN_MODE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, extraParagraphSpacing);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, shortPwrBtn, SHORT_PWRBTN_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, statusBar, STATUS_BAR_MODE_COUNT);  // legacy
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, orientation, ORIENTATION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, frontButtonLayout, FRONT_BUTTON_LAYOUT_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, sideButtonLayout, SIDE_BUTTON_LAYOUT_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    {
      uint8_t storedFontFamily = BOOKERLY;
      serialization::readPod(inputFile, storedFontFamily);
      if (storedFontFamily == LEGACY_LEXEND_FONT_FAMILY) {
        fontFamily = BOOKERLY;
        strncpy(sdFontFamilyName, LEXEND_SD_FAMILY_NAME, sizeof(sdFontFamilyName) - 1);
        sdFontFamilyName[sizeof(sdFontFamilyName) - 1] = '\0';
      } else if (storedFontFamily < FONT_FAMILY_COUNT) {
        fontFamily = storedFontFamily;
      }
    }
    if (++settingsRead >= fileSettingsCount) break;
    {
      uint8_t legacyFontSize = static_cast<uint8_t>(MEDIUM - 1);
      serialization::readPod(inputFile, legacyFontSize);
      fontPointSize = legacyFontSizeSlotToPointSize(migrateLegacyFontSize(legacyFontSize));
    }
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, lineSpacing, LINE_COMPRESSION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, paragraphAlignment, PARAGRAPH_ALIGNMENT_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    {
      uint8_t legacySleepTimeout = SLEEP_10_MIN;
      readAndValidate(inputFile, legacySleepTimeout, SLEEP_TIMEOUT_COUNT);
      sleepTimeoutMinutes = sleepTimeoutEnumToMinutes(legacySleepTimeout);
    }
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, refreshFrequency, REFRESH_FREQUENCY_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, screenMargin);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, sleepScreenCoverMode, SLEEP_SCREEN_COVER_MODE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    {
      std::string urlStr;
      serialization::readString(inputFile, urlStr);
      strncpy(opdsServerUrl, urlStr.c_str(), sizeof(opdsServerUrl) - 1);
      opdsServerUrl[sizeof(opdsServerUrl) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, textAntiAliasing);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, hideBatteryPercentage, HIDE_BATTERY_PERCENTAGE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    {
      uint8_t legacyLongPressChapterSkip = 1;
      serialization::readPod(inputFile, legacyLongPressChapterSkip);
      longPressButtonBehavior = legacyLongPressChapterSkip ? LONG_PRESS_CHAPTER_SKIP : LONG_PRESS_OFF;
    }
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, hyphenationEnabled);
    if (++settingsRead >= fileSettingsCount) break;
    {
      std::string usernameStr;
      serialization::readString(inputFile, usernameStr);
      strncpy(opdsUsername, usernameStr.c_str(), sizeof(opdsUsername) - 1);
      opdsUsername[sizeof(opdsUsername) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount) break;
    {
      std::string passwordStr;
      serialization::readString(inputFile, passwordStr);
      strncpy(opdsPassword, passwordStr.c_str(), sizeof(opdsPassword) - 1);
      opdsPassword[sizeof(opdsPassword) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, sleepScreenCoverFilter, SLEEP_SCREEN_COVER_FILTER_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    {
      uint8_t legacyUiTheme = static_cast<uint8_t>(LYRA);
      serialization::readPod(inputFile, legacyUiTheme);
      uiTheme = migrateLegacyUiTheme(legacyUiTheme);
    }
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, frontButtonBack, FRONT_BUTTON_HARDWARE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, frontButtonConfirm, FRONT_BUTTON_HARDWARE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, frontButtonLeft, FRONT_BUTTON_HARDWARE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, frontButtonRight, FRONT_BUTTON_HARDWARE_COUNT);
    frontButtonMappingRead = true;
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, fadingFix);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, embeddedStyle);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, frontButtonFollowOrientation);
    if (++settingsRead >= fileSettingsCount) break;
  } while (false);

  if (frontButtonMappingRead) {
    CrossPointSettings::validateFrontButtonMapping(*this);
  } else {
    applyLegacyFrontButtonLayout(*this);
  }

  LOG_DBG("CPS", "Settings loaded from binary file");
  return true;
}

float CrossPointSettings::getReaderLineCompression() const {
  if (strcmp(sdFontFamilyName, LEXEND_SD_FAMILY_NAME) == 0) {
    switch (lineSpacing) {
      case TIGHT:
        return 0.90f;
      case NORMAL:
      default:
        return 0.95f;
      case WIDE:
        return 1.0f;
      case EXTRA_WIDE:
        return 1.1f;
    }
  }

  switch (fontFamily) {
    case BOOKERLY:
    default:
      switch (lineSpacing) {
        case TIGHT:
          return 0.95f;
        case NORMAL:
        default:
          return 1.0f;
        case WIDE:
          return 1.1f;
        case EXTRA_WIDE:
          return 1.2f;
      }
    case NOTOSANS:
      switch (lineSpacing) {
        case TIGHT:
          return 0.90f;
        case NORMAL:
        default:
          return 0.95f;
        case WIDE:
          return 1.0f;
        case EXTRA_WIDE:
          return 1.05f;
      }
  }
}

unsigned long CrossPointSettings::getSleepTimeoutMs() const {
  if (sleepTimeoutMinutes >= SLEEP_TIMEOUT_NEVER_MINUTES) return 0UL;
  const uint8_t minutes =
      std::clamp(sleepTimeoutMinutes, MIN_SLEEP_TIMEOUT_MINUTES, static_cast<uint8_t>(SLEEP_TIMEOUT_NEVER_MINUTES - 1));
  return static_cast<unsigned long>(minutes) * 60UL * 1000UL;
}

CrossPointSettings::StatusBarSpec CrossPointSettings::statusBarSpec() const {
  StatusBarSpec spec;
  spec.showChapterPageCount = statusBarChapterPageCount != 0;
  spec.showBookProgressPercent = statusBarBookProgressPercentage != 0;
  spec.titleMode = statusBarTitle;
  spec.showBattery = statusBarBattery != 0;
  spec.showBatteryPercent = hideBatteryPercentage == HIDE_NEVER;
  spec.clockMode = statusBarClock;
  spec.clock12h = clockFormat == 1;
  spec.clockUtcOffsetQ = clockUtcOffsetQ;
  spec.progressBarMode = statusBarProgressBar;
  spec.progressBarHeightPx =
      statusBarProgressBar != HIDE_PROGRESS ? static_cast<uint8_t>((statusBarProgressBarThickness + 1) * 2) : 0;
  spec.xtcMode = xtcStatusBarMode;
  return spec;
}

ReaderRenderSpec CrossPointSettings::readerRenderSpec(const uint16_t viewportWidth,
                                                      const uint16_t viewportHeight) const {
  ReaderRenderSpec spec;
  spec.fontId = getReaderFontId();
  spec.lineCompression = getReaderLineCompression();
  spec.extraParagraphSpacing = extraParagraphSpacing != 0;
  spec.forceParagraphIndents = forceParagraphIndents != 0;
  spec.paragraphAlignment = paragraphAlignment;
  spec.viewportWidth = viewportWidth;
  spec.viewportHeight = viewportHeight;
  spec.hyphenationEnabled = hyphenationEnabled != 0;
  spec.embeddedStyle = embeddedStyle != 0;
  spec.imageRendering = imageRendering;
  // Only NORMAL bionic reading changes the layout (bold prefixes are wider);
  // SUBTLE is a render-time effect and must not invalidate section caches.
  spec.focusReadingEnabled = bionicReading == BIONIC_READING_NORMAL;
  return spec;
}

uint64_t CrossPointSettings::getDailyGoalMs() const {
  switch (dailyGoalTarget) {
    case DAILY_GOAL_15_MIN:
      return 15ULL * 60ULL * 1000ULL;
    case DAILY_GOAL_30_MIN:
    default:
      return 30ULL * 60ULL * 1000ULL;
    case DAILY_GOAL_45_MIN:
      return 45ULL * 60ULL * 1000ULL;
    case DAILY_GOAL_60_MIN:
      return 60ULL * 60ULL * 1000ULL;
  }
}

uint8_t CrossPointSettings::getReadingStatsAutoBackupIntervalDays() const {
  switch (readingStatsAutoBackup) {
    case READING_STATS_AUTOBACKUP_1_DAY:
      return 1;
    case READING_STATS_AUTOBACKUP_7_DAYS:
      return 7;
    case READING_STATS_AUTOBACKUP_14_DAYS:
      return 14;
    case READING_STATS_AUTOBACKUP_21_DAYS:
      return 21;
    case READING_STATS_AUTOBACKUP_OFF:
    default:
      return 0;
  }
}

uint8_t CrossPointSettings::getSyncDayReminderStartThreshold() const {
  switch (syncDayReminderStarts) {
    case SYNC_DAY_REMINDER_OFF:
    default:
      return 0;
    case SYNC_DAY_REMINDER_10:
      return 10;
    case SYNC_DAY_REMINDER_20:
      return 20;
    case SYNC_DAY_REMINDER_30:
      return 30;
    case SYNC_DAY_REMINDER_40:
      return 40;
    case SYNC_DAY_REMINDER_50:
      return 50;
    case SYNC_DAY_REMINDER_60:
      return 60;
  }
}

bool CrossPointSettings::isHardwareRtcAutoDayClockActive() const {
  return halClock.isAvailable() && statusBarClock != STATUS_BAR_CLOCK_HIDE;
}

bool CrossPointSettings::shouldShowHeaderDate() const {
  if (!isHardwareRtcAutoDayClockActive()) {
    // Legacy X4 boolean mode, or X3 while the RTC/status-bar clock is inactive: show the
    // reading-stats date only for explicit date-on. Time/both modes stay stored but hidden
    // until isHardwareRtcAutoDayClockActive() becomes true again.
    if (displayDay >= DISPLAY_HEADER_TIME_ONLY) {
      return false;
    }
    return displayDay != DISPLAY_HEADER_OFF;
  }
  return displayDay == DISPLAY_HEADER_DATE_ONLY || displayDay == DISPLAY_HEADER_BOTH;
}

bool CrossPointSettings::shouldShowHeaderTime() const {
  if (!isHardwareRtcAutoDayClockActive()) {
    return false;
  }
  return displayDay == DISPLAY_HEADER_TIME_ONLY || displayDay == DISPLAY_HEADER_BOTH;
}

void CrossPointSettings::normalizeDisplayDay() {
  if (displayDay >= DISPLAY_HEADER_MODE_COUNT) {
    displayDay = DISPLAY_HEADER_DATE_ONLY;
  }
}

uint8_t CrossPointSettings::getEffectiveSyncDayReminderStartThreshold() const {
  if (isHardwareRtcAutoDayClockActive()) {
    return 0;
  }
  return getSyncDayReminderStartThreshold();
}

int CrossPointSettings::getRefreshFrequency() const {
  switch (refreshFrequency) {
    case REFRESH_1:
      return 1;
    case REFRESH_5:
      return 5;
    case REFRESH_10:
      return 10;
    case REFRESH_15:
    default:
      return 15;
    case REFRESH_30:
      return 30;
    case REFRESH_NEVER:
      // Effectively disables the periodic full refresh; the page counter
      // never reaches the threshold in practice.
      return std::numeric_limits<int>::max();
  }
}

void CrossPointSettings::clearSdFontFamily() {
  sdFontFamilyName[0] = '\0';
  fontPointSize =
      snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, std::size(BUILTIN_READER_POINT_SIZES), fontPointSize);
  saveToFile();
}

bool CrossPointSettings::getForcedReaderRefreshMode(HalDisplay::RefreshMode& mode) const {
  switch (readerRefreshMode) {
    case READER_REFRESH_FAST:
      mode = HalDisplay::FAST_REFRESH;
      return true;
    case READER_REFRESH_HALF:
      mode = HalDisplay::HALF_REFRESH;
      return true;
    case READER_REFRESH_FULL:
      mode = HalDisplay::FULL_REFRESH;
      return true;
    case READER_REFRESH_AUTO:
    default:
      return false;
  }
}

int CrossPointSettings::getReaderFontId() const {
  // Check SD card font first
  if (sdFontFamilyName[0] != '\0' && sdFontIdResolver) {
    const int id = sdFontIdResolver(sdFontResolverCtx, sdFontFamilyName, fontPointSize);
    if (id != 0) {
      return id;
    }
    // Fall through to built-in if SD font not found
  }

  // A built-in family only exists at BUILTIN_READER_POINT_SIZES, so a size
  // carried over from an SD family may not be one of them. ensureLoaded()
  // normally persists the snap; snap again here (without allocating - this runs
  // in the page render loop) so rendering is correct even before it has run.
  const uint8_t pt =
      snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, std::size(BUILTIN_READER_POINT_SIZES), fontPointSize);
  const bool sans = (fontFamily == NOTOSANS);
  switch (pt) {
    case 10:
      return sans ? NOTOSANS_10_FONT_ID : BOOKERLY_10_FONT_ID;
    case 12:
      return sans ? NOTOSANS_12_FONT_ID : BOOKERLY_12_FONT_ID;
    case 16:
      return sans ? NOTOSANS_16_FONT_ID : BOOKERLY_16_FONT_ID;
    case 18:
      return sans ? NOTOSANS_18_FONT_ID : BOOKERLY_18_FONT_ID;
    case 14:
    default:
      return sans ? NOTOSANS_14_FONT_ID : BOOKERLY_14_FONT_ID;
  }
}
