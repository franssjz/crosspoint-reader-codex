#include "util/BookIdentity.h"

#include <filesystem>

namespace BookIdentity {

std::string getStableDataDir(const std::string& bookId) { return bookId; }

std::string getStableDataFilePath(const std::string& bookId, const std::string& filename) {
  return (std::filesystem::path(bookId) / filename).string();
}

void ensureStableDataDir(const std::string& bookId) { std::filesystem::create_directories(bookId); }

}  // namespace BookIdentity
