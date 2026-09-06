#include "HalClock.h"

#include <Logging.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <time.h>

#include <cassert>
#include <cstdlib>
#include <cstring>

HalClock halClock;  // Singleton instance

namespace {
constexpr time_t VALID_UTC_EPOCH = static_cast<time_t>(1704067200UL);  // 2024-01-01 UTC

class ScopedTimezone {
 public:
  explicit ScopedTimezone(const char* tz) {
    const char* current = getenv("TZ");
    if (current) {
      hadPrevious = true;
      strncpy(previous, current, sizeof(previous) - 1);
      previous[sizeof(previous) - 1] = '\0';
    }
    setenv("TZ", tz, 1);
    tzset();
  }

  ~ScopedTimezone() {
    if (hadPrevious) {
      setenv("TZ", previous, 1);
    } else {
      unsetenv("TZ");
    }
    tzset();
  }

 private:
  char previous[96] = {};
  bool hadPrevious = false;
};

time_t utcTmToEpoch(const struct tm& utc) {
  struct tm copy = utc;
  copy.tm_isdst = 0;
  ScopedTimezone timezone("UTC0");
  return mktime(&copy);
}

bool isValidEpoch(const time_t epoch) { return epoch >= VALID_UTC_EPOCH; }

bool syncSystemClockFromNtpUtc() {
  LOG_INF("CLK", "Starting NTP sync...");
  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }

  const bool initialClockValid = isValidEpoch(time(nullptr));
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_setservername(1, "time.nist.gov");
  esp_sntp_init();

  constexpr int maxAttempts = 50;
  for (int i = 0; i < maxAttempts; i++) {
    const time_t currentTime = time(nullptr);
    const bool currentClockValid = isValidEpoch(currentTime);
    const bool syncCompleted = sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED;
    const bool clockJumpedToValid = !initialClockValid && currentClockValid;

    if ((syncCompleted || clockJumpedToValid) && currentClockValid) {
      return true;
    }
    delay(100);
  }

  LOG_ERR("CLK", "NTP sync timed out");
  return false;
}

bool isValidUtcTm(const struct tm& utc) {
  if (utc.tm_year < 124) {
    return false;
  }
  if (utc.tm_mon < 0 || utc.tm_mon > 11) {
    return false;
  }
  if (utc.tm_mday < 1 || utc.tm_mday > 31) {
    return false;
  }
  if (utc.tm_hour < 0 || utc.tm_hour > 23) {
    return false;
  }
  if (utc.tm_min < 0 || utc.tm_min > 59) {
    return false;
  }
  if (utc.tm_sec < 0 || utc.tm_sec > 59) {
    return false;
  }
  const time_t epoch = utcTmToEpoch(utc);
  return isValidEpoch(epoch);
}
}  // namespace

void HalClock::begin() {
  _available = _sdkRtc.begin();
  LOG_INF("CLK", _available ? "RTC found" : "RTC not found");
  if (!_available) {
    return;
  }

  struct tm utc{};
  readUtcTm(utc, true);
}

bool HalClock::readUtcTmFromChip(struct tm& outUtc, const bool forceRefresh) const {
  if (!_available) {
    return false;
  }

  const unsigned long now = millis();
  if (!forceRefresh && _lastPollMs != 0 && (now - _lastPollMs) < CLOCK_POLL_MS && _hasCachedTime) {
    outUtc = _cachedUtcTm;
    return true;
  }

  Rtc::DateTime dt;
  if (!_sdkRtc.now(dt)) {
    if (!_hasCachedTime) {
      return false;
    }
    outUtc = _cachedUtcTm;
    return true;
  }

  struct tm utc{};
  utc.tm_sec = dt.second;
  utc.tm_min = dt.minute;
  utc.tm_hour = dt.hour;
  utc.tm_mday = dt.day;
  utc.tm_mon = static_cast<int>(dt.month) - 1;
  utc.tm_year = static_cast<int>(dt.year) - 1900;
  utc.tm_wday = dt.weekday;
  utc.tm_isdst = 0;

  if (!isValidUtcTm(utc)) {
    if (!_hasCachedTime) {
      return false;
    }
    outUtc = _cachedUtcTm;
    return true;
  }

  _cachedUtcTm = utc;
  _lastPollMs = now;
  _hasCachedTime = true;
  outUtc = utc;
  return true;
}

bool HalClock::readUtcTm(struct tm& outUtc, const bool forceRefresh) const {
  return readUtcTmFromChip(outUtc, forceRefresh);
}

bool HalClock::readUtcEpoch(uint32_t& epochSeconds, const bool forceRefresh) const {
  struct tm utc{};
  if (!readUtcTm(utc, forceRefresh)) {
    return false;
  }
  const time_t epoch = utcTmToEpoch(utc);
  if (epoch < 0) {
    return false;
  }
  epochSeconds = static_cast<uint32_t>(epoch);
  return true;
}

bool HalClock::writeUtcTmToChip(const struct tm& utc) const {
  assert(utc.tm_hour >= 0 && utc.tm_hour < 24);
  assert(utc.tm_min >= 0 && utc.tm_min < 60);
  assert(utc.tm_sec >= 0 && utc.tm_sec < 60);

  Rtc::DateTime dt;
  dt.year = static_cast<uint16_t>(utc.tm_year + 1900);
  dt.month = static_cast<uint8_t>(utc.tm_mon + 1);
  dt.day = static_cast<uint8_t>(utc.tm_mday);
  dt.hour = static_cast<uint8_t>(utc.tm_hour);
  dt.minute = static_cast<uint8_t>(utc.tm_min);
  dt.second = static_cast<uint8_t>(utc.tm_sec);
  dt.weekday = static_cast<uint8_t>(utc.tm_wday < 0 ? 0 : utc.tm_wday);
  if (!_sdkRtc.set(dt)) {
    LOG_ERR("CLK", "Failed to write datetime to RTC");
    return false;
  }

  _cachedUtcTm = utc;
  _lastPollMs = 0;
  _hasCachedTime = true;
  return true;
}

bool HalClock::writeUtcTm(const struct tm& utc) {
  if (!_available) {
    return false;
  }
  return writeUtcTmToChip(utc);
}

bool HalClock::getUtcTime(uint8_t& hour, uint8_t& minute, const bool forceRefresh) const {
  struct tm utc{};
  if (!readUtcTm(utc, forceRefresh)) {
    return false;
  }
  hour = static_cast<uint8_t>(utc.tm_hour);
  minute = static_cast<uint8_t>(utc.tm_min);
  return true;
}

bool HalClock::formatTime(char* buf, size_t bufSize, uint8_t utcOffsetQuarterHoursBiased, bool use12Hour) const {
  if (bufSize < (use12Hour ? 9u : 6u)) return false;
  uint8_t h, m;
  if (!getUtcTime(h, m)) return false;

  // Apply UTC offset: convert biased value to signed quarter-hours.
  // Clamp against corrupted persisted values so display time can't drift outside [-12:00, +14:00].
  if (utcOffsetQuarterHoursBiased > 104) utcOffsetQuarterHoursBiased = 104;
  int offsetQuarterHours = static_cast<int>(utcOffsetQuarterHoursBiased) - 48;
  int totalMinutes = static_cast<int>(h) * 60 + static_cast<int>(m) + offsetQuarterHours * 15;

  // Wrap around 24 hours
  totalMinutes = ((totalMinutes % 1440) + 1440) % 1440;

  const int hour24 = totalMinutes / 60;
  const int min = totalMinutes % 60;
  if (use12Hour) {
    const bool pm = hour24 >= 12;
    int hour12 = hour24 % 12;
    if (hour12 == 0) hour12 = 12;
    snprintf(buf, bufSize, "%d:%02d %s", hour12, min, pm ? "PM" : "AM");
  } else {
    snprintf(buf, bufSize, "%02d:%02d", hour24, min);
  }
  return true;
}

bool HalClock::syncFromNTP() {
  if (!_available) {
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("CLK", "WiFi not connected, cannot sync NTP");
    return false;
  }

  if (!syncSystemClockFromNtpUtc()) {
    return false;
  }

  const time_t now = time(nullptr);
  struct tm utc{};
  gmtime_r(&now, &utc);

  if (writeUtcTm(utc)) {
    LOG_INF("CLK", "RTC set to %04d-%02d-%02d %02d:%02d:%02d UTC", utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
            utc.tm_hour, utc.tm_min, utc.tm_sec);
    return true;
  }
  return false;
}
