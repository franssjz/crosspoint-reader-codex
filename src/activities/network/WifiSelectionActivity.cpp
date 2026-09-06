#include "WifiSelectionActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <MemoryBudget.h>
#include <WiFi.h>
#include <esp_mac.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SdCardFontGlobals.h"
#include "WifiCredentialStore.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TimeUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;
constexpr fui::ActionId ACTION_SCAN = 2;
constexpr fui::ActionId ACTION_PROMPT = 3;
constexpr size_t WIFI_BSSID_LEN = 6;

bool hasBssidBytes(const uint8_t bssid[WIFI_BSSID_LEN]) {
  return std::any_of(bssid, bssid + WIFI_BSSID_LEN, [](const uint8_t part) { return part != 0; });
}
}  // namespace

WifiSelectionActivity::WifiSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const bool autoConnect, const bool syncRtcOnConnect,
                                             const bool autoConnectOnly)
    : Activity("WifiSelection", renderer, mappedInput),
      UiAppHost(renderer),
      allowAutoConnect(autoConnect),
      autoConnectOnly(autoConnectOnly),
      syncRtcOnConnect(syncRtcOnConnect) {}

void WifiSelectionActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<WifiSelectionActivity*>(user);
  if (self->state != WifiSelectionState::NETWORK_LIST) return;
  if (event.value < 0 || event.value >= static_cast<int16_t>(self->networks.size())) return;
  self->selectedNetworkIndex = static_cast<size_t>(event.value);
  // Long-press a saved network to forget it (mirrors the Left-button hold in loop()).
  if (event.longPress) {
    if (self->networks[self->selectedNetworkIndex].hasSavedPassword) {
      self->selectedSSID = self->networks[self->selectedNetworkIndex].ssid;
      self->state = WifiSelectionState::FORGET_PROMPT;
      self->forgetPromptSelection = 0;  // Default to "Cancel"
      self->app.clearTapFlash();
      self->requestUpdate();
    }
    return;
  }
  // Selection leaves this screen (password entry / connecting); a lingering
  // flash would gray an unrelated row.
  self->app.clearTapFlash();
  self->selectNetwork(static_cast<int>(self->selectedNetworkIndex));
}

void WifiSelectionActivity::onScanEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<WifiSelectionActivity*>(user);
  if (self->state != WifiSelectionState::NETWORK_LIST) return;
  self->app.clearTapFlash();  // the scan screen replaces this one
  self->startWifiScan();
}

void WifiSelectionActivity::onPromptEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<WifiSelectionActivity*>(user);
  if (self->state == WifiSelectionState::SAVE_PROMPT) {
    self->savePromptSelection = event.value;
    self->app.clearTapFlash();  // the action leaves this screen
    if (self->savePromptSelection == 0) {
      RenderLock lock(*self);
      WIFI_STORE.addCredential(self->selectedSSID, self->enteredPassword);
    }
    self->onComplete(true);
    return;
  }
  if (self->state == WifiSelectionState::FORGET_PROMPT) {
    self->forgetPromptSelection = event.value;
    self->app.clearTapFlash();  // the action leaves this screen
    if (self->forgetPromptSelection == 1) {
      RenderLock lock(*self);
      WIFI_STORE.removeCredential(self->selectedSSID);
      const auto network = find_if(self->networks.begin(), self->networks.end(),
                                   [self](const WifiNetworkInfo& net) { return net.ssid == self->selectedSSID; });
      if (network != self->networks.end()) {
        network->hasSavedPassword = false;
      }
      self->rebuildNetworkRowItems();
    }
    // Cancel or Forget: back to the cached list, keeping scan results and cursor.
    self->returnToNetworkList();
  }
}

void WifiSelectionActivity::onEnter() {
  Activity::onEnter();

  // WiFi startup needs several contiguous driver buffers. Release both the
  // active SD font and its catalog before the radio starts allocating them.
  const auto heapBeforeFontRelease = MemoryBudget::snapshot();
  const bool releasedSdFont = sdFontSystem.releaseForNetwork(renderer);
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->releaseSdFontCaches();
  }
  const auto heapAfterFontRelease = MemoryBudget::snapshot();
  LOG_DBG("WIFI", "SD font network trim released=%d free=%u->%u delta=%ld maxAlloc=%u->%u delta=%ld", releasedSdFont,
          heapBeforeFontRelease.freeHeap, heapAfterFontRelease.freeHeap,
          static_cast<int32_t>(heapAfterFontRelease.freeHeap) - static_cast<int32_t>(heapBeforeFontRelease.freeHeap),
          heapBeforeFontRelease.maxAllocHeap, heapAfterFontRelease.maxAllocHeap,
          static_cast<int32_t>(heapAfterFontRelease.maxAllocHeap) -
              static_cast<int32_t>(heapBeforeFontRelease.maxAllocHeap));

  // Load saved WiFi credentials - SD card operations need lock as we use SPI
  // for both
  {
    RenderLock lock(*this);
    WIFI_STORE.loadFromFile();
  }

  if (allowAutoConnect && autoConnectOnly && !WIFI_STORE.hasCredentials()) {
    LOG_DBG("WIFI", "Auto-connect only requested with no saved credentials");
    onComplete(false);
    return;
  }

  // Reset state
  selectedNetworkIndex = 0;
  networks.clear();
  networkStatuses.clear();
  networkRowItems.clear();
  realNetworkCount = 0;
  state = WifiSelectionState::SCANNING;
  selectedSSID.clear();
  selectedRequiresPassword = false;
  selectedChannel = 0;
  selectedHasBssid = false;
  std::memset(selectedBssid, 0, sizeof(selectedBssid));
  connectedIP.clear();
  connectionError.clear();
  enteredPassword.clear();
  usedSavedPassword = false;
  savePromptSelection = 0;
  forgetPromptSelection = 0;
  autoConnecting = false;
  manualNetworkListRequested = false;
  autoAttemptedSsids.clear();
  const size_t savedCredentialCount = WIFI_STORE.getCredentialCount();
  autoAttemptedSsids.reserve(savedCredentialCount);

  // Read the hardware-derived station MAC directly. WiFi.macAddress() depends
  // on the STA netif already existing, but this screen is entered while WiFi
  // is often still off (notably after an X4 Pro WiFi session).
  uint8_t mac[6] = {};
  char macStr[64];
  const esp_err_t macResult = esp_read_mac(mac, ESP_MAC_WIFI_STA);
  if (macResult == ESP_OK) {
    snprintf(macStr, sizeof(macStr), "%s %02x-%02x-%02x-%02x-%02x-%02x", tr(STR_MAC_ADDRESS), mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);
  } else {
    LOG_ERR("WIFI", "Failed to read station MAC (err=%d)", static_cast<int>(macResult));
    snprintf(macStr, sizeof(macStr), "%s --", tr(STR_MAC_ADDRESS));
  }
  cachedMacAddress = std::string(macStr);

  listNav.reset();
  resetUi();
  app.on(ACTION_ROW, &WifiSelectionActivity::onRowEvent, this);
  app.on(ACTION_SCAN, &WifiSelectionActivity::onScanEvent, this);
  app.on(ACTION_PROMPT, &WifiSelectionActivity::onPromptEvent, this);
  app.setScreen(&WifiSelectionActivity::listScreen, this);

  // Trigger first update to show scanning message
  requestUpdate();

  // Auto mode always scans first so we only attempt remembered networks that
  // are actually in range (last connected SSID preferred, then strongest saved
  // network; see tryNextSavedNetworkFromScan). The user can interrupt this and
  // show the scan result.
  if (allowAutoConnect && savedCredentialCount != 0) {
    startWifiScan(true);
    return;
  }

  // Fallback to scanning
  startWifiScan();
}

void WifiSelectionActivity::onExit() {
  Activity::onExit();

  LOG_DBG("WIFI", "Free heap at onExit start: %d bytes", ESP.getFreeHeap());

  // Stop any ongoing WiFi scan
  LOG_DBG("WIFI", "Deleting WiFi scan...");
  WiFi.scanDelete();
  LOG_DBG("WIFI", "Free heap after scanDelete: %d bytes", ESP.getFreeHeap());

  // Note: We do NOT disconnect WiFi here - the parent activity
  // (CrossPointWebServerActivity) manages WiFi connection state. We just clean
  // up the scan and task.

  LOG_DBG("WIFI", "Free heap at onExit end: %d bytes", ESP.getFreeHeap());
}

void WifiSelectionActivity::startWifiScan(const bool autoScan) {
  autoConnecting = autoScan;
  manualNetworkListRequested = false;
  listNav.reset();
  state = WifiSelectionState::SCANNING;
  networks.clear();
  // The cached rows point into the cleared networks' strings; drop them too.
  networkStatuses.clear();
  networkRowItems.clear();
  requestUpdate();

  // Set WiFi mode to station
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect();
  delay(100);

  // Start async scan
  WiFi.scanNetworks(true);  // true = async scan
}

void WifiSelectionActivity::returnToNetworkList() {
  // Back out of a prompt or a failure screen without rescanning. The cached
  // scan results are still valid, so reusing them keeps the user's place
  // instead of dropping them into another multi-second scan.
  autoConnecting = false;
  manualNetworkListRequested = false;
  state = WifiSelectionState::NETWORK_LIST;

  // Keep the network we were acting on under the cursor. A hidden network the
  // user typed by hand is not in the scan list, so the cursor simply stays on
  // the placeholder row it was launched from.
  if (!selectedSSID.empty()) {
    const auto selected = std::find_if(networks.begin(), networks.end(), [this](const WifiNetworkInfo& network) {
      return !network.isHiddenPlaceholder && network.ssid == selectedSSID;
    });
    if (selected != networks.end()) {
      selectedNetworkIndex = static_cast<size_t>(std::distance(networks.begin(), selected));
    }
  }
  if (selectedNetworkIndex >= networks.size()) {
    selectedNetworkIndex = 0;
  }
  listNav.selected = static_cast<int>(selectedNetworkIndex);
  listNav.follow(static_cast<int>(networks.size()));

  requestUpdate();
}

void WifiSelectionActivity::processWifiScanResults() {
  const int16_t scanResult = WiFi.scanComplete();

  if (scanResult == WIFI_SCAN_RUNNING) {
    // Scan still in progress
    return;
  }

  if (scanResult == WIFI_SCAN_FAILED) {
    networks.clear();
    realNetworkCount = 0;
    if (allowAutoConnect && autoConnectOnly) {
      LOG_DBG("WIFI", "Auto-connect only requested but WiFi scan failed");
      onComplete(false);
      return;
    }
    appendHiddenNetworkEntry();
    rebuildNetworkRowItems();
    autoConnecting = false;
    state = WifiSelectionState::NETWORK_LIST;
    selectedNetworkIndex = 0;
    requestUpdate();
    return;
  }

  // Scan complete, process results — deduplicate in-place, keeping strongest signal
  networks.clear();
  networks.reserve(scanResult);

  for (int i = 0; i < scanResult; i++) {
    char ssid[33];
    strlcpy(ssid, WiFi.SSID(i).c_str(), sizeof(ssid));
    const int32_t rssi = WiFi.RSSI(i);

    // Skip hidden networks (empty SSID)
    if (ssid[0] == '\0') {
      continue;
    }

    auto it =
        std::find_if(networks.begin(), networks.end(), [&ssid](const WifiNetworkInfo& n) { return n.ssid == ssid; });
    if (it == networks.end()) {
      WifiNetworkInfo network;
      network.ssid = ssid;
      network.rssi = rssi;
      network.isEncrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
      network.hasSavedPassword = WIFI_STORE.hasSavedCredential(network.ssid);
      network.channel = WiFi.channel(i);
      WiFi.BSSID(i, network.bssid);
      network.hasBssid = network.channel > 0 && hasBssidBytes(network.bssid);
      networks.push_back(std::move(network));
    } else if (rssi > it->rssi) {
      // Stronger AP for the same SSID: remember it as the connect target.
      it->rssi = rssi;
      it->isEncrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
      it->channel = WiFi.channel(i);
      WiFi.BSSID(i, it->bssid);
      it->hasBssid = it->channel > 0 && hasBssidBytes(it->bssid);
    }
  }

  // Sort: saved-password networks first, then by signal strength (strongest first)
  std::sort(networks.begin(), networks.end(), [](const WifiNetworkInfo& a, const WifiNetworkInfo& b) {
    if (a.hasSavedPassword != b.hasSavedPassword) {
      return a.hasSavedPassword;
    }
    return a.rssi > b.rssi;
  });

  realNetworkCount = networks.size();
  appendHiddenNetworkEntry();
  rebuildNetworkRowItems();

  WiFi.scanDelete();

  if (autoConnecting && !manualNetworkListRequested && tryNextSavedNetworkFromScan()) {
    return;
  }

  if (allowAutoConnect && autoConnectOnly) {
    LOG_DBG("WIFI", "Auto-connect only found no saved network in range");
    onComplete(false);
    return;
  }

  autoConnecting = false;
  state = WifiSelectionState::NETWORK_LIST;
  selectedNetworkIndex = 0;
  requestUpdate();
}

void WifiSelectionActivity::appendHiddenNetworkEntry() {
  // Synthetic list entry that lets the user type an SSID that is not broadcast.
  // ESP32 can join hidden APs as long as the SSID is supplied to WiFi.begin().
  WifiNetworkInfo placeholder;
  placeholder.rssi = 0;
  placeholder.isEncrypted = true;  // Treated as encrypted; an empty password still connects open APs
  placeholder.hasSavedPassword = false;
  placeholder.isHiddenPlaceholder = true;
  networks.push_back(std::move(placeholder));
}

// Derives networkStatuses/networkRowItems from `networks`. Called whenever
// `networks` changes (both branches of processWifiScanResults()) so
// buildListScreen() reuses the cached rows on every repaint instead of
// re-deriving a "+ * ||||" status string per network each time.
void WifiSelectionActivity::rebuildNetworkRowItems() {
  networkStatuses.assign(networks.size(), std::string());
  networkRowItems.clear();
  networkRowItems.reserve(networks.size());
  for (size_t i = 0; i < networks.size(); i++) {
    const auto& network = networks[i];
    if (!network.isHiddenPlaceholder) {
      networkStatuses[i] = std::string(network.hasSavedPassword ? "+ " : "") + (network.isEncrypted ? "* " : "") +
                           getSignalStrengthIndicator(network.rssi);
    }
    fui::ListItem item;
    item.label = network.isHiddenPlaceholder ? tr(STR_ADD_HIDDEN_NETWORK) : network.ssid.c_str();
    if (!networkStatuses[i].empty()) item.value = networkStatuses[i].c_str();
    item.actionValue = static_cast<int16_t>(i);
    networkRowItems.push_back(item);
  }
}

void WifiSelectionActivity::selectNetwork(const int index) {
  if (index < 0 || index >= static_cast<int>(networks.size())) {
    return;
  }

  const auto& network = networks[index];

  // Synthetic "Add hidden network..." entry: prompt the user to type the SSID first
  if (network.isHiddenPlaceholder) {
    promptHiddenSsid();
    return;
  }

  setSelectedNetwork(network);
  usedSavedPassword = false;
  enteredPassword.clear();
  autoConnecting = false;

  // Saved credential (including saved open networks): connect directly
  if (connectUsingSavedCredential(network, false)) {
    return;
  }

  if (selectedRequiresPassword) {
    promptPasswordEntry();
  } else {
    // Connect directly for open networks
    attemptConnection();
  }
}

void WifiSelectionActivity::promptPasswordEntry() {
  // Show password entry
  state = WifiSelectionState::PASSWORD_ENTRY;
  // Don't allow screen updates while changing activity
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_ENTER_WIFI_PASSWORD),
                                                                 "",  // No initial text
                                                                 64,  // Max password length
                                                                 InputType::Text),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             state = WifiSelectionState::NETWORK_LIST;
                           } else {
                             enteredPassword = std::get<KeyboardResult>(result.data).text;
                             // state will be updated in next loop iteration
                           }
                         });
}

void WifiSelectionActivity::promptHiddenSsid() {
  selectedSSID.clear();
  selectedRequiresPassword = true;  // Hidden networks are usually encrypted; empty password still joins open APs
  selectedChannel = 0;
  selectedHasBssid = false;
  std::memset(selectedBssid, 0, sizeof(selectedBssid));
  usedSavedPassword = false;
  enteredPassword.clear();
  autoConnecting = false;

  // Suppress rendering during the activity transition (see render()).
  state = WifiSelectionState::HIDDEN_SSID_ENTRY;
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_ENTER_WIFI_SSID),
                                                                 "",  // No initial text
                                                                 32,  // Max SSID length (IEEE 802.11: 32 bytes)
                                                                 InputType::Text),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             state = WifiSelectionState::NETWORK_LIST;
                             return;
                           }
                           selectedSSID = std::get<KeyboardResult>(result.data).text;
                           if (selectedSSID.empty()) {
                             state = WifiSelectionState::NETWORK_LIST;
                           }
                           // Otherwise stay in HIDDEN_SSID_ENTRY; loop() continues the flow.
                         });
}

void WifiSelectionActivity::setSelectedNetwork(const WifiNetworkInfo& network) {
  selectedSSID = network.ssid;
  selectedRequiresPassword = network.isEncrypted;
  selectedChannel = network.channel;
  selectedHasBssid = network.hasBssid;
  std::memset(selectedBssid, 0, sizeof(selectedBssid));
  if (selectedHasBssid) {
    std::memcpy(selectedBssid, network.bssid, sizeof(selectedBssid));
  }
}

bool WifiSelectionActivity::connectUsingSavedCredential(const WifiNetworkInfo& network,
                                                        const bool isAutoConnectAttempt) {
  const auto savedCred = WIFI_STORE.findCredential(network.ssid);
  if (!savedCred || (network.isEncrypted && savedCred->password.empty())) {
    return false;
  }

  setSelectedNetwork(network);
  enteredPassword = savedCred->password;
  usedSavedPassword = true;
  autoConnecting = isAutoConnectAttempt;
  LOG_DBG("WiFi", "Using saved credential for %s, password length: %zu", selectedSSID.c_str(), enteredPassword.size());
  attemptConnection();
  return true;
}

bool WifiSelectionActivity::hasAttemptedAutoSsid(const std::string& ssid) const {
  return std::find(autoAttemptedSsids.begin(), autoAttemptedSsids.end(), ssid) != autoAttemptedSsids.end();
}

bool WifiSelectionActivity::tryAutoConnectCredential(const WifiCredential& cred) {
  if (hasAttemptedAutoSsid(cred.ssid)) {
    return false;
  }

  LOG_DBG("WIFI", "Attempting saved network: %s", cred.ssid.c_str());
  autoAttemptedSsids.push_back(cred.ssid);
  // Pin the strongest scanned AP for this SSID when we have one.
  const auto scanned = std::find_if(networks.begin(), networks.end(), [&cred](const WifiNetworkInfo& network) {
    return !network.isHiddenPlaceholder && network.ssid == cred.ssid;
  });
  if (scanned != networks.end()) {
    selectedNetworkIndex = static_cast<size_t>(std::distance(networks.begin(), scanned));
    setSelectedNetwork(*scanned);
  } else {
    selectedSSID = cred.ssid;
    selectedChannel = 0;
    selectedHasBssid = false;
    std::memset(selectedBssid, 0, sizeof(selectedBssid));
  }
  enteredPassword = cred.password;
  selectedRequiresPassword = !cred.password.empty();
  usedSavedPassword = true;
  autoConnecting = true;
  manualNetworkListRequested = false;
  attemptConnection();
  requestUpdate();
  return true;
}

bool WifiSelectionActivity::tryNextSavedNetworkFromScan() {
  // Prefer the last connected network when it is in range and saved; the list
  // is sorted saved-first / strongest-first, so the loop below then falls back
  // to the strongest remaining saved network.
  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  if (!lastSsid.empty() && !hasAttemptedAutoSsid(lastSsid)) {
    const auto remembered = std::find_if(networks.begin(), networks.end(), [&lastSsid](const WifiNetworkInfo& n) {
      return !n.isHiddenPlaceholder && n.hasSavedPassword && n.ssid == lastSsid;
    });
    if (remembered != networks.end()) {
      const auto cred = WIFI_STORE.findCredential(remembered->ssid);
      if (cred && tryAutoConnectCredential(*cred)) {
        return true;
      }
    }
  }

  for (const auto& network : networks) {
    if (network.isHiddenPlaceholder || !network.hasSavedPassword || hasAttemptedAutoSsid(network.ssid)) {
      continue;
    }

    const auto cred = WIFI_STORE.findCredential(network.ssid);
    if (cred && tryAutoConnectCredential(*cred)) {
      return true;
    }
  }
  return false;
}

void WifiSelectionActivity::handleAutoConnectFailure() {
  LOG_DBG("WIFI", "Saved network failed: %s", selectedSSID.c_str());
  // Stop the SDK from retrying in the background while we move on.
  WiFi.disconnect();

  if (!networks.empty()) {
    if (tryNextSavedNetworkFromScan()) {
      return;
    }
    if (autoConnectOnly) {
      LOG_DBG("WIFI", "Auto-connect only: no saved network connected");
      onComplete(false);
      return;
    }
    autoConnecting = false;
    state = WifiSelectionState::NETWORK_LIST;
    selectedNetworkIndex = 0;
    requestUpdate();
    return;
  }

  if (autoConnectOnly) {
    onComplete(false);
    return;
  }
  startWifiScan(true);
}

void WifiSelectionActivity::showNetworkListFromAutoConnect() {
  LOG_DBG("WIFI", "User requested manual network list");
  WiFi.disconnect();
  autoConnecting = false;
  manualNetworkListRequested = true;

  if (networks.empty()) {
    startWifiScan(false);
    manualNetworkListRequested = true;
    return;
  }

  state = WifiSelectionState::NETWORK_LIST;
  selectedNetworkIndex = 0;
  requestUpdate();
}

void WifiSelectionActivity::attemptConnection() {
  state = autoConnecting ? WifiSelectionState::AUTO_CONNECTING : WifiSelectionState::CONNECTING;
  connectionStartTime = millis();
  connectedIP.clear();
  connectionError.clear();
  requestUpdate();

  WiFi.persistent(false);  // Credentials are managed by WifiCredentialStore; suppress SDK NVS auto-connect
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);  // Abort any in-progress SDK auto-connect and clear NVS-saved SSID
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  // Scan all channels so networks with multiple APs use the strongest matching
  // BSSID instead of the first match found by the framework's default fast scan.
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  // Set hostname so routers show "CrossPoint-Reader-AABBCCDDEEFF" instead of "esp32-XXXXXXXXXXXX"
  uint8_t mac[6] = {};
  const esp_err_t macResult = esp_read_mac(mac, ESP_MAC_WIFI_STA);
  if (macResult == ESP_OK) {
    char hostname[sizeof("CrossPoint-Reader-") + 12];
    snprintf(hostname, sizeof(hostname), "CrossPoint-Reader-%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5]);
    WiFi.setHostname(hostname);
  } else {
    LOG_ERR("WIFI", "Failed to read station MAC for hostname (err=%d)", static_cast<int>(macResult));
  }

  const char* password = (selectedRequiresPassword && !enteredPassword.empty()) ? enteredPassword.c_str() : nullptr;
  if (selectedHasBssid && selectedChannel > 0) {
    // Join the exact AP we scanned (strongest BSSID/channel) instead of letting
    // the framework pick among same-SSID APs and time out on a weak one (#85).
    LOG_DBG("WIFI", "Connecting to %s on channel %d via BSSID %02x:%02x:%02x:%02x:%02x:%02x", selectedSSID.c_str(),
            static_cast<int>(selectedChannel), selectedBssid[0], selectedBssid[1], selectedBssid[2], selectedBssid[3],
            selectedBssid[4], selectedBssid[5]);
    WiFi.begin(selectedSSID.c_str(), password, selectedChannel, selectedBssid);
  } else if (password != nullptr) {
    WiFi.begin(selectedSSID.c_str(), password);
  } else {
    WiFi.begin(selectedSSID.c_str());
  }
}

void WifiSelectionActivity::checkConnectionStatus() {
  if (state != WifiSelectionState::CONNECTING && state != WifiSelectionState::AUTO_CONNECTING) {
    return;
  }

  const wl_status_t status = WiFi.status();

  if (status == WL_CONNECTED) {
    // Successfully connected
    IPAddress ip = WiFi.localIP();
    char ipStr[16];
    snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    connectedIP = ipStr;
    autoConnecting = false;

#if defined(ENABLE_SERIAL_LOG) && LOG_LEVEL >= 2
    uint8_t connectedBssid[6] = {};
    WiFi.BSSID(connectedBssid);
    LOG_DBG("WIFI", "Connected BSSID: %02x:%02x:%02x:%02x:%02x:%02x, channel: %d, RSSI: %d dBm",
            static_cast<unsigned>(connectedBssid[0]), static_cast<unsigned>(connectedBssid[1]),
            static_cast<unsigned>(connectedBssid[2]), static_cast<unsigned>(connectedBssid[3]),
            static_cast<unsigned>(connectedBssid[4]), static_cast<unsigned>(connectedBssid[5]), WiFi.channel(),
            WiFi.RSSI());
#endif

    // Sync RTC from NTP on the first successful WiFi connection only. The DS3231
    // drifts ~2 ppm so one sync is enough; users can force a re-sync from
    // Settings > Customise Status Bar > Sync clock now.
    if (syncRtcOnConnect && halClock.isAvailable() && !SETTINGS.clockHasBeenSynced) {
      if (halClock.syncFromNTP()) {
        SETTINGS.clockHasBeenSynced = 1;
        SETTINGS.saveToFile();
        TimeUtils::applySystemClockFromRtc(true);
      }
    }

    // Save this as the last connected network - SD card operations need lock as
    // we use SPI for both
    {
      RenderLock lock(*this);
      WIFI_STORE.setLastConnectedSsid(selectedSSID);
    }

    // If we entered a new password, ask if user wants to save it
    // Otherwise, immediately complete so parent can start web server
    if (!usedSavedPassword && !enteredPassword.empty()) {
      state = WifiSelectionState::SAVE_PROMPT;
      savePromptSelection = 0;  // Default to "Yes"
      requestUpdate();
    } else {
      // Using saved password or open network - complete immediately
      LOG_DBG("WIFI",
              "Connected with saved/open credentials, "
              "completing immediately");
      onComplete(true);
    }
    return;
  }

  if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) {
    connectionError = tr(STR_ERROR_GENERAL_FAILURE);
    if (status == WL_NO_SSID_AVAIL) {
      connectionError = tr(STR_ERROR_NETWORK_NOT_FOUND);
    }
    if (autoConnecting) {
      handleAutoConnectFailure();
      return;
    }
    // Stop the SDK from retrying in the background while the user is back in
    // the list; the timeout path below does the same.
    WiFi.disconnect();
    state = WifiSelectionState::CONNECTION_FAILED;
    requestUpdate();
    return;
  }

  // Check for timeout
  const unsigned long timeoutMs = autoConnecting ? AUTO_CONNECTION_TIMEOUT_MS : CONNECTION_TIMEOUT_MS;
  if (millis() - connectionStartTime > timeoutMs) {
    LOG_ERR("WIFI", "Connection timeout for %s, status=%d, channel=%d, bssid=%d", selectedSSID.c_str(),
            static_cast<int>(status), static_cast<int>(selectedChannel), selectedHasBssid ? 1 : 0);
    WiFi.disconnect();
    connectionError = tr(STR_ERROR_CONNECTION_TIMEOUT);
    if (autoConnecting) {
      handleAutoConnectFailure();
      return;
    }
    state = WifiSelectionState::CONNECTION_FAILED;
    requestUpdate();
    return;
  }
}

void WifiSelectionActivity::loop() {
  // Check scan progress
  if (state == WifiSelectionState::SCANNING) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      WiFi.scanDelete();
      onComplete(false);
      return;
    }
    if (autoConnecting && mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      autoConnecting = false;
      manualNetworkListRequested = true;
      requestUpdate();
    }
    processWifiScanResults();
    return;
  }

  // Check connection progress
  if (state == WifiSelectionState::CONNECTING || state == WifiSelectionState::AUTO_CONNECTING) {
    if (state == WifiSelectionState::AUTO_CONNECTING) {
      if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        WiFi.disconnect();
        onComplete(false);
        return;
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        showNetworkListFromAutoConnect();
        return;
      }
    }
    checkConnectionStatus();
    return;
  }

  // Reached once the hidden-network SSID has been entered (and was non-empty).
  if (state == WifiSelectionState::HIDDEN_SSID_ENTRY) {
    WifiNetworkInfo hidden;
    hidden.ssid = selectedSSID;
    hidden.isEncrypted = true;
    hidden.hasSavedPassword = WIFI_STORE.hasSavedCredential(selectedSSID);
    // We may already know this hidden network - connect with the saved password
    if (connectUsingSavedCredential(hidden, false)) {
      return;
    }
    // Prompt for the password (empty password connects to open hidden APs)
    promptPasswordEntry();
    return;
  }

  if (state == WifiSelectionState::PASSWORD_ENTRY) {
    // Reach here once password entry finished in subactivity
    attemptConnection();
    return;
  }

  // Handle save prompt state
  if (state == WifiSelectionState::SAVE_PROMPT) {
    // Touch goes through the FreeInkApp: render() registered the dialog
    // button hit rects; route the snapshot and let onPromptEvent dispatch.
    const auto route = routeTouch(mappedInput);
    if (route.routed && app.invalidated()) requestUpdate();
    if (route) return;  // dispatched to onPromptEvent

    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (savePromptSelection > 0) {
        savePromptSelection--;
        requestUpdate();
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
               mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (savePromptSelection < 1) {
        savePromptSelection++;
        requestUpdate();
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (savePromptSelection == 0) {
        // User chose "Yes" - save the password
        RenderLock lock(*this);
        WIFI_STORE.addCredential(selectedSSID, enteredPassword);
      }
      // Complete - parent will start web server
      onComplete(true);
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      // Skip saving, complete anyway
      onComplete(true);
    }
    return;
  }

  // Handle forget prompt state (connection failed with saved credentials)
  if (state == WifiSelectionState::FORGET_PROMPT) {
    // Touch goes through the FreeInkApp: render() registered the dialog
    // button hit rects; route the snapshot and let onPromptEvent dispatch.
    const auto route = routeTouch(mappedInput);
    if (route.routed && app.invalidated()) requestUpdate();
    if (route) return;  // dispatched to onPromptEvent

    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (forgetPromptSelection > 0) {
        forgetPromptSelection--;
        requestUpdate();
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
               mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (forgetPromptSelection < 1) {
        forgetPromptSelection++;
        requestUpdate();
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (forgetPromptSelection == 1) {
        RenderLock lock(*this);
        // User chose "Forget network" - forget the network
        WIFI_STORE.removeCredential(selectedSSID);
        // Update the network list to reflect the change
        const auto network = find_if(networks.begin(), networks.end(),
                                     [this](const WifiNetworkInfo& net) { return net.ssid == selectedSSID; });
        if (network != networks.end()) {
          network->hasSavedPassword = false;
        }
        rebuildNetworkRowItems();
      }
      // Go back to the cached network list (whether Cancel or Forget network was selected)
      returnToNetworkList();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      // Skip forgetting, go back to network list
      returnToNetworkList();
    }
    return;
  }

  // Handle connected state (should not normally be reached - connection
  // completes immediately)
  if (state == WifiSelectionState::CONNECTED) {
    // Safety fallback - immediately complete
    onComplete(true);
    return;
  }

  // Handle connection failed state
  if (state == WifiSelectionState::CONNECTION_FAILED) {
    // Back always dismisses straight to the network list (#170). Only Confirm
    // (or a tap) opts in to the forget prompt, and only when a saved credential
    // is what failed.
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      returnToNetworkList();
      return;
    }
    int tapX = 0;
    int tapY = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(tapX, tapY)) {
      if (usedSavedPassword) {
        autoConnecting = false;
        state = WifiSelectionState::FORGET_PROMPT;
        forgetPromptSelection = 0;  // Default to "Cancel"
        requestUpdate();
      } else {
        returnToNetworkList();
      }
      return;
    }
    return;
  }

  // Handle network list state
  if (state == WifiSelectionState::NETWORK_LIST) {
    if (manualNetworkListRequested) {
      if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
        manualNetworkListRequested = false;
      }
      return;
    }

    // Check for Back button to exit (cancel)
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      onComplete(false);
      return;
    }

    // Check for Confirm button to select network or rescan
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (!networks.empty()) {
        selectNetwork(selectedNetworkIndex);
      } else {
        startWifiScan();
      }
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      startWifiScan();
      return;
    }

    const bool leftPressed = mappedInput.wasPressed(MappedInputManager::Button::Left);
    if (leftPressed) {
      const bool hasSavedPassword = !networks.empty() && networks[selectedNetworkIndex].hasSavedPassword;
      if (hasSavedPassword) {
        selectedSSID = networks[selectedNetworkIndex].ssid;
        state = WifiSelectionState::FORGET_PROMPT;
        forgetPromptSelection = 0;  // Default to "Cancel"
        requestUpdate();
        return;
      }
    }

    // Touch goes through the FreeInkApp: render() registered the row hit
    // rects; route the snapshot and let onRowEvent dispatch. Long-press on a
    // network row fires "forget" while the finger is down.
    const auto route = routeTouch(mappedInput, /*withLongPress=*/true);
    if (route.routed && app.invalidated()) requestUpdate();
    if (route) return;  // dispatched to onRowEvent

    if (!networks.empty()) {
      // Swipes scroll the viewport; the selection stays put and button
      // navigation pulls the view back to it.
      const auto swipe = mappedInput.wasSwipe();
      if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
        const int delta = swipe == MappedInputManager::SwipeDir::Up ? listNav.visibleRows : -listNav.visibleRows;
        if (listNav.scrollBy(delta, static_cast<int>(networks.size()))) requestUpdate();
        return;
      }
    }

    const auto moveSelection = [this](const int index) {
      selectedNetworkIndex = static_cast<size_t>(index);
      listNav.selected = index;
      listNav.follow(static_cast<int>(networks.size()));
      requestUpdate();
    };
    buttonNavigator.onNext(
        [this, &moveSelection] { moveSelection(ButtonNavigator::nextIndex(selectedNetworkIndex, networks.size())); });
    buttonNavigator.onPrevious([this, &moveSelection] {
      moveSelection(ButtonNavigator::previousIndex(selectedNetworkIndex, networks.size()));
    });
  }
}

std::string WifiSelectionActivity::getSignalStrengthIndicator(const int32_t rssi) const {
  // Convert RSSI to signal bars representation
  if (rssi >= -50) {
    return "||||";  // Excellent
  }
  if (rssi >= -60) {
    return " |||";  // Good
  }
  if (rssi >= -70) {
    return "  ||";  // Fair
  }
  return "   |";  // Very weak
}

void WifiSelectionActivity::render(RenderLock&&) {
  // Don't render if we're in a keyboard-entry state - we're just transitioning
  // from the keyboard subactivity back to the main activity
  if (state == WifiSelectionState::PASSWORD_ENTRY || state == WifiSelectionState::HIDDEN_SSID_ENTRY) {
    return;
  }

  renderer.clearScreen();

  auto& theme = UITheme::getInstance();
  auto metrics = theme.getMetrics();
  Rect screen = theme.getScreenSafeArea(renderer, true, false);

  // Draw header
  // STR_NETWORKS_FOUND is ~37 bytes once the Arabic translation is substituted,
  // so 32 truncated it. See ClockSyncActivity for the same class of bug.
  char countStr[64];
  snprintf(countStr, sizeof(countStr), tr(STR_NETWORKS_FOUND), realNetworkCount);
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_WIFI_NETWORKS), countStr);
  GUI.drawSubHeader(
      renderer,
      Rect{screen.x, screen.y + metrics.topPadding + metrics.headerHeight, screen.width, metrics.tabBarHeight},
      cachedMacAddress.c_str());

  switch (state) {
    case WifiSelectionState::AUTO_CONNECTING:
      renderConnecting(&screen, &metrics);
      break;
    case WifiSelectionState::SCANNING:
      renderConnecting(&screen, &metrics);  // Reuse connecting screen with different message
      break;
    case WifiSelectionState::NETWORK_LIST:
      renderNetworkList(&screen, &metrics);
      break;
    case WifiSelectionState::HIDDEN_SSID_ENTRY:
      // Transitioning to/from the SSID keyboard subactivity - nothing to draw
      break;
    case WifiSelectionState::CONNECTING:
      renderConnecting(&screen, &metrics);
      break;
    case WifiSelectionState::CONNECTED:
      renderConnected(&screen, &metrics);
      break;
    case WifiSelectionState::SAVE_PROMPT:
    case WifiSelectionState::FORGET_PROMPT: {
      // The app's screen builder draws the option dialog panel itself.
      renderUi();
      const auto labels =
          mappedInput.mapLabels(state == WifiSelectionState::SAVE_PROMPT ? tr(STR_CANCEL) : tr(STR_BACK),
                                tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }
    case WifiSelectionState::CONNECTION_FAILED:
      renderConnectionFailed(&screen, &metrics);
      break;
  }

  renderer.displayBuffer();
}

void WifiSelectionActivity::listScreen(UiScreen& screen, void* user) {
  static_cast<WifiSelectionActivity*>(user)->buildListScreen(screen);
}

void WifiSelectionActivity::buildListScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content below the header + MAC sub-band, above the legend line.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight +
                           metrics.verticalSpacing),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + metrics.verticalSpacing * 2),
      static_cast<int16_t>(safe.x)});

  if (state == WifiSelectionState::SAVE_PROMPT || state == WifiSelectionState::FORGET_PROMPT) {
    buildPromptDialog(screen);
    return;
  }

  if (networks.empty()) {
    screen.centeredText(tr(STR_NO_NETWORKS), screen.theme().bodyText);
    if (mappedInput.hasTouch()) {
      // Touch has no OK button to rescan with; offer the retry on screen instead
      // of the "Press OK" hint renderNetworkList draws for button boards.
      const auto& theme = screen.theme();
      const fui::Rect body = screen.body();
      const int16_t buttonWidth = static_cast<int16_t>(body.width / 2);
      const fui::Rect buttonRect{static_cast<int16_t>(body.x + (body.width - buttonWidth) / 2),
                                 static_cast<int16_t>(body.y + body.height * 2 / 3), buttonWidth, theme.rowHeight};
      fui::ButtonProps scan;
      scan.label = tr(STR_RETRY);
      scan.action = ACTION_SCAN;
      scan.inputMask = fui::InputTouch;
      scan.text = theme.bodyText;
      fui::button(screen.frame(), buttonRect, scan);
    }
    return;
  }

  // networkStatuses/networkRowItems are built once per processWifiScanResults()
  // call (see rebuildNetworkRowItems()) and reused here on every repaint.
  fui::ListProps props;
  props.items = networkRowItems.data();
  props.count = static_cast<uint16_t>(networkRowItems.size());
  props.action = ACTION_ROW;
  // Tap opens; long-press a saved network forgets it (physical buttons stay in loop()).
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  props.valueInset = 8;  // air between the signal bars and the row edge
  // Long SSIDs wrap onto a second line inside the row (two body lines always
  // fit the theme row height) instead of truncating; the trailing value is
  // just the short status glyphs, so skip the balanced 60%-band wrap cap.
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 2;
  props.balanceWrappedLabelWithValue = false;
  listNav.selected = static_cast<int>(selectedNetworkIndex);
  int16_t rowHeight = screen.theme().rowHeight;
  if (!mappedInput.hasTouch()) {
    // Non-touch hardware (X3/X4) keeps the original, denser row height
    // instead of FreeInkUI's touch-target-sized default (see
    // UiListActivity::syncListViewport; this screen predates that base and
    // syncs its own viewport directly). A long SSID that wraps grows only
    // its own row: list() sizes wrapped items per-row.
    rowHeight = static_cast<int16_t>(metrics.listRowHeight);
    props.rowHeight = rowHeight;
  }
  listNav.syncToProps(screen.body(), rowHeight, screen.theme().listRowGap, static_cast<int>(networks.size()), props);
  screen.list(props);
}

void WifiSelectionActivity::buildPromptDialog(UiScreen& screen) {
  const bool isForget = state == WifiSelectionState::FORGET_PROMPT;

  // Owned for the duration of the draw; the dialog wraps long SSIDs itself.
  const std::string ssidInfo = std::string(tr(STR_NETWORK_PREFIX)) + selectedSSID;

  const int selection = isForget ? forgetPromptSelection : savePromptSelection;
  fui::DialogOption options[2];
  options[0].label = isForget ? tr(STR_CANCEL) : tr(STR_YES);
  options[1].label = isForget ? tr(STR_FORGET_NETWORK) : tr(STR_NO);
  for (int i = 0; i < 2; i++) {
    options[i].action = ACTION_PROMPT;
    options[i].value = static_cast<int16_t>(i);
    options[i].state = selection == i ? fui::StateFocused : fui::StateNormal;
  }

  fui::OptionDialogProps props;
  props.title = isForget ? tr(STR_FORGET_NETWORK) : tr(STR_CONNECTED);
  props.headline = ssidInfo.c_str();
  props.message = isForget ? tr(STR_FORGET_AND_REMOVE) : tr(STR_SAVE_PASSWORD);
  props.options = options;
  props.optionCount = 2;
  // Stacked full-width options (matching OptionPopup): side-by-side halves
  // truncate the long "Forget network" label.
  props.verticalOptions = true;
  props.titleText = screen.theme().smallText;
  props.titleText.bold = true;
  // TextStyle defaults to maxLines=1 (ellipsis truncation); let the SSID
  // headline and the question wrap. optionDialogHeight measures with the same
  // styles, so the dialog grows to fit the wrapped lines.
  props.headlineText = screen.theme().bodyText;
  props.headlineText.maxLines = 2;
  props.messageText = screen.theme().smallText;
  props.messageText.maxLines = 3;
  props.buttonText = screen.theme().smallText;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  // defaultPopupStyles() (fui::optionDialog's fallback when styles is left
  // unset) has no border; opt one in explicitly using the theme's popup frame
  // metrics, matching OptionPopup::render().
  const auto& metrics = UITheme::getInstance().getMetrics();
  props.styles = fui::defaultPopupStyles();
  props.styles.normal.border = fui::Paint::solid(fui::Color::Black);
  props.styles.normal.borderWidth = static_cast<uint8_t>(metrics.popupFrameThickness);
  props.styles.normal.radius = static_cast<uint8_t>(metrics.popupCornerRadius);
  props.styles.selected = props.styles.normal;
  props.styles.focused = props.styles.normal;
  props.styles.active = props.styles.normal;
  props.styles.disabled = props.styles.normal;

  const fui::Rect body = screen.body();
  int16_t width = static_cast<int16_t>(renderer.getScreenWidth() * 3 / 4);
  if (width > body.width) width = body.width;
  const int16_t height = fui::optionDialogHeight(screen.target(), props, width);
  fui::optionDialog(screen.frame(), fui::centeredRect(body, fui::Size{width, height}), props);
}

void WifiSelectionActivity::renderNetworkList(const Rect* screen, const ThemeMetrics* metrics) {
  renderUi();
  if (networks.empty() && !mappedInput.hasTouch()) {
    // Below the centered "no networks" line the app drew. Touch boards get an
    // on-screen Retry button from the screen builder instead of this hint.
    const auto height = renderer.getLineHeight(UI_10_FONT_ID);
    const auto top = screen->y + (screen->height - height) / 2;
    UITheme::drawCenteredText(renderer, *screen, SMALL_FONT_ID, top + height + 10, tr(STR_PRESS_OK_SCAN));
  }

  GUI.drawHelpText(renderer,
                   Rect{screen->x, screen->y + screen->height - metrics->contentSidePadding - 15, screen->width, 20},
                   tr(STR_NETWORK_LEGEND));

  const bool hasSavedPassword = !networks.empty() && networks[selectedNetworkIndex].hasSavedPassword;
  const char* forgetLabel = hasSavedPassword ? tr(STR_FORGET_BUTTON) : "";

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CONNECT), forgetLabel, tr(STR_RETRY));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void WifiSelectionActivity::renderConnecting(const Rect* screen, const ThemeMetrics* metrics) const {
  constexpr int MAX_STATUS_LINES = 2;
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = screen->y + (screen->height - height) / 2;
  const int statusX = screen->x + metrics->contentSidePadding;
  const int statusWidth = screen->width - metrics->contentSidePadding * 2;

  if (state == WifiSelectionState::SCANNING) {
    const char* statusText = autoConnecting ? tr(STR_FINDING_SAVED_WIFI) : tr(STR_SCANNING);
    const Rect statusBounds{statusX, screen->y, statusWidth, screen->height};
    UITheme::drawCenteredWrappedText(renderer, statusBounds, UI_10_FONT_ID, statusText, MAX_STATUS_LINES);
    if (autoConnecting) {
      const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_SHOW_NETWORKS), "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }
  } else {
    const char* statusText = autoConnecting ? tr(STR_CONNECTING_SAVED_WIFI) : tr(STR_CONNECTING);
    const Rect statusBounds{statusX, screen->y, statusWidth, top - metrics->verticalSpacing - screen->y};
    UITheme::drawCenteredWrappedText(renderer, statusBounds, UI_12_FONT_ID, statusText, MAX_STATUS_LINES, true,
                                     EpdFontFamily::BOLD, UITheme::TextVerticalAlignment::BOTTOM);

    std::string ssidInfo = std::string(tr(STR_TO_PREFIX)) + selectedSSID;
    if (ssidInfo.length() > 25) {
      ssidInfo.replace(22, ssidInfo.length() - 22, "...");
    }
    UITheme::drawCenteredText(renderer, *screen, UI_10_FONT_ID, top, ssidInfo.c_str());
    if (autoConnecting) {
      const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_SHOW_NETWORKS), "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }
  }
}

void WifiSelectionActivity::renderConnected(const Rect* screen, const ThemeMetrics* metrics) const {
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = screen->y + (screen->height - height * 4) / 2;

  UITheme::drawCenteredText(renderer, *screen, UI_12_FONT_ID, top - 30, tr(STR_CONNECTED), true, EpdFontFamily::BOLD);

  std::string ssidInfo = std::string(tr(STR_NETWORK_PREFIX)) + selectedSSID;
  if (ssidInfo.length() > 28) {
    ssidInfo.replace(25, ssidInfo.length() - 25, "...");
  }
  UITheme::drawCenteredText(renderer, *screen, UI_10_FONT_ID, top + 10, ssidInfo.c_str());

  const std::string ipInfo = std::string(tr(STR_IP_ADDRESS_PREFIX)) + connectedIP;
  UITheme::drawCenteredText(renderer, *screen, UI_10_FONT_ID, top + 40, ipInfo.c_str());

  // Use centralized button hints
  const auto labels = mappedInput.mapLabels("", tr(STR_DONE), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void WifiSelectionActivity::renderConnectionFailed(const Rect* screen, const ThemeMetrics* metrics) const {
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = screen->y + (screen->height - height * 2) / 2;

  UITheme::drawCenteredText(renderer, *screen, UI_12_FONT_ID, top - 20, tr(STR_CONNECTION_FAILED), true,
                            EpdFontFamily::BOLD);
  UITheme::drawCenteredText(renderer, *screen, UI_10_FONT_ID, top + 20, connectionError.c_str());

  // Confirm only leads to the forget prompt when a saved credential failed;
  // otherwise it just dismisses like Back does.
  const char* confirmLabel = usedSavedPassword ? tr(STR_FORGET_BUTTON) : tr(STR_DONE);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void WifiSelectionActivity::onComplete(const bool connected) {
  ActivityResult result;
  result.isCancelled = !connected;
  if (connected) {
    result.data = WifiResult{true, selectedSSID, connectedIP};
  }
  setResult(std::move(result));
  finish();
}
