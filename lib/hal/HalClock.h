#pragma once

#include <Arduino.h>
#include <Rtc.h>

#include <ctime>

class HalClock;
extern HalClock halClock;  // Singleton

// Wall-clock HAL over the FreeInk SDK RTC driver. BoardConfig selects the chip:
// DS3231 on the Xteink X3, BM8563 (PCF8563-compatible) on the X4 Pro. Boards
// without an RTC report isAvailable() == false and every accessor returns false.
class HalClock {
  bool _available = false;
  mutable Rtc _sdkRtc;
  mutable tm _cachedUtcTm{};
  mutable bool _hasCachedTime = false;
  mutable unsigned long _lastPollMs = 0;

  static constexpr unsigned long CLOCK_POLL_MS = 10000;  // 10 seconds

  bool readUtcTmFromChip(struct tm& outUtc, bool forceRefresh) const;
  bool writeUtcTmToChip(const struct tm& utc) const;

 public:
  // Call after BoardConfig has selected the active device (gpio.begin()) and
  // after powerManager.begin() so the sensor I2C bus is up.
  void begin();

  // True if an RTC is present on this device
  bool isAvailable() const { return _available; }

  // Read the RTC as UTC calendar time. Uses a short poll cache unless forceRefresh is set.
  bool readUtcTm(struct tm& outUtc, bool forceRefresh = false) const;

  // Read UTC epoch seconds from the RTC.
  bool readUtcEpoch(uint32_t& epochSeconds, bool forceRefresh = false) const;

  // Write UTC calendar time to the RTC.
  bool writeUtcTm(const struct tm& utc);

  // Get current UTC hour/minute, using the poll cache when fresh.
  bool getUtcTime(uint8_t& hour, uint8_t& minute, bool forceRefresh = false) const;

  // Upstream-compatible accessor: current UTC hour (0-23) and minute (0-59).
  bool getTime(uint8_t& hour, uint8_t& minute) const { return getUtcTime(hour, minute); }

  // Format time into a caller-provided buffer.
  // 24h mode produces "HH:MM" (needs >=6 bytes); 12h mode produces "H:MM AM"/"HH:MM PM" (needs >=9 bytes).
  // utcOffsetQuarterHoursBiased: biased quarter-hour offset (48 = UTC+0, 0 = UTC-12, 104 = UTC+14).
  // Returns false if RTC is not available.
  bool formatTime(char* buf, size_t bufSize, uint8_t utcOffsetQuarterHoursBiased = 48, bool use12Hour = false) const;

  // Sync the RTC from an NTP server. Requires WiFi to be connected.
  // Writes full UTC date/time and blocks for up to ~5s while waiting for SNTP.
  // Returns true if the RTC was successfully updated.
  //
  // Debouncing (skip if already synced once) is enforced by the caller, not here,
  // so the HAL stays free of any app-layer settings dependency.
  bool syncFromNTP();
};
