#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct ReadingSessionLogEntry {
  uint32_t dayOrdinal = 0;
  uint32_t sessionMs = 0;
  std::string bookId;
  std::string path;
};

namespace ReadingSessionLog {

inline constexpr size_t MAX_ENTRIES = 128;

inline void makeRoomForAppend(std::vector<ReadingSessionLogEntry>& entries) {
  if (entries.size() < MAX_ENTRIES) {
    return;
  }

  const size_t removeCount = entries.size() - MAX_ENTRIES + 1;
  entries.erase(entries.begin(), entries.begin() + static_cast<std::ptrdiff_t>(removeCount));
}

}  // namespace ReadingSessionLog
