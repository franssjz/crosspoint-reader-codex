#include <Arduino.h>
#include <Epub.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <Logging.h>
#include <SPI.h>
#include <WiFi.h>
#include <builtinFonts/all.h>

#include <algorithm>
#include <cstring>

#include "AchievementsStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "FavoritesStore.h"
#include "FlashcardsStore.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "SdCardFontGlobals.h"
#include "SilentRestart.h"
#include "UiFontSelection.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/settings/SdFirmwareUpdateActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BootRecovery.h"
#include "util/ButtonNavigator.h"
#include "util/CprVcodexLogs.h"
#include "util/PowerShortcutState.h"
#include "util/ScreenshotUtil.h"
#include "util/TimeUtils.h"
#include "util/WebUploadRecovery.h"
#include "version.h"

MappedInputManager mappedInputManager(gpio);
GfxRenderer renderer(display);
ActivityManager activityManager(renderer, mappedInputManager);
FontDecompressor fontDecompressor;
SdCardFontSystem sdFontSystem;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts());

// Fonts
EpdFont bookerly14RegularFont(&bookerly_14_regular);
EpdFont bookerly14BoldFont(&bookerly_14_bold);
EpdFont bookerly14ItalicFont(&bookerly_14_italic);
EpdFont bookerly14BoldItalicFont(&bookerly_14_bolditalic);
EpdFontFamily bookerly14FontFamily(&bookerly14RegularFont, &bookerly14BoldFont, &bookerly14ItalicFont,
                                   &bookerly14BoldItalicFont);
#ifndef OMIT_FONTS
EpdFont bookerly10RegularFont(&bookerly_10_regular);
EpdFont bookerly10BoldFont(&bookerly_10_bold);
EpdFont bookerly10ItalicFont(&bookerly_10_italic);
EpdFont bookerly10BoldItalicFont(&bookerly_10_bolditalic);
EpdFontFamily bookerly10FontFamily(&bookerly10RegularFont, &bookerly10BoldFont, &bookerly10ItalicFont,
                                   &bookerly10BoldItalicFont);
EpdFont bookerly12RegularFont(&bookerly_12_regular);
EpdFont bookerly12BoldFont(&bookerly_12_bold);
EpdFont bookerly12ItalicFont(&bookerly_12_italic);
EpdFont bookerly12BoldItalicFont(&bookerly_12_bolditalic);
EpdFontFamily bookerly12FontFamily(&bookerly12RegularFont, &bookerly12BoldFont, &bookerly12ItalicFont,
                                   &bookerly12BoldItalicFont);
EpdFont bookerly16RegularFont(&bookerly_16_regular);
EpdFont bookerly16BoldFont(&bookerly_16_bold);
EpdFont bookerly16ItalicFont(&bookerly_16_italic);
EpdFont bookerly16BoldItalicFont(&bookerly_16_bolditalic);
EpdFontFamily bookerly16FontFamily(&bookerly16RegularFont, &bookerly16BoldFont, &bookerly16ItalicFont,
                                   &bookerly16BoldItalicFont);
EpdFont bookerly18RegularFont(&bookerly_18_regular);
EpdFont bookerly18BoldFont(&bookerly_18_bold);
EpdFont bookerly18ItalicFont(&bookerly_18_italic);
EpdFont bookerly18BoldItalicFont(&bookerly_18_bolditalic);
EpdFontFamily bookerly18FontFamily(&bookerly18RegularFont, &bookerly18BoldFont, &bookerly18ItalicFont,
                                   &bookerly18BoldItalicFont);

EpdFont notosans10RegularFont(&notosans_10_regular);
EpdFont notosans10BoldFont(&notosans_10_bold);
EpdFont notosans10ItalicFont(&notosans_10_italic);
EpdFont notosans10BoldItalicFont(&notosans_10_bolditalic);
EpdFontFamily notosans10FontFamily(&notosans10RegularFont, &notosans10BoldFont, &notosans10ItalicFont,
                                   &notosans10BoldItalicFont);
EpdFont notosans12RegularFont(&notosans_12_regular);
EpdFont notosans12BoldFont(&notosans_12_bold);
EpdFont notosans12ItalicFont(&notosans_12_italic);
EpdFont notosans12BoldItalicFont(&notosans_12_bolditalic);
EpdFontFamily notosans12FontFamily(&notosans12RegularFont, &notosans12BoldFont, &notosans12ItalicFont,
                                   &notosans12BoldItalicFont);
EpdFont notosans14RegularFont(&notosans_14_regular);
EpdFont notosans14BoldFont(&notosans_14_bold);
EpdFont notosans14ItalicFont(&notosans_14_italic);
EpdFont notosans14BoldItalicFont(&notosans_14_bolditalic);
EpdFontFamily notosans14FontFamily(&notosans14RegularFont, &notosans14BoldFont, &notosans14ItalicFont,
                                   &notosans14BoldItalicFont);
EpdFont notosans16RegularFont(&notosans_16_regular);
EpdFont notosans16BoldFont(&notosans_16_bold);
EpdFont notosans16ItalicFont(&notosans_16_italic);
EpdFont notosans16BoldItalicFont(&notosans_16_bolditalic);
EpdFontFamily notosans16FontFamily(&notosans16RegularFont, &notosans16BoldFont, &notosans16ItalicFont,
                                   &notosans16BoldItalicFont);
EpdFont notosans18RegularFont(&notosans_18_regular);
EpdFont notosans18BoldFont(&notosans_18_bold);
EpdFont notosans18ItalicFont(&notosans_18_italic);
EpdFont notosans18BoldItalicFont(&notosans_18_bolditalic);
EpdFontFamily notosans18FontFamily(&notosans18RegularFont, &notosans18BoldFont, &notosans18ItalicFont,
                                   &notosans18BoldItalicFont);
#endif  // OMIT_FONTS

EpdFont smallFont(&notosans_8_regular);
EpdFontFamily smallFontFamily(&smallFont);

EpdFont ui10RegularFont(&ubuntu_10_regular);
EpdFont ui10BoldFont(&ubuntu_10_bold);
EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);

EpdFont ui12RegularFont(&ubuntu_12_regular);
EpdFont ui12BoldFont(&ubuntu_12_bold);
EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont);

namespace {

bool shouldUseNotoUiFonts(const Language lang) {
#ifdef OMIT_FONTS
  (void)lang;
  return false;
#else
  return lang == Language::VI;
#endif
}

void applyUiFontsForLanguage(const Language lang) {
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);

#ifdef OMIT_FONTS
  (void)lang;
  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
#else
  if (shouldUseNotoUiFonts(lang)) {
    // Keep Vietnamese UI at 10 pt for both slots to preserve existing layouts
    // while still providing full glyph coverage.
    renderer.insertFont(UI_10_FONT_ID, notosans10FontFamily);
    renderer.insertFont(UI_12_FONT_ID, notosans10FontFamily);
    LOG_INF("MAIN", "UI fonts: Noto Sans 8/10/10 for language %s", I18N.getLanguageName(lang));
    return;
  }

  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
#endif

  LOG_INF("MAIN", "UI fonts: default UI stack for language %s", I18N.getLanguageName(lang));
}

}  // namespace

void refreshUiFontsForCurrentLanguage() { applyUiFontsForLanguage(I18N.getLanguage()); }
void useLanguageSelectionUiFonts() { applyUiFontsForLanguage(Language::VI); }

// measurement of power button press duration calibration value
unsigned long t1 = 0;
unsigned long t2 = 0;

// Definitions for SilentRestart.h. RTC_NOINIT survives ESP.restart() but not power loss.
RTC_NOINIT_ATTR uint32_t silentRebootMagic;
RTC_NOINIT_ATTR uint32_t silentRebootTarget;
constexpr uint32_t SILENT_REBOOT_MAGIC = 0xC1EAB007;
constexpr uint32_t SILENT_REBOOT_TARGET_HOME = 0;
constexpr uint32_t SILENT_REBOOT_TARGET_READER = 1;

// Latched once deep sleep is committed. WiFi activities also restart silently
// from onExit(), but deep sleep already gives us a clean heap on wake.
static bool deepSleepInProgress = false;
static PowerShortcutState powerShortcuts;

namespace {
struct QuickLockBackdrop {
  static constexpr int SIZE = 40;
  uint8_t bytes[6 * SIZE] = {};  // A 40px square touches at most six bytes per row in every orientation.
  int x = 0;
  int y = 0;
  size_t byteCount = 0;
  GfxRenderer::Orientation orientation = GfxRenderer::Orientation::Portrait;
  bool valid = false;
  void save() {
    int top, right, bottom, left;
    renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
    x = std::max(left, renderer.getScreenWidth() - right - SIZE);
    y = std::max(top, renderer.getScreenHeight() - bottom - SIZE);
    orientation = renderer.getOrientation();
    byteCount = renderer.getRegionByteSize(x, y, SIZE, SIZE);
    valid = byteCount <= sizeof(bytes) && renderer.copyRegionToBuffer(x, y, SIZE, SIZE, bytes, byteCount);
  }
  bool restore() {
    if (!valid || orientation != renderer.getOrientation()) return false;
    valid = false;
    return renderer.copyBufferToRegion(x, y, SIZE, SIZE, bytes, byteCount);
  }
};
QuickLockBackdrop quickLockBackdrop;

void notifyQuickLockChanged() {
  halTiltSensor.clearPendingEvents();
  if (powerShortcuts.isLocked()) {
    // Drain the last queued page render before saving the small badge region.
    activityManager.requestUpdateAndWait();
    RenderLock lock;
    if (activityManager.isReaderActivity()) READING_STATS.noteActivity();
    quickLockBackdrop.save();
    if (quickLockBackdrop.valid) {
      const int x = quickLockBackdrop.x;
      const int y = quickLockBackdrop.y;
      renderer.fillRect(x, y, QuickLockBackdrop::SIZE, QuickLockBackdrop::SIZE, false);
      renderer.drawRoundedRect(x + 12, y + 4, 16, 22, 4, 8, true);
      renderer.fillRect(x + 7, y + 19, 26, 16, true);
      renderer.fillRect(x + 18, y + 25, 4, 6, false);
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    }
  } else {
    bool restored;
    {
      RenderLock lock;
      // Keep the same session and counters, excluding the whole locked interval.
      READING_STATS.resumeSession();
      restored = quickLockBackdrop.restore();
      if (restored) renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    }
    if (!restored) activityManager.requestUpdateAndWait();
  }
}
}  // namespace

void silentRestart() {
  if (deepSleepInProgress) return;
  silentRebootTarget = SILENT_REBOOT_TARGET_HOME;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Silent restart (target=home)");
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  ESP.restart();
}

void silentRestartToReader() {
  if (deepSleepInProgress) return;
  silentRebootTarget = SILENT_REBOOT_TARGET_READER;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Silent restart (target=reader)");
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  ESP.restart();
}

// Verify power button press duration on wake-up from deep sleep
// Pre-condition: isWakeupByPowerButton() == true
void verifyPowerButtonDuration() {
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP) {
    // Fast path for short press
    // Needed because inputManager.isPressed() may take up to ~500ms to return the correct state
    return;
  }

  // Give the user up to 1000ms to start holding the power button, and must hold for SETTINGS.getPowerButtonDuration()
  const auto start = millis();
  bool abort = false;
  // Subtract the current time, because inputManager only starts counting the HeldTime from the first update()
  // This way, we remove the time we already took to reach here from the duration,
  // assuming the button was held until now from millis()==0 (i.e. device start time).
  const uint16_t calibration = start;
  const uint16_t calibratedPressDuration =
      (calibration < SETTINGS.getPowerButtonDuration()) ? SETTINGS.getPowerButtonDuration() - calibration : 1;

  gpio.update();
  // Needed because inputManager.isPressed() may take up to ~500ms to return the correct state
  while (!gpio.isPressed(HalGPIO::BTN_POWER) && millis() - start < 1000) {
    delay(10);  // only wait 10ms each iteration to not delay too much in case of short configured duration.
    gpio.update();
  }

  t2 = millis();
  if (gpio.isPressed(HalGPIO::BTN_POWER)) {
    do {
      delay(10);
      gpio.update();
    } while (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getPowerButtonHeldTime() < calibratedPressDuration);
    abort = gpio.getPowerButtonHeldTime() < calibratedPressDuration;
  } else {
    abort = true;
  }

  if (abort) {
    // Button released too early. Returning to sleep.
    // IMPORTANT: Re-arm the wakeup trigger before sleeping again
    powerManager.startDeepSleep(gpio);
  }
}
void waitForPowerRelease() {
  gpio.update();
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    delay(50);
    gpio.update();
  }
}

// Enter deep sleep mode
void enterDeepSleep() {
  HalPowerManager::Lock powerLock;  // Ensure we are at normal CPU frequency for sleep preparation
  if (powerShortcuts.isLocked()) {
    RenderLock lock;
    READING_STATS.resumeSession();  // onExit must not count the locked time when ending the session.
  }
  APP_STATE.lastSleepFromReader = activityManager.isReaderActivity();

  deepSleepInProgress = true;
  activityManager.goToSleep();

  // Persist only after the sleep screen has been rendered. Serializing state
  // while a reader is still alive fragments the heap immediately before the
  // large contiguous allocation required by PNGdec.
  APP_STATE.saveToFile();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  halTiltSensor.deepSleep();
  display.deepSleep();
  LOG_DBG("MAIN", "Entering deep sleep");

  powerManager.startDeepSleep(gpio);
}

void ensureSdFontLoaded() {
  if (Storage.ready()) {
    sdFontSystem.ensureLoaded(renderer);
  }
}

void setupDisplayAndFonts(bool seamless = false) {
  display.begin(seamless);
  renderer.begin();
  renderer.setDarkMode(SETTINGS.darkMode);
  activityManager.begin();
  LOG_DBG("MAIN", "Display initialized");

  // Initialize font decompressor for compressed reader fonts
  if (!fontDecompressor.init()) {
    LOG_ERR("MAIN", "Font decompressor init failed");
  }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  renderer.insertFont(BOOKERLY_14_FONT_ID, bookerly14FontFamily);
#ifndef OMIT_FONTS
  renderer.insertFont(BOOKERLY_10_FONT_ID, bookerly10FontFamily);
  renderer.insertFont(BOOKERLY_12_FONT_ID, bookerly12FontFamily);
  renderer.insertFont(BOOKERLY_16_FONT_ID, bookerly16FontFamily);
  renderer.insertFont(BOOKERLY_18_FONT_ID, bookerly18FontFamily);

  renderer.insertFont(NOTOSANS_10_FONT_ID, notosans10FontFamily);
  renderer.insertFont(NOTOSANS_12_FONT_ID, notosans12FontFamily);
  renderer.insertFont(NOTOSANS_14_FONT_ID, notosans14FontFamily);
  renderer.insertFont(NOTOSANS_16_FONT_ID, notosans16FontFamily);
  renderer.insertFont(NOTOSANS_18_FONT_ID, notosans18FontFamily);
#endif  // OMIT_FONTS
  refreshUiFontsForCurrentLanguage();
  if (Storage.ready()) {
    sdFontSystem.begin(renderer);
  }
  LOG_DBG("MAIN", "Fonts setup");
}

void setup() {
  t1 = millis();

  HalSystem::begin();
  // checkPanic() consumes the watchdog capture marker after a successful SD
  // dump, so keep the boot classification for the later route decision.
  const bool rebootedFromPanic = HalSystem::isRebootFromPanic();

  const bool isSilentReboot = (silentRebootMagic == SILENT_REBOOT_MAGIC);
  const uint32_t snapshotTarget =
      (isSilentReboot && silentRebootTarget <= SILENT_REBOOT_TARGET_READER) ? silentRebootTarget : 0;
  silentRebootMagic = 0;
  silentRebootTarget = 0;

  gpio.begin();
  powerManager.begin();
  halTiltSensor.begin();
  halClock.begin();

  // Disable Arduino core's NVS auto-persist of Wi-Fi credentials. WifiSelectionActivity
  // always scans first and uses WifiCredentialStore (SD card JSON) as the source of
  // truth; the SDK's hidden nvs.net80211 copy must not auto-reconnect behind the user.
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);

#ifdef ENABLE_SERIAL_LOG
  if (gpio.isUsbConnected()) {
    Serial.begin(115200);
    const unsigned long start = millis();
    while (!Serial && (millis() - start) < 500) {
      delay(10);
    }
  }
#endif

  LOG_INF("MAIN", "Hardware detect: %s", gpio.deviceIsX3() ? "X3" : "X4");

  // SD Card Initialization
  // We need 6 open files concurrently when parsing a new chapter
  if (!Storage.begin()) {
    LOG_ERR("MAIN", "SD card initialization failed");
    setupDisplayAndFonts(isSilentReboot);
    activityManager.goToFullScreenMessage("SD card error", EpdFontFamily::BOLD);
    return;
  }
  if (!WebUploadRecovery::recover(Storage)) {
    LOG_ERR("MAIN", "Upload recovery needs attention; preserved previous files");
  }
  // Stamp all SdFat creates/syncs with RTC time when available, else last Sync Day.
  TimeUtils::registerSdFatDateTimeCallback();

  HalSystem::checkPanic();
  BootRecovery::initialize();

  const auto logSkip = [](const char* message) { CPR_VCODEX_LOG_EVENT("BOOT", message); };

  if (BootRecovery::shouldSkipSettings()) {
    logSkip("Skipping settings load due to recovery mode");
  } else {
    BootRecovery::enterStage(BootRecovery::BootStage::Settings);
    SETTINGS.loadFromFile();
  }

  if (BootRecovery::shouldSkipLanguage()) {
    logSkip("Skipping language load due to recovery mode");
  } else {
    BootRecovery::enterStage(BootRecovery::BootStage::Language);
    I18N.loadSettings();
  }

  if (BootRecovery::shouldSkipKOReader()) {
    logSkip("Skipping KOReader credential load due to recovery mode");
  } else {
    BootRecovery::enterStage(BootRecovery::BootStage::KOReader);
    KOREADER_STORE.loadFromFile();
  }

  if (BootRecovery::shouldSkipOPDS()) {
    logSkip("Skipping OPDS store load due to recovery mode");
  } else {
    BootRecovery::enterStage(BootRecovery::BootStage::OPDS);
    OPDS_STORE.loadFromFile();
  }

  BootRecovery::enterStage(BootRecovery::BootStage::UiTheme);
  UITheme::getInstance().reload();
  ButtonNavigator::setMappedInputManager(mappedInputManager);

  const auto wakeupReason = gpio.getWakeupReason();
  switch (wakeupReason) {
    case HalGPIO::WakeupReason::PowerButton:
      LOG_DBG("MAIN", "Verifying power button press duration");
      gpio.verifyPowerButtonWakeup(SETTINGS.getPowerButtonDuration(),
                                   SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP);
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
      // If USB power caused a cold boot, go back to sleep
      LOG_DBG("MAIN", "Wakeup reason: After USB Power");
      powerManager.startDeepSleep(gpio);
      break;
    case HalGPIO::WakeupReason::AfterFlash:
      // After flashing, just proceed to boot
    case HalGPIO::WakeupReason::Other:
    default:
      break;
  }

  // Recovery firmware mode: hold the upper side button (BTN_UP) together with
  // Power while waking to jump directly to the SD firmware picker. This is the
  // only practical recovery route on USB-locked X3 devices, so sample the raw
  // hardware button after InputManager's debounce state has settled.
  bool recoveryFirmwareMode = false;
  if (wakeupReason == HalGPIO::WakeupReason::PowerButton) {
    const unsigned long settleStart = millis();
    while (millis() - settleStart < 500) {
      gpio.update();
      delay(10);
    }
    if (gpio.isPressed(HalGPIO::BTN_UP)) {
      recoveryFirmwareMode = true;
      LOG_INF("MAIN", "Recovery firmware mode (UP + POWER held at boot)");
    }
  }

  // First serial output only here to avoid timing inconsistencies for power button press duration verification
  LOG_DBG("MAIN", "Starting CrossPoint version %s", CROSSPOINT_VERSION);

  gpio.update();
  const bool manualSafeBoot = gpio.isPressed(HalGPIO::BTN_BACK);
  if (manualSafeBoot) {
    CPR_VCODEX_LOG_EVENT("BOOT", "Manual safe boot requested by holding Back during boot");
  }

  BootRecovery::enterStage(BootRecovery::BootStage::DisplayAndFonts);
  setupDisplayAndFonts(isSilentReboot);

  if (!isSilentReboot) {
    activityManager.goToBoot();
  }

  const bool skipStateLoad = manualSafeBoot || BootRecovery::shouldSkipState();
  const bool skipReadingStatsLoad = manualSafeBoot || BootRecovery::shouldSkipReadingStats();
  const bool skipRecentBooksLoad = manualSafeBoot || BootRecovery::shouldSkipRecentBooks();
  const bool skipFavoritesLoad = manualSafeBoot || BootRecovery::shouldSkipFavorites();
  const bool skipFlashcardsLoad = manualSafeBoot || BootRecovery::shouldSkipFlashcards();
  const bool skipAchievementsLoad = manualSafeBoot || BootRecovery::shouldSkipAchievements();
  const bool forceHomeBoot = manualSafeBoot || BootRecovery::shouldForceHome();

  if (skipStateLoad) {
    logSkip("Skipping app state load due to recovery mode");
  } else {
    BootRecovery::enterStage(BootRecovery::BootStage::State);
    APP_STATE.loadFromFile();
  }

  if (skipReadingStatsLoad) {
    logSkip("Skipping reading stats load due to recovery mode");
    READING_STATS.markLoadSkippedForRecovery();
  } else {
    BootRecovery::enterStage(BootRecovery::BootStage::ReadingStats);
    if (READING_STATS.loadFromFile()) {
      READING_STATS.createDueAutoBackup();
    }
  }

  if (skipRecentBooksLoad) {
    logSkip("Skipping recent books load due to recovery mode");
  } else {
    BootRecovery::enterStage(BootRecovery::BootStage::RecentBooks);
    RECENT_BOOKS.loadFromFile();
  }

  if (skipFavoritesLoad) {
    logSkip("Skipping favorites load due to recovery mode");
  } else {
    BootRecovery::enterStage(BootRecovery::BootStage::Favorites);
    FAVORITES.loadFromFile();
  }

  if (skipFlashcardsLoad) {
    logSkip("Skipping flashcards load due to recovery mode");
  } else {
    BootRecovery::enterStage(BootRecovery::BootStage::Flashcards);
    FLASHCARDS.loadFromFile();
  }

  if (skipAchievementsLoad) {
    logSkip("Skipping achievements load due to recovery mode");
  } else {
    BootRecovery::enterStage(BootRecovery::BootStage::Achievements);
    ACHIEVEMENTS.loadFromFile();
  }

  if (halClock.isAvailable() && SETTINGS.clockHasBeenSynced) {
    TimeUtils::applySystemClockFromRtc(true);
  }

  const bool countUsefulStart = !isSilentReboot && !forceHomeBoot &&
                                wakeupReason != HalGPIO::WakeupReason::AfterUSBPower &&
                                wakeupReason != HalGPIO::WakeupReason::AfterFlash;
  const uint8_t syncDayReminderThreshold = SETTINGS.getEffectiveSyncDayReminderStartThreshold();
  BootRecovery::enterStage(BootRecovery::BootStage::RouteDecision);

  if (recoveryFirmwareMode) {
    // Skip normal home/reader routing and keep recovery self-contained: Back
    // cannot escape the firmware picker while recoveryMode is true.
    activityManager.replaceActivity(
        std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInputManager, /*recoveryMode=*/true));
  } else if (rebootedFromPanic && !forceHomeBoot) {
    // If we rebooted from a panic, go to crash report screen to show the panic info
    activityManager.goToCrashReport();
  } else if (isSilentReboot && snapshotTarget == SILENT_REBOOT_TARGET_READER && !APP_STATE.openEpubPath.empty()) {
    activityManager.goToReader(APP_STATE.openEpubPath);
  } else if (isSilentReboot) {
    activityManager.goHome();
  } else {
    const bool bootToHome = forceHomeBoot || APP_STATE.openEpubPath.empty() || !APP_STATE.lastSleepFromReader ||
                            mappedInputManager.isPressed(MappedInputManager::Button::Back) ||
                            APP_STATE.readerActivityLoadCount > 0;

    if (bootToHome) {
      if (countUsefulStart) {
        APP_STATE.recordUsefulStart(syncDayReminderThreshold);
        APP_STATE.saveToFile();
      }
      activityManager.goHome();
    } else {
      // Clear app state to avoid getting into a boot loop if the epub doesn't load
      const auto path = APP_STATE.openEpubPath;
      APP_STATE.openEpubPath = "";
      APP_STATE.readerActivityLoadCount++;
      if (countUsefulStart) {
        APP_STATE.recordUsefulStart(syncDayReminderThreshold);
      }
      APP_STATE.saveToFile();
      activityManager.goToReader(path);
    }
  }

  BootRecovery::markBootCompleted();

  if (isSilentReboot) {
    activityManager.requestUpdateAndWait();
    gpio.update();
    delay(10);
    gpio.update();
  }

  // Ensure we're not still holding the power button before leaving setup
  waitForPowerRelease();
}

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
#ifdef ENABLE_SERIAL_LOG
  static unsigned long lastMemPrint = 0;
#endif

  gpio.update();
  if (!powerShortcuts.isLocked()) {
    halTiltSensor.update(SETTINGS.tiltPageTurn, SETTINGS.orientation, activityManager.isReaderActivity());
  }

  renderer.setFadingFix(SETTINGS.fadingFix);
  renderer.setDarkMode(SETTINGS.darkMode);
  renderer.setTextDarkness(SETTINGS.textDarkness);

#ifdef ENABLE_SERIAL_LOG
  if (Serial && millis() - lastMemPrint >= 10000) {
    LOG_INF("MEM", "Free: %d bytes, Total: %d bytes, Min Free: %d bytes, MaxAlloc: %d bytes", ESP.getFreeHeap(),
            ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    lastMemPrint = millis();
  }

  // Handle incoming serial commands,
  // nb: we use logSerial from logging to avoid deprecation warnings
  if (logSerial.available() > 0) {
    String line = logSerial.readStringUntil('\n');
    if (line.startsWith("CMD:")) {
      String cmd = line.substring(4);
      cmd.trim();
      if (cmd == "SCREENSHOT") {
        const uint32_t bufferSize = display.getBufferSize();
        logSerial.printf("SCREENSHOT_START:%d\n", bufferSize);
        uint8_t* buf = display.getFrameBuffer();
        logSerial.write(buf, bufferSize);
        logSerial.printf("SCREENSHOT_END\n");
      }
    }
  }
#endif

  // Check for any user activity (button press or release) or active background work
  static unsigned long lastActivityTime = millis();
  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || halTiltSensor.hadActivity() ||
      activityManager.preventAutoSleep()) {
    lastActivityTime = millis();         // Reset inactivity timer
    powerManager.setPowerSaving(false);  // Restore normal CPU frequency on user activity
  }

  const bool powerPressed = gpio.isPressed(HalGPIO::BTN_POWER);
  const bool shortPowerRelease =
      gpio.wasReleased(HalGPIO::BTN_POWER) && gpio.getPowerButtonHeldTime() < SETTINGS.getPowerButtonDuration();
  // Transfers and flashing must keep servicing their activity loop. Quick Lock
  // is available again once that activity releases its no-sleep guard.
  const bool enableQuickLock =
      SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::QUICK_LOCK && !activityManager.preventAutoSleep();
  bool anyButtonPressed = false;
  for (uint8_t button = HalGPIO::BTN_BACK; button <= HalGPIO::BTN_POWER; ++button) {
    anyButtonPressed = anyButtonPressed || gpio.isPressed(button);
  }
  const auto shortcut = powerShortcuts.update(millis(), powerPressed, gpio.isPressed(HalGPIO::BTN_DOWN),
                                              shortPowerRelease, enableQuickLock, anyButtonPressed);
  if (shortcut.event == PowerShortcutState::Event::LockChanged) {
    notifyQuickLockChanged();
    lastActivityTime = millis();
    delay(10);
    return;
  }
  if (powerShortcuts.isLocked()) {
    if (powerShortcuts.shouldSleep(millis(), SETTINGS.getSleepTimeoutMs()) ||
        (powerPressed && !gpio.isPressed(HalGPIO::BTN_DOWN) &&
         gpio.getPowerButtonHeldTime() > SETTINGS.getPowerButtonDuration())) {
      enterDeepSleep();
    }
    powerManager.setPowerSaving(true);
    delay(50);
    return;
  }
  if (shortcut.event == PowerShortcutState::Event::Screenshot) {
    RenderLock lock;
    ScreenshotUtil::takeScreenshot(renderer);
  }
  if (shortcut.consume) {
    delay(10);
    return;
  }

  const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();
  if (millis() - lastActivityTime >= sleepTimeoutMs) {
    LOG_DBG("SLP", "Auto-sleep triggered after %lu ms of inactivity", sleepTimeoutMs);
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getPowerButtonHeldTime() > SETTINGS.getPowerButtonDuration()) {
    // If the screenshot combination is potentially being pressed, don't sleep
    if (gpio.isPressed(HalGPIO::BTN_DOWN)) {
      return;
    }
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  // Refresh screen when power button is short-pressed with FORCE_REFRESH setting.
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::FORCE_REFRESH &&
      mappedInputManager.wasReleased(MappedInputManager::Button::Power)) {
    LOG_DBG("MAIN", "Manual screen refresh triggered");
    if (!activityManager.handleForcedRefresh()) {
      RenderLock lock;
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
  }

  // Refresh the battery icon when USB is plugged or unplugged.
  // Placed after sleep guards so we never queue a render that won't be processed.
  if (gpio.wasUsbStateChanged()) {
    activityManager.requestUpdate();
  }

  const unsigned long activityStartTime = millis();
  activityManager.loop();
  TimeUtils::tickSystemClockFromRtc();
  const unsigned long activityDuration = millis() - activityStartTime;

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      LOG_DBG("LOOP", "New max loop duration: %lu ms (activity: %lu ms)", maxLoopDuration, activityDuration);
    }
  }

  // Add delay at the end of the loop to prevent tight spinning
  // When an activity requests skip loop delay (e.g., webserver running), use yield() for faster response
  // Otherwise, use longer delay to save power
  if (activityManager.skipLoopDelay()) {
    powerManager.setPowerSaving(false);  // Make sure we're at full performance when skipLoopDelay is requested
    yield();                             // Give FreeRTOS a chance to run tasks, but return immediately
  } else {
    if (millis() - lastActivityTime >= HalPowerManager::IDLE_POWER_SAVING_MS) {
      // If we've been inactive for a while, increase the delay to save power
      powerManager.setPowerSaving(true);  // Lower CPU frequency after extended inactivity
      delay(50);
    } else {
      // Short delay to prevent tight loop while still being responsive
      delay(10);
    }
  }
}
