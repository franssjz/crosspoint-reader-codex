#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace VisibleTextPageLookup {

template <typename Entries>
std::optional<uint16_t> find(const Entries& entries, const uint32_t offset, const bool preferFirstAtOffset = false) {
  if (entries.empty()) return std::nullopt;
  uint16_t result = 0;
  for (size_t index = 0; index < entries.size(); ++index) {
    const uint32_t pageStart = entries[index].visibleTextOffset;
    if (preferFirstAtOffset && pageStart == offset) return static_cast<uint16_t>(index);
    if (pageStart > offset) break;
    result = static_cast<uint16_t>(index);
  }
  return result;
}

}  // namespace VisibleTextPageLookup
