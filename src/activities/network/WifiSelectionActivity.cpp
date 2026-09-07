#include "WifiSelectionActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <MemoryBudget.h>
#include <WiFi.h>

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

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#include <esp_mac.h>
#endif

namespace {
constexpr size_t WIFI_BSSID_LEN = 6;

bool hasBssidBytes(const uint8_t bssid[WIFI_BSSID_LEN]) {
  return std::any_of(bssid, bssid + WIFI_BSSID_LEN, [](const uint8_t part) { return part != 0; });
}
}  // namespace

void WifiSelectionActivity::onEnter() {
  Activity::onEnter();

  // WiFi startup needs several contiguous driver buffers. Release both the
  // active SD font and its catalog before the radio starts allocating them.
  const auto heapBeforeFontRelease = MemoryBudget::snapshot();
  const bool releasedSdFont = sdFontSystem.releaseForNetwork(renderer);
  const auto heapAfterFontRelease = MemoryBudget::snapshot();
  LOG_DBG("WIFI", "SD font network trim released=%d free=%u->%u delta=%ld maxAlloc=%u->%u delta=%ld",
          releasedSdFont, heapBeforeFontRelease.freeHeap, heapAfterFontRelease.freeHeap,
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
  autoConnectAttempted = false;

  // Cache the MAC address for display.
  // On ESP32, read the base MAC directly to avoid placeholder values when the
  // WiFi driver has not fully initialized yet.
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
  uint8_t baseMac[6] = {};
  char macStr[64];
  const esp_err_t macResult = esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
  if (macResult == ESP_OK) {
    snprintf(macStr, sizeof(macStr), "%s %02x-%02x-%02x-%02x-%02x-%02x", tr(STR_MAC_ADDRESS), baseMac[0],
             baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
  } else {
    LOG_ERR("WIFI", "Failed to read station MAC (err=%d)", static_cast<int>(macResult));
    snprintf(macStr, sizeof(macStr), "%s --", tr(STR_MAC_ADDRESS));
  }
  cachedMacAddress = std::string(macStr);
#else
  WiFi.mode(WIFI_STA);
  delay(10);
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char macStr[64];
  snprintf(macStr, sizeof(macStr), "%s %02x-%02x-%02x-%02x-%02x-%02x", tr(STR_MAC_ADDRESS), mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);
  cachedMacAddress = std::string(macStr);
#endif

  // Trigger first update to show scanning message
  requestUpdate();

  // Auto mode always scans first so we only attempt remembered networks that
  // are actually in range.
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

void WifiSelectionActivity::startWifiScan() {
  autoConnecting = false;
  state = WifiSelectionState::SCANNING;
  networks.clear();
  realNetworkCount = 0;
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
    state = WifiSelectionState::NETWORK_LIST;
    selectedNetworkIndex = 0;
    requestUpdate();
    return;
  }

  // Scan complete, process results. Deduplicate in-place by SSID, keeping the
  // strongest signal without allocating a separate map on the heap.
  networks.clear();
  networks.reserve(scanResult);

  for (int i = 0; i < scanResult; i++) {
    std::string ssid = WiFi.SSID(i).c_str();
    const int32_t rssi = WiFi.RSSI(i);

    // Skip hidden networks (empty SSID)
    if (ssid.empty()) {
      continue;
    }

    auto existing = std::find_if(networks.begin(), networks.end(), [&ssid](const WifiNetworkInfo& network) {
      return network.ssid == ssid;
    });

    if (existing == networks.end()) {
      WifiNetworkInfo network;
      network.ssid = ssid;
      network.rssi = rssi;
      network.isEncrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
      network.hasSavedPassword = WIFI_STORE.hasSavedCredential(network.ssid);
      network.channel = WiFi.channel(i);
      WiFi.BSSID(i, network.bssid);
      network.hasBssid = network.channel > 0 && hasBssidBytes(network.bssid);
      networks.push_back(network);
    } else if (rssi > existing->rssi) {
      existing->rssi = rssi;
      existing->isEncrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
      existing->hasSavedPassword = WIFI_STORE.hasSavedCredential(existing->ssid);
      existing->channel = WiFi.channel(i);
      WiFi.BSSID(i, existing->bssid);
      existing->hasBssid = existing->channel > 0 && hasBssidBytes(existing->bssid);
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

  WiFi.scanDelete();

  // Auto-connect to the best saved-password network if one is in range and the
  // parent flow explicitly allows automatic selection. Prefer the last
  // connected SSID when it is present in scan results; otherwise fall back to
  // the strongest saved network because the list is already sorted with saved
  // networks first and RSSI descending inside each group.
  if (allowAutoConnect && !autoConnectAttempted && realNetworkCount > 0) {
    autoConnectAttempted = true;
    const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
    if (!lastSsid.empty()) {
      const auto remembered = std::find_if(networks.begin(), networks.end(), [&lastSsid](const WifiNetworkInfo& network) {
        return network.ssid == lastSsid && network.hasSavedPassword;
      });
      if (remembered != networks.end()) {
        selectedNetworkIndex = static_cast<size_t>(std::distance(networks.begin(), remembered));
        LOG_DBG("WIFI", "Found last connected network in range: %s (RSSI: %d) - auto-connecting", remembered->ssid.c_str(),
                remembered->rssi);
        if (connectUsingSavedCredential(*remembered, true)) {
          return;
        }
      }
    }

    if (networks[0].hasSavedPassword) {
      selectedNetworkIndex = 0;
      LOG_DBG("WIFI", "Found saved network in range: %s (RSSI: %d) - auto-connecting", networks[0].ssid.c_str(),
              networks[0].rssi);
      if (connectUsingSavedCredential(networks[0], true)) {
        return;
      }
    }
  }

  if (allowAutoConnect && autoConnectOnly) {
    LOG_DBG("WIFI", "Auto-connect only found no saved network in range");
    onComplete(false);
    return;
  }

  state = WifiSelectionState::NETWORK_LIST;
  selectedNetworkIndex = 0;
  requestUpdate();
}

void WifiSelectionActivity::appendHiddenNetworkEntry() {
  WifiNetworkInfo placeholder;
  placeholder.rssi = 0;
  placeholder.isEncrypted = true;
  placeholder.hasSavedPassword = false;
  placeholder.isHiddenPlaceholder = true;
  networks.push_back(std::move(placeholder));
}

void WifiSelectionActivity::selectNetwork(const int index) {
  if (index < 0 || index >= static_cast<int>(networks.size())) {
    return;
  }

  const auto& network = networks[index];
  if (network.isHiddenPlaceholder) {
    promptHiddenSsid();
    return;
  }

  setSelectedNetwork(network);
  usedSavedPassword = false;
  enteredPassword.clear();
  autoConnecting = false;

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
  state = WifiSelectionState::PASSWORD_ENTRY;
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_ENTER_WIFI_PASSWORD),
                                                                 "",  // No initial text
                                                                 64,  // Max password length
                                                                 InputType::Password),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             state = WifiSelectionState::NETWORK_LIST;
                           } else {
                             enteredPassword = std::get<KeyboardResult>(result.data).text;
                           }
                         });
}

void WifiSelectionActivity::promptHiddenSsid() {
  selectedSSID.clear();
  selectedRequiresPassword = true;
  selectedChannel = 0;
  selectedHasBssid = false;
  std::memset(selectedBssid, 0, sizeof(selectedBssid));
  usedSavedPassword = false;
  enteredPassword.clear();
  autoConnecting = false;

  state = WifiSelectionState::HIDDEN_SSID_ENTRY;
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_ENTER_WIFI_SSID),
                                                                 "",  // No initial text
                                                                 32,  // IEEE 802.11 SSID limit
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

bool WifiSelectionActivity::connectUsingSavedCredential(const WifiNetworkInfo& network, const bool isAutoConnectAttempt) {
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

  // Read the hardware-derived station MAC directly: WiFi.macAddress() can
  // still return a placeholder before the STA netif is ready (upstream
  // 9b090266).
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
  uint8_t mac[6] = {};
  const esp_err_t macResult = esp_read_mac(mac, ESP_MAC_WIFI_STA);
  if (macResult == ESP_OK) {
    char hostname[sizeof("CrossPoint-Reader-") + 12];
    snprintf(hostname, sizeof(hostname), "CrossPoint-Reader-%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);
    WiFi.setHostname(hostname);
  } else {
    LOG_ERR("WIFI", "Failed to read station MAC for hostname (err=%d)", static_cast<int>(macResult));
  }
#else
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  const String hostname = "CrossPoint-Reader-" + mac;
  WiFi.setHostname(hostname.c_str());
#endif

  const char* password = (selectedRequiresPassword && !enteredPassword.empty()) ? enteredPassword.c_str() : nullptr;
  if (selectedHasBssid && selectedChannel > 0) {
    LOG_DBG("WIFI",
            "Connecting to %s on channel %d via BSSID %02x:%02x:%02x:%02x:%02x:%02x",
            selectedSSID.c_str(), static_cast<int>(selectedChannel), selectedBssid[0], selectedBssid[1],
            selectedBssid[2], selectedBssid[3], selectedBssid[4], selectedBssid[5]);
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
    // Stop the SDK from retrying in the background while the user is back in
    // the list; the timeout path below does the same.
    WiFi.disconnect();
    if (autoConnectOnly) {
      LOG_DBG("WIFI", "Auto-connect only failed for saved network %s (status=%d)", selectedSSID.c_str(),
              static_cast<int>(status));
      onComplete(false);
      return;
    }
    state = WifiSelectionState::CONNECTION_FAILED;
    requestUpdate();
    return;
  }

  // Check for timeout
  if (millis() - connectionStartTime > CONNECTION_TIMEOUT_MS) {
    LOG_ERR("WIFI", "Connection timeout for %s, status=%d, channel=%d, bssid=%d", selectedSSID.c_str(),
            static_cast<int>(status), static_cast<int>(selectedChannel), selectedHasBssid ? 1 : 0);
    WiFi.disconnect();
    connectionError = tr(STR_ERROR_CONNECTION_TIMEOUT);
    if (autoConnectOnly) {
      LOG_DBG("WIFI", "Auto-connect only timed out for saved network %s", selectedSSID.c_str());
      onComplete(false);
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
    processWifiScanResults();
    return;
  }

  // Check connection progress
  if (state == WifiSelectionState::CONNECTING || state == WifiSelectionState::AUTO_CONNECTING) {
    checkConnectionStatus();
    return;
  }

  if (state == WifiSelectionState::HIDDEN_SSID_ENTRY) {
    WifiNetworkInfo hidden;
    hidden.ssid = selectedSSID;
    hidden.isEncrypted = true;
    hidden.hasSavedPassword = WIFI_STORE.hasSavedCredential(selectedSSID);
    if (connectUsingSavedCredential(hidden, false)) {
      return;
    }
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
      }
      // Go back to network list (whether Cancel or Forget network was selected)
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
    // Back always dismisses straight to the network list. Only Confirm opts in
    // to the forget prompt, and only when a saved credential is what failed.
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      returnToNetworkList();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
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
  }

  // Handle network list state
  if (state == WifiSelectionState::NETWORK_LIST) {
    // Check for Back button to exit (cancel)
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      onComplete(false);
      return;
    }

    // Check for Confirm button to select network or rescan
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
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

    // Handle navigation
    buttonNavigator.onNext([this] {
      selectedNetworkIndex = ButtonNavigator::nextIndex(selectedNetworkIndex, networks.size());
      requestUpdate();
    });

    buttonNavigator.onPrevious([this] {
      selectedNetworkIndex = ButtonNavigator::previousIndex(selectedNetworkIndex, networks.size());
      requestUpdate();
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
  // Don't render if we're in PASSWORD_ENTRY state - we're just transitioning
  // from the keyboard subactivity back to the main activity
  if (state == WifiSelectionState::HIDDEN_SSID_ENTRY || state == WifiSelectionState::PASSWORD_ENTRY) {
    return;
  }

  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Draw header
  // Translated UTF-8 labels can occupy substantially more bytes than English.
  char countStr[64];
  snprintf(countStr, sizeof(countStr), tr(STR_NETWORKS_FOUND), realNetworkCount);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_WIFI_NETWORKS),
                 countStr);
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                    cachedMacAddress.c_str());

  switch (state) {
    case WifiSelectionState::AUTO_CONNECTING:
      renderConnecting();
      break;
    case WifiSelectionState::SCANNING:
      renderConnecting();  // Reuse connecting screen with different message
      break;
    case WifiSelectionState::NETWORK_LIST:
      renderNetworkList();
      break;
    case WifiSelectionState::HIDDEN_SSID_ENTRY:
      break;
    case WifiSelectionState::CONNECTING:
      renderConnecting();
      break;
    case WifiSelectionState::CONNECTED:
      renderConnected();
      break;
    case WifiSelectionState::SAVE_PROMPT:
      renderSavePrompt();
      break;
    case WifiSelectionState::CONNECTION_FAILED:
      renderConnectionFailed();
      break;
    case WifiSelectionState::FORGET_PROMPT:
      renderForgetPrompt();
      break;
  }

  renderer.displayBuffer();
}

void WifiSelectionActivity::renderNetworkList() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (networks.empty()) {
    // No networks found or scan failed
    const auto height = renderer.getLineHeight(UI_10_FONT_ID);
    const auto top = (pageHeight - height) / 2;
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_NO_NETWORKS));
    renderer.drawCenteredText(SMALL_FONT_ID, top + height + 10, tr(STR_PRESS_OK_SCAN));
  } else {
    int contentTop = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
    int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(networks.size()),
        selectedNetworkIndex,
        [this](int index) {
          const auto& network = networks[index];
          return network.isHiddenPlaceholder ? std::string(tr(STR_ADD_HIDDEN_NETWORK)) : network.ssid;
        },
        nullptr, nullptr,
        [this](int index) {
          const auto& network = networks[index];
          if (network.isHiddenPlaceholder) {
            return std::string();
          }
          return std::string(network.hasSavedPassword ? "+ " : "") + (network.isEncrypted ? "* " : "") +
                 getSignalStrengthIndicator(network.rssi);
        });
  }

  GUI.drawHelpText(renderer,
                   Rect{0, pageHeight - metrics.buttonHintsHeight - metrics.contentSidePadding - 15, pageWidth, 20},
                   tr(STR_NETWORK_LEGEND));

  const bool hasSavedPassword = !networks.empty() && networks[selectedNetworkIndex].hasSavedPassword;
  const char* forgetLabel = hasSavedPassword ? tr(STR_FORGET_BUTTON) : "";

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CONNECT), forgetLabel, tr(STR_RETRY));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void WifiSelectionActivity::renderConnecting() const {
  const auto pageHeight = renderer.getScreenHeight();
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - height) / 2;

  if (state == WifiSelectionState::SCANNING) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_SCANNING));
  } else {
    renderer.drawCenteredText(UI_12_FONT_ID, top - 40, tr(STR_CONNECTING), true, EpdFontFamily::BOLD);

    std::string ssidInfo = std::string(tr(STR_TO_PREFIX)) + selectedSSID;
    if (ssidInfo.length() > 25) {
      ssidInfo.replace(22, ssidInfo.length() - 22, "...");
    }
    renderer.drawCenteredText(UI_10_FONT_ID, top, ssidInfo.c_str());
  }
}

void WifiSelectionActivity::renderConnected() const {
  const auto pageHeight = renderer.getScreenHeight();
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - height * 4) / 2;

  renderer.drawCenteredText(UI_12_FONT_ID, top - 30, tr(STR_CONNECTED), true, EpdFontFamily::BOLD);

  std::string ssidInfo = std::string(tr(STR_NETWORK_PREFIX)) + selectedSSID;
  if (ssidInfo.length() > 28) {
    ssidInfo.replace(25, ssidInfo.length() - 25, "...");
  }
  renderer.drawCenteredText(UI_10_FONT_ID, top + 10, ssidInfo.c_str());

  const std::string ipInfo = std::string(tr(STR_IP_ADDRESS_PREFIX)) + connectedIP;
  renderer.drawCenteredText(UI_10_FONT_ID, top + 40, ipInfo.c_str());

  // Use centralized button hints
  const auto labels = mappedInput.mapLabels("", tr(STR_DONE), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void WifiSelectionActivity::renderSavePrompt() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - height * 3) / 2;

  renderer.drawCenteredText(UI_12_FONT_ID, top - 40, tr(STR_CONNECTED), true, EpdFontFamily::BOLD);

  std::string ssidInfo = std::string(tr(STR_NETWORK_PREFIX)) + selectedSSID;
  if (ssidInfo.length() > 28) {
    ssidInfo.replace(25, ssidInfo.length() - 25, "...");
  }
  renderer.drawCenteredText(UI_10_FONT_ID, top, ssidInfo.c_str());

  renderer.drawCenteredText(UI_10_FONT_ID, top + 40, tr(STR_SAVE_PASSWORD));

  // Draw Yes/No buttons
  const int buttonY = top + 80;
  constexpr int buttonWidth = 60;
  constexpr int buttonSpacing = 30;
  constexpr int totalWidth = buttonWidth * 2 + buttonSpacing;
  const int startX = (pageWidth - totalWidth) / 2;

  // Draw "Yes" button
  if (savePromptSelection == 0) {
    std::string text = "[" + std::string(tr(STR_YES)) + "]";
    renderer.drawText(UI_10_FONT_ID, startX, buttonY, text.c_str());
  } else {
    renderer.drawText(UI_10_FONT_ID, startX + 4, buttonY, tr(STR_YES));
  }

  // Draw "No" button
  if (savePromptSelection == 1) {
    std::string text = "[" + std::string(tr(STR_NO)) + "]";
    renderer.drawText(UI_10_FONT_ID, startX + buttonWidth + buttonSpacing, buttonY, text.c_str());
  } else {
    renderer.drawText(UI_10_FONT_ID, startX + buttonWidth + buttonSpacing + 4, buttonY, tr(STR_NO));
  }

  // Use centralized button hints
  const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void WifiSelectionActivity::renderConnectionFailed() const {
  const auto pageHeight = renderer.getScreenHeight();
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - height * 2) / 2;

  renderer.drawCenteredText(UI_12_FONT_ID, top - 20, tr(STR_CONNECTION_FAILED), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, top + 20, connectionError.c_str());

  // Confirm only leads to the forget prompt when a saved credential failed;
  // otherwise it just dismisses like Back does.
  const char* confirmLabel = usedSavedPassword ? tr(STR_FORGET_BUTTON) : tr(STR_DONE);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void WifiSelectionActivity::renderForgetPrompt() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - height * 3) / 2;

  renderer.drawCenteredText(UI_12_FONT_ID, top - 40, tr(STR_FORGET_NETWORK), true, EpdFontFamily::BOLD);

  std::string ssidInfo = std::string(tr(STR_NETWORK_PREFIX)) + selectedSSID;
  if (ssidInfo.length() > 28) {
    ssidInfo.replace(25, ssidInfo.length() - 25, "...");
  }
  renderer.drawCenteredText(UI_10_FONT_ID, top, ssidInfo.c_str());

  renderer.drawCenteredText(UI_10_FONT_ID, top + 40, tr(STR_FORGET_AND_REMOVE));

  // Draw Cancel/Forget network buttons
  const int buttonY = top + 80;
  constexpr int buttonWidth = 120;
  constexpr int buttonSpacing = 30;
  constexpr int totalWidth = buttonWidth * 2 + buttonSpacing;
  const int startX = (pageWidth - totalWidth) / 2;

  // Draw "Cancel" button
  if (forgetPromptSelection == 0) {
    std::string text = "[" + std::string(tr(STR_CANCEL)) + "]";
    renderer.drawText(UI_10_FONT_ID, startX, buttonY, text.c_str());
  } else {
    renderer.drawText(UI_10_FONT_ID, startX + 4, buttonY, tr(STR_CANCEL));
  }

  // Draw "Forget network" button
  if (forgetPromptSelection == 1) {
    std::string text = "[" + std::string(tr(STR_FORGET_BUTTON)) + "]";
    renderer.drawText(UI_10_FONT_ID, startX + buttonWidth + buttonSpacing, buttonY, text.c_str());
  } else {
    renderer.drawText(UI_10_FONT_ID, startX + buttonWidth + buttonSpacing + 4, buttonY, tr(STR_FORGET_BUTTON));
  }

  // Use centralized button hints
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
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
