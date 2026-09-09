#include <gtest/gtest.h>

#include <cstring>

#include "src/util/PageTurnQueue.h"
#include "src/util/ReaderPreferences.h"

namespace {
TEST(PageTurnQueue, KeepsBurstsAcrossChaptersAndLatestReversalWins) {
  PageTurnQueue queue;
  for (int round = 0; round < 3; ++round) {
    ASSERT_TRUE(queue.push(true));
    ASSERT_TRUE(queue.push(true));
    ASSERT_TRUE(queue.push(false));
    ASSERT_TRUE(queue.push(false));
    bool forward;
    ASSERT_TRUE(queue.pop(forward));
    EXPECT_FALSE(forward);
    ASSERT_TRUE(queue.pop(forward));
    EXPECT_FALSE(forward);
    EXPECT_TRUE(queue.empty());
  }
}

TEST(PageTurnQueue, FullQueuePreservesEarlierInputAndCancelDiscardsAllPendingTurns) {
  PageTurnQueue queue;
  for (int i = 0; i < 8; ++i) ASSERT_TRUE(queue.push(true));
  EXPECT_FALSE(queue.push(true));
  bool forward;
  ASSERT_TRUE(queue.pop(forward));
  EXPECT_TRUE(forward);
  queue.clear();
  EXPECT_FALSE(queue.pop(forward));
  ASSERT_TRUE(queue.push(false));
  ASSERT_TRUE(queue.pop(forward));
  EXPECT_FALSE(forward);
}

struct Settings : ReaderPreferences {
  int language = 7;
  int progress = 83;
  int shortPwrBtn = 4;
  char credential[24] = "existing-user-secret";
};

TEST(ReaderPreferenceScope, NoBookProfileKeepsPreviousGlobalBehavior) {
  Settings settings;
  settings.fontFamily = 1;
  settings.fontSize = 4;
  ReaderPreferenceScope scope;
  EXPECT_FALSE(scope.isActive());
  EXPECT_EQ(scope.persisted(settings).fontFamily, 1);
  settings.fontSize = 2;
  EXPECT_EQ(scope.persisted(settings).fontSize, 2);
  scope.end(settings);
  EXPECT_EQ(settings.fontSize, 2);
}

TEST(ReaderPreferenceScope, UnrelatedSaveNeverSerializesTheCurrentBooksFont) {
  Settings settings;
  settings.fontSize = 2;
  settings.orientation = 0;
  std::strcpy(settings.sdFontFamilyName, "Global Thai");
  ReaderPreferences book = ReaderPreferences::capture(settings);
  book.fontSize = 4;
  book.orientation = 3;
  std::strcpy(book.sdFontFamilyName, "Book serif");
  ReaderPreferenceScope scope;
  scope.begin(settings, book);
  settings.fontSize = 3;
  settings.language = 11;
  const auto serialized = scope.persisted(settings);
  EXPECT_EQ(serialized.fontSize, 2);
  EXPECT_EQ(serialized.orientation, 0);
  EXPECT_STREQ(serialized.sdFontFamilyName, "Global Thai");
  EXPECT_EQ(settings.fontSize, 3);  // Serialization does not mutate a render's settings.
  EXPECT_EQ(settings.orientation, 3);
  scope.end(settings);
  EXPECT_EQ(settings.fontSize, 2);
  EXPECT_STREQ(settings.sdFontFamilyName, "Global Thai");
  EXPECT_EQ(settings.language, 11);
  EXPECT_EQ(settings.shortPwrBtn, 4);
  EXPECT_EQ(settings.progress, 83);
  EXPECT_STREQ(settings.credential, "existing-user-secret");
}

TEST(ReaderPreferenceScope, EveryScopedFieldRestoresWithoutReplacingUnrelatedData) {
  Settings settings;
#define SET_GLOBAL(name, maximum) settings.name = maximum;
  CPR_READER_PREFERENCE_FIELDS(SET_GLOBAL)
#undef SET_GLOBAL
  ReaderPreferenceScope scope;
  ReaderPreferences book;
  scope.begin(settings, book);
#define CHECK_BOOK(name, maximum) EXPECT_EQ(settings.name, 0) << #name;
  CPR_READER_PREFERENCE_FIELDS(CHECK_BOOK)
#undef CHECK_BOOK
  scope.end(settings);
#define CHECK_GLOBAL(name, maximum) EXPECT_EQ(settings.name, maximum) << #name;
  CPR_READER_PREFERENCE_FIELDS(CHECK_GLOBAL)
#undef CHECK_GLOBAL
  EXPECT_STREQ(settings.credential, "existing-user-secret");
}

TEST(ReaderPreferenceScope, RepeatedOverrideAndDifferentBooksDoNotCaptureAnOverrideAsGlobal) {
  Settings settings;
  settings.fontSize = 2;
  ReaderPreferences first;
  first.fontSize = 3;
  ReaderPreferences second;
  second.fontSize = 4;
  ReaderPreferenceScope scope;
  scope.begin(settings, first);
  scope.begin(settings, second);
  EXPECT_EQ(scope.persisted(settings).fontSize, 2);
  scope.end(settings);
  EXPECT_EQ(settings.fontSize, 2);
  scope.begin(settings, second);
  EXPECT_EQ(settings.fontSize, 4);
  scope.end(settings);
  EXPECT_EQ(settings.fontSize, 2);
}
}  // namespace
