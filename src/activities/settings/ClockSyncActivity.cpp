#include "ClockSyncActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TimeUtils.h"

namespace {
  void wifiOff() {
    TimeUtils::stopNtp();
    WiFi.disconnect(false);
    delay(100);
    WiFi.mode(WIFI_OFF);
    delay(100);
  }
}

void ClockSyncActivity::onEnter() {
  Activity::onEnter();
  TimeUtils::configureTimezone();
  syncedTime[0] = '\0';
  wifiConnectedOnEnter = WiFi.status() == WL_CONNECTED;
  connectedInActivity = false;

  if (wifiConnectedOnEnter) {
    beginSync();
    return;
  }

  WiFi.mode(WIFI_STA);
  openWifiSelection();
}

void ClockSyncActivity::onExit() {
  Activity::onExit();

  if (!wifiConnectedOnEnter && connectedInActivity) {
    wifiOff();
  }
}

void ClockSyncActivity::openWifiSelection() {
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, true, false),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void ClockSyncActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected || WiFi.status() != WL_CONNECTED) {
    state = NO_WIFI;
    requestUpdate();
    return;
  }

  if (!wifiConnectedOnEnter) {
    connectedInActivity = true;
  }
  beginSync();
}

void ClockSyncActivity::beginSync() {
  state = SYNCING;
  requestUpdate();
}

void ClockSyncActivity::runSync() {
  const bool ok = halClock.syncFromNTP();
  if (!ok) {
    state = FAILED;
    requestUpdate();
    return;
  }

  // Mark as synced so the auto-sync hook stops firing on future WiFi connects.
  SETTINGS.clockHasBeenSynced = 1;
  SETTINGS.saveToFile();

  TimeUtils::applySystemClockFromRtc(true);

  // Read the freshly synced time back for the user-facing confirmation.
  char buf[9];
  if (TimeUtils::formatStatusBarClockTime(buf, sizeof(buf), SETTINGS.clockFormat == 1)) {
    snprintf(syncedTime, sizeof(syncedTime), "%s", buf);
  }
  state = SUCCESS;
  requestUpdate();
}

void ClockSyncActivity::loop() {
  if (state == SYNCING) {
    // First-tick: render the "Syncing..." screen, then perform the (blocking) sync.
    // requestUpdateAndWait below forces the render before we block on NTP.
    requestUpdateAndWait();
    runSync();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    finish();
  }
}

void ClockSyncActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CLOCK_SYNC));

  const int midY = pageHeight / 2;

  switch (state) {
    case SYNCING:
      renderer.drawCenteredText(UI_12_FONT_ID, midY, tr(STR_CLOCK_SYNCING));
      break;
    case SUCCESS: {
      renderer.drawCenteredText(UI_12_FONT_ID, midY - 20, tr(STR_CLOCK_SYNC_OK), true, EpdFontFamily::BOLD);
      if (syncedTime[0] != '\0') {
        // Leave room for the longest translated label plus the formatted time.
        char line[64];
        snprintf(line, sizeof(line), "%s %s", tr(STR_CURRENT_TIME), syncedTime);
        renderer.drawCenteredText(UI_10_FONT_ID, midY + 10, line);
      }
      break;
    }
    case NO_WIFI:
      renderer.drawCenteredText(UI_12_FONT_ID, midY - 20, tr(STR_CLOCK_SYNC_NO_WIFI), true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 10, tr(STR_CLOCK_SYNC_NO_WIFI_HINT));
      break;
    case FAILED:
      renderer.drawCenteredText(UI_12_FONT_ID, midY - 20, tr(STR_CLOCK_SYNC_FAIL), true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 10, tr(STR_CHECK_SERIAL_OUTPUT));
      break;
  }

  if (state != SYNCING) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OK_BUTTON), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
