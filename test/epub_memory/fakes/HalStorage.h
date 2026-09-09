#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

class FsFile {
  std::vector<uint8_t>* bytes_ = nullptr;
  size_t position_ = 0;

 public:
  inline static size_t writeBudget = SIZE_MAX, readBudget = SIZE_MAX, maxRead = 0;
  inline static bool failClose = false;
  void open(std::vector<uint8_t>& bytes, bool writing) {
    bytes_ = &bytes;
    position_ = 0;
    if (writing) bytes.clear();
  }
  explicit operator bool() const { return bytes_ != nullptr; }
  uint64_t fileSize64() const { return bytes_ ? bytes_->size() : 0; }
  size_t position() const { return position_; }
  size_t available() const { return bytes_ && position_ < bytes_->size() ? bytes_->size() - position_ : 0; }
  bool seek64(uint64_t position) {
    if (!bytes_ || position > bytes_->size()) return false;
    position_ = position;
    return true;
  }
  bool seek(size_t position) { return seek64(position); }
  int read(void* output, size_t count) {
    if (!bytes_ || !readBudget) return -1;
    maxRead = std::max(maxRead, count);
    count = std::min({count, available(), readBudget});
    if (count) std::memcpy(output, bytes_->data() + position_, count);
    position_ += count;
    if (readBudget != SIZE_MAX) readBudget -= count;
    return static_cast<int>(count);
  }
  size_t write(const void* data, size_t count) {
    if (!bytes_) return 0;
    count = std::min(count, writeBudget);
    if (writeBudget != SIZE_MAX) writeBudget -= count;
    bytes_->resize(std::max(bytes_->size(), position_ + count));
    if (count) std::memcpy(bytes_->data() + position_, data, count);
    position_ += count;
    return count;
  }
  size_t write(uint8_t value) { return write(&value, 1); }
  void flush() {}
  bool close() {
    bytes_ = nullptr;
    return !failClose;
  }
};
using HalFile = FsFile;
class FakeStorage {
 public:
  std::map<std::string, std::vector<uint8_t>> files;
  bool exists(const char* path) const { return files.count(path) != 0; }
  bool remove(const char* path) { return files.erase(path) != 0; }
  bool rename(const char* from, const char* to) {
    if (!exists(from) || exists(to)) return false;
    files[to] = std::move(files[from]);
    files.erase(from);
    return true;
  }
  bool openFileForWrite(const char*, const std::string& path, FsFile& file) {
    file.open(files[path], true);
    return true;
  }
  bool openFileForRead(const char*, const std::string& path, FsFile& file) {
    const auto found = files.find(path);
    if (found == files.end()) return false;
    file.open(found->second, false);
    return true;
  }
};
inline FakeStorage Storage;
