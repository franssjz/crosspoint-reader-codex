#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Txt.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "FavoritesStore.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "activities/apps/AchievementsActivity.h"
#include "activities/apps/BookmarksAppActivity.h"
#include "activities/apps/DictionaryActivity.h"
#include "activities/apps/FavoritesAppActivity.h"
#include "activities/apps/FlashcardsAppActivity.h"
#include "activities/apps/IfFoundActivity.h"
#include "activities/apps/ReadingHeatmapActivity.h"
#include "activities/apps/ReadingProfileActivity.h"
#include "activities/apps/ReadingStatsActivity.h"
#include "activities/apps/SleepAppActivity.h"
#include "activities/apps/SyncDayActivity.h"
#include "activities/settings/ClockSyncActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "components/themes/lyra/LyraCarouselTheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/ShortcutRegistry.h"
#include "util/ShortcutUiMetadata.h"

namespace {
constexpr unsigned long RECENT_BOOK_LONG_PRESS_MS = 1000;
constexpr int DEFAULT_HOME_SHORTCUT_PAGE_SIZE = 4;
constexpr int LYRA_HOME_SHORTCUT_PAGE_SIZE = 5;
constexpr const char* CAROUSEL_FRAME_CACHE_DIR = "/.crosspoint/home-carousel-cache";
constexpr uint32_t FNV1A_OFFSET = 2166136261UL;
constexpr uint32_t FNV1A_PRIME = 16777619UL;

struct HomeShortcutEntry {
  const ShortcutDefinition* definition = nullptr;
  bool isAppsHub = false;
};

std::string getRecentBookConfirmationLabel(const RecentBook& book) {
  return !book.title.empty() ? book.title : book.path;
}

std::string getBookTitleFromPath(const std::string& path) {
  const size_t slashPos = path.find_last_of('/');
  const std::string filename = slashPos == std::string::npos ? path : path.substr(slashPos + 1);
  const size_t dotPos = filename.rfind('.');
  return dotPos == std::string::npos ? filename : filename.substr(0, dotPos);
}

bool homeUsesFavorites() { return SETTINGS.homeBookSource == CrossPointSettings::HOME_BOOKS_FAVORITES; }

RecentBook toRecentBook(const FavoriteBook& book) {
  RecentBook recentBook{book.bookId, book.path, book.title, book.author, book.coverBmpPath};
  if (recentBook.title.empty()) {
    recentBook.title = getBookTitleFromPath(recentBook.path);
  }
  return recentBook;
}

void updateHomeBookMetadata(const RecentBook& book) {
  if (homeUsesFavorites()) {
    FAVORITES.updateBook(book.path, book.title, book.author, book.coverBmpPath, book.bookId);
    return;
  }

  RECENT_BOOKS.updateBook(book.path, book.title, book.author, book.coverBmpPath, book.bookId);
}

bool canLoadHomeCover(const std::string& path) {
  return FsHelpers::hasEpubExtension(path) || FsHelpers::hasXtcExtension(path) || FsHelpers::hasTxtExtension(path) ||
         FsHelpers::hasMarkdownExtension(path);
}

bool isValidBmpFile(const std::string& path) {
  if (path.empty() || !Storage.exists(path.c_str())) {
    return false;
  }

  HalFile file;
  if (!Storage.openFileForRead("HOME", path, file)) {
    return false;
  }

  Bitmap bitmap(file);
  const bool valid = bitmap.parseHeaders() == BmpReaderError::Ok;
  file.close();
  return valid;
}

bool isValidHomeCoverPath(const std::string& coverBmpPath, const int coverHeight) {
  return isValidBmpFile(UITheme::getCoverThumbPath(coverBmpPath, coverHeight));
}

void removeInvalidHomeCoverTarget(const std::string& coverBmpPath, const int coverHeight) {
  if (coverBmpPath.empty()) {
    return;
  }

  const std::string resolvedPath = UITheme::getCoverThumbPath(coverBmpPath, coverHeight);
  if (Storage.exists(resolvedPath.c_str()) && !isValidBmpFile(resolvedPath)) {
    Storage.remove(resolvedPath.c_str());
  }
}

std::string getFavoriteRemovalKey(const FavoriteBook& book) {
  if (!book.path.empty()) {
    return book.path;
  }
  return book.bookId;
}

RecentBook resolveFavoriteForHome(const FavoriteBook& favorite) {
  RecentBook book = toRecentBook(favorite);
  if (book.path.empty() || !Storage.exists(book.path.c_str())) {
    return book;
  }

  const bool mayHaveCover = FsHelpers::hasEpubExtension(book.path) || FsHelpers::hasXtcExtension(book.path);
  if (!book.bookId.empty() && !book.title.empty() && (!mayHaveCover || !book.coverBmpPath.empty())) {
    return book;
  }

  const FavoriteBook resolved = FAVORITES.getDataFromBook(book.path);
  bool changed = false;

  if (book.bookId.empty() && !resolved.bookId.empty()) {
    book.bookId = resolved.bookId;
    changed = true;
  }
  if (book.title.empty() && !resolved.title.empty()) {
    book.title = resolved.title;
    changed = true;
  }
  if (book.author.empty() && !resolved.author.empty()) {
    book.author = resolved.author;
    changed = true;
  }
  if (book.coverBmpPath.empty() && !resolved.coverBmpPath.empty()) {
    book.coverBmpPath = resolved.coverBmpPath;
    changed = true;
  }

  if (changed) {
    FAVORITES.updateBook(book.path, book.title, book.author, book.coverBmpPath, book.bookId);
  }
  return book;
}

std::vector<HomeShortcutEntry> getHomeShortcutEntries(const bool hasOpdsServers) {
  std::vector<HomeShortcutEntry> entries;
  entries.push_back(HomeShortcutEntry{nullptr, true});

  for (const auto& definition : getShortcutDefinitions()) {
    if (definition.id == ShortcutId::OpdsBrowser && !hasOpdsServers) {
      continue;
    }
    const auto location = static_cast<CrossPointSettings::SHORTCUT_LOCATION>(SETTINGS.*(definition.locationPtr));
    if (location == CrossPointSettings::SHORTCUT_HOME && getShortcutVisibility(definition)) {
      entries.push_back(HomeShortcutEntry{&definition});
    }
  }

  std::stable_sort(entries.begin(), entries.end(), [](const HomeShortcutEntry& lhs, const HomeShortcutEntry& rhs) {
    const uint8_t lhsOrder = lhs.isAppsHub ? SETTINGS.appsHubShortcutOrder : getShortcutOrder(*lhs.definition);
    const uint8_t rhsOrder = rhs.isAppsHub ? SETTINGS.appsHubShortcutOrder : getShortcutOrder(*rhs.definition);
    return lhsOrder < rhsOrder;
  });

  return entries;
}

// Builds the carousel shortcut list without truncating configured Home entries.
// Settings is still injected if missing, and Apps remains pinned last so the
// user always has an escape hatch even with aggressive shortcut customization.
std::vector<HomeShortcutEntry> buildCarouselEntries(const std::vector<HomeShortcutEntry>& all) {
  std::vector<HomeShortcutEntry> result;
  HomeShortcutEntry appsEntry{nullptr, true};
  bool foundApps = false;
  bool foundSettings = false;

  for (const auto& e : all) {
    if (e.isAppsHub) {
      appsEntry = e;
      foundApps = true;
    } else {
      if (e.definition && e.definition->id == ShortcutId::Settings) {
        foundSettings = true;
      }
      result.push_back(e);
    }
  }

  if (!foundSettings) {
    for (const auto& def : getShortcutDefinitions()) {
      if (def.id == ShortcutId::Settings) {
        result.push_back(HomeShortcutEntry{&def});
        foundSettings = true;
        break;
      }
    }
  }

  if (foundApps) {
    result.push_back(appsEntry);
  }
  return result;
}

std::string getHomeShortcutTitle(const HomeShortcutEntry& entry) {
  if (entry.isAppsHub) {
    return tr(STR_APPS);
  }
  if (!entry.definition) {
    return "";
  }
  return ShortcutUiMetadata::getName(*entry.definition);
}

std::string getHomeShortcutSubtitle(const HomeShortcutEntry& entry) {
  return entry.definition ? ShortcutUiMetadata::getSubtitle(*entry.definition) : "";
}

UIIcon getHomeShortcutIcon(const HomeShortcutEntry& entry) {
  if (entry.isAppsHub) {
    return UIIcon::Apps;
  }
  return entry.definition ? entry.definition->icon : UIIcon::Folder;
}

bool showHomeShortcutAccessory(const HomeShortcutEntry& entry) {
  return entry.definition && ShortcutUiMetadata::showAccessory(*entry.definition);
}

bool isLyraCarouselTheme() {
  return static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_CAROUSEL;
}

int wrapBookIndex(int index, int bookCount) {
  if (bookCount <= 0) {
    return 0;
  }
  while (index < 0) {
    index += bookCount;
  }
  return index % bookCount;
}

uint32_t fnv1aByte(uint32_t hash, const uint8_t value) { return (hash ^ value) * FNV1A_PRIME; }

uint32_t fnv1aString(uint32_t hash, const std::string& value) {
  for (const char c : value) {
    hash = fnv1aByte(hash, static_cast<uint8_t>(c));
  }
  return fnv1aByte(hash, 0xFF);
}

uint32_t fnv1aU32(uint32_t hash, const uint32_t value) {
  hash = fnv1aByte(hash, static_cast<uint8_t>(value & 0xFF));
  hash = fnv1aByte(hash, static_cast<uint8_t>((value >> 8) & 0xFF));
  hash = fnv1aByte(hash, static_cast<uint8_t>((value >> 16) & 0xFF));
  return fnv1aByte(hash, static_cast<uint8_t>((value >> 24) & 0xFF));
}

std::string getCarouselCenterThumbPath(const RecentBook& book) {
  return UITheme::getCoverThumbPath(book.coverBmpPath, LyraCarouselTheme::kCenterCoverW,
                                    LyraCarouselTheme::kCenterCoverH);
}

std::string getCarouselLegacyThumbPath(const RecentBook& book) {
  return UITheme::getCoverThumbPath(book.coverBmpPath, LyraCarouselMetrics::values.homeCoverHeight);
}

bool hasCarouselUsableThumb(const RecentBook& book) {
  if (book.coverBmpPath.empty()) {
    return true;
  }
  const std::string centerCoverPath = getCarouselCenterThumbPath(book);
  if (Storage.exists(centerCoverPath.c_str())) {
    return true;
  }
  const std::string legacyCoverPath = getCarouselLegacyThumbPath(book);
  return Storage.exists(legacyCoverPath.c_str());
}

uint32_t hashCarouselThumbState(uint32_t hash, const RecentBook& book) {
  if (book.coverBmpPath.empty()) {
    return fnv1aByte(hash, 0);
  }
  const std::string centerCoverPath = getCarouselCenterThumbPath(book);
  const std::string legacyCoverPath = getCarouselLegacyThumbPath(book);
  hash = fnv1aByte(hash, Storage.exists(centerCoverPath.c_str()) ? 1 : 0);
  return fnv1aByte(hash, Storage.exists(legacyCoverPath.c_str()) ? 1 : 0);
}

uint8_t getCarouselBookProgressPercent(const RecentBook& recentBook) {
  const ReadingBookStats* stats = nullptr;
  if (!recentBook.bookId.empty()) {
    stats = READING_STATS.findBook(recentBook.bookId);
  }
  if (stats == nullptr) {
    stats = READING_STATS.findBook(recentBook.path);
  }
  if (stats == nullptr) {
    return 0;
  }
  return std::min<uint8_t>(stats->lastProgressPercent, 100);
}

uint32_t getCarouselFrameHash(const std::vector<RecentBook>& books, const int centerIdx, const int screenWidth,
                              const int screenHeight, const size_t bufferSize, const bool darkMode) {
  uint32_t hash = FNV1A_OFFSET;
  hash = fnv1aString(hash, "lyra-carousel-frame-v7-progress-badge");
  hash = fnv1aU32(hash, static_cast<uint32_t>(screenWidth));
  hash = fnv1aU32(hash, static_cast<uint32_t>(screenHeight));
  hash = fnv1aU32(hash, static_cast<uint32_t>(bufferSize));
  hash = fnv1aU32(hash, darkMode ? 1U : 0U);
  hash = fnv1aU32(hash, static_cast<uint32_t>(SETTINGS.homeBookSource));
  hash = fnv1aU32(hash, static_cast<uint32_t>(books.size()));
  hash = fnv1aU32(hash, static_cast<uint32_t>(centerIdx));

  for (const RecentBook& book : books) {
    hash = fnv1aString(hash, book.bookId);
    hash = fnv1aString(hash, book.path);
    hash = fnv1aString(hash, book.title);
    hash = fnv1aString(hash, book.author);
    hash = fnv1aString(hash, book.coverBmpPath);
    hash = hashCarouselThumbState(hash, book);
    hash = fnv1aByte(hash, getCarouselBookProgressPercent(book));
  }

  return hash;
}

std::string getCarouselFrameCachePathFromHash(const uint32_t hash) {
  char filename[96];
  std::snprintf(filename, sizeof(filename), "%s/%08lx.bin", CAROUSEL_FRAME_CACHE_DIR, static_cast<unsigned long>(hash));
  return filename;
}

int getHomeShortcutPageSize() {
  return static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA
             ? LYRA_HOME_SHORTCUT_PAGE_SIZE
             : DEFAULT_HOME_SHORTCUT_PAGE_SIZE;
}

bool shortcutMatchesMenuItem(const HomeShortcutEntry& entry, const HomeMenuItem item) {
  if (entry.isAppsHub || entry.definition == nullptr) {
    return false;
  }
  switch (item) {
    case HomeMenuItem::FILE_BROWSER:
      return entry.definition->id == ShortcutId::BrowseFiles;
    case HomeMenuItem::RECENTS:
      return entry.definition->id == ShortcutId::RecentBooks;
    case HomeMenuItem::OPDS_BROWSER:
      return entry.definition->id == ShortcutId::OpdsBrowser;
    case HomeMenuItem::FILE_TRANSFER:
      return entry.definition->id == ShortcutId::FileTransfer;
    case HomeMenuItem::SETTINGS_MENU:
      return entry.definition->id == ShortcutId::Settings;
    default:
      return false;
  }
}

}  // namespace

int HomeActivity::getMenuItemCount() const {
  auto entries = getHomeShortcutEntries(hasOpdsServers);
  if (isLyraCarouselTheme()) {
    entries = buildCarouselEntries(entries);
  }
  return static_cast<int>(recentBooks.size()) + static_cast<int>(entries.size());
}

void HomeActivity::loadRecentBooks(const int maxBooks) {
  invalidateResidentCarouselFrame();
  invalidateCarouselFrameHash();
  recentBooks.clear();
  if (homeUsesFavorites()) {
    const auto books = FAVORITES.getBooks();
    std::vector<std::string> staleFavorites;
    recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

    for (const FavoriteBook& book : books) {
      if (book.path.empty() || !Storage.exists(book.path.c_str())) {
        const std::string removalKey = getFavoriteRemovalKey(book);
        if (!removalKey.empty()) {
          staleFavorites.push_back(removalKey);
        }
        continue;
      }

      if (static_cast<int>(recentBooks.size()) < maxBooks) {
        recentBooks.push_back(resolveFavoriteForHome(book));
      }
    }

    for (const std::string& key : staleFavorites) {
      FAVORITES.removeBook(key);
    }
    return;
  }

  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    if (static_cast<int>(recentBooks.size()) >= maxBooks) {
      break;
    }
    if (!RecentBooksStore::isMissing(book)) {
      recentBooks.push_back(book);
    }
  }
}

void HomeActivity::reloadHomeBooks(const int maxBooks) {
  loadRecentBooks(maxBooks);

  const int menuCount = getMenuItemCount();
  if (selectorIndex >= menuCount) {
    selectorIndex = std::max(0, menuCount - 1);
  }

  recentsLoading = false;
  recentsLoaded = !needsRecentCoverLoad(UITheme::getInstance().getMetrics().homeCoverHeight);
  coverRendered = false;
  freeCoverBuffer();
}

bool HomeActivity::needsRecentCoverLoad(const int coverHeight) const {
  for (const RecentBook& book : recentBooks) {
    if (!canLoadHomeCover(book.path)) {
      continue;
    }

    if (book.coverBmpPath.empty()) {
      return true;
    }

    const bool missingThumb =
        isLyraCarouselTheme() ? !hasCarouselUsableThumb(book) : !isValidHomeCoverPath(book.coverBmpPath, coverHeight);
    if (missingThumb) {
      return true;
    }
  }
  return false;
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  // The first home render can cache a placeholder while thumbnails are still missing.
  // Drop that cache before generating covers so the next render reads the fresh BMPs.
  coverRendered = false;
  freeCoverBuffer();

  bool showingLoading = false;
  Rect popupRect;
  bool needsRefresh = false;

  const auto updateProgress = [this, &showingLoading, &popupRect](const int progress) {
    RenderLock lock(*this);
    if (!showingLoading) {
      showingLoading = true;
      popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
    }
    GUI.fillPopupProgress(renderer, popupRect, progress);
  };

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (isLyraCarouselTheme() && progress != lastCarouselBookIndex) {
      progress++;
      continue;
    }
    if (!canLoadHomeCover(book.path)) {
      progress++;
      continue;
    }

    const bool missingThumb =
        book.coverBmpPath.empty() ||
        (isLyraCarouselTheme() ? !hasCarouselUsableThumb(book) : !isValidHomeCoverPath(book.coverBmpPath, coverHeight));
    if (missingThumb) {
      if (isLyraCarouselTheme()) {
        carouselCoverLoadAttemptPath = book.path;
        carouselFramesReady = false;
        invalidateResidentCarouselFrame();
        invalidateCarouselFrameHash();
      }
      updateProgress(10 + progress * (90 / std::max(1, static_cast<int>(recentBooks.size()))));
      removeInvalidHomeCoverTarget(book.coverBmpPath, coverHeight);

      if (FsHelpers::hasEpubExtension(book.path)) {
        Epub epub(book.path, "/.crosspoint");
        if (epub.load(isLyraCarouselTheme(), true)) {
          if (!epub.getTitle().empty()) {
            book.title = epub.getTitle();
          }
          if (!epub.getAuthor().empty()) {
            book.author = epub.getAuthor();
          }
          book.coverBmpPath = epub.getThumbBmpPath();
          const bool success =
              isLyraCarouselTheme()
                  ? epub.generateThumbBmp(LyraCarouselTheme::kCenterCoverW, LyraCarouselTheme::kCenterCoverH) &&
                        isValidBmpFile(getCarouselCenterThumbPath(book))
                  : epub.generateThumbBmp(coverHeight) && isValidHomeCoverPath(book.coverBmpPath, coverHeight);
          if (!success && !isLyraCarouselTheme()) {
            removeInvalidHomeCoverTarget(book.coverBmpPath, coverHeight);
            book.coverBmpPath = "";
          }
          updateHomeBookMetadata(book);
          coverRendered = false;
          needsRefresh = true;
        }
      } else if (FsHelpers::hasXtcExtension(book.path)) {
        Xtc xtc(book.path, "/.crosspoint");
        if (xtc.load()) {
          const std::string title = xtc.getTitle();
          const std::string author = xtc.getAuthor();
          if (!title.empty()) {
            book.title = title;
          }
          if (!author.empty()) {
            book.author = author;
          }
          book.coverBmpPath = xtc.getThumbBmpPath();
          const bool success =
              isLyraCarouselTheme()
                  ? xtc.generateThumbBmp(LyraCarouselTheme::kCenterCoverW, LyraCarouselTheme::kCenterCoverH) &&
                        isValidBmpFile(getCarouselCenterThumbPath(book))
                  : xtc.generateThumbBmp(coverHeight) && isValidHomeCoverPath(book.coverBmpPath, coverHeight);
          if (!success && !isLyraCarouselTheme()) {
            removeInvalidHomeCoverTarget(book.coverBmpPath, coverHeight);
            book.coverBmpPath = "";
          }
          updateHomeBookMetadata(book);
          coverRendered = false;
          needsRefresh = true;
        }
      } else if (FsHelpers::hasTxtExtension(book.path) || FsHelpers::hasMarkdownExtension(book.path)) {
        Txt txt(book.path, "/.crosspoint");
        if (txt.load()) {
          const std::string title = txt.getTitle();
          if (!title.empty()) {
            book.title = title;
          }
          book.coverBmpPath = txt.getCoverBmpPath();
          removeInvalidHomeCoverTarget(book.coverBmpPath, coverHeight);
          const bool success = txt.generateCoverBmp() && isValidHomeCoverPath(book.coverBmpPath, coverHeight);
          if (!success) {
            removeInvalidHomeCoverTarget(book.coverBmpPath, coverHeight);
            book.coverBmpPath = "";
          }
          updateHomeBookMetadata(book);
          coverRendered = false;
          needsRefresh = true;
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
  if (needsRefresh) {
    if (isLyraCarouselTheme()) {
      carouselFramesReady = false;
      invalidateResidentCarouselFrame();
      invalidateCarouselFrameHash();
      preRenderCarouselFrames();
    }
    requestUpdate();
  }
}

void HomeActivity::scheduleCarouselCoverLoadIfNeeded() {
  if (!isLyraCarouselTheme() || recentBooks.empty() || lastCarouselBookIndex < 0 ||
      lastCarouselBookIndex >= static_cast<int>(recentBooks.size())) {
    return;
  }
  const RecentBook& book = recentBooks[lastCarouselBookIndex];
  if (book.path != carouselCoverLoadAttemptPath && canLoadHomeCover(book.path) &&
      (book.coverBmpPath.empty() || !hasCarouselUsableThumb(book))) {
    recentsLoaded = false;
    requestUpdate();
  }
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  hasOpdsServers = OPDS_STORE.hasServers();

  selectorIndex = 0;
  firstRenderDone = false;
  recentsLoading = false;
  recentsLoaded = false;
  lastCarouselBookIndex = 0;
  invalidateResidentCarouselFrame();
  invalidateCarouselFrameHash();
  carouselFramesReady = false;
  carouselCoverLoadAttemptPath.clear();

  const auto& metrics = UITheme::getInstance().getMetrics();
  reloadHomeBooks(metrics.homeRecentBooksCount);

  // Land on the shortcut the user came back from (ActivityManager::goHome).
  if (initialMenuItem != HomeMenuItem::NONE) {
    selectorIndex = indexForMenuItem(initialMenuItem);
    if (isLyraCarouselTheme() && !recentBooks.empty() && selectorIndex >= static_cast<int>(recentBooks.size())) {
      lastCarouselBookIndex = 0;
    }
  }

  requestUpdate();
}

int HomeActivity::indexForMenuItem(const HomeMenuItem item) const {
  if (item == HomeMenuItem::NONE) {
    return 0;
  }
  auto entries = getHomeShortcutEntries(hasOpdsServers);
  if (isLyraCarouselTheme()) {
    entries = buildCarouselEntries(entries);
  }
  for (size_t i = 0; i < entries.size(); i++) {
    if (shortcutMatchesMenuItem(entries[i], item)) {
      return static_cast<int>(recentBooks.size()) + static_cast<int>(i);
    }
  }
  // The shortcut is not on Home (moved to Apps): fall back to the first entry.
  return 0;
}

void HomeActivity::onExit() {
  Activity::onExit();
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  if (coverRectW <= 0 || coverRectH <= 0) return false;
  freeCoverBuffer();

  const size_t needed = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (needed == 0) return false;

  coverBuffer = static_cast<uint8_t*>(malloc(needed));
  if (!coverBuffer) {
    LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", static_cast<unsigned>(needed));
    return false;
  }
  coverBufferSize = needed;

  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize)) {
    free(coverBuffer);
    coverBuffer = nullptr;
    coverBufferSize = 0;
    return false;
  }

  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer || coverRectW <= 0 || coverRectH <= 0) return false;
  return renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize);
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferSize = 0;
  coverBufferStored = false;
}

bool HomeActivity::loadCarouselFrameFromStorage(int bookIndex) {
  if (recentBooks.empty()) {
    return false;
  }

  const int bookCount = static_cast<int>(recentBooks.size());
  const int safeBookIndex = wrapBookIndex(bookIndex, bookCount);
  const size_t bufferSize = renderer.getBufferSize();
  const std::string cachePath = getCarouselFrameCachePathFromHash(getCachedCarouselFrameHash(safeBookIndex));

  HalFile file;
  if (!Storage.openFileForRead("HCR", cachePath, file)) {
    return false;
  }

  if (file.size() != bufferSize) {
    file.close();
    Storage.remove(cachePath.c_str());
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    file.close();
    return false;
  }

  size_t totalRead = 0;
  while (totalRead < bufferSize) {
    const int bytesRead = file.read(frameBuffer + totalRead, bufferSize - totalRead);
    if (bytesRead <= 0) {
      break;
    }
    totalRead += static_cast<size_t>(bytesRead);
  }
  file.close();

  if (totalRead != bufferSize) {
    Storage.remove(cachePath.c_str());
    invalidateResidentCarouselFrame();
    return false;
  }

  invalidateResidentCarouselFrame();
  carouselFramesReady = true;
  return true;
}

bool HomeActivity::saveCarouselFrameToStorage(int bookIndex) {
  if (!isLyraCarouselTheme() || recentBooks.empty()) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  const int bookCount = static_cast<int>(recentBooks.size());
  const int safeBookIndex = wrapBookIndex(bookIndex, bookCount);
  const size_t bufferSize = renderer.getBufferSize();
  const std::string cachePath = getCarouselFrameCachePathFromHash(getCachedCarouselFrameHash(safeBookIndex));

  Storage.mkdir("/.crosspoint");
  Storage.mkdir(CAROUSEL_FRAME_CACHE_DIR);

  HalFile file;
  if (!Storage.openFileForWrite("HCR", cachePath, file)) {
    return false;
  }

  const size_t written = file.write(frameBuffer, bufferSize);
  file.close();

  if (written != bufferSize) {
    Storage.remove(cachePath.c_str());
    return false;
  }

  return true;
}

bool HomeActivity::renderCarouselFrame(int bookIndex) {
  if (recentBooks.empty()) {
    return false;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr, nullptr);
  HeaderDateUtils::drawTopLine(renderer, HeaderDateUtils::getDisplayDateText());

  bool localCoverRendered = false;
  bool localCoverBufferStored = false;
  bool localBufferRestored = false;
  const int bookCount = static_cast<int>(recentBooks.size());
  const int safeBookIndex = wrapBookIndex(bookIndex, bookCount);
  // setPreRenderIndex sets lastCarouselSelectorIndex so drawRecentBookCover
  // picks the correct center book. We pass bookCount (not safeBookIndex) as
  // selectorIndex so inCarouselRow=false and the frame is stored with a thin
  // outline; drawCarouselBorder() overlays the thick selection border at
  // display time only when the carousel row is actually active.
  LyraCarouselTheme::setPreRenderIndex(safeBookIndex);
  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, bookCount, localCoverRendered, localCoverBufferStored, localBufferRestored,
                          [] { return false; });

  if (!renderer.getFrameBuffer()) {
    invalidateResidentCarouselFrame();
    return false;
  }
  invalidateResidentCarouselFrame();
  carouselFramesReady = true;
  saveCarouselFrameToStorage(safeBookIndex);
  return true;
}

void HomeActivity::invalidateResidentCarouselFrame() {
  residentCarouselFrameIndex = -1;
  residentCarouselSelectorIndex = -1;
  residentCarouselFrameHash = 0;
  residentCarouselFrameValid = false;
}

void HomeActivity::invalidateCarouselFrameHash() {
  cachedCarouselFrameHashIndex = -1;
  cachedCarouselFrameHash = 0;
  cachedCarouselFrameHashValid = false;
}

void HomeActivity::requestFreshHomeRender(const bool immediate) {
  if (isLyraCarouselTheme()) {
    invalidateResidentCarouselFrame();
  }
  requestUpdate(immediate);
}

uint32_t HomeActivity::getCachedCarouselFrameHash(const int bookIndex) {
  if (recentBooks.empty()) {
    return 0;
  }

  const int safeBookIndex = wrapBookIndex(bookIndex, static_cast<int>(recentBooks.size()));
  if (!cachedCarouselFrameHashValid || cachedCarouselFrameHashIndex != safeBookIndex) {
    cachedCarouselFrameHash =
        getCarouselFrameHash(recentBooks, safeBookIndex, renderer.getScreenWidth(), renderer.getScreenHeight(),
                             renderer.getBufferSize(), renderer.isDarkMode());
    cachedCarouselFrameHashIndex = safeBookIndex;
    cachedCarouselFrameHashValid = true;
  }
  return cachedCarouselFrameHash;
}

void HomeActivity::preRenderCarouselFrames() {
  if (!isLyraCarouselTheme() || recentBooks.empty()) {
    return;
  }

  freeCoverBuffer();
  const int bookCount = static_cast<int>(recentBooks.size());
  const int centerIdx = wrapBookIndex(lastCarouselBookIndex, bookCount);
  // Load the center frame from the SD cache directly into the frame buffer, or
  // render it fresh and write it to the SD cache for future loads.
  if (!loadCarouselFrameFromStorage(centerIdx)) {
    renderCarouselFrame(centerIdx);
  }
  carouselFramesReady = true;
}

void HomeActivity::loop() {
  if (firstRenderDone && !recentsLoaded && !recentsLoading) {
    loadRecentCovers(UITheme::getInstance().getMetrics().homeCoverHeight);
    return;
  }

  const int menuCount = getMenuItemCount();
  const int recentCount = static_cast<int>(recentBooks.size());
  const int homeCount = std::max(0, menuCount - recentCount);
  const int shortcutPageSize = getHomeShortcutPageSize();

  if (isLyraCarouselTheme()) {
    // Carousel navigation: Left/Right move within the focused row;
    // Up/Down toggle between the carousel row and the shortcuts row.
    const bool inCarouselRow = recentCount > 0 && selectorIndex < recentCount;

    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (inCarouselRow) {
        selectorIndex = (selectorIndex + recentCount - 1) % recentCount;
        requestUpdate();
      } else if (homeCount > 0) {
        const int homeIdx = selectorIndex - recentCount;
        selectorIndex = recentCount + (homeIdx + homeCount - 1) % homeCount;
        requestUpdate();
      }
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (inCarouselRow) {
        selectorIndex = (selectorIndex + 1) % recentCount;
        requestUpdate();
      } else if (homeCount > 0) {
        const int homeIdx = selectorIndex - recentCount;
        selectorIndex = recentCount + (homeIdx + 1) % homeCount;
        requestUpdate();
      }
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
        mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      if (inCarouselRow && homeCount > 0) {
        lastCarouselBookIndex = selectorIndex;
        selectorIndex = recentCount;  // land on first shortcut
        requestUpdate();
      } else if (!inCarouselRow && recentCount > 0) {
        selectorIndex = wrapBookIndex(lastCarouselBookIndex, recentCount);
        requestUpdate();
      }
    }
  } else {
    buttonNavigator.onNextPress([this, menuCount] {
      selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
      requestUpdate();
    });

    buttonNavigator.onPreviousPress([this, menuCount] {
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
      requestUpdate();
    });

    buttonNavigator.onNextContinuous([this, menuCount, recentCount, homeCount, shortcutPageSize] {
      if (menuCount <= 0) {
        return;
      }

      if (homeCount <= shortcutPageSize) {
        selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
      } else if (selectorIndex < recentCount) {
        selectorIndex = recentCount;
      } else {
        const int selectedHomeIndex = selectorIndex - recentCount;
        selectorIndex = recentCount + ButtonNavigator::nextPageIndex(selectedHomeIndex, homeCount, shortcutPageSize);
      }
      requestUpdate();
    });

    buttonNavigator.onPreviousContinuous([this, menuCount, recentCount, homeCount, shortcutPageSize] {
      if (menuCount <= 0) {
        return;
      }

      if (homeCount <= shortcutPageSize) {
        selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
      } else if (selectorIndex < recentCount) {
        selectorIndex = recentCount + ButtonNavigator::previousPageIndex(0, homeCount, shortcutPageSize);
      } else {
        const int selectedHomeIndex = selectorIndex - recentCount;
        selectorIndex =
            recentCount + ButtonNavigator::previousPageIndex(selectedHomeIndex, homeCount, shortcutPageSize);
      }
      requestUpdate();
    });
  }

  // Touch: swipes move the selection, taps select/open covers and shortcuts.
  if (handleTouch()) {
    return;
  }

  // Back is otherwise unused on Home: resume the most recent book directly
  // (recentBooks is most-recent-first and already pruned of missing files).
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && !recentBooks.empty() &&
      mappedInput.getHeldTime() < RECENT_BOOK_LONG_PRESS_MS) {
    onSelectBook(recentBooks[0].path);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectorIndex < static_cast<int>(recentBooks.size()) &&
        mappedInput.getHeldTime() >= RECENT_BOOK_LONG_PRESS_MS) {
      promptRemoveSelectedBook();
      return;
    }
    activateSelection();
  }
}

bool HomeActivity::handleTouch() {
  if (!mappedInput.hasTouch()) {
    return false;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int menuCount = getMenuItemCount();
  const int recentCount = static_cast<int>(recentBooks.size());
  const bool carouselTheme = isLyraCarouselTheme();
  auto homeEntries = getHomeShortcutEntries(hasOpdsServers);
  if (carouselTheme) {
    homeEntries = buildCarouselEntries(homeEntries);
  }
  const int homeCount = static_cast<int>(homeEntries.size());
  const bool inCarouselRow = carouselTheme && recentCount > 0 && selectorIndex < recentCount;

  // Swipes: vertical moves the selection (or toggles carousel rows); horizontal
  // walks the focused carousel row.
  const auto swipe = mappedInput.wasSwipe();
  if (swipe != MappedInputManager::SwipeDir::None) {
    if (carouselTheme) {
      if (swipe == MappedInputManager::SwipeDir::Left || swipe == MappedInputManager::SwipeDir::Right) {
        // Swiping left reveals the next item (content follows the finger).
        const int step = swipe == MappedInputManager::SwipeDir::Left ? 1 : -1;
        if (inCarouselRow) {
          selectorIndex = wrapBookIndex(selectorIndex + step, recentCount);
        } else if (homeCount > 0) {
          const int homeIdx = selectorIndex - recentCount;
          selectorIndex = recentCount + wrapBookIndex(homeIdx + step, homeCount);
        }
      } else if (inCarouselRow && homeCount > 0) {
        lastCarouselBookIndex = selectorIndex;
        selectorIndex = recentCount;
      } else if (!inCarouselRow && recentCount > 0) {
        selectorIndex = wrapBookIndex(lastCarouselBookIndex, recentCount);
      }
    } else if (swipe == MappedInputManager::SwipeDir::Up) {
      selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    } else if (swipe == MappedInputManager::SwipeDir::Down) {
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    } else {
      return false;
    }
    requestUpdate();
    return true;
  }

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto select = [this](const int index, const MappedInputManager::RowTouch touch) {
    if (touch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != index) {
        selectorIndex = index;
        requestUpdate();
      }
      return;
    }
    selectorIndex = index;
    activateSelection();
  };

  // Cover tile(s): one column per visible book. On the carousel the centre
  // cover's column opens the focused book and the side-cover columns step the
  // carousel (column edges come from the theme's drawn cover geometry).
  if (recentCount > 0) {
    const int tileTop = metrics.homeTopPadding;
    int tileBottom = metrics.homeTopPadding + metrics.homeCoverTileHeight;
    if (carouselTheme) {
      // The tall carousel tile can run into the icon band; the band wins there.
      tileBottom = std::min(tileBottom, LyraCarouselTheme::menuBandRect(renderer).y);
      int tx = 0;
      int ty = 0;
      if (mappedInput.wasScreenTapped(tx, ty)) {
        if (ty >= tileTop && ty < tileBottom) {
          const int column = LyraCarouselTheme::coverColumnAt(renderer, tx);
          const int center = wrapBookIndex(lastCarouselBookIndex, recentCount);
          if (column == 0) {
            selectorIndex = center;
            activateSelection();
          } else {
            selectorIndex = wrapBookIndex(center + column, recentCount);
            requestUpdate();
          }
          return true;
        }
      } else if (mappedInput.wasScreenTouchDown(tx, ty) && ty >= tileTop && ty < tileBottom) {
        return true;  // no pre-select on the carousel; the tap decides
      }
    } else {
      const int coverColumnCount = std::max(1, metrics.homeRecentBooksCount);
      const int visibleBooks = std::min(recentCount, coverColumnCount);
      const int coverColumnWidth = (pageWidth - 2 * metrics.contentSidePadding) / coverColumnCount;
      int touchedBook = -1;
      const auto coverTouch = mappedInput.colTouch(touchedBook, metrics.contentSidePadding, coverColumnWidth,
                                                   visibleBooks, tileTop, tileBottom, coverColumnWidth);
      if (coverTouch != MappedInputManager::RowTouch::None) {
        select(touchedBook, coverTouch);
        return true;
      }
    }
  }

  if (homeCount <= 0) {
    return false;
  }

  if (carouselTheme) {
    // Bottom icon band: a sliding window of up to five shortcuts centred on the
    // selection; geometry shared with LyraCarouselTheme::drawButtonMenu.
    const int visibleCount = std::min(homeCount, LyraCarouselTheme::kVisibleMenuSlots);
    const int windowStart = LyraCarouselTheme::menuWindowStart(homeCount, selectorIndex - recentCount);
    const Rect band = LyraCarouselTheme::menuBandRect(renderer);
    const int tileW = band.width / visibleCount;
    int slot = -1;
    const auto menuTouch = mappedInput.colTouch(slot, band.x, tileW, visibleCount, band.y, band.y + band.height, tileW);
    if (menuTouch != MappedInputManager::RowTouch::None) {
      // Tap only: pre-selecting on touch-down would re-centre the window and
      // slide a different shortcut under the finger before the release.
      if (menuTouch == MappedInputManager::RowTouch::Tap) {
        selectorIndex = recentCount + windowStart + slot;
        activateSelection();
      }
      return true;
    }
    return false;
  }

  // Shortcut rows: same geometry as render(): a button menu below the cover
  // tile, paged with a sub-header once the entries exceed a page.
  const Rect shortcutsRect{
      0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing, pageWidth,
      pageHeight - (metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing +
                    metrics.buttonHintsHeight + metrics.verticalSpacing)};
  const int shortcutPageSize = getHomeShortcutPageSize();
  int listTop = shortcutsRect.y;
  int listHeight = shortcutsRect.height;
  int pageStart = 0;
  int pageItemCount = homeCount;
  if (homeCount > shortcutPageSize) {
    const int headerHeight = 34;
    listTop = shortcutsRect.y + headerHeight + 12;
    listHeight = std::max(0, shortcutsRect.height - headerHeight - 12);
    const int selectedHomeIndex = selectorIndex - recentCount;
    const int currentPage = std::max(0, selectedHomeIndex >= 0 ? selectedHomeIndex / shortcutPageSize : 0);
    pageStart = currentPage * shortcutPageSize;
    pageItemCount = std::min(shortcutPageSize, homeCount - pageStart);
  }
  if (pageItemCount <= 0) {
    return false;
  }
  // Row height as drawn by the theme (rows shrink when they do not all fit).
  const int gap = metrics.menuSpacing;
  const int rowHeight =
      std::min(GUI.getMenuRowHeight(renderer), (listHeight - gap * std::max(0, pageItemCount - 1)) / pageItemCount);
  if (rowHeight <= 0) {
    return false;
  }
  int menuRow = -1;
  const auto menuTouch =
      mappedInput.rowTouch(menuRow, listTop, rowHeight + gap, pageItemCount, 0, INT32_MAX, rowHeight);
  if (menuTouch != MappedInputManager::RowTouch::None) {
    select(recentCount + pageStart + menuRow, menuTouch);
    return true;
  }
  return false;
}

void HomeActivity::promptRemoveSelectedBook() {
  if (selectorIndex < 0 || selectorIndex >= static_cast<int>(recentBooks.size())) {
    return;
  }
  const RecentBook selectedBook = recentBooks[selectorIndex];
  const int currentSelection = selectorIndex;
  const bool deleteFromFavorites = homeUsesFavorites();
  const StrId confirmationPrompt =
      deleteFromFavorites ? StrId::STR_DELETE_FROM_FAVORITES : StrId::STR_DELETE_FROM_RECENTS;
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, I18N.get(confirmationPrompt),
                                                                getRecentBookConfirmationLabel(selectedBook)),
                         [this, selectedBook, currentSelection, deleteFromFavorites](const ActivityResult& result) {
                           if (isLyraCarouselTheme()) {
                             invalidateResidentCarouselFrame();
                           }

                           if (result.isCancelled) {
                             requestUpdate(true);
                             return;
                           }

                           const bool removed = deleteFromFavorites ? FAVORITES.removeBook(selectedBook.path)
                                                                    : RECENT_BOOKS.removeBook(selectedBook.path);
                           if (removed) {
                             const auto& metrics = UITheme::getInstance().getMetrics();
                             reloadHomeBooks(metrics.homeRecentBooksCount);
                             if (recentBooks.empty()) {
                               selectorIndex = 0;
                             } else if (currentSelection >= static_cast<int>(recentBooks.size())) {
                               selectorIndex = static_cast<int>(recentBooks.size()) - 1;
                             } else {
                               selectorIndex = currentSelection;
                             }
                             if (isLyraCarouselTheme()) {
                               lastCarouselBookIndex =
                                   selectorIndex < static_cast<int>(recentBooks.size()) ? selectorIndex : 0;
                               preRenderCarouselFrames();
                             }
                           }
                           requestUpdate(true);
                         });
}

void HomeActivity::activateSelection() {
  if (selectorIndex < 0) {
    return;
  }
  if (selectorIndex < static_cast<int>(recentBooks.size())) {
    onSelectBook(recentBooks[selectorIndex].path);
    return;
  }

  auto homeEntries = getHomeShortcutEntries(hasOpdsServers);
  if (isLyraCarouselTheme()) {
    homeEntries = buildCarouselEntries(homeEntries);
  }
  const int homeIndex = selectorIndex - static_cast<int>(recentBooks.size());
  if (homeIndex < 0 || homeIndex >= static_cast<int>(homeEntries.size())) {
    return;
  }

  const auto& selectedEntry = homeEntries[homeIndex];
  if (selectedEntry.isAppsHub) {
    onAppsOpen();
  } else if (selectedEntry.definition) {
    switch (selectedEntry.definition->id) {
      case ShortcutId::BrowseFiles:
        onFileBrowserOpen();
        break;
      case ShortcutId::ReadingStats:
        onReadingStatsOpen();
        break;
      case ShortcutId::SyncDay:
        onSyncDayOpen();
        break;
      case ShortcutId::Settings:
        activityManager.goToSettings();
        break;
      case ShortcutId::ReadingHeatmap:
        startActivityForResult(std::make_unique<ReadingHeatmapActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) { requestFreshHomeRender(true); });
        break;
      case ShortcutId::ReadingProfile:
        startActivityForResult(std::make_unique<ReadingProfileActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) { requestFreshHomeRender(true); });
        break;
      case ShortcutId::Achievements:
        startActivityForResult(std::make_unique<AchievementsActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) { requestFreshHomeRender(true); });
        break;
      case ShortcutId::IfFound:
        startActivityForResult(std::make_unique<IfFoundActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) { requestFreshHomeRender(true); });
        break;
      case ShortcutId::RecentBooks:
        activityManager.goToRecentBooks();
        break;
      case ShortcutId::Bookmarks:
        startActivityForResult(std::make_unique<BookmarksAppActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) { requestFreshHomeRender(true); });
        break;
      case ShortcutId::Favorites:
        startActivityForResult(std::make_unique<FavoritesAppActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) {
                                 const auto& metrics = UITheme::getInstance().getMetrics();
                                 reloadHomeBooks(metrics.homeRecentBooksCount);
                                 requestFreshHomeRender(true);
                               });
        break;
      case ShortcutId::Flashcards:
        startActivityForResult(std::make_unique<FlashcardsAppActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) { requestFreshHomeRender(true); });
        break;
      case ShortcutId::Dictionary:
        startActivityForResult(std::make_unique<DictionaryActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) { requestFreshHomeRender(true); });
        break;
      case ShortcutId::FileTransfer:
        activityManager.goToFileTransfer();
        break;
      case ShortcutId::Sleep:
        startActivityForResult(std::make_unique<SleepAppActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) { requestFreshHomeRender(true); });
        break;
      case ShortcutId::OpdsBrowser:
        onOpdsBrowserOpen();
        break;
    }
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int recentCount = static_cast<int>(recentBooks.size());
  const bool carouselTheme = isLyraCarouselTheme();
  const bool wasFirstRenderDone = firstRenderDone;
  const bool inCarouselRow = carouselTheme && selectorIndex < recentCount;
  if (inCarouselRow) {
    lastCarouselBookIndex = selectorIndex;
    scheduleCarouselCoverLoadIfNeeded();
  }

  bool usedCarouselFrame = false;
  if (carouselTheme && !recentBooks.empty()) {
    const int centerIdx = wrapBookIndex(lastCarouselBookIndex, recentCount);
    const uint32_t frameHash = getCachedCarouselFrameHash(centerIdx);
    const bool residentFrameMatches = residentCarouselFrameValid && residentCarouselFrameIndex == centerIdx &&
                                      residentCarouselSelectorIndex == selectorIndex &&
                                      residentCarouselFrameHash == frameHash;
    if (!residentFrameMatches) {
      // Load the center frame from the SD cache directly into the frame buffer,
      // or render it fresh (and cache to SD). No intermediate slot allocation.
      if (!loadCarouselFrameFromStorage(centerIdx)) {
        renderCarouselFrame(centerIdx);
      }
    }

    uint8_t* frameBuffer = renderer.getFrameBuffer();
    if (frameBuffer) {
      renderer.fillRect(0, 0, pageWidth, metrics.homeTopPadding, false);
      GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr, nullptr);
      HeaderDateUtils::drawTopLine(renderer, HeaderDateUtils::getDisplayDateText());
      GUI.drawCarouselBorder(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                             inCarouselRow);
      usedCarouselFrame = true;
      residentCarouselFrameIndex = centerIdx;
      residentCarouselSelectorIndex = selectorIndex;
      residentCarouselFrameHash = frameHash;
      residentCarouselFrameValid = true;
    }
  }

  if (!usedCarouselFrame) {
    invalidateResidentCarouselFrame();
    renderer.clearScreen();
    bool bufferRestored = coverBufferStored && restoreCoverBuffer();

    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr, nullptr);
    HeaderDateUtils::drawTopLine(renderer, HeaderDateUtils::getDisplayDateText());

    coverRectX = 0;
    coverRectY = metrics.homeTopPadding;
    coverRectW = pageWidth;
    coverRectH = metrics.homeCoverTileHeight;
    GUI.drawRecentBookCover(renderer, Rect{coverRectX, coverRectY, coverRectW, coverRectH}, recentBooks, selectorIndex,
                            coverRendered, coverBufferStored, bufferRestored,
                            std::bind(&HomeActivity::storeCoverBuffer, this));
  }

  auto homeEntries = getHomeShortcutEntries(hasOpdsServers);
  if (carouselTheme) {
    homeEntries = buildCarouselEntries(homeEntries);
  }
  const int selectedHomeIndex = selectorIndex - static_cast<int>(recentBooks.size());
  const Rect shortcutsRect{
      0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing, pageWidth,
      pageHeight - (metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing +
                    metrics.buttonHintsHeight + metrics.verticalSpacing)};

  const int shortcutDisplayCount = static_cast<int>(homeEntries.size());
  const int shortcutPageSize = getHomeShortcutPageSize();

  if (carouselTheme || shortcutDisplayCount <= shortcutPageSize) {
    GUI.drawButtonMenu(
        renderer, shortcutsRect, shortcutDisplayCount, selectedHomeIndex,
        [&homeEntries](const int index) { return getHomeShortcutTitle(homeEntries[index]); },
        [&homeEntries](const int index) { return getHomeShortcutIcon(homeEntries[index]); },
        [&homeEntries](const int index) { return getHomeShortcutSubtitle(homeEntries[index]); },
        [&homeEntries](const int index) { return showHomeShortcutAccessory(homeEntries[index]); });
  } else {
    const int headerHeight = 34;
    const int listTop = shortcutsRect.y + headerHeight + 12;
    const int listHeight = std::max(0, shortcutsRect.height - headerHeight - 12);
    const int currentPage = std::max(0, selectedHomeIndex >= 0 ? selectedHomeIndex / shortcutPageSize : 0);
    const int totalPages = (static_cast<int>(homeEntries.size()) + shortcutPageSize - 1) / shortcutPageSize;
    const int pageStart = currentPage * shortcutPageSize;
    const int pageItemCount = std::min(shortcutPageSize, static_cast<int>(homeEntries.size()) - pageStart);
    const int localSelectedIndex = (selectedHomeIndex >= pageStart && selectedHomeIndex < pageStart + pageItemCount)
                                       ? selectedHomeIndex - pageStart
                                       : -1;
    const std::string sectionLabel =
        std::string(tr(STR_SHORTCUTS_SECTION)) + " (" + std::to_string(homeEntries.size()) + ")";
    const std::string pageLabel = std::to_string(currentPage + 1) + "/" + std::to_string(totalPages);

    GUI.drawSubHeader(
        renderer,
        Rect{metrics.contentSidePadding, shortcutsRect.y, pageWidth - metrics.contentSidePadding * 2, headerHeight},
        sectionLabel.c_str(), pageLabel.c_str());
    GUI.drawButtonMenu(
        renderer, Rect{0, listTop, pageWidth, listHeight}, pageItemCount, localSelectedIndex,
        [&homeEntries, pageStart](const int index) { return getHomeShortcutTitle(homeEntries[pageStart + index]); },
        [&homeEntries, pageStart](const int index) { return getHomeShortcutIcon(homeEntries[pageStart + index]); },
        [&homeEntries, pageStart](const int index) { return getHomeShortcutSubtitle(homeEntries[pageStart + index]); },
        [&homeEntries, pageStart](const int index) {
          return showHomeShortcutAccessory(homeEntries[pageStart + index]);
        });
  }

  const char* backLabel = recentBooks.empty() ? "" : tr(STR_RESUME);
  const auto labels = carouselTheme
                          ? mappedInput.mapLabels(backLabel, tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT))
                          : mappedInput.mapLabels(backLabel, tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (cleanInitialRefresh && !firstRenderDone) {
    // Wake / home-gesture entry: one HALF refresh clears the retained frame.
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  } else {
    renderer.displayBuffer();
  }

  if (wasFirstRenderDone && carouselTheme && recentsLoaded && !carouselFramesReady && !recentBooks.empty()) {
    preRenderCarouselFrames();
    if (carouselFramesReady) {
      requestUpdate();
    }
  }

  if (!firstRenderDone) {
    firstRenderDone = true;
    if (!recentsLoaded || (carouselTheme && recentsLoaded && !carouselFramesReady)) {
      requestUpdate();
    }
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onAppsOpen() { activityManager.goToApps(); }

void HomeActivity::onReadingStatsOpen() {
  activityManager.replaceActivity(std::make_unique<ReadingStatsActivity>(renderer, mappedInput));
}

void HomeActivity::onSyncDayOpen() {
  if (SETTINGS.isHardwareRtcAutoDayClockActive()) {
    activityManager.replaceActivity(std::make_unique<ClockSyncActivity>(renderer, mappedInput));
    return;
  }
  activityManager.replaceActivity(std::make_unique<SyncDayActivity>(renderer, mappedInput));
}

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }
