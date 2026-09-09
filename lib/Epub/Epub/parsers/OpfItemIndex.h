#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string_view>

// No growing STL container in the manifest index: table/chunk allocations are
// explicit and fallible. 128 entries/chunk keep the largest entry allocation at
// 1536 bytes; sorting and binary search use O(1) indexed access without a second
// full copy. The total still grows with the manifest.
class OpfItemIndex {
 public:
  struct Entry {
    uint32_t idHash, idLen, fileOffset;
  };
  using Allocate = void* (*)(size_t);
  static constexpr size_t CHUNK_ENTRIES = 128;
  static constexpr size_t MAX_CHUNKS = 512;
  explicit OpfItemIndex(Allocate allocate = std::malloc) : allocate_(allocate) {}
  ~OpfItemIndex() { clear(); }
  OpfItemIndex(const OpfItemIndex&) = delete;
  OpfItemIndex& operator=(const OpfItemIndex&) = delete;

  void clear() {
    for (size_t i = 0; i < chunks_; ++i) std::free(table_[i]);
    std::free(table_);
    table_ = nullptr;
    tableCapacity_ = chunks_ = count_ = 0;
  }
  size_t size() const { return count_; }
  bool empty() const { return count_ == 0; }
  Entry& operator[](size_t index) { return table_[index / CHUNK_ENTRIES][index % CHUNK_ENTRIES]; }
  const Entry& operator[](size_t index) const { return table_[index / CHUNK_ENTRIES][index % CHUNK_ENTRIES]; }

  bool append(const Entry& entry) {
    if (count_ == chunks_ * CHUNK_ENTRIES) {
      if (chunks_ == MAX_CHUNKS) return false;
      if (chunks_ == tableCapacity_) {
        const size_t capacity = tableCapacity_ ? tableCapacity_ * 2 : 16;
        auto** next = static_cast<Entry**>(allocate_(capacity * sizeof(Entry*)));
        if (!next) return false;
        if (chunks_) std::memcpy(next, table_, chunks_ * sizeof(Entry*));
        std::free(table_);
        table_ = next;
        tableCapacity_ = capacity;
      }
      auto* chunk = static_cast<Entry*>(allocate_(CHUNK_ENTRIES * sizeof(Entry)));
      if (!chunk) return false;
      table_[chunks_++] = chunk;
    }
    (*this)[count_++] = entry;
    return true;
  }

  static bool less(const Entry& a, const Entry& b) {
    return a.idHash < b.idHash || (a.idHash == b.idHash && a.idLen < b.idLen);
  }
  void sort() {
    // In-place heap sort avoids an iterator abstraction and allocation/recursion.
    if (count_ < 2) return;
    for (size_t i = count_ / 2; i > 0; --i) siftDown(i - 1, count_);
    for (size_t end = count_ - 1; end > 0; --end) {
      std::swap((*this)[0], (*this)[end]);
      siftDown(0, end);
    }
  }
  size_t lowerBound(uint32_t hash, uint32_t length) const {
    size_t first = 0, last = count_;
    const Entry target{hash, length, 0};
    while (first < last) {
      const size_t middle = first + (last - first) / 2;
      if (less((*this)[middle], target))
        first = middle + 1;
      else
        last = middle;
    }
    return first;
  }

 private:
  Allocate allocate_;
  Entry** table_ = nullptr;
  size_t tableCapacity_ = 0, chunks_ = 0, count_ = 0;
  void siftDown(size_t root, size_t end) {
    while (root < end / 2) {
      size_t child = root * 2 + 1;
      if (child + 1 < end && less((*this)[child], (*this)[child + 1])) ++child;
      if (!less((*this)[root], (*this)[child])) return;
      std::swap((*this)[root], (*this)[child]);
      root = child;
    }
  }
};

// Consume and compare an entire length-prefixed ID with 64 bytes of scratch.
// A colliding hash is only a candidate; it never identifies a different item.
template <class File>
bool readStoredOpfId(File& file, std::string_view expected, bool& matches) {
  matches = false;
  uint32_t length = 0;
  const uint64_t initial = file.position(), fileSize = file.fileSize64();
  if (initial > fileSize || fileSize - initial < sizeof(length) || file.read(&length, sizeof(length)) != sizeof(length))
    return false;
  if (length > fileSize - initial - sizeof(length)) return false;
  if (length != expected.size()) return file.seek64(initial + sizeof(length) + length);
  char scratch[64];
  matches = true;
  for (size_t offset = 0; offset < length;) {
    const size_t count = std::min<size_t>(sizeof(scratch), length - offset);
    if (file.read(scratch, count) != static_cast<int>(count)) return false;
    if (std::memcmp(scratch, expected.data() + offset, count) != 0) matches = false;
    offset += count;
  }
  return true;
}
