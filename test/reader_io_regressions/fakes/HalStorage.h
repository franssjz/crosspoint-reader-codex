#pragma once
#include <Print.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

// In-memory I/O boundary. Production ZipFile, Expat and chapter layout are used
// unchanged; only SD access and renderer metrics are replaced for host tests.
struct TestFileData {
  std::vector<uint8_t> bytes;
  size_t failOffset = std::numeric_limits<size_t>::max();
  int failure = -1;
  size_t reads = 0;
};

class HalFile : public Print {
  friend class HalStorage;
  std::shared_ptr<TestFileData> data;
  size_t offset = 0;

 public:
  using Print::write;
  HalFile() = default;
  HalFile(HalFile&&) = default;
  HalFile& operator=(HalFile&&) = default;
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  int read(void* output, const size_t length) {
    if (!data) return -1;
    ++data->reads;
    if (offset == data->failOffset) return data->failure;
    const size_t count = std::min(length, data->bytes.size() - std::min(offset, data->bytes.size()));
    if (count) std::memcpy(output, data->bytes.data() + offset, count);
    offset += count;
    return static_cast<int>(count);
  }
  int read() {
    uint8_t result = 0;
    return read(&result, 1) == 1 ? result : -1;
  }
  size_t write(const uint8_t* source, const size_t length) override {
    if (!data) return 0;
    if (offset + length > data->bytes.size()) data->bytes.resize(offset + length);
    if (length) std::memcpy(data->bytes.data() + offset, source, length);
    offset += length;
    return length;
  }
  size_t write(const void* source, const size_t length) { return write(static_cast<const uint8_t*>(source), length); }
  bool seek(const size_t position) {
    offset = position;
    return data != nullptr;
  }
  bool seekCur(const int64_t amount) {
    if (!data || (amount < 0 && static_cast<uint64_t>(-amount) > offset)) return false;
    offset = static_cast<size_t>(static_cast<int64_t>(offset) + amount);
    return true;
  }
  size_t position() const { return offset; }
  size_t size() const { return data ? data->bytes.size() : 0; }
  uint64_t fileSize64() const { return static_cast<uint64_t>(size()); }
  size_t available() const { return size() - std::min(offset, size()); }
  bool isOpen() const { return data != nullptr; }
  bool flush() { return isOpen(); }
  explicit operator bool() const { return isOpen(); }
  bool close() {
    data.reset();
    return true;
  }
};
using FsFile = HalFile;

class HalStorage {
  std::map<std::string, std::shared_ptr<TestFileData>> files;

 public:
  static HalStorage& getInstance() {
    static HalStorage storage;
    return storage;
  }
  void reset() { files.clear(); }
  std::shared_ptr<TestFileData> put(const std::string& path, std::vector<uint8_t> bytes) {
    auto value = std::make_shared<TestFileData>();
    value->bytes = std::move(bytes);
    files[path] = value;
    return value;
  }
  std::shared_ptr<TestFileData> put(const std::string& path, const std::string& text) {
    return put(path, std::vector<uint8_t>(text.begin(), text.end()));
  }
  bool openFileForRead(const char*, const std::string& path, HalFile& file) const {
    const auto found = files.find(path);
    if (found == files.end()) return false;
    file.data = found->second;
    file.offset = 0;
    return true;
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    file.data = put(path, std::vector<uint8_t>{});
    file.offset = 0;
    return true;
  }
  bool exists(const char* path) const { return files.count(path) != 0; }
  bool remove(const char* path) { return files.erase(path) != 0; }
  bool rename(const char* from, const char* to) {
    const auto found = files.find(from);
    if (found == files.end() || files.count(to)) return false;
    files.emplace(to, found->second);
    files.erase(found);
    return true;
  }
};
#define Storage HalStorage::getInstance()
