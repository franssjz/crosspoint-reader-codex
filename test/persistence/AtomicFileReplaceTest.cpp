#include <gtest/gtest.h>

#include <map>
#include <string>
#include <utility>

#include "src/util/AtomicFileReplace.h"

namespace {
struct FileSystem {
  std::map<std::string, std::string> files;
  std::string failedRenameFrom;
  bool failRollback = false;
  bool failRemove = false;
  bool exists(const char* path) const { return files.count(path) != 0; }
  bool remove(const char* path) { return !failRemove && files.erase(path) == 1; }
  bool rename(const char* from, const char* to) {
    if (failedRenameFrom == from || (failRollback && std::string(from) == "settings.bak")) return false;
    const auto it = files.find(from);
    if (it == files.end() || exists(to)) return false;
    files.emplace(to, it->second);
    files.erase(it);
    return true;
  }
};
bool promote(FileSystem& fs) { return AtomicFileReplace::promote(fs, "settings.tmp", "settings", "settings.bak"); }
}  // namespace

TEST(AtomicFileReplace, RetainsPreviousConfigurationAfterSuccessfulSave) {
  FileSystem fs{{{"settings", "old"}, {"settings.tmp", "new"}}};
  ASSERT_TRUE(promote(fs));
  EXPECT_EQ(fs.files.at("settings"), "new");
  EXPECT_EQ(fs.files.at("settings.bak"), "old");
  EXPECT_FALSE(fs.exists("settings.tmp"));
}

TEST(AtomicFileReplace, FailedBackupDoesNotDiscardOriginalOrNewData) {
  FileSystem fs{{{"settings", "old"}, {"settings.tmp", "new"}}};
  fs.failedRenameFrom = "settings";
  EXPECT_FALSE(promote(fs));
  EXPECT_EQ(fs.files.at("settings"), "old");
  EXPECT_EQ(fs.files.at("settings.tmp"), "new");
}

TEST(AtomicFileReplace, FailedPromotionRollsBackWithoutDiscardingCompletedTemporary) {
  FileSystem fs{{{"settings", "old"}, {"settings.tmp", "new"}}};
  fs.failedRenameFrom = "settings.tmp";
  EXPECT_FALSE(promote(fs));
  EXPECT_EQ(fs.files.at("settings"), "old");
  EXPECT_EQ(fs.files.at("settings.tmp"), "new");
}

TEST(AtomicFileReplace, RemovedCardDuringPromotionAndRollbackLeavesRecoverableBackup) {
  FileSystem fs{{{"settings", "old"}, {"settings.tmp", "new"}}};
  fs.failedRenameFrom = "settings.tmp";
  fs.failRollback = true;
  EXPECT_FALSE(promote(fs));
  EXPECT_EQ(fs.files.at("settings.bak"), "old");
  EXPECT_EQ(fs.files.at("settings.tmp"), "new");
  fs.failedRenameFrom.clear();
  fs.failRollback = false;
  ASSERT_TRUE(AtomicFileReplace::recover(fs, "settings", "settings.bak"));
  EXPECT_EQ(fs.files.at("settings"), "old");
}

TEST(AtomicFileReplace, InterruptedUpgradeRecoversCommittedCopyBeforeUncommittedTemporary) {
  FileSystem fs{{{"settings.bak", "old"}, {"settings.tmp", "partial"}}};
  ASSERT_TRUE(AtomicFileReplace::recover(fs, "settings", "settings.bak"));
  EXPECT_EQ(fs.files.at("settings"), "old");
  EXPECT_EQ(fs.files.at("settings.tmp"), "partial");
}

TEST(AtomicFileReplace, FailedRotationKeepsBothExistingCommittedCopies) {
  FileSystem fs{{{"settings", "old"}, {"settings.bak", "older"}, {"settings.tmp", "new"}}};
  fs.failRemove = true;
  EXPECT_FALSE(promote(fs));
  EXPECT_EQ(fs.files.at("settings"), "old");
  EXPECT_EQ(fs.files.at("settings.bak"), "older");
}

TEST(AtomicFileReplace, MissingTemporaryNeverTouchesOriginal) {
  FileSystem fs{{{"settings", "old"}, {"settings.bak", "older"}}};
  EXPECT_FALSE(promote(fs));
  EXPECT_EQ(fs.files.at("settings"), "old");
  EXPECT_EQ(fs.files.at("settings.bak"), "older");
}

TEST(AtomicFileReplace, NewSaveCannotBypassUnrecoverablePreviousConfiguration) {
  FileSystem fs{{{"settings.bak", "old"}, {"settings.tmp", "defaults"}}};
  fs.failRollback = true;
  EXPECT_FALSE(promote(fs));
  EXPECT_FALSE(fs.exists("settings"));
  EXPECT_EQ(fs.files.at("settings.bak"), "old");
}
