#include <CrossPointSettings.h>
#include <Esp.h>
#include <HalStorage.h>
#include <Utf8.h>
#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <vector>

#include "activities/reader/DictionaryNavigation.h"

#define private public
#include "DictionaryStore.h"
#undef private

CrossPointSettings CrossPointSettings::instance;

namespace {
using Status = DictionaryLookupResult::Status;
constexpr const char* CONFIG = "/.crosspoint/dictionary_config.json";
constexpr const char* HISTORY = "/.crosspoint/dictionary_history.txt";

void append32(std::vector<uint8_t>& bytes, uint32_t value) {
  for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(static_cast<uint8_t>(value >> shift));
}

class DictionaryStoreTest : public testing::Test {
 protected:
  DictionaryStore& store = DictionaryStore::getInstance();
  void SetUp() override {
    Storage.reset();
    store = DictionaryStore{};
    store.configLoaded = store.scanned = true;
    dictionaryTestFreeHeap = 250000;
    dictionaryTestLargestBlock = 220000;
  }
  DictionaryEntry& dictionary(const std::vector<std::pair<std::string, std::string>>& words) {
    DictionaryEntry entry;
    entry.name = "Fixture";
    entry.languageId = "es";
    entry.ifoPath = "/dictionaries/es/fixture.ifo";
    entry.idxPath = "/dictionaries/es/fixture.idx";
    entry.dictPath = "/dictionaries/es/fixture.dict";
    entry.cachePath = "/dictionaries/es/fixture.cpridx";
    entry.sameTypeSequence = "m";
    std::vector<uint8_t> idx, data;
    for (const auto& [word, definition] : words) {
      idx.insert(idx.end(), word.begin(), word.end());
      idx.push_back(0);
      append32(idx, static_cast<uint32_t>(data.size()));
      append32(idx, static_cast<uint32_t>(definition.size()));
      data.insert(data.end(), definition.begin(), definition.end());
    }
    entry.idxFileSize = static_cast<uint32_t>(idx.size());
    entry.wordCount = static_cast<uint32_t>(words.size());
    Storage.put(entry.idxPath, std::move(idx));
    Storage.put(entry.dictPath, std::move(data));
    store.entries.push_back(std::move(entry));
    store.activeIndex = 0;
    store.activeIfoPath = store.entries[0].ifoPath;
    return store.entries[0];
  }
  void expectNoNewHistory() { EXPECT_FALSE(Storage.exists(HISTORY)); }
};

TEST_F(DictionaryStoreTest, ExactSynonymAndSpanishFormsKeepWorking) {
  auto& entry = dictionary({{"casa", "Un hogar."}, {"perro", "Un animal."}, {"saltar", "Dar un salto."}});
  entry.synPath = "/dictionaries/es/fixture.syn";
  std::vector<uint8_t> syn = {'c', 'a', 'n', 0};
  append32(syn, 1);
  Storage.put(entry.synPath, std::move(syn));
  EXPECT_EQ(store.lookup("casa").status, Status::Found);
  const auto plural = store.lookup("«Perros»");
  EXPECT_EQ(plural.status, Status::Found);
  EXPECT_EQ(plural.headword, "perro");
  const auto alias = store.lookup("can");
  EXPECT_EQ(alias.status, Status::Found);
  EXPECT_EQ(alias.definition, "Un animal.");
  EXPECT_EQ(store.lookup("saltarse").headword, "saltar");
  const auto before = Storage.readFile(HISTORY);
  EXPECT_EQ(store.lookup("zzzz", false).status, Status::NotFound);
  EXPECT_EQ(Storage.readFile(HISTORY), before);
}

TEST_F(DictionaryStoreTest, MissingDefinitionAndSignedReadAreIoErrorsWithoutHistory) {
  auto& entry = dictionary({{"test", "Definition"}});
  Storage.failOpen.insert(entry.dictPath);
  EXPECT_EQ(store.lookup("test").status, Status::IoError);
  expectNoNewHistory();
  Storage.failOpen.clear();
  Storage.files[entry.dictPath]->failReadAt = 0;
  EXPECT_EQ(store.lookup("test").status, Status::IoError);
  expectNoNewHistory();
  Storage.files[entry.dictPath]->failReadAt = std::numeric_limits<size_t>::max();
  EXPECT_EQ(store.lookup("test").status, Status::Found);
}

TEST_F(DictionaryStoreTest, MalformedOrTruncatedIndexIsNotCachedAsSuccess) {
  auto& entry = dictionary({{"test", "Definition"}});
  Storage.files[entry.idxPath]->bytes.pop_back();
  EXPECT_EQ(store.lookup("test").status, Status::InvalidDictionary);
  EXPECT_TRUE(entry.checkpoints.empty());
  EXPECT_FALSE(Storage.exists(entry.cachePath.c_str()));
  expectNoNewHistory();
}

TEST_F(DictionaryStoreTest, IndexIoFailureCanBeRetriedWithoutPoisoningCheckpoints) {
  auto& entry = dictionary({{"test", "Definition"}});
  auto idx = Storage.files[entry.idxPath];
  idx->failReadAt = 3;
  EXPECT_EQ(store.lookup("test").status, Status::IoError);
  EXPECT_TRUE(entry.checkpoints.empty());
  idx->failReadAt = std::numeric_limits<size_t>::max();
  EXPECT_EQ(store.lookup("test").status, Status::Found);
}

TEST_F(DictionaryStoreTest, DefinitionRangeAndEmptyPayloadAreInvalid) {
  auto& entry = dictionary({{"test", "Definition"}});
  Storage.files[entry.dictPath]->bytes.pop_back();
  EXPECT_EQ(store.lookup("test").status, Status::InvalidDictionary);
  expectNoNewHistory();
  store.entries.clear();
  dictionary({{"test", ""}});
  EXPECT_EQ(store.lookup("test").status, Status::InvalidDictionary);
  expectNoNewHistory();
}

TEST_F(DictionaryStoreTest, MalformedBinaryFieldCannotMasqueradeAsPartialSuccess) {
  std::string raw("mok\0W", 5);
  raw.append(4, static_cast<char>(0xFF));
  auto& entry = dictionary({{"test", raw}});
  entry.sameTypeSequence.clear();
  EXPECT_EQ(store.lookup("test").status, Status::InvalidDictionary);
  expectNoNewHistory();
}

TEST_F(DictionaryStoreTest, LastBinaryFieldWithSameTypeSequenceKeepsTheText) {
  const std::string raw("Meaning\0RIFFdata", 16);
  auto& entry = dictionary({{"test", raw}});
  entry.sameTypeSequence = "mW";
  const auto lookup = store.lookup("test");
  EXPECT_EQ(lookup.status, Status::Found);
  EXPECT_EQ(lookup.definition, "Meaning");
}

TEST_F(DictionaryStoreTest, LowHeapAndFragmentationAreDistinctFromNotFound) {
  dictionary({{"test", "Definition"}});
  dictionaryTestFreeHeap = 1024;
  EXPECT_EQ(store.lookup("test").status, Status::OutOfMemory);
  expectNoNewHistory();
  dictionaryTestFreeHeap = 250000;
  ASSERT_TRUE(store.prepareActive());
  dictionaryTestLargestBlock = 2000;
  EXPECT_EQ(store.lookup("test").status, Status::OutOfMemory);
  expectNoNewHistory();
  dictionaryTestLargestBlock = 220000;
  EXPECT_EQ(store.lookup("test").status, Status::Found);
}

TEST_F(DictionaryStoreTest, DefinitionCapKeepsUtf8BoundaryAndValidPrefix) {
  dictionary({{"test", std::string(8191, 'a') + "€tail"}});
  auto result = store.lookup("test");
  EXPECT_EQ(result.status, Status::Found);
  EXPECT_TRUE(result.truncated);
  EXPECT_LE(result.definition.size(), 8192);
  EXPECT_EQ(utf8SafeTruncateBuffer(result.definition.data(), static_cast<int>(result.definition.size())),
            result.definition.size());
}

TEST_F(DictionaryStoreTest, LegacyConfigurationVersionsKeepTheirSettings) {
  for (int version = 0; version <= 4; ++version) {
    Storage.reset();
    Storage.put(CONFIG, "{\"activeIfoPath\":\"/dictionaries/es/old.ifo\",\"definitionTextSize\":" +
                            std::to_string(version < 2   ? 2
                                           : version < 4 ? 3
                                                         : 1) +
                            ",\"definitionTextSizeVersion\":" + std::to_string(version) + "}");
    store.loadConfig();
    EXPECT_EQ(store.activeIfoPath, "/dictionaries/es/old.ifo");
    EXPECT_EQ(store.getDefinitionTextSize(), DictionaryStore::DEF_TEXT_LARGE);
  }
}

TEST_F(DictionaryStoreTest, FailedConfigPromotionPreservesDiskAndInMemorySelection) {
  dictionary({{"test", "Definition"}});
  ASSERT_TRUE(store.saveConfig());
  const auto old = Storage.readFile(CONFIG);
  Storage.failRenameFrom.insert(std::string(CONFIG) + ".tmp");
  EXPECT_FALSE(store.setDefinitionTextSize(DictionaryStore::DEF_TEXT_LARGE));
  EXPECT_EQ(store.getDefinitionTextSize(), DictionaryStore::DEF_TEXT_SMALL);
  EXPECT_EQ(Storage.readFile(CONFIG), old);
  Storage.failRenameFrom.clear();
  Storage.nextWriteLimit = 8;
  EXPECT_FALSE(store.setDefinitionTextSize(DictionaryStore::DEF_TEXT_LARGE));
  EXPECT_EQ(Storage.readFile(CONFIG), old);
}

TEST_F(DictionaryStoreTest, CorruptConfigIsNeverOverwrittenWithDefaults) {
  Storage.put(CONFIG, "{truncated");
  store.loadConfig();
  EXPECT_FALSE(store.saveConfig());
  EXPECT_EQ(Storage.readFile(CONFIG), "{truncated");
  Storage.put(std::string(CONFIG) + ".bak",
              "{\"activeIfoPath\":\"old.ifo\",\"definitionTextSize\":1,\"definitionTextSizeVersion\":4}");
  store.loadConfig();
  EXPECT_EQ(store.activeIfoPath, "old.ifo");
  EXPECT_EQ(store.getDefinitionTextSize(), DictionaryStore::DEF_TEXT_LARGE);
}

TEST_F(DictionaryStoreTest, HistoryFailureDoesNotReplaceExistingHistory) {
  Storage.put(HISTORY, "uno\ndos\n");
  Storage.nextWriteLimit = 3;
  store.addHistory("tres");
  EXPECT_EQ(Storage.readFile(HISTORY), "uno\ndos\n");
  Storage.files[HISTORY]->failReadAt = 4;
  store.addHistory("tres");
  EXPECT_EQ(Storage.readFile(HISTORY), "uno\ndos\n");
}

TEST_F(DictionaryStoreTest, HistoryRecoveryAndExplicitClearDoNotResurrectEntries) {
  Storage.put(std::string(HISTORY) + ".bak", "uno\n");
  EXPECT_EQ(store.getHistory(), std::vector<std::string>{"uno"});
  store.addHistory("dos");
  store.clearHistory();
  EXPECT_TRUE(store.getHistory().empty());
}

TEST(DictionaryNavigationTest, EightQueriesAndPagesAreBoundedWithoutDefinitions) {
  auto trail = std::make_unique<DictionaryNavigation::Trail>();
  EXPECT_TRUE(trail->push("first", 3));
  for (int i = 1; i < 8; ++i) EXPECT_TRUE(trail->push(("query" + std::to_string(i)).c_str(), i + 3));
  EXPECT_EQ(trail->size(), 8);
  EXPECT_FALSE(trail->push("ninth"));
  for (int i = 0; i < 20; ++i) {
    ASSERT_NE(trail->previous(), nullptr);
    EXPECT_EQ(trail->previous()->page, 9);
    EXPECT_TRUE(trail->pop());
    EXPECT_TRUE(trail->push("replacement", 17));
    EXPECT_EQ(trail->size(), 8);
  }
  while (trail->pop()) {
  }
  ASSERT_NE(trail->current(), nullptr);
  EXPECT_STREQ(trail->current()->query, "first");
  EXPECT_EQ(trail->current()->page, 3);
  EXPECT_EQ(trail->previous(), nullptr);
}

TEST(DictionaryNavigationTest, LongQueryIsRejectedWithoutChangingExistingTrail) {
  auto trail = std::make_unique<DictionaryNavigation::Trail>();
  const std::string longest(255, 'a');
  EXPECT_TRUE(trail->push(longest.c_str()));
  EXPECT_FALSE(trail->push((longest + 'b').c_str()));
  EXPECT_EQ(trail->size(), 1);
  EXPECT_STREQ(trail->current()->query, longest.c_str());
}

TEST(DictionaryNavigationTest, SelectionSpansKeepUtf8AndCombiningMarksIntact) {
  const std::string line = "  café, e\xCC\x81 ไทย 中文 -- hola";
  std::vector<std::string> words;
  size_t cursor = 0;
  DictionaryNavigation::WordSpan span;
  while (DictionaryNavigation::nextWord(line, cursor, span)) words.push_back(line.substr(span.offset, span.length));
  EXPECT_EQ(words, (std::vector<std::string>{"café", "e\xCC\x81", "ไทย", "中", "文", "hola"}));
  EXPECT_EQ(cursor, line.size());
}
}  // namespace
