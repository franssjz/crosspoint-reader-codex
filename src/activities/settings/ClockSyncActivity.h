#pragma once

#include "activities/Activity.h"

// Manual NTP resync action. Connects to WiFi if needed (reusing the normal
// WiFi selection flow), runs a forced sync (bypassing the once-per-device
// debounce), writes the result to the RTC/system clock, reports
// success/failure, then waits for Back / OK / a screen tap.
class ClockSyncActivity final : public Activity {
 public:
  explicit ClockSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ClockSync", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return state == SYNCING; }
  void render(RenderLock&&) override;

 private:
  enum State { SYNCING, SUCCESS, NO_WIFI, FAILED };
  State state = SYNCING;
  char syncedTime[16] = {0};
  bool wifiConnectedOnEnter = false;
  bool connectedInActivity = false;

  void beginSync();
  void openWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void runSync();
};
