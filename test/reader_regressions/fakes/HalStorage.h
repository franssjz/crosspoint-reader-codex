#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

class HalFile {
  friend class HalStorage;
  std::fstream stream;
  std::filesystem::path path;
  std::filesystem::directory_iterator iterator;
  std::filesystem::directory_iterator iteratorEnd;
  bool directory = false;
  bool writing = false;

 public:
  HalFile() = default;
  HalFile(HalFile&&) = default;
  HalFile& operator=(HalFile&&) = default;
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  int read(void* buffer, const size_t count) {
    stream.read(static_cast<char*>(buffer), static_cast<std::streamsize>(count));
    return static_cast<int>(stream.gcount());
  }
  size_t write(const void* buffer, const size_t count) {
    stream.write(static_cast<const char*>(buffer), static_cast<std::streamsize>(count));
    return stream ? count : 0;
  }
  bool seekCur(const int64_t offset) {
    stream.clear();
    stream.seekg(offset, std::ios::cur);
    return static_cast<bool>(stream);
  }
  uint32_t position() { return static_cast<uint32_t>(writing ? stream.tellp() : stream.tellg()); }
  bool seekSet(const uint32_t offset) {
    stream.clear();
    if (writing)
      stream.seekp(offset, std::ios::beg);
    else
      stream.seekg(offset, std::ios::beg);
    return static_cast<bool>(stream);
  }
  bool close() {
    if (!stream.is_open()) return true;
    stream.close();
    return !stream.fail();
  }
  void flush() { stream.flush(); }
  bool isDirectory() const { return directory; }
  void getName(char* output, const size_t outputSize) const {
    if (!output || outputSize == 0) return;
    const std::string name = path.filename().string();
    const size_t length = std::min(name.size(), outputSize - 1);
    name.copy(output, length);
    output[length] = '\0';
  }
  HalFile openNextFile() {
    if (!directory || iterator == iteratorEnd) return {};
    const auto entry = *iterator;
    ++iterator;
    HalFile file;
    file.path = entry.path();
    file.directory = entry.is_directory();
    if (file.directory) {
      std::error_code error;
      file.iterator = std::filesystem::directory_iterator(file.path, error);
      if (error) file.directory = false;
    } else {
      file.stream.open(file.path, std::ios::binary | std::ios::in);
    }
    return file;
  }
  explicit operator bool() const { return directory || stream.is_open(); }
};

using FsFile = HalFile;

class HalStorage {
 private:
  std::filesystem::path logicalRoot;

  std::filesystem::path resolve(const char* path) const {
    if (!logicalRoot.empty() && path && path[0] == '/') {
      return path[1] == '\0' ? logicalRoot : logicalRoot / (path + 1);
    }
    return path ? std::filesystem::path(path) : std::filesystem::path();
  }

 public:
  static HalStorage& getInstance() {
    static HalStorage storage;
    return storage;
  }

  void setTestRootPath(const std::filesystem::path& root) { logicalRoot = root; }
  void clearTestRootPath() { logicalRoot.clear(); }

  bool exists(const char* path) const { return std::filesystem::exists(resolve(path)); }
  bool remove(const char* path) const { return std::filesystem::remove(resolve(path)); }
  bool rename(const char* from, const char* to) const {
    std::error_code error;
    std::filesystem::rename(resolve(from), resolve(to), error);
    return !error;
  }
  bool mkdir(const char* path, const bool = true) const {
    std::error_code error;
    std::filesystem::create_directories(resolve(path), error);
    return !error;
  }
  HalFile open(const char* path) const {
    HalFile file;
    file.path = resolve(path);
    std::error_code error;
    file.directory = std::filesystem::is_directory(file.path, error);
    if (error) return {};
    if (file.directory) {
      file.iterator = std::filesystem::directory_iterator(file.path, error);
      if (error) return {};
    } else {
      file.stream.open(file.path, std::ios::binary | std::ios::in);
    }
    return file;
  }
  bool openFileForRead(const char*, const std::string& path, HalFile& file) const {
    file.path = resolve(path.c_str());
    file.stream.open(file.path, std::ios::binary | std::ios::in);
    file.writing = false;
    return file.stream.is_open();
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) const {
    file.path = resolve(path.c_str());
    file.stream.open(file.path, std::ios::binary | std::ios::out | std::ios::trunc);
    file.writing = true;
    return file.stream.is_open();
  }
};

#define Storage HalStorage::getInstance()
