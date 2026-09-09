#include "CrossPointState.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>
#include <Serialization.h>

#include <algorithm>
#include <string>

#include "util/TimeUtils.h"

namespace {
constexpr uint8_t STATE_FILE_VERSION = 6;
constexpr char STATE_FILE_BIN[] = "/.crosspoint/state.bin";
constexpr char STATE_FILE_JSON[] = "/.crosspoint/state.json";
constexpr char STATE_FILE_BAK[] = "/.crosspoint/state.bin.bak";
}  // namespace

CrossPointState CrossPointState::instance;

void KOReaderSyncSessionState::clear() {
  active = false;
  epubPath.clear();
  spineIndex = 0;
  page = 0;
  totalPagesInSpine = 0;
  paragraphIndex = 0;
  hasParagraphIndex = false;
  xhtmlSeekHint = 0;
  hasLocalKoReaderPosition = false;
  localKoReaderProgress.clear();
  localKoReaderPercentage = 0.0f;
  localChapterLabel.clear();
  intent = KOReaderSyncIntentState::COMPARE;
  outcome = KOReaderSyncOutcomeState::NONE;
  resultSpineIndex = 0;
  resultPage = 0;
  resultParagraphIndex = 0;
  resultHasParagraphIndex = false;
  resultListItemIndex = 0;
  resultHasListItemIndex = false;
  exitToHomeAfterSync = false;
  autoPullEpubPath.clear();
}

void PendingBookmarkJumpState::clear() {
  active = false;
  bookPath.clear();
  spineIndex = 0;
  pageNumber = 0;
}

bool CrossPointState::isRecentSleep(const uint16_t idx, const uint8_t checkCount) const {
  const uint8_t effectiveCount = std::min(checkCount, recentSleepFill);
  for (uint8_t i = 0; i < effectiveCount; i++) {
    const uint8_t slot = (recentSleepPos + SLEEP_RECENT_COUNT - 1 - i) % SLEEP_RECENT_COUNT;
    if (recentSleepImages[slot] == idx) return true;
  }
  return false;
}

void CrossPointState::pushRecentSleep(const uint16_t idx) {
  recentSleepImages[recentSleepPos] = idx;
  recentSleepPos = (recentSleepPos + 1) % SLEEP_RECENT_COUNT;
  if (recentSleepFill < SLEEP_RECENT_COUNT) recentSleepFill++;
}

uint16_t CrossPointState::getMostRecentSleepIndex() const {
  if (recentSleepFill == 0) {
    return UINT16_MAX;
  }
  const uint8_t slot = (recentSleepPos + SLEEP_RECENT_COUNT - 1) % SLEEP_RECENT_COUNT;
  return recentSleepImages[slot];
}

bool CrossPointState::saveToFile() {
  Storage.mkdir("/.crosspoint");
  lastKnownValidTimestamp = std::max(lastKnownValidTimestamp, TimeUtils::getCurrentValidTimestamp());
  return JsonSettingsIO::saveState(*this, STATE_FILE_JSON);
}

bool CrossPointState::loadFromFile() {
  JsonSettingsIO::recoverFile(STATE_FILE_JSON);
  const std::string tempPath = std::string(STATE_FILE_JSON) + ".tmp";
  if (!Storage.exists(STATE_FILE_JSON) && Storage.exists(tempPath.c_str())) {
    if (Storage.rename(tempPath.c_str(), STATE_FILE_JSON)) {
      LOG_DBG("CPS", "Recovered state.json from interrupted temp file");
    }
  }

  // Try JSON first
  if (Storage.exists(STATE_FILE_JSON)) {
    String json = Storage.readFile(STATE_FILE_JSON);
    if (!json.isEmpty()) {
      return JsonSettingsIO::loadState(*this, json.c_str());
    }
  }

  // Fall back to binary migration
  if (Storage.exists(STATE_FILE_BIN)) {
    if (loadFromBinaryFile()) {
      if (saveToFile()) {
        Storage.rename(STATE_FILE_BIN, STATE_FILE_BAK);
        LOG_DBG("CPS", "Migrated state.bin to state.json");
        return true;
      } else {
        LOG_ERR("CPS", "Failed to save state during migration");
        return false;
      }
    }
  }

  return false;
}

void CrossPointState::recordUsefulStart(const uint8_t reminderThreshold) {
  if (reminderThreshold == 0 || syncDayReminderLatched) {
    return;
  }

  if (syncDayReminderStartCount < UINT8_MAX) {
    syncDayReminderStartCount++;
  }

  if (syncDayReminderStartCount >= reminderThreshold) {
    syncDayReminderLatched = true;
  }
}

void CrossPointState::registerValidTimeSync(const uint32_t validTimestamp) {
  if (validTimestamp > 0) {
    lastKnownValidTimestamp = validTimestamp;
    syncDayReminderStartCount = 0;
    syncDayReminderLatched = false;
  }
}

bool CrossPointState::shouldShowSyncDayReminder(const uint8_t reminderThreshold) const {
  if (reminderThreshold == 0) {
    return false;
  }

  return syncDayReminderLatched || syncDayReminderStartCount >= reminderThreshold;
}

bool CrossPointState::loadFromBinaryFile() {
  FsFile inputFile;
  if (!Storage.openFileForRead("CPS", STATE_FILE_BIN, inputFile)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);
  if (version > STATE_FILE_VERSION) {
    LOG_ERR("CPS", "Deserialization failed: Unknown version %u", version);
    inputFile.close();
    return false;
  }

  std::fill(std::begin(recentSleepImages), std::end(recentSleepImages), 0);
  recentSleepPos = 0;
  recentSleepFill = 0;
  serialization::readString(inputFile, openEpubPath);
  if (version >= 2) {
    uint8_t legacyLastSleep = UINT8_MAX;
    serialization::readPod(inputFile, legacyLastSleep);
    if (legacyLastSleep != UINT8_MAX) {
      pushRecentSleep(static_cast<uint16_t>(legacyLastSleep));
    }
  }

  if (version >= 3) {
    serialization::readPod(inputFile, readerActivityLoadCount);
  }

  if (version >= 4) {
    serialization::readPod(inputFile, lastSleepFromReader);
  } else {
    lastSleepFromReader = false;
  }

  if (version >= 5) {
    serialization::readPod(inputFile, lastKnownValidTimestamp);
  } else {
    lastKnownValidTimestamp = 0;
  }

  if (version >= 6) {
    serialization::readPod(inputFile, syncDayReminderStartCount);
    serialization::readPod(inputFile, syncDayReminderLatched);
  } else {
    syncDayReminderStartCount = 0;
    syncDayReminderLatched = false;
  }

  inputFile.close();
  return true;
}
