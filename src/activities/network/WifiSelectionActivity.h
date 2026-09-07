#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Structure to hold WiFi network information
struct WifiNetworkInfo {
  std::string ssid;
  int32_t rssi = 0;
  bool isEncrypted = false;
  bool hasSavedPassword = false;  // Whether we have saved credentials for this network
  int32_t channel = 0;
  uint8_t bssid[6] = {};
  bool hasBssid = false;
  bool isHiddenPlaceholder = false;
  std::string ipAddress;  // Populated after connection for display
};

// WiFi selection states
enum class WifiSelectionState {
  AUTO_CONNECTING,    // Trying to connect to the last known network
  SCANNING,           // Scanning for networks
  NETWORK_LIST,       // Displaying available networks
  HIDDEN_SSID_ENTRY,  // Entering the SSID of a hidden network
  PASSWORD_ENTRY,     // Entering password for selected network
  CONNECTING,         // Attempting to connect
  CONNECTED,          // Successfully connected
  SAVE_PROMPT,        // Asking user if they want to save the password
  CONNECTION_FAILED,  // Connection failed
  FORGET_PROMPT       // Asking user if they want to forget the network
};

/**
 * WifiSelectionActivity is responsible for scanning WiFi APs and connecting to them.
 * It will:
 * - Enter scanning mode on entry
 * - List available WiFi networks
 * - Allow selection and launch KeyboardEntryActivity for password if needed
 * - Save the password if requested
 * - Call onComplete callback when connected or cancelled
 *
 * The onComplete callback receives true if connected successfully, false if cancelled.
 */
class WifiSelectionActivity final : public Activity {
  ButtonNavigator buttonNavigator;

  WifiSelectionState state = WifiSelectionState::SCANNING;
  size_t selectedNetworkIndex = 0;
  std::vector<WifiNetworkInfo> networks;
  size_t realNetworkCount = 0;

  // Selected network for connection
  std::string selectedSSID;
  bool selectedRequiresPassword = false;
  int32_t selectedChannel = 0;
  uint8_t selectedBssid[6] = {};
  bool selectedHasBssid = false;

  // Connection result
  std::string connectedIP;
  std::string connectionError;

  // Password to potentially save (from keyboard or saved credentials)
  std::string enteredPassword;

  // Cached MAC address string for display
  std::string cachedMacAddress;

  // Whether network was connected using a saved password (skip save prompt)
  bool usedSavedPassword = false;

  // Whether to attempt auto-connect on entry
  const bool allowAutoConnect;

  // Whether to cancel instead of showing the network list when auto-connect
  // cannot use a saved in-range network.
  const bool autoConnectOnly;

  // Whether a successful connection should perform the one-time RTC sync hook.
  const bool syncRtcOnConnect;

  // Whether we are attempting to auto-connect
  bool autoConnecting = false;

  // Whether auto-connect already ran for this activity entry. Auto-connect is a
  // one-shot on entry; without this a rescan after a failed auto-connect would
  // immediately retry the same network and trap the user in a loop.
  bool autoConnectAttempted = false;

  // Save/forget prompt selection (0 = Yes, 1 = No)
  int savePromptSelection = 0;
  int forgetPromptSelection = 0;

  // Connection timeout
  static constexpr unsigned long CONNECTION_TIMEOUT_MS = 15000;
  unsigned long connectionStartTime = 0;

  void renderNetworkList() const;
  void renderPasswordEntry() const;
  void renderConnecting() const;
  void renderConnected() const;
  void renderSavePrompt() const;
  void renderConnectionFailed() const;
  void renderForgetPrompt() const;

  void startWifiScan();
  void returnToNetworkList();
  void processWifiScanResults();
  void appendHiddenNetworkEntry();
  void selectNetwork(int index);
  void promptHiddenSsid();
  void promptPasswordEntry();
  void setSelectedNetwork(const WifiNetworkInfo& network);
  bool connectUsingSavedCredential(const WifiNetworkInfo& network, bool isAutoConnectAttempt);
  void attemptConnection();
  void checkConnectionStatus();
  std::string getSignalStrengthIndicator(int32_t rssi) const;

  void onComplete(bool connected);

 public:
  explicit WifiSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool autoConnect = false,
                                 bool syncRtcOnConnect = true, bool autoConnectOnly = false)
      : Activity("WifiSelection", renderer, mappedInput),
        allowAutoConnect(autoConnect),
        autoConnectOnly(autoConnectOnly),
        syncRtcOnConnect(syncRtcOnConnect) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
