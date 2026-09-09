#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <utility>

class FsFile {
 public:
  inline static size_t writeBudget = SIZE_MAX;
  inline static std::set<std::filesystem::path> openReaders;
  inline static size_t maxReadSize = 0;

 private:
  std::fstream file;
  std::filesystem::path path;
  bool writing = false;

 public:
  ~FsFile() { close(); }
  bool open(const std::filesystem::path& value, const bool write) {
    close();
    if (!write && openReaders.count(value)) return false;
    path = value;
    writing = write;
    file.open(path, std::ios::binary | (write ? std::ios::out | std::ios::trunc : std::ios::in));
    if (file.is_open() && !write) openReaders.insert(path);
    return file.is_open();
  }
  bool close() {
    if (!file.is_open()) return true;
    if (!writing) openReaders.erase(path);
    file.clear();
    file.close();
    return !file.fail();
  }
  bool isOpen() const { return file.is_open(); }
  explicit operator bool() const { return isOpen(); }
  bool seek64(const uint64_t offset) {
    file.clear();
    if (writing)
      file.seekp(static_cast<std::streamoff>(offset));
    else
      file.seekg(static_cast<std::streamoff>(offset));
    return !file.fail();
  }
  bool seek(const uint32_t offset) { return seek64(offset); }
  int read(void* buffer, const size_t count) {
    maxReadSize = std::max(maxReadSize, count);
    file.read(static_cast<char*>(buffer), static_cast<std::streamsize>(count));
    return static_cast<int>(file.gcount());
  }
  size_t write(const void* buffer, const size_t count) {
    const size_t allowed = std::min(count, writeBudget);
    if (writeBudget != SIZE_MAX) writeBudget -= allowed;
    file.write(static_cast<const char*>(buffer), static_cast<std::streamsize>(allowed));
    return file ? allowed : 0;
  }
  uint64_t fileSize64() const {
    std::error_code error;
    const auto result = std::filesystem::file_size(path, error);
    return error ? 0 : result;
  }
  uint64_t size() const { return fileSize64(); }
};

class HalStorage {
  std::filesystem::path root;
  std::filesystem::path resolve(const char* path) const { return root / (path[0] == '/' ? path + 1 : path); }

 public:
  void setRoot(std::filesystem::path path) { root = std::move(path); }
  bool exists(const char* path) const { return std::filesystem::exists(resolve(path)); }
  bool mkdir(const char* path) const {
    std::error_code error;
    std::filesystem::create_directories(resolve(path), error);
    return !error;
  }
  bool remove(const char* path) const {
    std::error_code error;
    return std::filesystem::remove(resolve(path), error);
  }
  bool removeDir(const char* path) const {
    std::error_code error;
    std::filesystem::remove_all(resolve(path), error);
    return !error;
  }
  bool rename(const char* from, const char* to) const {
    if (exists(to)) return false;
    std::error_code error;
    std::filesystem::rename(resolve(from), resolve(to), error);
    return !error;
  }
  bool openFileForRead(const char*, const char* path, FsFile& file) { return file.open(resolve(path), false); }
  bool openFileForRead(const char* tag, const std::string& path, FsFile& file) {
    return openFileForRead(tag, path.c_str(), file);
  }
  bool openFileForWrite(const char*, const std::string& path, FsFile& file) {
    return file.open(resolve(path.c_str()), true);
  }
};

inline HalStorage Storage;
