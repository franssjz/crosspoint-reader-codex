#include "RecentBooksStore.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>
#include <Serialization.h>
#include <Xtc.h>

#include <algorithm>

#include "ReadingStatsStore.h"
#include "util/BookIdentity.h"

namespace {
constexpr uint8_t RECENT_BOOKS_FILE_VERSION = 3;
constexpr char RECENT_BOOKS_FILE_BIN[] = "/.crosspoint/recent.bin";
constexpr char RECENT_BOOKS_FILE_JSON[] = "/.crosspoint/recent.json";
constexpr char RECENT_BOOKS_FILE_BAK[] = "/.crosspoint/recent.bin.bak";
constexpr int MAX_RECENT_BOOKS = 10;

std::string fallbackTitleFromPath(const std::string& path) {
  std::string filename = path;
  const size_t lastSlash = filename.find_last_of('/');
  if (lastSlash != std::string::npos) {
    filename = filename.substr(lastSlash + 1);
  }

  const size_t dotPos = filename.find_last_of('.');
  if (dotPos != std::string::npos) {
    filename = filename.substr(0, dotPos);
  }
  return filename;
}
}  // namespace

RecentBooksStore RecentBooksStore::instance;

int RecentBooksStore::findBookIndex(const std::string& path, const std::string& bookId) const {
  const std::string normalizedPath = BookIdentity::normalizePath(path);
  for (int index = 0; index < static_cast<int>(recentBooks.size()); ++index) {
    const auto& book = recentBooks[index];
    if (!bookId.empty() && !book.bookId.empty() && book.bookId == bookId) {
      return index;
    }
    if (!normalizedPath.empty() && book.path == normalizedPath) {
      return index;
    }
  }
  return -1;
}

void RecentBooksStore::normalizeBook(RecentBook& book) {
  book.path = BookIdentity::normalizePath(book.path);
  if (!book.bookId.empty()) {
    return;
  }

  if (!book.path.empty() && Storage.exists(book.path.c_str())) {
    book.bookId = BookIdentity::resolveStableBookId(book.path);
    return;
  }

  if (const auto* statsBook = READING_STATS.findMatchingBookForPath(book.path, book.title, book.author)) {
    book.bookId = statsBook->bookId;
  }
}

void RecentBooksStore::normalizeBooks() {
  for (auto& book : recentBooks) {
    normalizeBook(book);
  }

  std::vector<RecentBook> normalized;
  normalized.reserve(recentBooks.size());
  for (const auto& book : recentBooks) {
    const int existingIndex = [&normalized, &book]() {
      for (int index = 0; index < static_cast<int>(normalized.size()); ++index) {
        const auto& existing = normalized[index];
        if (!book.bookId.empty() && !existing.bookId.empty() && book.bookId == existing.bookId) {
          return index;
        }
        if (!book.path.empty() && existing.path == book.path) {
          return index;
        }
      }
      return -1;
    }();

    if (existingIndex < 0) {
      normalized.push_back(book);
      continue;
    }

    auto& existing = normalized[existingIndex];
    if (existing.bookId.empty()) {
      existing.bookId = book.bookId;
    }
    if (existing.path.empty() || (!book.path.empty() && Storage.exists(book.path.c_str()))) {
      existing.path = book.path;
    }
    if (existing.title.empty() && !book.title.empty()) {
      existing.title = book.title;
    }
    if (existing.author.empty() && !book.author.empty()) {
      existing.author = book.author;
    }
    if (existing.coverBmpPath.empty() && !book.coverBmpPath.empty()) {
      existing.coverBmpPath = book.coverBmpPath;
    }
  }

  recentBooks = std::move(normalized);
  if (recentBooks.size() > MAX_RECENT_BOOKS) {
    recentBooks.resize(MAX_RECENT_BOOKS);
  }
}

void RecentBooksStore::addBook(const std::string& path, const std::string& title, const std::string& author,
                               const std::string& coverBmpPath, const std::string& bookId) {
  // Drop stale entries first so a new add cannot evict a valid book in their stead.
  pruneMissing();

  const std::string normalizedPath = BookIdentity::normalizePath(path);
  const std::string resolvedBookId =
      !bookId.empty() ? bookId : (!normalizedPath.empty() ? BookIdentity::resolveStableBookId(normalizedPath) : "");

  const int existingIndex = findBookIndex(normalizedPath, resolvedBookId);
  if (existingIndex >= 0) {
    recentBooks.erase(recentBooks.begin() + existingIndex);
  }

  recentBooks.insert(recentBooks.begin(), {resolvedBookId, normalizedPath, title, author, coverBmpPath});

  if (recentBooks.size() > MAX_RECENT_BOOKS) {
    recentBooks.resize(MAX_RECENT_BOOKS);
  }

  saveToFile();
}

void RecentBooksStore::updateBook(const std::string& path, const std::string& title, const std::string& author,
                                  const std::string& coverBmpPath, const std::string& bookId) {
  const std::string normalizedPath = BookIdentity::normalizePath(path);
  const std::string resolvedBookId =
      !bookId.empty() ? bookId : (!normalizedPath.empty() ? BookIdentity::resolveStableBookId(normalizedPath) : "");
  const int existingIndex = findBookIndex(normalizedPath, resolvedBookId);
  if (existingIndex >= 0) {
    RecentBook& book = recentBooks[existingIndex];
    if (!resolvedBookId.empty()) {
      book.bookId = resolvedBookId;
    }
    if (!normalizedPath.empty()) {
      book.path = normalizedPath;
    }
    book.title = title;
    book.author = author;
    book.coverBmpPath = coverBmpPath;
    saveToFile();
  }
}

bool RecentBooksStore::updateBookPath(const std::string& oldKey, const std::string& newPath, const std::string& title,
                                      const std::string& author, const std::string& coverBmpPath,
                                      const std::string& bookId) {
  const std::string normalizedNewPath = BookIdentity::normalizePath(newPath);
  if (normalizedNewPath.empty()) {
    return false;
  }

  const std::string resolvedBookId =
      !bookId.empty() ? bookId
                      : (!normalizedNewPath.empty() ? BookIdentity::resolveStableBookId(normalizedNewPath) : "");
  const int existingIndex = findBookIndex(oldKey, resolvedBookId);
  if (existingIndex < 0) {
    return false;
  }

  RecentBook& book = recentBooks[existingIndex];
  if (!resolvedBookId.empty()) {
    book.bookId = resolvedBookId;
  }
  book.path = normalizedNewPath;
  if (!title.empty()) {
    book.title = title;
  }
  if (!author.empty()) {
    book.author = author;
  }
  if (!coverBmpPath.empty()) {
    book.coverBmpPath = coverBmpPath;
  }
  saveToFile();
  return true;
}

void RecentBooksStore::updatePath(const std::string& oldPath, const std::string& newPath,
                                  const std::string& oldCachePath, const std::string& newCachePath) {
  const int existingIndex = findBookIndex(oldPath, "");
  if (existingIndex < 0) {
    return;
  }

  std::string coverBmpPath;
  const RecentBook& existing = recentBooks[existingIndex];
  if (!oldCachePath.empty() && !existing.coverBmpPath.empty() && existing.coverBmpPath.rfind(oldCachePath, 0) == 0) {
    coverBmpPath = newCachePath + existing.coverBmpPath.substr(oldCachePath.size());
  }
  // updateBookPath keeps the existing title/author/cover when the overrides are empty and persists.
  updateBookPath(oldPath, newPath, "", "", coverBmpPath, existing.bookId);
}

bool RecentBooksStore::removeBook(const std::string& key) {
  const int existingIndex = findBookIndex(key, key);
  if (existingIndex < 0) {
    return false;
  }

  recentBooks.erase(recentBooks.begin() + existingIndex);
  saveToFile();
  return true;
}

bool RecentBooksStore::isMissing(const RecentBook& book) {
  return book.path.empty() || !Storage.exists(book.path.c_str());
}

bool RecentBooksStore::pruneMissing() {
  const size_t before = recentBooks.size();
  recentBooks.erase(std::remove_if(recentBooks.begin(), recentBooks.end(), &RecentBooksStore::isMissing),
                    recentBooks.end());
  return recentBooks.size() != before;
}

bool RecentBooksStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveRecentBooks(*this, RECENT_BOOKS_FILE_JSON);
}

RecentBook RecentBooksStore::getDataFromBook(std::string path) const {
  std::string lastBookFileName = "";
  const size_t lastSlash = path.find_last_of('/');
  if (lastSlash != std::string::npos) {
    lastBookFileName = path.substr(lastSlash + 1);
  }

  LOG_DBG("RBS", "Loading recent book: %s", path.c_str());

  // If epub, try to load the metadata for title/author and cover.
  // Use buildIfMissing=false to avoid heavy epub loading on boot; getTitle()/getAuthor() may be
  // blank until the book is opened, and entries with missing title are omitted from recent list.
  if (FsHelpers::hasEpubExtension(lastBookFileName)) {
    Epub epub(path, "/.crosspoint");
    epub.load(false, true);
    return RecentBook{BookIdentity::resolveStableBookId(path), path, epub.getTitle(), epub.getAuthor(),
                      epub.getThumbBmpPath()};
  } else if (FsHelpers::hasXtcExtension(lastBookFileName)) {
    // Handle XTC file
    Xtc xtc(path, "/.crosspoint");
    if (xtc.load()) {
      return RecentBook{BookIdentity::resolveStableBookId(path), path, xtc.getTitle(), xtc.getAuthor(),
                        xtc.getThumbBmpPath()};
    }
  } else if (FsHelpers::hasTxtExtension(lastBookFileName) || FsHelpers::hasMarkdownExtension(lastBookFileName)) {
    return RecentBook{BookIdentity::resolveStableBookId(path), path, lastBookFileName, "", ""};
  }
  return RecentBook{BookIdentity::resolveStableBookId(path), path, "", "", ""};
}

bool RecentBooksStore::loadFromFile() {
  const std::string tempPath = std::string(RECENT_BOOKS_FILE_JSON) + ".tmp";
  if (!Storage.exists(RECENT_BOOKS_FILE_JSON) && Storage.exists(tempPath.c_str())) {
    if (Storage.rename(tempPath.c_str(), RECENT_BOOKS_FILE_JSON)) {
      LOG_DBG("RBS", "Recovered recent.json from interrupted temp file");
    }
  }

  // Try JSON first
  if (Storage.exists(RECENT_BOOKS_FILE_JSON)) {
    String json = Storage.readFile(RECENT_BOOKS_FILE_JSON);
    if (!json.isEmpty()) {
      return JsonSettingsIO::loadRecentBooks(*this, json.c_str());
    }
  }

  // Fall back to binary migration
  if (Storage.exists(RECENT_BOOKS_FILE_BIN)) {
    if (loadFromBinaryFile()) {
      saveToFile();
      Storage.rename(RECENT_BOOKS_FILE_BIN, RECENT_BOOKS_FILE_BAK);
      LOG_DBG("RBS", "Migrated recent.bin to recent.json");
      return true;
    }
  }

  return false;
}

bool RecentBooksStore::loadFromBinaryFile() {
  HalFile inputFile;
  if (!Storage.openFileForRead("RBS", RECENT_BOOKS_FILE_BIN, inputFile)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);
  if (version == 1 || version == 2) {
    // Old version: migrate lightly to avoid opening EPUB/XTC during boot.
    uint8_t count;
    serialization::readPod(inputFile, count);
    recentBooks.clear();
    recentBooks.reserve(count);
    for (uint8_t i = 0; i < count; i++) {
      std::string path;
      serialization::readString(inputFile, path);
      std::string title;
      std::string author;
      if (version == 2) {
        serialization::readString(inputFile, title);
        serialization::readString(inputFile, author);
      }

      const std::string normalizedPath = BookIdentity::normalizePath(path);
      if (normalizedPath.empty()) {
        continue;
      }

      if (title.empty()) {
        title = fallbackTitleFromPath(normalizedPath);
      }

      recentBooks.push_back({BookIdentity::resolveStableBookId(normalizedPath), normalizedPath, title, author, ""});
    }
  } else if (version == 3) {
    uint8_t count;
    serialization::readPod(inputFile, count);

    recentBooks.clear();
    recentBooks.reserve(count);
    uint8_t omitted = 0;

    for (uint8_t i = 0; i < count; i++) {
      std::string path, title, author, coverBmpPath;
      serialization::readString(inputFile, path);
      serialization::readString(inputFile, title);
      serialization::readString(inputFile, author);
      serialization::readString(inputFile, coverBmpPath);

      // Omit books with missing title (e.g. saved before metadata was available)
      if (title.empty()) {
        omitted++;
        continue;
      }

      recentBooks.push_back({BookIdentity::resolveStableBookId(path), path, title, author, coverBmpPath});
    }

    if (omitted > 0) {
      // Explicitly close() file before saveToFile() rewrites the same file
      inputFile.close();
      saveToFile();
      LOG_DBG("RBS", "Omitted %u recent book(s) with missing title", omitted);
      return true;
    }
  } else {
    LOG_ERR("RBS", "Deserialization failed: Unknown version %u", version);
    return false;
  }

  inputFile.close();
  normalizeBooks();
  LOG_DBG("RBS", "Recent books loaded from binary file (%d entries)", static_cast<int>(recentBooks.size()));
  return true;
}
