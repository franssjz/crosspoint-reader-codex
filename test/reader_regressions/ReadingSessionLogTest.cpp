#include <gtest/gtest.h>

#include <vector>

#include "ReadingSessionLog.h"

TEST(ReadingSessionLogTest, MakesRoomBeforeAppendingAtCapacity) {
  std::vector<ReadingSessionLogEntry> entries;
  entries.reserve(ReadingSessionLog::MAX_ENTRIES);
  for (size_t index = 0; index < ReadingSessionLog::MAX_ENTRIES; ++index) {
    entries.push_back(ReadingSessionLogEntry{static_cast<uint32_t>(index + 1), 180000, {}, {}});
  }
  const size_t capacity = entries.capacity();

  ReadingSessionLog::makeRoomForAppend(entries);
  entries.push_back(ReadingSessionLogEntry{999, 240000, {}, {}});

  ASSERT_EQ(entries.size(), ReadingSessionLog::MAX_ENTRIES);
  EXPECT_EQ(entries.capacity(), capacity);
  EXPECT_EQ(entries.front().dayOrdinal, 2U);
  EXPECT_EQ(entries.back().dayOrdinal, 999U);
}

TEST(ReadingSessionLogTest, TrimsOversizedImportedHistoryToNewestEntries) {
  std::vector<ReadingSessionLogEntry> entries(ReadingSessionLog::MAX_ENTRIES + 20);
  for (size_t index = 0; index < entries.size(); ++index) {
    entries[index].dayOrdinal = static_cast<uint32_t>(index + 1);
  }

  ReadingSessionLog::makeRoomForAppend(entries);
  entries.push_back(ReadingSessionLogEntry{999, 240000, "book-id", {}});

  ASSERT_EQ(entries.size(), ReadingSessionLog::MAX_ENTRIES);
  EXPECT_EQ(entries.front().dayOrdinal, 22U);
  EXPECT_EQ(entries.back().bookId, "book-id");
}
