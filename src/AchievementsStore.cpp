#include "AchievementsStore.h"

#include <HalStorage.h>
#include <I18n.h>
#include <JsonSettingsIO.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "activities/reader/BookmarkStore.h"
#include "util/TimeUtils.h"

namespace {
constexpr char ACHIEVEMENTS_FILE_JSON[] = "/.crosspoint/achievements.json";

uint32_t countGoalDaysFromStats() {
  uint32_t count = 0;
  for (const auto& day : READING_STATS.getReadingDays()) {
    if (day.readingMs >= getDailyReadingGoalMs()) {
      ++count;
    }
  }
  return count;
}

uint32_t countSessionsFromStats() {
  uint32_t count = 0;
  for (const auto& book : READING_STATS.getBooks()) {
    count += book.sessions;
  }
  return count;
}

uint32_t countCurrentBookmarksFromStats() {
  uint32_t count = 0;
  for (const auto& book : READING_STATS.getBooks()) {
    if (book.bookId.empty()) {
      continue;
    }

    BookmarkStore store;
    store.load("", book.bookId);
    count += static_cast<uint32_t>(store.getAll().size());
  }
  return count;
}

uint32_t findLongestSessionFromStats() {
  uint32_t maxSessionMs = 0;
  for (const auto& session : READING_STATS.getSessionLog()) {
    maxSessionMs = std::max(maxSessionMs, session.sessionMs);
  }
  for (const auto& book : READING_STATS.getBooks()) {
    maxSessionMs = std::max(maxSessionMs, book.lastSessionMs);
  }
  return maxSessionMs;
}

std::string formatDurationCompact(const uint64_t totalMs) {
  const uint64_t totalMinutes = totalMs / 60000ULL;
  const uint64_t hours = totalMinutes / 60ULL;
  const uint64_t minutes = totalMinutes % 60ULL;
  if (hours == 0) {
    return std::to_string(minutes) + "m";
  }
  return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
}

std::string formatUInt(const char* format, const uint64_t value) {
  char buffer[96];
  snprintf(buffer, sizeof(buffer), format, static_cast<unsigned int>(value));
  return buffer;
}

std::string formatText(const char* format, const std::string& value) {
  char buffer[128];
  snprintf(buffer, sizeof(buffer), format, value.c_str());
  return buffer;
}
}  // namespace

AchievementsStore AchievementsStore::instance;

const std::array<AchievementDefinition, static_cast<size_t>(AchievementId::_COUNT)>& AchievementsStore::definitions() {
  static const std::array<AchievementDefinition, static_cast<size_t>(AchievementId::_COUNT)> items = {
      AchievementDefinition{AchievementId::FirstBookStarted, AchievementMetric::BooksStarted, 1,
                            StrId::STR_ACH_TITLE_FIRST_BOOK_STARTED},
      AchievementDefinition{AchievementId::FiveBooksStarted, AchievementMetric::BooksStarted, 5,
                            StrId::STR_ACH_TITLE_FIVE_BOOKS_STARTED},
      AchievementDefinition{AchievementId::TenBooksStarted, AchievementMetric::BooksStarted, 10,
                            StrId::STR_ACH_TITLE_TEN_BOOKS_STARTED},
      AchievementDefinition{AchievementId::TwentyFiveBooksStarted, AchievementMetric::BooksStarted, 25,
                            StrId::STR_ACH_TITLE_TWENTY_FIVE_BOOKS_STARTED},
      AchievementDefinition{AchievementId::FiftyBooksStarted, AchievementMetric::BooksStarted, 50,
                            StrId::STR_ACH_TITLE_FIFTY_BOOKS_STARTED},
      AchievementDefinition{AchievementId::FirstSession, AchievementMetric::Sessions, 1,
                            StrId::STR_ACH_TITLE_FIRST_SESSION},
      AchievementDefinition{AchievementId::TenSessions, AchievementMetric::Sessions, 10,
                            StrId::STR_ACH_TITLE_TEN_SESSIONS},
      AchievementDefinition{AchievementId::TwentyFiveSessions, AchievementMetric::Sessions, 25,
                            StrId::STR_ACH_TITLE_TWENTY_FIVE_SESSIONS},
      AchievementDefinition{AchievementId::FiftySessions, AchievementMetric::Sessions, 50,
                            StrId::STR_ACH_TITLE_FIFTY_SESSIONS},
      AchievementDefinition{AchievementId::OneHundredSessions, AchievementMetric::Sessions, 100,
                            StrId::STR_ACH_TITLE_ONE_HUNDRED_SESSIONS},
      AchievementDefinition{AchievementId::TwoHundredSessions, AchievementMetric::Sessions, 200,
                            StrId::STR_ACH_TITLE_TWO_HUNDRED_SESSIONS},
      AchievementDefinition{AchievementId::FirstBookFinished, AchievementMetric::BooksFinished, 1,
                            StrId::STR_ACH_TITLE_FIRST_BOOK_FINISHED},
      AchievementDefinition{AchievementId::TwoBooksFinished, AchievementMetric::BooksFinished, 2,
                            StrId::STR_ACH_TITLE_TWO_BOOKS_FINISHED},
      AchievementDefinition{AchievementId::ThreeBooksFinished, AchievementMetric::BooksFinished, 3,
                            StrId::STR_ACH_TITLE_THREE_BOOKS_FINISHED},
      AchievementDefinition{AchievementId::FiveBooksFinished, AchievementMetric::BooksFinished, 5,
                            StrId::STR_ACH_TITLE_FIVE_BOOKS_FINISHED},
      AchievementDefinition{AchievementId::SevenBooksFinished, AchievementMetric::BooksFinished, 7,
                            StrId::STR_ACH_TITLE_SEVEN_BOOKS_FINISHED},
      AchievementDefinition{AchievementId::TenBooksFinished, AchievementMetric::BooksFinished, 10,
                            StrId::STR_ACH_TITLE_TEN_BOOKS_FINISHED},
      AchievementDefinition{AchievementId::FifteenBooksFinished, AchievementMetric::BooksFinished, 15,
                            StrId::STR_ACH_TITLE_FIFTEEN_BOOKS_FINISHED},
      AchievementDefinition{AchievementId::TwentyBooksFinished, AchievementMetric::BooksFinished, 20,
                            StrId::STR_ACH_TITLE_TWENTY_BOOKS_FINISHED},
      AchievementDefinition{AchievementId::TwentyFiveBooksFinished, AchievementMetric::BooksFinished, 25,
                            StrId::STR_ACH_TITLE_TWENTY_FIVE_BOOKS_FINISHED},
      AchievementDefinition{AchievementId::ThirtyBooksFinished, AchievementMetric::BooksFinished, 30,
                            StrId::STR_ACH_TITLE_THIRTY_BOOKS_FINISHED},
      AchievementDefinition{AchievementId::ThirtyFiveBooksFinished, AchievementMetric::BooksFinished, 35,
                            StrId::STR_ACH_TITLE_THIRTY_FIVE_BOOKS_FINISHED},
      AchievementDefinition{AchievementId::FortyBooksFinished, AchievementMetric::BooksFinished, 40,
                            StrId::STR_ACH_TITLE_FORTY_BOOKS_FINISHED},
      AchievementDefinition{AchievementId::FortyFiveBooksFinished, AchievementMetric::BooksFinished, 45,
                            StrId::STR_ACH_TITLE_FORTY_FIVE_BOOKS_FINISHED},
      AchievementDefinition{AchievementId::FiftyBooksFinished, AchievementMetric::BooksFinished, 50,
                            StrId::STR_ACH_TITLE_FIFTY_BOOKS_FINISHED},
      AchievementDefinition{AchievementId::FiftyFiveBooksFinished, AchievementMetric::BooksFinished, 55,
                            StrId::STR_ACH_TITLE_FIFTY_FIVE_BOOKS_FINISHED},
      AchievementDefinition{AchievementId::SixtyBooksFinished, AchievementMetric::BooksFinished, 60,
                            StrId::STR_ACH_TITLE_SIXTY_BOOKS_FINISHED},
      AchievementDefinition{AchievementId::SixtyFiveBooksFinished, AchievementMetric::BooksFinished, 65,
                            StrId::STR_ACH_TITLE_SIXTY_FIVE_BOOKS_FINISHED},
      AchievementDefinition{AchievementId::SeventyBooksFinished, AchievementMetric::BooksFinished, 70,
                            StrId::STR_ACH_TITLE_SEVENTY_BOOKS_FINISHED},
      AchievementDefinition{AchievementId::SeventyFiveBooksFinished, AchievementMetric::BooksFinished, 75,
                            StrId::STR_ACH_TITLE_SEVENTY_FIVE_BOOKS_FINISHED},
      AchievementDefinition{AchievementId::EightyBooksFinished, AchievementMetric::BooksFinished, 80,
                            StrId::STR_ACH_TITLE_EIGHTY_BOOKS_FINISHED},
      AchievementDefinition{AchievementId::EightyFiveBooksFinished, AchievementMetric::BooksFinished, 85,
                            StrId::STR_ACH_TITLE_EIGHTY_FIVE_BOOKS_FINISHED},
      AchievementDefinition{AchievementId::NinetyBooksFinished, AchievementMetric::BooksFinished, 90,
                            StrId::STR_ACH_TITLE_NINETY_BOOKS_FINISHED},
      AchievementDefinition{AchievementId::NinetyFiveBooksFinished, AchievementMetric::BooksFinished, 95,
                            StrId::STR_ACH_TITLE_NINETY_FIVE_BOOKS_FINISHED},
      AchievementDefinition{AchievementId::OneHundredBooksFinished, AchievementMetric::BooksFinished, 100,
                            StrId::STR_ACH_TITLE_ONE_HUNDRED_BOOKS_FINISHED},
      AchievementDefinition{AchievementId::ReadingOneHour, AchievementMetric::TotalReadingMs, 60ULL * 60ULL * 1000ULL,
                            StrId::STR_ACH_TITLE_READING_ONE_HOUR},
      AchievementDefinition{AchievementId::ReadingFiveHours, AchievementMetric::TotalReadingMs,
                            5ULL * 60ULL * 60ULL * 1000ULL, StrId::STR_ACH_TITLE_READING_FIVE_HOURS},
      AchievementDefinition{AchievementId::ReadingTenHours, AchievementMetric::TotalReadingMs,
                            10ULL * 60ULL * 60ULL * 1000ULL, StrId::STR_ACH_TITLE_READING_TEN_HOURS},
      AchievementDefinition{AchievementId::ReadingOneDay, AchievementMetric::TotalReadingMs,
                            24ULL * 60ULL * 60ULL * 1000ULL, StrId::STR_ACH_TITLE_READING_ONE_DAY},
      AchievementDefinition{AchievementId::ReadingFiftyHours, AchievementMetric::TotalReadingMs,
                            50ULL * 60ULL * 60ULL * 1000ULL, StrId::STR_ACH_TITLE_READING_FIFTY_HOURS},
      AchievementDefinition{AchievementId::ReadingOneHundredHours, AchievementMetric::TotalReadingMs,
                            100ULL * 60ULL * 60ULL * 1000ULL, StrId::STR_ACH_TITLE_READING_ONE_HUNDRED_HOURS},
      AchievementDefinition{AchievementId::ReadingTwoHundredHours, AchievementMetric::TotalReadingMs,
                            200ULL * 60ULL * 60ULL * 1000ULL, StrId::STR_ACH_TITLE_READING_TWO_HUNDRED_HOURS},
      AchievementDefinition{AchievementId::FirstGoalDay, AchievementMetric::GoalDays, 1,
                            StrId::STR_ACH_TITLE_FIRST_GOAL_DAY},
      AchievementDefinition{AchievementId::SevenGoalDays, AchievementMetric::GoalDays, 7,
                            StrId::STR_ACH_TITLE_SEVEN_GOAL_DAYS},
      AchievementDefinition{AchievementId::ThirtyGoalDays, AchievementMetric::GoalDays, 30,
                            StrId::STR_ACH_TITLE_THIRTY_GOAL_DAYS},
      AchievementDefinition{AchievementId::SixtyGoalDays, AchievementMetric::GoalDays, 60,
                            StrId::STR_ACH_TITLE_SIXTY_GOAL_DAYS},
      AchievementDefinition{AchievementId::EightyGoalDays, AchievementMetric::GoalDays, 80,
                            StrId::STR_ACH_TITLE_EIGHTY_GOAL_DAYS},
      AchievementDefinition{AchievementId::ThreeGoalStreak, AchievementMetric::MaxGoalStreak, 3,
                            StrId::STR_ACH_TITLE_THREE_GOAL_STREAK},
      AchievementDefinition{AchievementId::SevenGoalStreak, AchievementMetric::MaxGoalStreak, 7,
                            StrId::STR_ACH_TITLE_SEVEN_GOAL_STREAK},
      AchievementDefinition{AchievementId::FourteenGoalStreak, AchievementMetric::MaxGoalStreak, 14,
                            StrId::STR_ACH_TITLE_FOURTEEN_GOAL_STREAK},
      AchievementDefinition{AchievementId::ThirtyGoalStreak, AchievementMetric::MaxGoalStreak, 30,
                            StrId::STR_ACH_TITLE_THIRTY_GOAL_STREAK},
      AchievementDefinition{AchievementId::SixtyGoalStreak, AchievementMetric::MaxGoalStreak, 60,
                            StrId::STR_ACH_TITLE_SIXTY_GOAL_STREAK},
      AchievementDefinition{AchievementId::FirstBookmark, AchievementMetric::TotalBookmarksAdded, 1,
                            StrId::STR_ACH_TITLE_FIRST_BOOKMARK},
      AchievementDefinition{AchievementId::TenBookmarks, AchievementMetric::TotalBookmarksAdded, 10,
                            StrId::STR_ACH_TITLE_TEN_BOOKMARKS},
      AchievementDefinition{AchievementId::TwentyFiveBookmarks, AchievementMetric::TotalBookmarksAdded, 25,
                            StrId::STR_ACH_TITLE_TWENTY_FIVE_BOOKMARKS},
      AchievementDefinition{AchievementId::FiftyBookmarks, AchievementMetric::TotalBookmarksAdded, 50,
                            StrId::STR_ACH_TITLE_FIFTY_BOOKMARKS},
      AchievementDefinition{AchievementId::FifteenMinuteSession, AchievementMetric::MaxSessionMs,
                            15ULL * 60ULL * 1000ULL, StrId::STR_ACH_TITLE_FIFTEEN_MINUTE_SESSION},
      AchievementDefinition{AchievementId::ThirtyMinuteSession, AchievementMetric::MaxSessionMs,
                            30ULL * 60ULL * 1000ULL, StrId::STR_ACH_TITLE_THIRTY_MINUTE_SESSION},
      AchievementDefinition{AchievementId::FortyFiveMinuteSession, AchievementMetric::MaxSessionMs,
                            45ULL * 60ULL * 1000ULL, StrId::STR_ACH_TITLE_FORTY_FIVE_MINUTE_SESSION},
      AchievementDefinition{AchievementId::SixtyMinuteSession, AchievementMetric::MaxSessionMs, 60ULL * 60ULL * 1000ULL,
                            StrId::STR_ACH_TITLE_SIXTY_MINUTE_SESSION},
      AchievementDefinition{AchievementId::NinetyMinuteSession, AchievementMetric::MaxSessionMs,
                            90ULL * 60ULL * 1000ULL, StrId::STR_ACH_TITLE_NINETY_MINUTE_SESSION},
      AchievementDefinition{AchievementId::TwoHourSession, AchievementMetric::MaxSessionMs, 120ULL * 60ULL * 1000ULL,
                            StrId::STR_ACH_TITLE_TWO_HOUR_SESSION},
  };
  return items;
}

uint32_t AchievementsStore::getReferenceTimestamp() {
  bool usedFallback = false;
  const uint32_t timestamp = READING_STATS.getDisplayTimestamp(&usedFallback);
  return TimeUtils::isClockValid(timestamp) ? timestamp : static_cast<uint32_t>(time(nullptr));
}

bool AchievementsStore::hasString(const std::vector<std::string>& values, const std::string& value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

const AchievementDefinition& AchievementsStore::getDefinition(const AchievementId id) const {
  const auto& items = definitions();
  const auto it = std::find_if(items.begin(), items.end(),
                               [id](const AchievementDefinition& definition) { return definition.id == id; });
  if (it != items.end()) {
    return *it;
  }
  return items.front();
}

void AchievementsStore::markDirty() { dirty = true; }

std::string AchievementsStore::getTitle(const AchievementId id) const {
  const auto& definition = getDefinition(id);
  return I18N.get(definition.titleId);
}

std::string AchievementsStore::getDescription(const AchievementId id) const {
  const auto& definition = getDefinition(id);

  switch (definition.metric) {
    case AchievementMetric::BooksStarted:
      if (definition.target == 1) {
        return I18N.get(StrId::STR_ACH_DESC_START_FIRST_BOOK);
      }
      return formatUInt(I18N.get(StrId::STR_ACH_DESC_START_BOOKS_FMT), definition.target);
    case AchievementMetric::BooksFinished:
      if (definition.target == 1) {
        return I18N.get(StrId::STR_ACH_DESC_FINISH_FIRST_BOOK);
      }
      return formatUInt(I18N.get(StrId::STR_ACH_DESC_FINISH_BOOKS_FMT), definition.target);
    case AchievementMetric::Sessions:
      if (definition.target == 1) {
        return I18N.get(StrId::STR_ACH_DESC_FIRST_SESSION);
      }
      return formatUInt(I18N.get(StrId::STR_ACH_DESC_SESSIONS_FMT), definition.target);
    case AchievementMetric::TotalReadingMs:
      return formatText(I18N.get(StrId::STR_ACH_DESC_TOTAL_READING_FMT), formatDurationCompact(definition.target));
    case AchievementMetric::GoalDays:
      if (definition.target == 1) {
        return I18N.get(StrId::STR_ACH_DESC_FIRST_GOAL_DAY);
      }
      return formatUInt(I18N.get(StrId::STR_ACH_DESC_GOAL_DAYS_FMT), definition.target);
    case AchievementMetric::MaxGoalStreak:
      return formatUInt(I18N.get(StrId::STR_ACH_DESC_GOAL_STREAK_FMT), definition.target);
    case AchievementMetric::TotalBookmarksAdded:
      if (definition.target == 1) {
        return I18N.get(StrId::STR_ACH_DESC_FIRST_BOOKMARK);
      }
      return formatUInt(I18N.get(StrId::STR_ACH_DESC_BOOKMARKS_FMT), definition.target);
    case AchievementMetric::MaxSessionMs:
      return formatText(I18N.get(StrId::STR_ACH_DESC_LONG_SESSION_FMT), formatDurationCompact(definition.target));
  }

  return "";
}

void AchievementsStore::unlock(const AchievementId id, const uint32_t timestamp, const bool enqueuePopup) {
  auto& state = states[indexOf(id)];
  if (state.unlocked) {
    return;
  }

  state.unlocked = true;
  state.unlockedAt = timestamp;
  if (enqueuePopup && SETTINGS.achievementPopups) {
    pendingUnlocks.push_back(id);
  }
  markDirty();
}

uint64_t AchievementsStore::getEffectiveTodayReadingMs(const uint32_t dayOrdinal) const {
  uint64_t todayReadingMs = READING_STATS.getTodayReadingMs();
  if (dayOrdinal != 0 && dayOrdinal == resetDayOrdinal) {
    if (todayReadingMs > resetDayBaselineMs) {
      todayReadingMs -= resetDayBaselineMs;
    } else {
      todayReadingMs = 0;
    }
  }
  return todayReadingMs;
}

AchievementsStore::ProgressSnapshot AchievementsStore::buildProgressSnapshot() const {
  ProgressSnapshot snapshot;
  snapshot.booksStarted = static_cast<uint32_t>(startedBooks.size());
  snapshot.booksFinished = static_cast<uint32_t>(finishedBooks.size());
  snapshot.sessions = countedSessions;
  snapshot.totalReadingMs = accumulatedReadingMs;
  snapshot.goalDays = goalDaysCount;
  snapshot.maxGoalStreak = maxGoalStreak;
  snapshot.totalBookmarksAdded = totalBookmarksAdded;
  snapshot.maxSessionMs = longestSessionMs;
  return snapshot;
}

void AchievementsStore::evaluateProgress(const bool enqueuePopups) {
  const auto progress = buildProgressSnapshot();
  const uint32_t unlockTimestamp = getReferenceTimestamp();

  for (const auto& definition : definitions()) {
    uint64_t currentValue = 0;
    switch (definition.metric) {
      case AchievementMetric::BooksStarted:
        currentValue = progress.booksStarted;
        break;
      case AchievementMetric::BooksFinished:
        currentValue = progress.booksFinished;
        break;
      case AchievementMetric::Sessions:
        currentValue = progress.sessions;
        break;
      case AchievementMetric::TotalReadingMs:
        currentValue = progress.totalReadingMs;
        break;
      case AchievementMetric::GoalDays:
        currentValue = progress.goalDays;
        break;
      case AchievementMetric::MaxGoalStreak:
        currentValue = progress.maxGoalStreak;
        break;
      case AchievementMetric::TotalBookmarksAdded:
        currentValue = progress.totalBookmarksAdded;
        break;
      case AchievementMetric::MaxSessionMs:
        currentValue = progress.maxSessionMs;
        break;
    }

    if (currentValue >= definition.target) {
      unlock(definition.id, unlockTimestamp, enqueuePopups);
    }
  }
}

void AchievementsStore::bootstrapFromCurrentStats() {
  startedBooks.clear();
  finishedBooks.clear();

  for (const auto& book : READING_STATS.getBooks()) {
    if (book.bookId.empty()) {
      continue;
    }
    startedBooks.push_back(book.bookId);
    if (book.completed) {
      finishedBooks.push_back(book.bookId);
    }
  }

  accumulatedReadingMs = READING_STATS.getTotalReadingMs();
  countedSessions = countSessionsFromStats();
  totalBookmarksAdded = countCurrentBookmarksFromStats();
  longestSessionMs = findLongestSessionFromStats();
  resetDayOrdinal = 0;
  resetDayBaselineMs = 0;
  refreshGoalDerivedProgressFromStats();

  evaluateProgress(false);
  markDirty();
}

bool AchievementsStore::refreshGoalDerivedProgressFromStats() {
  const uint32_t newGoalDaysCount = countGoalDaysFromStats();
  const uint32_t newCurrentGoalStreak = READING_STATS.getCurrentStreakDays();
  const uint32_t newMaxGoalStreak = READING_STATS.getMaxStreakDays();

  uint32_t newLastGoalDayOrdinal = 0;
  for (const auto& day : READING_STATS.getReadingDays()) {
    if (day.readingMs >= getDailyReadingGoalMs()) {
      newLastGoalDayOrdinal = day.dayOrdinal;
    }
  }

  const bool changed = goalDaysCount != newGoalDaysCount || currentGoalStreak != newCurrentGoalStreak ||
                       maxGoalStreak != newMaxGoalStreak || lastGoalDayOrdinal != newLastGoalDayOrdinal;

  goalDaysCount = newGoalDaysCount;
  currentGoalStreak = newCurrentGoalStreak;
  maxGoalStreak = newMaxGoalStreak;
  lastGoalDayOrdinal = newLastGoalDayOrdinal;
  return changed;
}

void AchievementsStore::reconcileFromCurrentStats() {
  if (!SETTINGS.achievementsEnabled) {
    return;
  }
  evaluateProgress(false);
  if (dirty) {
    saveToFile();
  }
}

void AchievementsStore::recordSessionEnded(const ReadingSessionSnapshot& snapshot) {
  if (!SETTINGS.achievementsEnabled || !snapshot.valid || snapshot.serial == 0 ||
      snapshot.serial == lastProcessedSessionSerial || (snapshot.bookId.empty() && snapshot.path.empty())) {
    return;
  }

  lastProcessedSessionSerial = snapshot.serial;
  const std::string bookKey = !snapshot.bookId.empty() ? snapshot.bookId : snapshot.path;

  if (!hasString(startedBooks, bookKey)) {
    startedBooks.push_back(bookKey);
    markDirty();
  }

  accumulatedReadingMs += snapshot.sessionMs;
  longestSessionMs = std::max(longestSessionMs, snapshot.sessionMs);
  if (snapshot.counted) {
    ++countedSessions;
  }

  if (snapshot.completedThisSession && !hasString(finishedBooks, bookKey)) {
    finishedBooks.push_back(bookKey);
    markDirty();
  }

  const uint32_t referenceTimestamp = getReferenceTimestamp();
  const uint32_t dayOrdinal =
      TimeUtils::isClockValid(referenceTimestamp) ? TimeUtils::getLocalDayOrdinal(referenceTimestamp) : 0;
  const uint64_t effectiveTodayReadingMs = getEffectiveTodayReadingMs(dayOrdinal);
  if (dayOrdinal != 0 && effectiveTodayReadingMs >= getDailyReadingGoalMs() && lastGoalDayOrdinal != dayOrdinal) {
    ++goalDaysCount;
    if (lastGoalDayOrdinal != 0 && lastGoalDayOrdinal + 1 == dayOrdinal) {
      ++currentGoalStreak;
    } else {
      currentGoalStreak = 1;
    }
    maxGoalStreak = std::max(maxGoalStreak, currentGoalStreak);
    lastGoalDayOrdinal = dayOrdinal;
    markDirty();
  }

  markDirty();
  evaluateProgress(true);
  saveToFile();
}

void AchievementsStore::recordBookmarkAdded() {
  if (!SETTINGS.achievementsEnabled) {
    return;
  }

  ++totalBookmarksAdded;
  markDirty();
  evaluateProgress(true);
  saveToFile();
}

std::vector<AchievementView> AchievementsStore::buildViews() const {
  const auto progress = buildProgressSnapshot();
  std::vector<AchievementView> views;
  views.reserve(definitions().size());

  for (const auto& definition : definitions()) {
    uint64_t currentValue = 0;
    switch (definition.metric) {
      case AchievementMetric::BooksStarted:
        currentValue = progress.booksStarted;
        break;
      case AchievementMetric::BooksFinished:
        currentValue = progress.booksFinished;
        break;
      case AchievementMetric::Sessions:
        currentValue = progress.sessions;
        break;
      case AchievementMetric::TotalReadingMs:
        currentValue = progress.totalReadingMs;
        break;
      case AchievementMetric::GoalDays:
        currentValue = progress.goalDays;
        break;
      case AchievementMetric::MaxGoalStreak:
        currentValue = progress.maxGoalStreak;
        break;
      case AchievementMetric::TotalBookmarksAdded:
        currentValue = progress.totalBookmarksAdded;
        break;
      case AchievementMetric::MaxSessionMs:
        currentValue = progress.maxSessionMs;
        break;
    }

    views.push_back(AchievementView{&definition, states[indexOf(definition.id)], currentValue, definition.target});
  }

  std::stable_sort(views.begin(), views.end(), [](const AchievementView& lhs, const AchievementView& rhs) {
    if (lhs.state.unlocked != rhs.state.unlocked) {
      return lhs.state.unlocked > rhs.state.unlocked;
    }
    if (lhs.state.unlocked && rhs.state.unlocked && lhs.state.unlockedAt != rhs.state.unlockedAt) {
      return lhs.state.unlockedAt > rhs.state.unlockedAt;
    }
    return false;
  });

  return views;
}

std::string AchievementsStore::popNextPopupMessage() {
  if (pendingUnlocks.empty()) {
    return "";
  }

  const AchievementId id = pendingUnlocks.front();
  pendingUnlocks.erase(pendingUnlocks.begin());
  return std::string(tr(STR_ACHIEVEMENT_UNLOCKED)) + ": " + getTitle(id);
}

bool AchievementsStore::saveToFile() const {
  if (!dirty) {
    return true;
  }

  Storage.mkdir("/.crosspoint");
  const bool saved = JsonSettingsIO::saveAchievements(*this, ACHIEVEMENTS_FILE_JSON);
  if (saved) {
    dirty = false;
  }
  return saved;
}

bool AchievementsStore::loadFromFile() {
  JsonSettingsIO::recoverFile(ACHIEVEMENTS_FILE_JSON);
  const std::string tempPath = std::string(ACHIEVEMENTS_FILE_JSON) + ".tmp";
  if (!Storage.exists(ACHIEVEMENTS_FILE_JSON) && Storage.exists(tempPath.c_str())) {
    if (Storage.rename(tempPath.c_str(), ACHIEVEMENTS_FILE_JSON)) {
      LOG_DBG("ACH", "Recovered achievements.json from interrupted temp file");
    }
  }

  if (!Storage.exists(ACHIEVEMENTS_FILE_JSON)) {
    bootstrapFromCurrentStats();
    return saveToFile();
  }

  const bool loaded = JsonSettingsIO::loadAchievementsFromFile(*this, ACHIEVEMENTS_FILE_JSON);
  if (!loaded) {
    return false;
  }

  const bool loadNeedsSave = dirty;
  dirty = loadNeedsSave;
  if (refreshGoalDerivedProgressFromStats()) {
    markDirty();
  }
  evaluateProgress(false);
  if (dirty) {
    saveToFile();
  }
  return true;
}

void AchievementsStore::reset() {
  states = {};
  startedBooks.clear();
  finishedBooks.clear();
  pendingUnlocks.clear();
  accumulatedReadingMs = 0;
  countedSessions = 0;
  totalBookmarksAdded = 0;
  longestSessionMs = 0;
  goalDaysCount = 0;
  currentGoalStreak = 0;
  maxGoalStreak = 0;
  lastGoalDayOrdinal = 0;
  lastProcessedSessionSerial = 0;

  const uint32_t referenceTimestamp = getReferenceTimestamp();
  if (TimeUtils::isClockValid(referenceTimestamp)) {
    resetDayOrdinal = TimeUtils::getLocalDayOrdinal(referenceTimestamp);
    resetDayBaselineMs = READING_STATS.getTodayReadingMs();
  } else {
    resetDayOrdinal = 0;
    resetDayBaselineMs = 0;
  }

  markDirty();
  saveToFile();
}

void AchievementsStore::syncWithPreviousStats() {
  for (const auto& book : READING_STATS.getBooks()) {
    if (book.bookId.empty()) {
      continue;
    }

    if (!hasString(startedBooks, book.bookId)) {
      startedBooks.push_back(book.bookId);
      markDirty();
    }

    if (book.completed && !hasString(finishedBooks, book.bookId)) {
      finishedBooks.push_back(book.bookId);
      markDirty();
    }
  }

  accumulatedReadingMs = std::max<uint64_t>(accumulatedReadingMs, READING_STATS.getTotalReadingMs());
  countedSessions = std::max(countedSessions, countSessionsFromStats());
  totalBookmarksAdded = std::max(totalBookmarksAdded, countCurrentBookmarksFromStats());
  longestSessionMs = std::max(longestSessionMs, findLongestSessionFromStats());
  refreshGoalDerivedProgressFromStats();

  markDirty();
  evaluateProgress(false);
  saveToFile();
}

void AchievementsStore::rebuildProgressFromCurrentStats() {
  bootstrapFromCurrentStats();
  saveToFile();
}
