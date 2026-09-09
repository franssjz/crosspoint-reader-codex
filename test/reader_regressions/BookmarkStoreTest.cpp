#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "lib/Epub/Epub/VisibleTextPageLookup.h"
#include "src/activities/reader/BookmarkStore.h"
#include "src/activities/reader/ProgressFile.h"

namespace {

class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    path = std::filesystem::temp_directory_path() /
           ("cpr-vcodex-bookmarks-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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
  if (version >= 3)
    writePod(file, count32);
  else
    writePod(file, count16);
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
  struct PageStart {
    uint32_t visibleTextOffset;
  };
  const std::vector<PageStart> pages{{0}, {60}, {60}, {120}};
  EXPECT_EQ(VisibleTextPageLookup::find(pages, 60U, true), 1);
  EXPECT_EQ(VisibleTextPageLookup::find(pages, 60U, false), 2);
}

TEST(BookmarkStoreV5, AnchoredHighlightUsesItsVisibleStartAfterRepagination) {
  struct PageStart {
    uint32_t visibleTextOffset;
  };
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

std::string readBytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

TEST(BookmarkStoreV5, DeferredTextRoundTripsTheFullOldFormatByteForByte) {
  TemporaryDirectory temporary;
  const auto path = temporary.path / "bookmarks.bin";
  BookmarkStore eager;
  eager.load(temporary.path.string());
  for (uint16_t i = 0; i < 256; ++i) {
    ASSERT_TRUE(eager.addTextHighlight(i, i, i + 1, 3, 8, std::string(512, 'a' + i % 26), i * 900U));
  }
  eager.save();
  const auto original = readBytes(path);

  BookmarkStore lazy;
  lazy.load(temporary.path.string(), "", true);
  ASSERT_FALSE(lazy.hasLoadError());
  ASSERT_EQ(lazy.getAll().size(), 256U);
  for (size_t i = 0; i < lazy.getAll().size(); ++i) {
    const auto& mark = lazy.getAll()[i];
    EXPECT_TRUE(mark.snippet.empty());
    ASSERT_NE(lazy.getText(mark), nullptr);
    EXPECT_EQ(std::string(lazy.getText(mark)), std::string(512, 'a' + i % 26));
    EXPECT_EQ(mark.visibleTextOffset, i * 900U);
  }
  lazy.markDirty();
  lazy.save();
  EXPECT_EQ(readBytes(path), original);
  EXPECT_EQ(readBytes(path.string() + ".bak"), original);
}

TEST(BookmarkStoreV5, DeferredSnapshotsResolveMovedOffsetsAfterDeleteAndSave) {
  TemporaryDirectory temporary;
  BookmarkStore eager;
  eager.load(temporary.path.string());
  ASSERT_TRUE(eager.addTextHighlight(1, 1, 1, 0, 1, "first"));
  ASSERT_TRUE(eager.addTextHighlight(1, 1, 1, 0, 1, "second is longer"));
  eager.save();
  BookmarkStore lazy;
  lazy.load(temporary.path.string(), "", true);
  const auto snapshots = lazy.getAll();
  ASSERT_TRUE(lazy.removeItem(snapshots[0]));
  lazy.save();
  EXPECT_STREQ(lazy.getText(snapshots[1]), "second is longer");
  ASSERT_TRUE(lazy.addTextHighlight(2, 3, 4, 0, 5, "new selection", 777U));
  lazy.save();
  EXPECT_STREQ(lazy.getText(snapshots[1]), "second is longer");
  BookmarkStore reload;
  reload.load(temporary.path.string());
  ASSERT_EQ(reload.getAll().size(), 2U);
  EXPECT_EQ(reload.getAll()[0].snippet, "second is longer");
  EXPECT_EQ(reload.getAll()[1].snippet, "new selection");
  EXPECT_EQ(reload.getAll()[1].visibleTextOffset, 777U);
}

TEST(BookmarkStoreV5, FailedDeferredSourceReadCannotReplaceExistingRecords) {
  TemporaryDirectory temporary;
  const auto path = temporary.path / "bookmarks.bin";
  BookmarkStore store;
  store.load(temporary.path.string());
  store.addTextHighlight(1, 1, 1, 0, 1, "irreplaceable text");
  store.save();
  const auto original = readBytes(path);
  BookmarkStore lazy;
  lazy.load(temporary.path.string(), "", true);
  std::filesystem::rename(path, temporary.path / "sd-removed.bin");
  lazy.toggle(4, 5, "added while SD unavailable");
  lazy.save();
  EXPECT_FALSE(std::filesystem::exists(path));
  EXPECT_EQ(readBytes(temporary.path / "sd-removed.bin"), original);
  std::filesystem::rename(temporary.path / "sd-removed.bin", path);
  lazy.save();
  BookmarkStore recovered;
  recovered.load(temporary.path.string());
  ASSERT_EQ(recovered.getAll().size(), 2U);
  EXPECT_EQ(recovered.getAll()[0].snippet, "irreplaceable text");
}

TEST(BookmarkStoreV5, UnknownAndTruncatedFilesAreNeverOverwrittenByAnEmptyLoad) {
  for (uint8_t version = 1; version <= 6; ++version) {
    TemporaryDirectory temporary;
    const auto path = temporary.path / "bookmarks.bin";
    writeLegacyPageMark(path, version);
    std::filesystem::resize_file(path, 4);
    const auto original = readBytes(path);
    BookmarkStore store;
    store.load(temporary.path.string(), "", true);
    ASSERT_TRUE(store.hasLoadError()) << static_cast<int>(version);
    EXPECT_FALSE(store.toggle(5, 6, "new"));
    store.clear();
    store.markDirty();
    store.save();
    EXPECT_EQ(readBytes(path), original);
  }
}

TEST(BookmarkStoreV5, OlderPageMarkMigrationsRetainAnUntouchedBackup) {
  for (uint8_t version = 1; version <= 4; ++version) {
    TemporaryDirectory temporary;
    const auto path = temporary.path / "bookmarks.bin";
    if (version == 4)
      writeV4PageMark(path);
    else
      writeLegacyPageMark(path, version);
    const auto original = readBytes(path);
    BookmarkStore store;
    store.load(temporary.path.string(), "", true);
    ASSERT_FALSE(store.hasLoadError());
    store.markDirty();
    store.save();
    EXPECT_EQ(readBytes(path.string() + ".bak"), original);
    BookmarkStore reload;
    reload.load(temporary.path.string());
    ASSERT_EQ(reload.getAll().size(), 1U);
    EXPECT_EQ(reload.getAll()[0].snippet, version == 4 ? "legacy page" : version >= 2 ? "legacy snippet" : "");
  }
}

TEST(BookmarkStoreV5, EagerSnapshotFromAnotherLoadDoesNotDeleteAnIdCollision) {
  TemporaryDirectory temporary;
  BookmarkStore store;
  store.load(temporary.path.string());
  store.addTextHighlight(1, 1, 1, 0, 1, "first");
  store.addTextHighlight(1, 1, 1, 0, 1, "second");
  store.save();
  const auto snapshots = store.getAll();
  store.removeItem(snapshots[0]);
  store.save();
  BookmarkStore reload;
  reload.load(temporary.path.string());
  EXPECT_FALSE(reload.removeItem(snapshots[0]));
  EXPECT_TRUE(reload.removeItem(snapshots[1]));
}

TEST(BookmarkStoreV5, RecoversLegacyBackupBeforeStableMigration) {
  TemporaryDirectory temporary;
  const auto legacyDir = temporary.path / "legacy-cache";
  const auto stableDir = temporary.path / "stable-book";
  std::filesystem::create_directories(legacyDir);
  const auto legacyPath = legacyDir / "bookmarks.bin";
  writeV4PageMark(legacyPath);
  const auto original = readBytes(legacyPath);
  std::filesystem::rename(legacyPath, legacyPath.string() + ".bak");

  BookmarkStore store;
  store.load(legacyDir.string(), stableDir.string(), true);
  ASSERT_FALSE(store.hasLoadError());
  ASSERT_EQ(store.getAll().size(), 1U);
  EXPECT_EQ(store.getAll()[0].spineIndex, 3);
  EXPECT_EQ(store.getAll()[0].pageNumber, 12);
  EXPECT_STREQ(store.getText(store.getAll()[0]), "legacy page");
  EXPECT_EQ(readBytes(legacyPath), original);

  BookmarkStore migrated;
  migrated.load(stableDir.string());
  ASSERT_EQ(migrated.getAll().size(), 1U);
  EXPECT_EQ(migrated.getAll()[0].snippet, "legacy page");
}

TEST(BookmarkStoreV5, MultiPageHighlightBatchRoundTripsEveryAnchor) {
  struct Fragment {
    std::string text;
    uint16_t spineIndex;
    uint16_t pageNumber;
    uint16_t startWordIndex;
    uint16_t endWordIndex;
    uint32_t visibleTextOffset;
  };
  TemporaryDirectory temporary;
  BookmarkStore store;
  store.load(temporary.path.string());
  const std::vector<Fragment> fragments{
      {"first page", 2, 8, 4, 11, 900}, {"middle page", 2, 9, 0, 17, 1200}, {"last page", 3, 0, 0, 3, 0}};
  ASSERT_TRUE(store.addTextHighlightFragments(fragments));
  ASSERT_TRUE(store.save());

  BookmarkStore reloaded;
  reloaded.load(temporary.path.string());
  ASSERT_EQ(reloaded.getAll().size(), fragments.size());
  for (size_t i = 0; i < fragments.size(); ++i) {
    const auto& mark = reloaded.getAll()[i];
    EXPECT_EQ(mark.snippet, fragments[i].text);
    EXPECT_EQ(mark.spineIndex, fragments[i].spineIndex);
    EXPECT_EQ(mark.pageNumber, fragments[i].pageNumber);
    EXPECT_EQ(mark.startWordIndex, fragments[i].startWordIndex);
    EXPECT_EQ(mark.endWordIndex, fragments[i].endWordIndex);
    EXPECT_TRUE(mark.hasVisibleTextOffset);
    EXPECT_EQ(mark.visibleTextOffset, fragments[i].visibleTextOffset);
  }
}

TEST(BookmarkStoreV5, InvalidMultiPageBatchDoesNotMutateExistingData) {
  struct Fragment {
    std::string text;
    uint16_t spineIndex = 0;
    uint16_t pageNumber = 0;
    uint16_t startWordIndex = 0;
    uint16_t endWordIndex = 0;
    uint32_t visibleTextOffset = 0;
  };
  TemporaryDirectory temporary;
  BookmarkStore store;
  store.load(temporary.path.string());
  ASSERT_TRUE(store.addTextHighlight(1, 1, 1, 0, 1, "existing"));
  ASSERT_TRUE(store.save());
  const auto original = readBytes(temporary.path / "bookmarks.bin");
  const std::vector<Fragment> invalid{{"valid"}, {std::string(513, 'x')}};
  EXPECT_FALSE(store.addTextHighlightFragments(invalid));
  EXPECT_TRUE(store.save());
  ASSERT_EQ(store.getAll().size(), 1U);
  EXPECT_EQ(store.getAll()[0].snippet, "existing");
  EXPECT_EQ(readBytes(temporary.path / "bookmarks.bin"), original);
}

TEST(ProgressCompatibility, KeepsLegacyFourAndSixBytePayloadsAndTheirPreviousCopy) {
  for (size_t size : {4U, 6U}) {
    TemporaryDirectory temporary;
    const auto path = temporary.path / "progress.bin";
    const uint8_t oldData[] = {7, 0, 0x81, 0, 0xEE, 1};
    const uint8_t newData[] = {9, 0, 0x24, 1, 0xFF, 1};
    ASSERT_TRUE(ProgressFile::writeAtomicPath("test", path.string(), oldData, size));
    ASSERT_TRUE(ProgressFile::writeAtomicPath("test", path.string(), newData, size));
    EXPECT_EQ(readBytes(path), std::string(reinterpret_cast<const char*>(newData), size));
    EXPECT_EQ(readBytes(path.string() + ".previous"), std::string(reinterpret_cast<const char*>(oldData), size));
  }
}

TEST(ProgressCompatibility, RestartPrefersCommittedProgressToInterruptedTemporary) {
  TemporaryDirectory temporary;
  const auto path = temporary.path / "progress.bin";
  const uint8_t oldData[] = {3, 0, 123, 0};
  ASSERT_TRUE(ProgressFile::writeAtomicPath("test", path.string(), oldData, sizeof(oldData)));
  std::filesystem::rename(path, path.string() + ".previous");
  std::ofstream temporaryWrite(path.string() + ".tmp", std::ios::binary);
  temporaryWrite.put(0);
  temporaryWrite.close();
  ASSERT_TRUE(ProgressFile::recover(path.string()));
  EXPECT_EQ(readBytes(path), std::string(reinterpret_cast<const char*>(oldData), sizeof(oldData)));
}

}  // namespace
