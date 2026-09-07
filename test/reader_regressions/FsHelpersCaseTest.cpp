#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>

#include <HalStorage.h>

#include "lib/FsHelpers/FsHelpers.h"

namespace {

class TemporaryStorageRoot {
 public:
  TemporaryStorageRoot()
      : path(std::filesystem::temp_directory_path() /
             ("cpr-vcodex-fs-case-" +
              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
    std::filesystem::create_directories(path);
    Storage.setTestRootPath(path);
  }
  ~TemporaryStorageRoot() {
    Storage.clearTestRootPath();
    std::filesystem::remove_all(path);
  }

  std::filesystem::path path;
};

TEST(FsHelpersCase, ResolvesDirectRootDirectoriesAndPreservesTheirStoredCase) {
  TemporaryStorageRoot storage;
  std::filesystem::create_directories(storage.path / "FoNtS");
  std::filesystem::create_directories(storage.path / ".SLEEP");

  char resolved[32];
  ASSERT_TRUE(FsHelpers::resolveRootDirectoryIgnoreCase("/fonts", resolved, sizeof(resolved)));
  EXPECT_STREQ(resolved, "/FoNtS");
  ASSERT_TRUE(FsHelpers::resolveRootDirectoryIgnoreCase("/.sleep", resolved, sizeof(resolved)));
  EXPECT_STREQ(resolved, "/.SLEEP");
}

TEST(FsHelpersCase, KeepsExactPathsAndRejectsUnsafeOrTruncatedRequests) {
  TemporaryStorageRoot storage;
  std::filesystem::create_directories(storage.path / "Dictionaries");

  char resolved[32];
  ASSERT_TRUE(FsHelpers::resolveRootDirectoryIgnoreCase("/Dictionaries", resolved, sizeof(resolved)));
  EXPECT_STREQ(resolved, "/Dictionaries");
  EXPECT_FALSE(FsHelpers::resolveRootDirectoryIgnoreCase("Dictionaries", resolved, sizeof(resolved)));
  EXPECT_FALSE(FsHelpers::resolveRootDirectoryIgnoreCase("/Dictionaries/nested", resolved, sizeof(resolved)));
  EXPECT_FALSE(FsHelpers::resolveRootDirectoryIgnoreCase("/Dictionaries", resolved, 4));
}

}  // namespace
