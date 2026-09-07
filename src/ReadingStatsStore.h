#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "ReadingSessionLog.h"

inline uint64_t getDailyReadingGoalMs() { return SETTINGS.getDailyGoalMs(); }

struct ReadingDayStats {
  uint32_t dayOrdinal = 0;
  uint64_t readingMs = 0;
};

struct ReadingBookStats {
  std::string bookId;
  std::string path;
  std::vector<std::string> knownPaths;
  std::string title;
  std::string author;
  std::string coverBmpPath;
  std::string chapterTitle;
  std::vector<ReadingDayStats> readingDays;
  uint64_t totalReadingMs = 0;
  uint32_t sessions = 0;
  uint32_t lastSessionMs = 0;
  uint32_t firstReadAt = 0;
  uint32_t lastReadAt = 0;
  uint32_t completedAt = 0;
  uint8_t lastProgressPercent = 0;
  uint8_t chapterProgressPercent = 0;
  bool completed = false;
};

struct ReadingSessionSnapshot {
  bool valid = false;
  uint32_t serial = 0;
  std::string bookId;
  std::string path;
  uint32_t sessionMs = 0;
  bool counted = false;
  bool completedThisSession = false;
  uint8_t startProgressPercent = 0;
  uint8_t endProgressPercent = 0;
};

class ReadingStatsStore;
namespace JsonSettingsIO {
bool saveReadingStats(const ReadingStatsStore& store, const char* path);
bool loadReadingStats(ReadingStatsStore& store, const char* json);
bool loadReadingStatsFromFile(ReadingStatsStore& store, const char* path);
bool loadReadingStatsDocument(ReadingStatsStore& store, const JsonDocument& doc);
}  // namespace JsonSettingsIO

class ReadingStatsStore {
  static ReadingStatsStore instance;

  struct SummaryCache {
    bool valid = false;
    uint32_t referenceDayOrdinal = 0;
    uint32_t booksFinishedCount = 0;
    uint64_t totalReadingMs = 0;
    uint64_t todayReadingMs = 0;
    uint64_t recent7ReadingMs = 0;
    uint64_t recent30ReadingMs = 0;
    uint32_t currentStreakDays = 0;
    uint32_t maxStreakDays = 0;
    uint64_t goalReadingMs = 0;
  };

  struct SessionState {
    bool active = false;
    size_t bookIndex = 0;
    unsigned long lastInteractionMs = 0;
    uint64_t accumulatedMs = 0;
    uint8_t startProgressPercent = 0;
    bool startCompleted = false;
  };

  std::vector<ReadingBookStats> books;
  std::vector<ReadingDayStats> legacyReadingDays;
  std::vector<ReadingDayStats> readingDays;
  std::vector<ReadingSessionLogEntry> sessionLog;
  SessionState activeSession;
  ReadingSessionSnapshot lastSessionSnapshot;
  uint32_t sessionSerialCounter = 0;
  mutable SummaryCache summaryCache;
  mutable bool dirty = false;
  mutable unsigned long lastSaveMs = 0;
  mutable bool persistenceSuspended = false;
  mutable bool skippedSaveLogged = false;
  mutable bool internalBackupPrepared = false;

  friend bool JsonSettingsIO::saveReadingStats(const ReadingStatsStore&, const char*);
  friend bool JsonSettingsIO::loadReadingStats(ReadingStatsStore&, const char*);
  friend bool JsonSettingsIO::loadReadingStatsFromFile(ReadingStatsStore&, const char*);
  friend bool JsonSettingsIO::loadReadingStatsDocument(ReadingStatsStore&, const JsonDocument&);

  size_t findBookIndexByPath(const std::string& path) const;
  size_t findBookIndexByBookId(const std::string& bookId) const;
  size_t findLegacyMergeCandidate(const std::string& path, const std::string& title = "",
                                  const std::string& author = "") const;
  void mergeBookInto(ReadingBookStats& primary, const ReadingBookStats& duplicate);
  void normalizeBook(ReadingBookStats& book);
  void normalizeBooks();
  void rememberBookPath(ReadingBookStats& book, const std::string& path);
  void rememberBookIdAlias(ReadingBookStats& book, const std::string& bookId);
  size_t getOrCreateBookIndex(const std::string& path, const std::string& title, const std::string& author,
                              const std::string& coverBmpPath, const std::string& preferredBookId = "");
  void touchBook(size_t index);
  ReadingDayStats& getOrCreateReadingDay(uint32_t epochSeconds);
  ReadingDayStats& getOrCreateBookReadingDay(ReadingBookStats& book, uint32_t epochSeconds);
  uint32_t getLatestKnownTimestamp() const;
  uint32_t getReferenceTimestamp(uint32_t preferredTimestamp, uint32_t bookTimestamp = 0) const;
  uint32_t getReferenceDayOrdinal() const;
  void updateBookReadTimestamp(ReadingBookStats& book, uint32_t preferredTimestamp);
  void recordReadingTime(ReadingBookStats& book, uint32_t epochSeconds, uint64_t readingMs);
  void appendSessionLogEntry(uint32_t dayOrdinal, uint32_t sessionMs, const ReadingBookStats& book);
  bool convertLegacyReadingDaysToUnassigned();
  void rebuildAggregatedReadingDays();
  bool removeIgnoredBooks();
  bool hasAnyStats() const;
  void invalidateSummaryCache();
  void rebuildSummaryCache() const;
  bool shouldSaveDeferred() const;
  void markDirty();
  bool prepareInternalBackup() const;
  bool refreshInternalBackupFromMain() const;
  bool restoreInternalBackupToMain(const char* reason) const;
  bool maybeCreateAutoBackup(bool force) const;
  bool persistToFile(const char* path) const;
  static bool isClockValid(uint32_t epochSeconds);

 public:
  ~ReadingStatsStore() = default;

  static ReadingStatsStore& getInstance() { return instance; }

  void beginSession(const std::string& path, const std::string& title, const std::string& author,
                    const std::string& coverBmpPath, uint8_t progressPercent = 0, const std::string& chapterTitle = "",
                    uint8_t chapterProgressPercent = 0);
  void noteActivity();
  void tickActiveSession();
  void resumeSession();
  void updateProgress(uint8_t progressPercent, bool completed = false, const std::string& chapterTitle = "",
                      uint8_t chapterProgressPercent = 0);
  void endSession();
  bool adjustBookReadingTime(const std::string& path, uint32_t dayOrdinal, int32_t deltaMs);
  bool setBookFirstReadDate(const std::string& path, uint32_t dayOrdinal);
  bool updateBookMetadata(const std::string& path, const std::string& title, const std::string& author,
                          const std::string& coverBmpPath);
  bool updateBookPath(const std::string& oldKey, const std::string& newPath, const std::string& title = "",
                      const std::string& author = "", const std::string& coverBmpPath = "",
                      const std::string& bookId = "");
  bool removeBook(const std::string& path);
  const ReadingBookStats* findBook(const std::string& key) const;
  const ReadingBookStats* findMatchingBookForPath(const std::string& path, const std::string& title = "",
                                                  const std::string& author = "") const;
  const ReadingSessionSnapshot& getLastSessionSnapshot() const { return lastSessionSnapshot; }

  const std::vector<ReadingBookStats>& getBooks() const { return books; }
  const std::vector<ReadingDayStats>& getReadingDays() const { return readingDays; }
  const std::vector<ReadingSessionLogEntry>& getSessionLog() const { return sessionLog; }
  static bool shouldIgnorePath(const std::string& path);

  uint32_t getBooksStartedCount() const { return static_cast<uint32_t>(books.size()); }
  uint32_t getBooksFinishedCount() const;
  uint64_t getTotalReadingMs() const;
  uint64_t getTodayReadingMs() const;
  uint64_t getRecentReadingMs(uint32_t days) const;
  uint32_t getCurrentStreakDays() const;
  uint32_t getMaxStreakDays() const;
  uint32_t getDisplayTimestamp(bool* usedFallback = nullptr) const;
  bool hasReadingDays() const { return !readingDays.empty(); }

  void reset();
  bool exportToFile(const std::string& path) const;
  bool importFromFile(const std::string& path);
  bool saveToFile() const;
  bool isAutoBackupDue() const;
  bool createDueAutoBackup() const;
  bool hasAutoBackups() const;
  bool ensureAutoBackupForEnabledSetting() const;
  int clearAutoBackups() const;
  bool loadFromFile();
  void markLoadSkippedForRecovery();
  bool releaseMemoryForNetwork();
  bool reloadAfterNetwork();
};

#define READING_STATS ReadingStatsStore::getInstance()
