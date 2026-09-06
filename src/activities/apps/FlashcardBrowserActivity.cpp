#include "FlashcardBrowserActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "CrossPointSettings.h"
#include "FlashcardReviewActivity.h"
#include "FlashcardSessionSummaryActivity.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr unsigned long GO_ROOT_MS = 1000;

bool hasCsvExtension(const std::string_view filename) {
  if (filename.size() < 4) {
    return false;
  }
  const size_t dotPos = filename.rfind('.');
  if (dotPos == std::string_view::npos) {
    return false;
  }
  std::string extension(filename.substr(dotPos));
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return extension == ".csv";
}

void sortFileList(std::vector<std::string>& strs) {
  std::sort(begin(strs), end(strs), [](const std::string& str1, const std::string& str2) {
    const bool isDir1 = !str1.empty() && str1.back() == '/';
    const bool isDir2 = !str2.empty() && str2.back() == '/';
    if (isDir1 != isDir2) return isDir1;

    const char* s1 = str1.c_str();
    const char* s2 = str2.c_str();
    while (*s1 && *s2) {
      if (isdigit(*s1) && isdigit(*s2)) {
        const char* start1 = s1;
        const char* start2 = s2;
        while (*s1 == '0') s1++;
        while (*s2 == '0') s2++;
        const char* num1 = s1;
        const char* num2 = s2;
        while (isdigit(*s1)) s1++;
        while (isdigit(*s2)) s2++;
        const int len1 = s1 - num1;
        const int len2 = s2 - num2;
        if (len1 != len2) return len1 < len2;
        const int cmp = strncmp(num1, num2, len1);
        if (cmp != 0) return cmp < 0;
        const int leading1 = num1 - start1;
        const int leading2 = num2 - start2;
        if (leading1 != leading2) return leading1 < leading2;
      } else {
        const char c1 = static_cast<char>(tolower(*s1));
        const char c2 = static_cast<char>(tolower(*s2));
        if (c1 != c2) return c1 < c2;
        ++s1;
        ++s2;
      }
    }
    return *s1 < *s2;
  });
}

std::string getFileName(std::string filename) {
  if (!filename.empty() && filename.back() == '/') {
    filename.pop_back();
  }
  return filename;
}

bool isDirectoryEntry(const std::string& entry) { return !entry.empty() && entry.back() == '/'; }
}  // namespace

void FlashcardBrowserActivity::loadFiles() {
  files.clear();

  auto root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    rebuildRowItems();
    return;
  }

  root.rewindDirectory();
  char name[500];
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    if ((!SETTINGS.showHiddenFiles && name[0] == '.') || strcmp(name, "System Volume Information") == 0) {
      file.close();
      continue;
    }

    if (file.isDirectory()) {
      files.emplace_back(std::string(name) + "/");
    } else {
      std::string_view filename{name};
      if (hasCsvExtension(filename)) {
        files.emplace_back(filename);
      }
    }
    file.close();
  }
  root.close();
  sortFileList(files);
  rebuildRowItems();
}

void FlashcardBrowserActivity::rebuildRowItems() {
  rowNames.clear();
  rowItems.clear();
  rowNames.reserve(files.size());
  rowItems.reserve(files.size());
  for (size_t i = 0; i < files.size(); ++i) {
    rowNames.push_back(getFileName(files[i]));
    fui::ListItem item;
    item.label = rowNames.back().c_str();
    item.icon = listIconFor(UITheme::getFileIcon(files[i]));
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
}

bool FlashcardBrowserActivity::openDeckPath(const std::string& path) {
  startActivityForResult(
      std::make_unique<FlashcardReviewActivity>(renderer, mappedInput, path), [this](const ActivityResult& result) {
        if (result.isCancelled) {
          requestUpdate();
          return;
        }

        if (const auto* session = std::get_if<FlashcardSessionResult>(&result.data)) {
          startActivityForResult(std::make_unique<FlashcardSessionSummaryActivity>(renderer, mappedInput, *session),
                                 [this](const ActivityResult&) { requestUpdate(); });
          return;
        }
        requestUpdate();
      });
  return true;
}

void FlashcardBrowserActivity::onEnter() {
  UiListActivity::onEnter();

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
    nav.selected = static_cast<int>(findEntry(fileName));
  } else {
    root.close();
    loadFiles();
  }
}

void FlashcardBrowserActivity::onExit() {
  Activity::onExit();
  rowItems.clear();
  rowNames.clear();
  files.clear();
}

bool FlashcardBrowserActivity::handleCustomInput() {
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= GO_ROOT_MS &&
      basepath != "/" && !lockLongPressBack) {
    closeRouting();
    {
      RenderLock lock(*this);
      basepath = "/";
      loadFiles();
      nav.selected = 0;
      nav.top = 0;
    }
    requestUpdate();
    return true;
  }

  if (lockLongPressBack && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    lockLongPressBack = false;
    return true;
  }
  return false;
}

bool FlashcardBrowserActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!files.empty()) activateIndex(nav.selected);
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (mappedInput.getHeldTime() < GO_ROOT_MS) {
      if (basepath != "/") {
        const std::string oldPath = basepath;
        closeRouting();
        {
          RenderLock lock(*this);
          basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
          if (basepath.empty()) basepath = "/";
          loadFiles();

          const auto pos = oldPath.find_last_of('/');
          const std::string dirName = oldPath.substr(pos + 1) + "/";
          nav.selected = static_cast<int>(findEntry(dirName));
          nav.top = 0;
          nav.follow(listCount());
        }
        requestUpdate();
      } else {
        finish();
      }
    }
    return true;
  }
  return false;
}

void FlashcardBrowserActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  const std::string entry = files[index];

  if (isDirectoryEntry(entry)) {
    closeRouting();
    {
      RenderLock lock(*this);
      if (basepath.back() != '/') {
        basepath += "/";
      }
      basepath += entry.substr(0, entry.length() - 1);
      loadFiles();
      nav.selected = 0;
      nav.top = 0;
    }
    requestUpdate();
    return;
  }

  std::string fullPath = basepath;
  if (fullPath.back() != '/') {
    fullPath += "/";
  }
  fullPath += entry;
  (void)openDeckPath(fullPath);
}

void FlashcardBrowserActivity::drawChrome() {
  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_FLASHCARDS), tr(STR_OPEN));
}

void FlashcardBrowserActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), files.empty() ? "" : tr(STR_OPEN),
                                            files.empty() ? "" : tr(STR_DIR_UP), files.empty() ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void FlashcardBrowserActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // Current-path band at the bottom (separator on top, left-truncated path).
  {
    const int pathLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const fui::Rect band = screen.takeBottom(static_cast<int16_t>(pathLineHeight + metrics.verticalSpacing));
    screen.target().fill(fui::Rect{band.x, band.y, band.width, 3}, fui::Paint::solid(fui::Color::Black));
    const int pathY =
        band.y + metrics.verticalSpacing / 2 + (band.height - metrics.verticalSpacing / 2 - pathLineHeight) / 2;
    const int pathMaxWidth = band.width - metrics.contentSidePadding * 2;
    const char* pathStr = basepath.c_str();
    const char* pathDisplay = pathStr;
    char leftTruncBuf[256];
    if (renderer.getTextWidth(SMALL_FONT_ID, pathStr) > pathMaxWidth) {
      const char ellipsis[] = "\xe2\x80\xa6";
      const int ellipsisWidth = renderer.getTextWidth(SMALL_FONT_ID, ellipsis);
      const int available = pathMaxWidth - ellipsisWidth;
      const char* p = pathStr;
      while (*p) {
        if (renderer.getTextWidth(SMALL_FONT_ID, p) <= available) break;
        ++p;
        while (*p && (static_cast<unsigned char>(*p) & 0xC0) == 0x80) ++p;
      }
      snprintf(leftTruncBuf, sizeof(leftTruncBuf), "%s%s", ellipsis, p);
      pathDisplay = leftTruncBuf;
    }
    renderer.drawText(SMALL_FONT_ID, band.x + metrics.contentSidePadding, pathY, pathDisplay);
  }

  if (files.empty()) {
    screen.centeredText(tr(STR_NO_FILES_FOUND), screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  fui::TextStyle label = screen.theme().smallText;
  label.maxLines = 2;  // long deck names wrap; also the caller-owned marker
  props.labelText = label;
  props.partialTrailingRow = true;
  syncListViewport(screen, props);
  screen.list(props);
}

size_t FlashcardBrowserActivity::findEntry(const std::string& name) const {
  for (size_t index = 0; index < files.size(); ++index) {
    if (files[index] == name) {
      return index;
    }
  }
  return 0;
}
