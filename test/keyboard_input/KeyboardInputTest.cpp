#include <gtest/gtest.h>

#include <array>
#include <string>

#include "KeyboardLayoutSet.h"
#include "KeyboardText.h"
#include "util/PowerShortcutState.h"

TEST(KeyboardLayouts, LegacyDefaultAndLanguageCycleKeepLatinAccess) {
  EXPECT_EQ(KeyboardLayoutSet::normalizeMask(0), 1);
  EXPECT_EQ(KeyboardLayoutSet::normalizeMask(1), 1);
  EXPECT_EQ(KeyboardLayoutSet::normalizeMask(0x10), 0x11);
  EXPECT_EQ(KeyboardLayoutSet::normalizeMask(0x20), 1);
  EXPECT_EQ(KeyboardLayoutSet::normalizeMask(0xFF), 0x1F);
  EXPECT_EQ(KeyboardLayoutSet::first(0x18), KeyboardLayoutSet::Spanish);
  EXPECT_EQ(KeyboardLayoutSet::next(0x18, KeyboardLayoutSet::Spanish), KeyboardLayoutSet::Cyrillic);
  EXPECT_EQ(KeyboardLayoutSet::next(0x18, KeyboardLayoutSet::Cyrillic), KeyboardLayoutSet::Spanish);
  EXPECT_FALSE(KeyboardLayoutSet::multiple(1));
  EXPECT_TRUE(KeyboardLayoutSet::multiple(0x10));
  const std::array<const char*, 4> original = {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm=.,"};
  const std::array<const char*, 4> shifted = {"!@#$%^&*()", "QWERTYUIOP", "ASDFGHJKL_", "ZXCVBNM+><"};
  EXPECT_EQ(KeyboardLayoutSet::columns(0), 10);
  for (int row = 0; row < 4; ++row)
    for (int col = 0; col < 10; ++col) {
      EXPECT_EQ(KeyboardLayoutSet::key(0, row, col).primary, original[row][col]);
      EXPECT_EQ(KeyboardLayoutSet::key(0, row, col).secondary, shifted[row][col]);
    }
}

TEST(KeyboardLayouts, EveryKeyIsACompleteUnicodeScalar) {
  for (uint8_t id = 0; id < KeyboardLayoutSet::COUNT; ++id)
    for (int row = 0; row < 4; ++row)
      for (int column = 0; column < KeyboardLayoutSet::columns(id); ++column) {
        const auto key = KeyboardLayoutSet::key(id, row, column);
        for (uint32_t cp : {key.primary, key.secondary}) {
          ASSERT_NE(cp, 0u);
          char bytes[5];
          const size_t count = KeyboardText::encode(cp, bytes);
          EXPECT_GE(count, 1u);
          uint32_t decoded = 0;
          EXPECT_EQ(KeyboardText::decode(bytes, decoded), count);
          EXPECT_EQ(decoded, cp);
        }
      }
  EXPECT_EQ(KeyboardLayoutSet::key(KeyboardLayoutSet::Spanish, 2, 9).primary, 0xF1u);
  EXPECT_EQ(KeyboardLayoutSet::key(KeyboardLayoutSet::Cyrillic, 0, 10).secondary, 0x401u);
  EXPECT_EQ(KeyboardLayoutSet::key(KeyboardLayoutSet::HebrewReserved, 0, 0).primary, 0u);
}

TEST(KeyboardText, CursorDeletionAndInsertionNeverSplitUtf8) {
  std::string text = "a";
  size_t cursor = text.size();
  ASSERT_TRUE(KeyboardText::insert(text, cursor, 0xF1, 0));
  ASSERT_TRUE(KeyboardText::insert(text, cursor, 0x416, 0));
  ASSERT_TRUE(KeyboardText::insert(text, cursor, 0x1F642, 0));
  ASSERT_EQ(text.size(), 9u);
  EXPECT_EQ(cursor, 9u);
  cursor = KeyboardText::previous(text, cursor);
  EXPECT_EQ(cursor, 5u);
  ASSERT_TRUE(KeyboardText::erasePrevious(text, cursor));
  EXPECT_EQ(cursor, 3u);
  EXPECT_EQ(text, std::string("a\xC3\xB1\xF0\x9F\x99\x82"));
  cursor = KeyboardText::next(text, cursor);
  EXPECT_EQ(cursor, 7u);
  ASSERT_TRUE(KeyboardText::erasePrevious(text, cursor));
  EXPECT_EQ(text, std::string("a\xC3\xB1"));
  EXPECT_EQ(KeyboardText::previous(text, 0), 0u);
  EXPECT_EQ(KeyboardText::next(text, text.size()), text.size());
}

TEST(KeyboardText, ByteLimitsPreserveEarlierPasswordsAndRejectPartialScalars) {
  std::string text = "abc";
  size_t cursor = 1;
  EXPECT_FALSE(KeyboardText::insert(text, cursor, 0xF1, 4));
  EXPECT_EQ(text, "abc");
  EXPECT_EQ(cursor, 1u);
  EXPECT_TRUE(KeyboardText::insert(text, cursor, 0xF1, 5));
  EXPECT_EQ(text, std::string("a\xC3\xB1"
                              "bc"));
  EXPECT_EQ(cursor, 3u);
  EXPECT_FALSE(KeyboardText::insert(text, cursor, 0xD800, 0));
  EXPECT_FALSE(KeyboardText::insert(text, cursor, 0x110000, 0));
}

TEST(KeyboardText, MaskingCountsCharactersAndMapsCursorWithOptionalReveal) {
  const std::string text = "a\xC3\xB1\xF0\x9F\x99\x82";
  auto masked = KeyboardText::display(text, 3, true, false);
  EXPECT_EQ(masked.text, "***");
  EXPECT_EQ(masked.cursor, 2u);
  masked = KeyboardText::display(text, 3, true, true);
  EXPECT_EQ(masked.text, std::string("*\xC3\xB1*"));
  EXPECT_EQ(masked.cursor, 3u);
  masked = KeyboardText::display(text, text.size(), true, true);
  EXPECT_EQ(masked.text, std::string("**\xF0\x9F\x99\x82"));
  EXPECT_EQ(masked.cursor, masked.text.size());
  const auto visible = KeyboardText::display(text, 3, false, false);
  EXPECT_EQ(visible.text, text);
  EXPECT_EQ(visible.cursor, 3u);
}

TEST(PowerShortcuts, ScreenshotConsumesBothReleaseOrdersWithoutLockOrPageLeak) {
  for (bool powerFirst : {false, true}) {
    PowerShortcutState state;
    EXPECT_EQ(state.update(0, true, true, false, true).event, PowerShortcutState::Event::Screenshot);
    EXPECT_EQ(state.update(10, true, true, false, true).event, PowerShortcutState::Event::None);
    auto releasedOne = state.update(20, !powerFirst, powerFirst, powerFirst, true);
    EXPECT_TRUE(releasedOne.consume);
    EXPECT_EQ(releasedOne.event, PowerShortcutState::Event::None);
    EXPECT_TRUE(state.update(30, false, false, !powerFirst, true).consume);
    EXPECT_FALSE(state.isLocked());
    EXPECT_FALSE(state.update(40, false, false, false, true).consume);
  }
}

TEST(PowerShortcuts, DisabledDefaultAndLockTimerDoNotChangeOnBlockedButtons) {
  PowerShortcutState state;
  EXPECT_FALSE(state.update(0, false, false, true, false).consume);
  EXPECT_EQ(state.update(100, false, false, true, true).event, PowerShortcutState::Event::LockChanged);
  EXPECT_TRUE(state.isLocked());
  EXPECT_TRUE(state.update(200, false, false, false, true).consume);
  EXPECT_FALSE(state.shouldSleep(1099, 1000));
  EXPECT_TRUE(state.shouldSleep(1100, 1000));
  EXPECT_FALSE(state.shouldSleep(1100, 0));
  EXPECT_EQ(state.update(1200, false, false, true, true).event, PowerShortcutState::Event::LockChanged);
  EXPECT_FALSE(state.isLocked());
  EXPECT_TRUE(state.update(1210, false, false, false, true, true).consume);
  EXPECT_TRUE(state.update(1220, false, false, false, true, false).consume);
  EXPECT_FALSE(state.update(1230, false, false, false, true, false).consume);
}

TEST(PowerShortcuts, TimeoutHandlesMillisWrapAndScreenshotCannotUnlock) {
  PowerShortcutState state;
  state.update(0xFFFFFFF0u, false, false, true, true);
  EXPECT_FALSE(state.shouldSleep(15, 32));
  EXPECT_TRUE(state.shouldSleep(16, 32));
  EXPECT_EQ(state.update(20, true, true, false, true).event, PowerShortcutState::Event::None);
  state.update(30, false, false, true, true);
  EXPECT_TRUE(state.isLocked());
}
