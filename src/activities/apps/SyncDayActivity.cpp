#include "SyncDayActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include <algorithm>
#include <ctime>
#include <utility>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "ManualDateActivity.h"
#include "ReadingStatsStore.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/settings/TimeZoneSelectActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/TimeUtils.h"
#include "util/TimeZoneRegistry.h"

namespace fui = freeink::ui;

namespace {
constexpr int ROW_SYNC_NOW = 0;
constexpr int ROW_SET_DATE = 1;
constexpr int ROW_WIFI_CHOICE = 2;
constexpr int ROW_TIME_ZONE = 3;
constexpr int ROW_DATE_FORMAT = 4;

void wifiOff() {
  TimeUtils::stopNtp();
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);
}

std::string getObtainedDateLabel() {
  const auto displayInfo = HeaderDateUtils::getDisplayDateInfo();
  if (!TimeUtils::isClockValid(displayInfo.timestamp)) {
    return tr(STR_NOT_SET);
  }

  return TimeUtils::formatDate(displayInfo.timestamp, displayInfo.usedFallback);
}

std::string getTimeZoneLabel() {
  return TimeZoneRegistry::getPresetLabel(TimeZoneRegistry::clampPresetIndex(SETTINGS.timeZonePreset));
}

const char* getDateFormatLabel() {
  switch (static_cast<CrossPointSettings::DATE_FORMAT>(SETTINGS.dateFormat)) {
    case CrossPointSettings::DATE_MM_DD_YYYY:
      return tr(STR_DATE_FORMAT_MM_DD_YYYY);
    case CrossPointSettings::DATE_YYYY_MM_DD:
      return tr(STR_DATE_FORMAT_YYYY_MM_DD);
    case CrossPointSettings::DATE_DD_MM_YYYY:
    default:
      return tr(STR_DATE_FORMAT_DD_MM_YYYY);
  }
}

const char* getWifiChoiceLabel() {
  return SETTINGS.syncDayWifiChoice == CrossPointSettings::SYNC_DAY_WIFI_MANUAL ? tr(STR_MANUAL)
                                                                                : tr(STR_REFRESH_MODE_AUTO);
}

// Wrapped paragraph: measures the wrapped height, reserves it, draws it.
void addParagraph(UiAppHost::UiScreen& screen, const char* text, fui::TextStyle style, const int16_t gap) {
  const fui::Rect body = screen.body();
  if (body.empty()) return;
  const fui::Size size = fui::measureWrappedText(screen.target(), text, style, body.width);
  const fui::Rect rect = screen.takeTop(size.height, gap);
  screen.target().text(rect, text, style);
}
}  // namespace

void SyncDayActivity::onEnter() {
  UiListActivity::onEnter();
  TimeUtils::configureTimezone();
  wifiConnectedOnEnter = isWifiConnected();
  connectedInActivity = false;
  syncing = false;
  lastSyncSucceeded = false;
  lastSyncFailed = false;

  const StrId labels[ACTION_COUNT] = {StrId::STR_SYNC_NOW, StrId::STR_SET_DATE, StrId::STR_CHOOSE_WIFI,
                                      StrId::STR_TIME_ZONE, StrId::STR_DATE_FORMAT};
  const UIIcon icons[ACTION_COUNT] = {UIIcon::Wifi, UIIcon::Recent, UIIcon::Settings, UIIcon::Settings, UIIcon::Recent};
  for (int i = 0; i < ACTION_COUNT; ++i) {
    rowItems[i] = fui::ListItem{};
    rowItems[i].label = I18N.get(labels[i]);
    rowItems[i].icon = listIconFor(icons[i], 32);
    rowItems[i].actionValue = static_cast<int16_t>(i);
  }
  refreshRowValues();
}

void SyncDayActivity::onExit() {
  Activity::onExit();

  if (!wifiConnectedOnEnter && connectedInActivity) {
    wifiOff();
  }
}

// Assigns the live subtitle/value text into the row-owned strings.
void SyncDayActivity::refreshRowValues() {
  rowSubtitles[ROW_SYNC_NOW] = getObtainedDateLabel();
  rowSubtitles[ROW_SET_DATE] = tr(STR_MANUAL);
  rowSubtitles[ROW_WIFI_CHOICE] = getWifiChoiceLabel();
  rowSubtitles[ROW_TIME_ZONE] = getTimeZoneLabel();
  rowSubtitles[ROW_DATE_FORMAT] = getDateFormatLabel();
  networkStatus = isWifiConnected() ? tr(STR_CONNECTED) : tr(STR_NOT_CONNECTED);
  for (int i = 0; i < ACTION_COUNT; ++i) {
    rowItems[i].subtitle = rowSubtitles[i].c_str();
  }
  rowItems[ROW_SYNC_NOW].value = networkStatus.c_str();
}

void SyncDayActivity::activateIndex(const int index) {
  if (index < 0 || index >= ACTION_COUNT) return;
  nav.selected = index;
  if (index == ROW_SYNC_NOW) {
    app.clearTapFlash();
    const bool chooseWifiManually = SETTINGS.syncDayWifiChoice == CrossPointSettings::SYNC_DAY_WIFI_MANUAL;
    if (chooseWifiManually) {
      openWifiSelection(false);
    } else if (isWifiConnected()) {
      syncTime();
    } else {
      openWifiSelection(true);
    }
  } else if (index == ROW_SET_DATE) {
    app.clearTapFlash();
    openManualDateSelection();
  } else if (index == ROW_WIFI_CHOICE) {
    SETTINGS.syncDayWifiChoice = (SETTINGS.syncDayWifiChoice + 1) % CrossPointSettings::SYNC_DAY_WIFI_CHOICE_COUNT;
    SETTINGS.saveToFile();
    requestUpdate();
  } else if (index == ROW_TIME_ZONE) {
    app.clearTapFlash();
    openTimeZoneSelection();
  } else {
    SETTINGS.dateFormat = (SETTINGS.dateFormat + 1) % CrossPointSettings::DATE_FORMAT_COUNT;
    SETTINGS.saveToFile();
    requestUpdate();
  }
}

void SyncDayActivity::drawChrome() { HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_SYNC_DAY)); }

void SyncDayActivity::render(RenderLock&& lock) {
  if (syncing) {
    renderer.clearScreen();
    drawChrome();
    const int pageHeight = renderer.getScreenHeight();
    renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 20, tr(STR_SYNCING_TIME), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 8, tr(STR_SYNC_DAY_HINT));
    renderer.displayBuffer();
    return;
  }
  UiListActivity::render(std::move(lock));
}

void SyncDayActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // Values track live state (Wi-Fi status, settings changed in sub-screens).
  refreshRowValues();

  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(ACTION_COUNT);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  fui::TextStyle label = screen.theme().smallText;
  label.bold = true;
  props.labelText = label;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  // The list takes exactly its rows; the help text lives underneath.
  const int16_t rowHeight = props.rowHeight > 0 ? props.rowHeight : screen.theme().rowHeight;
  const int16_t rowGap = props.rowGap >= 0 ? props.rowGap : screen.theme().listRowGap;
  const int16_t listHeight = static_cast<int16_t>(rowHeight * ACTION_COUNT + rowGap * (ACTION_COUNT - 1));
  screen.list(props, listHeight);

  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  const int16_t sidePadding = static_cast<int16_t>(metrics.contentSidePadding);
  screen.insetContent(fui::Insets{0, sidePadding, 0, sidePadding});

  fui::TextStyle body = screen.theme().smallText;
  body.maxLines = 4;
  if (nav.selected == ROW_SYNC_NOW) {
    const std::string status = getStatusMessage();
    addParagraph(screen, status.c_str(), body, static_cast<int16_t>(screen.theme().spaceMd));
  }
  fui::TextStyle title = body;
  title.bold = true;
  addParagraph(screen, tr(STR_SYNC_DAY_INFO_TITLE), title, static_cast<int16_t>(screen.theme().spaceSm));
  addParagraph(screen, tr(STR_SYNC_DAY_INFO_1), body, static_cast<int16_t>(screen.theme().spaceXs));
  addParagraph(screen, tr(STR_SYNC_DAY_INFO_2), body, static_cast<int16_t>(screen.theme().spaceXs));
  addParagraph(screen, tr(STR_SYNC_DAY_INFO_3), body, 0);
}

bool SyncDayActivity::isWifiConnected() const { return WiFi.status() == WL_CONNECTED; }

std::string SyncDayActivity::getStatusMessage() const {
  if (lastSyncSucceeded) {
    return tr(STR_TIME_SYNCED);
  }

  if (lastSyncFailed) {
    return tr(STR_TIME_SYNC_FAILED);
  }

  const auto displayInfo = HeaderDateUtils::getDisplayDateInfo();
  if (displayInfo.usedFallback || !TimeUtils::isClockValid(displayInfo.timestamp)) {
    return tr(STR_SYNC_DAY_WIFI_HINT);
  }

  return tr(STR_SYNC_DAY_HINT);
}

void SyncDayActivity::openTimeZoneSelection() {
  startActivityForResult(std::make_unique<TimeZoneSelectActivity>(renderer, mappedInput),
                         [this](const ActivityResult&) { requestUpdate(); });
}

void SyncDayActivity::openManualDateSelection() {
  const uint32_t previousValidTimestamp = APP_STATE.lastKnownValidTimestamp;
  startActivityForResult(std::make_unique<ManualDateActivity>(renderer, mappedInput),
                         [this, previousValidTimestamp](const ActivityResult&) {
                           if (APP_STATE.lastKnownValidTimestamp != previousValidTimestamp) {
                             createDueReadingStatsBackupWithFeedback();
                           }
                           requestUpdate();
                         });
}

void SyncDayActivity::openWifiSelection(const bool allowAutoConnect) {
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, allowAutoConnect),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled || !isWifiConnected()) {
                             requestUpdate();
                             return;
                           }

                           if (!wifiConnectedOnEnter) {
                             connectedInActivity = true;
                           }
                           syncTime();
                         });
}

void SyncDayActivity::syncTime() {
  syncing = true;
  lastSyncSucceeded = false;
  lastSyncFailed = false;
  requestUpdate(true);

  const bool hadValidTimeBefore = TimeUtils::isClockValid();
  const bool ntpSuccess = TimeUtils::syncTimeWithNtp();
  const uint32_t currentValidTimestamp = TimeUtils::getCurrentValidTimestamp();
  const bool effectiveSuccess = ntpSuccess || (!hadValidTimeBefore && currentValidTimestamp > 0);
  if (effectiveSuccess && currentValidTimestamp > 0) {
    APP_STATE.registerValidTimeSync(currentValidTimestamp);
    APP_STATE.saveToFile();
  }

  syncing = false;
  lastSyncSucceeded = effectiveSuccess;
  lastSyncFailed = !effectiveSuccess;
  requestUpdate(true);
  if (effectiveSuccess) {
    createDueReadingStatsBackupWithFeedback();
    requestUpdate(true);
  }
}

void SyncDayActivity::showTransientPopup(const char* message, const int progress, const unsigned long delayMs) {
  requestUpdateAndWait();

  {
    RenderLock lock(*this);
    const Rect popupRect = GUI.drawPopup(renderer, message);
    if (progress >= 0) {
      GUI.fillPopupProgress(renderer, popupRect, progress);
    }
  }

  if (delayMs > 0) {
    delay(delayMs);
  }
}

void SyncDayActivity::createDueReadingStatsBackupWithFeedback() {
  if (!READING_STATS.isAutoBackupDue()) {
    return;
  }

  showTransientPopup(tr(STR_READING_STATS_BACKUP_RUNNING), 20, 120);
  const bool backupReady = READING_STATS.createDueAutoBackup();
  showTransientPopup(backupReady ? tr(STR_READING_STATS_BACKUP_DONE) : tr(STR_READING_STATS_BACKUP_PENDING),
                     backupReady ? 100 : -1, backupReady ? 350 : 700);
}
