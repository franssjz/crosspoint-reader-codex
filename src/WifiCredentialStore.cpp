#include "WifiCredentialStore.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>
#include <ObfuscationUtils.h>
#include <Serialization.h>

#include <algorithm>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// Initialize the static instance
WifiCredentialStore WifiCredentialStore::instance;

namespace {
// File format version (for binary migration)
constexpr uint8_t WIFI_FILE_VERSION = 2;

// File paths
constexpr char WIFI_FILE_BIN[] = "/.crosspoint/wifi.bin";
constexpr char WIFI_FILE_JSON[] = "/.crosspoint/wifi.json";
constexpr char WIFI_FILE_BAK[] = "/.crosspoint/wifi.bin.bak";

// Legacy obfuscation key - "CrossPoint" in ASCII (only used for binary migration)
constexpr uint8_t LEGACY_OBFUSCATION_KEY[] = {0x43, 0x72, 0x6F, 0x73, 0x73, 0x50, 0x6F, 0x69, 0x6E, 0x74};
constexpr size_t LEGACY_KEY_LENGTH = sizeof(LEGACY_OBFUSCATION_KEY);

void legacyDeobfuscate(std::string& data) {
  for (size_t i = 0; i < data.size(); i++) {
    data[i] ^= LEGACY_OBFUSCATION_KEY[i % LEGACY_KEY_LENGTH];
  }
}
}  // namespace

bool WifiCredentialStore::saveToFile() const {
  std::lock_guard<std::mutex> lock(persistenceMutex);
  return saveToFileUnlocked();
}

bool WifiCredentialStore::saveToFileUnlocked() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveWifi(*this, WIFI_FILE_JSON);
}

bool WifiCredentialStore::loadFromFile() {
  std::lock_guard<std::mutex> lock(persistenceMutex);
  const std::string tempPath = std::string(WIFI_FILE_JSON) + ".tmp";
  if (!Storage.exists(WIFI_FILE_JSON) && Storage.exists(tempPath.c_str())) {
    if (Storage.rename(tempPath.c_str(), WIFI_FILE_JSON)) {
      LOG_DBG("WCS", "Recovered wifi.json from interrupted temp file");
    }
  }

  // Try JSON first
  if (Storage.exists(WIFI_FILE_JSON)) {
    String json = Storage.readFile(WIFI_FILE_JSON);
    if (!json.isEmpty()) {
      bool resave = false;
      bool result = JsonSettingsIO::loadWifi(*this, json.c_str(), &resave);
      if (result && resave) {
        LOG_DBG("WCS", "Resaving JSON with obfuscated passwords");
        saveToFileUnlocked();
      }
      return result;
    }
  }

  // Fall back to binary migration
  if (Storage.exists(WIFI_FILE_BIN)) {
    if (loadFromBinaryFile()) {
      if (saveToFileUnlocked()) {
        Storage.rename(WIFI_FILE_BIN, WIFI_FILE_BAK);
        LOG_DBG("WCS", "Migrated wifi.bin to wifi.json");
        return true;
      } else {
        LOG_ERR("WCS", "Failed to save wifi during migration");
        return false;
      }
    }
  }

  return false;
}

bool WifiCredentialStore::loadFromBinaryFile() {
  HalFile file;
  if (!Storage.openFileForRead("WCS", WIFI_FILE_BIN, file)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(file, version);
  if (version > WIFI_FILE_VERSION) {
    LOG_DBG("WCS", "Unknown file version: %u", version);
    return false;
  }

  std::string loadedLastConnectedSsid;
  if (version >= 2) serialization::readString(file, loadedLastConnectedSsid);

  uint8_t count;
  serialization::readPod(file, count);

  std::vector<WifiCredential> loadedCredentials;
  loadedCredentials.reserve(std::min(static_cast<size_t>(count), MAX_NETWORKS));
  for (uint8_t i = 0; i < count && i < MAX_NETWORKS; i++) {
    WifiCredential cred;
    serialization::readString(file, cred.ssid);
    serialization::readString(file, cred.password);
    legacyDeobfuscate(cred.password);
    if (cred.password.size() > MAX_PASSWORD_LENGTH) {
      LOG_ERR("WCS", "Discarding oversized binary password for %s", cred.ssid.c_str());
      continue;
    }
    loadedCredentials.push_back(std::move(cred));
  }

  {
    std::lock_guard<std::mutex> lock(credentialMutex);
    lastConnectedSsid = std::move(loadedLastConnectedSsid);
    credentials = std::move(loadedCredentials);
  }

  return true;
}

bool WifiCredentialStore::addCredential(const std::string& ssid, const std::string& password) {
  if (password.size() > MAX_PASSWORD_LENGTH) {
    LOG_ERR("WCS", "Cannot save password for %s: limit of %zu bytes exceeded", ssid.c_str(), MAX_PASSWORD_LENGTH);
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(credentialMutex);
    const auto cred = find_if(credentials.begin(), credentials.end(),
                              [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });
    if (cred != credentials.end()) {
      cred->password = password;
      LOG_DBG("WCS", "Updated credentials for: %s", ssid.c_str());
    } else {
      if (credentials.size() >= MAX_NETWORKS) {
        LOG_DBG("WCS", "Cannot add more networks, limit of %zu reached", MAX_NETWORKS);
        return false;
      }

      credentials.push_back({ssid, password});
      LOG_DBG("WCS", "Added credentials for: %s", ssid.c_str());
    }
  }
  return saveToFile();
}

bool WifiCredentialStore::removeCredential(const std::string& ssid) {
  {
    std::lock_guard<std::mutex> lock(credentialMutex);
    const auto cred = find_if(credentials.begin(), credentials.end(),
                              [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });
    if (cred == credentials.end()) return false;

    credentials.erase(cred);
    LOG_DBG("WCS", "Removed credentials for: %s", ssid.c_str());
    if (ssid == lastConnectedSsid) lastConnectedSsid.clear();
  }
  return saveToFile();
}

std::optional<WifiCredential> WifiCredentialStore::findCredential(const std::string& ssid) const {
  std::lock_guard<std::mutex> lock(credentialMutex);
  const auto cred = find_if(credentials.begin(), credentials.end(),
                            [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });

  if (cred != credentials.end()) return *cred;
  return std::nullopt;
}

std::optional<WifiCredential> WifiCredentialStore::getCredentialAt(const size_t index) const {
  std::lock_guard<std::mutex> lock(credentialMutex);
  if (index >= credentials.size()) return std::nullopt;
  return credentials[index];
}

std::optional<std::string> WifiCredentialStore::getSsidAt(const size_t index) const {
  std::lock_guard<std::mutex> lock(credentialMutex);
  if (index >= credentials.size()) return std::nullopt;
  return credentials[index].ssid;
}

size_t WifiCredentialStore::getCredentialCount() const {
  std::lock_guard<std::mutex> lock(credentialMutex);
  return credentials.size();
}

std::vector<WifiCredentialSummary> WifiCredentialStore::getCredentialSummaries() const {
  std::lock_guard<std::mutex> lock(credentialMutex);
  std::vector<WifiCredentialSummary> summaries;
  summaries.reserve(credentials.size());
  for (const auto& credential : credentials) {
    summaries.push_back({credential.ssid, !credential.password.empty(), credential.ssid == lastConnectedSsid});
  }
  return summaries;
}

bool WifiCredentialStore::hasCredentials() const {
  std::lock_guard<std::mutex> lock(credentialMutex);
  return !credentials.empty();
}

bool WifiCredentialStore::hasSavedCredential(const std::string& ssid) const {
  std::lock_guard<std::mutex> lock(credentialMutex);
  return find_if(credentials.begin(), credentials.end(),
                 [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; }) != credentials.end();
}

void WifiCredentialStore::setLastConnectedSsid(const std::string& ssid) {
  {
    std::lock_guard<std::mutex> lock(credentialMutex);
    if (lastConnectedSsid == ssid) return;
    lastConnectedSsid = ssid;
  }
  saveToFile();
}

std::string WifiCredentialStore::getLastConnectedSsid() const {
  std::lock_guard<std::mutex> lock(credentialMutex);
  return lastConnectedSsid;
}

void WifiCredentialStore::clearLastConnectedSsid() {
  {
    std::lock_guard<std::mutex> lock(credentialMutex);
    if (lastConnectedSsid.empty()) return;
    lastConnectedSsid.clear();
  }
  saveToFile();
}

void WifiCredentialStore::clearAll() {
  {
    std::lock_guard<std::mutex> lock(credentialMutex);
    credentials.clear();
    lastConnectedSsid.clear();
  }
  saveToFile();
  LOG_DBG("WCS", "Cleared all WiFi credentials");
}
