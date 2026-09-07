#pragma once
#include <HalDisplay.h>
#include <HalStorage.h>

#include <cstdint>
#include <iosfwd>

class CrossPointSettings {
 private:
  // Private constructor for singleton
  CrossPointSettings() = default;

  // Static instance
  static CrossPointSettings instance;

 public:
  // Delete copy constructor and assignment
  CrossPointSettings(const CrossPointSettings&) = delete;
  CrossPointSettings& operator=(const CrossPointSettings&) = delete;

  enum SLEEP_SCREEN_MODE {
    DARK = 0,
    LIGHT = 1,
    CUSTOM = 2,
    COVER = 3,
    BLANK = 4,
    COVER_CUSTOM = 5,
    READING_DASHBOARD = 6,
    COVER_STATS = 7,
    COVER_STATS_V2 = 8,
    CUSTOM_STATS = 9,
    CUSTOM_STATS_V2 = 10,
    SLEEP_SCREEN_MODE_COUNT
  };
  enum SLEEP_SCREEN_COVER_MODE { FIT = 0, CROP = 1, SLEEP_SCREEN_COVER_MODE_COUNT };
  enum SLEEP_SCREEN_COVER_FILTER {
    NO_FILTER = 0,
    BLACK_AND_WHITE = 1,
    INVERTED_BLACK_AND_WHITE = 2,
    SLEEP_SCREEN_COVER_FILTER_COUNT
  };

  // Status bar enum - legacy
  enum STATUS_BAR_MODE {
    NONE = 0,
    NO_PROGRESS = 1,
    FULL = 2,
    BOOK_PROGRESS_BAR = 3,
    ONLY_BOOK_PROGRESS_BAR = 4,
    CHAPTER_PROGRESS_BAR = 5,
    STATUS_BAR_MODE_COUNT
  };
  enum STATUS_BAR_PROGRESS_BAR {
    BOOK_PROGRESS = 0,
    CHAPTER_PROGRESS = 1,
    HIDE_PROGRESS = 2,
    STATUS_BAR_PROGRESS_BAR_COUNT
  };
  enum STATUS_BAR_PROGRESS_BAR_THICKNESS {
    PROGRESS_BAR_THIN = 0,
    PROGRESS_BAR_NORMAL = 1,
    PROGRESS_BAR_THICK = 2,
    STATUS_BAR_PROGRESS_BAR_THICKNESS_COUNT
  };
  enum STATUS_BAR_TITLE { BOOK_TITLE = 0, CHAPTER_TITLE = 1, HIDE_TITLE = 2, STATUS_BAR_TITLE_COUNT };
  enum XTC_STATUS_BAR_MODE {
    XTC_STATUS_BAR_HIDE = 0,
    XTC_STATUS_BAR_BOTTOM = 1,
    XTC_STATUS_BAR_TOP = 2,
    XTC_STATUS_BAR_MODE_COUNT
  };
  // STATUS_BAR_CLOCK_RIGHT = 1 matches the legacy boolean "show clock" value.
  enum STATUS_BAR_CLOCK {
    STATUS_BAR_CLOCK_HIDE = 0,
    STATUS_BAR_CLOCK_RIGHT = 1,
    STATUS_BAR_CLOCK_LEFT = 2,
    STATUS_BAR_CLOCK_COUNT
  };

  enum ORIENTATION {
    PORTRAIT = 0,       // 480x800 logical coordinates (current default)
    LANDSCAPE_CW = 1,   // 800x480 logical coordinates, rotated 180° (swap top/bottom)
    INVERTED = 2,       // 480x800 logical coordinates, inverted
    LANDSCAPE_CCW = 3,  // 800x480 logical coordinates, native panel orientation
    ORIENTATION_COUNT
  };

  // Front button layout options (legacy)
  // Default: Back, Confirm, Left, Right
  // Swapped: Left, Right, Back, Confirm
  enum FRONT_BUTTON_LAYOUT {
    BACK_CONFIRM_LEFT_RIGHT = 0,
    LEFT_RIGHT_BACK_CONFIRM = 1,
    LEFT_BACK_CONFIRM_RIGHT = 2,
    BACK_CONFIRM_RIGHT_LEFT = 3,
    FRONT_BUTTON_LAYOUT_COUNT
  };

  // Front button hardware identifiers (for remapping)
  enum FRONT_BUTTON_HARDWARE {
    FRONT_HW_BACK = 0,
    FRONT_HW_CONFIRM = 1,
    FRONT_HW_LEFT = 2,
    FRONT_HW_RIGHT = 3,
    FRONT_BUTTON_HARDWARE_COUNT
  };

  // Side button layout options
  // Default: Previous, Next
  // Swapped: Next, Previous
  enum SIDE_BUTTON_LAYOUT { PREV_NEXT = 0, NEXT_PREV = 1, SIDE_BUTTON_LAYOUT_COUNT };

  // Font family options
  enum FONT_FAMILY { BOOKERLY = 0, NOTOSANS = 1, FONT_FAMILY_COUNT };
  static constexpr uint8_t BUILTIN_FONT_COUNT = FONT_FAMILY_COUNT;
  // Font size options
  enum FONT_SIZE { X_SMALL = 0, SMALL = 1, MEDIUM = 2, LARGE = 3, EXTRA_LARGE = 4, FONT_SIZE_COUNT };
  enum TEXT_DARKNESS {
    TEXT_DARKNESS_NORMAL = 0,
    TEXT_DARKNESS_LEGACY_BW = 1,
    TEXT_DARKNESS_DARK = 2,
    TEXT_DARKNESS_EXTRA_DARK = 3,
    TEXT_DARKNESS_COUNT
  };
  enum BIONIC_READING_MODE {
    BIONIC_READING_OFF = 0,
    BIONIC_READING_NORMAL = 1,
    BIONIC_READING_SUBTLE = 2,
    BIONIC_READING_MODE_COUNT
  };
  enum LINE_COMPRESSION { TIGHT = 0, NORMAL = 1, WIDE = 2, EXTRA_WIDE = 3, LINE_COMPRESSION_COUNT };
  enum PARAGRAPH_ALIGNMENT {
    JUSTIFIED = 0,
    LEFT_ALIGN = 1,
    CENTER_ALIGN = 2,
    RIGHT_ALIGN = 3,
    BOOK_STYLE = 4,
    PARAGRAPH_ALIGNMENT_COUNT
  };

  // Auto-sleep timeout options (in minutes)
  enum SLEEP_TIMEOUT {
    SLEEP_1_MIN = 0,
    SLEEP_5_MIN = 1,
    SLEEP_10_MIN = 2,
    SLEEP_15_MIN = 3,
    SLEEP_30_MIN = 4,
    SLEEP_TIMEOUT_COUNT
  };

  // E-ink refresh frequency (pages between full refreshes)
  enum REFRESH_FREQUENCY {
    REFRESH_1 = 0,
    REFRESH_5 = 1,
    REFRESH_10 = 2,
    REFRESH_15 = 3,
    REFRESH_30 = 4,
    REFRESH_FREQUENCY_COUNT
  };

  enum READER_REFRESH_MODE {
    READER_REFRESH_AUTO = 0,
    READER_REFRESH_FAST = 1,
    READER_REFRESH_HALF = 2,
    READER_REFRESH_FULL = 3,
    READER_REFRESH_MODE_COUNT
  };

  // Short power button press actions
  enum SHORT_PWRBTN {
    IGNORE = 0,
    SLEEP = 1,
    PAGE_TURN = 2,
    FORCE_REFRESH = 3,
    TOGGLE_STATUS_BAR = 4,
    SHORT_PWRBTN_COUNT
  };
  enum TILT_PAGE_TURN { TILT_OFF = 0, TILT_NORMAL = 1, TILT_INVERTED = 2, TILT_PAGE_TURN_COUNT };

  // Hide battery percentage
  enum HIDE_BATTERY_PERCENTAGE { HIDE_NEVER = 0, HIDE_READER = 1, HIDE_ALWAYS = 2, HIDE_BATTERY_PERCENTAGE_COUNT };

  // Page turn button long-press behavior
  enum LONG_PRESS_BUTTON_BEHAVIOR {
    LONG_PRESS_OFF = 0,
    LONG_PRESS_CHAPTER_SKIP = 1,
    LONG_PRESS_ORIENTATION_CHANGE = 2,
    LONG_PRESS_BUTTON_BEHAVIOR_COUNT
  };

  // UI Theme
  enum UI_THEME { LYRA = 0, LYRA_CUSTOM = 1, LYRA_CAROUSEL = 2, UI_THEME_COUNT };
  enum DATE_FORMAT { DATE_DD_MM_YYYY = 0, DATE_MM_DD_YYYY = 1, DATE_YYYY_MM_DD = 2, DATE_FORMAT_COUNT };
  enum DISPLAY_HEADER {
    DISPLAY_HEADER_OFF = 0,
    DISPLAY_HEADER_DATE_ONLY = 1,
    DISPLAY_HEADER_TIME_ONLY = 2,
    DISPLAY_HEADER_BOTH = 3,
    DISPLAY_HEADER_MODE_COUNT = 4,
  };
  enum SYNC_DAY_WIFI_CHOICE { SYNC_DAY_WIFI_AUTO = 0, SYNC_DAY_WIFI_MANUAL = 1, SYNC_DAY_WIFI_CHOICE_COUNT };
  enum DAILY_GOAL_TARGET {
    DAILY_GOAL_15_MIN = 0,
    DAILY_GOAL_30_MIN = 1,
    DAILY_GOAL_45_MIN = 2,
    DAILY_GOAL_60_MIN = 3,
    DAILY_GOAL_TARGET_COUNT
  };
  enum READING_STATS_AUTOBACKUP {
    READING_STATS_AUTOBACKUP_OFF = 0,
    READING_STATS_AUTOBACKUP_1_DAY = 1,
    READING_STATS_AUTOBACKUP_7_DAYS = 2,
    READING_STATS_AUTOBACKUP_14_DAYS = 3,
    READING_STATS_AUTOBACKUP_21_DAYS = 4,
    READING_STATS_AUTOBACKUP_COUNT
  };
  enum FLASHCARD_STUDY_MODE {
    FLASHCARD_STUDY_DUE = 0,
    FLASHCARD_STUDY_SCHEDULED = 1,
    FLASHCARD_STUDY_INFINITE = 2,
    FLASHCARD_STUDY_SEQUENTIAL = 3,
    FLASHCARD_STUDY_MODE_COUNT
  };
  enum FLASHCARD_SESSION_SIZE {
    FLASHCARD_SESSION_10 = 0,
    FLASHCARD_SESSION_20 = 1,
    FLASHCARD_SESSION_30 = 2,
    FLASHCARD_SESSION_50 = 3,
    FLASHCARD_SESSION_ALL = 4,
    FLASHCARD_SESSION_SIZE_COUNT
  };
  enum SYNC_DAY_REMINDER_STARTS {
    SYNC_DAY_REMINDER_OFF = 0,
    SYNC_DAY_REMINDER_10 = 1,
    SYNC_DAY_REMINDER_20 = 2,
    SYNC_DAY_REMINDER_30 = 3,
    SYNC_DAY_REMINDER_40 = 4,
    SYNC_DAY_REMINDER_50 = 5,
    SYNC_DAY_REMINDER_60 = 6,
    SYNC_DAY_REMINDER_STARTS_COUNT
  };
  enum OPDS_FILENAME_FORMAT {
    OPDS_FILENAME_AUTHOR_TITLE = 0,
    OPDS_FILENAME_TITLE_AUTHOR = 1,
    OPDS_FILENAME_FORMAT_COUNT
  };
  enum SHORTCUT_LOCATION { SHORTCUT_HOME = 0, SHORTCUT_APPS = 1, SHORTCUT_LOCATION_COUNT };
  enum HOME_BOOK_SOURCE { HOME_BOOKS_RECENTS = 0, HOME_BOOKS_FAVORITES = 1, HOME_BOOK_SOURCE_COUNT };
  enum SLEEP_IMAGE_ORDER { SLEEP_IMAGE_SHUFFLE = 0, SLEEP_IMAGE_SEQUENTIAL = 1, SLEEP_IMAGE_ORDER_COUNT };

  // Image rendering in EPUB reader
  enum IMAGE_RENDERING { IMAGES_DISPLAY = 0, IMAGES_PLACEHOLDER = 1, IMAGES_SUPPRESS = 2, IMAGE_RENDERING_COUNT };

  // Sleep screen settings
  uint8_t sleepScreen = DARK;
  // Sleep screen cover mode settings
  uint8_t sleepScreenCoverMode = FIT;
  // Sleep screen cover filter
  uint8_t sleepScreenCoverFilter = NO_FILTER;
  // Use a full clean refresh when drawing the sleep screen
  uint8_t cleanSleepRefresh = 1;
  // Status bar settings (statusBar retained for migration only)
  uint8_t statusBar = FULL;
  uint8_t statusBarChapterPageCount = 1;
  uint8_t statusBarBookProgressPercentage = 1;
  uint8_t statusBarProgressBar = HIDE_PROGRESS;
  uint8_t statusBarProgressBarThickness = PROGRESS_BAR_NORMAL;
  uint8_t statusBarTitle = CHAPTER_TITLE;
  uint8_t statusBarBattery = 1;
  uint8_t xtcStatusBarMode = XTC_STATUS_BAR_HIDE;
  // Clock display in status bar (X3 only, requires DS3231 RTC)
  uint8_t statusBarClock = STATUS_BAR_CLOCK_HIDE;
  // Legacy migration field for pre-unified timezone clock offset. No longer shown in UI.
  uint8_t clockUtcOffsetQ = 48;
  // Clock display format: 0 = 24-hour, 1 = 12-hour
  uint8_t clockFormat = 0;
  // Set once an NTP sync succeeds. Used to skip re-syncing on every WiFi connect.
  // Resetting to 0 (e.g. via the web UI) forces a re-sync on next WiFi connect.
  uint8_t clockHasBeenSynced = 0;
  // Text rendering settings
  uint8_t extraParagraphSpacing = 1;
  uint8_t forceParagraphIndents = 0;
  uint8_t textAntiAliasing = 1;
  uint8_t textDarkness = TEXT_DARKNESS_NORMAL;
  // Short power button click behaviour
  uint8_t shortPwrBtn = IGNORE;
  // Tilt-based page turning (X3 only, requires QMI8658 IMU)
  uint8_t tiltPageTurn = TILT_OFF;
  // EPUB reading orientation settings
  // 0 = portrait (default), 1 = landscape clockwise, 2 = inverted, 3 = landscape counter-clockwise
  uint8_t orientation = PORTRAIT;
  // Button layouts (front layout retained for migration only)
  uint8_t frontButtonLayout = BACK_CONFIRM_LEFT_RIGHT;
  uint8_t sideButtonLayout = PREV_NEXT;
  uint8_t frontButtonFollowOrientation = 0;
  // Front button remap (logical -> hardware)
  // Used by MappedInputManager to translate logical buttons into physical front buttons.
  uint8_t frontButtonBack = FRONT_HW_BACK;
  uint8_t frontButtonConfirm = FRONT_HW_CONFIRM;
  uint8_t frontButtonLeft = FRONT_HW_LEFT;
  uint8_t frontButtonRight = FRONT_HW_RIGHT;
  // Reader font settings
  uint8_t fontFamily = BOOKERLY;
  uint8_t fontSize = MEDIUM;
  uint8_t lineSpacing = NORMAL;
  uint8_t paragraphAlignment = JUSTIFIED;
  // Auto-sleep timeout setting (default 10 minutes)
  uint8_t sleepTimeout = SLEEP_10_MIN;
  // E-ink refresh frequency (default 15 pages)
  uint8_t refreshFrequency = REFRESH_15;
  // Reader refresh override (default auto)
  uint8_t readerRefreshMode = READER_REFRESH_AUTO;
  uint8_t hyphenationEnabled = 0;
  uint8_t bionicReading = 0;
  char sdFontFamilyName[32] = "";

  // Reader screen margin settings
  uint8_t screenMargin = 5;
  // OPDS browser settings
  char opdsServerUrl[128] = "";
  char opdsUsername[64] = "";
  char opdsPassword[64] = "";
  uint8_t opdsFilenameFormat = OPDS_FILENAME_AUTHOR_TITLE;
  uint8_t koSyncAutoPullOnOpen = 0;
  uint8_t koSyncAutoPushOnClose = 0;
  // Hide battery percentage
  uint8_t hideBatteryPercentage = HIDE_NEVER;
  // Page turn button long-press behavior
  uint8_t longPressButtonBehavior = LONG_PRESS_CHAPTER_SKIP;
  // UI Theme
  uint8_t uiTheme = LYRA_CUSTOM;
  // Experimental global dark mode for the device UI and supported readers.
  uint8_t darkMode = 0;
  uint8_t antiGhostingExperimental = 0;
  // Home/apps helpers
  uint8_t displayDay = 1;
  uint8_t autoSyncDay = 1;
  uint8_t homeBookSource = HOME_BOOKS_RECENTS;
  uint8_t syncDayWifiChoice = SYNC_DAY_WIFI_AUTO;
  uint8_t syncDayReminderStarts = SYNC_DAY_REMINDER_20;
  char sleepDirectory[128] = "";
  uint8_t sleepImageOrder = SLEEP_IMAGE_SHUFFLE;
  uint8_t timeZonePreset = 0;
  uint8_t dateFormat = DATE_DD_MM_YYYY;
  uint8_t dailyGoalTarget = DAILY_GOAL_30_MIN;
  uint8_t readingStatsAutoBackup = READING_STATS_AUTOBACKUP_7_DAYS;
  uint8_t flashcardStudyMode = FLASHCARD_STUDY_DUE;
  uint8_t flashcardSessionSize = FLASHCARD_SESSION_ALL;
  uint8_t showStatsAfterReading = 1;
  uint8_t moveCompletedBooks = 0;
  uint8_t achievementsEnabled = 1;
  uint8_t achievementPopups = 1;
  uint8_t appsHubShortcutOrder = 1;
  uint8_t browseFilesShortcut = SHORTCUT_HOME;
  uint8_t browseFilesShortcutOrder = 0;
  // Legacy Stats shortcut fields retained for settings.json migration to readingStatsShortcut.
  uint8_t statsShortcut = SHORTCUT_HOME;
  uint8_t statsShortcutOrder = 2;
  uint8_t syncDayShortcut = SHORTCUT_HOME;
  uint8_t syncDayShortcutOrder = 3;
  uint8_t settingsShortcut = SHORTCUT_HOME;
  uint8_t settingsShortcutOrder = 4;
  uint8_t readingStatsShortcut = SHORTCUT_APPS;
  uint8_t readingStatsShortcutOrder = 5;
  uint8_t readingHeatmapShortcut = SHORTCUT_APPS;
  uint8_t readingHeatmapShortcutOrder = 6;
  uint8_t readingProfileShortcut = SHORTCUT_APPS;
  uint8_t readingProfileShortcutOrder = 7;
  uint8_t achievementsShortcut = SHORTCUT_APPS;
  uint8_t achievementsShortcutOrder = 8;
  uint8_t ifFoundShortcut = SHORTCUT_APPS;
  uint8_t ifFoundShortcutOrder = 9;
  uint8_t readMeShortcut = SHORTCUT_APPS;
  uint8_t readMeShortcutOrder = 10;
  uint8_t recentBooksShortcut = SHORTCUT_APPS;
  uint8_t recentBooksShortcutOrder = 11;
  uint8_t bookmarksShortcut = SHORTCUT_APPS;
  uint8_t bookmarksShortcutOrder = 12;
  uint8_t favoritesShortcut = SHORTCUT_APPS;
  uint8_t favoritesShortcutOrder = 13;
  uint8_t flashcardsShortcut = SHORTCUT_APPS;
  uint8_t flashcardsShortcutOrder = 14;
  uint8_t dictionaryShortcut = SHORTCUT_APPS;
  uint8_t dictionaryShortcutOrder = 15;
  uint8_t fileTransferShortcut = SHORTCUT_APPS;
  uint8_t fileTransferShortcutOrder = 16;
  uint8_t screenCleanShortcut = SHORTCUT_APPS;
  uint8_t screenCleanShortcutOrder = 17;
  uint8_t sleepShortcut = SHORTCUT_APPS;
  uint8_t sleepShortcutOrder = 18;
  uint8_t opdsBrowserShortcut = SHORTCUT_HOME;
  uint8_t opdsBrowserShortcutOrder = 19;
  uint8_t browseFilesShortcutVisible = 1;
  // Legacy Stats shortcut visibility retained for settings.json migration to readingStatsShortcut.
  uint8_t statsShortcutVisible = 1;
  uint8_t syncDayShortcutVisible = 1;
  uint8_t settingsShortcutVisible = 1;
  uint8_t readingStatsShortcutVisible = 1;
  uint8_t readingHeatmapShortcutVisible = 1;
  uint8_t readingProfileShortcutVisible = 1;
  uint8_t achievementsShortcutVisible = 1;
  uint8_t ifFoundShortcutVisible = 1;
  uint8_t readMeShortcutVisible = 1;
  uint8_t recentBooksShortcutVisible = 1;
  uint8_t bookmarksShortcutVisible = 1;
  uint8_t favoritesShortcutVisible = 1;
  uint8_t flashcardsShortcutVisible = 1;
  uint8_t dictionaryShortcutVisible = 1;
  uint8_t fileTransferShortcutVisible = 1;
  uint8_t screenCleanShortcutVisible = 1;
  uint8_t sleepShortcutVisible = 1;
  uint8_t opdsBrowserShortcutVisible = 1;
  // Sunlight fading compensation
  uint8_t fadingFix = 0;
  // Use book's embedded CSS styles for EPUB rendering (1 = enabled, 0 = disabled)
  uint8_t embeddedStyle = 1;
  // Show hidden files/directories (starting with '.') in the file browser (0 = hidden, 1 = show)
  uint8_t showHiddenFiles = 0;
  // Hide the file-browser extension value so long titles get more row width.
  uint8_t hideFileExtension = 0;
  // Image rendering mode in EPUB reader
  uint8_t imageRendering = IMAGES_DISPLAY;

  ~CrossPointSettings() = default;

  // Get singleton instance
  static CrossPointSettings& getInstance() { return instance; }

  using SdFontIdResolver = int (*)(void* ctx, const char* familyName, uint8_t fontSize);
  SdFontIdResolver sdFontIdResolver = nullptr;
  void* sdFontResolverCtx = nullptr;

  uint16_t getPowerButtonDuration() const {
    return (shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP) ? 10 : 400;
  }
  int getReaderFontId() const;

  // If count_only is true, returns the number of settings items that would be written.
  uint8_t writeSettings(FsFile& file, bool count_only = false) const;

  bool saveToFile() const;
  bool loadFromFile();

  static void validateFrontButtonMapping(CrossPointSettings& settings);

 private:
  bool loadFromBinaryFile();

 public:
  float getReaderLineCompression() const;
  unsigned long getSleepTimeoutMs() const;
  uint64_t getDailyGoalMs() const;
  uint8_t getReadingStatsAutoBackupIntervalDays() const;
  uint8_t getSyncDayReminderStartThreshold() const;
  uint8_t getEffectiveSyncDayReminderStartThreshold() const;
  bool isHardwareRtcAutoDayClockActive() const;
  bool shouldShowHeaderDate() const;
  bool shouldShowHeaderTime() const;
  // Clamps corrupt displayDay values on load. Does not downgrade time/both modes when the RTC
  // is temporarily unavailable; shouldShowHeaderDate/Time gate runtime display instead.
  void normalizeDisplayDay();
  int getRefreshFrequency() const;
  bool getForcedReaderRefreshMode(HalDisplay::RefreshMode& mode) const;
};

// Helper macro to access settings
#define SETTINGS CrossPointSettings::getInstance()
