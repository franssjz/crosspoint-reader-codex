#pragma once
#include <string>
#include <vector>

struct RecentBook {
  std::string bookId;
  std::string path;
  std::string title;
  std::string author;
  std::string coverBmpPath;

  bool operator==(const RecentBook& other) const {
    return !bookId.empty() && !other.bookId.empty() ? bookId == other.bookId : path == other.path;
  }
};

class RecentBooksStore;
namespace JsonSettingsIO {
bool saveRecentBooks(const RecentBooksStore& store, const char* path);
bool loadRecentBooks(RecentBooksStore& store, const char* json);
}  // namespace JsonSettingsIO

class RecentBooksStore {
  // Static instance
  static RecentBooksStore instance;

  std::vector<RecentBook> recentBooks;

  friend bool JsonSettingsIO::saveRecentBooks(const RecentBooksStore&, const char*);
  friend bool JsonSettingsIO::loadRecentBooks(RecentBooksStore&, const char*);

 public:
  ~RecentBooksStore() = default;

  // Get singleton instance
  static RecentBooksStore& getInstance() { return instance; }

  // Add a book to the recent list (moves to front if already exists)
  void addBook(const std::string& path, const std::string& title, const std::string& author,
               const std::string& coverBmpPath, const std::string& bookId = "");

  void updateBook(const std::string& path, const std::string& title, const std::string& author,
                  const std::string& coverBmpPath, const std::string& bookId = "");
  bool updateBookPath(const std::string& oldKey, const std::string& newPath, const std::string& title = "",
                      const std::string& author = "", const std::string& coverBmpPath = "",
                      const std::string& bookId = "");

  bool removeBook(const std::string& key);

  // Upstream API: remove the entry whose path matches (used when a book is removed from recents or
  // finished/read). Returns true if an entry was found and removed. Persistence is best-effort.
  bool removeByPath(const std::string& path) { return removeBook(path); }

  // Upstream API: repoint an entry's path (and coverBmpPath, if it lived under the old cache dir)
  // after the backing file and cache dir were moved on disk. No-op if no entry matches oldPath.
  // Persists on success. Keeps the entry's list position (does not reorder).
  void updatePath(const std::string& oldPath, const std::string& newPath, const std::string& oldCachePath,
                  const std::string& newCachePath);

  // True if the book's backing file is no longer present on the SD card.
  static bool isMissing(const RecentBook& book);

  // Remove entries whose backing file is no longer on the SD card.
  // Returns true if any entry was removed. Does not persist; caller decides.
  bool pruneMissing();

  // Get the list of recent books (most recent first)
  const std::vector<RecentBook>& getBooks() const { return recentBooks; }

  // Get the count of recent books
  int getCount() const { return static_cast<int>(recentBooks.size()); }

  bool saveToFile() const;

  bool loadFromFile();
  RecentBook getDataFromBook(std::string path) const;

 private:
  int findBookIndex(const std::string& path, const std::string& bookId) const;
  void normalizeBook(RecentBook& book);
  void normalizeBooks();
  bool loadFromBinaryFile();
};

// Helper macro to access recent books store
#define RECENT_BOOKS RecentBooksStore::getInstance()
