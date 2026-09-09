#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>

// Optional acceleration only. Failure to allocate this cache must not prevent
// opening a book or turn its progress into zero. Large books use the same SD
// lookup as an allocation failure, with no second copy of chapter href strings.
class CumulativeSizeCache {
 public:
  static constexpr size_t MAX_ENTRIES = 1024;
  using Allocate = void* (*)(size_t);
  explicit CumulativeSizeCache(Allocate allocate = std::malloc) : allocate_(allocate) {}
  ~CumulativeSizeCache() { clear(); }
  CumulativeSizeCache(const CumulativeSizeCache&) = delete;
  CumulativeSizeCache& operator=(const CumulativeSizeCache&) = delete;

  void clear() {
    std::free(values_);
    values_ = nullptr;
    attempted_ = false;
    count_ = 0;
  }

  template <class ReadValue>
  bool get(size_t index, size_t count, ReadValue read, uint32_t& output) {
    if (index >= count) return false;
    if (!attempted_) {
      attempted_ = true;
      if (count && count <= MAX_ENTRIES) {
        values_ = static_cast<uint32_t*>(allocate_(count * sizeof(uint32_t)));
        if (values_) {
          count_ = count;
          for (size_t i = 0; i < count; ++i) {
            if (!read(i, values_[i])) {
              std::free(values_);
              values_ = nullptr;
              count_ = 0;
              break;
            }
          }
        }
      }
    }
    if (values_ && count == count_) {
      output = values_[index];
      return true;
    }
    return read(index, output);
  }

  size_t cachedEntries() const { return count_; }

 private:
  Allocate allocate_;
  uint32_t* values_ = nullptr;
  size_t count_ = 0;
  bool attempted_ = false;
};

// The book.bin format is unchanged: LUT offset -> uint32 href length -> href
// bytes -> uint32 cumulative size -> int16 TOC index. Check every range before
// a seek so a damaged length cannot allocate or skip beyond the actual file.
template <class File>
bool readCumulativeSizeFromLut(File& file, uint32_t lutOffset, size_t index, uint64_t firstEntryOffset,
                               uint32_t& output) {
  const uint64_t fileSize = file.fileSize64();
  const uint64_t lutPosition = static_cast<uint64_t>(lutOffset) + index * sizeof(uint32_t);
  if (lutPosition > fileSize || fileSize - lutPosition < sizeof(uint32_t) || !file.seek64(lutPosition)) return false;
  uint32_t entryPosition = 0, hrefLength = 0;
  if (file.read(&entryPosition, sizeof(entryPosition)) != sizeof(entryPosition) || entryPosition < firstEntryOffset ||
      entryPosition > fileSize || fileSize - entryPosition < 10 || !file.seek64(entryPosition) ||
      file.read(&hrefLength, sizeof(hrefLength)) != sizeof(hrefLength))
    return false;
  const uint64_t sizePosition = static_cast<uint64_t>(entryPosition) + sizeof(uint32_t) + hrefLength;
  if (sizePosition > fileSize || fileSize - sizePosition < sizeof(uint32_t) + sizeof(int16_t) ||
      !file.seek64(sizePosition))
    return false;
  return file.read(&output, sizeof(output)) == sizeof(output);
}
