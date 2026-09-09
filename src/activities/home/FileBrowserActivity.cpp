#include "FileBrowserActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>

#include "../util/ConfirmationActivity.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"

namespace {
constexpr unsigned long GO_HOME_MS = 1000;
constexpr size_t NAME_BUFFER_SIZE = 500;
}  // namespace

// Called with RenderLock held: the index and its visible window are one
// snapshot. The render task never walks the SD directory or reallocates it.
void FileBrowserActivity::loadFiles() {
  fileCount = 0;
  windowFirst = SIZE_MAX;
  windowCount = 0;
  listingError = ListingError::None;
  struct Filter {
    Mode mode;
    bool hidden;
  } filter{mode, SETTINGS.showHiddenFiles != 0};
  const auto accept = [](const char* name, bool isDir, const void* context) {
    const auto& f = *static_cast<const Filter*>(context);
    if (!name[0] || strcmp(name, ".") == 0 || strcmp(name, "..") == 0 || (!f.hidden && name[0] == '.') ||
        strcmp(name, "System Volume Information") == 0)
      return false;
    if (isDir) return true;
    const std::string_view filename{name};
    return f.mode == Mode::PickFirmware
               ? FsHelpers::checkFileExtension(filename, ".bin")
               : (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename) ||
                  FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename) ||
                  FsHelpers::hasBmpExtension(filename) || FsHelpers::hasPngExtension(filename));
  };
  if (!indexEntry || !fileNameBuffer) {
    listingError = ListingError::Memory;
    return;
  }
  if (!fileIndex.open(basepath.c_str(), accept, &filter,
                      1 + (mode == Mode::PickFirmware ? 2 : 0) + (filter.hidden ? 1 : 0))) {
    listingError = fileIndex.directoryReadFailed() ? ListingError::Directory : ListingError::Index;
    return;
  }
  fileCount = fileIndex.totalCount();
  selectorIndex = fileCount ? std::min(selectorIndex, fileCount - 1) : 0;
  loadVisibleWindow();
}

bool FileBrowserActivity::loadVisibleWindow() {
  if (fileCount == 0) return true;
  const int pathReserved = renderer.getLineHeight(SMALL_FONT_ID) + UITheme::getInstance().getMetrics().verticalSpacing;
  const size_t pageItems =
      std::max(1, UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false, pathReserved));
  const size_t first = (selectorIndex / pageItems) * pageItems;
  if (first == windowFirst && windowCount > 0 && windowCapacity == pageItems) return true;
  if (windowCapacity != pageItems) {
    auto newWindow = makeUniqueNoThrow<VisibleEntry[]>(pageItems);
    if (!newWindow) {
      listingError = ListingError::Memory;
      fileCount = windowCount = 0;
      return false;
    }
    visibleEntries = std::move(newWindow);
    windowCapacity = pageItems;
  }
  windowCount = 0;
  windowFirst = first;
  const size_t count = std::min(pageItems, fileCount - first);
  std::string prefix = basepath;
  if (prefix.empty() || prefix.back() != '/') prefix += '/';
  for (size_t i = 0; i < count; ++i) {
    if (!fileIndex.entryAt(first + i, *indexEntry)) {
      listingError = ListingError::Index;
      fileCount = windowCount = 0;
      return false;
    }
    auto& visible = visibleEntries[i];
    const size_t len = strlen(indexEntry->name);
    memcpy(visible.name, indexEntry->name, len + 1);
    visible.completed = false;
    if (indexEntry->isDir) {
      visible.name[len] = '/';
      visible.name[len + 1] = '\0';
    } else if (mode == Mode::Books) {
      const auto* book = READING_STATS.findBook(prefix + indexEntry->name);
      visible.completed = book && book->completed;
    }
    ++windowCount;
  }
  return true;
}

void FileBrowserActivity::setSelection(size_t row) {
  selectorIndex = fileCount ? std::min(row, fileCount - 1) : 0;
  loadVisibleWindow();
  requestUpdate();
}

const char* FileBrowserActivity::visibleName(size_t row) const {
  return row >= windowFirst && row - windowFirst < windowCount ? visibleEntries[row - windowFirst].name : "";
}

bool FileBrowserActivity::visibleCompleted(size_t row) const {
  return row >= windowFirst && row - windowFirst < windowCount && visibleEntries[row - windowFirst].completed;
}

void FileBrowserActivity::onEnter() {
  Activity::onEnter();
  RenderLock lock(*this);

  fileNameBuffer = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  indexEntry = makeUniqueNoThrow<FileIndex::Entry>();
  if (!fileNameBuffer || !indexEntry) {
    LOG_ERR("FileBrowser", "malloc failed for name buffer");
    listingError = ListingError::Memory;
    requestUpdate();
    return;
  }

  selectorIndex = 0;

  auto root = Storage.open(basepath.c_str());
  if (!root) {
    basepath = "/";
    loadFiles();
  } else if (!root.isDirectory()) {
    root.close();
    lockLongPressBack = mappedInput.isPressed(MappedInputManager::Button::Back);

    const std::string oldPath = basepath;
    basepath = FsHelpers::extractFolderPath(basepath);
    loadFiles();

    const auto pos = oldPath.find_last_of('/');
    const std::string fileName = oldPath.substr(pos + 1);
    setSelection(findEntry(fileName));
  } else {
    root.close();
    loadFiles();
  }

  requestUpdate();
}

void FileBrowserActivity::onExit() {
  Activity::onExit();
  // ActivityManager calls onExit while already holding RenderLock.
  fileIndex.close();
  visibleEntries.reset();
  indexEntry.reset();
  fileCount = windowCount = windowCapacity = 0;
  windowFirst = SIZE_MAX;
  fileNameBuffer.reset();
}

// To avoid traversing directories twice (once for cache clearing, once for deletion),
// we do both in one pass here, instead of using Storage.removeDir
bool FileBrowserActivity::removeDirFile(const std::string& fullPath) {
  auto file = Storage.open(fullPath.c_str());
  if (!file) {
    LOG_ERR("FileBrowser", "Failed to open for metadata clearing: %s", fullPath.c_str());
    return false;
  }

  if (!file.isDirectory()) {
    file.close();
    clearBookCache(fullPath);
    return Storage.remove(fullPath.c_str());
  }
  file.close();

  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "fileNameBuffer not allocated");
    return false;
  }

  // Stack of (dirPath, postOrder): postOrder=true means rmdir this path after children are processed.
  std::vector<std::pair<std::string, bool>> stack;
  stack.reserve(16);
  stack.push_back({fullPath, false});

  while (!stack.empty()) {
    auto [currentPath, postOrder] = std::move(stack.back());
    stack.pop_back();

    if (postOrder) {
      if (!Storage.rmdir(currentPath.c_str())) {
        LOG_ERR("FileBrowser", "Failed to rmdir: %s", currentPath.c_str());
        return false;
      }
      continue;
    }

    auto dir = Storage.open(currentPath.c_str());
    if (!dir) {
      LOG_ERR("FileBrowser", "Failed to open dir: %s", currentPath.c_str());
      return false;
    }
    if (!dir.isDirectory()) {
      LOG_ERR("FileBrowser", "Not a directory: %s", currentPath.c_str());
      return false;
    }

    // Push this dir for post-order rmdir (after all children are processed).
    stack.push_back({currentPath, true});

    dir.rewindDirectory();
    for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      if (!entry.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE)) return false;
      if (strcmp(fileNameBuffer.get(), ".") == 0 || strcmp(fileNameBuffer.get(), "..") == 0) {
        continue;
      }
      std::string entryPath = currentPath;
      if (entryPath.back() != '/') {
        entryPath += "/";
      }
      entryPath += fileNameBuffer.get();

      const bool isDir = entry.isDirectory();
      entry.close();

      if (isDir) {
        stack.push_back({std::move(entryPath), false});
      } else {
        clearBookCache(entryPath);
        if (!Storage.remove(entryPath.c_str())) {
          LOG_ERR("FileBrowser", "Failed to remove file: %s", entryPath.c_str());
          return false;
        }
      }
    }
    if (dir.iterationFailed() || dir.allocationFailed()) return false;
    dir.close();
  }

  return true;
}

void FileBrowserActivity::loop() {
  RenderLock lock(*this);
  // Long press BACK (1s+) goes to root folder
  // but Long press BACK (1s+) from ReaderActivity sends us here with the MappedInput already set.
  // So ignore it the first time.
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= GO_HOME_MS &&
      basepath != "/" && !lockLongPressBack) {
    basepath = "/";
    selectorIndex = 0;
    loadFiles();
    requestUpdate();
    return;
  }

  if (lockLongPressBack && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    lockLongPressBack = false;
    return;
  }

  const int pathReserved = renderer.getLineHeight(SMALL_FONT_ID) + UITheme::getInstance().getMetrics().verticalSpacing;
  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false, pathReserved);

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (fileCount == 0) return;

    const std::string entry = visibleName(selectorIndex);
    if (entry.empty()) return;
    bool isDirectory = (entry.back() == '/');

    if (mode == Mode::Books && mappedInput.getHeldTime() >= GO_HOME_MS) {
      // --- LONG PRESS ACTION: DELETE FILE OR DIRECTORY ---
      std::string cleanBasePath = basepath;
      if (cleanBasePath.back() != '/') cleanBasePath += "/";
      const std::string fullPath = cleanBasePath + entry;

      auto handler = [this, fullPath](const ActivityResult& res) {
        RenderLock lock(*this);
        if (!res.isCancelled) {
          LOG_DBG("FileBrowser", "Attempting to delete: %s", fullPath.c_str());
          if (removeDirFile(fullPath)) {
            LOG_DBG("FileBrowser", "Deleted successfully");
            loadFiles();
            if (fileCount == 0) {
              selectorIndex = 0;
            } else if (selectorIndex >= fileCount) {
              // Move selection to the new "last" item
              selectorIndex = fileCount - 1;
            }

            requestUpdate(true);
          } else {
            LOG_ERR("FileBrowser", "Failed to delete file: %s", fullPath.c_str());
          }
        } else {
          LOG_DBG("FileBrowser", "Delete cancelled by user");
        }
      };

      std::string heading = tr(STR_DELETE) + std::string("? ");

      lock.unlock();
      startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, entry), handler);
      return;
    }

    if (basepath.back() != '/') basepath += "/";

    if (isDirectory) {
      basepath += entry.substr(0, entry.length() - 1);
      selectorIndex = 0;
      loadFiles();
      requestUpdate();
    } else {
      const std::string selectedPath = basepath + entry;
      if (mode != Mode::Books) {
        setResult(ActivityResult{FilePathResult{selectedPath}});
        lock.unlock();
        finish();
      } else {
        lock.unlock();
        onSelectBook(selectedPath);
      }
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Short press: go up one directory, or go home if at root
    if (mappedInput.getHeldTime() < GO_HOME_MS) {
      if (basepath != "/") {
        const std::string oldPath = basepath;

        basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
        if (basepath.empty()) basepath = "/";
        loadFiles();

        const auto pos = oldPath.find_last_of('/');
        const std::string dirName = oldPath.substr(pos + 1) + "/";
        setSelection(findEntry(dirName));

        requestUpdate();
      } else {
        lock.unlock();
        onGoHome();
        return;
      }
    }
  }

  int listSize = static_cast<int>(fileCount);
  buttonNavigator.onNextRelease(
      [this, listSize] { setSelection(ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize)); });

  buttonNavigator.onPreviousRelease(
      [this, listSize] { setSelection(ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize)); });

  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    setSelection(ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems));
  });

  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    setSelection(ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems));
  });
}

std::string getFileName(std::string filename) {
  if (filename.empty()) return {};
  if (filename.back() == '/') {
    filename.pop_back();
    if (!UITheme::getInstance().getTheme().showsFileIcons()) {
      return "[" + filename + "]";
    }
    return filename;
  }
  const auto pos = filename.rfind('.');
  return filename.substr(0, pos);
}

std::string getFileExtension(std::string filename) {
  if (filename.empty()) return {};
  if (filename.back() == '/') {
    return "";
  }
  const auto pos = filename.rfind('.');
  return pos == std::string::npos ? std::string() : filename.substr(pos);
}

void FileBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  std::string folderName = (basepath == "/") ? tr(STR_SD_CARD) : basepath.substr(basepath.rfind('/') + 1);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, folderName.c_str());

  const int pathLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int pathReserved = pathLineHeight + metrics.verticalSpacing;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - pathReserved;
  if (fileCount == 0) {
    const StrId emptyMessage = listingError == ListingError::Directory ? StrId::STR_DIRECTORY_READ_FAILED
                               : listingError == ListingError::Index   ? StrId::STR_FILE_INDEX_FAILED
                               : listingError == ListingError::Memory  ? StrId::STR_MEMORY_ERROR
                                                                       : StrId::STR_NO_FILES_FOUND;
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, I18N.get(emptyMessage));
  } else {
    struct PrewarmCtx {
      const VisibleEntry* entries;
    } ctx{visibleEntries.get()};
    const auto getter = [](const void* raw, uint32_t i) -> const char* {
      return static_cast<const PrewarmCtx*>(raw)->entries[i].name;
    };
    renderer.prewarmFallbackText(UI_10_FONT_ID, getter, &ctx, windowCount);
    renderer.prewarmFallbackText(SMALL_FONT_ID, getter, &ctx, windowCount);
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, fileCount, selectorIndex,
        [this](int index) { return getFileName(visibleName(index)); }, nullptr,
        [this](int index) { return UITheme::getFileIcon(visibleName(index)); },
        [this](int index) { return SETTINGS.hideFileExtension ? std::string() : getFileExtension(visibleName(index)); },
        false, [this](int index) { return index >= 0 && visibleCompleted(index); });
  }

  // Show the selected entry in full when it fits. When it does not, preserve
  // the suffix here because the list row already shows the beginning; this is
  // especially useful for books whose distinguishing text is near the end.
  {
    const int pathY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - pathLineHeight;
    const int separatorY = pathY - metrics.verticalSpacing / 2;
    renderer.drawLine(0, separatorY, pageWidth - 1, separatorY, 3, true);
    const int infoMaxWidth = pageWidth - metrics.contentSidePadding * 2;
    const bool hasSelection = fileCount != 0 && selectorIndex >= 0 && selectorIndex < static_cast<int>(fileCount);
    const char* infoStr = hasSelection ? visibleName(selectorIndex) : basepath.c_str();
    renderer.prewarmFallbackText(
        SMALL_FONT_ID, [](const void* raw, uint32_t) { return static_cast<const char* const*>(raw)[0]; }, &infoStr, 1);
    const char* infoDisplay = infoStr;
    char leftTruncBuf[256];
    if (renderer.getTextWidth(SMALL_FONT_ID, infoStr) > infoMaxWidth) {
      const char ellipsis[] = "\xe2\x80\xa6";  // UTF-8 ellipsis (…)
      const int ellipsisWidth = renderer.getTextWidth(SMALL_FONT_ID, ellipsis);
      const int available = infoMaxWidth - ellipsisWidth;
      // Walk forward from the start until the suffix fits, skipping UTF-8 continuation bytes
      const char* p = infoStr;
      while (*p) {
        if (renderer.getTextWidth(SMALL_FONT_ID, p) <= available) break;
        ++p;
        while (*p && (static_cast<unsigned char>(*p) & 0xC0) == 0x80) ++p;
      }
      snprintf(leftTruncBuf, sizeof(leftTruncBuf), "%s%s", ellipsis, p);
      infoDisplay = leftTruncBuf;
    }
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, pathY, infoDisplay);
  }

  // Help text
  const auto labels =
      mappedInput.mapLabels(basepath == "/" ? tr(STR_HOME) : tr(STR_BACK), fileCount == 0 ? "" : tr(STR_OPEN),
                            fileCount == 0 ? "" : tr(STR_DIR_UP), fileCount == 0 ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

size_t FileBrowserActivity::findEntry(const std::string& name) {
  const std::string bareName = !name.empty() && name.back() == '/' ? name.substr(0, name.size() - 1) : name;
  const size_t row = fileIndex.findRowByName(bareName.c_str());
  return row == SIZE_MAX ? 0 : row;
}
