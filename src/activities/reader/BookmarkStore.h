#pragma once

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "util/BookIdentity.h"

class BookmarkStore {
 public:
  struct Bookmark {
    uint16_t spineIndex = 0;
    uint16_t pageNumber = 0;
    uint16_t endPageNumber = 0;
    uint16_t startWordIndex = 0;
    uint16_t endWordIndex = 0;
    std::string snippet;
    bool isTextHighlight = false;
    bool hasVisibleTextOffset = false;
    uint32_t visibleTextOffset = 0;
  };

  void load(const std::string& cachePath, const std::string& bookId = "") {
    storagePath.clear();
    legacyPath.clear();
    if (!bookId.empty()) {
      BookIdentity::ensureStableDataDir(bookId);
      storagePath = BookIdentity::getStableDataFilePath(bookId, "bookmarks.bin");
      if (!cachePath.empty()) {
        legacyPath = cachePath + "/bookmarks.bin";
      }
    } else {
      storagePath = cachePath.empty() ? "" : (cachePath + "/bookmarks.bin");
    }

    bookmarks.clear();
    dirty = false;

    if (!storagePath.empty() && !Storage.exists(storagePath.c_str())) {
      const std::string backupPath = storagePath + ".bak";
      if (Storage.exists(backupPath.c_str())) {
        Storage.rename(backupPath.c_str(), storagePath.c_str());
      }
    }

    HalFile file;
    bool loadedLegacyPath = false;
    if (!Storage.openFileForRead("BKM", getFilePath(), file)) {
      if (storagePath == legacyPath || legacyPath.empty() || !Storage.openFileForRead("BKM", legacyPath, file)) {
        return;
      }
      loadedLegacyPath = true;
    }

    if (getFilePath().empty()) {
      return;
    }

    uint8_t version = 0;
    if (file.read(reinterpret_cast<uint8_t*>(&version), sizeof(version)) != sizeof(version) || version < 1 ||
        version > FILE_VERSION) {
      file.close();
      return;
    }

    uint32_t count = 0;
    if (version >= 3) {
      if (file.read(reinterpret_cast<uint8_t*>(&count), sizeof(count)) != sizeof(count)) {
        file.close();
        return;
      }
    } else {
      uint16_t legacyCount = 0;
      if (file.read(reinterpret_cast<uint8_t*>(&legacyCount), sizeof(legacyCount)) != sizeof(legacyCount)) {
        file.close();
        return;
      }
      count = legacyCount;
    }
    if (count > MAX_ITEMS) {
      file.close();
      return;
    }

    bookmarks.reserve(static_cast<size_t>(count));
    for (uint32_t index = 0; index < count; ++index) {
      Bookmark bookmark;
      if (file.read(reinterpret_cast<uint8_t*>(&bookmark.spineIndex), sizeof(bookmark.spineIndex)) !=
              sizeof(bookmark.spineIndex) ||
          file.read(reinterpret_cast<uint8_t*>(&bookmark.pageNumber), sizeof(bookmark.pageNumber)) !=
              sizeof(bookmark.pageNumber)) {
        bookmarks.clear();
        file.close();
        return;
      }

      if (version >= 4) {
        uint8_t kind = 0;
        uint16_t snippetLen = 0;
        if (file.read(&kind, sizeof(kind)) != sizeof(kind) ||
            file.read(reinterpret_cast<uint8_t*>(&bookmark.endPageNumber), sizeof(bookmark.endPageNumber)) !=
                sizeof(bookmark.endPageNumber) ||
            file.read(reinterpret_cast<uint8_t*>(&bookmark.startWordIndex), sizeof(bookmark.startWordIndex)) !=
                sizeof(bookmark.startWordIndex) ||
            file.read(reinterpret_cast<uint8_t*>(&bookmark.endWordIndex), sizeof(bookmark.endWordIndex)) !=
                sizeof(bookmark.endWordIndex) ||
            file.read(reinterpret_cast<uint8_t*>(&snippetLen), sizeof(snippetLen)) != sizeof(snippetLen) ||
            snippetLen > MAX_HIGHLIGHT_TEXT_LEN) {
          bookmarks.clear();
          file.close();
          return;
        }
        bookmark.isTextHighlight = kind == TEXT_HIGHLIGHT_KIND;
        if (snippetLen > 0) {
          bookmark.snippet.resize(snippetLen);
          if (file.read(reinterpret_cast<uint8_t*>(bookmark.snippet.data()), snippetLen) != snippetLen) {
            bookmarks.clear();
            file.close();
            return;
          }
        }
        if (version >= 5) {
          uint8_t flags = 0;
          if (file.read(&flags, sizeof(flags)) != sizeof(flags) ||
              file.read(reinterpret_cast<uint8_t*>(&bookmark.visibleTextOffset), sizeof(bookmark.visibleTextOffset)) !=
                  sizeof(bookmark.visibleTextOffset)) {
            bookmarks.clear();
            file.close();
            return;
          }
          bookmark.hasVisibleTextOffset = (flags & HAS_VISIBLE_TEXT_OFFSET_FLAG) != 0;
        }
      } else if (version >= 2) {
        uint8_t snippetLen = 0;
        if (file.read(&snippetLen, 1) == 1 && snippetLen > 0) {
          char buffer[MAX_SNIPPET_LEN + 1];
          const uint8_t toRead = std::min(snippetLen, static_cast<uint8_t>(MAX_SNIPPET_LEN));
          if (file.read(reinterpret_cast<uint8_t*>(buffer), toRead) == toRead) {
            buffer[toRead] = '\0';
            bookmark.snippet = buffer;
          }
          if (snippetLen > toRead) {
            file.seekCur(snippetLen - toRead);
          }
        }
      }
      if (version < 4) {
        bookmark.endPageNumber = bookmark.pageNumber;
      }

      bookmarks.push_back(bookmark);
    }

    file.close();

    if (loadedLegacyPath && !storagePath.empty()) {
      dirty = true;
      save();
    }
  }

  void save() {
    if (!dirty || storagePath.empty()) {
      return;
    }

    const std::string tempPath = storagePath + ".tmp";
    const std::string backupPath = storagePath + ".bak";
    if (Storage.exists(tempPath.c_str())) {
      Storage.remove(tempPath.c_str());
    }

    HalFile file;
    if (!Storage.openFileForWrite("BKM", tempPath, file)) {
      LOG_ERR("BKM", "Failed to save bookmarks");
      return;
    }

    auto writePodChecked = [&file](const auto& value) {
      return file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(value)) == sizeof(value);
    };

    const uint32_t count = static_cast<uint32_t>(bookmarks.size());
    bool ok = writePodChecked(FILE_VERSION) && writePodChecked(count);

    for (const auto& bookmark : bookmarks) {
      ok = ok && writePodChecked(bookmark.spineIndex) && writePodChecked(bookmark.pageNumber);
      const uint8_t kind = bookmark.isTextHighlight ? TEXT_HIGHLIGHT_KIND : PAGE_MARK_KIND;
      const uint16_t snippetLen =
          static_cast<uint16_t>(std::min(bookmark.snippet.size(), static_cast<size_t>(MAX_HIGHLIGHT_TEXT_LEN)));
      ok = ok && writePodChecked(kind) && writePodChecked(bookmark.endPageNumber) &&
           writePodChecked(bookmark.startWordIndex) && writePodChecked(bookmark.endWordIndex) &&
           writePodChecked(snippetLen);
      if (snippetLen > 0) {
        ok = ok && file.write(reinterpret_cast<const uint8_t*>(bookmark.snippet.c_str()), snippetLen) == snippetLen;
      }
      const uint8_t flags = bookmark.hasVisibleTextOffset ? HAS_VISIBLE_TEXT_OFFSET_FLAG : 0;
      ok = ok && writePodChecked(flags) && writePodChecked(bookmark.visibleTextOffset);
    }

    ok = ok && file.close();
    if (!ok) {
      LOG_ERR("BKM", "Failed while writing bookmarks");
      Storage.remove(tempPath.c_str());
      return;
    }

    const bool hadOriginal = Storage.exists(storagePath.c_str());
    if (hadOriginal) {
      if (Storage.exists(backupPath.c_str())) {
        Storage.remove(backupPath.c_str());
      }
      if (!Storage.rename(storagePath.c_str(), backupPath.c_str())) {
        LOG_ERR("BKM", "Failed to back up highlights");
        Storage.remove(tempPath.c_str());
        return;
      }
    }
    if (!Storage.rename(tempPath.c_str(), storagePath.c_str())) {
      LOG_ERR("BKM", "Failed to replace highlights");
      if (hadOriginal) {
        Storage.rename(backupPath.c_str(), storagePath.c_str());
      }
      Storage.remove(tempPath.c_str());
      return;
    }
    if (hadOriginal && Storage.exists(backupPath.c_str())) {
      Storage.remove(backupPath.c_str());
    }

    dirty = false;
  }

  bool toggle(const uint16_t spineIndex, const uint16_t pageNumber, const std::string& snippet = "",
              const std::optional<uint32_t> visibleTextOffset = std::nullopt) {
    auto it = find(spineIndex, pageNumber, visibleTextOffset);
    if (it != bookmarks.end()) {
      bookmarks.erase(it);
      dirty = true;
      return false;
    }

    Bookmark bookmark;
    bookmark.spineIndex = spineIndex;
    bookmark.pageNumber = pageNumber;
    bookmark.endPageNumber = pageNumber;
    bookmark.snippet = snippet.substr(0, MAX_SNIPPET_LEN);
    bookmark.hasVisibleTextOffset = visibleTextOffset.has_value();
    bookmark.visibleTextOffset = visibleTextOffset.value_or(0);
    bookmarks.push_back(std::move(bookmark));
    dirty = true;
    return true;
  }

  bool addTextHighlight(const uint16_t spineIndex, const uint16_t pageNumber, const uint16_t endPageNumber,
                        const uint16_t startWordIndex, const uint16_t endWordIndex, const std::string& text,
                        const std::optional<uint32_t> visibleTextOffset = std::nullopt) {
    if (text.empty() || bookmarks.size() >= MAX_ITEMS) {
      return false;
    }
    const auto duplicate = std::find_if(bookmarks.begin(), bookmarks.end(), [&](const Bookmark& item) {
      if (!item.isTextHighlight || item.spineIndex != spineIndex || item.snippet != text) return false;
      if (visibleTextOffset && item.hasVisibleTextOffset) {
        return item.visibleTextOffset == *visibleTextOffset;
      }
      return item.pageNumber == pageNumber && item.endPageNumber == endPageNumber &&
             item.startWordIndex == startWordIndex && item.endWordIndex == endWordIndex;
    });
    if (duplicate != bookmarks.end()) {
      return true;
    }
    Bookmark bookmark;
    bookmark.spineIndex = spineIndex;
    bookmark.pageNumber = pageNumber;
    bookmark.endPageNumber = endPageNumber;
    bookmark.startWordIndex = startWordIndex;
    bookmark.endWordIndex = endWordIndex;
    bookmark.snippet = text.substr(0, MAX_HIGHLIGHT_TEXT_LEN);
    bookmark.isTextHighlight = true;
    bookmark.hasVisibleTextOffset = visibleTextOffset.has_value();
    bookmark.visibleTextOffset = visibleTextOffset.value_or(0);
    bookmarks.push_back(std::move(bookmark));
    dirty = true;
    return true;
  }

  bool remove(const uint16_t spineIndex, const uint16_t pageNumber) {
    auto it = findPageMark(spineIndex, pageNumber);
    if (it == bookmarks.end()) {
      return false;
    }

    bookmarks.erase(it);
    dirty = true;
    return true;
  }

  bool removeItem(const Bookmark& item) {
    const auto it = std::find_if(bookmarks.begin(), bookmarks.end(), [&](const Bookmark& current) {
      return current.isTextHighlight == item.isTextHighlight && current.spineIndex == item.spineIndex &&
             current.pageNumber == item.pageNumber && current.endPageNumber == item.endPageNumber &&
             current.startWordIndex == item.startWordIndex && current.endWordIndex == item.endWordIndex &&
             current.snippet == item.snippet && current.hasVisibleTextOffset == item.hasVisibleTextOffset &&
             (!current.hasVisibleTextOffset || current.visibleTextOffset == item.visibleTextOffset);
    });
    if (it == bookmarks.end()) {
      return false;
    }
    bookmarks.erase(it);
    dirty = true;
    return true;
  }

  void clear() {
    if (bookmarks.empty()) {
      return;
    }
    bookmarks.clear();
    dirty = true;
  }

  [[nodiscard]] bool has(const uint16_t spineIndex, const uint16_t pageNumber,
                         const std::optional<uint32_t> visibleTextOffset = std::nullopt) const {
    return std::any_of(bookmarks.begin(), bookmarks.end(), [&](const Bookmark& bookmark) {
      if (bookmark.isTextHighlight || bookmark.spineIndex != spineIndex) return false;
      if (visibleTextOffset && bookmark.hasVisibleTextOffset) {
        return bookmark.visibleTextOffset == *visibleTextOffset;
      }
      return bookmark.pageNumber == pageNumber;
    });
  }

  [[nodiscard]] const std::vector<Bookmark>& getAll() const { return bookmarks; }
  [[nodiscard]] bool isEmpty() const { return bookmarks.empty(); }
  void markDirty() { dirty = true; }

 private:
  static constexpr uint8_t FILE_VERSION = 5;
  static constexpr uint8_t PAGE_MARK_KIND = 0;
  static constexpr uint8_t TEXT_HIGHLIGHT_KIND = 1;
  static constexpr uint8_t HAS_VISIBLE_TEXT_OFFSET_FLAG = 1;
  static constexpr uint8_t MAX_SNIPPET_LEN = 80;
  static constexpr uint16_t MAX_HIGHLIGHT_TEXT_LEN = 512;
  static constexpr size_t MAX_ITEMS = 256;

  std::vector<Bookmark> bookmarks;
  std::string storagePath;
  std::string legacyPath;
  bool dirty = false;

  [[nodiscard]] std::string getFilePath() const { return storagePath; }

  std::vector<Bookmark>::iterator findPageMark(const uint16_t spineIndex, const uint16_t pageNumber,
                                               const std::optional<uint32_t> visibleTextOffset = std::nullopt) {
    return std::find_if(bookmarks.begin(), bookmarks.end(), [&](const Bookmark& bookmark) {
      if (bookmark.isTextHighlight || bookmark.spineIndex != spineIndex) return false;
      if (visibleTextOffset && bookmark.hasVisibleTextOffset) {
        return bookmark.visibleTextOffset == *visibleTextOffset;
      }
      return bookmark.pageNumber == pageNumber;
    });
  }

  std::vector<Bookmark>::iterator find(const uint16_t spineIndex, const uint16_t pageNumber,
                                       const std::optional<uint32_t> visibleTextOffset = std::nullopt) {
    return findPageMark(spineIndex, pageNumber, visibleTextOffset);
  }
};
