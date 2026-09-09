#include <MemoryBudget.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <numeric>
#include <random>

#include "Epub/CumulativeSizeCache.h"
#include "Epub/css/CssParser.h"
#include "Epub/parsers/OpfItemIndex.h"

namespace {
class EpubMemory : public testing::Test {
 protected:
  void SetUp() override {
    Storage.files.clear();
    MemoryBudget::heap = {1024 * 1024, 1024 * 1024};
    MemoryBudget::successfulSnapshots = SIZE_MAX;
    FsFile::writeBudget = FsFile::readBudget = SIZE_MAX;
    FsFile::maxRead = 0;
    FsFile::failClose = false;
  }
  static FsFile source(const std::string& text) {
    Storage.files["source"] = {text.begin(), text.end()};
    FsFile file;
    Storage.openFileForRead("", "source", file);
    return file;
  }
};

TEST_F(EpubMemory, CssStopsBeforeMapGrowthAndDoesNotPersistPartialRules) {
  CssParser parser("/book");
  auto file = source("p {text-align:center} .x {font-weight:bold}");
  MemoryBudget::successfulSnapshots = 1;
  EXPECT_FALSE(parser.loadFromStream(file));
  EXPECT_EQ(parser.ruleCount(), 1u);
  EXPECT_FALSE(parser.isComplete());
  EXPECT_FALSE(parser.saveToCache());
  EXPECT_FALSE(Storage.exists("/book/css_rules.cache"));
  MemoryBudget::successfulSnapshots = SIZE_MAX;
  EXPECT_EQ(parser.resolveStyle("p", "").textAlign, CssTextAlign::Center);
  EXPECT_FALSE(parser.loadFromCache());
  EXPECT_EQ(parser.ruleCount(), 1u);
}

TEST_F(EpubMemory, CssSkippedStylesheetStillAllowsOtherRulesWithoutCompleteCache) {
  CssParser parser("/book");
  parser.markIncomplete();
  auto file = source("p {text-align:right}");
  EXPECT_FALSE(parser.loadFromStream(file));
  EXPECT_EQ(parser.ruleCount(), 1u);
  EXPECT_EQ(parser.resolveStyle("p", "").textAlign, CssTextAlign::Right);
  EXPECT_FALSE(parser.saveToCache());
}

TEST_F(EpubMemory, CssValidCacheRoundTripRetainsFormat) {
  CssParser parser("/book");
  auto file = source("p {text-align:center} .x {font-weight:bold; font-variant-caps:small-caps} p {font-style:italic}");
  ASSERT_TRUE(parser.loadFromStream(file));
  ASSERT_TRUE(parser.saveToCache());
  EXPECT_EQ(Storage.files["/book/css_rules.cache"][0], CssParser::CSS_CACHE_VERSION);
  CssParser restored("/book");
  ASSERT_TRUE(restored.loadFromCache());
  EXPECT_EQ(restored.ruleCount(), 2u);
  const auto style = restored.resolveStyle("p", "x");
  EXPECT_EQ(style.textAlign, CssTextAlign::Center);
  EXPECT_EQ(style.fontWeight, CssFontWeight::Bold);
  EXPECT_EQ(style.fontStyle, CssFontStyle::Italic);
  EXPECT_TRUE(style.hasFontVariantCaps());
  EXPECT_EQ(style.fontVariantCaps, CssFontVariantCaps::SmallCaps);
}

TEST_F(EpubMemory, CssSmallCapsAcceptsShorthandImportantAndExplicitNormal) {
  const auto small = CssParser::parseInlineStyle("font-variant: small-caps !important");
  EXPECT_TRUE(small.hasFontVariantCaps());
  EXPECT_EQ(small.fontVariantCaps, CssFontVariantCaps::SmallCaps);
  const auto normal = CssParser::parseInlineStyle("font-variant-caps: normal");
  EXPECT_TRUE(normal.hasFontVariantCaps());
  EXPECT_EQ(normal.fontVariantCaps, CssFontVariantCaps::Normal);
}

TEST_F(EpubMemory, CssShortWriteAndFailedClosePreservePreviousCache) {
  CssParser parser("/book");
  auto file = source("p {text-align:center}");
  ASSERT_TRUE(parser.loadFromStream(file));
  ASSERT_TRUE(parser.saveToCache());
  const auto original = Storage.files["/book/css_rules.cache"];
  FsFile::writeBudget = 10;
  EXPECT_FALSE(parser.saveToCache());
  EXPECT_EQ(Storage.files["/book/css_rules.cache"], original);
  FsFile::writeBudget = SIZE_MAX;
  FsFile::failClose = true;
  EXPECT_FALSE(parser.saveToCache());
  EXPECT_EQ(Storage.files["/book/css_rules.cache"], original);
}

TEST_F(EpubMemory, CssCacheLowHeapRetainsOnlySessionPrefix) {
  CssParser parser("/book");
  auto file = source("p {text-align:center} .x {font-weight:bold}");
  ASSERT_TRUE(parser.loadFromStream(file));
  ASSERT_TRUE(parser.saveToCache());
  CssParser restored("/book");
  // One reserve and one insertion succeed; the second insertion is refused.
  MemoryBudget::successfulSnapshots = 2;
  EXPECT_FALSE(restored.loadFromCache());
  EXPECT_EQ(restored.ruleCount(), 1u);
  EXPECT_FALSE(restored.saveToCache());
  EXPECT_TRUE(Storage.exists("/book/css_rules.cache"));
}

TEST_F(EpubMemory, CssTruncationOversizedBuffersAndReadFailureNeverBecomeComplete) {
  for (const auto& text :
       {std::string("p {text-align:center"), std::string("/* unfinished"),
        std::string(2000, 'p') + " {font-weight:bold}", std::string("p {") + std::string(2000, 'x') + ":bold}"}) {
    CssParser parser("/book");
    auto file = source(text);
    EXPECT_FALSE(parser.loadFromStream(file));
    EXPECT_FALSE(parser.saveToCache());
  }
  CssParser parser("/book");
  auto file = source("p {text-align:center}");
  FsFile::readBudget = 5;
  EXPECT_FALSE(parser.loadFromStream(file));
  EXPECT_FALSE(parser.saveToCache());
}

size_t allocationsLeft = SIZE_MAX;
void* allocateWithFailure(size_t bytes) {
  if (!allocationsLeft) return nullptr;
  if (allocationsLeft != SIZE_MAX) --allocationsLeft;
  return std::malloc(bytes);
}

TEST_F(EpubMemory, ChunkedIndexSortsAndSearchesAcrossManyChunkBoundaries) {
  OpfItemIndex index;
  std::vector<OpfItemIndex::Entry> expected;
  std::mt19937 random(31234);
  for (size_t i = 0; i < 5000; ++i) {
    OpfItemIndex::Entry entry{static_cast<uint32_t>(random() % 400), static_cast<uint32_t>(random() % 100),
                              static_cast<uint32_t>(i)};
    ASSERT_TRUE(index.append(entry));
    expected.push_back(entry);
  }
  index.sort();
  std::sort(expected.begin(), expected.end(), OpfItemIndex::less);
  ASSERT_EQ(index.size(), expected.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(index[i].idHash, expected[i].idHash);
    EXPECT_EQ(index[i].idLen, expected[i].idLen);
    const auto lower =
        std::lower_bound(expected.begin(), expected.end(), expected[i], OpfItemIndex::less) - expected.begin();
    EXPECT_EQ(index.lowerBound(expected[i].idHash, expected[i].idLen), static_cast<size_t>(lower));
  }
}

TEST_F(EpubMemory, IndexAllocationFailuresPreservePriorEntriesAndCanRetry) {
  for (size_t failAt : {size_t{0}, size_t{1}, size_t{2}, size_t{17}}) {
    allocationsLeft = failAt;
    OpfItemIndex index(allocateWithFailure);
    size_t added = 0;
    while (index.append({static_cast<uint32_t>(added), 5, static_cast<uint32_t>(added)})) ++added;
    EXPECT_EQ(index.size(), added);
    for (size_t i = 0; i < added; ++i) EXPECT_EQ(index[i].fileOffset, i);
    allocationsLeft = SIZE_MAX;
    ASSERT_TRUE(index.append({123456, 8, 42}));
    EXPECT_EQ(index.size(), added + 1);
  }
}

TEST_F(EpubMemory, StoredIdsAreComparedFullyWithBoundedReadsIncludingCollisions) {
  const std::string id(70000, 'x');
  std::vector<uint8_t> bytes(4 + id.size());
  const uint32_t length = static_cast<uint32_t>(id.size());
  std::memcpy(bytes.data(), &length, 4);
  std::memcpy(bytes.data() + 4, id.data(), id.size());
  FsFile file;
  file.open(bytes, false);
  bool matches = false;
  ASSERT_TRUE(readStoredOpfId(file, id, matches));
  EXPECT_TRUE(matches);
  EXPECT_LE(FsFile::maxRead, 64u);
  std::string collision = id;
  collision.back() = 'y';
  file.seek(0);
  ASSERT_TRUE(readStoredOpfId(file, collision, matches));
  EXPECT_FALSE(matches);
  bytes.pop_back();
  file.seek(0);
  EXPECT_FALSE(readStoredOpfId(file, id, matches));
}

TEST_F(EpubMemory, CumulativeCacheCaps1024AndFallsBackOn1025OrAllocationFailure) {
  for (size_t count : {size_t{1024}, size_t{1025}}) {
    CumulativeSizeCache cache;
    size_t reads = 0;
    const auto read = [&](size_t i, uint32_t& value) {
      ++reads;
      value = static_cast<uint32_t>(i * 123);
      return true;
    };
    uint32_t value = 0;
    ASSERT_TRUE(cache.get(count - 1, count, read, value));
    EXPECT_EQ(value, (count - 1) * 123);
    EXPECT_EQ(cache.cachedEntries(), count == 1024 ? count : 0u);
    EXPECT_EQ(reads, count == 1024 ? count : 1u);
    ASSERT_TRUE(cache.get(0, count, read, value));
    EXPECT_EQ(value, 0u);
    EXPECT_EQ(reads, count == 1024 ? count : 2u);
  }
  allocationsLeft = 0;
  CumulativeSizeCache noMemory(allocateWithFailure);
  uint32_t value = 0;
  EXPECT_TRUE(noMemory.get(
      4, 10,
      [](size_t i, uint32_t& value) {
        value = static_cast<uint32_t>(i + 1);
        return true;
      },
      value));
  EXPECT_EQ(value, 5u);
  EXPECT_EQ(noMemory.cachedEntries(), 0u);
}

TEST_F(EpubMemory, CumulativeLookupSkipsLongHrefWithoutAllocationOrLargeRead) {
  std::vector<uint8_t> bytes(4 + 4 + 30000 + 4 + 2, 0);
  const uint32_t entry = 4, hrefLength = 30000, expected = 123456;
  std::memcpy(bytes.data(), &entry, 4);
  std::memcpy(bytes.data() + 4, &hrefLength, 4);
  std::memcpy(bytes.data() + 8 + hrefLength, &expected, 4);
  FsFile file;
  file.open(bytes, false);
  uint32_t value = 0;
  ASSERT_TRUE(readCumulativeSizeFromLut(file, 0, 0, 4, value));
  EXPECT_EQ(value, expected);
  EXPECT_LE(FsFile::maxRead, 4u);
  const uint32_t invalid = UINT32_MAX;
  std::memcpy(bytes.data() + 4, &invalid, 4);
  EXPECT_FALSE(readCumulativeSizeFromLut(file, 0, 0, 4, value));
  EXPECT_FALSE(readCumulativeSizeFromLut(file, 0, 999999, 4, value));
}
}  // namespace
