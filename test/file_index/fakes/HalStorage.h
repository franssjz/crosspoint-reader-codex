#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>
constexpr int O_RDONLY = 0;
constexpr int O_RDWR = 2;
constexpr int O_CREAT = 4;
constexpr int O_TRUNC = 8;
class HalFile;
class TestStorage {
 public:
  struct Node {
    bool directory = false;
    std::vector<uint8_t> bytes;
  };
  std::map<std::string, Node> nodes;
  std::map<std::string, int> handles;
  std::map<std::string, int> passes;
  std::string failureDirectory;
  int failAfterEntry = -1;
  int failOnPass = 0;
  bool failAsAllocation = false;
  std::string failName;
  int64_t writeBudget = -1;
  size_t writeCalls = 0;
  size_t largestRead = 0;
  bool rejectWrites = false;
  static TestStorage& instance() {
    static TestStorage storage;
    return storage;
  }
  void reset() {
    *this = TestStorage{};
    nodes["/"].directory = true;
  }
  static std::string normalize(std::string path) {
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    return path;
  }
  void add(const std::string& path, bool dir = false, const std::string& data = "") {
    nodes[path] = Node{dir, std::vector<uint8_t>(data.begin(), data.end())};
  }
  bool exists(const char* path) const { return nodes.count(normalize(path)) != 0; }
  bool ensureDirectoryExists(const char* path) {
    auto key = normalize(path);
    if (nodes.count(key)) return nodes.at(key).directory;
    if (rejectWrites) return false;
    nodes[key].directory = true;
    return true;
  }
  bool remove(const char* path) {
    const auto key = normalize(path);
    if (rejectWrites || handles[key] != 0) return false;
    return nodes.erase(key) != 0;
  }
  bool rename(const char* from, const char* to) {
    const std::string a = normalize(from), b = normalize(to);
    if (rejectWrites || !nodes.count(a) || nodes.count(b) || handles[a] || handles[b]) return false;
    auto node = nodes.extract(a);
    node.key() = b;
    nodes.insert(std::move(node));
    return true;
  }
  HalFile open(const char* path, int flags = O_RDONLY);
};
#define Storage TestStorage::instance()
class HalFile {
  TestStorage::Node* node = nullptr;
  std::string path;
  size_t cursor = 0;
  std::vector<std::string> children;
  size_t next = 0;
  bool allocationError = false;
  bool iterationError = false;
  bool writable = false;
  friend class TestStorage;
  HalFile(TestStorage::Node* node, std::string path, bool writable)
      : node(node), path(std::move(path)), writable(writable) {
    ++Storage.handles[this->path];
  }

 public:
  HalFile() = default;
  ~HalFile() { close(); }
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  HalFile(HalFile&& other) noexcept { *this = std::move(other); }
  HalFile& operator=(HalFile&& other) noexcept {
    if (this != &other) {
      close();
      node = other.node;
      path = std::move(other.path);
      cursor = other.cursor;
      children = std::move(other.children);
      next = other.next;
      writable = other.writable;
      allocationError = other.allocationError;
      iterationError = other.iterationError;
      other.node = nullptr;
    }
    return *this;
  }
  operator bool() const { return node != nullptr; }
  bool isOpen() const { return node != nullptr; }
  bool close() {
    if (node) {
      --Storage.handles[path];
      node = nullptr;
    }
    return true;
  }
  bool isDirectory() const { return node && node->directory; }
  uint64_t fileSize64() const { return node ? node->bytes.size() : 0; }
  size_t position() const { return cursor; }
  bool seek(size_t offset) {
    if (!node) return false;
    cursor = offset;
    return true;
  }
  void flush() {}
  int read(void* out, size_t count) {
    if (!node || node->directory) return -1;
    Storage.largestRead = std::max(Storage.largestRead, count);
    const size_t n = std::min(count, node->bytes.size() - std::min(cursor, node->bytes.size()));
    if (n) memcpy(out, node->bytes.data() + cursor, n);
    cursor += n;
    return static_cast<int>(n);
  }
  size_t write(const void* data, size_t count) {
    ++Storage.writeCalls;
    if (!node || !writable || Storage.rejectWrites) return 0;
    size_t n = count;
    if (Storage.writeBudget >= 0) {
      n = std::min<size_t>(count, static_cast<size_t>(Storage.writeBudget));
      Storage.writeBudget -= n;
    }
    if (cursor + n > node->bytes.size()) node->bytes.resize(cursor + n);
    if (n) memcpy(node->bytes.data() + cursor, data, n);
    cursor += n;
    return n;
  }
  size_t getName(char* out, size_t cap) {
    const auto name = path.substr(path.find_last_of('/') + 1);
    if (name == Storage.failName || name.size() >= cap) {
      if (cap) out[0] = 0;
      return 0;
    }
    memcpy(out, name.c_str(), name.size() + 1);
    return name.size();
  }
  void rewindDirectory() {
    children.clear();
    next = 0;
    allocationError = false;
    ++Storage.passes[path];
    const auto prefix = path == "/" ? path : path + "/";
    for (const auto& [key, value] : Storage.nodes) {
      if (key.size() > prefix.size() && key.rfind(prefix, 0) == 0 && key.find('/', prefix.size()) == std::string::npos)
        children.push_back(key);
    }
  }
  HalFile openNextFile() {
    allocationError = iterationError = false;
    if (!node || !node->directory) return {};
    if (path == Storage.failureDirectory && Storage.failAfterEntry >= 0 &&
        static_cast<int>(next) >= Storage.failAfterEntry &&
        (Storage.failOnPass == 0 || Storage.passes[path] == Storage.failOnPass)) {
      allocationError = Storage.failAsAllocation;
      iterationError = !allocationError;
      return {};
    }
    if (next >= children.size()) return {};
    return Storage.open(children[next++].c_str());
  }
  bool allocationFailed() const { return allocationError; }
  bool iterationFailed() const { return iterationError; }
};
inline HalFile TestStorage::open(const char* raw, int flags) {
  const auto path = normalize(raw);
  auto found = nodes.find(path);
  if (found == nodes.end()) {
    if (!(flags & O_CREAT) || rejectWrites) return {};
    found = nodes.emplace(path, Node{}).first;
  }
  const bool writable = (flags & O_RDWR) != 0;
  if (writable && rejectWrites) return {};
  if (flags & O_TRUNC) found->second.bytes.clear();
  return HalFile(&found->second, path, writable);
}
