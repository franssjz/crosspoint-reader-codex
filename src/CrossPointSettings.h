#pragma once
#include <Epub/ReaderRenderSpec.h>
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
    // Upstream additions, appended so persisted fork indices stay valid.
    QUICK_RESUME = 11,
    TRANSPARENT_CUSTOM = 12,
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
    STATUS_BAR_CLOCK_COUNT,
    STATUS_BAR_CLOCK_MODE_COUNT = STATUS_BAR_CLOCK_COUNT  // upstream name
  };
  using STATUS_BAR_CLOCK_MODE = STATUS_BAR_CLOCK;  // upstream name

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
  enum SIDE_BUTTON_LAYOUT { PREV_NEXT = 0, NEXT_PREV = 1, SIDE_BUTTONS_DISABLED = 2, SIDE_BUTTON_LAYOUT_COUNT };

  // Font family options (built-in fonts only; SD card fonts use sdFontFamilyName).
  // NOTOSERIF is upstream's name for slot 0 (the fork ships Bookerly there).
  enum FONT_FAMILY { BOOKERLY = 0, NOTOSERIF = 0, NOTOSANS = 1, FONT_FAMILY_COUNT };
  static constexpr uint8_t BUILTIN_FONT_COUNT = FONT_FAMILY_COUNT;
  // Legacy font size slots. The reader font size is a point size since the
  // upstream merge (see fontPointSize); files written by older fork builds hold
  // one of these 0..4 slots under "fontSize" and are folded on load.
  enum FONT_SIZE { X_SMALL = 0, SMALL = 1, MEDIUM = 2, LARGE = 3, EXTRA_LARGE = 4, FONT_SIZE_COUNT };
  static constexpr uint8_t LEGACY_FONT_SIZE_MAX = EXTRA_LARGE;
  static constexpr uint8_t DEFAULT_FONT_POINT_SIZE = 14;
  // Slot -> point size the slot used to render at (0..4 -> 10,12,14,16,18).
  static constexpr uint8_t legacyFontSizeSlotToPointSize(const uint8_t slot) {
    return static_cast<uint8_t>(10 + (slot <= LEGACY_FONT_SIZE_MAX ? slot : MEDIUM) * 2);
  }
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

  // Legacy auto-sleep timeout options (migration only; see sleepTimeoutMinutes)
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
    REFRESH_NEVER = 5,
    REFRESH_FREQUENCY_COUNT
  };

  enum READER_REFRESH_MODE {
    READER_REFRESH_AUTO = 0,
    READER_REFRESH_FAST = 1,
    READER_REFRESH_HALF = 2,
    READER_REFRESH_FULL = 3,
    READER_REFRESH_MODE_COUNT
  };

  // Short power button press actions. Persisted by index: TOGGLE_STATUS_BAR is
  // the fork's value 4, upstream's FOOTNOTES / PWR_CONFIRM are appended after it.
  // Any enum label list (SettingsList, web UI) must follow this order.
  enum SHORT_PWRBTN {
    IGNORE = 0,
    SLEEP = 1,
    PAGE_TURN = 2,
    FORCE_REFRESH = 3,
    TOGGLE_STATUS_BAR = 4,
    FOOTNOTES = 5,
    PWR_CONFIRM = 6,
    SHORT_PWRBTN_COUNT
  };
  enum TILT_PAGE_TURN {
    TILT_OFF = 0,
    TILT_NORMAL = 1,
    TILT_INVERTED = 2,
    TILT_NVERTED = TILT_INVERTED,  // upstream spelling
    TILT_PAGE_TURN_COUNT
  };

  // Long-press Confirm action while reading an EPUB. The setting cycles through these values.
  // Persisted in settings.json by index: any new function MUST use a value >= 2 and be appended at
  // the END of the enumValues array in SettingsList, otherwise stored indices shift.
  enum LONG_PRESS_MENU_FUNCTION {
    LP_MENU_KOSYNC = 0,
    LP_MENU_DISABLED = 1,
    LP_MENU_BOOKMARK = 2,
    LP_MENU_DICTIONARY = 3,
    LP_MENU_READER_MENU = 4,
    LONG_PRESS_MENU_FUNCTION_COUNT
  };

  // Hide battery percentage
  enum HIDE_BATTERY_PERCENTAGE { HIDE_NEVER = 0, HIDE_READER = 1, HIDE_ALWAYS = 2, HIDE_BATTERY_PERCENTAGE_COUNT };

  // Page turn button long-press behavior (fork names first, upstream names as aliases)
  enum LONG_PRESS_BUTTON_BEHAVIOR {
    LONG_PRESS_OFF = 0,
    LONG_PRESS_CHAPTER_SKIP = 1,
    LONG_PRESS_ORIENTATION_CHANGE = 2,
    OFF = LONG_PRESS_OFF,
    CHAPTER_SKIP = LONG_PRESS_CHAPTER_SKIP,
    ORIENTATION_CHANGE = LONG_PRESS_ORIENTATION_CHANGE,
    LONG_PRESS_BUTTON_BEHAVIOR_COUNT = 3
  };

  // UI Theme. Fork values first; upstream's themes appended in this exact order so
  // persisted fork indices stay valid (upstream's own numbering differs).
  enum UI_THEME {
    LYRA = 0,
    LYRA_CUSTOM = 1,
    LYRA_CAROUSEL = 2,
    CLASSIC = 3,
    ROUNDEDRAFF = 4,
    LYRA_3_COVERS = 5,
    UI_THEME_COUNT
  };
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

  // How Select opens the reader menu: the classic full-screen list, or a toolbar
  // overlay (top/bottom bars with Contents / Text / More bottom-sheet panels).
  enum READER_MENU_STYLE { READER_MENU_LIST = 0, READER_MENU_TOOLBAR = 1, READER_MENU_STYLE_COUNT };

  enum TOUCH_READER_CONTROLS {
    TOUCH_READER_OFF = 0,
    TOUCH_READER_ON = 1,
    TOUCH_READER_SWIPE = 2,
    TOUCH_READER_INVERTED_TAP = 3,
    TOUCH_READER_CONTROLS_COUNT
  };

  // How the reader menu opens on touch boards. Persisted under the legacy
  // "tapForReaderMenu" key: 0/1 keep their old Off/Tap meaning.
  enum SHOW_READER_MENU { READER_MENU_OFF = 0, READER_MENU_TAP = 1, READER_MENU_SWIPE_UP = 2, SHOW_READER_MENU_COUNT };

  enum QUICK_RESUME_SLEEP_SCREEN {
    QUICK_RESUME_NEVER = 0,
    QUICK_RESUME_AFTER_TIMEOUT = 1,
    QUICK_RESUME_SLEEP_SCREEN_COUNT
  };

  // Reader screen margin limits
  static constexpr uint8_t SCREEN_MARGIN_MIN = 5;
  static constexpr uint8_t SCREEN_MARGIN_MAX = 40;
  static constexpr uint8_t SCREEN_MARGIN_STEP = 5;

  // Auto-sleep timeout limits (minutes); SLEEP_TIMEOUT_NEVER_MINUTES disables auto-sleep.
  static constexpr uint8_t MIN_SLEEP_TIMEOUT_MINUTES = 1;
  static constexpr uint8_t SLEEP_TIMEOUT_NEVER_MINUTES = 31;
  static constexpr uint8_t MAX_SLEEP_TIMEOUT_MINUTES = SLEEP_TIMEOUT_NEVER_MINUTES;

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
  // Clock UTC offset in quarter-hour steps, biased by 48 so it fits in uint8_t
  // (48 = UTC+0, 0 = UTC-12:00, 104 = UTC+14:00). The fork's Sync Day timezone
  // preset (timeZonePreset) drives the on-device clock; this field is kept for
  // migration and for upstream's ClockOffsetActivity / web settings.
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
  // Point size of the reader font. Only sizes the active family actually ships
  // are selectable; SdCardFontSystem::ensureLoaded() snaps this to the nearest
  // available size (and persists the snap) whenever the family changes.
  uint8_t fontPointSize = DEFAULT_FONT_POINT_SIZE;
  uint8_t lineSpacing = NORMAL;
  uint8_t paragraphAlignment = JUSTIFIED;
  // Auto-sleep timeout in minutes (default 10). Legacy SLEEP_TIMEOUT enum values are migration-only.
  uint8_t sleepTimeoutMinutes = 10;
  // E-ink refresh frequency (default 15 pages)
  uint8_t refreshFrequency = REFRESH_15;
  // Reader refresh override (default auto)
  uint8_t readerRefreshMode = READER_REFRESH_AUTO;
  uint8_t hyphenationEnabled = 0;
  // Bionic / focus reading mode (BIONIC_READING_MODE). Upstream code reads and
  // toggles this through the focusReadingEnabled alias below.
  uint8_t bionicReading = 0;
  char sdFontFamilyName[32] = "";
  // Dictionary folder name under /dictionaries (upstream setting, empty = none).
  // The fork's DictionaryStore (DICTIONARIES) remains authoritative for its own
  // StarDict feature; this field is persisted for upstream-derived code paths.
  char dictionaryName[32] = "";

  // Reader screen margin settings
  uint8_t screenMargin = SCREEN_MARGIN_MIN;
  // OPDS browser settings
  char opdsServerUrl[128] = "";
  char opdsUsername[64] = "";
  char opdsPassword[64] = "";
  // OPDS download destination folder ("" = SD root). Global default; the fork's
  // per-server directories (OpdsServerStore) take precedence where configured.
  char opdsDownloadFolder[64] = "";
  uint8_t opdsFilenameFormat = OPDS_FILENAME_AUTHOR_TITLE;
  uint8_t koSyncAutoPullOnOpen = 0;
  uint8_t koSyncAutoPushOnClose = 0;
  // Hide battery percentage
  uint8_t hideBatteryPercentage = HIDE_NEVER;
  // Page turn button long-press behavior
  uint8_t longPressButtonBehavior = LONG_PRESS_CHAPTER_SKIP;
  // Long-press Confirm function in EPUB reader (cycles through LONG_PRESS_MENU_FUNCTION values).
  // Fork default is Bookmark: the fork's reader always toggled a page mark on a long Confirm
  // press, and that gesture now routes through this setting (upstream defaults to Disabled).
  uint8_t longPressMenuFunction = LP_MENU_BOOKMARK;
  // UI Theme
  uint8_t uiTheme = LYRA_CUSTOM;
  // Global dark mode / night mode (inverted output polarity). Upstream calls this
  // screenInverted; see the alias below.
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
  // Power button return from footnotes (1 = enabled, 0 = disabled)
  uint8_t pwrBtnFootnoteBack = 1;
  // Use book's embedded CSS styles for EPUB rendering (1 = enabled, 0 = disabled)
  uint8_t embeddedStyle = 1;
  uint8_t readerMenuStyle = READER_MENU_LIST;
  // Show hidden files/directories (starting with '.') in the file browser (0 = hidden, 1 = show)
  uint8_t showHiddenFiles = 0;
  // Hide the file-browser extension value so long titles get more row width.
  uint8_t hideFileExtension = 0;
  // Remove a book from the Recent Books list when its End-of-Book screen is reached (0 = off, 1 = on)
  uint8_t removeReadBooksFromRecents = 0;
  // Short press Back goes to file browser instead of home (0 = disabled, 1 = enabled)
  uint8_t backShortToFileBrowser = 0;
  // Image rendering mode in EPUB reader
  uint8_t imageRendering = IMAGES_DISPLAY;
  // Touch screen reader zones/gestures on boards with a touch controller.
  uint8_t touchReaderControls = TOUCH_READER_SWIPE;
  // Reader menu open gesture (SHOW_READER_MENU: off / center tap / bottom-edge
  // up-swipe). Only surfaced on home-key boards; elsewhere it stays at the Tap default.
  uint8_t showReaderMenu = READER_MENU_TAP;
  // Frontlight quick-panel state. Category-less SettingsList entries persist
  // these without adding them to the regular Settings screen.
  uint8_t frontlightBrightness = 60;
  uint8_t frontlightWarmth = 50;  // 0 = cool .. 100 = warm
  uint8_t frontlightOn = 0;
  // Restore the saved on/off state after a normal boot or wake. Brightness and
  // warmth are always remembered even when this is disabled.
  uint8_t frontlightRestoreOnWake = 1;
  // Language setting (Language enum index, default 0 = EN). Persisted as an ISO
  // code string for stability across enum reorders.
  uint8_t language = 0;
  // Keyboard layouts the user can reach, using keyboard_layouts::ALL table bits.
  // 0 means "not configured", resolved to the UI language's layout plus English.
  uint16_t keyboardLayouts = 0;
  // Quick Resume: keep current content visible with moon icon instead of showing a static sleep screen.
  uint8_t quickResumeSleepScreen = QUICK_RESUME_NEVER;

  // ---- Upstream field-name aliases ----
  // Upstream code reads and assigns these as plain fields. Each is a reference
  // to the fork field that carries the persisted value (one JSON key each), so
  // both names always agree. Member pointers must use the fork name
  // (&CrossPointSettings::darkMode etc.); a pointer to a reference member is ill-formed.
  uint8_t& screenInverted = darkMode;
  uint8_t& focusReadingEnabled = bionicReading;
  uint8_t& moveFinishedToReadFolder = moveCompletedBooks;

  ~CrossPointSettings() = default;

  // Get singleton instance
  static CrossPointSettings& getInstance() { return instance; }

  using SdFontIdResolver = int (*)(void* ctx, const char* familyName, uint8_t pointSize);
  SdFontIdResolver sdFontIdResolver = nullptr;
  void* sdFontResolverCtx = nullptr;

  uint16_t getPowerButtonDuration() const {
    return (shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP) ? 10 : 400;
  }
  int getReaderFontId() const;

  // Drop the SD font selection and fall back to the built-in family. The reader
  // point size comes back into BUILTIN_READER_POINT_SIZES with it, since that is
  // the only set a built-in family ships. Both fields are persisted in one write.
  void clearSdFontFamily();

  // Resolved status-bar composition. Consumers read the spec; only settings
  // editors read the raw fields. Deliberately unlocked: every field it reads is
  // a single byte, so the worst case is one frame drawn with a mixed status bar.
  struct StatusBarSpec {
    bool showChapterPageCount = false;
    bool showBookProgressPercent = false;
    uint8_t titleMode = HIDE_TITLE;  // STATUS_BAR_TITLE
    bool showBattery = false;
    bool showBatteryPercent = false;
    uint8_t clockMode = STATUS_BAR_CLOCK_HIDE;  // STATUS_BAR_CLOCK
    bool clock12h = false;
    uint8_t clockUtcOffsetQ = 48;             // 48 = UTC+0
    uint8_t progressBarMode = HIDE_PROGRESS;  // STATUS_BAR_PROGRESS_BAR
    uint8_t progressBarHeightPx = 0;          // (thickness+1)*2; 0 when the bar is hidden
    uint8_t xtcMode = XTC_STATUS_BAR_HIDE;    // XTC_STATUS_BAR_MODE

    bool showsProgressBar() const { return progressBarMode != HIDE_PROGRESS; }
    bool showsTitle() const { return titleMode != HIDE_TITLE; }
    bool showsClock() const { return clockMode != STATUS_BAR_CLOCK_HIDE; }
    // Visibility of the text lane. Clock hardware presence is the caller's
    // concern: pass halClock.isAvailable(), or true for layout reservation.
    bool textLaneVisible(bool clockAvailable) const {
      return showChapterPageCount || showBookProgressPercent || showsTitle() || showBattery ||
             (showsClock() && clockAvailable);
    }
  };
  StatusBarSpec statusBarSpec() const;

  // Resolved text-rendering configuration for the Epub layout engine. The
  // viewport is renderer/orientation-derived, so the caller supplies it.
  ReaderRenderSpec readerRenderSpec(uint16_t viewportWidth, uint16_t viewportHeight) const;

  // If count_only is true, returns the number of settings items that would be written.
  uint8_t writeSettings(HalFile& file, bool count_only = false) const;

  bool saveToFile() const;
  bool loadFromFile();

  static void validateFrontButtonMapping(CrossPointSettings& settings);
  static uint8_t sleepTimeoutEnumToMinutes(uint8_t legacyValue);

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
