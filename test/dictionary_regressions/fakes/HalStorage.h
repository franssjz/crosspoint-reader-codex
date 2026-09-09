#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class String : public std::string {
 public:
  using std::string::string;
  String(std::string value) : std::string(std::move(value)) {}
  bool isEmpty() const { return empty(); }
};

struct DictionaryTestFile {
  std::vector<uint8_t> bytes;
  size_t failReadAt = std::numeric_limits<size_t>::max();
  int readResult = -1;
  size_t writeLimit = std::numeric_limits<size_t>::max();
  bool failSeek = false;
};

class HalFile {
  friend class HalStorage;
  std::shared_ptr<DictionaryTestFile> data;
  size_t offset = 0;

 public:
  HalFile() = default;
  HalFile(HalFile&&) = default;
  HalFile& operator=(HalFile&&) = default;
  HalFile(const HalFile&) = delete;
  int read(void* output, size_t length) {
    if (!data) return -1;
    if (offset >= data->failReadAt) return data->readResult;
    const size_t count =
        std::min({length, data->bytes.size() - std::min(offset, data->bytes.size()), data->failReadAt - offset});
    if (count) std::memcpy(output, data->bytes.data() + offset, count);
    offset += count;
    return static_cast<int>(count);
  }
  int read() {
    uint8_t value = 0;
    return read(&value, 1) == 1 ? value : -1;
  }
  size_t write(const void* source, size_t length) {
    if (!data) return 0;
    const size_t count = std::min(length, data->writeLimit - std::min(data->writeLimit, offset));
    if (offset + count > data->bytes.size()) data->bytes.resize(offset + count);
    if (count) std::memcpy(data->bytes.data() + offset, source, count);
    offset += count;
    return count;
  }
  size_t write(uint8_t value) { return write(&value, 1); }
  bool seekSet(size_t target) {
    if (!data || data->failSeek || target > data->bytes.size()) return false;
    offset = target;
    return true;
  }
  bool seekCur(int64_t amount) { return amount >= -static_cast<int64_t>(offset) && seekSet(offset + amount); }
  size_t position() const { return offset; }
  size_t fileSize() { return data ? data->bytes.size() : 0; }
  void flush() {}
  bool close() {
    data.reset();
    return true;
  }
  explicit operator bool() const { return data != nullptr; }
  bool isDirectory() const { return false; }
  void rewindDirectory() {}
  HalFile openNextFile() { return {}; }
  size_t getName(char* buffer, size_t size) {
    if (size) buffer[0] = 0;
    return 0;
  }
};
using FsFile = HalFile;

class HalStorage {
 public:
  std::map<std::string, std::shared_ptr<DictionaryTestFile>> files;
  std::set<std::string> failOpen;
  std::set<std::string> failRenameFrom;
  size_t nextWriteLimit = std::numeric_limits<size_t>::max();
  static HalStorage& getInstance() {
    static HalStorage value;
    return value;
  }
  void reset() {
    files.clear();
    failOpen.clear();
    failRenameFrom.clear();
    nextWriteLimit = std::numeric_limits<size_t>::max();
  }
  std::shared_ptr<DictionaryTestFile> put(const std::string& path, std::vector<uint8_t> bytes) {
    auto file = std::make_shared<DictionaryTestFile>();
    file->bytes = std::move(bytes);
    files[path] = file;
    return file;
  }
  std::shared_ptr<DictionaryTestFile> put(const std::string& path, const std::string& text) {
    return put(path, std::vector<uint8_t>(text.begin(), text.end()));
  }
  bool exists(const char* path) const { return files.count(path) != 0; }
  bool openFileForRead(const char*, const std::string& path, HalFile& file) const {
    auto it = files.find(path);
    if (it == files.end() || failOpen.count(path)) return false;
    file.data = it->second;
    file.offset = 0;
    return true;
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    if (failOpen.count(path)) return false;
    file.data = put(path, std::vector<uint8_t>{});
    file.data->writeLimit = nextWriteLimit;
    nextWriteLimit = std::numeric_limits<size_t>::max();
    file.offset = 0;
    return true;
  }
  HalFile open(const char* path) const {
    HalFile file;
    openFileForRead("", path, file);
    return file;
  }
  bool remove(const char* path) { return files.erase(path) != 0; }
  bool mkdir(const char*) { return true; }
  bool rename(const char* from, const char* to) {
    auto it = files.find(from);
    if (it == files.end() || files.count(to) || failRenameFrom.count(from)) return false;
    files[to] = it->second;
    files.erase(it);
    return true;
  }
  String readFile(const char* path) const {
    auto it = files.find(path);
    if (it == files.end() || failOpen.count(path)) return {};
    return std::string(it->second->bytes.begin(), it->second->bytes.end());
  }
};
#define Storage HalStorage::getInstance()
