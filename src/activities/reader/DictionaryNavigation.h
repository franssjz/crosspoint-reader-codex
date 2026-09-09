#pragma once

#include <Utf8.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace DictionaryNavigation {

// Keep queries and page numbers, never a definition, Page, or Activity per hop.
// Fixed storage also makes repeated lookups independent of heap fragmentation.
class Trail {
 public:
  static constexpr size_t CAPACITY = 8;
  static constexpr size_t QUERY_BYTES = 256;  // StarDict index headwords: <=255 bytes.
  struct Entry {
    char query[QUERY_BYTES] = {};
    uint16_t page = 0;
  };
  bool push(const char* query, int page = 0) {
    if (!query || count_ >= CAPACITY) return false;
    const size_t length = std::strlen(query);
    if (length == 0 || length >= QUERY_BYTES) return false;
    std::memcpy(entries_[count_].query, query, length + 1);
    entries_[count_].page = static_cast<uint16_t>(std::clamp(page, 0, 65535));
    ++count_;
    return true;
  }
  bool canPush() const { return count_ < CAPACITY; }
  const Entry* previous() const { return count_ > 1 ? &entries_[count_ - 2] : nullptr; }
  const Entry* current() const { return count_ ? &entries_[count_ - 1] : nullptr; }
  void setPage(int page) {
    if (count_) entries_[count_ - 1].page = static_cast<uint16_t>(std::clamp(page, 0, 65535));
  }
  bool pop() {
    if (count_ <= 1) return false;
    entries_[--count_] = {};
    return true;
  }
  size_t size() const { return count_; }

 private:
  std::array<Entry, CAPACITY> entries_{};
  size_t count_ = 0;
};

struct WordSpan {
  size_t offset = 0;
  size_t length = 0;
};

inline bool isWordCodepoint(uint32_t cp) {
  if (cp < 0x80)
    return (cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || cp == '\'' || cp == '-';
  if (cp == 0xA0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 || cp == 0x2029 || cp == 0x202F ||
      cp == 0x205F || cp == 0x3000)
    return false;
  if ((cp >= 0x2000 && cp <= 0x206F) || (cp >= 0x3000 && cp <= 0x303F)) return cp == 0x2019;
  return cp != 0xFFFD;
}

// Byte spans refer to the existing wrapped line. No copied word list is kept.
// CJK ideographs remain selectable without spaces; combining marks stay with
// their base. Other scripts, including Thai, retain complete UTF-8 sequences.
inline bool nextWord(const std::string& line, size_t& cursor, WordSpan& result) {
  const auto* base = reinterpret_cast<const unsigned char*>(line.c_str());
  while (cursor < line.size()) {
    const size_t start = cursor;
    const auto* p = base + cursor;
    const uint32_t cp = utf8NextCodepoint(&p);
    cursor = static_cast<size_t>(p - base);
    if (!isWordCodepoint(cp) || cp == '\'' || cp == '-' || cp == 0x2019) continue;
    const bool cjk = utf8IsCjkBreakable(cp);
    while (cursor < line.size()) {
      const auto* next = base + cursor;
      const uint32_t nextCp = utf8NextCodepoint(&next);
      if (!utf8IsCombiningMark(nextCp) && (cjk || utf8IsCjkBreakable(nextCp) || !isWordCodepoint(nextCp))) break;
      cursor = static_cast<size_t>(next - base);
    }
    result = {start, cursor - start};
    return true;
  }
  return false;
}
}  // namespace DictionaryNavigation
