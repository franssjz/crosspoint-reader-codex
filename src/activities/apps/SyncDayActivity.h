#pragma once

#include <string>

#include "activities/UiListActivity.h"

// Sync Day: five action rows (sync now, set date, Wi-Fi choice, time zone,
// date format) with live values, a status line and the "how it works" help
// text underneath. While an NTP sync runs the screen shows a progress note.
class SyncDayActivity final : public UiListActivity {
  static constexpr int ACTION_COUNT = 5;

  bool wifiConnectedOnEnter = false;
  bool connectedInActivity = false;
  bool syncing = false;
  bool lastSyncSucceeded = false;
  bool lastSyncFailed = false;
  // Row storage; subtitles/values are refreshed into these strings from
  // buildScreen() (they track live Wi-Fi state and settings).
  std::string rowSubtitles[ACTION_COUNT];
  std::string networkStatus;
  freeink::ui::ListItem rowItems[ACTION_COUNT]{};

  void refreshRowValues();
  void openWifiSelection(bool allowAutoConnect);
  void openManualDateSelection();
  void openTimeZoneSelection();
  void syncTime();
  void showTransientPopup(const char* message, int progress = -1, unsigned long delayMs = 0);
  void createDueReadingStatsBackupWithFeedback();
  bool isWifiConnected() const;
  std::string getStatusMessage() const;

  int listCount() const override { return ACTION_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // Input is ignored while a sync is running.
  bool handleCustomInput() override { return syncing; }
  void drawChrome() override;

 public:
  explicit SyncDayActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("SyncDay", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return syncing; }
};
