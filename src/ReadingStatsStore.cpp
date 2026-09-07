#include "ReadingStatsStore.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "CrossPointState.h"
#include "util/BookIdentity.h"
#include "util/CprVcodexLogs.h"
#include "util/TimeUtils.h"

namespace {
constexpr char READING_STATS_FILE_JSON[] = "/.crosspoint/reading_stats.json";
constexpr char READING_STATS_BACKUP_FILE_JSON[] = "/.crosspoint/reading_stats.json.bak";
constexpr char READING_STATS_EXPORT_DIR[] = "/exports";
constexpr char READING_STATS_BACKUP_EXPORT_PREFIX[] = "/exports/stats_backup_";
constexpr char READING_STATS_BACKUP_EXPORT_FILE_PREFIX[] = "stats_backup_";
constexpr size_t MAX_READING_STATS_AUTO_BACKUPS = 30;
constexpr unsigned long MAX_READING_GAP_MS = 30UL * 60UL * 1000UL;
constexpr unsigned long SESSION_HEARTBEAT_MS = 60UL * 1000UL;
constexpr unsigned long DEFERRED_SAVE_INTERVAL_MS = 30UL * 1000UL;
constexpr uint64_t MIN_SESSION_READING_MS = 3ULL * 60ULL * 1000ULL;

uint8_t clampPercent(const uint8_t percent) { return std::min<uint8_t>(percent, 100); }

bool countsForStreak(const ReadingDayStats& day) { return day.readingMs >= getDailyReadingGoalMs(); }

bool textWindowShowsReadingStatsData(const std::string& text) {
  static constexpr const char* DATA_ARRAY_KEYS[] = {
      "\"readingDays\":[",
      "\"legacyReadingDays\":[",
      "\"sessionLog\":[",
      "\"books\":[",
  };

  for (const char* key : DATA_ARRAY_KEYS) {
    size_t pos = 0;
    while ((pos = text.find(key, pos)) != std::string::npos) {
      size_t valuePos = pos + std::strlen(key);
      while (valuePos < text.size() &&
             (text[valuePos] == ' ' || text[valuePos] == '\n' || text[valuePos] == '\r' || text[valuePos] == '\t')) {
        ++valuePos;
      }
      if (valuePos < text.size() && text[valuePos] != ']') {
        return true;
      }
      pos = valuePos;
    }
  }
  return false;
}

bool statsFileAppearsToHaveData(const char* path) {
  if (!path || !Storage.exists(path)) {
    return false;
  }

  HalFile file;
  if (!Storage.openFileForRead("RST", path, file)) {
    return false;
  }

  char buffer[256];
  std::string window;
  window.reserve(512);
  while (true) {
    const int readBytes = file.read(buffer, sizeof(buffer));
    if (readBytes <= 0) {
      break;
    }
    window.append(buffer, static_cast<size_t>(readBytes));
    if (textWindowShowsReadingStatsData(window)) {
      file.close();
      return true;
    }
    if (window.size() > 512) {
      window.erase(0, window.size() - 256);
    }
  }

  file.close();
  return false;
}

bool copyFileViaTemp(const char* moduleName, const char* sourcePath, const char* targetPath) {
  if (!sourcePath || !targetPath || !Storage.exists(sourcePath)) {
    return false;
  }

  const std::string tempPath = std::string(targetPath) + ".tmp";
  if (Storage.exists(tempPath.c_str())) {
    Storage.remove(tempPath.c_str());
  }

  HalFile source;
  if (!Storage.openFileForRead(moduleName, sourcePath, source)) {
    return false;
  }

  HalFile target;
  if (!Storage.openFileForWrite(moduleName, tempPath.c_str(), target)) {
    source.close();
    return false;
  }

  char buffer[512];
  bool ok = true;
  while (true) {
    const int readBytes = source.read(buffer, sizeof(buffer));
    if (readBytes < 0) {
      ok = false;
      break;
    }
    if (readBytes == 0) {
      break;
    }
    const size_t written = target.write(buffer, static_cast<size_t>(readBytes));
    if (written != static_cast<size_t>(readBytes)) {
      ok = false;
      break;
    }
  }

  target.flush();
  target.close();
  source.close();

  if (!ok) {
    Storage.remove(tempPath.c_str());
    return false;
  }

  if (Storage.exists(targetPath) && !Storage.remove(targetPath)) {
    Storage.remove(tempPath.c_str());
    return false;
  }

  if (!Storage.rename(tempPath.c_str(), targetPath)) {
    Storage.remove(tempPath.c_str());
    return false;
  }

  return true;
}

std::string formatBackupDateFromDayOrdinal(const uint32_t dayOrdinal) {
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  if (!TimeUtils::getDateFromDayOrdinal(dayOrdinal, year, month, day)) {
    return "";
  }

  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02u-%02u", year, month, day);
  return std::string(buffer);
}

std::string getAutoBackupPathForDayOrdinal(const uint32_t dayOrdinal) {
  const std::string dateText = formatBackupDateFromDayOrdinal(dayOrdinal);
  return dateText.empty() ? std::string() : std::string(READING_STATS_BACKUP_EXPORT_PREFIX) + dateText;
}

bool autoBackupFileHasDataForDayOrdinal(const uint32_t dayOrdinal) {
  const std::string backupPath = getAutoBackupPathForDayOrdinal(dayOrdinal);
  return !backupPath.empty() && statsFileAppearsToHaveData(backupPath.c_str());
}

bool parseAutoBackupDayOrdinal(const char* name, uint32_t& dayOrdinal) {
  if (!name || std::strncmp(name, READING_STATS_BACKUP_EXPORT_FILE_PREFIX,
                            std::strlen(READING_STATS_BACKUP_EXPORT_FILE_PREFIX)) != 0) {
    return false;
  }

  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  int consumed = 0;
  if (std::sscanf(name, "stats_backup_%4d-%2u-%2u%n", &year, &month, &day, &consumed) != 3 || name[consumed] != '\0') {
    return false;
  }

  if (!TimeUtils::getTimestampForLocalDate(year, month, day, nullptr)) {
    return false;
  }

  dayOrdinal = TimeUtils::getDayOrdinalForDate(year, month, day);
  return dayOrdinal != 0;
}

uint32_t getLatestAutoBackupDayOrdinal() {
  auto dir = Storage.open(READING_STATS_EXPORT_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return 0;
  }

  uint32_t latestDayOrdinal = 0;
  char name[256];
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory()) {
      entry.close();
      continue;
    }

    entry.getName(name, sizeof(name));
    entry.close();

    uint32_t dayOrdinal = 0;
    if (!parseAutoBackupDayOrdinal(name, dayOrdinal)) {
      continue;
    }

    const std::string backupPath = std::string(READING_STATS_EXPORT_DIR) + "/" + name;
    if (statsFileAppearsToHaveData(backupPath.c_str())) {
      latestDayOrdinal = std::max(latestDayOrdinal, dayOrdinal);
    }
  }
  dir.close();
  return latestDayOrdinal;
}

size_t countAutoBackupFiles() {
  auto dir = Storage.open(READING_STATS_EXPORT_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return 0;
  }

  size_t count = 0;
  char name[256];
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory()) {
      entry.close();
      continue;
    }

    entry.getName(name, sizeof(name));
    entry.close();

    uint32_t dayOrdinal = 0;
    if (parseAutoBackupDayOrdinal(name, dayOrdinal)) {
      ++count;
    }
  }
  dir.close();
  return count;
}

bool findOldestAutoBackupPath(std::string& oldestPath) {
  auto dir = Storage.open(READING_STATS_EXPORT_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return false;
  }

  uint32_t oldestDayOrdinal = 0;
  char name[256];
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory()) {
      entry.close();
      continue;
    }

    entry.getName(name, sizeof(name));
    entry.close();

    uint32_t dayOrdinal = 0;
    if (!parseAutoBackupDayOrdinal(name, dayOrdinal)) {
      continue;
    }
    if (oldestDayOrdinal == 0 || dayOrdinal < oldestDayOrdinal) {
      oldestDayOrdinal = dayOrdinal;
      oldestPath = std::string(READING_STATS_EXPORT_DIR) + "/" + name;
    }
  }
  dir.close();
  return oldestDayOrdinal != 0 && !oldestPath.empty();
}

void pruneAutoBackupsToLimit(const size_t maxBackups) {
  while (countAutoBackupFiles() > maxBackups) {
    std::string oldestPath;
    if (!findOldestAutoBackupPath(oldestPath)) {
      break;
    }
    if (!Storage.remove(oldestPath.c_str())) {
      LOG_ERR("RST", "Failed to prune old reading stats backup %s", oldestPath.c_str());
      break;
    }
    LOG_DBG("RST", "Pruned old reading stats backup %s", oldestPath.c_str());
  }
}

std::string toLowerAscii(std::string value) {
  for (char& c : value) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return value;
}

bool isRootIfFoundPath(const std::string& normalizedPath) {
  if (normalizedPath.size() <= 1 || normalizedPath.front() != '/') {
    return false;
  }
  if (normalizedPath.find('/', 1) != std::string::npos) {
    return false;
  }

  const std::string lowerName = toLowerAscii(normalizedPath.substr(1));
  return lowerName == "if_found.txt" || lowerName == "if_found.txt.txt";
}

bool isIgnoredStatsPath(const std::string& path) {
  if (path.empty()) {
    return false;
  }

  std::string normalized = FsHelpers::normalisePath(path);
  if (normalized.empty()) {
    return false;
  }

  if (normalized.front() != '/') {
    normalized.insert(normalized.begin(), '/');
  }

  return normalized == "/ignore_stats" || normalized.rfind("/ignore_stats/", 0) == 0 || isRootIfFoundPath(normalized);
}

void normalizeReadingDays(std::vector<ReadingDayStats>& readingDays) {
  std::sort(readingDays.begin(), readingDays.end(), [](const ReadingDayStats& left, const ReadingDayStats& right) {
    return left.dayOrdinal < right.dayOrdinal;
  });

  std::vector<ReadingDayStats> mergedDays;
  mergedDays.reserve(readingDays.size());
  for (const auto& day : readingDays) {
    if (!mergedDays.empty() && mergedDays.back().dayOrdinal == day.dayOrdinal) {
      mergedDays.back().readingMs += day.readingMs;
    } else {
      mergedDays.push_back(day);
    }
  }

  readingDays = std::move(mergedDays);
}

void addReadingToDays(std::vector<ReadingDayStats>& days, const uint32_t dayOrdinal, const uint64_t readingMs) {
  if (dayOrdinal == 0 || readingMs == 0) {
    return;
  }

  auto it =
      std::lower_bound(days.begin(), days.end(), dayOrdinal,
                       [](const ReadingDayStats& day, const uint32_t ordinal) { return day.dayOrdinal < ordinal; });
  if (it == days.end() || it->dayOrdinal != dayOrdinal) {
    days.insert(it, ReadingDayStats{dayOrdinal, readingMs});
  } else {
    it->readingMs += readingMs;
  }
}

bool containsString(const std::vector<std::string>& values, const std::string& value) {
  return !value.empty() && std::find(values.begin(), values.end(), value) != values.end();
}

bool containsReadingDay(const std::vector<ReadingDayStats>& days, const uint32_t dayOrdinal) {
  return dayOrdinal != 0 && std::any_of(days.begin(), days.end(), [dayOrdinal](const ReadingDayStats& day) {
           return day.dayOrdinal == dayOrdinal;
         });
}

bool sessionHasBookIdentity(const ReadingSessionLogEntry& session) {
  return !session.bookId.empty() || !session.path.empty();
}

bool sessionMatchesBook(const ReadingSessionLogEntry& session, const ReadingBookStats& book) {
  if (!session.bookId.empty() && !book.bookId.empty() && session.bookId == book.bookId) {
    return true;
  }

  const std::string normalizedSessionPath = BookIdentity::normalizePath(session.path);
  return !normalizedSessionPath.empty() &&
         (normalizedSessionPath == book.path || containsString(book.knownPaths, normalizedSessionPath));
}

void dedupeStrings(std::vector<std::string>& values) {
  values.erase(std::remove_if(values.begin(), values.end(), [](const std::string& value) { return value.empty(); }),
               values.end());
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end()), values.end());
}
}  // namespace

ReadingStatsStore ReadingStatsStore::instance;

size_t ReadingStatsStore::findBookIndexByPath(const std::string& path) const {
  if (path.empty()) {
    return books.size();
  }

  const std::string normalizedPath = BookIdentity::normalizePath(path);
  for (size_t index = 0; index < books.size(); ++index) {
    const auto& book = books[index];
    if (book.path == normalizedPath || containsString(book.knownPaths, normalizedPath)) {
      return index;
    }
  }
  return books.size();
}

size_t ReadingStatsStore::findBookIndexByBookId(const std::string& bookId) const {
  if (bookId.empty()) {
    return books.size();
  }

  for (size_t index = 0; index < books.size(); ++index) {
    if (books[index].bookId == bookId) {
      return index;
    }
  }
  return books.size();
}

size_t ReadingStatsStore::findLegacyMergeCandidate(const std::string& path, const std::string& title,
                                                   const std::string& author) const {
  const std::string extension = BookIdentity::getFileExtensionLower(path);
  size_t candidateIndex = books.size();

  for (size_t index = 0; index < books.size(); ++index) {
    const auto& book = books[index];
    if (!BookIdentity::isLegacyBookId(book.bookId) && Storage.exists(book.path.c_str())) {
      continue;
    }
    if (title.empty() || book.title.empty() || book.title != title) {
      continue;
    }
    if (!author.empty() && !book.author.empty() && book.author != author) {
      continue;
    }
    if (!extension.empty() && BookIdentity::getFileExtensionLower(book.path) != extension) {
      continue;
    }

    if (candidateIndex != books.size()) {
      return books.size();
    }
    candidateIndex = index;
  }

  return candidateIndex;
}

void ReadingStatsStore::rememberBookPath(ReadingBookStats& book, const std::string& path) {
  const std::string normalizedPath = BookIdentity::normalizePath(path);
  if (normalizedPath.empty()) {
    return;
  }

  if (!containsString(book.knownPaths, normalizedPath)) {
    book.knownPaths.push_back(normalizedPath);
  }
  book.path = normalizedPath;
  dedupeStrings(book.knownPaths);
}

void ReadingStatsStore::rememberBookIdAlias(ReadingBookStats&, const std::string&) {}

void ReadingStatsStore::mergeBookInto(ReadingBookStats& primary, const ReadingBookStats& duplicate) {
  const uint32_t primaryLastReadAtBefore = primary.lastReadAt;

  if (primary.bookId.empty() ||
      (BookIdentity::isLegacyBookId(primary.bookId) && !BookIdentity::isLegacyBookId(duplicate.bookId))) {
    primary.bookId = duplicate.bookId;
  }

  if (!duplicate.path.empty() && !containsString(primary.knownPaths, duplicate.path)) {
    primary.knownPaths.push_back(duplicate.path);
  }
  for (const auto& knownPath : duplicate.knownPaths) {
    if (!containsString(primary.knownPaths, knownPath)) {
      primary.knownPaths.push_back(knownPath);
    }
  }
  dedupeStrings(primary.knownPaths);

  if ((!primary.path.empty() && !Storage.exists(primary.path.c_str()) && !duplicate.path.empty() &&
       Storage.exists(duplicate.path.c_str())) ||
      (!duplicate.path.empty() && duplicate.lastReadAt > primaryLastReadAtBefore)) {
    primary.path = duplicate.path;
  }

  if (primary.title.empty() || (!duplicate.title.empty() && duplicate.lastReadAt >= primary.lastReadAt)) {
    primary.title = duplicate.title;
  }
  if (primary.author.empty() || (!duplicate.author.empty() && duplicate.lastReadAt >= primary.lastReadAt)) {
    primary.author = duplicate.author;
  }
  if (primary.coverBmpPath.empty() || (!duplicate.coverBmpPath.empty() && duplicate.lastReadAt >= primary.lastReadAt)) {
    primary.coverBmpPath = duplicate.coverBmpPath;
  }
  if (primary.chapterTitle.empty() || (!duplicate.chapterTitle.empty() && duplicate.lastReadAt >= primary.lastReadAt)) {
    primary.chapterTitle = duplicate.chapterTitle;
  }

  primary.totalReadingMs += duplicate.totalReadingMs;
  primary.sessions += duplicate.sessions;
  primary.lastSessionMs = std::max(primary.lastSessionMs, duplicate.lastSessionMs);
  if (primary.firstReadAt == 0 || (duplicate.firstReadAt != 0 && duplicate.firstReadAt < primary.firstReadAt)) {
    primary.firstReadAt = duplicate.firstReadAt;
  }
  primary.lastReadAt = std::max(primary.lastReadAt, duplicate.lastReadAt);
  if (primary.completedAt == 0) {
    primary.completedAt = duplicate.completedAt;
  } else if (duplicate.completedAt != 0) {
    primary.completedAt = std::min(primary.completedAt, duplicate.completedAt);
  }
  if (duplicate.lastReadAt >= primary.lastReadAt) {
    primary.lastProgressPercent = duplicate.lastProgressPercent;
    primary.chapterProgressPercent = duplicate.chapterProgressPercent;
  } else {
    primary.lastProgressPercent = std::max(primary.lastProgressPercent, duplicate.lastProgressPercent);
    primary.chapterProgressPercent = std::max(primary.chapterProgressPercent, duplicate.chapterProgressPercent);
  }
  primary.completed = primary.completed || duplicate.completed;
  primary.readingDays.insert(primary.readingDays.end(), duplicate.readingDays.begin(), duplicate.readingDays.end());
  normalizeReadingDays(primary.readingDays);
}

void ReadingStatsStore::normalizeBook(ReadingBookStats& book) {
  if (book.bookId.empty()) {
    book.bookId = BookIdentity::resolveStableBookId(book.path);
  }
  if (book.path.empty() && !book.knownPaths.empty()) {
    book.path = book.knownPaths.front();
  }
  rememberBookPath(book, book.path);
  normalizeReadingDays(book.readingDays);
  book.lastProgressPercent = clampPercent(book.lastProgressPercent);
  book.chapterProgressPercent = clampPercent(book.chapterProgressPercent);
}

void ReadingStatsStore::normalizeBooks() {
  for (auto& book : books) {
    normalizeBook(book);
  }

  for (size_t primaryIndex = 0; primaryIndex < books.size(); ++primaryIndex) {
    size_t duplicateIndex = primaryIndex + 1;
    while (duplicateIndex < books.size()) {
      if (!books[primaryIndex].bookId.empty() && books[primaryIndex].bookId == books[duplicateIndex].bookId) {
        mergeBookInto(books[primaryIndex], books[duplicateIndex]);
        books.erase(books.begin() + static_cast<std::ptrdiff_t>(duplicateIndex));
        continue;
      }
      ++duplicateIndex;
    }
  }
}

size_t ReadingStatsStore::getOrCreateBookIndex(const std::string& path, const std::string& title,
                                               const std::string& author, const std::string& coverBmpPath,
                                               const std::string& preferredBookId) {
  const std::string normalizedPath = BookIdentity::normalizePath(path);
  const std::string resolvedBookId =
      !preferredBookId.empty() ? preferredBookId : BookIdentity::resolveStableBookId(normalizedPath);

  size_t index = findBookIndexByPath(normalizedPath);
  if (index == books.size() && !resolvedBookId.empty()) {
    index = findBookIndexByBookId(resolvedBookId);
  }
  if (index == books.size()) {
    index = findLegacyMergeCandidate(normalizedPath, title, author);
  }

  if (index == books.size()) {
    ReadingBookStats book;
    book.bookId = resolvedBookId;
    book.path = normalizedPath;
    if (!normalizedPath.empty()) {
      book.knownPaths.push_back(normalizedPath);
    }
    book.title = title;
    book.author = author;
    book.coverBmpPath = coverBmpPath;
    books.insert(books.begin(), std::move(book));
    return 0;
  }

  auto& book = books[index];
  if (book.bookId.empty() ||
      (BookIdentity::isLegacyBookId(book.bookId) && !BookIdentity::isLegacyBookId(resolvedBookId))) {
    book.bookId = resolvedBookId;
  }
  rememberBookPath(book, normalizedPath);
  if (!title.empty()) {
    book.title = title;
  }
  if (!author.empty()) {
    book.author = author;
  }
  if (!coverBmpPath.empty()) {
    book.coverBmpPath = coverBmpPath;
  }
  return index;
}

const ReadingBookStats* ReadingStatsStore::findBook(const std::string& key) const {
  if (shouldIgnorePath(key)) {
    return nullptr;
  }

  const size_t pathIndex = findBookIndexByPath(key);
  if (pathIndex < books.size()) {
    return &books[pathIndex];
  }

  const size_t bookIdIndex = findBookIndexByBookId(key);
  return bookIdIndex < books.size() ? &books[bookIdIndex] : nullptr;
}

const ReadingBookStats* ReadingStatsStore::findMatchingBookForPath(const std::string& path, const std::string& title,
                                                                   const std::string& author) const {
  if (shouldIgnorePath(path)) {
    return nullptr;
  }

  if (const auto* exactBook = findBook(path)) {
    return exactBook;
  }

  const std::string resolvedBookId = BookIdentity::calculateContentBookId(path);
  if (!resolvedBookId.empty()) {
    const size_t bookIdIndex = findBookIndexByBookId(resolvedBookId);
    if (bookIdIndex < books.size()) {
      return &books[bookIdIndex];
    }
  }

  const size_t legacyIndex = findLegacyMergeCandidate(path, title, author);
  return legacyIndex < books.size() ? &books[legacyIndex] : nullptr;
}

ReadingDayStats& ReadingStatsStore::getOrCreateReadingDay(const uint32_t epochSeconds) {
  const uint32_t dayOrdinal = TimeUtils::getLocalDayOrdinal(epochSeconds);
  auto it =
      std::lower_bound(readingDays.begin(), readingDays.end(), dayOrdinal,
                       [](const ReadingDayStats& day, const uint32_t ordinal) { return day.dayOrdinal < ordinal; });
  if (it == readingDays.end() || it->dayOrdinal != dayOrdinal) {
    it = readingDays.insert(it, ReadingDayStats{dayOrdinal, 0});
  }
  return *it;
}

ReadingDayStats& ReadingStatsStore::getOrCreateBookReadingDay(ReadingBookStats& book, const uint32_t epochSeconds) {
  const uint32_t dayOrdinal = TimeUtils::getLocalDayOrdinal(epochSeconds);
  auto it =
      std::lower_bound(book.readingDays.begin(), book.readingDays.end(), dayOrdinal,
                       [](const ReadingDayStats& day, const uint32_t ordinal) { return day.dayOrdinal < ordinal; });
  if (it == book.readingDays.end() || it->dayOrdinal != dayOrdinal) {
    it = book.readingDays.insert(it, ReadingDayStats{dayOrdinal, 0});
  }
  return *it;
}

uint32_t ReadingStatsStore::getLatestKnownTimestamp() const {
  uint32_t latestTimestamp = APP_STATE.lastKnownValidTimestamp;
  for (const auto& book : books) {
    if (isClockValid(book.lastReadAt)) {
      latestTimestamp = std::max(latestTimestamp, book.lastReadAt);
    }
    if (isClockValid(book.completedAt)) {
      latestTimestamp = std::max(latestTimestamp, book.completedAt);
    }
    if (isClockValid(book.firstReadAt)) {
      latestTimestamp = std::max(latestTimestamp, book.firstReadAt);
    }
  }
  return latestTimestamp;
}

uint32_t ReadingStatsStore::getReferenceTimestamp(const uint32_t preferredTimestamp,
                                                  const uint32_t bookTimestamp) const {
  if (isClockValid(preferredTimestamp)) {
    return preferredTimestamp;
  }

  if (isClockValid(APP_STATE.lastKnownValidTimestamp)) {
    return APP_STATE.lastKnownValidTimestamp;
  }

  const uint32_t latestKnownTimestamp = getLatestKnownTimestamp();
  if (isClockValid(latestKnownTimestamp)) {
    return latestKnownTimestamp;
  }

  return isClockValid(bookTimestamp) ? bookTimestamp : 0;
}

uint32_t ReadingStatsStore::getReferenceDayOrdinal() const {
  const uint32_t referenceTimestamp = getReferenceTimestamp(TimeUtils::getAuthoritativeTimestamp());
  if (isClockValid(referenceTimestamp)) {
    return TimeUtils::getLocalDayOrdinal(referenceTimestamp);
  }
  if (!readingDays.empty()) {
    return readingDays.back().dayOrdinal;
  }
  return 0;
}

void ReadingStatsStore::updateBookReadTimestamp(ReadingBookStats& book, const uint32_t preferredTimestamp) {
  const uint32_t referenceTimestamp = getReferenceTimestamp(preferredTimestamp, book.lastReadAt);
  if (!isClockValid(referenceTimestamp)) {
    return;
  }

  if (book.firstReadAt == 0) {
    book.firstReadAt = referenceTimestamp;
  }
  book.lastReadAt = referenceTimestamp;
}

void ReadingStatsStore::touchBook(const size_t index) {
  if (index == 0 || index >= books.size()) {
    return;
  }

  ReadingBookStats book = books[index];
  books.erase(books.begin() + static_cast<std::ptrdiff_t>(index));
  books.insert(books.begin(), std::move(book));

  if (activeSession.active) {
    if (activeSession.bookIndex == index) {
      activeSession.bookIndex = 0;
    } else if (activeSession.bookIndex < index) {
      activeSession.bookIndex++;
    }
  }
}

bool ReadingStatsStore::isClockValid(const uint32_t epochSeconds) { return TimeUtils::isClockValid(epochSeconds); }

bool ReadingStatsStore::shouldIgnorePath(const std::string& path) { return isIgnoredStatsPath(path); }

void ReadingStatsStore::recordReadingTime(ReadingBookStats& book, const uint32_t epochSeconds,
                                          const uint64_t readingMs) {
  if (!isClockValid(epochSeconds) || readingMs == 0) {
    return;
  }

  getOrCreateBookReadingDay(book, epochSeconds).readingMs += readingMs;
  getOrCreateReadingDay(epochSeconds).readingMs += readingMs;
}

void ReadingStatsStore::appendSessionLogEntry(const uint32_t dayOrdinal, const uint32_t sessionMs,
                                              const ReadingBookStats& book) {
  if (dayOrdinal == 0 || sessionMs == 0) {
    return;
  }

  ReadingSessionLog::makeRoomForAppend(sessionLog);

  ReadingSessionLogEntry entry;
  entry.dayOrdinal = dayOrdinal;
  entry.sessionMs = sessionMs;
  entry.bookId = book.bookId;
  if (entry.bookId.empty()) {
    entry.path = book.path;
  }
  sessionLog.push_back(std::move(entry));
}

bool ReadingStatsStore::convertLegacyReadingDaysToUnassigned() {
  std::vector<ReadingDayStats> bookTotals;
  for (const auto& book : books) {
    for (const auto& day : book.readingDays) {
      addReadingToDays(bookTotals, day.dayOrdinal, day.readingMs);
    }
  }

  std::vector<ReadingDayStats> legacyTotals = legacyReadingDays;
  normalizeReadingDays(legacyTotals);

  std::vector<ReadingDayStats> unassignedDays;
  unassignedDays.reserve(legacyTotals.size());
  for (const auto& legacyDay : legacyTotals) {
    if (legacyDay.dayOrdinal == 0 || legacyDay.readingMs == 0) {
      continue;
    }

    uint64_t assignedMs = 0;
    auto it =
        std::lower_bound(bookTotals.begin(), bookTotals.end(), legacyDay.dayOrdinal,
                         [](const ReadingDayStats& day, const uint32_t ordinal) { return day.dayOrdinal < ordinal; });
    if (it != bookTotals.end() && it->dayOrdinal == legacyDay.dayOrdinal) {
      assignedMs = it->readingMs;
    }

    if (legacyDay.readingMs > assignedMs) {
      unassignedDays.push_back(ReadingDayStats{legacyDay.dayOrdinal, legacyDay.readingMs - assignedMs});
    }
  }

  const auto sameDays = [](const std::vector<ReadingDayStats>& left, const std::vector<ReadingDayStats>& right) {
    if (left.size() != right.size()) {
      return false;
    }
    for (size_t index = 0; index < left.size(); ++index) {
      if (left[index].dayOrdinal != right[index].dayOrdinal || left[index].readingMs != right[index].readingMs) {
        return false;
      }
    }
    return true;
  };

  const bool changed = !sameDays(legacyReadingDays, unassignedDays);
  legacyReadingDays = std::move(unassignedDays);
  return changed;
}

void ReadingStatsStore::rebuildAggregatedReadingDays() {
  readingDays = legacyReadingDays;
  normalizeReadingDays(readingDays);

  for (const auto& book : books) {
    for (const auto& day : book.readingDays) {
      addReadingToDays(readingDays, day.dayOrdinal, day.readingMs);
    }
  }
}

bool ReadingStatsStore::removeIgnoredBooks() {
  const size_t originalCount = books.size();
  books.erase(std::remove_if(books.begin(), books.end(),
                             [](const ReadingBookStats& book) { return shouldIgnorePath(book.path); }),
              books.end());
  return books.size() != originalCount;
}

bool ReadingStatsStore::hasAnyStats() const {
  return !books.empty() || !legacyReadingDays.empty() || !readingDays.empty() || !sessionLog.empty();
}

void ReadingStatsStore::invalidateSummaryCache() { summaryCache.valid = false; }

void ReadingStatsStore::markDirty() {
  dirty = true;
  invalidateSummaryCache();
}

bool ReadingStatsStore::prepareInternalBackup() const {
  if (internalBackupPrepared) {
    return true;
  }

  if (!Storage.exists(READING_STATS_FILE_JSON)) {
    internalBackupPrepared = true;
    return true;
  }

  if (!statsFileAppearsToHaveData(READING_STATS_FILE_JSON)) {
    internalBackupPrepared = true;
    return true;
  }

  Storage.mkdir("/.crosspoint");
  const bool copied = copyFileViaTemp("RST", READING_STATS_FILE_JSON, READING_STATS_BACKUP_FILE_JSON);
  if (copied) {
    LOG_DBG("RST", "Prepared reading stats backup");
    internalBackupPrepared = true;
  } else {
    LOG_ERR("RST", "Failed to prepare reading stats backup");
    CPR_VCODEX_LOG_EVENT("RST", "Failed to prepare reading stats backup");
  }
  return copied;
}

bool ReadingStatsStore::refreshInternalBackupFromMain() const {
  const std::string tempPath = std::string(READING_STATS_BACKUP_FILE_JSON) + ".tmp";
  if (Storage.exists(tempPath.c_str())) {
    Storage.remove(tempPath.c_str());
  }

  if (!Storage.exists(READING_STATS_FILE_JSON) || !statsFileAppearsToHaveData(READING_STATS_FILE_JSON)) {
    if (Storage.exists(READING_STATS_BACKUP_FILE_JSON)) {
      Storage.remove(READING_STATS_BACKUP_FILE_JSON);
    }
    internalBackupPrepared = true;
    return true;
  }

  Storage.mkdir("/.crosspoint");
  const bool copied = copyFileViaTemp("RST", READING_STATS_FILE_JSON, READING_STATS_BACKUP_FILE_JSON);
  if (copied) {
    internalBackupPrepared = true;
  }
  return copied;
}

bool ReadingStatsStore::restoreInternalBackupToMain(const char* reason) const {
  if (!statsFileAppearsToHaveData(READING_STATS_BACKUP_FILE_JSON)) {
    return false;
  }

  const bool restored = copyFileViaTemp("RST", READING_STATS_BACKUP_FILE_JSON, READING_STATS_FILE_JSON);
  if (restored) {
    internalBackupPrepared = false;
    std::string message = "Restored reading stats backup";
    if (reason && reason[0] != '\0') {
      message += " after ";
      message += reason;
    }
    LOG_DBG("RST", "%s", message.c_str());
    CPR_VCODEX_LOG_EVENT("RST", message);
  }
  return restored;
}

bool ReadingStatsStore::shouldSaveDeferred() const {
  if (!dirty) {
    return false;
  }
  if (!activeSession.active) {
    return true;
  }
  return lastSaveMs == 0 || (millis() - lastSaveMs) >= DEFERRED_SAVE_INTERVAL_MS;
}

bool ReadingStatsStore::maybeCreateAutoBackup(const bool force) const {
  const uint8_t intervalDays = SETTINGS.getReadingStatsAutoBackupIntervalDays();
  if (intervalDays == 0 || !hasAnyStats()) {
    return false;
  }

  const uint32_t referenceTimestamp = getReferenceTimestamp(TimeUtils::getAuthoritativeTimestamp());
  if (!isClockValid(referenceTimestamp)) {
    return false;
  }

  const uint32_t dayOrdinal = TimeUtils::getLocalDayOrdinal(referenceTimestamp);
  if (dayOrdinal == 0) {
    return false;
  }

  if (!force && !isAutoBackupDue()) {
    return false;
  }

  const uint32_t latestBackupDay = getLatestAutoBackupDayOrdinal();
  if (force && latestBackupDay != 0 && dayOrdinal < latestBackupDay) {
    return false;
  }

  const std::string backupPath = getAutoBackupPathForDayOrdinal(dayOrdinal);
  if (backupPath.empty()) {
    return false;
  }

  Storage.mkdir(READING_STATS_EXPORT_DIR);
  const bool saved = JsonSettingsIO::saveReadingStats(*this, backupPath.c_str());
  if (saved) {
    pruneAutoBackupsToLimit(MAX_READING_STATS_AUTO_BACKUPS);
    APP_STATE.lastReadingStatsBackupDayOrdinal = dayOrdinal;
    APP_STATE.saveToFile();
    LOG_DBG("RST", "Auto-backed up reading stats to %s", backupPath.c_str());
  } else {
    LOG_ERR("RST", "Failed to auto-back up reading stats to %s", backupPath.c_str());
  }
  return saved;
}

bool ReadingStatsStore::isAutoBackupDue() const {
  const uint8_t intervalDays = SETTINGS.getReadingStatsAutoBackupIntervalDays();
  if (intervalDays == 0 || !hasAnyStats()) {
    return false;
  }

  const uint32_t referenceTimestamp = getReferenceTimestamp(TimeUtils::getAuthoritativeTimestamp());
  if (!isClockValid(referenceTimestamp)) {
    return false;
  }

  const uint32_t dayOrdinal = TimeUtils::getLocalDayOrdinal(referenceTimestamp);
  if (dayOrdinal == 0) {
    return false;
  }

  const uint32_t latestBackupDay = getLatestAutoBackupDayOrdinal();
  if (latestBackupDay != 0) {
    if (dayOrdinal <= latestBackupDay) {
      return false;
    }
    return (dayOrdinal - latestBackupDay) >= intervalDays;
  }

  return true;
}

bool ReadingStatsStore::createDueAutoBackup() const {
  if (!isAutoBackupDue()) {
    return false;
  }
  return maybeCreateAutoBackup(true);
}

bool ReadingStatsStore::hasAutoBackups() const { return getLatestAutoBackupDayOrdinal() != 0; }

bool ReadingStatsStore::ensureAutoBackupForEnabledSetting() const {
  if (SETTINGS.getReadingStatsAutoBackupIntervalDays() == 0) {
    return false;
  }

  const uint32_t referenceTimestamp = getReferenceTimestamp(TimeUtils::getAuthoritativeTimestamp());
  uint32_t dayOrdinal = 0;
  if (isClockValid(referenceTimestamp)) {
    dayOrdinal = TimeUtils::getLocalDayOrdinal(referenceTimestamp);
  }

  if (dayOrdinal != 0 && APP_STATE.lastReadingStatsBackupDayOrdinal == dayOrdinal &&
      autoBackupFileHasDataForDayOrdinal(dayOrdinal)) {
    return true;
  }

  if (APP_STATE.lastReadingStatsBackupDayOrdinal != 0) {
    APP_STATE.lastReadingStatsBackupDayOrdinal = 0;
    APP_STATE.saveToFile();
  }

  return maybeCreateAutoBackup(true);
}

int ReadingStatsStore::clearAutoBackups() const {
  auto dir = Storage.open(READING_STATS_EXPORT_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    if (APP_STATE.lastReadingStatsBackupDayOrdinal != 0) {
      APP_STATE.lastReadingStatsBackupDayOrdinal = 0;
      APP_STATE.saveToFile();
    }
    return 0;
  }

  int removedCount = 0;
  char name[256];
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory()) {
      entry.close();
      continue;
    }

    entry.getName(name, sizeof(name));
    entry.close();
    if (std::strncmp(name, READING_STATS_BACKUP_EXPORT_FILE_PREFIX,
                     std::strlen(READING_STATS_BACKUP_EXPORT_FILE_PREFIX)) != 0) {
      continue;
    }

    const std::string backupPath = std::string(READING_STATS_EXPORT_DIR) + "/" + name;
    if (Storage.remove(backupPath.c_str())) {
      ++removedCount;
    }
  }
  dir.close();

  if (APP_STATE.lastReadingStatsBackupDayOrdinal != 0) {
    APP_STATE.lastReadingStatsBackupDayOrdinal = 0;
    APP_STATE.saveToFile();
  }

  return removedCount;
}

bool ReadingStatsStore::persistToFile(const char* path) const {
  if (persistenceSuspended && path != nullptr && std::strcmp(path, READING_STATS_FILE_JSON) == 0) {
    if (!skippedSaveLogged) {
      LOG_ERR("RST", "Skipping reading stats save because loading was skipped in recovery mode");
      CPR_VCODEX_LOG_EVENT("RST", "Skipped reading stats save after recovery-mode load skip");
      skippedSaveLogged = true;
    }
    lastSaveMs = millis();
    return true;
  }

  Storage.mkdir("/.crosspoint");
  if (path != nullptr && std::strcmp(path, READING_STATS_FILE_JSON) == 0) {
    prepareInternalBackup();
  }

  const bool saved = JsonSettingsIO::saveReadingStats(*this, path);
  if (saved) {
    dirty = false;
    lastSaveMs = millis();
    if (path != nullptr && std::strcmp(path, READING_STATS_FILE_JSON) == 0) {
      if (!Storage.exists(READING_STATS_BACKUP_FILE_JSON) && hasAnyStats()) {
        refreshInternalBackupFromMain();
      }
      maybeCreateAutoBackup(false);
    }
  }
  return saved;
}

void ReadingStatsStore::rebuildSummaryCache() const {
  SummaryCache cache;
  cache.referenceDayOrdinal = getReferenceDayOrdinal();
  cache.goalReadingMs = getDailyReadingGoalMs();

  for (const auto& book : books) {
    cache.totalReadingMs += book.totalReadingMs;
    if (book.completed) {
      cache.booksFinishedCount++;
    }
  }
  if (cache.referenceDayOrdinal != 0) {
    const uint32_t start7DayOrdinal = (cache.referenceDayOrdinal >= 6) ? (cache.referenceDayOrdinal - 6) : 0;
    const uint32_t start30DayOrdinal = (cache.referenceDayOrdinal >= 29) ? (cache.referenceDayOrdinal - 29) : 0;

    std::vector<uint32_t> eligibleDays;
    eligibleDays.reserve(readingDays.size());

    for (const auto& day : readingDays) {
      if (day.dayOrdinal == cache.referenceDayOrdinal) {
        cache.todayReadingMs = day.readingMs;
      }
      if (day.dayOrdinal >= start7DayOrdinal && day.dayOrdinal <= cache.referenceDayOrdinal) {
        cache.recent7ReadingMs += day.readingMs;
      }
      if (day.dayOrdinal >= start30DayOrdinal && day.dayOrdinal <= cache.referenceDayOrdinal) {
        cache.recent30ReadingMs += day.readingMs;
      }
      if (countsForStreak(day)) {
        eligibleDays.push_back(day.dayOrdinal);
      }
    }

    if (!eligibleDays.empty()) {
      cache.maxStreakDays = 1;
      uint32_t currentMaxStreak = 1;
      for (size_t index = 1; index < eligibleDays.size(); ++index) {
        if (eligibleDays[index] == eligibleDays[index - 1] + 1) {
          currentMaxStreak++;
        } else {
          currentMaxStreak = 1;
        }
        cache.maxStreakDays = std::max(cache.maxStreakDays, currentMaxStreak);
      }

      const uint32_t latestEligibleDay = eligibleDays.back();
      const bool streakIsStillAlive =
          latestEligibleDay == cache.referenceDayOrdinal ||
          (cache.referenceDayOrdinal > 0 && latestEligibleDay + 1 == cache.referenceDayOrdinal);
      if (streakIsStillAlive) {
        cache.currentStreakDays = 1;
        for (size_t index = eligibleDays.size() - 1; index > 0; --index) {
          if (eligibleDays[index] == eligibleDays[index - 1] + 1) {
            cache.currentStreakDays++;
            continue;
          }
          break;
        }
      }
    }
  }

  cache.valid = true;
  summaryCache = cache;
}

void ReadingStatsStore::beginSession(const std::string& path, const std::string& title, const std::string& author,
                                     const std::string& coverBmpPath, const uint8_t progressPercent,
                                     const std::string& chapterTitle, const uint8_t chapterProgressPercent) {
  if (path.empty()) {
    return;
  }

  if (activeSession.active) {
    endSession();
  }

  if (shouldIgnorePath(path)) {
    activeSession = {};
    lastSessionSnapshot = {};
    return;
  }

  const std::string resolvedBookId = BookIdentity::resolveStableBookId(path);
  size_t index = getOrCreateBookIndex(path, title, author, coverBmpPath, resolvedBookId);
  touchBook(index);

  auto& book = books[0];
  activeSession.startProgressPercent = book.lastProgressPercent;
  activeSession.startCompleted = book.completed;
  book.lastProgressPercent = clampPercent(progressPercent);
  book.chapterTitle = chapterTitle;
  book.chapterProgressPercent = clampPercent(chapterProgressPercent);
  if (book.lastProgressPercent >= 100) {
    book.completed = true;
  }

  updateBookReadTimestamp(book, TimeUtils::getAuthoritativeTimestamp());

  activeSession.active = true;
  activeSession.bookIndex = 0;
  activeSession.lastInteractionMs = millis();
  activeSession.accumulatedMs = 0;

  markDirty();
}

void ReadingStatsStore::noteActivity() {
  if (!activeSession.active || activeSession.bookIndex >= books.size()) {
    return;
  }

  const unsigned long nowMs = millis();
  const unsigned long elapsedMs = nowMs - activeSession.lastInteractionMs;
  const unsigned long creditedMs = std::min(elapsedMs, MAX_READING_GAP_MS);

  if (creditedMs > 0) {
    auto& book = books[activeSession.bookIndex];
    book.totalReadingMs += creditedMs;
    activeSession.accumulatedMs += creditedMs;
    const uint32_t referenceTimestamp = getReferenceTimestamp(TimeUtils::getAuthoritativeTimestamp(), book.lastReadAt);
    recordReadingTime(book, referenceTimestamp, creditedMs);
    updateBookReadTimestamp(book, referenceTimestamp);
    markDirty();
  }

  activeSession.lastInteractionMs = nowMs;
  if (shouldSaveDeferred()) {
    saveToFile();
  }
}

void ReadingStatsStore::tickActiveSession() {
  if (!activeSession.active || activeSession.bookIndex >= books.size()) {
    return;
  }

  const unsigned long nowMs = millis();
  if ((nowMs - activeSession.lastInteractionMs) < SESSION_HEARTBEAT_MS) {
    return;
  }

  noteActivity();
}

void ReadingStatsStore::resumeSession() {
  if (!activeSession.active) {
    return;
  }
  activeSession.lastInteractionMs = millis();
}

void ReadingStatsStore::updateProgress(const uint8_t progressPercent, const bool completed,
                                       const std::string& chapterTitle, const uint8_t chapterProgressPercent) {
  if (!activeSession.active || activeSession.bookIndex >= books.size()) {
    return;
  }

  auto& book = books[activeSession.bookIndex];
  const uint8_t clampedBookProgress = clampPercent(progressPercent);
  const uint8_t clampedChapterProgress = clampPercent(chapterProgressPercent);
  const bool progressChanged = book.lastProgressPercent != clampedBookProgress;
  const bool chapterTitleChanged = book.chapterTitle != chapterTitle;
  const bool chapterProgressChanged = book.chapterProgressPercent != clampedChapterProgress;
  const bool completionChanged = !book.completed && (completed || clampedBookProgress >= 100);

  if (!progressChanged && !chapterTitleChanged && !chapterProgressChanged && !completionChanged) {
    return;
  }

  book.lastProgressPercent = clampedBookProgress;
  book.chapterTitle = chapterTitle;
  book.chapterProgressPercent = clampedChapterProgress;
  if (completed || clampedBookProgress >= 100) {
    book.completed = true;
  }

  updateBookReadTimestamp(book, TimeUtils::getAuthoritativeTimestamp());
  if (completionChanged && book.completedAt == 0) {
    book.completedAt = book.lastReadAt;
  }

  markDirty();
  if (shouldSaveDeferred()) {
    saveToFile();
  }
}

bool ReadingStatsStore::updateBookMetadata(const std::string& path, const std::string& title, const std::string& author,
                                           const std::string& coverBmpPath) {
  if (shouldIgnorePath(path)) {
    return false;
  }

  const size_t index = findBookIndexByPath(path);
  if (index >= books.size()) {
    return false;
  }
  auto it = books.begin() + static_cast<std::ptrdiff_t>(index);

  bool changed = false;
  if (!title.empty() && it->title != title) {
    it->title = title;
    changed = true;
  }
  if (!author.empty() && it->author != author) {
    it->author = author;
    changed = true;
  }
  if (!coverBmpPath.empty() && it->coverBmpPath != coverBmpPath) {
    it->coverBmpPath = coverBmpPath;
    changed = true;
  }

  if (changed) {
    markDirty();
    if (shouldSaveDeferred()) {
      saveToFile();
    }
  }
  return changed;
}

bool ReadingStatsStore::updateBookPath(const std::string& oldKey, const std::string& newPath, const std::string& title,
                                       const std::string& author, const std::string& coverBmpPath,
                                       const std::string& bookId) {
  const std::string normalizedNewPath = BookIdentity::normalizePath(newPath);
  if (normalizedNewPath.empty() || shouldIgnorePath(normalizedNewPath)) {
    return false;
  }

  size_t index = findBookIndexByPath(oldKey);
  if (index >= books.size() && !bookId.empty()) {
    index = findBookIndexByBookId(bookId);
  }
  if (index >= books.size()) {
    return false;
  }

  auto& book = books[index];
  if (!bookId.empty() &&
      (book.bookId.empty() || (BookIdentity::isLegacyBookId(book.bookId) && !BookIdentity::isLegacyBookId(bookId)))) {
    book.bookId = bookId;
  }

  rememberBookPath(book, oldKey);
  rememberBookPath(book, normalizedNewPath);
  if (!title.empty()) {
    book.title = title;
  }
  if (!author.empty()) {
    book.author = author;
  }
  if (!coverBmpPath.empty()) {
    book.coverBmpPath = coverBmpPath;
  }

  markDirty();
  saveToFile();
  return true;
}

bool ReadingStatsStore::removeBook(const std::string& path) {
  const size_t index = findBookIndexByPath(path);
  if (index >= books.size()) {
    return false;
  }

  auto it = books.begin() + static_cast<std::ptrdiff_t>(index);
  const ReadingBookStats removedBook = *it;
  const std::vector<ReadingDayStats> removedReadingDays = removedBook.readingDays;
  const bool hadBookReadingDays = !removedReadingDays.empty();
  const size_t removedIndex = index;
  books.erase(it);

  if (activeSession.active) {
    if (activeSession.bookIndex == removedIndex) {
      activeSession = {};
    } else if (activeSession.bookIndex > removedIndex) {
      activeSession.bookIndex--;
    }
  }

  if (hadBookReadingDays) {
    rebuildAggregatedReadingDays();
  }

  const auto oldSessionLogSize = sessionLog.size();
  sessionLog.erase(std::remove_if(sessionLog.begin(), sessionLog.end(),
                                  [&](const ReadingSessionLogEntry& session) {
                                    if (sessionMatchesBook(session, removedBook)) {
                                      return true;
                                    }
                                    return !sessionHasBookIdentity(session) &&
                                           containsReadingDay(removedReadingDays, session.dayOrdinal) &&
                                           !containsReadingDay(readingDays, session.dayOrdinal);
                                  }),
                   sessionLog.end());

  if ((!removedBook.bookId.empty() && lastSessionSnapshot.bookId == removedBook.bookId) ||
      lastSessionSnapshot.path == removedBook.path ||
      containsString(removedBook.knownPaths, lastSessionSnapshot.path)) {
    lastSessionSnapshot = {};
  }

  if (oldSessionLogSize != sessionLog.size()) {
    invalidateSummaryCache();
  }
  markDirty();
  saveToFile();
  return true;
}

void ReadingStatsStore::endSession() {
  if (!activeSession.active || activeSession.bookIndex >= books.size()) {
    lastSessionSnapshot = {};
    activeSession = {};
    return;
  }

  noteActivity();

  auto& book = books[activeSession.bookIndex];
  const bool countedSession = activeSession.accumulatedMs >= MIN_SESSION_READING_MS;
  const uint32_t sessionMs = (activeSession.accumulatedMs > static_cast<uint64_t>(UINT32_MAX))
                                 ? UINT32_MAX
                                 : static_cast<uint32_t>(activeSession.accumulatedMs);
  if (countedSession) {
    book.sessions++;
    book.lastSessionMs = sessionMs;
    const uint32_t sessionTimestamp = getReferenceTimestamp(TimeUtils::getAuthoritativeTimestamp(), book.lastReadAt);
    if (isClockValid(sessionTimestamp)) {
      appendSessionLogEntry(TimeUtils::getLocalDayOrdinal(sessionTimestamp), sessionMs, book);
    }
    markDirty();
  }

  lastSessionSnapshot.valid = true;
  lastSessionSnapshot.serial = ++sessionSerialCounter;
  lastSessionSnapshot.bookId = book.bookId;
  lastSessionSnapshot.path = book.path;
  lastSessionSnapshot.sessionMs = sessionMs;
  lastSessionSnapshot.counted = countedSession;
  lastSessionSnapshot.completedThisSession = !activeSession.startCompleted && book.completed;
  lastSessionSnapshot.startProgressPercent = activeSession.startProgressPercent;
  lastSessionSnapshot.endProgressPercent = book.lastProgressPercent;

  activeSession = {};
  saveToFile();
}

bool ReadingStatsStore::adjustBookReadingTime(const std::string& path, const uint32_t dayOrdinal,
                                              const int32_t deltaMs) {
  if (dayOrdinal == 0 || deltaMs == 0) {
    return false;
  }

  const size_t index = findBookIndexByPath(path);
  if (index >= books.size()) {
    return false;
  }

  auto& book = books[index];
  if (deltaMs > 0) {
    const uint64_t addedMs = static_cast<uint64_t>(deltaMs);
    addReadingToDays(book.readingDays, dayOrdinal, addedMs);
    book.totalReadingMs += addedMs;
  } else {
    const uint64_t requestedRemoveMs = static_cast<uint64_t>(-deltaMs);
    auto it =
        std::lower_bound(book.readingDays.begin(), book.readingDays.end(), dayOrdinal,
                         [](const ReadingDayStats& day, const uint32_t ordinal) { return day.dayOrdinal < ordinal; });
    if (it == book.readingDays.end() || it->dayOrdinal != dayOrdinal || it->readingMs < requestedRemoveMs ||
        book.totalReadingMs < requestedRemoveMs) {
      return false;
    }
    it->readingMs -= requestedRemoveMs;
    if (it->readingMs == 0) {
      book.readingDays.erase(it);
    }
    book.totalReadingMs -= requestedRemoveMs;
  }

  rebuildAggregatedReadingDays();
  markDirty();
  return saveToFile();
}

bool ReadingStatsStore::setBookFirstReadDate(const std::string& path, const uint32_t dayOrdinal) {
  if (dayOrdinal == 0) {
    return false;
  }

  const size_t index = findBookIndexByPath(path);
  if (index >= books.size()) {
    return false;
  }

  auto& book = books[index];
  const uint32_t referenceDayOrdinal = getReferenceDayOrdinal();
  if (referenceDayOrdinal != 0 && dayOrdinal > referenceDayOrdinal) {
    return false;
  }
  if (!book.readingDays.empty() && dayOrdinal > book.readingDays.front().dayOrdinal) {
    return false;
  }
  if (isClockValid(book.lastReadAt) && dayOrdinal > TimeUtils::getLocalDayOrdinal(book.lastReadAt)) {
    return false;
  }
  if (isClockValid(book.completedAt) && dayOrdinal > TimeUtils::getLocalDayOrdinal(book.completedAt)) {
    return false;
  }

  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  if (!TimeUtils::getDateFromDayOrdinal(dayOrdinal, year, month, day)) {
    return false;
  }

  uint32_t timestamp = 0;
  if (!TimeUtils::getTimestampForLocalDate(year, month, day, &timestamp)) {
    return false;
  }

  if (book.firstReadAt == timestamp) {
    return true;
  }

  book.firstReadAt = timestamp;
  markDirty();
  return saveToFile();
}

uint32_t ReadingStatsStore::getBooksFinishedCount() const {
  if (!summaryCache.valid || summaryCache.goalReadingMs != getDailyReadingGoalMs()) {
    rebuildSummaryCache();
  }
  return summaryCache.booksFinishedCount;
}

uint64_t ReadingStatsStore::getTotalReadingMs() const {
  if (!summaryCache.valid || summaryCache.goalReadingMs != getDailyReadingGoalMs()) {
    rebuildSummaryCache();
  }
  return summaryCache.totalReadingMs;
}

uint64_t ReadingStatsStore::getTodayReadingMs() const {
  if (!summaryCache.valid || summaryCache.referenceDayOrdinal != getReferenceDayOrdinal() ||
      summaryCache.goalReadingMs != getDailyReadingGoalMs()) {
    rebuildSummaryCache();
  }
  return summaryCache.todayReadingMs;
}

uint64_t ReadingStatsStore::getRecentReadingMs(const uint32_t days) const {
  if (days == 0) {
    return 0;
  }
  if (!summaryCache.valid || summaryCache.referenceDayOrdinal != getReferenceDayOrdinal() ||
      summaryCache.goalReadingMs != getDailyReadingGoalMs()) {
    rebuildSummaryCache();
  }
  if (days <= 7) {
    return summaryCache.recent7ReadingMs;
  }
  if (days <= 30) {
    return summaryCache.recent30ReadingMs;
  }

  if (readingDays.empty() || summaryCache.referenceDayOrdinal == 0) {
    return 0;
  }

  const uint32_t startDayOrdinal =
      (summaryCache.referenceDayOrdinal >= days - 1) ? (summaryCache.referenceDayOrdinal - (days - 1)) : 0;
  uint64_t totalMs = 0;
  for (const auto& day : readingDays) {
    if (day.dayOrdinal >= startDayOrdinal && day.dayOrdinal <= summaryCache.referenceDayOrdinal) {
      totalMs += day.readingMs;
    }
  }
  return totalMs;
}

uint32_t ReadingStatsStore::getCurrentStreakDays() const {
  if (!summaryCache.valid || summaryCache.referenceDayOrdinal != getReferenceDayOrdinal() ||
      summaryCache.goalReadingMs != getDailyReadingGoalMs()) {
    rebuildSummaryCache();
  }
  return summaryCache.currentStreakDays;
}

uint32_t ReadingStatsStore::getMaxStreakDays() const {
  if (!summaryCache.valid || summaryCache.goalReadingMs != getDailyReadingGoalMs()) {
    rebuildSummaryCache();
  }
  return summaryCache.maxStreakDays;
}

uint32_t ReadingStatsStore::getDisplayTimestamp(bool* usedFallback) const {
  const uint32_t authoritativeTimestamp = TimeUtils::getAuthoritativeTimestamp();
  if (isClockValid(authoritativeTimestamp)) {
    if (usedFallback) {
      *usedFallback = false;
    }
    return authoritativeTimestamp;
  }

  const uint32_t latestKnownTimestamp = getLatestKnownTimestamp();
  if (usedFallback) {
    *usedFallback = isClockValid(latestKnownTimestamp);
  }
  return latestKnownTimestamp;
}

void ReadingStatsStore::reset() {
  persistenceSuspended = false;
  skippedSaveLogged = false;
  internalBackupPrepared = true;
  const std::string backupTempPath = std::string(READING_STATS_BACKUP_FILE_JSON) + ".tmp";
  if (Storage.exists(READING_STATS_BACKUP_FILE_JSON)) {
    Storage.remove(READING_STATS_BACKUP_FILE_JSON);
  }
  if (Storage.exists(backupTempPath.c_str())) {
    Storage.remove(backupTempPath.c_str());
  }
  books.clear();
  legacyReadingDays.clear();
  readingDays.clear();
  sessionLog.clear();
  activeSession = {};
  lastSessionSnapshot = {};
  markDirty();
  saveToFile();
}

bool ReadingStatsStore::exportToFile(const std::string& path) const {
  if (path.empty()) {
    return false;
  }
  return JsonSettingsIO::saveReadingStats(*this, path.c_str());
}

bool ReadingStatsStore::importFromFile(const std::string& path) {
  if (path.empty()) {
    CPR_VCODEX_LOG_EVENT("RST", "Reading stats import rejected an empty path");
    return false;
  }
  if (!Storage.exists(path.c_str())) {
    CPR_VCODEX_LOG_EVENT("RST", std::string("Reading stats import file was not found: ") + path);
    return false;
  }

  if (!refreshInternalBackupFromMain()) {
    LOG_ERR("RST", "Could not prepare rollback backup before stats import");
    CPR_VCODEX_LOG_EVENT("RST", "Reading stats import aborted because rollback backup failed");
    return false;
  }

  auto reloadOriginalStats = [this]() {
    books.clear();
    books.shrink_to_fit();
    legacyReadingDays.clear();
    legacyReadingDays.shrink_to_fit();
    readingDays.clear();
    readingDays.shrink_to_fit();
    sessionLog.clear();
    sessionLog.shrink_to_fit();
    activeSession = {};
    lastSessionSnapshot = {};
    sessionSerialCounter = 0;
    dirty = false;
    invalidateSummaryCache();
    return loadFromFile();
  };

  LOG_DBG("RST", "Before stats import: free=%u largest=%u", ESP.getFreeHeap(),
          heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_DEFAULT));
  const bool loaded = JsonSettingsIO::loadReadingStatsFromFile(*this, path.c_str());
  if (!loaded) {
    CPR_VCODEX_LOG_EVENT("RST", std::string("Reading stats import source was rejected: ") + path);
    return false;
  }

  if (!hasAnyStats()) {
    CPR_VCODEX_LOG_EVENT("RST", "Rejected empty reading stats import");
    reloadOriginalStats();
    return false;
  }

  persistenceSuspended = false;
  skippedSaveLogged = false;
  internalBackupPrepared = true;
  normalizeReadingDays(readingDays);
  normalizeBooks();
  activeSession = {};
  lastSessionSnapshot = {};
  sessionSerialCounter = 0;
  if (sessionLog.size() > ReadingSessionLog::MAX_ENTRIES) {
    sessionLog.erase(sessionLog.begin(), sessionLog.begin() + static_cast<std::ptrdiff_t>(
                                                                  sessionLog.size() - ReadingSessionLog::MAX_ENTRIES));
  }
  removeIgnoredBooks();
  rebuildAggregatedReadingDays();
  const uint32_t latestKnownTimestamp = getLatestKnownTimestamp();
  if (!isClockValid(APP_STATE.lastKnownValidTimestamp) && isClockValid(latestKnownTimestamp)) {
    APP_STATE.lastKnownValidTimestamp = latestKnownTimestamp;
    APP_STATE.saveToFile();
  }
  markDirty();
  const bool saved = saveToFile();
  if (!saved) {
    LOG_ERR("RST", "Reading stats import save failed; restoring previous stats");
    CPR_VCODEX_LOG_EVENT("RST", "Reading stats import rolled back after save failure");
    reloadOriginalStats();
    return false;
  }

  LOG_DBG("RST", "After stats import: free=%u largest=%u", ESP.getFreeHeap(),
          heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_DEFAULT));
  return true;
}

bool ReadingStatsStore::saveToFile() const {
  if (!dirty && Storage.exists(READING_STATS_FILE_JSON)) {
    return true;
  }
  if (activeSession.active && !shouldSaveDeferred()) {
    return true;
  }
  return persistToFile(READING_STATS_FILE_JSON);
}

bool ReadingStatsStore::loadFromFile() {
  const std::string tempPath = std::string(READING_STATS_FILE_JSON) + ".tmp";
  if (!Storage.exists(READING_STATS_FILE_JSON) && Storage.exists(tempPath.c_str())) {
    if (Storage.rename(tempPath.c_str(), READING_STATS_FILE_JSON)) {
      LOG_DBG("RST", "Recovered reading_stats.json from interrupted temp file");
    }
  }

  if (!Storage.exists(READING_STATS_FILE_JSON)) {
    restoreInternalBackupToMain("missing main file");
  }

  if (!Storage.exists(READING_STATS_FILE_JSON)) {
    return false;
  }

  auto loadMainFile = [this]() -> bool {
    const bool loaded = JsonSettingsIO::loadReadingStatsFromFile(*this, READING_STATS_FILE_JSON);
    if (!loaded) {
      return false;
    }

    const bool needsSave = dirty;
    normalizeReadingDays(readingDays);
    normalizeBooks();
    removeIgnoredBooks();
    rebuildAggregatedReadingDays();
    const uint32_t latestKnownTimestamp = getLatestKnownTimestamp();
    if (!isClockValid(APP_STATE.lastKnownValidTimestamp) && isClockValid(latestKnownTimestamp)) {
      APP_STATE.lastKnownValidTimestamp = latestKnownTimestamp;
    }
    activeSession = {};
    lastSessionSnapshot = {};
    sessionSerialCounter = 0;
    if (sessionLog.size() > ReadingSessionLog::MAX_ENTRIES) {
      sessionLog.erase(
          sessionLog.begin(),
          sessionLog.begin() + static_cast<std::ptrdiff_t>(sessionLog.size() - ReadingSessionLog::MAX_ENTRIES));
    }
    invalidateSummaryCache();
    if (needsSave) {
      markDirty();
      saveToFile();
    } else {
      dirty = false;
      lastSaveMs = millis();
    }
    persistenceSuspended = false;
    skippedSaveLogged = false;
    prepareInternalBackup();
    return true;
  };

  bool loaded = loadMainFile();
  if (!loaded && restoreInternalBackupToMain("main load failure")) {
    loaded = loadMainFile();
  } else if (loaded && !hasAnyStats() && statsFileAppearsToHaveData(READING_STATS_BACKUP_FILE_JSON) &&
             restoreInternalBackupToMain("empty main file")) {
    loaded = loadMainFile();
  }
  if (!loaded) {
    markLoadSkippedForRecovery();
    CPR_VCODEX_LOG_EVENT("RST", "Reading stats persistence suspended after load failure");
  }
  return loaded;
}

void ReadingStatsStore::markLoadSkippedForRecovery() {
  persistenceSuspended = true;
  skippedSaveLogged = false;
  internalBackupPrepared = false;
  activeSession = {};
  lastSessionSnapshot = {};
  dirty = false;
  invalidateSummaryCache();
}

bool ReadingStatsStore::releaseMemoryForNetwork() {
  LOG_DBG("RST", "Before network release: free=%u largest=%u", ESP.getFreeHeap(),
          heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_DEFAULT));

  if (activeSession.active) {
    endSession();
  }
  const ReadingSessionSnapshot preservedLastSessionSnapshot = lastSessionSnapshot;

  if (dirty && !saveToFile()) {
    LOG_ERR("RST", "Failed to save reading stats before network memory release");
    return false;
  }

  books.clear();
  books.shrink_to_fit();
  legacyReadingDays.clear();
  legacyReadingDays.shrink_to_fit();
  readingDays.clear();
  readingDays.shrink_to_fit();
  sessionLog.clear();
  sessionLog.shrink_to_fit();

  activeSession = {};
  lastSessionSnapshot = preservedLastSessionSnapshot;
  sessionSerialCounter = 0;
  invalidateSummaryCache();
  dirty = false;
  lastSaveMs = millis();

  LOG_DBG("RST", "After network release: free=%u largest=%u", ESP.getFreeHeap(),
          heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_DEFAULT));
  return true;
}

bool ReadingStatsStore::reloadAfterNetwork() {
  if (!Storage.exists(READING_STATS_FILE_JSON)) {
    return true;
  }

  const ReadingSessionSnapshot preservedLastSessionSnapshot = lastSessionSnapshot;
  const bool loaded = loadFromFile();
  if (!loaded) {
    LOG_ERR("RST", "Failed to reload reading stats after network operation");
  } else if (preservedLastSessionSnapshot.valid) {
    lastSessionSnapshot = preservedLastSessionSnapshot;
  }
  return loaded;
}
