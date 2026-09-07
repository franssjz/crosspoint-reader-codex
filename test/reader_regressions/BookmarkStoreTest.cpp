#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "lib/Epub/Epub/VisibleTextPageLookup.h"
#include "src/activities/reader/BookmarkStore.h"

namespace {

class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    path = std::filesystem::temp_directory_path() /
           ("cpr-vcodex-bookmarks-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(path);
  }
  ~TemporaryDirectory() { std::filesystem::remove_all(path); }

  std::filesystem::path path;
};

template <typename T>
void writePod(std::ofstream& file, const T& value) {
  file.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

void writeV4PageMark(const std::filesystem::path& path) {
  std::ofstream file(path, std::ios::binary);
  const uint8_t version = 4;
  const uint32_t count = 1;
  const uint16_t spine = 3;
  const uint16_t page = 12;
  const uint8_t kind = 0;
  const uint16_t endPage = 12;
  const uint16_t startWord = 0;
  const uint16_t endWord = 0;
  const std::string snippet = "legacy page";
  const uint16_t snippetLength = static_cast<uint16_t>(snippet.size());
  writePod(file, version);
  writePod(file, count);
  writePod(file, spine);
  writePod(file, page);
  writePod(file, kind);
  writePod(file, endPage);
  writePod(file, startWord);
  writePod(file, endWord);
  writePod(file, snippetLength);
  file.write(snippet.data(), static_cast<std::streamsize>(snippet.size()));
}

void writeLegacyPageMark(const std::filesystem::path& path, const uint8_t version) {
  std::ofstream file(path, std::ios::binary);
  const uint32_t count32 = 1;
  const uint16_t count16 = 1;
  const uint16_t spine = 7;
  const uint16_t page = 21;
  const std::string snippet = "legacy snippet";
  writePod(file, version);
  if (version >= 3) writePod(file, count32);
  else writePod(file, count16);
  writePod(file, spine);
  writePod(file, page);
  if (version >= 2) {
    const uint8_t length = static_cast<uint8_t>(snippet.size());
    writePod(file, length);
    file.write(snippet.data(), static_cast<std::streamsize>(snippet.size()));
  }
}

TEST(BookmarkStoreV5, LoadsV4AndUpgradesWithoutInventingAnAnchor) {
  TemporaryDirectory temporary;
  const auto filePath = temporary.path / "bookmarks.bin";
  writeV4PageMark(filePath);

  BookmarkStore store;
  store.load(temporary.path.string());
  ASSERT_EQ(store.getAll().size(), 1U);
  EXPECT_EQ(store.getAll()[0].spineIndex, 3);
  EXPECT_EQ(store.getAll()[0].pageNumber, 12);
  EXPECT_FALSE(store.getAll()[0].hasVisibleTextOffset);

  store.markDirty();
  store.save();
  std::ifstream upgraded(filePath, std::ios::binary);
  EXPECT_EQ(upgraded.get(), 5);

  BookmarkStore reloaded;
  reloaded.load(temporary.path.string());
  ASSERT_EQ(reloaded.getAll().size(), 1U);
  EXPECT_FALSE(reloaded.getAll()[0].hasVisibleTextOffset);
  EXPECT_EQ(reloaded.getAll()[0].snippet, "legacy page");
}

TEST(BookmarkStoreV5, MigratesEveryLegacyPageMarkFormat) {
  for (uint8_t version = 1; version <= 3; version++) {
    TemporaryDirectory temporary;
    writeLegacyPageMark(temporary.path / "bookmarks.bin", version);
    BookmarkStore store;
    store.load(temporary.path.string());
    ASSERT_EQ(store.getAll().size(), 1U) << "version " << static_cast<int>(version);
    const auto& mark = store.getAll().front();
    EXPECT_EQ(mark.spineIndex, 7);
    EXPECT_EQ(mark.pageNumber, 21);
    EXPECT_EQ(mark.endPageNumber, 21);
    EXPECT_FALSE(mark.hasVisibleTextOffset);
    EXPECT_EQ(mark.snippet, version >= 2 ? "legacy snippet" : "");
  }
}

TEST(BookmarkStoreV5, RoundTripsPageMarksAndHighlightsWithVisibleOffsets) {
  TemporaryDirectory temporary;
  BookmarkStore store;
  store.load(temporary.path.string());
  EXPECT_TRUE(store.toggle(2, 9, "page text", 145U));
  EXPECT_TRUE(store.addTextHighlight(2, 9, 9, 4, 7, "highlight text", 145U));
  store.save();

  BookmarkStore reloaded;
  reloaded.load(temporary.path.string());
  ASSERT_EQ(reloaded.getAll().size(), 2U);
  EXPECT_TRUE(reloaded.has(2, 4, 145U));
  EXPECT_TRUE(reloaded.getAll()[0].hasVisibleTextOffset);
  EXPECT_EQ(reloaded.getAll()[0].visibleTextOffset, 145U);
  EXPECT_TRUE(reloaded.getAll()[1].isTextHighlight);
  EXPECT_EQ(reloaded.getAll()[1].visibleTextOffset, 145U);
}

TEST(BookmarkStoreV5, AnchoredPageMarkSurvivesRepagination) {
  struct PageStart {
    uint32_t visibleTextOffset;
  };
  const std::vector<PageStart> repaginated{{0}, {60}, {120}, {180}};
  const auto page = VisibleTextPageLookup::find(repaginated, 145U, true);
  ASSERT_TRUE(page.has_value());
  EXPECT_EQ(*page, 2);
}

TEST(BookmarkStoreV5, ExactDuplicatePageStartsCanPreferTheirFirstPage) {
  struct PageStart { uint32_t visibleTextOffset; };
  const std::vector<PageStart> pages{{0}, {60}, {60}, {120}};
  EXPECT_EQ(VisibleTextPageLookup::find(pages, 60U, true), 1);
  EXPECT_EQ(VisibleTextPageLookup::find(pages, 60U, false), 2);
}

TEST(BookmarkStoreV5, AnchoredHighlightUsesItsVisibleStartAfterRepagination) {
  struct PageStart { uint32_t visibleTextOffset; };
  const std::vector<PageStart> repaginated{{0}, {40}, {95}, {160}, {225}};
  const auto page = VisibleTextPageLookup::find(repaginated, 145U, true);
  ASSERT_TRUE(page.has_value());
  EXPECT_EQ(*page, 2);
}

TEST(BookmarkStoreV5, RejectsATruncatedV5RecordAtomically) {
  TemporaryDirectory temporary;
  const auto filePath = temporary.path / "bookmarks.bin";
  std::ofstream file(filePath, std::ios::binary);
  const uint8_t version = 5;
  const uint32_t count = 1;
  const uint16_t incompleteSpine = 3;
  writePod(file, version);
  writePod(file, count);
  writePod(file, incompleteSpine);
  file.close();

  BookmarkStore store;
  store.load(temporary.path.string());
  EXPECT_TRUE(store.isEmpty());
}

TEST(BookmarkStoreV5, AnchoredHighlightsDeduplicateAcrossChangedPageNumbers) {
  TemporaryDirectory temporary;
  BookmarkStore store;
  store.load(temporary.path.string());
  EXPECT_TRUE(store.addTextHighlight(1, 3, 3, 2, 5, "same selection", 88U));
  EXPECT_TRUE(store.addTextHighlight(1, 4, 4, 2, 5, "same selection", 88U));
  EXPECT_EQ(store.getAll().size(), 1U);
}

}  // namespace
