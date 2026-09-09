#include "JsonSettingsIO.h"

#include <ArduinoJson.h>
#include <CredentialIntegrity.h>
#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>
#include <Stream.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "AchievementsStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "FavoritesStore.h"
#include "KOReaderCredentialStore.h"
#include "OpdsServerStore.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "SettingsList.h"
#include "WifiCredentialStore.h"
#include "activities/util/KeyboardLayoutSet.h"
#include "util/AtomicFileReplace.h"
#include "util/BookIdentity.h"
#include "util/CprVcodexLogs.h"
#include "util/ShortcutRegistry.h"
#include "util/TimeZoneRegistry.h"

namespace {
constexpr uint8_t FONT_FAMILY_SCHEMA_VERSION = 3;
constexpr uint8_t FONT_SIZE_SCHEMA_VERSION = 2;
constexpr uint8_t UI_THEME_SCHEMA_VERSION = 3;
constexpr uint8_t TEXT_DARKNESS_SCHEMA_VERSION = 2;
constexpr uint8_t FLASHCARD_STUDY_MODE_SCHEMA_VERSION = 2;
constexpr uint8_t LEGACY_LEXEND_FONT_FAMILY = 2;
constexpr char LEXEND_SD_FAMILY_NAME[] = "Lexend";

class HalFileStream : public Stream {
 public:
  explicit HalFileStream(HalFile& file) : file(file) {}

  int available() override { return file.available() + (peekedByte >= 0 ? 1 : 0); }

  int read() override {
    if (peekedByte >= 0) {
      const int byte = peekedByte;
      peekedByte = -1;
      return byte;
    }
    return file.read();
  }

  int peek() override {
    if (peekedByte < 0) {
      peekedByte = file.read();
    }
    return peekedByte;
  }

  void flush() override { file.flush(); }

  size_t write(uint8_t value) override { return file.write(value); }

 private:
  HalFile& file;
  int peekedByte = -1;
};

// Serializes one scalar at a time so large stores do not need a full
// JsonDocument in memory while the destination file is open. String values are
// linked only for the duration of serializeJson(), avoiding a second copy.
class JsonStreamWriter {
 public:
  explicit JsonStreamWriter(HalFile& file) : file_(file) {}

  void literal(const char* text) {
    if (!ok_ || !text) return;
    const size_t length = std::strlen(text);
    expectedBytes_ += length;
    const size_t written = file_.write(text, length);
    writtenBytes_ += written;
    if (written != length) ok_ = false;
  }

  void value(const std::string& text) { writeScalar(JsonString(text.data(), text.size(), true)); }

  template <typename T>
  void value(const T value) {
    writeScalar(value);
  }

  bool ok() const { return ok_; }
  size_t expectedBytes() const { return expectedBytes_; }
  size_t writtenBytes() const { return writtenBytes_; }

 private:
  template <typename T>
  void writeScalar(const T value) {
    if (!ok_) return;

    JsonDocument scalar;
    if (!scalar.set(value) || scalar.overflowed()) {
      ok_ = false;
      return;
    }

    const size_t expected = measureJson(scalar);
    const size_t written = serializeJson(scalar, file_);
    expectedBytes_ += expected;
    writtenBytes_ += written;
    if (written != expected) ok_ = false;
  }

  HalFile& file_;
  size_t expectedBytes_ = 0;
  size_t writtenBytes_ = 0;
  bool ok_ = true;
};

bool promoteJsonTempFile(const char* moduleName, const char* tempPath, const char* targetPath,
                         const size_t expectedSize) {
  // Reopen after close to reject a short/failed SD write before touching the
  // committed file. This is a save path, never a render/input hot path.
  HalFile verification;
  if (!Storage.openFileForRead(moduleName, tempPath, verification)) return false;
  const bool complete = expectedSize > 0 && verification.fileSize64() == expectedSize;
  verification.close();
  if (!complete) {
    LOG_ERR(moduleName, "Refused incomplete JSON replacement: %s", tempPath);
    return false;
  }
  // .bak already has application-specific semantics for reading statistics.
  // Use a separate transaction backup without changing any existing formats.
  const std::string previous = std::string(targetPath) + ".previous";
  if (!AtomicFileReplace::promote(Storage, tempPath, targetPath, previous.c_str())) {
    LOG_ERR(moduleName, "JSON replacement failed; preserved committed data: %s", targetPath);
    return false;
  }
  return true;
}

bool saveJsonDocumentToFile(const char* moduleName, const char* path, const JsonDocument& doc) {
  if (!path || !*path) {
    LOG_ERR(moduleName, "Missing JSON path for write");
    CPR_VCODEX_LOG_EVENT(moduleName, "Missing JSON path for write");
    return false;
  }

  const char* targetPath = path;
  char tempPath[256];
  const int tempPathLength = snprintf(tempPath, sizeof(tempPath), "%s.tmp", targetPath);
  if (tempPathLength < 0 || static_cast<size_t>(tempPathLength) >= sizeof(tempPath)) {
    LOG_ERR(moduleName, "JSON path is too long for atomic write: %s", targetPath);
    CPR_VCODEX_LOG_EVENT(moduleName, std::string("JSON path is too long for atomic write: ") + targetPath);
    return false;
  }

  if (doc.overflowed()) {
    LOG_ERR(moduleName, "JSON document overflowed before write: %s", targetPath);
    CPR_VCODEX_LOG_EVENT(moduleName, std::string("Refused to write overflowed JSON document: ") + targetPath);
    return false;
  }

  if (Storage.exists(tempPath)) {
    Storage.remove(tempPath);
  }

  HalFile file;
  if (!Storage.openFileForWrite(moduleName, tempPath, file)) {
    LOG_ERR(moduleName, "Could not open JSON file for write: %s", tempPath);
    CPR_VCODEX_LOG_EVENT(moduleName, std::string("Could not open JSON temp file for write: ") + tempPath);
    return false;
  }

  const size_t expected = measureJson(doc);
  const size_t written = serializeJson(doc, file);
  file.flush();
  const bool closed = file.close();
  if (!closed || written == 0 || written != expected) {
    Storage.remove(tempPath);
    LOG_ERR(moduleName, "Incomplete JSON write for %s: %u/%u bytes", targetPath, static_cast<unsigned>(written),
            static_cast<unsigned>(expected));
    CPR_VCODEX_LOG_EVENT(moduleName, std::string("Incomplete JSON write for ") + targetPath + ": " +
                                         std::to_string(written) + "/" + std::to_string(expected) + " bytes");
    return false;
  }

  return promoteJsonTempFile(moduleName, tempPath, targetPath, expected);
}

bool loadJsonDocumentFromFile(const char* moduleName, const char* path, JsonDocument& doc) {
  HalFile file;
  if (!Storage.openFileForRead(moduleName, path, file)) {
    LOG_ERR(moduleName, "Could not open JSON file for read: %s", path);
    if (Storage.exists(path)) {
      CPR_VCODEX_LOG_EVENT(moduleName, std::string("Could not open JSON file for read: ") + path);
    }
    return false;
  }

  HalFileStream stream(file);
  auto error = deserializeJson(doc, stream);
  file.close();
  if (error || doc.overflowed()) {
    const char* message = error ? error.c_str() : "document overflow";
    LOG_ERR(moduleName, "JSON parse error: %s", message);
#ifndef CPR_DISABLE_EVENT_LOGS
    const std::string reportBody =
        std::string("File: ") + path + "\nModule: " + moduleName + "\nError: " + message + "\n";
    std::string outPath;
    if (CPR_VCODEX_WRITE_REPORT("json_error", reportBody, &outPath)) {
      CPR_VCODEX_LOG_EVENT(moduleName, std::string("Saved JSON parse error report to ") + outPath);
    }
#endif
    return false;
  }
  return true;
}

uint8_t migrateStoredUiTheme(const uint8_t rawUiTheme, const uint8_t schemaVersion, const uint8_t currentDefault,
                             bool* needsResave) {
  if (schemaVersion >= UI_THEME_SCHEMA_VERSION) {
    const uint8_t clampedTheme =
        rawUiTheme < static_cast<uint8_t>(CrossPointSettings::UI_THEME_COUNT) ? rawUiTheme : currentDefault;
    if (clampedTheme != rawUiTheme && needsResave) *needsResave = true;
    return clampedTheme;
  }

  // Legacy/theme-consolidation migration:
  // - 0 (Classic) -> Lyra
  // - 2/3 (Extended/Custom) -> Lyra vCodex
  // - 1 is ambiguous: in the current 2-theme schema it already means Lyra vCodex,
  //   while in older schemas it meant Lyra. Prefer preserving the newer stored value.
  uint8_t migratedTheme = currentDefault;
  switch (rawUiTheme) {
    case 0:
      migratedTheme = CrossPointSettings::LYRA;
      break;
    case 1:
      migratedTheme = CrossPointSettings::LYRA_CUSTOM;
      break;
    case 2:
    case 3:
      migratedTheme = CrossPointSettings::LYRA_CUSTOM;
      break;
    default:
      migratedTheme = currentDefault;
      break;
  }

  if (migratedTheme != rawUiTheme && needsResave) *needsResave = true;
  return migratedTheme;
}

uint8_t migrateStoredFlashcardStudyMode(const uint8_t rawMode, const uint8_t schemaVersion,
                                        const uint8_t currentDefault, bool* needsResave) {
  if (schemaVersion >= FLASHCARD_STUDY_MODE_SCHEMA_VERSION) {
    const uint8_t clampedMode =
        rawMode < static_cast<uint8_t>(CrossPointSettings::FLASHCARD_STUDY_MODE_COUNT) ? rawMode : currentDefault;
    if (clampedMode != rawMode && needsResave) *needsResave = true;
    return clampedMode;
  }

  // Legacy mapping before Due existed:
  // - 0 -> Scheduled
  // - 1 -> Infinite
  uint8_t migratedMode = currentDefault;
  switch (rawMode) {
    case 0:
      migratedMode = CrossPointSettings::FLASHCARD_STUDY_SCHEDULED;
      break;
    case 1:
      migratedMode = CrossPointSettings::FLASHCARD_STUDY_INFINITE;
      break;
    default:
      migratedMode = currentDefault;
      break;
  }

  if (migratedMode != rawMode && needsResave) *needsResave = true;
  return migratedMode;
}

void migrateLegacyStatsShortcut(CrossPointSettings& settings, const JsonDocument& doc, bool* needsResave) {
  const bool hasLegacyStatsShortcut =
      !doc["statsShortcut"].isNull() || !doc["statsShortcutOrder"].isNull() || !doc["statsShortcutVisible"].isNull();
  if (!hasLegacyStatsShortcut) {
    return;
  }

  const bool legacyVisible = settings.statsShortcutVisible != 0;
  const auto legacyLocation = static_cast<CrossPointSettings::SHORTCUT_LOCATION>(settings.statsShortcut);
  if (legacyVisible &&
      (legacyLocation == CrossPointSettings::SHORTCUT_HOME || legacyLocation == CrossPointSettings::SHORTCUT_APPS)) {
    settings.readingStatsShortcut = settings.statsShortcut;
    settings.readingStatsShortcutOrder = settings.statsShortcutOrder;
    settings.readingStatsShortcutVisible = 1;
  }

  settings.statsShortcutVisible = 0;
  if (needsResave) *needsResave = true;
}
}  // namespace

// Convert legacy settings.
void applyLegacyStatusBarSettings(CrossPointSettings& settings) {
  switch (static_cast<CrossPointSettings::STATUS_BAR_MODE>(settings.statusBar)) {
    case CrossPointSettings::NONE:
      settings.statusBarChapterPageCount = 0;
      settings.statusBarBookProgressPercentage = 0;
      settings.statusBarProgressBar = CrossPointSettings::HIDE_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::HIDE_TITLE;
      settings.statusBarBattery = 0;
      break;
    case CrossPointSettings::NO_PROGRESS:
      settings.statusBarChapterPageCount = 0;
      settings.statusBarBookProgressPercentage = 0;
      settings.statusBarProgressBar = CrossPointSettings::HIDE_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::CHAPTER_TITLE;
      settings.statusBarBattery = 1;
      break;
    case CrossPointSettings::BOOK_PROGRESS_BAR:
      settings.statusBarChapterPageCount = 1;
      settings.statusBarBookProgressPercentage = 0;
      settings.statusBarProgressBar = CrossPointSettings::BOOK_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::CHAPTER_TITLE;
      settings.statusBarBattery = 1;
      break;
    case CrossPointSettings::ONLY_BOOK_PROGRESS_BAR:
      settings.statusBarChapterPageCount = 1;
      settings.statusBarBookProgressPercentage = 0;
      settings.statusBarProgressBar = CrossPointSettings::BOOK_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::HIDE_TITLE;
      settings.statusBarBattery = 0;
      break;
    case CrossPointSettings::CHAPTER_PROGRESS_BAR:
      settings.statusBarChapterPageCount = 0;
      settings.statusBarBookProgressPercentage = 1;
      settings.statusBarProgressBar = CrossPointSettings::CHAPTER_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::CHAPTER_TITLE;
      settings.statusBarBattery = 1;
      break;
    case CrossPointSettings::FULL:
    default:
      settings.statusBarChapterPageCount = 1;
      settings.statusBarBookProgressPercentage = 1;
      settings.statusBarProgressBar = CrossPointSettings::HIDE_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::CHAPTER_TITLE;
      settings.statusBarBattery = 1;
      break;
  }
}

namespace {
void migrateDisplayHeaderSettings(CrossPointSettings& s, const JsonDocument& doc, bool* needsResave) {
  if (doc["displayHeaderTime"].isNull()) {
    return;
  }

  const uint8_t headerTime = doc["displayHeaderTime"] | static_cast<uint8_t>(0);
  if (headerTime > 1) {
    return;
  }

  if (headerTime) {
    if (s.displayDay == CrossPointSettings::DISPLAY_HEADER_OFF) {
      s.displayDay = CrossPointSettings::DISPLAY_HEADER_TIME_ONLY;
    } else if (s.displayDay == CrossPointSettings::DISPLAY_HEADER_DATE_ONLY) {
      s.displayDay = CrossPointSettings::DISPLAY_HEADER_BOTH;
    }
  }

  if (needsResave) {
    *needsResave = true;
  }
}
}  // namespace

bool loadSettingsDirect(CrossPointSettings& s, const JsonDocument& doc, bool* needsResave) {
  auto clamp = [](uint8_t val, uint8_t maxVal, uint8_t def) -> uint8_t { return val < maxVal ? val : def; };
  auto loadToggle = [&](const char* key, uint8_t& field) {
    field = clamp(doc[key] | field, static_cast<uint8_t>(2), field);
  };
  auto loadEnum = [&](const char* key, uint8_t& field, const uint8_t count) {
    field = clamp(doc[key] | field, count, field);
  };
  auto loadValue = [&](const char* key, uint8_t& field, const uint8_t minValue, const uint8_t maxValue) {
    uint8_t value = doc[key] | field;
    if (value < minValue) {
      value = minValue;
    } else if (value > maxValue) {
      value = maxValue;
    }
    field = value;
  };
  auto loadString = [&](const char* key, char* dest, const size_t maxLen) {
    const std::string value = doc[key] | std::string(dest);
    strncpy(dest, value.c_str(), maxLen - 1);
    dest[maxLen - 1] = '\0';
  };

  if (doc["statusBarChapterPageCount"].isNull()) {
    applyLegacyStatusBarSettings(s);
  }

  loadEnum("sleepScreen", s.sleepScreen, CrossPointSettings::SLEEP_SCREEN_MODE_COUNT);
  loadEnum("sleepScreenCoverMode", s.sleepScreenCoverMode, CrossPointSettings::SLEEP_SCREEN_COVER_MODE_COUNT);
  loadEnum("sleepScreenCoverFilter", s.sleepScreenCoverFilter, CrossPointSettings::SLEEP_SCREEN_COVER_FILTER_COUNT);
  loadToggle("cleanSleepRefresh", s.cleanSleepRefresh);
  loadEnum("hideBatteryPercentage", s.hideBatteryPercentage, CrossPointSettings::HIDE_BATTERY_PERCENTAGE_COUNT);
  loadEnum("refreshFrequency", s.refreshFrequency, CrossPointSettings::REFRESH_FREQUENCY_COUNT);
  {
    const uint8_t rawUiTheme = doc["uiTheme"] | s.uiTheme;
    const uint8_t uiThemeSchemaVersion = doc["uiThemeSchemaVersion"] | static_cast<uint8_t>(0);
    s.uiTheme = migrateStoredUiTheme(rawUiTheme, uiThemeSchemaVersion, s.uiTheme, needsResave);
  }
  loadToggle("fadingFix", s.fadingFix);
  loadToggle("darkMode", s.darkMode);
  loadToggle("antiGhostingExperimental", s.antiGhostingExperimental);

  loadString("sdFontFamilyName", s.sdFontFamilyName, sizeof(s.sdFontFamilyName));
  const uint8_t rawFontFamily = doc["fontFamily"] | s.fontFamily;
  const uint8_t fontFamilySchemaVersion = doc["fontFamilySchemaVersion"] | static_cast<uint8_t>(0);
  if (fontFamilySchemaVersion < FONT_FAMILY_SCHEMA_VERSION && rawFontFamily == LEGACY_LEXEND_FONT_FAMILY &&
      s.sdFontFamilyName[0] == '\0') {
    s.fontFamily = CrossPointSettings::BOOKERLY;
    strncpy(s.sdFontFamilyName, LEXEND_SD_FAMILY_NAME, sizeof(s.sdFontFamilyName) - 1);
    s.sdFontFamilyName[sizeof(s.sdFontFamilyName) - 1] = '\0';
    if (needsResave) *needsResave = true;
  } else if (rawFontFamily >= static_cast<uint8_t>(CrossPointSettings::FONT_FAMILY_COUNT)) {
    s.fontFamily = CrossPointSettings::BOOKERLY;
    if (needsResave) *needsResave = true;
  } else {
    s.fontFamily = rawFontFamily;
  }
  if (fontFamilySchemaVersion < FONT_FAMILY_SCHEMA_VERSION && needsResave) {
    *needsResave = true;
  }

  loadEnum("fontSize", s.fontSize, CrossPointSettings::FONT_SIZE_COUNT);
  const uint8_t fontSizeSchemaVersion = doc["fontSizeSchemaVersion"] | static_cast<uint8_t>(0);
  if (fontSizeSchemaVersion < FONT_SIZE_SCHEMA_VERSION && !doc["fontSize"].isNull()) {
    const uint8_t legacyFontSize = doc["fontSize"] | static_cast<uint8_t>(CrossPointSettings::MEDIUM - 1);
    if (legacyFontSize < static_cast<uint8_t>(CrossPointSettings::EXTRA_LARGE)) {
      s.fontSize = static_cast<uint8_t>(legacyFontSize + 1);
      if (needsResave) *needsResave = true;
    }
  }

  loadEnum("lineSpacing", s.lineSpacing, CrossPointSettings::LINE_COMPRESSION_COUNT);
  loadEnum("wordSpacing", s.wordSpacing, 5);
  loadEnum("readerAutoPageTurn", s.readerAutoPageTurn, 5);
  loadValue("screenMargin", s.screenMargin, 5, 40);
  loadEnum("paragraphAlignment", s.paragraphAlignment, CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT);
  loadToggle("embeddedStyle", s.embeddedStyle);
  loadToggle("hyphenationEnabled", s.hyphenationEnabled);
  loadEnum("bionicReading", s.bionicReading, CrossPointSettings::BIONIC_READING_MODE_COUNT);
  loadEnum("orientation", s.orientation, CrossPointSettings::ORIENTATION_COUNT);
  loadToggle("extraParagraphSpacing", s.extraParagraphSpacing);
  loadToggle("forceParagraphIndents", s.forceParagraphIndents);
  loadToggle("textAntiAliasing", s.textAntiAliasing);
  {
    const uint8_t textDarknessSchemaVersion = doc["textDarknessSchemaVersion"] | static_cast<uint8_t>(0);
    const uint8_t rawTextDarkness = doc["textDarkness"] | s.textDarkness;
    if (textDarknessSchemaVersion < TEXT_DARKNESS_SCHEMA_VERSION && !doc["textDarkness"].isNull()) {
      if (rawTextDarkness == 1) {
        s.textDarkness = CrossPointSettings::TEXT_DARKNESS_DARK;
        if (needsResave) *needsResave = true;
      } else if (rawTextDarkness == 2) {
        s.textDarkness = CrossPointSettings::TEXT_DARKNESS_EXTRA_DARK;
        if (needsResave) *needsResave = true;
      } else {
        s.textDarkness = CrossPointSettings::TEXT_DARKNESS_NORMAL;
      }
    } else if (rawTextDarkness < static_cast<uint8_t>(CrossPointSettings::TEXT_DARKNESS_COUNT)) {
      s.textDarkness = rawTextDarkness;
    } else {
      s.textDarkness = CrossPointSettings::TEXT_DARKNESS_NORMAL;
      if (needsResave) *needsResave = true;
    }
  }
  loadEnum("readerRefreshMode", s.readerRefreshMode, CrossPointSettings::READER_REFRESH_MODE_COUNT);
  loadEnum("imageRendering", s.imageRendering, CrossPointSettings::IMAGE_RENDERING_COUNT);

  loadEnum("sideButtonLayout", s.sideButtonLayout, CrossPointSettings::SIDE_BUTTON_LAYOUT_COUNT);
  loadToggle("frontButtonFollowOrientation", s.frontButtonFollowOrientation);
  if (!doc["longPressButtonBehavior"].isNull()) {
    loadEnum("longPressButtonBehavior", s.longPressButtonBehavior,
             CrossPointSettings::LONG_PRESS_BUTTON_BEHAVIOR_COUNT);
  } else if (!doc["longPressChapterSkip"].isNull()) {
    s.longPressButtonBehavior = (doc["longPressChapterSkip"] | true) ? CrossPointSettings::LONG_PRESS_CHAPTER_SKIP
                                                                     : CrossPointSettings::LONG_PRESS_OFF;
    if (needsResave) *needsResave = true;
  }
  {
    const uint8_t rawShortPwrBtn = doc["shortPwrBtn"] | s.shortPwrBtn;
    if (rawShortPwrBtn < static_cast<uint8_t>(CrossPointSettings::SHORT_PWRBTN_COUNT)) {
      s.shortPwrBtn = rawShortPwrBtn;
    } else {
      s.shortPwrBtn = CrossPointSettings::IGNORE;
      if (needsResave) *needsResave = true;
    }
  }
  loadEnum("tiltPageTurn", s.tiltPageTurn, CrossPointSettings::TILT_PAGE_TURN_COUNT);
  s.keyboardLayouts = KeyboardLayoutSet::normalizeMask(doc["keyboardLayouts"] | s.keyboardLayouts);
  loadEnum("sleepTimeout", s.sleepTimeout, CrossPointSettings::SLEEP_TIMEOUT_COUNT);
  loadToggle("showHiddenFiles", s.showHiddenFiles);
  loadToggle("hideFileExtension", s.hideFileExtension);

  loadString("opdsServerUrl", s.opdsServerUrl, sizeof(s.opdsServerUrl));
  loadString("opdsUsername", s.opdsUsername, sizeof(s.opdsUsername));
  loadEnum("opdsFilenameFormat", s.opdsFilenameFormat, CrossPointSettings::OPDS_FILENAME_FORMAT_COUNT);
  loadToggle("koSyncAutoPullOnOpen", s.koSyncAutoPullOnOpen);
  loadToggle("koSyncAutoPushOnClose", s.koSyncAutoPushOnClose);
  {
    bool ok = false;
    std::string password = obfuscation::deobfuscateFromBase64(doc["opdsPassword_obf"] | "", &ok);
    if (!ok || password.empty()) {
      password = doc["opdsPassword"] | std::string(s.opdsPassword);
      if (password != s.opdsPassword && needsResave) *needsResave = true;
    }
    strncpy(s.opdsPassword, password.c_str(), sizeof(s.opdsPassword) - 1);
    s.opdsPassword[sizeof(s.opdsPassword) - 1] = '\0';
  }

  loadToggle("statusBarChapterPageCount", s.statusBarChapterPageCount);
  loadToggle("statusBarBookProgressPercentage", s.statusBarBookProgressPercentage);
  loadEnum("statusBarProgressBar", s.statusBarProgressBar, CrossPointSettings::STATUS_BAR_PROGRESS_BAR_COUNT);
  loadEnum("statusBarProgressBarThickness", s.statusBarProgressBarThickness,
           CrossPointSettings::STATUS_BAR_PROGRESS_BAR_THICKNESS_COUNT);
  loadEnum("statusBarTitle", s.statusBarTitle, CrossPointSettings::STATUS_BAR_TITLE_COUNT);
  loadToggle("statusBarBattery", s.statusBarBattery);
  loadEnum("xtcStatusBarMode", s.xtcStatusBarMode, CrossPointSettings::XTC_STATUS_BAR_MODE_COUNT);
  loadEnum("statusBarClock", s.statusBarClock, CrossPointSettings::STATUS_BAR_CLOCK_COUNT);
  loadEnum("clockFormat", s.clockFormat, static_cast<uint8_t>(2));
  loadToggle("clockHasBeenSynced", s.clockHasBeenSynced);

  using S = CrossPointSettings;
  s.frontButtonBack =
      clamp(doc["frontButtonBack"] | (uint8_t)S::FRONT_HW_BACK, S::FRONT_BUTTON_HARDWARE_COUNT, S::FRONT_HW_BACK);
  s.frontButtonConfirm = clamp(doc["frontButtonConfirm"] | (uint8_t)S::FRONT_HW_CONFIRM, S::FRONT_BUTTON_HARDWARE_COUNT,
                               S::FRONT_HW_CONFIRM);
  s.frontButtonLeft =
      clamp(doc["frontButtonLeft"] | (uint8_t)S::FRONT_HW_LEFT, S::FRONT_BUTTON_HARDWARE_COUNT, S::FRONT_HW_LEFT);
  s.frontButtonRight =
      clamp(doc["frontButtonRight"] | (uint8_t)S::FRONT_HW_RIGHT, S::FRONT_BUTTON_HARDWARE_COUNT, S::FRONT_HW_RIGHT);
  s.homeBookSource = clamp(doc["homeBookSource"] | s.homeBookSource, S::HOME_BOOK_SOURCE_COUNT, s.homeBookSource);
  s.displayDay = clamp(doc["displayDay"] | s.displayDay, S::DISPLAY_HEADER_MODE_COUNT, s.displayDay);
  migrateDisplayHeaderSettings(s, doc, needsResave);
  s.autoSyncDay = clamp(doc["autoSyncDay"] | s.autoSyncDay, static_cast<uint8_t>(2), s.autoSyncDay);
  s.syncDayWifiChoice =
      clamp(doc["syncDayWifiChoice"] | s.syncDayWifiChoice, S::SYNC_DAY_WIFI_CHOICE_COUNT, s.syncDayWifiChoice);
  s.syncDayReminderStarts = clamp(doc["syncDayReminderStarts"] | s.syncDayReminderStarts,
                                  S::SYNC_DAY_REMINDER_STARTS_COUNT, s.syncDayReminderStarts);
  {
    const std::string sleepDirectory = doc["sleepDirectory"] | std::string("");
    strncpy(s.sleepDirectory, sleepDirectory.c_str(), sizeof(s.sleepDirectory) - 1);
    s.sleepDirectory[sizeof(s.sleepDirectory) - 1] = '\0';
  }
  s.sleepImageOrder = clamp(doc["sleepImageOrder"] | static_cast<uint8_t>(S::SLEEP_IMAGE_SHUFFLE),
                            S::SLEEP_IMAGE_ORDER_COUNT, S::SLEEP_IMAGE_SHUFFLE);
  s.timeZonePreset =
      TimeZoneRegistry::clampPresetIndex(doc["timeZonePreset"] | TimeZoneRegistry::DEFAULT_TIME_ZONE_INDEX);
  s.dateFormat = clamp(doc["dateFormat"] | s.dateFormat, S::DATE_FORMAT_COUNT, s.dateFormat);
  s.dailyGoalTarget = clamp(doc["dailyGoalTarget"] | s.dailyGoalTarget, S::DAILY_GOAL_TARGET_COUNT, s.dailyGoalTarget);
  s.readingStatsAutoBackup = clamp(doc["readingStatsAutoBackup"] | s.readingStatsAutoBackup,
                                   S::READING_STATS_AUTOBACKUP_COUNT, s.readingStatsAutoBackup);
  {
    const uint8_t rawFlashcardStudyMode = doc["flashcardStudyMode"] | s.flashcardStudyMode;
    const uint8_t flashcardStudyModeSchemaVersion = doc["flashcardStudyModeSchemaVersion"] | static_cast<uint8_t>(0);
    s.flashcardStudyMode = migrateStoredFlashcardStudyMode(rawFlashcardStudyMode, flashcardStudyModeSchemaVersion,
                                                           s.flashcardStudyMode, needsResave);
  }
  s.flashcardSessionSize = clamp(doc["flashcardSessionSize"] | s.flashcardSessionSize, S::FLASHCARD_SESSION_SIZE_COUNT,
                                 s.flashcardSessionSize);
  s.showStatsAfterReading =
      clamp(doc["showStatsAfterReading"] | s.showStatsAfterReading, static_cast<uint8_t>(2), s.showStatsAfterReading);
  s.moveCompletedBooks =
      clamp(doc["moveCompletedBooks"] | s.moveCompletedBooks, static_cast<uint8_t>(2), s.moveCompletedBooks);
  s.achievementsEnabled =
      clamp(doc["achievementsEnabled"] | s.achievementsEnabled, static_cast<uint8_t>(2), s.achievementsEnabled);
  s.achievementPopups =
      clamp(doc["achievementPopups"] | s.achievementPopups, static_cast<uint8_t>(2), s.achievementPopups);

  const uint8_t shortcutLocationCount = S::SHORTCUT_LOCATION_COUNT;
  const uint8_t shortcutOrderCount = static_cast<uint8_t>(getShortcutDefinitions().size() + 1);
  s.appsHubShortcutOrder =
      clamp(doc["appsHubShortcutOrder"] | s.appsHubShortcutOrder, shortcutOrderCount, s.appsHubShortcutOrder);
  s.browseFilesShortcut =
      clamp(doc["browseFilesShortcut"] | s.browseFilesShortcut, shortcutLocationCount, s.browseFilesShortcut);
  s.browseFilesShortcutOrder = clamp(doc["browseFilesShortcutOrder"] | s.browseFilesShortcutOrder, shortcutOrderCount,
                                     s.browseFilesShortcutOrder);
  s.statsShortcut = clamp(doc["statsShortcut"] | s.statsShortcut, shortcutLocationCount, s.statsShortcut);
  s.statsShortcutOrder =
      clamp(doc["statsShortcutOrder"] | s.statsShortcutOrder, shortcutOrderCount, s.statsShortcutOrder);
  s.syncDayShortcut = clamp(doc["syncDayShortcut"] | s.syncDayShortcut, shortcutLocationCount, s.syncDayShortcut);
  s.syncDayShortcutOrder =
      clamp(doc["syncDayShortcutOrder"] | s.syncDayShortcutOrder, shortcutOrderCount, s.syncDayShortcutOrder);
  s.settingsShortcut = clamp(doc["settingsShortcut"] | s.settingsShortcut, shortcutLocationCount, s.settingsShortcut);
  s.settingsShortcutOrder =
      clamp(doc["settingsShortcutOrder"] | s.settingsShortcutOrder, shortcutOrderCount, s.settingsShortcutOrder);
  s.readingStatsShortcut =
      clamp(doc["readingStatsShortcut"] | s.readingStatsShortcut, shortcutLocationCount, s.readingStatsShortcut);
  s.readingStatsShortcutOrder = clamp(doc["readingStatsShortcutOrder"] | s.readingStatsShortcutOrder,
                                      shortcutOrderCount, s.readingStatsShortcutOrder);
  s.readingHeatmapShortcut =
      clamp(doc["readingHeatmapShortcut"] | s.readingHeatmapShortcut, shortcutLocationCount, s.readingHeatmapShortcut);
  s.readingHeatmapShortcutOrder = clamp(doc["readingHeatmapShortcutOrder"] | s.readingHeatmapShortcutOrder,
                                        shortcutOrderCount, s.readingHeatmapShortcutOrder);
  s.readingProfileShortcut =
      clamp(doc["readingProfileShortcut"] | s.readingProfileShortcut, shortcutLocationCount, s.readingProfileShortcut);
  s.readingProfileShortcutOrder = clamp(doc["readingProfileShortcutOrder"] | s.readingProfileShortcutOrder,
                                        shortcutOrderCount, s.readingProfileShortcutOrder);
  s.achievementsShortcut =
      clamp(doc["achievementsShortcut"] | s.achievementsShortcut, shortcutLocationCount, s.achievementsShortcut);
  s.achievementsShortcutOrder = clamp(doc["achievementsShortcutOrder"] | s.achievementsShortcutOrder,
                                      shortcutOrderCount, s.achievementsShortcutOrder);
  s.ifFoundShortcut = clamp(doc["ifFoundShortcut"] | s.ifFoundShortcut, shortcutLocationCount, s.ifFoundShortcut);
  s.ifFoundShortcutOrder =
      clamp(doc["ifFoundShortcutOrder"] | s.ifFoundShortcutOrder, shortcutOrderCount, s.ifFoundShortcutOrder);
  s.readMeShortcut = clamp(doc["readMeShortcut"] | s.readMeShortcut, shortcutLocationCount, s.readMeShortcut);
  s.readMeShortcutOrder =
      clamp(doc["readMeShortcutOrder"] | s.readMeShortcutOrder, shortcutOrderCount, s.readMeShortcutOrder);
  s.recentBooksShortcut =
      clamp(doc["recentBooksShortcut"] | s.recentBooksShortcut, shortcutLocationCount, s.recentBooksShortcut);
  s.recentBooksShortcutOrder = clamp(doc["recentBooksShortcutOrder"] | s.recentBooksShortcutOrder, shortcutOrderCount,
                                     s.recentBooksShortcutOrder);
  s.bookmarksShortcut =
      clamp(doc["bookmarksShortcut"] | s.bookmarksShortcut, shortcutLocationCount, s.bookmarksShortcut);
  s.bookmarksShortcutOrder =
      clamp(doc["bookmarksShortcutOrder"] | s.bookmarksShortcutOrder, shortcutOrderCount, s.bookmarksShortcutOrder);
  s.favoritesShortcut =
      clamp(doc["favoritesShortcut"] | s.favoritesShortcut, shortcutLocationCount, s.favoritesShortcut);
  s.favoritesShortcutOrder =
      clamp(doc["favoritesShortcutOrder"] | s.favoritesShortcutOrder, shortcutOrderCount, s.favoritesShortcutOrder);
  s.flashcardsShortcut =
      clamp(doc["flashcardsShortcut"] | s.flashcardsShortcut, shortcutLocationCount, s.flashcardsShortcut);
  s.flashcardsShortcutOrder =
      clamp(doc["flashcardsShortcutOrder"] | s.flashcardsShortcutOrder, shortcutOrderCount, s.flashcardsShortcutOrder);
  s.dictionaryShortcut =
      clamp(doc["dictionaryShortcut"] | s.dictionaryShortcut, shortcutLocationCount, s.dictionaryShortcut);
  s.dictionaryShortcutOrder =
      clamp(doc["dictionaryShortcutOrder"] | s.dictionaryShortcutOrder, shortcutOrderCount, s.dictionaryShortcutOrder);
  s.fileTransferShortcut =
      clamp(doc["fileTransferShortcut"] | s.fileTransferShortcut, shortcutLocationCount, s.fileTransferShortcut);
  s.fileTransferShortcutOrder = clamp(doc["fileTransferShortcutOrder"] | s.fileTransferShortcutOrder,
                                      shortcutOrderCount, s.fileTransferShortcutOrder);
  s.screenCleanShortcut =
      clamp(doc["screenCleanShortcut"] | s.screenCleanShortcut, shortcutLocationCount, s.screenCleanShortcut);
  s.screenCleanShortcutOrder = clamp(doc["screenCleanShortcutOrder"] | s.screenCleanShortcutOrder, shortcutOrderCount,
                                     s.screenCleanShortcutOrder);
  s.sleepShortcut = clamp(doc["sleepShortcut"] | s.sleepShortcut, shortcutLocationCount, s.sleepShortcut);
  s.sleepShortcutOrder =
      clamp(doc["sleepShortcutOrder"] | s.sleepShortcutOrder, shortcutOrderCount, s.sleepShortcutOrder);
  s.opdsBrowserShortcut =
      clamp(doc["opdsBrowserShortcut"] | s.opdsBrowserShortcut, shortcutLocationCount, s.opdsBrowserShortcut);
  s.opdsBrowserShortcutOrder = clamp(doc["opdsBrowserShortcutOrder"] | s.opdsBrowserShortcutOrder, shortcutOrderCount,
                                     s.opdsBrowserShortcutOrder);

  s.browseFilesShortcutVisible = clamp(doc["browseFilesShortcutVisible"] | s.browseFilesShortcutVisible,
                                       static_cast<uint8_t>(2), s.browseFilesShortcutVisible);
  s.statsShortcutVisible =
      clamp(doc["statsShortcutVisible"] | s.statsShortcutVisible, static_cast<uint8_t>(2), s.statsShortcutVisible);
  s.syncDayShortcutVisible = clamp(doc["syncDayShortcutVisible"] | s.syncDayShortcutVisible, static_cast<uint8_t>(2),
                                   s.syncDayShortcutVisible);
  s.settingsShortcutVisible = clamp(doc["settingsShortcutVisible"] | s.settingsShortcutVisible, static_cast<uint8_t>(2),
                                    s.settingsShortcutVisible);
  s.readingStatsShortcutVisible = clamp(doc["readingStatsShortcutVisible"] | s.readingStatsShortcutVisible,
                                        static_cast<uint8_t>(2), s.readingStatsShortcutVisible);
  s.readingHeatmapShortcutVisible = clamp(doc["readingHeatmapShortcutVisible"] | s.readingHeatmapShortcutVisible,
                                          static_cast<uint8_t>(2), s.readingHeatmapShortcutVisible);
  s.readingProfileShortcutVisible = clamp(doc["readingProfileShortcutVisible"] | s.readingProfileShortcutVisible,
                                          static_cast<uint8_t>(2), s.readingProfileShortcutVisible);
  s.achievementsShortcutVisible = clamp(doc["achievementsShortcutVisible"] | s.achievementsShortcutVisible,
                                        static_cast<uint8_t>(2), s.achievementsShortcutVisible);
  s.ifFoundShortcutVisible = clamp(doc["ifFoundShortcutVisible"] | s.ifFoundShortcutVisible, static_cast<uint8_t>(2),
                                   s.ifFoundShortcutVisible);
  s.readMeShortcutVisible =
      clamp(doc["readMeShortcutVisible"] | s.readMeShortcutVisible, static_cast<uint8_t>(2), s.readMeShortcutVisible);
  s.recentBooksShortcutVisible = clamp(doc["recentBooksShortcutVisible"] | s.recentBooksShortcutVisible,
                                       static_cast<uint8_t>(2), s.recentBooksShortcutVisible);
  s.bookmarksShortcutVisible = clamp(doc["bookmarksShortcutVisible"] | s.bookmarksShortcutVisible,
                                     static_cast<uint8_t>(2), s.bookmarksShortcutVisible);
  s.favoritesShortcutVisible = clamp(doc["favoritesShortcutVisible"] | s.favoritesShortcutVisible,
                                     static_cast<uint8_t>(2), s.favoritesShortcutVisible);
  s.flashcardsShortcutVisible = clamp(doc["flashcardsShortcutVisible"] | s.flashcardsShortcutVisible,
                                      static_cast<uint8_t>(2), s.flashcardsShortcutVisible);
  s.dictionaryShortcutVisible = clamp(doc["dictionaryShortcutVisible"] | s.dictionaryShortcutVisible,
                                      static_cast<uint8_t>(2), s.dictionaryShortcutVisible);
  s.fileTransferShortcutVisible = clamp(doc["fileTransferShortcutVisible"] | s.fileTransferShortcutVisible,
                                        static_cast<uint8_t>(2), s.fileTransferShortcutVisible);
  s.screenCleanShortcutVisible = clamp(doc["screenCleanShortcutVisible"] | s.screenCleanShortcutVisible,
                                       static_cast<uint8_t>(2), s.screenCleanShortcutVisible);
  s.sleepShortcutVisible =
      clamp(doc["sleepShortcutVisible"] | s.sleepShortcutVisible, static_cast<uint8_t>(2), s.sleepShortcutVisible);
  s.opdsBrowserShortcutVisible = clamp(doc["opdsBrowserShortcutVisible"] | s.opdsBrowserShortcutVisible,
                                       static_cast<uint8_t>(2), s.opdsBrowserShortcutVisible);

  migrateLegacyStatsShortcut(s, doc, needsResave);
  normalizeShortcutOrderSettings(s);
  CrossPointSettings::validateFrontButtonMapping(s);
  s.normalizeDisplayDay();

  LOG_DBG("CPS", "Settings loaded from file");
  return true;
}

// ---- CrossPointState ----

bool JsonSettingsIO::saveState(const CrossPointState& s, const char* path) {
  JsonDocument doc;
  doc["openEpubPath"] = s.openEpubPath;
  JsonArray recentArr = doc["recentSleepImages"].to<JsonArray>();
  for (int i = 0; i < CrossPointState::SLEEP_RECENT_COUNT; i++) recentArr.add(s.recentSleepImages[i]);
  doc["recentSleepPos"] = s.recentSleepPos;
  doc["recentSleepFill"] = s.recentSleepFill;
  doc["readerActivityLoadCount"] = s.readerActivityLoadCount;
  doc["lastSleepFromReader"] = s.lastSleepFromReader;
  doc["lastKnownValidTimestamp"] = s.lastKnownValidTimestamp;
  doc["lastReadingStatsBackupDayOrdinal"] = s.lastReadingStatsBackupDayOrdinal;
  doc["syncDayReminderStartCount"] = s.syncDayReminderStartCount;
  doc["syncDayReminderLatched"] = s.syncDayReminderLatched;
  JsonObject sync = doc["koReaderSyncSession"].to<JsonObject>();
  sync["active"] = s.koReaderSyncSession.active;
  sync["epubPath"] = s.koReaderSyncSession.epubPath;
  sync["spineIndex"] = s.koReaderSyncSession.spineIndex;
  sync["page"] = s.koReaderSyncSession.page;
  sync["totalPagesInSpine"] = s.koReaderSyncSession.totalPagesInSpine;
  sync["paragraphIndex"] = s.koReaderSyncSession.paragraphIndex;
  sync["hasParagraphIndex"] = s.koReaderSyncSession.hasParagraphIndex;
  sync["xhtmlSeekHint"] = s.koReaderSyncSession.xhtmlSeekHint;
  sync["hasLocalKoReaderPosition"] = s.koReaderSyncSession.hasLocalKoReaderPosition;
  sync["localKoReaderProgress"] = s.koReaderSyncSession.localKoReaderProgress;
  sync["localKoReaderPercentage"] = s.koReaderSyncSession.localKoReaderPercentage;
  sync["localChapterLabel"] = s.koReaderSyncSession.localChapterLabel;
  sync["intent"] = static_cast<uint8_t>(s.koReaderSyncSession.intent);
  sync["outcome"] = static_cast<uint8_t>(s.koReaderSyncSession.outcome);
  sync["resultSpineIndex"] = s.koReaderSyncSession.resultSpineIndex;
  sync["resultPage"] = s.koReaderSyncSession.resultPage;
  sync["resultParagraphIndex"] = s.koReaderSyncSession.resultParagraphIndex;
  sync["resultHasParagraphIndex"] = s.koReaderSyncSession.resultHasParagraphIndex;
  sync["resultListItemIndex"] = s.koReaderSyncSession.resultListItemIndex;
  sync["resultHasListItemIndex"] = s.koReaderSyncSession.resultHasListItemIndex;
  sync["exitToHomeAfterSync"] = s.koReaderSyncSession.exitToHomeAfterSync;
  sync["autoPullEpubPath"] = s.koReaderSyncSession.autoPullEpubPath;
  JsonObject jump = doc["pendingBookmarkJump"].to<JsonObject>();
  jump["active"] = s.pendingBookmarkJump.active;
  jump["bookPath"] = s.pendingBookmarkJump.bookPath;
  jump["spineIndex"] = s.pendingBookmarkJump.spineIndex;
  jump["pageNumber"] = s.pendingBookmarkJump.pageNumber;
  return saveJsonDocumentToFile("CPS", path, doc);
}

bool JsonSettingsIO::loadState(CrossPointState& s, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("CPS", "JSON parse error: %s", error.c_str());
    CPR_VCODEX_LOG_EVENT("CPS", std::string("Settings JSON parse error: ") + error.c_str());
    return false;
  }

  s.openEpubPath = doc["openEpubPath"] | std::string("");
  memset(s.recentSleepImages, 0, sizeof(s.recentSleepImages));
  JsonArrayConst recentArr = doc["recentSleepImages"];
  const int actualCount = recentArr.isNull() ? 0
                                             : std::min(static_cast<int>(recentArr.size()),
                                                        static_cast<int>(CrossPointState::SLEEP_RECENT_COUNT));
  for (int i = 0; i < actualCount; i++) s.recentSleepImages[i] = recentArr[i] | static_cast<uint16_t>(0);
  s.recentSleepPos = doc["recentSleepPos"] | static_cast<uint8_t>(0);
  if (s.recentSleepPos >= CrossPointState::SLEEP_RECENT_COUNT)
    s.recentSleepPos = actualCount > 0 ? s.recentSleepPos % CrossPointState::SLEEP_RECENT_COUNT : 0;
  s.recentSleepFill = doc["recentSleepFill"] | static_cast<uint8_t>(0);
  s.recentSleepFill = static_cast<uint8_t>(std::min(static_cast<int>(s.recentSleepFill), actualCount));
  if (s.recentSleepFill == 0 && !doc["lastSleepImage"].isNull()) {
    const uint8_t legacy = doc["lastSleepImage"] | static_cast<uint8_t>(UINT8_MAX);
    if (legacy != UINT8_MAX) s.pushRecentSleep(static_cast<uint16_t>(legacy));
  }
  s.readerActivityLoadCount = doc["readerActivityLoadCount"] | (uint8_t)0;
  s.lastSleepFromReader = doc["lastSleepFromReader"] | false;
  s.lastKnownValidTimestamp = doc["lastKnownValidTimestamp"] | static_cast<uint32_t>(0);
  s.lastReadingStatsBackupDayOrdinal = doc["lastReadingStatsBackupDayOrdinal"] | static_cast<uint32_t>(0);
  s.syncDayReminderStartCount = doc["syncDayReminderStartCount"] | (uint8_t)0;
  s.syncDayReminderLatched = doc["syncDayReminderLatched"] | false;
  {
    JsonObjectConst sync = doc["koReaderSyncSession"];
    if (!sync.isNull()) {
      s.koReaderSyncSession.active = sync["active"] | false;
      s.koReaderSyncSession.epubPath = sync["epubPath"] | std::string("");
      s.koReaderSyncSession.spineIndex = sync["spineIndex"] | 0;
      s.koReaderSyncSession.page = sync["page"] | 0;
      s.koReaderSyncSession.totalPagesInSpine = sync["totalPagesInSpine"] | 0;
      s.koReaderSyncSession.paragraphIndex = sync["paragraphIndex"] | static_cast<uint16_t>(0);
      s.koReaderSyncSession.hasParagraphIndex = sync["hasParagraphIndex"] | false;
      s.koReaderSyncSession.xhtmlSeekHint = sync["xhtmlSeekHint"] | static_cast<uint32_t>(0);
      s.koReaderSyncSession.hasLocalKoReaderPosition = sync["hasLocalKoReaderPosition"] | false;
      s.koReaderSyncSession.localKoReaderProgress = sync["localKoReaderProgress"] | std::string("");
      s.koReaderSyncSession.localKoReaderPercentage = sync["localKoReaderPercentage"] | 0.0f;
      s.koReaderSyncSession.localChapterLabel = sync["localChapterLabel"] | std::string("");
      s.koReaderSyncSession.intent = static_cast<KOReaderSyncIntentState>(sync["intent"] | static_cast<uint8_t>(0));
      s.koReaderSyncSession.outcome = static_cast<KOReaderSyncOutcomeState>(sync["outcome"] | static_cast<uint8_t>(0));
      s.koReaderSyncSession.resultSpineIndex = sync["resultSpineIndex"] | 0;
      s.koReaderSyncSession.resultPage = sync["resultPage"] | 0;
      s.koReaderSyncSession.resultParagraphIndex = sync["resultParagraphIndex"] | static_cast<uint16_t>(0);
      s.koReaderSyncSession.resultHasParagraphIndex = sync["resultHasParagraphIndex"] | false;
      s.koReaderSyncSession.resultListItemIndex = sync["resultListItemIndex"] | static_cast<uint16_t>(0);
      s.koReaderSyncSession.resultHasListItemIndex = sync["resultHasListItemIndex"] | false;
      s.koReaderSyncSession.exitToHomeAfterSync = sync["exitToHomeAfterSync"] | false;
      s.koReaderSyncSession.autoPullEpubPath = sync["autoPullEpubPath"] | std::string("");
    } else {
      s.koReaderSyncSession.clear();
    }
    JsonObjectConst jump = doc["pendingBookmarkJump"];
    if (!jump.isNull()) {
      s.pendingBookmarkJump.active = jump["active"] | false;
      s.pendingBookmarkJump.bookPath = jump["bookPath"] | std::string("");
      s.pendingBookmarkJump.spineIndex = jump["spineIndex"] | static_cast<uint16_t>(0);
      s.pendingBookmarkJump.pageNumber = jump["pageNumber"] | static_cast<uint16_t>(0);
    } else {
      s.pendingBookmarkJump.clear();
    }
  }
  return true;
}

bool JsonSettingsIO::recoverFile(const char* path) {
  if (!path || !*path) return false;
  const std::string previous = std::string(path) + ".previous";
  const bool recovered = AtomicFileReplace::recover(Storage, path, previous.c_str());
  if (!recovered && Storage.exists(previous.c_str())) {
    LOG_ERR("JSN", "Could not recover previous committed data: %s", path);
  }
  return recovered;
}

// ---- CrossPointSettings ----

bool JsonSettingsIO::saveBookReaderSettings(const ReaderPreferences& settings, const bool enabled, const char* path) {
  JsonDocument doc;
  doc["version"] = 1;
  doc["enabled"] = enabled;
#define CPR_SAVE_BOOK_FIELD(name, maximum) doc[#name] = settings.name;
  CPR_READER_PREFERENCE_FIELDS(CPR_SAVE_BOOK_FIELD)
#undef CPR_SAVE_BOOK_FIELD
  doc["sdFontFamilyName"] = settings.sdFontFamilyName;
  return saveJsonDocumentToFile("BRS", path, doc);
}

bool JsonSettingsIO::loadBookReaderSettings(ReaderPreferences& settings, bool& enabled, const char* path) {
  JsonDocument doc;
  if (!loadJsonDocumentFromFile("BRS", path, doc) || !doc.is<JsonObject>() || !doc["version"].is<unsigned>() ||
      doc["version"].as<unsigned>() != 1 || !doc["enabled"].is<bool>())
    return false;
  ReaderPreferences parsed = settings;
#define CPR_LOAD_BOOK_FIELD(name, maximum)                                               \
  if (!doc[#name].isNull()) {                                                            \
    if (!doc[#name].is<unsigned>() || doc[#name].as<unsigned>() > maximum) return false; \
    parsed.name = doc[#name].as<uint8_t>();                                              \
  }
  CPR_READER_PREFERENCE_FIELDS(CPR_LOAD_BOOK_FIELD)
#undef CPR_LOAD_BOOK_FIELD
  if (!doc["sdFontFamilyName"].isNull()) {
    if (!doc["sdFontFamilyName"].is<const char*>()) return false;
    const char* font = doc["sdFontFamilyName"].as<const char*>();
    if (std::strlen(font) >= sizeof(parsed.sdFontFamilyName)) return false;
    std::strcpy(parsed.sdFontFamilyName, font);
  }
  settings = parsed;
  enabled = doc["enabled"].as<bool>();
  return true;
}

bool JsonSettingsIO::saveSettings(const CrossPointSettings& s, const char* path) {
  JsonDocument doc;
  const ReaderPreferences reader = s.persistedReaderPreferences();

  doc["sleepScreen"] = s.sleepScreen;
  doc["sleepScreenCoverMode"] = s.sleepScreenCoverMode;
  doc["sleepScreenCoverFilter"] = s.sleepScreenCoverFilter;
  doc["cleanSleepRefresh"] = s.cleanSleepRefresh;
  doc["hideBatteryPercentage"] = s.hideBatteryPercentage;
  doc["refreshFrequency"] = reader.refreshFrequency;
  doc["uiTheme"] = s.uiTheme;
  doc["uiThemeSchemaVersion"] = UI_THEME_SCHEMA_VERSION;
  doc["fadingFix"] = reader.fadingFix;
  doc["darkMode"] = reader.darkMode;
  doc["antiGhostingExperimental"] = s.antiGhostingExperimental;

  doc["fontFamily"] = reader.fontFamily;
  doc["fontFamilySchemaVersion"] = FONT_FAMILY_SCHEMA_VERSION;
  if (reader.sdFontFamilyName[0] != '\0') {
    doc["sdFontFamilyName"] = reader.sdFontFamilyName;
  }
  doc["fontSize"] = reader.fontSize;
  doc["fontSizeSchemaVersion"] = FONT_SIZE_SCHEMA_VERSION;
  doc["lineSpacing"] = reader.lineSpacing;
  doc["wordSpacing"] = reader.wordSpacing;
  doc["screenMargin"] = reader.screenMargin;
  doc["paragraphAlignment"] = reader.paragraphAlignment;
  doc["embeddedStyle"] = reader.embeddedStyle;
  doc["hyphenationEnabled"] = reader.hyphenationEnabled;
  doc["bionicReading"] = reader.bionicReading;
  doc["orientation"] = reader.orientation;
  doc["extraParagraphSpacing"] = reader.extraParagraphSpacing;
  doc["forceParagraphIndents"] = reader.forceParagraphIndents;
  doc["textAntiAliasing"] = reader.textAntiAliasing;
  doc["textDarkness"] = reader.textDarkness;
  doc["textDarknessSchemaVersion"] = TEXT_DARKNESS_SCHEMA_VERSION;
  doc["readerRefreshMode"] = reader.readerRefreshMode;
  doc["imageRendering"] = reader.imageRendering;
  doc["readerAutoPageTurn"] = reader.readerAutoPageTurn;

  doc["sideButtonLayout"] = s.sideButtonLayout;
  doc["frontButtonFollowOrientation"] = s.frontButtonFollowOrientation;
  doc["longPressButtonBehavior"] = s.longPressButtonBehavior;
  doc["longPressChapterSkip"] = s.longPressButtonBehavior == CrossPointSettings::LONG_PRESS_CHAPTER_SKIP;
  doc["shortPwrBtn"] = s.shortPwrBtn;
  doc["keyboardLayouts"] = KeyboardLayoutSet::normalizeMask(s.keyboardLayouts);
  doc["tiltPageTurn"] = s.tiltPageTurn;

  doc["sleepTimeout"] = s.sleepTimeout;
  doc["showHiddenFiles"] = s.showHiddenFiles;
  doc["hideFileExtension"] = s.hideFileExtension;

  doc["displayDay"] = s.displayDay;
  doc["syncDayWifiChoice"] = s.syncDayWifiChoice;
  doc["syncDayReminderStarts"] = s.syncDayReminderStarts;
  doc["dateFormat"] = s.dateFormat;
  doc["dailyGoalTarget"] = s.dailyGoalTarget;
  doc["readingStatsAutoBackup"] = s.readingStatsAutoBackup;
  doc["flashcardStudyModeSchemaVersion"] = FLASHCARD_STUDY_MODE_SCHEMA_VERSION;
  doc["flashcardStudyMode"] = s.flashcardStudyMode;
  doc["flashcardSessionSize"] = s.flashcardSessionSize;
  doc["showStatsAfterReading"] = s.showStatsAfterReading;
  doc["moveCompletedBooks"] = s.moveCompletedBooks;
  doc["achievementsEnabled"] = s.achievementsEnabled;
  doc["achievementPopups"] = s.achievementPopups;

  doc["opdsServerUrl"] = s.opdsServerUrl;
  doc["opdsUsername"] = s.opdsUsername;
  doc["opdsPassword_obf"] = obfuscation::obfuscateToBase64(s.opdsPassword);
  doc["opdsFilenameFormat"] = s.opdsFilenameFormat;
  doc["koSyncAutoPullOnOpen"] = s.koSyncAutoPullOnOpen;
  doc["koSyncAutoPushOnClose"] = s.koSyncAutoPushOnClose;

  doc["statusBarChapterPageCount"] = s.statusBarChapterPageCount;
  doc["statusBarBookProgressPercentage"] = s.statusBarBookProgressPercentage;
  doc["statusBarProgressBar"] = s.statusBarProgressBar;
  doc["statusBarProgressBarThickness"] = s.statusBarProgressBarThickness;
  doc["statusBarTitle"] = s.statusBarTitle;
  doc["statusBarBattery"] = s.statusBarBattery;
  doc["xtcStatusBarMode"] = s.xtcStatusBarMode;
  doc["statusBarClock"] = s.statusBarClock;
  doc["clockFormat"] = s.clockFormat;
  doc["clockHasBeenSynced"] = s.clockHasBeenSynced;

  // Front button remap - managed by RemapFrontButtons sub-activity, not in SettingsList.
  doc["frontButtonBack"] = s.frontButtonBack;
  doc["frontButtonConfirm"] = s.frontButtonConfirm;
  doc["frontButtonLeft"] = s.frontButtonLeft;
  doc["frontButtonRight"] = s.frontButtonRight;
  doc["homeBookSource"] = s.homeBookSource;
  doc["autoSyncDay"] = s.autoSyncDay;
  doc["sleepDirectory"] = s.sleepDirectory;
  doc["sleepImageOrder"] = s.sleepImageOrder;
  doc["timeZonePreset"] = TimeZoneRegistry::clampPresetIndex(s.timeZonePreset);
  doc["appsHubShortcutOrder"] = s.appsHubShortcutOrder;
  doc["browseFilesShortcut"] = s.browseFilesShortcut;
  doc["browseFilesShortcutOrder"] = s.browseFilesShortcutOrder;
  doc["syncDayShortcut"] = s.syncDayShortcut;
  doc["syncDayShortcutOrder"] = s.syncDayShortcutOrder;
  doc["settingsShortcut"] = s.settingsShortcut;
  doc["settingsShortcutOrder"] = s.settingsShortcutOrder;
  doc["readingStatsShortcut"] = s.readingStatsShortcut;
  doc["readingStatsShortcutOrder"] = s.readingStatsShortcutOrder;
  doc["readingHeatmapShortcut"] = s.readingHeatmapShortcut;
  doc["readingHeatmapShortcutOrder"] = s.readingHeatmapShortcutOrder;
  doc["readingProfileShortcut"] = s.readingProfileShortcut;
  doc["readingProfileShortcutOrder"] = s.readingProfileShortcutOrder;
  doc["achievementsShortcut"] = s.achievementsShortcut;
  doc["achievementsShortcutOrder"] = s.achievementsShortcutOrder;
  doc["ifFoundShortcut"] = s.ifFoundShortcut;
  doc["ifFoundShortcutOrder"] = s.ifFoundShortcutOrder;
  doc["readMeShortcut"] = s.readMeShortcut;
  doc["readMeShortcutOrder"] = s.readMeShortcutOrder;
  doc["recentBooksShortcut"] = s.recentBooksShortcut;
  doc["recentBooksShortcutOrder"] = s.recentBooksShortcutOrder;
  doc["bookmarksShortcut"] = s.bookmarksShortcut;
  doc["bookmarksShortcutOrder"] = s.bookmarksShortcutOrder;
  doc["favoritesShortcut"] = s.favoritesShortcut;
  doc["favoritesShortcutOrder"] = s.favoritesShortcutOrder;
  doc["flashcardsShortcut"] = s.flashcardsShortcut;
  doc["flashcardsShortcutOrder"] = s.flashcardsShortcutOrder;
  doc["dictionaryShortcut"] = s.dictionaryShortcut;
  doc["dictionaryShortcutOrder"] = s.dictionaryShortcutOrder;
  doc["fileTransferShortcut"] = s.fileTransferShortcut;
  doc["fileTransferShortcutOrder"] = s.fileTransferShortcutOrder;
  doc["screenCleanShortcut"] = s.screenCleanShortcut;
  doc["screenCleanShortcutOrder"] = s.screenCleanShortcutOrder;
  doc["sleepShortcut"] = s.sleepShortcut;
  doc["sleepShortcutOrder"] = s.sleepShortcutOrder;
  doc["opdsBrowserShortcut"] = s.opdsBrowserShortcut;
  doc["opdsBrowserShortcutOrder"] = s.opdsBrowserShortcutOrder;
  doc["browseFilesShortcutVisible"] = s.browseFilesShortcutVisible;
  doc["syncDayShortcutVisible"] = s.syncDayShortcutVisible;
  doc["settingsShortcutVisible"] = s.settingsShortcutVisible;
  doc["readingStatsShortcutVisible"] = s.readingStatsShortcutVisible;
  doc["readingHeatmapShortcutVisible"] = s.readingHeatmapShortcutVisible;
  doc["readingProfileShortcutVisible"] = s.readingProfileShortcutVisible;
  doc["achievementsShortcutVisible"] = s.achievementsShortcutVisible;
  doc["ifFoundShortcutVisible"] = s.ifFoundShortcutVisible;
  doc["readMeShortcutVisible"] = s.readMeShortcutVisible;
  doc["recentBooksShortcutVisible"] = s.recentBooksShortcutVisible;
  doc["bookmarksShortcutVisible"] = s.bookmarksShortcutVisible;
  doc["favoritesShortcutVisible"] = s.favoritesShortcutVisible;
  doc["flashcardsShortcutVisible"] = s.flashcardsShortcutVisible;
  doc["dictionaryShortcutVisible"] = s.dictionaryShortcutVisible;
  doc["fileTransferShortcutVisible"] = s.fileTransferShortcutVisible;
  doc["screenCleanShortcutVisible"] = s.screenCleanShortcutVisible;
  doc["sleepShortcutVisible"] = s.sleepShortcutVisible;
  doc["opdsBrowserShortcutVisible"] = s.opdsBrowserShortcutVisible;

  return saveJsonDocumentToFile("CPS", path, doc);
}

bool JsonSettingsIO::loadSettings(CrossPointSettings& s, const char* json, bool* needsResave) {
  if (needsResave) *needsResave = false;
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("CPS", "JSON parse error: %s", error.c_str());
    CPR_VCODEX_LOG_EVENT("CPS", std::string("State JSON parse error: ") + error.c_str());
    return false;
  }

  return loadSettingsDirect(s, doc, needsResave);

  auto clamp = [](uint8_t val, uint8_t maxVal, uint8_t def) -> uint8_t { return val < maxVal ? val : def; };

  // Legacy migration: if statusBarChapterPageCount is absent this is a pre-refactor settings file.
  // Populate s with migrated values now so the generic loop below picks them up as defaults and clamps them.
  if (doc["statusBarChapterPageCount"].isNull()) {
    applyLegacyStatusBarSettings(s);
  }

  for (const auto& info : getSettingsList()) {
    if (!info.key) continue;
    // Dynamic entries (KOReader etc.) are stored in their own files - skip.
    if (!info.valuePtr && !info.stringOffset) continue;

    if (info.stringOffset) {
      const char* strPtr = (const char*)&s + info.stringOffset;
      const std::string fieldDefault = strPtr;  // current buffer = struct-initializer default
      std::string val;
      if (info.obfuscated) {
        bool ok = false;
        val = obfuscation::deobfuscateFromBase64(doc[std::string(info.key) + "_obf"] | "", &ok);
        if (!ok || val.empty()) {
          val = doc[info.key] | fieldDefault;
          if (val != fieldDefault && needsResave) *needsResave = true;
        }
      } else {
        val = doc[info.key] | fieldDefault;
      }
      char* destPtr = (char*)&s + info.stringOffset;
      if (info.stringMaxLen == 0) {
        LOG_ERR("CPS", "Misconfigured SettingInfo: stringMaxLen is 0 for key '%s'", info.key);
        destPtr[0] = '\0';
        if (needsResave) *needsResave = true;
        continue;
      }
      strncpy(destPtr, val.c_str(), info.stringMaxLen - 1);
      destPtr[info.stringMaxLen - 1] = '\0';
    } else {
      const uint8_t fieldDefault = s.*(info.valuePtr);  // struct-initializer default, read before we overwrite it
      uint8_t v = doc[info.key] | fieldDefault;
      if (info.type == SettingType::ENUM) {
        v = clamp(v, (uint8_t)info.enumValues.size(), fieldDefault);
      } else if (info.type == SettingType::TOGGLE) {
        v = clamp(v, (uint8_t)2, fieldDefault);
      } else if (info.type == SettingType::VALUE) {
        if (v < info.valueRange.min)
          v = info.valueRange.min;
        else if (v > info.valueRange.max)
          v = info.valueRange.max;
      }
      s.*(info.valuePtr) = v;
    }
  }

  // Front button remap - managed by RemapFrontButtons sub-activity, not in SettingsList.
  const uint8_t fontSizeSchemaVersion = doc["fontSizeSchemaVersion"] | static_cast<uint8_t>(0);
  if (fontSizeSchemaVersion < FONT_SIZE_SCHEMA_VERSION && !doc["fontSize"].isNull()) {
    const uint8_t legacyFontSize = doc["fontSize"] | static_cast<uint8_t>(CrossPointSettings::MEDIUM - 1);
    if (legacyFontSize < static_cast<uint8_t>(CrossPointSettings::EXTRA_LARGE)) {
      s.fontSize = static_cast<uint8_t>(legacyFontSize + 1);
      if (needsResave) *needsResave = true;
    }
  }

  const uint8_t rawFontFamily = doc["fontFamily"] | s.fontFamily;
  if (rawFontFamily >= static_cast<uint8_t>(CrossPointSettings::FONT_FAMILY_COUNT)) {
    s.fontFamily = CrossPointSettings::BOOKERLY;
    if (needsResave) *needsResave = true;
  } else {
    s.fontFamily = rawFontFamily;
  }

  using S = CrossPointSettings;
  s.frontButtonBack =
      clamp(doc["frontButtonBack"] | (uint8_t)S::FRONT_HW_BACK, S::FRONT_BUTTON_HARDWARE_COUNT, S::FRONT_HW_BACK);
  s.frontButtonConfirm = clamp(doc["frontButtonConfirm"] | (uint8_t)S::FRONT_HW_CONFIRM, S::FRONT_BUTTON_HARDWARE_COUNT,
                               S::FRONT_HW_CONFIRM);
  s.frontButtonLeft =
      clamp(doc["frontButtonLeft"] | (uint8_t)S::FRONT_HW_LEFT, S::FRONT_BUTTON_HARDWARE_COUNT, S::FRONT_HW_LEFT);
  s.frontButtonRight =
      clamp(doc["frontButtonRight"] | (uint8_t)S::FRONT_HW_RIGHT, S::FRONT_BUTTON_HARDWARE_COUNT, S::FRONT_HW_RIGHT);
  s.displayDay = clamp(doc["displayDay"] | s.displayDay, S::DISPLAY_HEADER_MODE_COUNT, s.displayDay);
  migrateDisplayHeaderSettings(s, doc, needsResave);
  s.autoSyncDay = clamp(doc["autoSyncDay"] | s.autoSyncDay, static_cast<uint8_t>(2), s.autoSyncDay);
  s.syncDayWifiChoice =
      clamp(doc["syncDayWifiChoice"] | s.syncDayWifiChoice, S::SYNC_DAY_WIFI_CHOICE_COUNT, s.syncDayWifiChoice);
  s.syncDayReminderStarts = clamp(doc["syncDayReminderStarts"] | s.syncDayReminderStarts,
                                  S::SYNC_DAY_REMINDER_STARTS_COUNT, s.syncDayReminderStarts);
  {
    const std::string sleepDirectory = doc["sleepDirectory"] | std::string("");
    strncpy(s.sleepDirectory, sleepDirectory.c_str(), sizeof(s.sleepDirectory) - 1);
    s.sleepDirectory[sizeof(s.sleepDirectory) - 1] = '\0';
  }
  s.sleepImageOrder = clamp(doc["sleepImageOrder"] | static_cast<uint8_t>(S::SLEEP_IMAGE_SHUFFLE),
                            S::SLEEP_IMAGE_ORDER_COUNT, S::SLEEP_IMAGE_SHUFFLE);
  s.timeZonePreset =
      TimeZoneRegistry::clampPresetIndex(doc["timeZonePreset"] | TimeZoneRegistry::DEFAULT_TIME_ZONE_INDEX);
  s.dateFormat = clamp(doc["dateFormat"] | s.dateFormat, S::DATE_FORMAT_COUNT, s.dateFormat);
  s.opdsFilenameFormat =
      clamp(doc["opdsFilenameFormat"] | s.opdsFilenameFormat, S::OPDS_FILENAME_FORMAT_COUNT, s.opdsFilenameFormat);
  s.koSyncAutoPullOnOpen =
      clamp(doc["koSyncAutoPullOnOpen"] | s.koSyncAutoPullOnOpen, static_cast<uint8_t>(2), s.koSyncAutoPullOnOpen);
  s.koSyncAutoPushOnClose =
      clamp(doc["koSyncAutoPushOnClose"] | s.koSyncAutoPushOnClose, static_cast<uint8_t>(2), s.koSyncAutoPushOnClose);
  s.dailyGoalTarget = clamp(doc["dailyGoalTarget"] | s.dailyGoalTarget, S::DAILY_GOAL_TARGET_COUNT, s.dailyGoalTarget);
  {
    const uint8_t rawFlashcardStudyMode = doc["flashcardStudyMode"] | s.flashcardStudyMode;
    const uint8_t flashcardStudyModeSchemaVersion = doc["flashcardStudyModeSchemaVersion"] | static_cast<uint8_t>(0);
    s.flashcardStudyMode = migrateStoredFlashcardStudyMode(rawFlashcardStudyMode, flashcardStudyModeSchemaVersion,
                                                           s.flashcardStudyMode, nullptr);
  }
  s.flashcardSessionSize = clamp(doc["flashcardSessionSize"] | s.flashcardSessionSize, S::FLASHCARD_SESSION_SIZE_COUNT,
                                 s.flashcardSessionSize);
  s.showStatsAfterReading =
      clamp(doc["showStatsAfterReading"] | s.showStatsAfterReading, static_cast<uint8_t>(2), s.showStatsAfterReading);
  s.achievementsEnabled =
      clamp(doc["achievementsEnabled"] | s.achievementsEnabled, static_cast<uint8_t>(2), s.achievementsEnabled);
  s.achievementPopups =
      clamp(doc["achievementPopups"] | s.achievementPopups, static_cast<uint8_t>(2), s.achievementPopups);

  const uint8_t shortcutLocationCount = S::SHORTCUT_LOCATION_COUNT;
  const uint8_t shortcutOrderCount = static_cast<uint8_t>(getShortcutDefinitions().size() + 1);
  s.appsHubShortcutOrder =
      clamp(doc["appsHubShortcutOrder"] | s.appsHubShortcutOrder, shortcutOrderCount, s.appsHubShortcutOrder);
  s.browseFilesShortcut =
      clamp(doc["browseFilesShortcut"] | s.browseFilesShortcut, shortcutLocationCount, s.browseFilesShortcut);
  s.browseFilesShortcutOrder = clamp(doc["browseFilesShortcutOrder"] | s.browseFilesShortcutOrder, shortcutOrderCount,
                                     s.browseFilesShortcutOrder);
  s.statsShortcut = clamp(doc["statsShortcut"] | s.statsShortcut, shortcutLocationCount, s.statsShortcut);
  s.statsShortcutOrder =
      clamp(doc["statsShortcutOrder"] | s.statsShortcutOrder, shortcutOrderCount, s.statsShortcutOrder);
  s.syncDayShortcut = clamp(doc["syncDayShortcut"] | s.syncDayShortcut, shortcutLocationCount, s.syncDayShortcut);
  s.syncDayShortcutOrder =
      clamp(doc["syncDayShortcutOrder"] | s.syncDayShortcutOrder, shortcutOrderCount, s.syncDayShortcutOrder);
  s.settingsShortcut = clamp(doc["settingsShortcut"] | s.settingsShortcut, shortcutLocationCount, s.settingsShortcut);
  s.settingsShortcutOrder =
      clamp(doc["settingsShortcutOrder"] | s.settingsShortcutOrder, shortcutOrderCount, s.settingsShortcutOrder);
  s.readingStatsShortcut =
      clamp(doc["readingStatsShortcut"] | s.readingStatsShortcut, shortcutLocationCount, s.readingStatsShortcut);
  s.readingStatsShortcutOrder = clamp(doc["readingStatsShortcutOrder"] | s.readingStatsShortcutOrder,
                                      shortcutOrderCount, s.readingStatsShortcutOrder);
  s.readingHeatmapShortcut =
      clamp(doc["readingHeatmapShortcut"] | s.readingHeatmapShortcut, shortcutLocationCount, s.readingHeatmapShortcut);
  s.readingHeatmapShortcutOrder = clamp(doc["readingHeatmapShortcutOrder"] | s.readingHeatmapShortcutOrder,
                                        shortcutOrderCount, s.readingHeatmapShortcutOrder);
  s.readingProfileShortcut =
      clamp(doc["readingProfileShortcut"] | s.readingProfileShortcut, shortcutLocationCount, s.readingProfileShortcut);
  s.readingProfileShortcutOrder = clamp(doc["readingProfileShortcutOrder"] | s.readingProfileShortcutOrder,
                                        shortcutOrderCount, s.readingProfileShortcutOrder);
  s.achievementsShortcut =
      clamp(doc["achievementsShortcut"] | s.achievementsShortcut, shortcutLocationCount, s.achievementsShortcut);
  s.achievementsShortcutOrder = clamp(doc["achievementsShortcutOrder"] | s.achievementsShortcutOrder,
                                      shortcutOrderCount, s.achievementsShortcutOrder);
  s.ifFoundShortcut = clamp(doc["ifFoundShortcut"] | s.ifFoundShortcut, shortcutLocationCount, s.ifFoundShortcut);
  s.ifFoundShortcutOrder =
      clamp(doc["ifFoundShortcutOrder"] | s.ifFoundShortcutOrder, shortcutOrderCount, s.ifFoundShortcutOrder);
  s.readMeShortcut = clamp(doc["readMeShortcut"] | s.readMeShortcut, shortcutLocationCount, s.readMeShortcut);
  s.readMeShortcutOrder =
      clamp(doc["readMeShortcutOrder"] | s.readMeShortcutOrder, shortcutOrderCount, s.readMeShortcutOrder);
  s.recentBooksShortcut =
      clamp(doc["recentBooksShortcut"] | s.recentBooksShortcut, shortcutLocationCount, s.recentBooksShortcut);
  s.recentBooksShortcutOrder = clamp(doc["recentBooksShortcutOrder"] | s.recentBooksShortcutOrder, shortcutOrderCount,
                                     s.recentBooksShortcutOrder);
  s.bookmarksShortcut =
      clamp(doc["bookmarksShortcut"] | s.bookmarksShortcut, shortcutLocationCount, s.bookmarksShortcut);
  s.bookmarksShortcutOrder =
      clamp(doc["bookmarksShortcutOrder"] | s.bookmarksShortcutOrder, shortcutOrderCount, s.bookmarksShortcutOrder);
  s.favoritesShortcut =
      clamp(doc["favoritesShortcut"] | s.favoritesShortcut, shortcutLocationCount, s.favoritesShortcut);
  s.favoritesShortcutOrder =
      clamp(doc["favoritesShortcutOrder"] | s.favoritesShortcutOrder, shortcutOrderCount, s.favoritesShortcutOrder);
  s.flashcardsShortcut =
      clamp(doc["flashcardsShortcut"] | s.flashcardsShortcut, shortcutLocationCount, s.flashcardsShortcut);
  s.flashcardsShortcutOrder =
      clamp(doc["flashcardsShortcutOrder"] | s.flashcardsShortcutOrder, shortcutOrderCount, s.flashcardsShortcutOrder);
  s.fileTransferShortcut =
      clamp(doc["fileTransferShortcut"] | s.fileTransferShortcut, shortcutLocationCount, s.fileTransferShortcut);
  s.fileTransferShortcutOrder = clamp(doc["fileTransferShortcutOrder"] | s.fileTransferShortcutOrder,
                                      shortcutOrderCount, s.fileTransferShortcutOrder);
  s.screenCleanShortcut =
      clamp(doc["screenCleanShortcut"] | s.screenCleanShortcut, shortcutLocationCount, s.screenCleanShortcut);
  s.screenCleanShortcutOrder = clamp(doc["screenCleanShortcutOrder"] | s.screenCleanShortcutOrder, shortcutOrderCount,
                                     s.screenCleanShortcutOrder);
  s.sleepShortcut = clamp(doc["sleepShortcut"] | s.sleepShortcut, shortcutLocationCount, s.sleepShortcut);
  s.sleepShortcutOrder =
      clamp(doc["sleepShortcutOrder"] | s.sleepShortcutOrder, shortcutOrderCount, s.sleepShortcutOrder);
  s.opdsBrowserShortcut =
      clamp(doc["opdsBrowserShortcut"] | s.opdsBrowserShortcut, shortcutLocationCount, s.opdsBrowserShortcut);
  s.opdsBrowserShortcutOrder = clamp(doc["opdsBrowserShortcutOrder"] | s.opdsBrowserShortcutOrder, shortcutOrderCount,
                                     s.opdsBrowserShortcutOrder);

  s.browseFilesShortcutVisible = clamp(doc["browseFilesShortcutVisible"] | s.browseFilesShortcutVisible,
                                       static_cast<uint8_t>(2), s.browseFilesShortcutVisible);
  s.statsShortcutVisible =
      clamp(doc["statsShortcutVisible"] | s.statsShortcutVisible, static_cast<uint8_t>(2), s.statsShortcutVisible);
  s.syncDayShortcutVisible = clamp(doc["syncDayShortcutVisible"] | s.syncDayShortcutVisible, static_cast<uint8_t>(2),
                                   s.syncDayShortcutVisible);
  s.settingsShortcutVisible = clamp(doc["settingsShortcutVisible"] | s.settingsShortcutVisible, static_cast<uint8_t>(2),
                                    s.settingsShortcutVisible);
  s.readingStatsShortcutVisible = clamp(doc["readingStatsShortcutVisible"] | s.readingStatsShortcutVisible,
                                        static_cast<uint8_t>(2), s.readingStatsShortcutVisible);
  s.readingHeatmapShortcutVisible = clamp(doc["readingHeatmapShortcutVisible"] | s.readingHeatmapShortcutVisible,
                                          static_cast<uint8_t>(2), s.readingHeatmapShortcutVisible);
  s.readingProfileShortcutVisible = clamp(doc["readingProfileShortcutVisible"] | s.readingProfileShortcutVisible,
                                          static_cast<uint8_t>(2), s.readingProfileShortcutVisible);
  s.achievementsShortcutVisible = clamp(doc["achievementsShortcutVisible"] | s.achievementsShortcutVisible,
                                        static_cast<uint8_t>(2), s.achievementsShortcutVisible);
  s.ifFoundShortcutVisible = clamp(doc["ifFoundShortcutVisible"] | s.ifFoundShortcutVisible, static_cast<uint8_t>(2),
                                   s.ifFoundShortcutVisible);
  s.readMeShortcutVisible =
      clamp(doc["readMeShortcutVisible"] | s.readMeShortcutVisible, static_cast<uint8_t>(2), s.readMeShortcutVisible);
  s.recentBooksShortcutVisible = clamp(doc["recentBooksShortcutVisible"] | s.recentBooksShortcutVisible,
                                       static_cast<uint8_t>(2), s.recentBooksShortcutVisible);
  s.bookmarksShortcutVisible = clamp(doc["bookmarksShortcutVisible"] | s.bookmarksShortcutVisible,
                                     static_cast<uint8_t>(2), s.bookmarksShortcutVisible);
  s.favoritesShortcutVisible = clamp(doc["favoritesShortcutVisible"] | s.favoritesShortcutVisible,
                                     static_cast<uint8_t>(2), s.favoritesShortcutVisible);
  s.flashcardsShortcutVisible = clamp(doc["flashcardsShortcutVisible"] | s.flashcardsShortcutVisible,
                                      static_cast<uint8_t>(2), s.flashcardsShortcutVisible);
  s.fileTransferShortcutVisible = clamp(doc["fileTransferShortcutVisible"] | s.fileTransferShortcutVisible,
                                        static_cast<uint8_t>(2), s.fileTransferShortcutVisible);
  s.screenCleanShortcutVisible = clamp(doc["screenCleanShortcutVisible"] | s.screenCleanShortcutVisible,
                                       static_cast<uint8_t>(2), s.screenCleanShortcutVisible);
  s.sleepShortcutVisible =
      clamp(doc["sleepShortcutVisible"] | s.sleepShortcutVisible, static_cast<uint8_t>(2), s.sleepShortcutVisible);
  s.opdsBrowserShortcutVisible = clamp(doc["opdsBrowserShortcutVisible"] | s.opdsBrowserShortcutVisible,
                                       static_cast<uint8_t>(2), s.opdsBrowserShortcutVisible);

  normalizeShortcutOrderSettings(s);
  CrossPointSettings::validateFrontButtonMapping(s);

  LOG_DBG("CPS", "Settings loaded from file");

  return true;
}

// ---- KOReaderCredentialStore ----

bool JsonSettingsIO::saveKOReader(const KOReaderCredentialStore& store, const char* path) {
  JsonDocument doc;

  JsonArray arr = doc["profiles"].to<JsonArray>();
  for (const auto& profile : store.profiles) {
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = profile.name;
    obj["username"] = profile.username;
    obj["password_obf"] = obfuscation::obfuscateToBase64(profile.password);
    obj["serverUrl"] = profile.serverUrl;
    obj["matchMethod"] = static_cast<uint8_t>(profile.matchMethod);
    obj["sendMetadata"] = profile.sendMetadata;
    obj["syncBehavior"] = static_cast<uint8_t>(profile.syncBehavior);
  }
  doc["activeIndex"] = store.activeIndex;

  return saveJsonDocumentToFile("KRS", path, doc);
}

bool JsonSettingsIO::loadKOReader(KOReaderCredentialStore& store, const char* json, bool* needsResave) {
  if (needsResave) *needsResave = false;
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("KRS", "JSON parse error: %s", error.c_str());
    CPR_VCODEX_LOG_EVENT("KRS", std::string("KOReader JSON parse error: ") + error.c_str());
    return false;
  }

  if (!doc["profiles"].isNull()) {
    // Current multi-profile format.
    store.profiles.clear();
    JsonArray arr = doc["profiles"].as<JsonArray>();
    for (JsonObject obj : arr) {
      if (store.profiles.size() >= KOReaderCredentialStore::MAX_PROFILES) break;
      KOReaderProfile profile;
      profile.name = obj["name"] | std::string("");
      profile.username = obj["username"] | std::string("");
      bool ok = false;
      profile.password = obfuscation::deobfuscateFromBase64(obj["password_obf"] | "", &ok);
      if (!ok || profile.password.empty()) {
        profile.password = obj["password"] | std::string("");
        if (!profile.password.empty() && needsResave) *needsResave = true;
      }
      profile.serverUrl = obj["serverUrl"] | std::string("");
      uint8_t method = obj["matchMethod"] | (uint8_t)0;
      profile.matchMethod = method <= static_cast<uint8_t>(DocumentMatchMethod::BINARY)
                                ? static_cast<DocumentMatchMethod>(method)
                                : DocumentMatchMethod::FILENAME;
      profile.sendMetadata = obj["sendMetadata"] | false;
      const uint8_t behavior = obj["syncBehavior"] | static_cast<uint8_t>(KOReaderSyncBehavior::ASK_EVERY_TIME);
      profile.syncBehavior = behavior <= static_cast<uint8_t>(KOReaderSyncBehavior::SMART)
                                 ? static_cast<KOReaderSyncBehavior>(behavior)
                                 : KOReaderSyncBehavior::ASK_EVERY_TIME;
      store.profiles.push_back(std::move(profile));
    }
    const int active = doc["activeIndex"] | 0;
    store.activeIndex = (active >= 0 && static_cast<size_t>(active) < store.profiles.size())
                            ? active
                            : (store.profiles.empty() ? -1 : 0);
  } else {
    // Legacy single-record format written before multi-profile support -- wrap it
    // into a single "Profile 1" and flag for resave so the file upgrades on disk.
    KOReaderProfile profile;
    profile.name = "Profile 1";
    profile.username = doc["username"] | std::string("");
    bool ok = false;
    profile.password = obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", &ok);
    if (!ok || profile.password.empty()) {
      profile.password = doc["password"] | std::string("");
    }
    profile.serverUrl = doc["serverUrl"] | std::string("");
    const uint8_t method = doc["matchMethod"] | static_cast<uint8_t>(DocumentMatchMethod::FILENAME);
    profile.matchMethod = method <= static_cast<uint8_t>(DocumentMatchMethod::BINARY)
                              ? static_cast<DocumentMatchMethod>(method)
                              : DocumentMatchMethod::FILENAME;
    profile.sendMetadata = doc["sendMetadata"] | false;
    const uint8_t behavior = doc["syncBehavior"] | static_cast<uint8_t>(KOReaderSyncBehavior::ASK_EVERY_TIME);
    profile.syncBehavior = behavior <= static_cast<uint8_t>(KOReaderSyncBehavior::SMART)
                               ? static_cast<KOReaderSyncBehavior>(behavior)
                               : KOReaderSyncBehavior::ASK_EVERY_TIME;

    store.profiles.clear();
    store.profiles.push_back(std::move(profile));
    store.activeIndex = 0;
    if (needsResave) *needsResave = true;
  }

  LOG_DBG("KRS", "Loaded %zu KOReader profile(s), active index %d", store.profiles.size(), store.activeIndex);
  return true;
}

// Legacy single-record mirror (koreader.json's original shape). Kept in sync with
// whichever profile is active so other firmware sharing the same SD card (e.g. stock
// crosspoint-reader, which only ever understood one KOReader account) keeps working
// unchanged -- it never sees the multi-profile store and has no reason to.
bool JsonSettingsIO::saveKOReaderLegacyMirror(const KOReaderCredentialStore& store, const char* path) {
  JsonDocument doc;
  doc["username"] = store.getUsername();
  doc["password_obf"] = obfuscation::obfuscateToBase64(store.getPassword());
  doc["serverUrl"] = store.getServerUrl();
  doc["matchMethod"] = static_cast<uint8_t>(store.getMatchMethod());
  doc["sendMetadata"] = store.getSendMetadata();
  doc["syncBehavior"] = static_cast<uint8_t>(store.getSyncBehavior());
  return saveJsonDocumentToFile("KRS", path, doc);
}

bool JsonSettingsIO::loadKOReaderLegacyProfile(KOReaderProfile& profile, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("KRS", "Legacy koreader.json parse error: %s", error.c_str());
    CPR_VCODEX_LOG_EVENT("KRS", std::string("Legacy koreader.json parse error: ") + error.c_str());
    return false;
  }

  profile.username = doc["username"] | std::string("");
  bool ok = false;
  profile.password = obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", &ok);
  if (!ok || profile.password.empty()) {
    profile.password = doc["password"] | std::string("");
  }
  profile.serverUrl = doc["serverUrl"] | std::string("");
  const uint8_t method = doc["matchMethod"] | static_cast<uint8_t>(DocumentMatchMethod::FILENAME);
  profile.matchMethod = method <= static_cast<uint8_t>(DocumentMatchMethod::BINARY)
                            ? static_cast<DocumentMatchMethod>(method)
                            : DocumentMatchMethod::FILENAME;
  profile.sendMetadata = doc["sendMetadata"] | false;
  const uint8_t behavior = doc["syncBehavior"] | static_cast<uint8_t>(KOReaderSyncBehavior::ASK_EVERY_TIME);
  profile.syncBehavior = behavior <= static_cast<uint8_t>(KOReaderSyncBehavior::SMART)
                             ? static_cast<KOReaderSyncBehavior>(behavior)
                             : KOReaderSyncBehavior::ASK_EVERY_TIME;

  LOG_DBG("KRS", "Loaded legacy KOReader credentials for user: %s", profile.username.c_str());
  return true;
}

// ---- WifiCredentialStore ----

bool JsonSettingsIO::saveWifi(const WifiCredentialStore& store, const char* path) {
  JsonDocument doc;
  {
    std::lock_guard<std::mutex> lock(store.credentialMutex);
    doc["lastConnectedSsid"] = store.lastConnectedSsid;

    JsonArray arr = doc["credentials"].to<JsonArray>();
    for (const auto& cred : store.credentials) {
      JsonObject obj = arr.add<JsonObject>();
      obj["ssid"] = cred.ssid;
      obj["password_obf"] = obfuscation::obfuscateToBase64(cred.password);
      obj["password_len"] = cred.password.size();
      obj["password_crc32"] = credential_integrity::crc32(cred.password);
    }
  }

  return saveJsonDocumentToFile("WCS", path, doc);
}

bool JsonSettingsIO::loadWifi(WifiCredentialStore& store, const char* json, bool* needsResave) {
  bool resave = false;
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("WCS", "JSON parse error: %s", error.c_str());
    CPR_VCODEX_LOG_EVENT("WCS", std::string("WiFi JSON parse error: ") + error.c_str());
    return false;
  }

  std::string loadedLastConnectedSsid = doc["lastConnectedSsid"] | std::string("");
  std::vector<WifiCredential> loadedCredentials;
  JsonArray arr = doc["credentials"].as<JsonArray>();
  loadedCredentials.reserve(std::min(arr.size(), store.MAX_NETWORKS));
  for (JsonObject obj : arr) {
    if (loadedCredentials.size() >= store.MAX_NETWORKS) break;
    WifiCredential cred;
    cred.ssid = obj["ssid"] | std::string("");

    const JsonVariant passwordLength = obj["password_len"];
    const bool hasPasswordLength = !passwordLength.isNull();
    size_t expectedLength = 0;
    if (hasPasswordLength) {
      if (!passwordLength.is<size_t>()) {
        LOG_ERR("WCS", "Discarding corrupted password for %s (invalid length)", cred.ssid.c_str());
        resave = true;
        continue;
      }
      expectedLength = passwordLength.as<size_t>();
      if (expectedLength > store.MAX_PASSWORD_LENGTH) {
        LOG_ERR("WCS", "Discarding oversized password for %s (%zu bytes)", cred.ssid.c_str(), expectedLength);
        resave = true;
        continue;
      }
    }

    bool ok = false;
    bool tooLong = false;
    cred.password =
        obfuscation::deobfuscateFromBase64(obj["password_obf"] | "", store.MAX_PASSWORD_LENGTH, &ok, &tooLong);
    if (tooLong) {
      LOG_ERR("WCS", "Discarding oversized password for %s", cred.ssid.c_str());
      resave = true;
      continue;
    }
    if (!ok) {
      const char* legacyPassword = obj["password"] | "";
      const size_t legacyLength = strlen(legacyPassword);
      if (legacyLength > store.MAX_PASSWORD_LENGTH) {
        LOG_ERR("WCS", "Discarding oversized legacy password for %s", cred.ssid.c_str());
        resave = true;
        continue;
      }
      cred.password.assign(legacyPassword, legacyLength);
      if (!cred.password.empty()) resave = true;
    }

    bool integrityValid = true;
    if (hasPasswordLength) {
      if (cred.password.size() != expectedLength) {
        LOG_ERR("WCS", "Discarding corrupted password for %s (expected %zu bytes, decoded %zu)", cred.ssid.c_str(),
                expectedLength, cred.password.size());
        integrityValid = false;
      }
    } else {
      resave = true;
    }

    const JsonVariant checksum = obj["password_crc32"];
    if (checksum.is<uint32_t>()) {
      if (credential_integrity::crc32(cred.password) != checksum.as<uint32_t>()) {
        LOG_ERR("WCS", "Discarding corrupted password for %s (checksum mismatch)", cred.ssid.c_str());
        integrityValid = false;
      }
    } else if (checksum.isNull()) {
      resave = true;
    } else {
      LOG_ERR("WCS", "Discarding corrupted password for %s (invalid checksum)", cred.ssid.c_str());
      integrityValid = false;
    }

    if (!integrityValid) {
      resave = true;
      continue;
    }
    loadedCredentials.push_back(std::move(cred));
  }

  const size_t loadedCount = loadedCredentials.size();
  {
    std::lock_guard<std::mutex> lock(store.credentialMutex);
    store.lastConnectedSsid = std::move(loadedLastConnectedSsid);
    store.credentials = std::move(loadedCredentials);
  }

  if (needsResave) *needsResave = resave;
  LOG_DBG("WCS", "Loaded %zu WiFi credentials from file", loadedCount);
  return true;
}

// ---- RecentBooksStore ----

bool JsonSettingsIO::saveRecentBooks(const RecentBooksStore& store, const char* path) {
  JsonDocument doc;
  doc["formatVersion"] = 2;
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& book : store.getBooks()) {
    JsonObject obj = arr.add<JsonObject>();
    obj["bookId"] = book.bookId;
    obj["path"] = book.path;
    obj["title"] = book.title;
    obj["author"] = book.author;
    obj["coverBmpPath"] = book.coverBmpPath;
  }

  return saveJsonDocumentToFile("RBS", path, doc);
}

bool JsonSettingsIO::loadRecentBooks(RecentBooksStore& store, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("RBS", "JSON parse error: %s", error.c_str());
    CPR_VCODEX_LOG_EVENT("RBS", std::string("Recent books JSON parse error: ") + error.c_str());
    return false;
  }

  store.recentBooks.clear();
  const uint32_t formatVersion = doc["formatVersion"] | static_cast<uint32_t>(1);
  JsonArray arr = doc["books"].as<JsonArray>();
  for (JsonObject obj : arr) {
    if (store.getCount() >= 10) break;
    RecentBook book;
    book.bookId = obj["bookId"] | std::string("");
    book.path = obj["path"] | std::string("");
    book.title = obj["title"] | std::string("");
    book.author = obj["author"] | std::string("");
    book.coverBmpPath = obj["coverBmpPath"] | std::string("");
    if (formatVersion < 2) {
      book.bookId.clear();
    }
    store.recentBooks.push_back(book);
  }

  store.normalizeBooks();
  LOG_DBG("RBS", "Recent books loaded from file (%d entries)", store.getCount());
  return true;
}

// ---- FavoritesStore ----

bool JsonSettingsIO::saveFavorites(const FavoritesStore& store, const char* path) {
  JsonDocument doc;
  doc["formatVersion"] = 1;
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& book : store.getBooks()) {
    JsonObject obj = arr.add<JsonObject>();
    obj["bookId"] = book.bookId;
    obj["path"] = book.path;
    obj["title"] = book.title;
    obj["author"] = book.author;
    obj["coverBmpPath"] = book.coverBmpPath;
  }

  return saveJsonDocumentToFile("FAV", path, doc);
}

bool JsonSettingsIO::loadFavorites(FavoritesStore& store, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("FAV", "JSON parse error: %s", error.c_str());
    CPR_VCODEX_LOG_EVENT("FAV", std::string("Favorites JSON parse error: ") + error.c_str());
    return false;
  }

  store.favoriteBooks.clear();
  JsonArray arr = doc["books"].as<JsonArray>();
  for (JsonObject obj : arr) {
    FavoriteBook book;
    book.bookId = obj["bookId"] | std::string("");
    book.path = obj["path"] | std::string("");
    book.title = obj["title"] | std::string("");
    book.author = obj["author"] | std::string("");
    book.coverBmpPath = obj["coverBmpPath"] | std::string("");
    store.favoriteBooks.push_back(book);
  }

  store.normalizeBooks();
  LOG_DBG("FAV", "Favorites loaded from file (%d entries)", store.getCount());
  return true;
}

// ---- ReadingStatsStore ----

bool JsonSettingsIO::saveReadingStats(const ReadingStatsStore& store, const char* path) {
  if (!path || !*path) {
    LOG_ERR("RST", "Missing JSON path for write");
    CPR_VCODEX_LOG_EVENT("RST", "Missing JSON path for write");
    return false;
  }

  char tempPath[256];
  const int tempPathLength = snprintf(tempPath, sizeof(tempPath), "%s.tmp", path);
  if (tempPathLength < 0 || static_cast<size_t>(tempPathLength) >= sizeof(tempPath)) {
    LOG_ERR("RST", "JSON path is too long for atomic write: %s", path);
    CPR_VCODEX_LOG_EVENT("RST", std::string("JSON path is too long for atomic write: ") + path);
    return false;
  }

  if (Storage.exists(tempPath)) {
    Storage.remove(tempPath);
  }

  HalFile file;
  if (!Storage.openFileForWrite("RST", tempPath, file)) {
    LOG_ERR("RST", "Could not open JSON file for write: %s", tempPath);
    CPR_VCODEX_LOG_EVENT("RST", std::string("Could not open JSON temp file for write: ") + tempPath);
    return false;
  }

  JsonStreamWriter writer(file);
  writer.literal("{\"formatVersion\":6,\"readingDays\":[");
  bool first = true;
  for (const auto& day : store.getReadingDays()) {
    if (!first) writer.literal(",");
    first = false;
    writer.literal("{\"dayOrdinal\":");
    writer.value(day.dayOrdinal);
    writer.literal(",\"readingMs\":");
    writer.value(day.readingMs);
    writer.literal("}");
  }

  writer.literal("],\"legacyReadingDays\":[");
  first = true;
  for (const auto& day : store.legacyReadingDays) {
    if (!first) writer.literal(",");
    first = false;
    writer.literal("{\"dayOrdinal\":");
    writer.value(day.dayOrdinal);
    writer.literal(",\"readingMs\":");
    writer.value(day.readingMs);
    writer.literal("}");
  }

  writer.literal("],\"sessionLog\":[");
  first = true;
  for (const auto& session : store.getSessionLog()) {
    if (!first) writer.literal(",");
    first = false;
    writer.literal("{\"dayOrdinal\":");
    writer.value(session.dayOrdinal);
    writer.literal(",\"sessionMs\":");
    writer.value(session.sessionMs);
    if (!session.bookId.empty()) {
      writer.literal(",\"bookId\":");
      writer.value(session.bookId);
    }
    if (!session.path.empty()) {
      writer.literal(",\"path\":");
      writer.value(session.path);
    }
    writer.literal("}");
  }

  writer.literal("],\"books\":[");
  first = true;
  for (const auto& book : store.getBooks()) {
    if (!first) writer.literal(",");
    first = false;
    writer.literal("{\"bookId\":");
    writer.value(book.bookId);
    writer.literal(",\"path\":");
    writer.value(book.path);
    writer.literal(",\"knownPaths\":[");
    bool firstKnownPath = true;
    for (const auto& knownPath : book.knownPaths) {
      if (!firstKnownPath) writer.literal(",");
      firstKnownPath = false;
      writer.value(knownPath);
    }
    writer.literal("],\"title\":");
    writer.value(book.title);
    writer.literal(",\"author\":");
    writer.value(book.author);
    writer.literal(",\"coverBmpPath\":");
    writer.value(book.coverBmpPath);
    writer.literal(",\"chapterTitle\":");
    writer.value(book.chapterTitle);
    writer.literal(",\"totalReadingMs\":");
    writer.value(book.totalReadingMs);
    writer.literal(",\"sessions\":");
    writer.value(book.sessions);
    writer.literal(",\"lastSessionMs\":");
    writer.value(book.lastSessionMs);
    writer.literal(",\"firstReadAt\":");
    writer.value(book.firstReadAt);
    writer.literal(",\"lastReadAt\":");
    writer.value(book.lastReadAt);
    writer.literal(",\"completedAt\":");
    writer.value(book.completedAt);
    writer.literal(",\"lastProgressPercent\":");
    writer.value(book.lastProgressPercent);
    writer.literal(",\"chapterProgressPercent\":");
    writer.value(book.chapterProgressPercent);
    writer.literal(",\"completed\":");
    writer.value(book.completed);
    writer.literal(",\"readingDays\":[");
    bool firstBookDay = true;
    for (const auto& day : book.readingDays) {
      if (!firstBookDay) writer.literal(",");
      firstBookDay = false;
      writer.literal("{\"dayOrdinal\":");
      writer.value(day.dayOrdinal);
      writer.literal(",\"readingMs\":");
      writer.value(day.readingMs);
      writer.literal("}");
    }
    writer.literal("]}");
  }
  writer.literal("]}");

  file.flush();
  const bool closed = file.close();
  if (!closed || !writer.ok() || writer.writtenBytes() == 0 || writer.writtenBytes() != writer.expectedBytes()) {
    Storage.remove(tempPath);
    LOG_ERR("RST", "Incomplete JSON write for %s: %u/%u bytes", path, static_cast<unsigned>(writer.writtenBytes()),
            static_cast<unsigned>(writer.expectedBytes()));
    CPR_VCODEX_LOG_EVENT("RST", std::string("Incomplete JSON write for ") + path + ": " +
                                    std::to_string(writer.writtenBytes()) + "/" +
                                    std::to_string(writer.expectedBytes()) + " bytes");
    return false;
  }

  return promoteJsonTempFile("RST", tempPath, path, writer.writtenBytes());
}

bool JsonSettingsIO::loadReadingStatsDocument(ReadingStatsStore& store, const JsonDocument& doc) {
  if (!doc.is<JsonObjectConst>()) {
    CPR_VCODEX_LOG_EVENT("RST", "Reading stats root is not a JSON object");
    return false;
  }

  const JsonVariantConst formatValue = doc["formatVersion"];
  if (!formatValue.isNull() && !formatValue.is<uint32_t>()) {
    CPR_VCODEX_LOG_EVENT("RST", "Reading stats formatVersion is not an unsigned integer");
    return false;
  }
  const uint32_t formatVersion = formatValue | static_cast<uint32_t>(1);
  if (formatVersion == 0 || formatVersion > 6) {
    CPR_VCODEX_LOG_EVENT("RST",
                         std::string("Unsupported reading stats formatVersion: ") + std::to_string(formatVersion));
    return false;
  }

  static constexpr const char* ARRAY_KEYS[] = {"readingDays", "legacyReadingDays", "sessionLog", "books"};
  bool hasStatsArray = false;
  bool missingCurrentArray = false;
  for (const char* key : ARRAY_KEYS) {
    const JsonVariantConst value = doc[key];
    if (value.isNull()) {
      missingCurrentArray = missingCurrentArray || formatVersion >= 6;
      continue;
    }
    if (!value.is<JsonArrayConst>()) {
      CPR_VCODEX_LOG_EVENT("RST", std::string("Reading stats field is not an array: ") + key);
      return false;
    }
    hasStatsArray = true;
  }
  if (!hasStatsArray) {
    CPR_VCODEX_LOG_EVENT("RST", "Reading stats document has no recognized data arrays");
    return false;
  }

  for (JsonVariantConst value : doc["books"].as<JsonArrayConst>()) {
    if (!value.is<JsonObjectConst>()) {
      CPR_VCODEX_LOG_EVENT("RST", "Reading stats books contains a non-object entry");
      return false;
    }
    const JsonObjectConst obj = value.as<JsonObjectConst>();
    if (!obj["knownPaths"].isNull() && !obj["knownPaths"].is<JsonArrayConst>()) {
      CPR_VCODEX_LOG_EVENT("RST", "Reading stats knownPaths is not an array");
      return false;
    }
    if (formatVersion >= 2 && !obj["readingDays"].isNull() && !obj["readingDays"].is<JsonArrayConst>()) {
      CPR_VCODEX_LOG_EVENT("RST", "Reading stats book readingDays is not an array");
      return false;
    }
  }
  for (JsonVariantConst value : doc["sessionLog"].as<JsonArrayConst>()) {
    if (!value.is<JsonObjectConst>()) {
      CPR_VCODEX_LOG_EVENT("RST", "Reading stats sessionLog contains a non-object entry");
      return false;
    }
  }

  store.books.clear();
  store.legacyReadingDays.clear();
  store.readingDays.clear();
  store.sessionLog.clear();
  store.dirty = missingCurrentArray;

  auto appendReadingDays = [](std::vector<ReadingDayStats>& destination, JsonArrayConst source) {
    for (JsonVariantConst value : source) {
      ReadingDayStats day;
      if (value.is<JsonObjectConst>()) {
        JsonObjectConst obj = value.as<JsonObjectConst>();
        day.dayOrdinal = obj["dayOrdinal"] | static_cast<uint32_t>(0);
        day.readingMs = obj["readingMs"] | static_cast<uint64_t>(0);
      } else {
        day.dayOrdinal = value | static_cast<uint32_t>(0);
        day.readingMs = 0;
      }
      if (day.dayOrdinal != 0) {
        destination.push_back(day);
      }
    }
  };

  appendReadingDays(store.readingDays, doc["readingDays"].as<JsonArrayConst>());
  std::vector<ReadingDayStats> declaredReadingDays = store.readingDays;
  if (formatVersion >= 2) {
    appendReadingDays(store.legacyReadingDays, doc["legacyReadingDays"].as<JsonArrayConst>());
    if (formatVersion < 6 && store.legacyReadingDays.empty()) {
      store.legacyReadingDays = store.readingDays;
    }
  } else {
    store.legacyReadingDays = store.readingDays;
  }

  if (formatVersion >= 4) {
    for (JsonObjectConst sessionObj : doc["sessionLog"].as<JsonArrayConst>()) {
      const uint32_t dayOrdinal = sessionObj["dayOrdinal"] | static_cast<uint32_t>(0);
      const uint32_t sessionMs = sessionObj["sessionMs"] | static_cast<uint32_t>(0);
      if (dayOrdinal == 0 || sessionMs == 0) continue;

      if (store.sessionLog.size() >= ReadingSessionLog::MAX_ENTRIES) {
        store.dirty = true;
      }
      ReadingSessionLog::makeRoomForAppend(store.sessionLog);

      ReadingSessionLogEntry session;
      session.dayOrdinal = dayOrdinal;
      session.sessionMs = sessionMs;
      session.bookId = sessionObj["bookId"] | std::string("");
      if (session.bookId.empty()) {
        session.path = BookIdentity::normalizePath(sessionObj["path"] | std::string(""));
      } else if (!sessionObj["path"].isNull()) {
        store.dirty = true;
      }
      store.sessionLog.push_back(std::move(session));
    }
  } else {
    store.dirty = true;
  }

  JsonArrayConst books = doc["books"].as<JsonArrayConst>();
  for (JsonObjectConst obj : books) {
    ReadingBookStats book;
    book.bookId = obj["bookId"] | std::string("");
    book.path = obj["path"] | std::string("");
    if (book.path.empty()) {
      continue;
    }
    for (JsonVariantConst value : obj["knownPaths"].as<JsonArrayConst>()) {
      const std::string knownPath = value | std::string("");
      if (!knownPath.empty()) {
        book.knownPaths.push_back(knownPath);
      }
    }
    book.title = obj["title"] | std::string("");
    book.author = obj["author"] | std::string("");
    book.coverBmpPath = obj["coverBmpPath"] | std::string("");
    book.chapterTitle = obj["chapterTitle"] | std::string("");
    book.totalReadingMs = obj["totalReadingMs"] | static_cast<uint64_t>(0);
    book.sessions = obj["sessions"] | static_cast<uint32_t>(0);
    book.lastSessionMs = obj["lastSessionMs"] | static_cast<uint32_t>(0);
    book.firstReadAt = obj["firstReadAt"] | static_cast<uint32_t>(0);
    book.lastReadAt = obj["lastReadAt"] | static_cast<uint32_t>(0);
    book.completedAt = obj["completedAt"] | static_cast<uint32_t>(0);
    book.lastProgressPercent = obj["lastProgressPercent"] | static_cast<uint8_t>(0);
    book.chapterProgressPercent = obj["chapterProgressPercent"] | static_cast<uint8_t>(0);
    book.completed = obj["completed"] | false;
    if (formatVersion >= 2) {
      appendReadingDays(book.readingDays, obj["readingDays"].as<JsonArrayConst>());
    }
    if (formatVersion < 3 || book.bookId.empty()) {
      store.dirty = true;
    }
    store.books.push_back(std::move(book));
  }

  if (formatVersion < 6) {
    store.convertLegacyReadingDaysToUnassigned();
    store.dirty = true;
  }
  store.rebuildAggregatedReadingDays();

  if (formatVersion >= 6) {
    auto normalizeDays = [](std::vector<ReadingDayStats>& days) {
      std::sort(days.begin(), days.end(), [](const ReadingDayStats& left, const ReadingDayStats& right) {
        return left.dayOrdinal < right.dayOrdinal;
      });
      size_t writeIndex = 0;
      for (const auto& day : days) {
        if (day.dayOrdinal == 0 || day.readingMs == 0) {
          continue;
        }
        if (writeIndex > 0 && days[writeIndex - 1].dayOrdinal == day.dayOrdinal) {
          days[writeIndex - 1].readingMs += day.readingMs;
        } else {
          days[writeIndex++] = day;
        }
      }
      days.resize(writeIndex);
    };
    normalizeDays(declaredReadingDays);

    bool aggregateMismatch = declaredReadingDays.size() != store.readingDays.size();
    for (const auto& declaredDay : declaredReadingDays) {
      const auto rebuiltIt =
          std::lower_bound(store.readingDays.begin(), store.readingDays.end(), declaredDay.dayOrdinal,
                           [](const ReadingDayStats& day, const uint32_t ordinal) { return day.dayOrdinal < ordinal; });
      const bool hasRebuiltDay =
          rebuiltIt != store.readingDays.end() && rebuiltIt->dayOrdinal == declaredDay.dayOrdinal;
      const uint64_t rebuiltMs = hasRebuiltDay ? rebuiltIt->readingMs : 0;
      if (rebuiltMs != declaredDay.readingMs) {
        aggregateMismatch = true;
      }
      if (declaredDay.readingMs > rebuiltMs) {
        store.legacyReadingDays.push_back(ReadingDayStats{declaredDay.dayOrdinal, declaredDay.readingMs - rebuiltMs});
      }
    }

    if (aggregateMismatch) {
      normalizeDays(store.legacyReadingDays);
      store.rebuildAggregatedReadingDays();
      store.dirty = true;
      CPR_VCODEX_LOG_EVENT("RST", "Reconciled reading stats aggregate totals without discarding stored data");
    }
  }

  std::stable_sort(store.sessionLog.begin(), store.sessionLog.end(),
                   [](const ReadingSessionLogEntry& left, const ReadingSessionLogEntry& right) {
                     return left.dayOrdinal < right.dayOrdinal;
                   });
  LOG_DBG("RST", "Reading stats loaded from file (%d books)", static_cast<int>(store.books.size()));
  return true;
}

bool JsonSettingsIO::loadReadingStats(ReadingStatsStore& store, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error || doc.overflowed()) {
    const char* message = error ? error.c_str() : "document overflow";
    LOG_ERR("RST", "JSON parse error: %s", message);
    CPR_VCODEX_LOG_EVENT("RST", std::string("Reading stats JSON parse error: ") + message);
    return false;
  }
  return loadReadingStatsDocument(store, doc);
}

bool JsonSettingsIO::loadReadingStatsFromFile(ReadingStatsStore& store, const char* path) {
  if (!Storage.exists(path)) {
    return false;
  }
  JsonDocument doc;
  const bool parsed = loadJsonDocumentFromFile("RST", path, doc);
  const bool loaded = parsed && !doc.overflowed() && loadReadingStatsDocument(store, doc);
  if (!loaded) {
    CPR_VCODEX_LOG_EVENT("RST", std::string("Failed to load reading stats from ") + path);
  }
  return loaded;
}

// ---- AchievementsStore ----

bool JsonSettingsIO::saveAchievements(const AchievementsStore& store, const char* path) {
  JsonDocument doc;
  doc["formatVersion"] = 2;
  doc["accumulatedReadingMs"] = store.accumulatedReadingMs;
  doc["countedSessions"] = store.countedSessions;
  doc["totalBookmarksAdded"] = store.totalBookmarksAdded;
  doc["longestSessionMs"] = store.longestSessionMs;
  doc["goalDaysCount"] = store.goalDaysCount;
  doc["currentGoalStreak"] = store.currentGoalStreak;
  doc["maxGoalStreak"] = store.maxGoalStreak;
  doc["lastGoalDayOrdinal"] = store.lastGoalDayOrdinal;
  doc["resetDayOrdinal"] = store.resetDayOrdinal;
  doc["resetDayBaselineMs"] = store.resetDayBaselineMs;

  JsonArray states = doc["states"].to<JsonArray>();
  for (const auto& state : store.states) {
    JsonObject obj = states.add<JsonObject>();
    obj["unlocked"] = state.unlocked;
    obj["unlockedAt"] = state.unlockedAt;
  }

  JsonArray startedBooks = doc["startedBooks"].to<JsonArray>();
  for (const auto& pathValue : store.startedBooks) {
    startedBooks.add(pathValue);
  }

  JsonArray finishedBooks = doc["finishedBooks"].to<JsonArray>();
  for (const auto& pathValue : store.finishedBooks) {
    finishedBooks.add(pathValue);
  }

  return saveJsonDocumentToFile("ACH", path, doc);
}

bool JsonSettingsIO::loadAchievements(AchievementsStore& store, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("ACH", "JSON parse error: %s", error.c_str());
    CPR_VCODEX_LOG_EVENT("ACH", std::string("Achievements JSON parse error: ") + error.c_str());
    return false;
  }

  store.states = {};
  store.startedBooks.clear();
  store.finishedBooks.clear();
  store.pendingUnlocks.clear();
  store.dirty = false;
  const uint32_t formatVersion = doc["formatVersion"] | static_cast<uint32_t>(1);

  store.accumulatedReadingMs = doc["accumulatedReadingMs"] | static_cast<uint64_t>(0);
  store.countedSessions = doc["countedSessions"] | static_cast<uint32_t>(0);
  store.totalBookmarksAdded = doc["totalBookmarksAdded"] | static_cast<uint32_t>(0);
  store.longestSessionMs = doc["longestSessionMs"] | static_cast<uint32_t>(0);
  store.goalDaysCount = doc["goalDaysCount"] | static_cast<uint32_t>(0);
  store.currentGoalStreak = doc["currentGoalStreak"] | static_cast<uint32_t>(0);
  store.maxGoalStreak = doc["maxGoalStreak"] | static_cast<uint32_t>(0);
  store.lastGoalDayOrdinal = doc["lastGoalDayOrdinal"] | static_cast<uint32_t>(0);
  store.resetDayOrdinal = doc["resetDayOrdinal"] | static_cast<uint32_t>(0);
  store.resetDayBaselineMs = doc["resetDayBaselineMs"] | static_cast<uint64_t>(0);
  // Session serials are runtime-only; persisted values collide after ReadingStatsStore resets on reboot.
  store.lastProcessedSessionSerial = 0;

  JsonArray states = doc["states"].as<JsonArray>();
  size_t stateIndex = 0;
  for (JsonObject obj : states) {
    if (stateIndex >= store.states.size()) {
      break;
    }
    store.states[stateIndex].unlocked = obj["unlocked"] | false;
    store.states[stateIndex].unlockedAt = obj["unlockedAt"] | static_cast<uint32_t>(0);
    ++stateIndex;
  }

  for (JsonVariant value : doc["startedBooks"].as<JsonArray>()) {
    std::string bookKey = value | std::string("");
    if (formatVersion < 2 && !bookKey.empty()) {
      if (const auto* statsBook = READING_STATS.findMatchingBookForPath(bookKey)) {
        bookKey = statsBook->bookId;
      } else {
        bookKey = BookIdentity::resolveStableBookId(bookKey);
      }
      store.dirty = true;
    }
    if (!bookKey.empty()) {
      store.startedBooks.push_back(bookKey);
    }
  }

  for (JsonVariant value : doc["finishedBooks"].as<JsonArray>()) {
    std::string bookKey = value | std::string("");
    if (formatVersion < 2 && !bookKey.empty()) {
      if (const auto* statsBook = READING_STATS.findMatchingBookForPath(bookKey)) {
        bookKey = statsBook->bookId;
      } else {
        bookKey = BookIdentity::resolveStableBookId(bookKey);
      }
      store.dirty = true;
    }
    if (!bookKey.empty()) {
      store.finishedBooks.push_back(bookKey);
    }
  }

  return true;
}

bool JsonSettingsIO::loadAchievementsFromFile(AchievementsStore& store, const char* path) {
  if (!Storage.exists(path)) {
    return false;
  }
  const String json = Storage.readFile(path);
  if (json.isEmpty()) {
    CPR_VCODEX_LOG_EVENT("ACH", std::string("Achievements file empty or unreadable: ") + path);
    return false;
  }
  const bool loaded = loadAchievements(store, json.c_str());
  if (!loaded) {
    CPR_VCODEX_LOG_EVENT("ACH", std::string("Failed to load achievements from ") + path);
  }
  return loaded;
}

// ---- OpdsServerStore ----
// Follows the same save/load pattern as WifiCredentialStore above.
// Passwords are XOR-obfuscated with the device MAC and base64-encoded ("password_obf" key).

bool JsonSettingsIO::saveOpds(const OpdsServerStore& store, const char* path) {
  JsonDocument doc;

  JsonArray arr = doc["servers"].to<JsonArray>();
  for (const auto& server : store.getServers()) {
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = server.name;
    obj["url"] = server.url;
    obj["username"] = server.username;
    obj["password_obf"] = obfuscation::obfuscateToBase64(server.password);
  }

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadOpds(OpdsServerStore& store, const char* json, bool* needsResave) {
  if (needsResave) *needsResave = false;
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("OPS", "JSON parse error: %s", error.c_str());
    return false;
  }

  store.servers.clear();
  JsonArray arr = doc["servers"].as<JsonArray>();
  for (JsonObject obj : arr) {
    if (store.servers.size() >= OpdsServerStore::MAX_SERVERS) break;
    OpdsServer server;
    server.name = obj["name"] | std::string("");
    server.url = obj["url"] | std::string("");
    server.username = obj["username"] | std::string("");
    // Try the obfuscated key first; fall back to plaintext "password" for
    // files written before obfuscation was added (or hand-edited JSON).
    bool ok = false;
    server.password = obfuscation::deobfuscateFromBase64(obj["password_obf"] | "", &ok);
    if (!ok || server.password.empty()) {
      server.password = obj["password"] | std::string("");
      if (!server.password.empty() && needsResave) *needsResave = true;
    }
    store.servers.push_back(std::move(server));
  }

  LOG_DBG("OPS", "Loaded %zu OPDS servers from file", store.servers.size());
  return true;
}
