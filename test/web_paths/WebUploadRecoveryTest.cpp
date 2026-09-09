#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <string>

#include "src/util/WebUploadRecovery.h"

namespace {
struct TestStorage {
  struct Entry {
    std::string bytes;
    bool directory = false;
  };
  struct File {
    TestStorage* fs = nullptr;
    std::string path;
    size_t offset = 0;
    explicit operator bool() const { return fs != nullptr; }
    bool isDirectory() const { return fs->entries.at(path).directory; }
    int read(void* output, size_t length) {
      if (fs->failReadPath == path) return -1;
      const auto& bytes = fs->entries.at(path).bytes;
      const size_t amount = std::min(length, bytes.size() - std::min(offset, bytes.size()));
      std::memcpy(output, bytes.data() + offset, amount);
      offset += amount;
      return static_cast<int>(amount);
    }
    size_t write(const void* input, size_t length) {
      if (fs->failWritePath == path) return 0;
      fs->entries.at(path).bytes.append(static_cast<const char*>(input), length);
      offset += length;
      return length;
    }
    size_t fileSize() const { return fs->entries.at(path).bytes.size(); }
    void flush() {}
    bool close() {
      const bool result = fs && fs->failClosePath != path;
      fs = nullptr;
      return result;
    }
  };
  std::map<std::string, Entry> entries;
  std::string failReadPath, failWritePath, failClosePath, failRenameDestination;
  bool exists(const char* path) const { return entries.count(path) != 0; }
  bool mkdir(const char* path) { return entries.emplace(path, Entry{"", true}).second; }
  File open(const char* path) { return exists(path) ? File{this, path} : File{}; }
  bool openFileForWrite(const char*, const char* path, File& file) {
    entries[path] = {};
    file = File{this, path};
    return true;
  }
  bool remove(const char* path) { return entries.erase(path) != 0; }
  bool rename(const char* from, const char* to) {
    if (!exists(from) || exists(to) || failRenameDestination == to) return false;
    entries[to] = entries.at(from);
    entries.erase(from);
    return true;
  }
};

class UploadRecovery : public ::testing::Test {
 protected:
  TestStorage fs;
  const std::string target = "/Books/book.epub";
  const std::string base = "/Books/.cpr-upload-" + std::string(64, 'a');
  const std::string temporary = base + ".partial";
  const std::string previous = base + ".previous";

  void SetUp() override {
    fs.entries[target] = {"previous committed book"};
    fs.entries[temporary] = {"new complete upload"};
  }
  bool prepare() { return WebUploadRecovery::prepare(fs, target.c_str(), temporary.c_str(), previous.c_str()); }
  bool recover() { return WebUploadRecovery::recover(fs); }
};
}  // namespace

TEST_F(UploadRecovery, JournalIsClosedAndReadableBeforeReplacement) {
  ASSERT_TRUE(prepare());
  WebUploadRecovery::Record record;
  ASSERT_TRUE(WebUploadRecovery::load(fs, WebUploadRecovery::JOURNAL, record));
  EXPECT_EQ(record.target, target);
  EXPECT_EQ(record.temporary, temporary);
  EXPECT_EQ(record.previous, previous);
  EXPECT_TRUE(record.hadTarget);
  EXPECT_FALSE(fs.exists(WebUploadRecovery::PENDING));
}

TEST_F(UploadRecovery, PowerLossBeforeFirstRenameKeepsOldBook) {
  ASSERT_TRUE(prepare());
  ASSERT_TRUE(recover());
  EXPECT_EQ(fs.entries.at(target).bytes, "previous committed book");
  EXPECT_EQ(fs.entries.at(temporary).bytes, "new complete upload");
  EXPECT_FALSE(fs.exists(WebUploadRecovery::JOURNAL));
}

TEST_F(UploadRecovery, PowerLossBetweenRenamesRestoresPreviousNeverStage) {
  ASSERT_TRUE(prepare());
  ASSERT_TRUE(fs.rename(target.c_str(), previous.c_str()));
  ASSERT_TRUE(recover());
  EXPECT_EQ(fs.entries.at(target).bytes, "previous committed book");
  EXPECT_EQ(fs.entries.at(temporary).bytes, "new complete upload");
  EXPECT_TRUE(recover());
}

TEST_F(UploadRecovery, PowerLossAfterPromotionKeepsNewAndBackup) {
  ASSERT_TRUE(prepare());
  ASSERT_TRUE(fs.rename(target.c_str(), previous.c_str()));
  ASSERT_TRUE(fs.rename(temporary.c_str(), target.c_str()));
  ASSERT_TRUE(recover());
  EXPECT_EQ(fs.entries.at(target).bytes, "new complete upload");
  EXPECT_EQ(fs.entries.at(previous).bytes, "previous committed book");
}

TEST_F(UploadRecovery, FailedRecoveryRetainsEveryRecoveryFileForRetry) {
  ASSERT_TRUE(prepare());
  ASSERT_TRUE(fs.rename(target.c_str(), previous.c_str()));
  fs.failRenameDestination = target;
  EXPECT_FALSE(recover());
  EXPECT_TRUE(fs.exists(WebUploadRecovery::JOURNAL));
  EXPECT_EQ(fs.entries.at(previous).bytes, "previous committed book");
  EXPECT_EQ(fs.entries.at(temporary).bytes, "new complete upload");
  fs.failRenameDestination.clear();
  EXPECT_TRUE(recover());
}

TEST_F(UploadRecovery, MissingPreviousDoesNotPublishStageOrRemoveJournal) {
  ASSERT_TRUE(prepare());
  fs.remove(target.c_str());
  EXPECT_FALSE(recover());
  EXPECT_FALSE(fs.exists(target.c_str()));
  EXPECT_TRUE(fs.exists(WebUploadRecovery::JOURNAL));
  EXPECT_TRUE(fs.exists(temporary.c_str()));
}

TEST_F(UploadRecovery, NewFileDoesNotResurrectAnIntentionallyDeletedOlderBook) {
  fs.remove(target.c_str());
  fs.entries[previous] = {"older deleted book"};
  ASSERT_TRUE(prepare());
  ASSERT_TRUE(recover());
  EXPECT_FALSE(fs.exists(target.c_str()));
  EXPECT_EQ(fs.entries.at(previous).bytes, "older deleted book");
  EXPECT_EQ(fs.entries.at(temporary).bytes, "new complete upload");
}

TEST_F(UploadRecovery, CorruptJournalCannotActOnFiles) {
  ASSERT_TRUE(prepare());
  auto& bytes = fs.entries.at(WebUploadRecovery::JOURNAL).bytes;
  bytes.back() ^= 0x10;
  EXPECT_FALSE(recover());
  EXPECT_EQ(fs.entries.at(target).bytes, "previous committed book");
  EXPECT_TRUE(fs.exists(WebUploadRecovery::JOURNAL));
}

TEST_F(UploadRecovery, ValidCrcCannotAuthorizeProtectedTargetOrUnrelatedBackup) {
  ASSERT_TRUE(prepare());
  auto& bytes = fs.entries.at(WebUploadRecovery::JOURNAL).bytes;
  bytes[WebUploadRecovery::HEADER_BYTES + 1] = '.';
  const auto checksum = WebUploadRecovery::crc32(bytes.data() + WebUploadRecovery::HEADER_BYTES,
                                                 bytes.size() - WebUploadRecovery::HEADER_BYTES,
                                                 WebUploadRecovery::crc32(bytes.data(), 20));
  WebUploadRecovery::write32(reinterpret_cast<uint8_t*>(bytes.data() + 20), checksum);
  EXPECT_FALSE(recover());
  EXPECT_EQ(fs.entries.at(target).bytes, "previous committed book");
  EXPECT_FALSE(
      WebUploadRecovery::validPaths(target, temporary, "/other/.cpr-upload-" + std::string(64, 'a') + ".previous"));
}

TEST_F(UploadRecovery, ShortWriteReadBackFailureAndCloseFailureCannotCommitJournal) {
  fs.failWritePath = WebUploadRecovery::PENDING;
  EXPECT_FALSE(prepare());
  EXPECT_FALSE(fs.exists(WebUploadRecovery::JOURNAL));
  fs.failWritePath.clear();
  fs.failReadPath = WebUploadRecovery::PENDING;
  EXPECT_FALSE(prepare());
  EXPECT_FALSE(fs.exists(WebUploadRecovery::JOURNAL));
  fs.failReadPath.clear();
  fs.failClosePath = WebUploadRecovery::PENDING;
  EXPECT_FALSE(prepare());
  EXPECT_FALSE(fs.exists(WebUploadRecovery::JOURNAL));
  EXPECT_EQ(fs.entries.at(target).bytes, "previous committed book");
}

TEST_F(UploadRecovery, FailedJournalRenameLeavesOldBookUntouched) {
  fs.failRenameDestination = WebUploadRecovery::JOURNAL;
  EXPECT_FALSE(prepare());
  EXPECT_EQ(fs.entries.at(target).bytes, "previous committed book");
  EXPECT_FALSE(fs.exists(WebUploadRecovery::JOURNAL));
}

TEST_F(UploadRecovery, RejectsTruncatedAndOversizedJournalBeforePathReads) {
  ASSERT_TRUE(prepare());
  const auto good = fs.entries.at(WebUploadRecovery::JOURNAL).bytes;
  fs.entries.at(WebUploadRecovery::JOURNAL).bytes.resize(12);
  EXPECT_FALSE(recover());
  fs.entries.at(WebUploadRecovery::JOURNAL).bytes = good;
  auto& bytes = fs.entries.at(WebUploadRecovery::JOURNAL).bytes;
  WebUploadRecovery::write32(reinterpret_cast<uint8_t*>(bytes.data() + 8), 0xFFFFFFFFu);
  EXPECT_FALSE(recover());
  EXPECT_EQ(fs.entries.at(target).bytes, "previous committed book");
}
