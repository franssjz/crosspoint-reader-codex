#include "BookIdentity.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <MD5Builder.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "KOReaderDocumentId.h"

namespace {
struct CachedBookIdentity {
  std::string path;
  size_t fileSize = 0;
  std::string bookId;
};

std::vector<CachedBookIdentity>& getIdentityCache() {
  static std::vector<CachedBookIdentity> cache;
  return cache;
}

std::string md5Hex(const std::string& input) {
  MD5Builder md5;
  md5.begin();
  md5.add(input.c_str());
  md5.calculate();
  return md5.toString().c_str();
}

bool isLowerHex32(const std::string& value) {
  if (value.size() != 32) {
    return false;
  }

  return std::all_of(value.begin(), value.end(),
                     [](const char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); });
}

std::string makeSafeDirectoryId(const std::string& bookId) { return isLowerHex32(bookId) ? bookId : md5Hex(bookId); }

size_t getFileSize(const std::string& path) {
  HalFile file;
  if (!Storage.openFileForRead("BID", path, file)) {
    return 0;
  }

  const size_t fileSize = file.fileSize();
  file.close();
  return fileSize;
}
}  // namespace

namespace BookIdentity {

std::string normalizePath(const std::string& path) {
  if (path.empty()) {
    return "";
  }

  std::string normalized = FsHelpers::normalisePath(path);
  if (normalized.empty()) {
    return "";
  }

  if (normalized.front() != '/') {
    normalized.insert(normalized.begin(), '/');
  }
  return normalized;
}

std::string getFileExtensionLower(const std::string& path) {
  const std::string normalized = normalizePath(path);
  const size_t dotPos = normalized.find_last_of('.');
  if (dotPos == std::string::npos) {
    return "";
  }

  std::string extension = normalized.substr(dotPos);
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return extension;
}

std::string calculateContentBookId(const std::string& path) {
  const std::string normalizedPath = normalizePath(path);
  if (normalizedPath.empty() || !Storage.exists(normalizedPath.c_str())) {
    return "";
  }

  const size_t fileSize = getFileSize(normalizedPath);
  auto& cache = getIdentityCache();
  for (auto it = cache.begin(); it != cache.end(); ++it) {
    if (it->path == normalizedPath && it->fileSize == fileSize) {
      if (it != cache.begin()) {
        CachedBookIdentity entry = *it;
        cache.erase(it);
        cache.insert(cache.begin(), std::move(entry));
      }
      return cache.front().bookId;
    }
  }

  const std::string bookId = KOReaderDocumentId::calculate(normalizedPath);
  if (bookId.empty()) {
    return "";
  }

  cache.insert(cache.begin(), CachedBookIdentity{normalizedPath, fileSize, bookId});
  if (cache.size() > 32) {
    cache.pop_back();
  }
  return bookId;
}

std::string resolveStableBookId(const std::string& path) {
  const std::string normalizedPath = normalizePath(path);
  if (normalizedPath.empty()) {
    return "";
  }

  if (const std::string contentId = calculateContentBookId(normalizedPath); !contentId.empty()) {
    return contentId;
  }

  return "legacy:" + normalizedPath;
}

bool isLegacyBookId(const std::string& bookId) { return bookId.rfind("legacy:", 0) == 0; }

std::string getStableDataDir(const std::string& bookId) {
  if (bookId.empty()) {
    return "";
  }
  return "/.crosspoint/bookdata/" + makeSafeDirectoryId(bookId);
}

std::string getStableDataFilePath(const std::string& bookId, const std::string& filename) {
  const std::string directory = getStableDataDir(bookId);
  if (directory.empty()) {
    return "";
  }
  return directory + "/" + filename;
}

void ensureStableDataDir(const std::string& bookId) {
  const std::string directory = getStableDataDir(bookId);
  if (directory.empty()) {
    return;
  }

  Storage.mkdir("/.crosspoint");
  Storage.mkdir("/.crosspoint/bookdata");
  Storage.mkdir(directory.c_str());
}

}  // namespace BookIdentity
