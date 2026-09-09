#include <FileIndex.h>
#include <FileIndexNaturalSort.h>
#include <Memory.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace {
bool acceptAll(const char* name, bool, const void*) { return name[0] != '.'; }
struct Filter {
  bool hidden;
  std::string extension;
};
bool acceptFiltered(const char* name, bool dir, const void* raw) {
  const auto& filter = *static_cast<const Filter*>(raw);
  const std::string value{name};
  return (filter.hidden || name[0] != '.') && (dir || (value.size() >= filter.extension.size() &&
                                                       value.compare(value.size() - filter.extension.size(),
                                                                     filter.extension.size(), filter.extension) == 0));
}
std::string displayEntry(FileIndex& index, size_t row) {
  FileIndex::Entry entry{};
  if (!index.entryAt(row, entry)) return "<failed>";
  return std::string(entry.name) + (entry.isDir ? "/" : "");
}
std::string cachePath() {
  for (const auto& [path, node] : Storage.nodes) {
    if (path.rfind("/.crosspoint/fileindex-cpr/", 0) == 0 && path.ends_with(".idx")) return path;
  }
  return {};
}
void write32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
  memcpy(bytes.data() + offset, &value, sizeof(value));
}
class FileIndexTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Storage.reset();
    Storage.add("/library", true);
    allocationBudget = -1;
    largestAllocation = 0;
  }
  void TearDown() override {
    for (const auto& [path, count] : Storage.handles) EXPECT_EQ(count, 0) << path;
  }
};

TEST_F(FileIndexTest, LargeDirectoryUsesBoundedScratchAndExactNumericOrder) {
  Storage.add("/library/folder10", true);
  Storage.add("/library/folder2", true);
  for (int i = 4000; i > 0; --i) Storage.add("/library/book" + std::to_string(i) + ".epub");
  Storage.add("/.crosspoint/settings.json", false, "keep configuration");
  Storage.add("/.crosspoint/progress.bin", false, "keep progress");
  const auto settings = Storage.nodes.at("/.crosspoint/settings.json").bytes;
  const auto progress = Storage.nodes.at("/.crosspoint/progress.bin").bytes;
  FileIndex index;
  ASSERT_TRUE(index.open("/library", acceptAll));
  ASSERT_EQ(index.totalCount(), 4002u);
  EXPECT_EQ(displayEntry(index, 0), "folder2/");
  EXPECT_EQ(displayEntry(index, 1), "folder10/");
  for (size_t i = 1; i <= 4000; ++i) EXPECT_EQ(displayEntry(index, i + 1), "book" + std::to_string(i) + ".epub");
  EXPECT_EQ(index.findRowByName("book1977.epub"), 1978u);
  EXPECT_EQ(index.findRowByName("missing.epub"), SIZE_MAX);
  // The production builder allocates a 64 x 32-byte run plus small name/state buffers.
  EXPECT_LE(largestAllocation, 2048u);
  EXPECT_LE(Storage.largestRead, 2048u);
  EXPECT_EQ(Storage.nodes.at("/.crosspoint/settings.json").bytes, settings);
  EXPECT_EQ(Storage.nodes.at("/.crosspoint/progress.bin").bytes, progress);
}

TEST_F(FileIndexTest, LongCommonPrefixesMergeBeyondOneTieSegment) {
  const std::string prefix = "A very long common book title exceeding the sort key ";
  for (int i = 200; i > 0; --i) Storage.add("/library/" + prefix + std::to_string(i) + ".epub");
  FileIndex index;
  ASSERT_TRUE(index.open("/library", acceptAll));
  for (size_t i = 1; i <= 200; ++i) EXPECT_EQ(displayEntry(index, i - 1), prefix + std::to_string(i) + ".epub");
}

TEST_F(FileIndexTest, PreservesDirectorySlashOrderingCaseAndLeadingZeroEquivalence) {
  Storage.add("/library/d", true);
  Storage.add("/library/d.", true);
  Storage.add("/library/d0", true);
  Storage.add("/library/d00", true);
  Storage.add("/library/Book10.epub");
  Storage.add("/library/book02.epub");
  Storage.add("/library/BOOK2.epub");
  FileIndex index;
  ASSERT_TRUE(index.open("/library", acceptAll));
  EXPECT_EQ(displayEntry(index, 0), "d./");
  EXPECT_EQ(displayEntry(index, 1), "d/");
  EXPECT_EQ(displayEntry(index, 2), "d0/");
  EXPECT_EQ(displayEntry(index, 3), "d00/");
  EXPECT_EQ(displayEntry(index, 4), "BOOK2.epub");
  EXPECT_EQ(displayEntry(index, 5), "book02.epub");
  EXPECT_EQ(displayEntry(index, 6), "Book10.epub");
  EXPECT_EQ(FileIndexSort::naturalCompare("a0002", "A2"), 0);
  std::string shorter = "a" + std::string(256, '9');
  std::string longer = "a1" + std::string(256, '0');
  std::array<uint8_t, 28> a{}, b{};
  FileIndexSort::naturalSortKey(shorter.c_str(), a.data(), a.size());
  FileIndexSort::naturalSortKey(longer.c_str(), b.data(), b.size());
  EXPECT_LT(memcmp(a.data(), b.data(), a.size()), 0);
}

TEST_F(FileIndexTest, ReusesIndexWithoutWritesAndRebuildsAfterRename) {
  Storage.add("/library/book1.epub");
  FileIndex index;
  ASSERT_TRUE(index.open("/library", acceptAll));
  index.close();
  Storage.writeCalls = 0;
  Storage.rejectWrites = true;
  allocationBudget = 0;  // the retained name and offset buffers are sufficient on reopen
  ASSERT_TRUE(index.open("/library", acceptAll));
  EXPECT_EQ(Storage.writeCalls, 0u);
  EXPECT_EQ(displayEntry(index, 0), "book1.epub");
  index.close();
  Storage.rejectWrites = false;
  allocationBudget = -1;
  ASSERT_TRUE(Storage.rename("/library/book1.epub", "/library/book2.epub"));
  ASSERT_TRUE(index.open("/library", acceptAll));
  EXPECT_GT(Storage.writeCalls, 0u);
  EXPECT_EQ(displayEntry(index, 0), "book2.epub");
}

TEST_F(FileIndexTest, FiltersHaveIndependentCachesAndHiddenChangesAreRescanned) {
  Storage.add("/library/books", true);
  Storage.add("/library/book.epub");
  Storage.add("/library/.private.epub");
  Storage.add("/library/firmware.bin");
  Storage.add("/library/picture.png");
  Filter books{false, ".epub"}, firmware{false, ".bin"}, images{false, ".png"};
  FileIndex browser, picker, viewer;
  ASSERT_TRUE(browser.open("/library", acceptFiltered, &books, 1));
  ASSERT_TRUE(picker.open("/library", acceptFiltered, &firmware, 3));
  ASSERT_TRUE(viewer.open("/library", acceptFiltered, &images, 5));
  EXPECT_EQ(displayEntry(browser, 1), "book.epub");
  EXPECT_EQ(displayEntry(picker, 1), "firmware.bin");
  EXPECT_EQ(displayEntry(viewer, 1), "picture.png");
  books.hidden = true;
  ASSERT_TRUE(browser.open("/library", acceptFiltered, &books, 2));
  EXPECT_EQ(browser.totalCount(), 3u);
  EXPECT_EQ(displayEntry(browser, 1), ".private.epub");
}

TEST_F(FileIndexTest, ReportsDirectoryReadFailuresInsteadOfAnEmptyOrPartialList) {
  Storage.add("/library/book1.epub");
  Storage.add("/library/book2.epub");
  for (int pass : {1, 2}) {
    Storage.passes.clear();
    Storage.failureDirectory = "/library";
    Storage.failAfterEntry = 1;
    Storage.failOnPass = pass;
    FileIndex index;
    EXPECT_FALSE(index.open("/library", acceptAll));
    EXPECT_TRUE(index.directoryReadFailed());
    EXPECT_FALSE(index.isOpen());
    EXPECT_EQ(index.totalCount(), 0u);
    EXPECT_TRUE(cachePath().empty());
  }
  Storage.failureDirectory.clear();
  FileIndex index;
  ASSERT_TRUE(index.open("/library", acceptAll));
  EXPECT_EQ(index.totalCount(), 2u);
}

TEST_F(FileIndexTest, AllocationAndNameFailuresDoNotCommitPartialListings) {
  Storage.add("/library/book1.epub");
  for (int budget : {0, 1, 2, 3, 4}) {
    allocationBudget = budget;
    FileIndex index;
    EXPECT_FALSE(index.open("/library", acceptAll));
    EXPECT_EQ(index.totalCount(), 0u);
    EXPECT_TRUE(cachePath().empty());
  }
  allocationBudget = -1;
  Storage.failureDirectory = "/library";
  Storage.failAfterEntry = 0;
  Storage.failAsAllocation = true;
  FileIndex index;
  EXPECT_FALSE(index.open("/library", acceptAll));
  EXPECT_FALSE(index.directoryReadFailed());
  Storage.failureDirectory.clear();
  Storage.failName = "book1.epub";
  EXPECT_FALSE(index.open("/library", acceptAll));
  EXPECT_TRUE(index.directoryReadFailed());
  EXPECT_TRUE(cachePath().empty());
}

TEST_F(FileIndexTest, ShortWritesLeaveEarlierCacheAndBooksIntact) {
  Storage.add("/library/book1.epub", false, "book payload");
  FileIndex index;
  ASSERT_TRUE(index.open("/library", acceptAll));
  index.close();
  const auto path = cachePath();
  const auto oldCache = Storage.nodes.at(path).bytes;
  Storage.add("/library/book2.epub", false, "new payload");
  Storage.writeBudget = 40;
  EXPECT_FALSE(index.open("/library", acceptAll));
  EXPECT_EQ(Storage.nodes.at(path).bytes, oldCache);
  EXPECT_EQ(Storage.nodes.at("/library/book1.epub").bytes,
            std::vector<uint8_t>({'b', 'o', 'o', 'k', ' ', 'p', 'a', 'y', 'l', 'o', 'a', 'd'}));
  Storage.writeBudget = -1;
  ASSERT_TRUE(index.open("/library", acceptAll));
  EXPECT_EQ(index.totalCount(), 2u);
}

TEST_F(FileIndexTest, CorruptHeadersRebuildAndBadRecordOffsetsAreRejected) {
  Storage.add("/library/book.epub");
  FileIndex index;
  ASSERT_TRUE(index.open("/library", acceptAll));
  index.close();
  const auto path = cachePath();
  Storage.nodes.at(path).bytes[0] = 0;
  ASSERT_TRUE(index.open("/library", acceptAll));
  EXPECT_EQ(displayEntry(index, 0), "book.epub");
  index.close();
  auto& bytes = Storage.nodes.at(path).bytes;
  // The final four bytes are the sole sorted record offset. A cache must never
  // interpret the header/path as an entry or return a path outside this folder.
  write32(bytes, bytes.size() - 4, 0);
  ASSERT_TRUE(index.open("/library", acceptAll));
  FileIndex::Entry entry{};
  EXPECT_FALSE(index.entryAt(0, entry));
  EXPECT_FALSE(index.isOpen());
  ASSERT_TRUE(index.open("/library", acceptAll));
  EXPECT_EQ(displayEntry(index, 0), "book.epub");
}

TEST_F(FileIndexTest, EmptyMissingAndLongDirectoriesRemainDistinct) {
  FileIndex index;
  ASSERT_TRUE(index.open("/library", acceptAll));
  EXPECT_EQ(index.totalCount(), 0u);
  EXPECT_FALSE(index.directoryReadFailed());
  EXPECT_FALSE(index.open("/missing", acceptAll));
  EXPECT_TRUE(index.directoryReadFailed());
  const std::string longPath = "/" + std::string(250, 'a') + "/" + std::string(250, 'b') + "/folder";
  Storage.add(longPath, true);
  Storage.add(longPath + "/book.epub");
  ASSERT_TRUE(index.open(longPath.c_str(), acceptAll));
  EXPECT_EQ(displayEntry(index, 0), "book.epub");
  index.close();
  Storage.rejectWrites = true;
  ASSERT_TRUE(index.open(longPath.c_str(), acceptAll));
}
}  // namespace
