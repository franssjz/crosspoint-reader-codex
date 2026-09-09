#pragma once

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "util/AtomicFileReplace.h"
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
    // In-memory only. The persisted BookmarkStore format remains v5.
    uint32_t textFileOffset = 0;
    uint16_t storedTextLength = 0;
    uint32_t memoryId = 0;
  };

  void load(const std::string& cachePath, const std::string& bookId = "", const bool deferHighlightText = false) {
    deferredText = deferHighlightText;
    readFailed = false;
    nextMemoryId = 1;
    textSourcePath.clear();
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
        if (!AtomicFileReplace::recover(Storage, storagePath.c_str(), backupPath.c_str())) {
          readFailed = true;
          return;
        }
      }
    }

    FsFile file;
    bool loadedLegacyPath = false;
    if (!Storage.openFileForRead("BKM", getFilePath(), file)) {
      if (!storagePath.empty() && Storage.exists(storagePath.c_str())) {
        readFailed = true;
        return;
      }
      if (!legacyPath.empty() && !Storage.exists(legacyPath.c_str())) {
        const std::string legacyBackup = legacyPath + ".bak";
        if (Storage.exists(legacyBackup.c_str()) &&
            !AtomicFileReplace::recover(Storage, legacyPath.c_str(), legacyBackup.c_str())) {
          readFailed = true;
          return;
        }
      }
      if (storagePath == legacyPath || legacyPath.empty() || !Storage.openFileForRead("BKM", legacyPath, file)) {
        readFailed = (!storagePath.empty() && Storage.exists(storagePath.c_str())) ||
                     (!legacyPath.empty() && Storage.exists(legacyPath.c_str()));
        return;
      }
      loadedLegacyPath = true;
    }

    readFailed = true;  // A malformed/unknown file must never be overwritten by an empty store.
    textSourcePath = loadedLegacyPath ? legacyPath : storagePath;
    if (getFilePath().empty()) {
      file.close();
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
      bookmark.memoryId = nextMemoryId++;
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
        bookmark.storedTextLength = snippetLen;
        bookmark.textFileOffset = file.position();
        if (snippetLen > 0) {
          if (deferredText && bookmark.isTextHighlight) {
            // Read through a reused owned buffer to validate truncation now,
            // without retaining every highlight string throughout reading.
            if (!ensureTextScratch() || file.read(textScratch.get(), snippetLen) != snippetLen) {
              bookmarks.clear();
              file.close();
              return;
            }
          } else {
            bookmark.snippet.resize(snippetLen);
            if (file.read(reinterpret_cast<uint8_t*>(bookmark.snippet.data()), snippetLen) != snippetLen) {
              bookmarks.clear();
              file.close();
              return;
            }
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
        if (file.read(&snippetLen, 1) != 1) {
          bookmarks.clear();
          file.close();
          return;
        }
        if (snippetLen > 0) {
          char buffer[MAX_SNIPPET_LEN + 1];
          const uint8_t toRead = std::min(snippetLen, static_cast<uint8_t>(MAX_SNIPPET_LEN));
          if (file.read(reinterpret_cast<uint8_t*>(buffer), toRead) != toRead) {
            bookmarks.clear();
            file.close();
            return;
          }
          buffer[toRead] = '\0';
          bookmark.snippet = buffer;
          // Older writers capped snippets at 80 bytes. Validate surplus bytes
          // from other v2/v3 producers too; a seek past EOF would hide damage.
          for (uint16_t skipped = toRead; skipped < snippetLen; ++skipped) {
            uint8_t ignored;
            if (file.read(&ignored, 1) != 1) {
              bookmarks.clear();
              file.close();
              return;
            }
          }
        }
      }
      if (version < 4) {
        bookmark.endPageNumber = bookmark.pageNumber;
      }

      bookmarks.push_back(bookmark);
    }

    file.close();
    readFailed = false;

    if (loadedLegacyPath && !storagePath.empty()) {
      dirty = true;
      save();
    }
  }

  bool save() {
    if (readFailed || storagePath.empty()) return false;
    if (!dirty) return true;

    const std::string tempPath = storagePath + ".tmp";
    const std::string backupPath = storagePath + ".bak";
    if (Storage.exists(tempPath.c_str())) {
      Storage.remove(tempPath.c_str());
    }

    FsFile file;
    if (!Storage.openFileForWrite("BKM", tempPath, file)) {
      LOG_ERR("BKM", "Failed to save bookmarks");
      return false;
    }

    auto writePodChecked = [&file](const auto& value) {
      return file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(value)) == sizeof(value);
    };

    const uint32_t count = static_cast<uint32_t>(bookmarks.size());
    bool ok = writePodChecked(FILE_VERSION) && writePodChecked(count);

    for (const auto& bookmark : bookmarks) {
      ok = ok && writePodChecked(bookmark.spineIndex) && writePodChecked(bookmark.pageNumber);
      const uint8_t kind = bookmark.isTextHighlight ? TEXT_HIGHLIGHT_KIND : PAGE_MARK_KIND;
      const uint16_t snippetLen = textLength(bookmark);
      const char* snippetText = getText(bookmark);
      if (snippetLen > 0 && !snippetText) {
        ok = false;
        break;
      }
      ok = ok && writePodChecked(kind) && writePodChecked(bookmark.endPageNumber) &&
           writePodChecked(bookmark.startWordIndex) && writePodChecked(bookmark.endWordIndex) &&
           writePodChecked(snippetLen);
      if (snippetLen > 0) {
        ok = ok && file.write(reinterpret_cast<const uint8_t*>(snippetText), snippetLen) == snippetLen;
      }
      const uint8_t flags = bookmark.hasVisibleTextOffset ? HAS_VISIBLE_TEXT_OFFSET_FLAG : 0;
      ok = ok && writePodChecked(flags) && writePodChecked(bookmark.visibleTextOffset);
    }

    const bool closed = file.close();
    ok = ok && closed;
    if (!ok) {
      LOG_ERR("BKM", "Failed while writing bookmarks");
      Storage.remove(tempPath.c_str());
      return false;
    }

    if (!AtomicFileReplace::promote(Storage, tempPath.c_str(), storagePath.c_str(), backupPath.c_str())) {
      LOG_ERR("BKM", "Failed to replace highlights; previous data retained");
      return false;
    }
    // Only change source offsets after successful promotion. A failed save
    // leaves every deferred reference pointing to the original committed file.
    uint32_t offset = sizeof(FILE_VERSION) + sizeof(uint32_t);
    for (auto& bookmark : bookmarks) {
      const uint16_t length = textLength(bookmark);
      bookmark.textFileOffset = offset + 13;
      bookmark.storedTextLength = length;
      offset += 18 + length;
      if (deferredText && bookmark.isTextHighlight) std::string().swap(bookmark.snippet);
    }
    textSourcePath = storagePath;

    dirty = false;
    return true;
  }

  bool toggle(const uint16_t spineIndex, const uint16_t pageNumber, const std::string& snippet = "",
              const std::optional<uint32_t> visibleTextOffset = std::nullopt) {
    if (readFailed) return false;
    auto it = find(spineIndex, pageNumber, visibleTextOffset);
    if (it != bookmarks.end()) {
      bookmarks.erase(it);
      dirty = true;
      return false;
    }

    if (bookmarks.size() >= MAX_ITEMS) return false;
    Bookmark bookmark;
    bookmark.memoryId = nextMemoryId++;
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
    if (readFailed || text.empty() || bookmarks.size() >= MAX_ITEMS) {
      return false;
    }
    const auto duplicate = std::find_if(bookmarks.begin(), bookmarks.end(), [&](const Bookmark& item) {
      if (!item.isTextHighlight || item.spineIndex != spineIndex) return false;
      const char* existingText = getText(item);
      if (!existingText || text != existingText) return false;
      if (visibleTextOffset && item.hasVisibleTextOffset) {
        return item.visibleTextOffset == *visibleTextOffset;
      }
      return item.pageNumber == pageNumber && item.endPageNumber == endPageNumber &&
             item.startWordIndex == startWordIndex && item.endWordIndex == endWordIndex;
    });
    if (duplicate != bookmarks.end()) {
      return true;
    }
    if (bookmarks.size() >= MAX_ITEMS) return false;
    Bookmark bookmark;
    bookmark.memoryId = nextMemoryId++;
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

  template <class Fragments>
  bool addTextHighlightFragments(const Fragments& fragments) {
    if (readFailed || fragments.empty() || fragments.size() > MAX_ITEMS - bookmarks.size()) return false;
    for (const auto& fragment : fragments) {
      if (fragment.text.empty() || fragment.text.size() > MAX_HIGHLIGHT_TEXT_LEN) return false;
    }
    const size_t oldSize = bookmarks.size();
    const bool wasDirty = dirty;
    for (const auto& fragment : fragments) {
      if (!addTextHighlight(fragment.spineIndex, fragment.pageNumber, fragment.pageNumber, fragment.startWordIndex,
                            fragment.endWordIndex, fragment.text, fragment.visibleTextOffset)) {
        bookmarks.resize(oldSize);
        dirty = wasDirty;
        return false;
      }
    }
    return true;
  }

  bool removeItem(const Bookmark& item) {
    const auto it = std::find_if(bookmarks.begin(), bookmarks.end(), [&](const Bookmark& current) {
      return current.isTextHighlight == item.isTextHighlight && current.spineIndex == item.spineIndex &&
             current.pageNumber == item.pageNumber && current.endPageNumber == item.endPageNumber &&
             current.startWordIndex == item.startWordIndex && current.endWordIndex == item.endWordIndex &&
             sameTextIdentity(current, item) && current.hasVisibleTextOffset == item.hasVisibleTextOffset &&
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
    if (readFailed || bookmarks.empty()) {
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

  // Valid until the next getText call on this store. Call once per candidate
  // highlight/visible menu row, not once per word or pixel.
  [[nodiscard]] const char* getText(const Bookmark& item) const {
    const Bookmark* stored = &item;
    if (item.memoryId != 0) {
      const auto found = std::find_if(bookmarks.begin(), bookmarks.end(),
                                      [&item](const Bookmark& current) { return current.memoryId == item.memoryId; });
      if (found != bookmarks.end()) stored = &*found;
    }
    if (!stored->snippet.empty() || stored->storedTextLength == 0) return stored->snippet.c_str();
    FsFile file;
    if (textSourcePath.empty() || !Storage.openFileForRead("BKM", textSourcePath, file)) return nullptr;
    const bool ok = ensureTextScratch() && stored->storedTextLength <= MAX_HIGHLIGHT_TEXT_LEN &&
                    file.seekSet(stored->textFileOffset) &&
                    file.read(textScratch.get(), stored->storedTextLength) == stored->storedTextLength;
    file.close();
    if (!ok) {
      LOG_ERR("BKM", "Could not read saved highlight text");
      return nullptr;
    }
    textScratch[stored->storedTextLength] = '\0';
    return textScratch.get();
  }

  [[nodiscard]] bool hasLoadError() const { return readFailed; }

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
  bool deferredText = false;
  bool readFailed = false;
  uint32_t nextMemoryId = 1;
  std::string textSourcePath;
  // Allocated only when deferred text is needed; eager app stores remain small
  // enough for existing stack callers. One bounded, fallible allocation/store.
  mutable std::unique_ptr<char[]> textScratch;

  bool ensureTextScratch() const {
    if (!textScratch) textScratch.reset(new (std::nothrow) char[MAX_HIGHLIGHT_TEXT_LEN + 1]);
    return textScratch != nullptr;
  }

  bool sameTextIdentity(const Bookmark& current, const Bookmark& item) const {
    // Eager snapshots can belong to a different load/session, so their memory
    // IDs are not identities. Compare payload before considering lazy IDs.
    if (!item.snippet.empty()) {
      const char* currentText = getText(current);
      return currentText && item.snippet == currentText;
    }
    if (item.storedTextLength > 0) return item.memoryId != 0 && current.memoryId == item.memoryId;
    return textLength(current) == 0;
  }

  static uint16_t textLength(const Bookmark& bookmark) {
    return bookmark.snippet.empty()
               ? bookmark.storedTextLength
               : static_cast<uint16_t>(std::min(bookmark.snippet.size(), static_cast<size_t>(MAX_HIGHLIGHT_TEXT_LEN)));
  }

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
