#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Document matching method for KOReader sync
enum class DocumentMatchMethod : uint8_t {
  FILENAME = 0,  // Match by filename (simpler, works across different file sources)
  BINARY = 1,    // Match by partial MD5 of file content (more accurate, but files must be identical)
};

enum class KOReaderSyncBehavior : uint8_t {
  ASK_EVERY_TIME = 0,
  SMART = 1,
};

// One saved KoReader server credential set.
struct KOReaderProfile {
  std::string name;
  std::string username;
  std::string password;   // Plaintext in memory; obfuscated with hardware key on disk
  std::string serverUrl;  // Custom sync server URL (empty = default)
  DocumentMatchMethod matchMethod = DocumentMatchMethod::FILENAME;  // Default to filename for compatibility
  bool sendMetadata = false;
  KOReaderSyncBehavior syncBehavior = KOReaderSyncBehavior::ASK_EVERY_TIME;
};

class KOReaderCredentialStore;
namespace JsonSettingsIO {
bool saveKOReader(const KOReaderCredentialStore& store, const char* path);
bool loadKOReader(KOReaderCredentialStore& store, const char* json, bool* needsResave);
}  // namespace JsonSettingsIO

/**
 * Singleton class for storing KOReader sync credential profiles on the SD card.
 * Holds a list of profiles plus an "active" index; all pre-existing accessors
 * (getUsername(), getPassword(), etc.) read through to whichever profile is
 * active, so code written before multi-profile support needs no changes.
 * Passwords are XOR-obfuscated with the device's unique hardware MAC address
 * and base64-encoded before writing to JSON (not cryptographically secure,
 * but prevents casual reading and ties credentials to the specific device).
 */
class KOReaderCredentialStore {
 private:
  static KOReaderCredentialStore instance;
  std::vector<KOReaderProfile> profiles;
  int activeIndex = -1;  // -1 = no profile saved yet

  static constexpr size_t MAX_PROFILES = 8;

  // Private constructor for singleton
  KOReaderCredentialStore() = default;

  bool loadFromBinaryFile();

  friend bool JsonSettingsIO::saveKOReader(const KOReaderCredentialStore&, const char*);
  friend bool JsonSettingsIO::loadKOReader(KOReaderCredentialStore&, const char*, bool*);

 public:
  // Delete copy constructor and assignment
  KOReaderCredentialStore(const KOReaderCredentialStore&) = delete;
  KOReaderCredentialStore& operator=(const KOReaderCredentialStore&) = delete;

  // Get singleton instance
  static KOReaderCredentialStore& getInstance() { return instance; }

  // Save/load from SD card
  bool saveToFile() const;
  bool loadFromFile();

  // --- Active-profile accessors -- unchanged signatures, existing callers keep working ---

  // Credential management (active profile; creates a first profile on first use if none exists)
  void setCredentials(const std::string& user, const std::string& pass);
  const std::string& getUsername() const;
  const std::string& getPassword() const;

  // Get MD5 hash of password for API authentication
  std::string getMd5Password() const;

  // Check if credentials are set on the active profile
  bool hasCredentials() const;

  // Clear credentials on the active profile
  void clearCredentials();

  // Server URL management (active profile)
  void setServerUrl(const std::string& url);
  const std::string& getServerUrl() const;

  // Get base URL for API calls (with http:// normalization if no protocol, falls back to default)
  std::string getBaseUrl() const;

  // Document matching method (active profile)
  void setMatchMethod(DocumentMatchMethod method);
  DocumentMatchMethod getMatchMethod() const;

  void setSendMetadata(bool enabled);
  bool getSendMetadata() const;
  void setSyncBehavior(KOReaderSyncBehavior behavior);
  KOReaderSyncBehavior getSyncBehavior() const;

  // Whether API calls target the CrossPoint sync server that supports protocol
  // extensions (rich `position` payloads). Upstream parity; the fork default stays
  // sync.koreader.rocks, so this is only true for an explicitly configured profile.
  bool usesCrossPointSyncServer() const;

  static std::string hashPassword(const std::string& password);
  static std::string resolveBaseUrl(const std::string& serverUrl);

  // --- Multi-profile management ---

  bool addProfile(const KOReaderProfile& profile);
  bool updateProfile(size_t index, const KOReaderProfile& profile);
  bool removeProfile(size_t index);

  const std::vector<KOReaderProfile>& getProfiles() const { return profiles; }
  const KOReaderProfile* getProfile(size_t index) const;
  size_t getCount() const { return profiles.size(); }
  bool hasProfiles() const { return !profiles.empty(); }
  bool canAddProfile() const { return profiles.size() < MAX_PROFILES; }

  // Index of the currently active profile, or -1 if none saved yet.
  int getActiveIndex() const { return activeIndex; }
  // Sets the active profile and persists the choice as the new default.
  bool setActiveIndex(size_t index);
};

// Helper macro to access credential store
#define KOREADER_STORE KOReaderCredentialStore::getInstance()
