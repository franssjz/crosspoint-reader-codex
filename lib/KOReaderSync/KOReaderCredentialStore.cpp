#include "KOReaderCredentialStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <MD5Builder.h>
#include <ObfuscationUtils.h>
#include <Serialization.h>

#include <string>
#include <utility>

#include "../../src/JsonSettingsIO.h"

// Initialize the static instance
KOReaderCredentialStore KOReaderCredentialStore::instance;

namespace {
// File format version (for binary migration)
constexpr uint8_t KOREADER_FILE_VERSION = 1;

// File paths
constexpr char KOREADER_FILE_BIN[] = "/.crosspoint/koreader.bin";
constexpr char KOREADER_FILE_BAK[] = "/.crosspoint/koreader.bin.bak";
// Authoritative multi-profile store (the full profile list + which one is active).
constexpr char KOREADER_PROFILES_FILE_JSON[] = "/.crosspoint/koreader_profiles.json";
// Legacy single-record file, kept in its original shape and always mirroring the
// active profile. This is the file stock crosspoint-reader (and any other tool
// that predates multi-profile support) reads -- see JsonSettingsIO::saveKOReaderLegacyMirror.
constexpr char KOREADER_FILE_JSON[] = "/.crosspoint/koreader.json";

// Default sync server URL
constexpr char DEFAULT_SERVER_URL[] = "https://sync.koreader.rocks:443";

// Legacy obfuscation key - "KOReader" in ASCII (only used for binary migration)
constexpr uint8_t LEGACY_OBFUSCATION_KEY[] = {0x4B, 0x4F, 0x52, 0x65, 0x61, 0x64, 0x65, 0x72};
constexpr size_t LEGACY_KEY_LENGTH = sizeof(LEGACY_OBFUSCATION_KEY);

void legacyDeobfuscate(std::string& data) {
  for (size_t i = 0; i < data.size(); i++) {
    data[i] ^= LEGACY_OBFUSCATION_KEY[i % LEGACY_KEY_LENGTH];
  }
}

const std::string kEmptyString;
}  // namespace

bool KOReaderCredentialStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  const bool profilesSaved = JsonSettingsIO::saveKOReader(*this, KOREADER_PROFILES_FILE_JSON);
  if (!profilesSaved) {
    return false;
  }

  // Best-effort: keep the legacy mirror in sync too, but don't fail the whole save
  // over it -- cpr-vcodex's own state (the profiles file) is what actually matters here.
  if (!JsonSettingsIO::saveKOReaderLegacyMirror(*this, KOREADER_FILE_JSON)) {
    LOG_ERR("KRS", "Failed to update legacy koreader.json mirror");
  }
  return true;
}

bool KOReaderCredentialStore::loadFromFile() {
  const std::string tempPath = std::string(KOREADER_PROFILES_FILE_JSON) + ".tmp";
  if (!Storage.exists(KOREADER_PROFILES_FILE_JSON) && Storage.exists(tempPath.c_str())) {
    if (Storage.rename(tempPath.c_str(), KOREADER_PROFILES_FILE_JSON)) {
      LOG_DBG("KRS", "Recovered koreader_profiles.json from interrupted temp file");
    }
  }

  // The multi-profile store is authoritative once it exists.
  if (Storage.exists(KOREADER_PROFILES_FILE_JSON)) {
    String json = Storage.readFile(KOREADER_PROFILES_FILE_JSON);
    if (!json.isEmpty()) {
      bool resave = false;
      bool result = JsonSettingsIO::loadKOReader(*this, json.c_str(), &resave);
      if (result && resave) {
        saveToFile();
        LOG_DBG("KRS", "Resaved KOReader profiles to update format");
      }
      return result;
    }
  }

  // No profiles file yet -- one-time migration from the legacy single-record
  // koreader.json. That file is also kept around afterwards as a compatibility
  // mirror (see saveToFile()), so this path only runs once per device.
  if (Storage.exists(KOREADER_FILE_JSON)) {
    String json = Storage.readFile(KOREADER_FILE_JSON);
    if (!json.isEmpty()) {
      KOReaderProfile profile;
      profile.name = "Profile 1";
      if (JsonSettingsIO::loadKOReaderLegacyProfile(profile, json.c_str())) {
        profiles.clear();
        profiles.push_back(std::move(profile));
        activeIndex = 0;
        saveToFile();  // creates koreader_profiles.json; rewrites the legacy mirror in place
        LOG_DBG("KRS", "Migrated legacy koreader.json to multi-profile format");
        return true;
      }
    }
  }

  // Fall back to binary migration (pre-dates even the single-profile JSON format)
  if (Storage.exists(KOREADER_FILE_BIN)) {
    if (loadFromBinaryFile()) {
      if (saveToFile()) {
        Storage.rename(KOREADER_FILE_BIN, KOREADER_FILE_BAK);
        LOG_DBG("KRS", "Migrated koreader.bin to koreader_profiles.json");
        return true;
      } else {
        LOG_ERR("KRS", "Failed to save KOReader credentials during migration");
        return false;
      }
    }
  }

  LOG_DBG("KRS", "No credentials file found");
  return false;
}

bool KOReaderCredentialStore::loadFromBinaryFile() {
  HalFile file;
  if (!Storage.openFileForRead("KRS", KOREADER_FILE_BIN, file)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(file, version);
  if (version != KOREADER_FILE_VERSION) {
    LOG_DBG("KRS", "Unknown file version: %u", version);
    return false;
  }

  KOReaderProfile profile;
  profile.name = "Profile 1";

  if (file.available()) {
    serialization::readString(file, profile.username);
  }

  if (file.available()) {
    serialization::readString(file, profile.password);
    legacyDeobfuscate(profile.password);
  }

  if (file.available()) {
    serialization::readString(file, profile.serverUrl);
  }

  if (file.available()) {
    uint8_t method;
    serialization::readPod(file, method);
    profile.matchMethod = static_cast<DocumentMatchMethod>(method);
  }

  LOG_DBG("KRS", "Loaded KOReader credentials from binary for user: %s", profile.username.c_str());

  profiles.clear();
  profiles.push_back(std::move(profile));
  activeIndex = 0;
  return true;
}

void KOReaderCredentialStore::setCredentials(const std::string& user, const std::string& pass) {
  if (activeIndex < 0) {
    // No profile yet -- create the first one, mirroring the old singleton's
    // "just start using it" behaviour for callers that never explicitly
    // manage profiles (on-device settings, the web UI, etc.).
    KOReaderProfile profile;
    profile.name = "Profile 1";
    profiles.push_back(std::move(profile));
    activeIndex = static_cast<int>(profiles.size()) - 1;
  }
  profiles[static_cast<size_t>(activeIndex)].username = user;
  profiles[static_cast<size_t>(activeIndex)].password = pass;
  LOG_DBG("KRS", "Set credentials for user: %s", user.c_str());
}

const std::string& KOReaderCredentialStore::getUsername() const {
  return activeIndex >= 0 ? profiles[static_cast<size_t>(activeIndex)].username : kEmptyString;
}

const std::string& KOReaderCredentialStore::getPassword() const {
  return activeIndex >= 0 ? profiles[static_cast<size_t>(activeIndex)].password : kEmptyString;
}

std::string KOReaderCredentialStore::getMd5Password() const { return hashPassword(getPassword()); }

std::string KOReaderCredentialStore::hashPassword(const std::string& password) {
  if (password.empty()) {
    return "";
  }

  MD5Builder md5;
  md5.begin();
  md5.add(reinterpret_cast<const uint8_t*>(password.data()), password.size());
  md5.calculate();

  uint8_t digest[16];
  md5.getBytes(digest);

  char hex[33];
  constexpr char HEX_DIGITS[] = "0123456789abcdef";
  for (size_t i = 0; i < sizeof(digest); ++i) {
    hex[i * 2] = HEX_DIGITS[digest[i] >> 4];
    hex[i * 2 + 1] = HEX_DIGITS[digest[i] & 0x0F];
  }
  hex[32] = '\0';
  return std::string(hex);
}

bool KOReaderCredentialStore::hasCredentials() const { return !getUsername().empty() && !getPassword().empty(); }

void KOReaderCredentialStore::clearCredentials() {
  if (activeIndex >= 0) {
    profiles[static_cast<size_t>(activeIndex)].username.clear();
    profiles[static_cast<size_t>(activeIndex)].password.clear();
    saveToFile();
  }
  LOG_DBG("KRS", "Cleared KOReader credentials");
}

void KOReaderCredentialStore::setServerUrl(const std::string& url) {
  if (activeIndex < 0) {
    KOReaderProfile profile;
    profile.name = "Profile 1";
    profiles.push_back(std::move(profile));
    activeIndex = static_cast<int>(profiles.size()) - 1;
  }
  profiles[static_cast<size_t>(activeIndex)].serverUrl = url;
  LOG_DBG("KRS", "Set server URL: %s", url.empty() ? "(default)" : url.c_str());
}

const std::string& KOReaderCredentialStore::getServerUrl() const {
  return activeIndex >= 0 ? profiles[static_cast<size_t>(activeIndex)].serverUrl : kEmptyString;
}

std::string KOReaderCredentialStore::getBaseUrl() const { return resolveBaseUrl(getServerUrl()); }

bool KOReaderCredentialStore::usesCrossPointSyncServer() const {
  // Upstream's crosspoint-sync server (sync.crosspointreader.com) accepts a CrossPoint-
  // specific `position` extension; third-party KOSync servers must never receive it.
  static constexpr char CROSSPOINT_SYNC_SERVER_URL[] = "https://sync.crosspointreader.com";
  return getBaseUrl() == CROSSPOINT_SYNC_SERVER_URL;
}

std::string KOReaderCredentialStore::resolveBaseUrl(const std::string& serverUrl) {
  std::string url;
  if (serverUrl.empty()) {
    url = DEFAULT_SERVER_URL;
  } else if (serverUrl.find("://") == std::string::npos) {
    // Normalize URL: add http:// if no protocol specified (local servers typically don't have SSL)
    url = "http://" + serverUrl;
  } else {
    url = serverUrl;
  }

  // Strip trailing slashes to avoid double-slash in API paths
  while (!url.empty() && url.back() == '/') {
    url.pop_back();
  }

  return url;
}

void KOReaderCredentialStore::setMatchMethod(DocumentMatchMethod method) {
  if (activeIndex < 0) {
    KOReaderProfile profile;
    profile.name = "Profile 1";
    profiles.push_back(std::move(profile));
    activeIndex = static_cast<int>(profiles.size()) - 1;
  }
  profiles[static_cast<size_t>(activeIndex)].matchMethod = method;
  LOG_DBG("KRS", "Set match method: %s", method == DocumentMatchMethod::FILENAME ? "Filename" : "Binary");
}

DocumentMatchMethod KOReaderCredentialStore::getMatchMethod() const {
  return activeIndex >= 0 ? profiles[static_cast<size_t>(activeIndex)].matchMethod : DocumentMatchMethod::FILENAME;
}

void KOReaderCredentialStore::setSendMetadata(const bool enabled) {
  if (activeIndex < 0) return;
  profiles[static_cast<size_t>(activeIndex)].sendMetadata = enabled;
}

bool KOReaderCredentialStore::getSendMetadata() const {
  return activeIndex >= 0 && profiles[static_cast<size_t>(activeIndex)].sendMetadata;
}

void KOReaderCredentialStore::setSyncBehavior(KOReaderSyncBehavior behavior) {
  if (activeIndex < 0) return;
  if (static_cast<uint8_t>(behavior) > static_cast<uint8_t>(KOReaderSyncBehavior::SMART)) {
    behavior = KOReaderSyncBehavior::ASK_EVERY_TIME;
  }
  profiles[static_cast<size_t>(activeIndex)].syncBehavior = behavior;
}

KOReaderSyncBehavior KOReaderCredentialStore::getSyncBehavior() const {
  return activeIndex >= 0 ? profiles[static_cast<size_t>(activeIndex)].syncBehavior
                          : KOReaderSyncBehavior::ASK_EVERY_TIME;
}

bool KOReaderCredentialStore::addProfile(const KOReaderProfile& profile) {
  if (!canAddProfile()) {
    LOG_DBG("KRS", "Cannot add more profiles, limit of %zu reached", MAX_PROFILES);
    return false;
  }

  const int previousActiveIndex = activeIndex;
  profiles.push_back(profile);
  if (activeIndex < 0) {
    activeIndex = static_cast<int>(profiles.size()) - 1;
  }
  LOG_DBG("KRS", "Added profile: %s", profile.name.c_str());
  if (saveToFile()) {
    return true;
  }

  profiles.pop_back();
  activeIndex = previousActiveIndex;
  LOG_ERR("KRS", "Failed to save added profile, restored previous state");
  return false;
}

bool KOReaderCredentialStore::updateProfile(size_t index, const KOReaderProfile& profile) {
  if (index >= profiles.size()) {
    return false;
  }

  KOReaderProfile previousProfile = profiles[index];
  profiles[index] = profile;
  LOG_DBG("KRS", "Updated profile: %s", profile.name.c_str());
  if (saveToFile()) {
    return true;
  }

  profiles[index] = std::move(previousProfile);
  LOG_ERR("KRS", "Failed to save updated profile at index %zu, restored previous state", index);
  return false;
}

bool KOReaderCredentialStore::removeProfile(size_t index) {
  if (index >= profiles.size()) {
    return false;
  }

  KOReaderProfile removedProfile = profiles[index];
  const int previousActiveIndex = activeIndex;
  LOG_DBG("KRS", "Removed profile: %s", profiles[index].name.c_str());
  profiles.erase(profiles.begin() + static_cast<ptrdiff_t>(index));

  if (profiles.empty()) {
    activeIndex = -1;
  } else if (activeIndex >= 0) {
    if (static_cast<size_t>(activeIndex) == index) {
      activeIndex = 0;  // removed profile was active -- fall back to the first remaining one
    } else if (static_cast<size_t>(activeIndex) > index) {
      activeIndex--;  // active profile shifted down by one
    }
  }

  if (saveToFile()) {
    return true;
  }

  profiles.insert(profiles.begin() + static_cast<ptrdiff_t>(index), std::move(removedProfile));
  activeIndex = previousActiveIndex;
  LOG_ERR("KRS", "Failed to save profile removal at index %zu, restored previous state", index);
  return false;
}

const KOReaderProfile* KOReaderCredentialStore::getProfile(size_t index) const {
  if (index >= profiles.size()) {
    return nullptr;
  }
  return &profiles[index];
}

bool KOReaderCredentialStore::setActiveIndex(size_t index) {
  if (index >= profiles.size()) {
    return false;
  }
  const int previousActiveIndex = activeIndex;
  activeIndex = static_cast<int>(index);
  LOG_DBG("KRS", "Active profile set to: %s", profiles[index].name.c_str());
  if (saveToFile()) {
    return true;
  }

  activeIndex = previousActiveIndex;
  LOG_ERR("KRS", "Failed to save active profile, restored previous selection");
  return false;
}
