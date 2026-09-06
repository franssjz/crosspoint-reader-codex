#pragma once
#include <cstdint>
#include <string>

enum class KOReaderSyncIntentState : uint8_t {
  COMPARE = 0,
  PULL_REMOTE = 1,
  PUSH_LOCAL = 2,
  AUTO_PUSH = 3,
  AUTO_PULL = 4,
};

enum class KOReaderSyncOutcomeState : uint8_t {
  NONE = 0,
  PENDING = 1,
  CANCELLED = 2,
  FAILED = 3,
  UPLOAD_COMPLETE = 4,
  APPLIED_REMOTE = 5,
};

struct PendingBookmarkJumpState {
  bool active = false;
  std::string bookPath;
  uint16_t spineIndex = 0;
  uint16_t pageNumber = 0;
  void clear();
};

struct KOReaderSyncSessionState {
  bool active = false;
  std::string epubPath;
  int spineIndex = 0;
  int page = 0;
  int totalPagesInSpine = 0;
  uint16_t paragraphIndex = 0;
  bool hasParagraphIndex = false;
  uint32_t xhtmlSeekHint = 0;
  bool hasLocalKoReaderPosition = false;
  std::string localKoReaderProgress;
  float localKoReaderPercentage = 0.0f;
  std::string localChapterLabel;
  KOReaderSyncIntentState intent = KOReaderSyncIntentState::COMPARE;
  KOReaderSyncOutcomeState outcome = KOReaderSyncOutcomeState::NONE;
  int resultSpineIndex = 0;
  int resultPage = 0;
  uint16_t resultParagraphIndex = 0;
  bool resultHasParagraphIndex = false;
  // Mirrors CrossPointPosition (lib/KOReaderSync/ProgressMapper.h) after the
  // upstream merge: liIndex/hasLiIndex replace listItemIndex/hasListItemIndex,
  // visibleTextOffset and xpathAnchorId are new.
  uint16_t resultLiIndex = 0;
  bool resultHasLiIndex = false;
  uint32_t resultVisibleTextOffset = 0;
  bool resultHasVisibleTextOffset = false;
  std::string resultXpathAnchorId;
  bool exitToHomeAfterSync = false;
  std::string autoPullEpubPath;
  void clear();
};

class CrossPointState {
  // Static instance
  static CrossPointState instance;

 public:
  static constexpr uint8_t SLEEP_RECENT_COUNT = 16;

  std::string openEpubPath;
  uint16_t recentSleepImages[SLEEP_RECENT_COUNT] = {};  // circular buffer of recent wallpaper indices
  uint8_t recentSleepPos = 0;                           // next write slot
  uint8_t recentSleepFill = 0;                          // valid entries (0..SLEEP_RECENT_COUNT)
  // Same ring for the transparent overlay sleep-screen images (upstream TRANSPARENT_CUSTOM).
  uint16_t recentOverlaySleepImages[SLEEP_RECENT_COUNT] = {};
  uint8_t recentOverlaySleepPos = 0;
  uint8_t recentOverlaySleepFill = 0;
  uint8_t readerActivityLoadCount = 0;
  bool lastSleepFromReader = false;
  // Cleared once the boot screen has been shown; set again before a persisted
  // sleep so the next cold boot shows it (upstream Quick Resume / persisted sleep wake).
  bool showBootScreen = true;
  uint32_t lastKnownValidTimestamp = 0;
  uint32_t lastReadingStatsBackupDayOrdinal = 0;
  uint8_t syncDayReminderStartCount = 0;
  bool syncDayReminderLatched = false;
  KOReaderSyncSessionState koReaderSyncSession;
  PendingBookmarkJumpState pendingBookmarkJump;
  ~CrossPointState() = default;

  // Get singleton instance
  static CrossPointState& getInstance() { return instance; }

  bool saveToFile();

  bool loadFromFile();
  bool isRecentSleep(uint16_t idx, uint8_t checkCount) const;
  bool isRecentOverlaySleep(uint16_t idx, uint8_t checkCount) const;
  void pushRecentSleep(uint16_t idx);
  void pushRecentOverlaySleep(uint16_t idx);
  uint16_t getMostRecentSleepIndex() const;
  void recordUsefulStart(uint8_t reminderThreshold);
  void registerValidTimeSync(uint32_t validTimestamp);
  bool shouldShowSyncDayReminder(uint8_t reminderThreshold) const;

 private:
  bool loadFromBinaryFile();
};

// Helper macro to access settings
#define APP_STATE CrossPointState::getInstance()
