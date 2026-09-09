#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct DictionaryEntry {
  std::string languageId;
  std::string directoryPath;
  std::string ifoPath;
  std::string idxPath;
  std::string dictPath;
  std::string synPath;
  std::string cachePath;
  std::string name;
  std::string lang;
  std::string sameTypeSequence;
  uint32_t wordCount = 0;
  uint32_t idxFileSize = 0;
  bool compressed = false;
  bool missingFiles = false;
  mutable std::vector<uint32_t> checkpoints;
  mutable std::vector<uint32_t> ordinals;
  mutable uint32_t totalWords = 0;
};

struct DictionaryLookupResult {
  enum class Status { Found, NotFound, NoDictionary, NotReady, IoError, InvalidDictionary, OutOfMemory };

  Status status = Status::NotFound;
  std::string query;
  std::string headword;
  std::string definition;
  std::string dictionaryName;
  bool truncated = false;
  std::vector<std::string> suggestions;
};

class DictionaryStore {
 public:
  enum DefinitionTextSize : uint8_t { DEF_TEXT_SMALL = 0, DEF_TEXT_LARGE = 1, DEF_TEXT_SIZE_COUNT };

  static DictionaryStore& getInstance();

  void loadConfig();
  bool saveConfig() const;
  void scan();
  void ensureScanned();
  const std::vector<DictionaryEntry>& getEntries() const { return entries; }
  int getActiveIndex() const { return activeIndex; }
  bool setActiveIndex(int index);
  std::string getActiveLabel() const;
  uint8_t getDefinitionTextSize() const { return definitionTextSize; }
  bool setDefinitionTextSize(uint8_t size);
  int getDefinitionFontId(int readerFontId) const;
  bool prepareActive(const std::function<void(int percent)>& onProgress = nullptr);
  bool hasActiveDictionary() const;

  DictionaryLookupResult lookup(const std::string& rawWord, bool includeSuggestions = true);
  std::vector<std::string> getHistory();
  void addHistory(const std::string& word);
  void clearHistory();

  static std::string cleanWord(const std::string& word);
  static const char* lookupErrorMessage(DictionaryLookupResult::Status status);

 private:
  DictionaryStore() = default;

  struct IndexHit {
    std::string headword;
    uint32_t dictOffset = 0;
    uint32_t dictSize = 0;
  };

  static constexpr const char* DICTIONARY_ROOT = "/dictionaries";
  static constexpr const char* CONFIG_PATH = "/.crosspoint/dictionary_config.json";
  static constexpr const char* HISTORY_PATH = "/.crosspoint/dictionary_history.txt";
  static constexpr int CHECKPOINT_INTERVAL = 1024;
  static constexpr size_t MAX_CHECKPOINT_COUNT = 4096;
  static constexpr size_t MAX_SCAN_ENTRIES = 64;
  static constexpr size_t MAX_DEFINITION_BYTES = 8192;
  static constexpr size_t MIN_DEFINITION_BYTES = 1024;
  static constexpr size_t MAX_HISTORY_ITEMS = 15;

  std::vector<DictionaryEntry> entries;
  DictionaryEntry activeOnlyEntry;
  std::string activeIfoPath;
  int activeIndex = -1;
  bool configLoaded = false;
  bool configReadable = true;
  bool historyReadable = true;
  bool scanned = false;
  bool activeOnlyLoaded = false;
  uint8_t definitionTextSize = DEF_TEXT_SMALL;

  bool loadEntryFromIfoPath(const std::string& ifoPath, DictionaryEntry& entry) const;
  bool ensureActiveEntryLoaded();
  void clearActiveOnlyEntry();
  DictionaryEntry* activeEntry();
  const DictionaryEntry* activeEntry() const;
  bool ensurePrepared(DictionaryEntry& entry, const std::function<void(int percent)>& onProgress = nullptr,
                      DictionaryLookupResult::Status* error = nullptr);
  bool loadCheckpointCache(DictionaryEntry& entry);
  bool saveCheckpointCache(const DictionaryEntry& entry) const;
  bool findIndexHit(const DictionaryEntry& entry, const std::string& word, IndexHit& hit,
                    DictionaryLookupResult::Status* error = nullptr) const;
  bool lookupSynonym(const DictionaryEntry& entry, const std::string& word, std::string& canonical,
                     DictionaryLookupResult::Status* error = nullptr) const;
  std::string headwordAtOrdinal(const DictionaryEntry& entry, uint32_t ordinal,
                                DictionaryLookupResult::Status* error = nullptr) const;
  std::string readDefinition(const DictionaryEntry& entry, const IndexHit& hit, bool& truncated,
                             DictionaryLookupResult::Status& status) const;
  std::vector<std::string> findSuggestions(const DictionaryEntry& entry, const std::string& word, int maxResults) const;
  std::vector<std::string> getFallbackForms(const DictionaryEntry& entry, const std::string& word) const;
};

#define DICTIONARIES DictionaryStore::getInstance()
