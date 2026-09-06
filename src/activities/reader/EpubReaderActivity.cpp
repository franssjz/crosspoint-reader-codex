#include "EpubReaderActivity.h"

#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalFrontlight.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <MemoryBudget.h>
#include <esp_system.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>

#include "AchievementsStore.h"
#include "BookmarksActivity.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "DictionaryHistoryActivity.h"
#include "DictionaryWordSelectActivity.h"
#include "EpubReaderChapterSelectionActivity.h"
#include "EpubReaderFootnotesActivity.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "EpubReaderUtils.h"
#include "HighlightTextMatcher.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncActivity.h"
#include "MappedInputManager.h"
#include "ProgressFile.h"
#include "QrDisplayActivity.h"
#include "ReaderFontSizes.h"
#include "ReaderPosition.h"
#include "ReaderQuickSettingsActivity.h"
#include "ReaderUtils.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "SdCardFontGlobals.h"
#include "activities/apps/DictionaryActivity.h"
#include "activities/apps/ReadingStatsDetailActivity.h"
#include "activities/settings/TextSettingsActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/AchievementPopupUtils.h"
#include "util/BookIdentity.h"
#include "util/ButtonNavigator.h"
#include "util/CompletedBookMover.h"
#include "util/ScreenshotUtil.h"
#include "util/TaskWatchdog.h"

namespace {
// Long Confirm press: the fork's page-mark toggle threshold, now shared by every
// SETTINGS.longPressMenuFunction action that fires from a held Confirm.
constexpr unsigned long bookmarkToggleMs = 700;
// pages per minute, first item is 1 to prevent division by zero if accessed
constexpr int PAGE_TURN_RATES[] = {1, 1, 3, 6, 12};

int clampPercent(int percent) {
  if (percent < 0) {
    return 0;
  }
  if (percent > 100) {
    return 100;
  }
  return percent;
}

std::string getStatsChapterTitle(Epub& epub, const int spineIndex) {
  int tocIndex = epub.getTocIndexForSpineIndex(spineIndex);
  if (tocIndex < 0) {
    int nearestTocIndex = -1;
    int nearestSpineIndex = -1;
    for (int index = 0; index < epub.getTocItemsCount(); ++index) {
      const int tocSpineIndex = epub.getSpineIndexForTocIndex(index);
      if (tocSpineIndex <= spineIndex && tocSpineIndex >= nearestSpineIndex) {
        nearestSpineIndex = tocSpineIndex;
        nearestTocIndex = index;
      }
    }
    tocIndex = nearestTocIndex;
  }

  if (tocIndex < 0) {
    return "";
  }

  const auto tocItem = epub.getTocItem(tocIndex);
  return tocItem.title;
}

uint8_t getStatsChapterProgressPercent(const int currentPage, const int pageCount) {
  if (pageCount <= 0) {
    return 0;
  }

  return static_cast<uint8_t>(clampPercent(
      static_cast<int>((static_cast<float>(currentPage + 1) / static_cast<float>(pageCount)) * 100.0f + 0.5f)));
}

bool releaseReaderSdFontCachesForLowMemory(const GfxRenderer& renderer, const char* tag, const char* reason) {
  const int fontId = SETTINGS.getReaderFontId();
  if (!renderer.isSdCardFont(fontId)) {
    return false;
  }

  const auto before = MemoryBudget::snapshot();
  if (!renderer.releaseSdCardFontForLowMemory(fontId)) {
    return false;
  }
  const auto after = MemoryBudget::snapshot();
  LOG_DBG(tag, "Released SD font caches after %s: free=%u->%u maxAlloc=%u->%u", reason, before.freeHeap, after.freeHeap,
          before.maxAllocHeap, after.maxAllocHeap);
  return true;
}

void markStatsCompletedAtEnd(Epub& epub, int spineIndex) {
  const int spineCount = epub.getSpineItemsCount();
  if (spineCount <= 0) {
    READING_STATS.updateProgress(100, true, "", 100);
    return;
  }

  if (spineIndex >= spineCount) {
    spineIndex = spineCount - 1;
  } else if (spineIndex < 0) {
    spineIndex = 0;
  }

  READING_STATS.updateProgress(100, true, getStatsChapterTitle(epub, spineIndex), 100);
}

std::string getStableProgressPath(const std::string& bookId) {
  return BookIdentity::getStableDataFilePath(bookId, "epub_progress.bin");
}

std::string getLegacyProgressPath(Epub& epub) { return epub.getCachePath() + "/progress.bin"; }

std::string extractBookmarkSnippet(Section& section) {
  auto page = section.loadPage(section.currentPage);
  if (!page) {
    return "";
  }

  std::string snippet;
  for (const auto& element : page->elements) {
    if (element->getTag() != TAG_PageLine) {
      continue;
    }

    const auto& line = static_cast<const PageLine&>(*element);
    if (!line.getBlock()) {
      continue;
    }

    for (uint16_t i = 0; i < line.getBlock()->wordCount(); ++i) {
      if (!snippet.empty()) {
        snippet += ' ';
      }
      snippet += line.getBlock()->wordText(i);
      if (snippet.size() >= 80) {
        return snippet;
      }
    }
  }

  return snippet;
}

void exitReaderToHomeOrStats(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& bookPath) {
  READING_STATS.endSession();
  ACHIEVEMENTS.recordSessionEnded(READING_STATS.getLastSessionSnapshot());
  showPendingAchievementPopups(renderer);
  const bool countedSession = READING_STATS.getLastSessionSnapshot().valid &&
                              READING_STATS.getLastSessionSnapshot().counted &&
                              READING_STATS.getLastSessionSnapshot().path == bookPath;

  if (SETTINGS.showStatsAfterReading && countedSession && !bookPath.empty()) {
    activityManager.replaceActivity(
        std::make_unique<ReadingStatsDetailActivity>(renderer, mappedInput, bookPath, ReadingStatsDetailContext{true}));
  } else {
    activityManager.goHome();
  }
}

// Progress record: spine, page, chapter page count (6 bytes, fork layout) plus
// the page's visible-text offset (4 more bytes, upstream layout) when known.
bool writeReaderProgressFile(const std::string& progressPath, const int spineIndex, const int currentPage,
                             const int pageCount, const std::optional<uint32_t> visibleTextOffset) {
  uint8_t data[10];
  data[0] = spineIndex & 0xFF;
  data[1] = (spineIndex >> 8) & 0xFF;
  data[2] = currentPage & 0xFF;
  data[3] = (currentPage >> 8) & 0xFF;
  data[4] = pageCount & 0xFF;
  data[5] = (pageCount >> 8) & 0xFF;
  size_t dataSize = 6;
  if (visibleTextOffset.has_value()) {
    data[6] = *visibleTextOffset & 0xFF;
    data[7] = (*visibleTextOffset >> 8) & 0xFF;
    data[8] = (*visibleTextOffset >> 16) & 0xFF;
    data[9] = (*visibleTextOffset >> 24) & 0xFF;
    dataSize = sizeof(data);
  }
  return ProgressFile::writeAtomicPath("ERS", progressPath, data, dataSize);
}

struct HighlightWordRef {
  const PageLine* line = nullptr;
  const TextBlock* block = nullptr;
  uint16_t index = 0;
};

bool hasEmSpacePrefix(const char* text) {
  return text && static_cast<unsigned char>(text[0]) == 0xE2 && static_cast<unsigned char>(text[1]) == 0x80 &&
         static_cast<unsigned char>(text[2]) == 0x83;
}

HighlightTextMatcher::TokenFragmentResult matchHighlightFragment(const HighlightWordRef& ref, const char* token,
                                                                 const size_t tokenLength, const size_t tokenOffset) {
  const char* rawWord = ref.block->wordText(ref.index);
  const char* word = rawWord + (hasEmSpacePrefix(rawWord) ? 3 : 0);
  return HighlightTextMatcher::matchTokenFragment(word, ref.block->wordEndsWithInsertedHyphen(ref.index), token,
                                                  tokenLength, tokenOffset);
}

}  // namespace

EpubReaderActivity::EpubReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       std::unique_ptr<Epub> epub, const int initialBookmarkSpineIndex,
                                       const int initialBookmarkPage,
                                       const std::optional<uint32_t> initialBookmarkVisibleTextOffset,
                                       const bool allowFastInitialRefresh)
    : Activity("EpubReader", renderer, mappedInput),
      epub(std::move(epub)),
      initialBookmarkSpineIndex(initialBookmarkSpineIndex),
      initialBookmarkPage(initialBookmarkPage),
      initialBookmarkVisibleTextOffset(initialBookmarkVisibleTextOffset) {
  if (allowFastInitialRefresh) {
    // Upstream: boot -> last book handoff may skip the first clean refresh.
    const int refreshFrequency = SETTINGS.getRefreshFrequency();
    pagesUntilFullRefresh = refreshFrequency > 1 ? refreshFrequency : 2;
  }
}

EpubReaderActivity::~EpubReaderActivity() {
  ImageBlock::setExtractor(nullptr, nullptr);
  discardOverlayPage();  // free the overlay's page snapshot if one is held
  section.reset();
  epub.reset();
}

void EpubReaderActivity::onEnter() {
  Activity::onEnter();

  if (!epub) {
    return;
  }

  ImageBlock::clearSessionRenderFailures();
  ImageBlock::setExtractor(epub.get(), [](void* ctx, const char* src, const char* dest) {
    return static_cast<Epub*>(ctx)->extractItemToFile(src, dest);
  });

  ensureSdFontLoaded();

  // Configure screen orientation based on settings
  // NOTE: This affects layout math and must be applied before any render calls.
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
  appliedOrientation = SETTINGS.orientation;

  epub->setupCacheDir();
  applyPendingSyncSession();
  stableBookId = BookIdentity::resolveStableBookId(epub->getPath());
  bookmarkStore.load(epub->getCachePath(), stableBookId);

  HalFile f;
  bool loadedProgress = false;
  bool loadedFromLegacy = false;
  const std::string stableProgressPath = getStableProgressPath(stableBookId);
  const std::string legacyProgressPath = getLegacyProgressPath(*epub);
  const std::string progressPath = (!stableProgressPath.empty() && Storage.exists(stableProgressPath.c_str()))
                                       ? stableProgressPath
                                       : legacyProgressPath;
  if (progressPath == legacyProgressPath) {
    loadedFromLegacy = !stableProgressPath.empty() && Storage.exists(legacyProgressPath.c_str());
  }
  if (Storage.openFileForRead("ERS", progressPath, f)) {
    uint8_t data[10];
    const int dataSize = f.read(data, sizeof(data));
    if (dataSize == 4 || dataSize == 6 || dataSize == 10) {
      currentSpineIndex = data[0] + (data[1] << 8);
      nextPageNumber = data[2] + (data[3] << 8);
      if (nextPageNumber == UINT16_MAX) {
        LOG_DBG("ERS", "Ignoring stale last-page sentinel from progress cache");
        nextPageNumber = 0;
      }
      const int spineCount = epub->getSpineItemsCount();
      if (spineCount > 0 && currentSpineIndex >= spineCount) {
        LOG_DBG("ERS", "Ignoring stale end-book spine index from progress cache: %d", currentSpineIndex);
        currentSpineIndex = spineCount - 1;
        nextPageNumber = 0;
      }
      cachedSpineIndex = currentSpineIndex;
      loadedProgress = true;
      LOG_DBG("ERS", "Loaded cache: %d, %d", currentSpineIndex, nextPageNumber);
    }
    if (dataSize >= 6) {
      cachedChapterTotalPageCount = data[4] + (data[5] << 8);
    }
    if (dataSize == 10) {
      cachedVisibleTextOffset = static_cast<uint32_t>(data[6]) | (static_cast<uint32_t>(data[7]) << 8) |
                                (static_cast<uint32_t>(data[8]) << 16) | (static_cast<uint32_t>(data[9]) << 24);
    }
    f.close();
    if (loadedFromLegacy) {
      writeReaderProgressFile(stableProgressPath.empty() ? legacyProgressPath : stableProgressPath, currentSpineIndex,
                              nextPageNumber, cachedChapterTotalPageCount, cachedVisibleTextOffset);
      if (!stableProgressPath.empty()) BookIdentity::ensureStableDataDir(stableBookId);
    }
  }
  // Only apply the EPUB text reference on first open; a saved position in spine 0 is valid progress.
  if (!loadedProgress && currentSpineIndex == 0) {
    int textSpineIndex = epub->getSpineIndexForTextReference();
    if (textSpineIndex != 0) {
      currentSpineIndex = textSpineIndex;
      cachedVisibleTextOffset.reset();
      LOG_DBG("ERS", "Opened for first time, navigating to text reference at index %d", textSpineIndex);
    }
  }

  if (initialBookmarkSpineIndex >= 0) {
    const int maxSpineIndex = std::max(0, epub->getSpineItemsCount() - 1);
    currentSpineIndex = std::min(initialBookmarkSpineIndex, maxSpineIndex);
    nextPageNumber = std::max(0, initialBookmarkPage);
    pendingVisibleTextOffset = initialBookmarkVisibleTextOffset;
    cachedSpineIndex = currentSpineIndex;
    clearDeferredReposition();
  }

  sessionStartSpineIndex = currentSpineIndex;
  sessionStartPage = nextPageNumber;
  sessionProgressTouched = false;

  // Save current epub as last opened epub and add to recent books
  APP_STATE.openEpubPath = epub->getPath();
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), epub->getThumbBmpPath(), stableBookId);
  READING_STATS.beginSession(
      epub->getPath(), epub->getTitle(), epub->getAuthor(), epub->getCoverBmpPath(),
      clampPercent(static_cast<int>(epub->calculateProgress(currentSpineIndex, 0.0f) * 100.0f + 0.5f)),
      getStatsChapterTitle(*epub, currentSpineIndex), 0);

  // Trigger first update
  requestUpdate();
}

void EpubReaderActivity::onExit() {
  Activity::onExit();

  ReaderUtils::requestReaderUiTransitionRefresh(renderer);

  // A footnote excursion must not persist as the reading position.
  if (footnoteDepth > 0 && epub) {
    const SavedPosition& origin = savedPositions[0];
    saveProgress(origin.spineIndex, origin.pageNumber, 0);
    footnoteDepth = 0;
  }

  // Reset orientation back to portrait for the rest of the UI
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  APP_STATE.readerActivityLoadCount = 0;
  READING_STATS.endSession();
  ACHIEVEMENTS.recordSessionEnded(READING_STATS.getLastSessionSnapshot());
  bookmarkStore.save();
  invalidateCurrentOverlayPageCache();
  overlayPopup.dismiss();
  toolbarUi.reset();
  discardOverlayPage();
  endOfBookOptions.reset();
  endOfBookOptionsReady.store(false, std::memory_order_release);
  ImageBlock::setExtractor(nullptr, nullptr);
  section.reset();
  epub.reset();
  APP_STATE.saveToFile();
}

ReaderRenderSpec EpubReaderActivity::makeRenderSpec(const uint16_t viewportWidth, const uint16_t viewportHeight) const {
  return SETTINGS.readerRenderSpec(viewportWidth, viewportHeight);
}

bool EpubReaderActivity::buildTickHeapGate() {
  buildHeapPaused =
      ESP.getFreeHeap() < BACKGROUND_BUILD_MIN_FREE_HEAP || ESP.getMaxAllocHeap() < BACKGROUND_BUILD_MIN_MAX_ALLOC;
  return !buildHeapPaused;
}

bool EpubReaderActivity::applyDeferredReposition() {
  if (!section || section->isBuilding()) return false;
  if (!cachedVisibleTextOffset.has_value() && cachedChapterTotalPageCount == 0) return false;
  bool changed = false;
  if (currentSpineIndex == cachedSpineIndex) {
    int newPage = section->currentPage;
    bool mappedOffset = false;
    // The content offset survives any re-pagination; the page-count ratio is
    // the fallback for caches written before offsets were recorded.
    if (cachedVisibleTextOffset.has_value()) {
      if (const auto offsetPage = section->getPageForVisibleTextOffset(*cachedVisibleTextOffset)) {
        newPage = *offsetPage;
        mappedOffset = true;
      }
    }
    if (!mappedOffset && cachedChapterTotalPageCount > 0) {
      newPage = ReaderPosition::resolveRestoredPage(section->currentPage, cachedChapterTotalPageCount,
                                                    section->pageCount, pendingPaginationReposition);
    }
    if (newPage < 0) newPage = 0;
    if (section->pageCount > 0 && newPage >= static_cast<int>(section->pageCount)) {
      newPage = section->pageCount - 1;
    }
    changed = newPage != section->currentPage;
    section->currentPage = newPage;
  }
  clearDeferredReposition();
  return changed;
}

void EpubReaderActivity::clearDeferredReposition() {
  cachedChapterTotalPageCount = 0;
  cachedVisibleTextOffset.reset();
  pendingPaginationReposition = false;
}

// Call under RenderLock before resetting the section for a layout change.
void EpubReaderActivity::rememberCurrentLayoutPosition() {
  if (!section) return;
  cachedSpineIndex = currentSpineIndex;
  cachedChapterTotalPageCount = section->estimatedTotalPages();
  pendingPaginationReposition = true;
  nextPageNumber = section->currentPage;
  cachedVisibleTextOffset.reset();
  if (section->currentPage >= 0 && section->currentPage < section->pageCount) {
    cachedVisibleTextOffset = section->getVisibleTextOffsetForPage(static_cast<uint16_t>(section->currentPage));
  }
}

bool EpubReaderActivity::isAtEndOfBook() const {
  return epub && currentSpineIndex > 0 && currentSpineIndex >= epub->getSpineItemsCount();
}

void EpubReaderActivity::returnFromEndOfBook() {
  if (!epub) return;
  currentSpineIndex = std::max(epub->getSpineItemsCount() - 1, 0);
  nextPageNumber = 0;
  pendingPageJump = std::numeric_limits<uint16_t>::max();
}

bool EpubReaderActivity::endOfBookMenuActive() const {
  return isAtEndOfBook() && endOfBookOptionsReady.load(std::memory_order_acquire) && endOfBookOptions &&
         endOfBookOptions->menuActive();
}

void EpubReaderActivity::clearEndOfBookOptionsIfNeeded() {
  if (isAtEndOfBook() || !endOfBookOptionsReady.load(std::memory_order_acquire)) return;

  RenderLock lock(*this);
  endOfBookOptionsReady.store(false, std::memory_order_release);
  endOfBookOptions.reset();
}

bool EpubReaderActivity::handleEndOfBookMenu() {
  if (!endOfBookMenuActive()) {
    return false;
  }

  std::string openPath;
  switch (endOfBookOptions->handleMenuInput(mappedInput, &openPath)) {
    case EndOfBookOptions::Action::OpenBook:
      markStatsCompletedAtEnd(*epub, currentSpineIndex);
      moveCompletedBookIfEnabled();
      activityManager.goToReader(openPath);
      return true;
    case EndOfBookOptions::Action::GoHome:
      markStatsCompletedAtEnd(*epub, currentSpineIndex);
      if (tryAutoPushOnClose()) return true;
      exitReaderAfterOptionalCompletedMove();
      return true;
    case EndOfBookOptions::Action::LastPage:
      returnFromEndOfBook();
      requestUpdate();
      return true;
    case EndOfBookOptions::Action::Redraw:
      requestUpdate();
      return true;
    case EndOfBookOptions::Action::None:
      return false;
  }
  return false;
}

bool EpubReaderActivity::handleBackNavigation() {
  // No left-edge swipe-to-exit on the reading surface: in swipe page-turn mode
  // a right swipe must page back instead (see ReaderUtils::handleBackNavigation).
  if (mappedInput.wasBackGesture()) {
    return false;
  }

  const bool backTriggered =
      mappedInput.wasLongPressed(MappedInputManager::Button::Back, ReaderUtils::GO_BACK_OR_HOME_MS) ||
      mappedInput.wasReleased(MappedInputManager::Button::Back);
  if (!backTriggered) return false;

  const bool longPress = mappedInput.getHeldTime() >= ReaderUtils::GO_BACK_OR_HOME_MS;

  // A short Back while inside a footnote excursion returns to the origin page.
  if (!longPress && footnoteDepth > 0) {
    restoreSavedPosition();
    return true;
  }

  if (longPress != static_cast<bool>(SETTINGS.backShortToFileBrowser)) {
    const std::string fileBrowserPath = moveCompletedBookIfEnabled();
    READING_STATS.endSession();
    ACHIEVEMENTS.recordSessionEnded(READING_STATS.getLastSessionSnapshot());
    showPendingAchievementPopups(renderer);
    activityManager.goToFileBrowser(fileBrowserPath);
    return true;
  }

  if (tryAutoPushOnClose()) {
    return true;
  }
  exitReaderAfterOptionalCompletedMove();
  return true;
}

// Short power press configured as "Footnotes" (upstream): one footnote jumps
// straight to it, several open the list, and inside a footnote it goes back.
void EpubReaderActivity::openFootnotesFromPowerButton() {
  if (footnoteDepth > 0) {
    restoreSavedPosition();
    return;
  }
  if (currentPageFootnotes.size() == 1) {
    navigateToHref(currentPageFootnotes[0].href, true);
  } else if (currentPageFootnotes.size() > 1) {
    READING_STATS.noteActivity();
    startActivityForResult(std::make_unique<EpubReaderFootnotesActivity>(renderer, mappedInput, currentPageFootnotes),
                           [this](const ActivityResult& result) {
                             READING_STATS.resumeSession();
                             if (!result.isCancelled) {
                               const auto& footnoteResult = std::get<FootnoteResult>(result.data);
                               navigateToHref(footnoteResult.href, true);
                             }
                             requestUpdate();
                           });
  }
}

unsigned long EpubReaderActivity::confirmLongPressThreshold() const {
  switch (SETTINGS.longPressMenuFunction) {
    case CrossPointSettings::LP_MENU_BOOKMARK:
    case CrossPointSettings::LP_MENU_DICTIONARY:
      return bookmarkToggleMs;
    case CrossPointSettings::LP_MENU_KOSYNC:
      return KOREADER_STORE.hasCredentials() ? ReaderUtils::GO_HOME_MS : 0;
    case CrossPointSettings::LP_MENU_READER_MENU:
      return bookmarkToggleMs;
    case CrossPointSettings::LP_MENU_DISABLED:
    default:
      return 0;
  }
}

// Runs the user-selected long-press action (held Confirm, or a Home-key hold on
// home-key boards). Returns true when the reader is being replaced.
bool EpubReaderActivity::runLongPressMenuFunction() {
  waitingForConfirmSecondClick = false;
  firstConfirmClickMs = 0UL;
  switch (SETTINGS.longPressMenuFunction) {
    case CrossPointSettings::LP_MENU_BOOKMARK:
      toggleCurrentPageBookmark();
      return false;
    case CrossPointSettings::LP_MENU_KOSYNC:
      if (KOREADER_STORE.hasCredentials()) {
        READING_STATS.noteActivity();
        launchKOReaderSync(SyncLaunchMode::COMPARE);
        return true;
      }
      return false;
    case CrossPointSettings::LP_MENU_DICTIONARY:
      openDictionaryWordSelect();
      return false;
    case CrossPointSettings::LP_MENU_READER_MENU:
      if (usesToolbarMenu() && section) {
        pendingManualTurn = 0;
        openOverlay(Overlay::Toolbar);
      } else {
        openReaderMenu();
      }
      return false;
    case CrossPointSettings::LP_MENU_DISABLED:
    default:
      return false;
  }
}

void EpubReaderActivity::openDictionaryWordSelect() {
  int overlayMarginLeft = 0;
  int overlayMarginTop = 0;
  auto page = loadCurrentPageForOverlay(overlayMarginLeft, overlayMarginTop);
  if (!page) {
    requestUpdate();
    return;
  }
  READING_STATS.noteActivity();
  startActivityForResult(
      std::make_unique<DictionaryWordSelectActivity>(renderer, mappedInput, page, SETTINGS.getReaderFontId(),
                                                     overlayMarginLeft, overlayMarginTop),
      [this](const ActivityResult&) {
        READING_STATS.resumeSession();
        ReaderUtils::requestReaderUiTransitionRefresh(renderer);
        requestUpdate();
      });
}

// Fork page-mark toggle (long Confirm press / toolbar), with the achievement hook.
void EpubReaderActivity::toggleCurrentPageBookmark() {
  if (!section || section->currentPage < 0 || section->currentPage >= section->pageCount) {
    return;
  }
  READING_STATS.noteActivity();
  const uint16_t spineIndex = static_cast<uint16_t>(currentSpineIndex);
  const uint16_t pageNumber = static_cast<uint16_t>(section->currentPage);
  const auto visibleOffset = section->getVisibleTextOffsetForPage(pageNumber);
  const bool wasBookmarked = bookmarkStore.has(spineIndex, pageNumber, visibleOffset);
  const std::string snippet = wasBookmarked ? "" : extractBookmarkSnippet(*section);
  const bool addedBookmark = bookmarkStore.toggle(spineIndex, pageNumber, snippet, visibleOffset);
  bookmarkStore.save();
  if (addedBookmark && epub && !READING_STATS.shouldIgnorePath(epub->getPath())) {
    ACHIEVEMENTS.recordBookmarkAdded();
  }
  const bool showedAchievement = showPendingAchievementPopups(renderer);
  if (!showedAchievement) {
    GUI.drawPopup(renderer, addedBookmark ? tr(STR_PAGE_MARK_ADDED) : tr(STR_PAGE_MARK_REMOVED));
    renderer.displayBuffer();
    delay(500);
  }
  updateBookmarkFlag();
  requestUpdate();
}

void EpubReaderActivity::updateBookmarkFlag() {
  if (!section || !epub || bookmarkStore.isEmpty() || section->currentPage < 0 ||
      section->currentPage >= section->pageCount) {
    currentPageBookmarked = false;
    return;
  }
  const uint16_t pageNumber = static_cast<uint16_t>(section->currentPage);
  currentPageBookmarked = bookmarkStore.has(static_cast<uint16_t>(currentSpineIndex), pageNumber,
                                            section->getVisibleTextOffsetForPage(pageNumber));
}

void EpubReaderActivity::openReaderMenu() {
  pendingManualTurn = 0;
  waitingForConfirmSecondClick = false;
  firstConfirmClickMs = 0UL;
  READING_STATS.noteActivity();
  int currentPage = 0;
  int totalPages = 0;
  float bookProgress = 0.0f;
  {
    RenderLock lock(*this);
    currentPage = section ? section->currentPage + 1 : 0;
    totalPages = section ? section->estimatedTotalPages() : 0;
    if (epub->getBookSize() > 0 && section && section->estimatedTotalPages() > 0) {
      const float chapterProgress =
          static_cast<float>(section->currentPage) / static_cast<float>(section->estimatedTotalPages());
      bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f;
    }
  }
  const int bookProgressPercent = clampPercent(static_cast<int>(bookProgress + 0.5f));
  // Night mode / frontlight toggle in place inside the menu; the snapshot lets the
  // reader notice the dark-mode flip when the menu closes.
  const auto before = captureReaderSettingsSnapshot();
  ReaderUtils::requestReaderUiTransitionRefresh(renderer);
  startActivityForResult(std::make_unique<EpubReaderMenuActivity>(renderer, mappedInput, epub->getTitle(), currentPage,
                                                                  totalPages, bookProgressPercent, SETTINGS.orientation,
                                                                  !currentPageFootnotes.empty()),
                         [this, before](const ActivityResult& result) {
                           READING_STATS.resumeSession();
                           // Always apply orientation change even if the menu was cancelled
                           const auto& menu = std::get<MenuResult>(result.data);
                           applyOrientation(menu.orientation);
                           toggleAutoPageTurn(menu.pageTurnOption);
                           applyReaderSettingsChanges(before);
                           if (!result.isCancelled) {
                             onReaderMenuConfirm(static_cast<EpubReaderMenuActivity::MenuAction>(menu.action));
                           }
                         });
}

void EpubReaderActivity::loop() {
  if (!epub) {
    // Should never happen
    finish();
    return;
  }

  READING_STATS.tickActiveSession();
  const unsigned long nowMs = millis();

  // Someone else turned the screen while this reader was stacked (the control
  // center's orientation tile). Reflow before the next render, or the page
  // would be drawn with a layout built for the previous frame size.
  if (appliedOrientation != SETTINGS.orientation) {
    applyOrientation(SETTINGS.orientation);
    requestUpdate();
    return;
  }

  // Idle prewarm (upstream): once the page has settled, batch-load the next
  // page's glyphs so the coming page turn skips its SD font pass.
  constexpr unsigned long IDLE_PREWARM_DEBOUNCE_MS = 400;
  if (section && !section->isBuilding() && overlay == Overlay::None && !RenderLock::peek() &&
      renderer.hasFrameBuffer() && lastRenderCompleteMs != 0 &&
      nowMs - lastRenderCompleteMs > IDLE_PREWARM_DEBOUNCE_MS && ESP.getFreeHeap() > RENDER_MIN_FREE_HEAP &&
      ESP.getMaxAllocHeap() > BACKGROUND_BUILD_MIN_MAX_ALLOC &&
      (idlePrewarmSpine != currentSpineIndex || idlePrewarmPage != section->currentPage)) {
    RenderLock lock(*this);
    if (section && !section->isBuilding() &&
        (idlePrewarmSpine != currentSpineIndex || idlePrewarmPage != section->currentPage)) {
      idlePrewarmSpine = currentSpineIndex;
      idlePrewarmPage = section->currentPage;
      const int nextPage = section->currentPage + 1;
      if (nextPage < static_cast<int>(section->pageCount)) {
        if (const auto p = section->loadPage(nextPage)) {
          if (auto* fcm = renderer.getFontCacheManager()) {
            const auto t0 = millis();
            auto scope = fcm->createPrewarmScope();
            p->recordFontUsage(*fcm, SETTINGS.getReaderFontId(), SETTINGS.bionicReading);
            scope.endScanAndPrewarm();
            LOG_DBG("ERS", "Idle prewarm: page %d in %lums", nextPage, millis() - t0);
          }
        }
      }
    }
  }

  if (section && !section->isBuilding() && section->isPartial() && !partialRebuildStartFailed &&
      buildViewportWidth > 0 &&
      section->currentPage + PARTIAL_REBUILD_START_MARGIN >= static_cast<int>(section->pageCount) &&
      !RenderLock::peek()) {
    RenderLock buildLock(*this);
    if (!section->startBuild(makeRenderSpec(buildViewportWidth, buildViewportHeight))) {
      partialRebuildStartFailed = true;
      LOG_ERR("ERS", "Failed to resume partial section build");
    }
  }

  if (section && section->isBuilding() && !RenderLock::peek() &&
      (section->isPartial() || static_cast<int>(section->pageCount) < section->currentPage + BUILD_WINDOW_AHEAD) &&
      buildTickHeapGate()) {
    RenderLock buildLock(*this);
    if (section && section->isBuilding() && buildTickHeapGate()) {
      if (!section->buildSomeMore(BACKGROUND_BUILD_PAGES_PER_TICK)) {
        LOG_ERR("ERS", "Background section build failed");
        // Preserve the visible page across the rebuild. nextPageNumber is normally
        // zero during ordinary page turns, which previously made a failed background
        // build reopen the chapter at its first cached page.
        nextPageNumber = section->currentPage;
        section.reset();
        requestUpdate();
      } else if (section->isBuildComplete() && applyDeferredReposition()) {
        requestUpdate();
      }
    }
  }

  if (waitingForConfirmSecondClick && ReaderUtils::hasNonConfirmNavigationInput(mappedInput)) {
    waitingForConfirmSecondClick = false;
    firstConfirmClickMs = 0UL;
  }

  const bool atEndOfBook = isAtEndOfBook();
  clearEndOfBookOptionsIfNeeded();

  // Upstream: optionally drop a finished book from Recents (and put it back
  // when the reader steps away from the end screen).
  if (SETTINGS.removeReadBooksFromRecents) {
    if (atEndOfBook && !recentsEntryRemoved) {
      recentsEntryRemoved = RECENT_BOOKS.removeByPath(epub->getPath());
    } else if (!atEndOfBook && recentsEntryRemoved) {
      RECENT_BOOKS.addBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), epub->getThumbBmpPath(), stableBookId);
      recentsEntryRemoved = false;
    }
  }

  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);

  // The toolbar reader menu owns all input while shown, ahead of the automatic page turn
  // below: the More panel's rate popup switches automatic turning on and leaves the panel
  // open, so the timer must neither flip the page under it nor eat the panel's next
  // Confirm/Back release.
  if (overlay != Overlay::None) {
    if (usesToolbarMenu()) {
      // Hold the interval at zero elapsed so closing the panel starts a fresh one.
      lastPageTurnTime = millis();
      handleOverlayInput();
      return;
    }
    // The style was switched off while an overlay was up (Settings reached via
    // the More panel); fall back to the clean page.
    overlay = Overlay::None;
    discardOverlayPage();
    requestUpdate();
    return;
  }

  if (automaticPageTurnActive) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        ReaderUtils::isTouchMenuGesture(renderer, mappedInput)) {
      automaticPageTurnActive = false;
      waitingForConfirmSecondClick = false;
      firstConfirmClickMs = 0UL;
      // updates chapter title space to indicate page turn disabled
      requestUpdate();
      return;
    }

    if (!section) {
      requestUpdate();
      return;
    }

    // Skips page turn if renderingMutex is busy
    if (RenderLock::peek()) {
      lastPageTurnTime = millis();
      return;
    }

    if ((millis() - lastPageTurnTime) >= pageTurnDuration) {
      pageTurn(true);
      return;
    }
  }

  // While the end-of-book suggestion menu is up it owns Confirm/Back/navigation, so it
  // gets this tick's input first and the long-press shortcuts below stay inert behind it.
  if (handleEndOfBookMenu()) {
    return;
  }
  const bool endOfBookMenuOpen = endOfBookMenuActive();

  if (ReaderUtils::shouldToggleStatusBar(mappedInput)) {
    toggleTemporaryStatusBar();
    return;
  }

  // Long Confirm press: the configured long-press function (page mark by default
  // in the fork). wasLongPressed() suppresses the release that follows it, so it
  // is left unpolled while the end-of-book menu owns Confirm.
  const unsigned long confirmHoldMs = confirmLongPressThreshold();
  if (!endOfBookMenuOpen && confirmHoldMs != 0 &&
      mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, confirmHoldMs)) {
    if (runLongPressMenuFunction()) return;
    return;
  }

  // Home-key boards have no front Confirm button, so a Home-key hold runs the
  // same user-selected long-press action. The SDK emits this event once per
  // hold and suppresses the short Home tap for the same contact.
  if (!endOfBookMenuOpen && mappedInput.wasHomeKeyHold()) {
    runLongPressMenuFunction();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (ReaderUtils::registerConfirmDoubleClick(waitingForConfirmSecondClick, firstConfirmClickMs, nowMs)) {
      requestCurrentPageFullRefresh();
      return;
    }
  }

  // Link taps take priority over the reader-menu and page-turn zones (upstream #3296).
  if (!atEndOfBook && !currentPageLinks.empty() && SETTINGS.touchReaderControls && mappedInput.hasTouch()) {
    int touchX = 0;
    int touchY = 0;
    if (mappedInput.wasScreenTapped(touchX, touchY)) {
      const auto* link = EpubReaderUtils::linkAtPoint(currentPageLinks, touchX, touchY, currentPageLinkMarginLeft,
                                                      currentPageLinkMarginTop);
      if (link) {
        READING_STATS.noteActivity();
        navigateToHref(link->href, true);
        return;
      }
    }
  }

  // Enter reader menu activity: a single Confirm click once the double-click
  // window has passed, or the touch menu gesture (center tap / edge swipe).
  const bool menuByClick =
      ReaderUtils::hasPendingConfirmSingleClickExpired(waitingForConfirmSecondClick, firstConfirmClickMs, nowMs);
  if (menuByClick || (!atEndOfBook && ReaderUtils::isTouchMenuGesture(renderer, mappedInput))) {
    waitingForConfirmSecondClick = false;
    firstConfirmClickMs = 0UL;
    // Toolbar style: the page is on screen and in the framebuffer, so paint the
    // toolbar over it (one refresh) instead of pushing a full-screen menu.
    if (usesToolbarMenu() && section && !atEndOfBook) {
      READING_STATS.noteActivity();
      pendingManualTurn = 0;
      openOverlay(Overlay::Toolbar);
    } else {
      openReaderMenu();
    }
    return;
  }

  if (handleBackNavigation()) {
    return;
  }

  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::FOOTNOTES &&
      mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      !mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    openFootnotesFromPowerButton();
    return;
  }

  // A turn that arrived while the render task still owned the page (or too soon
  // after the previous one) is replayed here once the guard clears (upstream).
  constexpr unsigned long kMinManualTurnGapMs = 200;
  const bool turnGuardActive = RenderLock::peek() || (millis() - lastPageTurnTime) < kMinManualTurnGapMs;
  if (pendingManualTurn != 0 && !turnGuardActive) {
    if (!section) {
      pendingManualTurn = 0;
      return;
    }
    const bool forward = pendingManualTurn > 0;
    pendingManualTurn = 0;
    pageTurn(forward);
    return;
  }

  auto [prevTriggered, nextTriggered, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);
  prevTriggered = prevTriggered || touch.prev;
  nextTriggered = nextTriggered || touch.next;
  if (!prevTriggered && !nextTriggered) {
    return;
  }
  if (fromTilt) {
    waitingForConfirmSecondClick = false;
    firstConfirmClickMs = 0UL;
  }

  // At end of the book, forward button goes home and back button returns to last page
  if (atEndOfBook) {
    if (endOfBookMenuOpen) return;
    if (nextTriggered) {
      markStatsCompletedAtEnd(*epub, currentSpineIndex);
      if (tryAutoPushOnClose()) {
        return;
      }
      exitReaderAfterOptionalCompletedMove();
    } else {
      returnFromEndOfBook();
      requestUpdate();
    }
    return;
  }

  // Don't skip chapter after screenshot (Power + Down)
  if (mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    return;
  }

  const unsigned long heldMs = (touch.prev || touch.next) ? touch.heldMs : mappedInput.getHeldTime();
  const bool longPress = !fromTilt && heldMs >= ReaderUtils::SKIP_HOLD_MS;

  if (longPress && SETTINGS.longPressButtonBehavior == CrossPointSettings::LONG_PRESS_CHAPTER_SKIP) {
    skipChapter(nextTriggered);
    return;
  }

  if (longPress && SETTINGS.longPressButtonBehavior == CrossPointSettings::LONG_PRESS_ORIENTATION_CHANGE) {
    const uint8_t newOrientation = nextTriggered ? (SETTINGS.orientation - 1 + CrossPointSettings::ORIENTATION_COUNT) %
                                                       CrossPointSettings::ORIENTATION_COUNT
                                                 : (SETTINGS.orientation + 1) % CrossPointSettings::ORIENTATION_COUNT;
    applyOrientation(newOrientation);
    requestUpdate();
    return;
  }

  // No current section, attempt to rerender the book
  if (!section) {
    requestUpdate();
    return;
  }

  if (turnGuardActive) {
    pendingManualTurn = prevTriggered ? -1 : 1;
    return;
  }

  if (prevTriggered) {
    pageTurn(false);
  } else {
    pageTurn(true);
  }
}

void EpubReaderActivity::skipChapter(const bool forward) {
  READING_STATS.noteActivity();
  lastPageTurnTime = millis();
  invalidateCurrentOverlayPageCache();

  if (!forward && section && section->currentPage > 0) {
    section->currentPage = 0;
    nextPageNumber = 0;
    sessionProgressTouched = true;
    requestUpdate();
    return;
  }

  if (!forward && currentSpineIndex <= 0) {
    return;
  }

  // We don't want to delete the section mid-render, so grab the semaphore
  {
    RenderLock lock(*this);
    clearDeferredReposition();
    nextPageNumber = 0;
    if (forward) {
      currentSpineIndex++;
    } else if (currentSpineIndex > 0) {
      pendingPageJump = std::numeric_limits<uint16_t>::max();
      currentSpineIndex--;
    }
    sessionProgressTouched = true;
    section.reset();
  }
  requestUpdate();
}

void EpubReaderActivity::requestCurrentPageFullRefresh() {
  READING_STATS.noteActivity();
  pendingForceFullRefresh = true;
  requestUpdate();
}

void EpubReaderActivity::toggleTemporaryStatusBar() {
  READING_STATS.noteActivity();
  statusBarTemporarilyHidden = !statusBarTemporarilyHidden;
  invalidateCurrentOverlayPageCache();
  RenderLock lock(*this);
  rememberCurrentLayoutPosition();
  section.reset();
  pendingForceFullRefresh = true;
  requestUpdate();
}

void EpubReaderActivity::cacheCurrentPageForOverlay(const std::shared_ptr<Page>& page, const int marginLeft,
                                                    const int marginTop) {
  if (!page || !section || page->hasImages()) {
    invalidateCurrentOverlayPageCache();
    return;
  }

  currentOverlayPageCache = page;
  currentOverlayPageSpineIndex = currentSpineIndex;
  currentOverlayPageNumber = section->currentPage;
  currentOverlayPageMarginLeft = marginLeft;
  currentOverlayPageMarginTop = marginTop;
}

void EpubReaderActivity::invalidateCurrentOverlayPageCache() {
  currentOverlayPageCache.reset();
  currentOverlayPageSpineIndex = -1;
  currentOverlayPageNumber = -1;
  currentOverlayPageMarginLeft = 0;
  currentOverlayPageMarginTop = 0;
}

std::shared_ptr<Page> EpubReaderActivity::loadCurrentPageForOverlay(int& outMarginLeft, int& outMarginTop) {
  outMarginLeft = 0;
  outMarginTop = 0;
  if (!section || section->currentPage < 0 || section->currentPage >= section->pageCount) {
    return nullptr;
  }

  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += SETTINGS.screenMargin;
  orientedMarginLeft += SETTINGS.screenMargin;
  outMarginLeft = orientedMarginLeft;
  outMarginTop = orientedMarginTop;

  if (currentOverlayPageCache && currentOverlayPageSpineIndex == currentSpineIndex &&
      currentOverlayPageNumber == section->currentPage && currentOverlayPageMarginLeft == orientedMarginLeft &&
      currentOverlayPageMarginTop == orientedMarginTop) {
    return currentOverlayPageCache;
  }

  auto page = section->loadPage(section->currentPage);
  if (!page) {
    return nullptr;
  }
  auto sharedPage = std::shared_ptr<Page>(std::move(page));
  cacheCurrentPageForOverlay(sharedPage, orientedMarginLeft, orientedMarginTop);
  return sharedPage;
}

void EpubReaderActivity::saveCurrentPageBookmark() {
  if (!section || section->currentPage < 0 || section->currentPage >= section->pageCount) {
    requestUpdate();
    return;
  }

  const uint16_t spineIndex = static_cast<uint16_t>(currentSpineIndex);
  const uint16_t pageNumber = static_cast<uint16_t>(section->currentPage);
  const auto visibleOffset = section->getVisibleTextOffsetForPage(pageNumber);
  if (bookmarkStore.has(spineIndex, pageNumber, visibleOffset)) {
    GUI.drawPopup(renderer, tr(STR_PAGE_MARK_ALREADY_SAVED));
    renderer.displayBuffer();
    delay(500);
    requestUpdate();
    return;
  }

  const std::string snippet = extractBookmarkSnippet(*section);
  const bool addedBookmark = bookmarkStore.toggle(spineIndex, pageNumber, snippet, visibleOffset);
  bookmarkStore.save();
  if (addedBookmark && epub && !READING_STATS.shouldIgnorePath(epub->getPath())) {
    ACHIEVEMENTS.recordBookmarkAdded();
  }

  const bool showedAchievement = showPendingAchievementPopups(renderer);
  if (!showedAchievement) {
    GUI.drawPopup(renderer, tr(STR_PAGE_MARK_ADDED));
    renderer.displayBuffer();
    delay(500);
  }
  updateBookmarkFlag();
  requestUpdate();
}

// Translate an absolute percent into a spine index plus a normalized position
// within that spine so we can jump after the section is loaded.
void EpubReaderActivity::jumpToPercent(int percent) {
  if (!epub) {
    return;
  }
  clearDeferredReposition();
  invalidateCurrentOverlayPageCache();

  // BookMetadataCache uses one seek-based file handle for spine lookups. Keep
  // the complete calculation serialized with render/status-bar metadata reads.
  RenderLock lock(*this);

  const size_t bookSize = epub->getBookSize();
  if (bookSize == 0) {
    return;
  }

  // Normalize input to 0-100 to avoid invalid jumps.
  percent = clampPercent(percent);

  // Convert percent into a byte-like absolute position across the spine sizes.
  // Use an overflow-safe computation: (bookSize / 100) * percent + (bookSize % 100) * percent / 100
  size_t targetSize =
      (bookSize / 100) * static_cast<size_t>(percent) + (bookSize % 100) * static_cast<size_t>(percent) / 100;
  if (percent >= 100) {
    // Ensure the final percent lands inside the last spine item.
    targetSize = bookSize - 1;
  }

  const int spineCount = epub->getSpineItemsCount();
  if (spineCount == 0) {
    return;
  }

  int targetSpineIndex = spineCount - 1;
  size_t prevCumulative = 0;

  for (int i = 0; i < spineCount; i++) {
    const size_t cumulative = epub->getCumulativeSpineItemSize(i);
    if (targetSize <= cumulative) {
      // Found the spine item containing the absolute position.
      targetSpineIndex = i;
      prevCumulative = (i > 0) ? epub->getCumulativeSpineItemSize(i - 1) : 0;
      break;
    }
  }

  const size_t cumulative = epub->getCumulativeSpineItemSize(targetSpineIndex);
  const size_t spineSize = (cumulative > prevCumulative) ? (cumulative - prevCumulative) : 0;
  // Store a normalized position within the spine so it can be applied once loaded.
  pendingSpineProgress =
      (spineSize == 0) ? 0.0f : static_cast<float>(targetSize - prevCumulative) / static_cast<float>(spineSize);
  pendingSpineProgress = std::clamp(pendingSpineProgress, 0.0f, 1.0f);

  // Reset state so render() reloads and repositions on the target spine.
  currentSpineIndex = targetSpineIndex;
  nextPageNumber = 0;
  pendingPercentJump = true;
  sessionProgressTouched = true;
  section.reset();
}

EpubReaderActivity::ReaderSettingsSnapshot EpubReaderActivity::captureReaderSettingsSnapshot() const {
  return ReaderSettingsSnapshot{
      SETTINGS.darkMode,
      SETTINGS.fadingFix,
      SETTINGS.refreshFrequency,
      SETTINGS.fontFamily,
      SETTINGS.fontPointSize,
      SETTINGS.lineSpacing,
      SETTINGS.screenMargin,
      SETTINGS.paragraphAlignment,
      SETTINGS.embeddedStyle,
      SETTINGS.hyphenationEnabled,
      SETTINGS.bionicReading,
      SETTINGS.orientation,
      SETTINGS.extraParagraphSpacing,
      SETTINGS.forceParagraphIndents,
      SETTINGS.textAntiAliasing,
      SETTINGS.textDarkness,
      SETTINGS.readerRefreshMode,
      SETTINGS.imageRendering,
      SETTINGS.sdFontFamilyName,
  };
}

void EpubReaderActivity::applyReaderSettingsChanges(const ReaderSettingsSnapshot& before) {
  const bool fontChanged = before.fontFamily != SETTINGS.fontFamily || before.fontPointSize != SETTINGS.fontPointSize ||
                           before.sdFontFamilyName != SETTINGS.sdFontFamilyName;
  const bool bionicNormalLayoutChanged = (before.bionicReading == CrossPointSettings::BIONIC_READING_NORMAL) !=
                                         (SETTINGS.bionicReading == CrossPointSettings::BIONIC_READING_NORMAL);
  const bool paginationChanged =
      fontChanged || before.lineSpacing != SETTINGS.lineSpacing || before.screenMargin != SETTINGS.screenMargin ||
      before.paragraphAlignment != SETTINGS.paragraphAlignment || before.embeddedStyle != SETTINGS.embeddedStyle ||
      before.hyphenationEnabled != SETTINGS.hyphenationEnabled ||
      before.extraParagraphSpacing != SETTINGS.extraParagraphSpacing ||
      before.forceParagraphIndents != SETTINGS.forceParagraphIndents || bionicNormalLayoutChanged ||
      before.imageRendering != SETTINGS.imageRendering;
  const bool orientationChanged = before.orientation != SETTINGS.orientation;
  const bool refreshPolicyChanged =
      before.refreshFrequency != SETTINGS.refreshFrequency || before.readerRefreshMode != SETTINGS.readerRefreshMode;
  const bool renderOnlyChanged = before.darkMode != SETTINGS.darkMode || before.fadingFix != SETTINGS.fadingFix ||
                                 before.bionicReading != SETTINGS.bionicReading ||
                                 before.textAntiAliasing != SETTINGS.textAntiAliasing ||
                                 before.textDarkness != SETTINGS.textDarkness || refreshPolicyChanged;
  const bool displayModeChanged = before.darkMode != SETTINGS.darkMode;
  const bool needsFullRefresh = orientationChanged || paginationChanged || displayModeChanged;

  if (!(paginationChanged || orientationChanged || renderOnlyChanged)) {
    return;
  }

  invalidateCurrentOverlayPageCache();
  discardOverlayPage();

  if (fontChanged) {
    ensureSdFontLoaded();
  }

  renderer.setFadingFix(SETTINGS.fadingFix);
  renderer.setDarkMode(SETTINGS.darkMode);
  renderer.setTextDarkness(SETTINGS.textDarkness);
  if (needsFullRefresh) {
    renderer.requestNextFullRefresh();
  }

  if (orientationChanged || paginationChanged) {
    RenderLock lock(*this);
    rememberCurrentLayoutPosition();
    if (orientationChanged) {
      ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
      appliedOrientation = SETTINGS.orientation;
    }
    section.reset();
  }

  if (refreshPolicyChanged) {
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  }

  pendingForceFullRefresh = needsFullRefresh;
  requestUpdate(true);
}

void EpubReaderActivity::onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action) {
  switch (action) {
    case EpubReaderMenuActivity::MenuAction::READER_SETTINGS: {
      const auto before = captureReaderSettingsSnapshot();
      READING_STATS.noteActivity();
      startActivityForResult(std::make_unique<ReaderQuickSettingsActivity>(renderer, mappedInput),
                             [this, before](const ActivityResult&) {
                               applyReaderSettingsChanges(before);
                               READING_STATS.resumeSession();
                             });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER: {
      const int spineIdx = currentSpineIndex;
      READING_STATS.noteActivity();
      // Release the section while the chapter list is up (upstream): picking a
      // chapter resets it anyway, and its tens-of-KB footprint is the difference
      // between the chapter list holding its CJK glyph arena and re-reading
      // glyphs from SD on every row step. Cancel restores via the cached position.
      {
        RenderLock lock(*this);
        if (section) {
          rememberCurrentLayoutPosition();
          pendingPaginationReposition = false;
        }
        section.reset();
        invalidateCurrentOverlayPageCache();
      }
      startActivityForResult(
          std::make_unique<EpubReaderChapterSelectionActivity>(renderer, mappedInput, epub, spineIdx),
          [this](const ActivityResult& result) {
            READING_STATS.resumeSession();
            if (!result.isCancelled) {
              const auto& chapterResult = std::get<ChapterResult>(result.data);
              RenderLock lock(*this);

              clearDeferredReposition();
              currentSpineIndex = chapterResult.spineIndex;

              // If anchor is not empty, it will be used later to calculate the page number.
              pendingAnchor = chapterResult.anchor;

              // Otherwise page 0 will be used.
              nextPageNumber = 0;
              sessionProgressTouched = true;
              section.reset();
            }
            requestUpdate();
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::FOOTNOTES: {
      READING_STATS.noteActivity();
      startActivityForResult(std::make_unique<EpubReaderFootnotesActivity>(renderer, mappedInput, currentPageFootnotes),
                             [this](const ActivityResult& result) {
                               READING_STATS.resumeSession();
                               if (!result.isCancelled) {
                                 const auto& footnoteResult = std::get<FootnoteResult>(result.data);
                                 navigateToHref(footnoteResult.href, true);
                               }
                               requestUpdate();
                             });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::LOOK_UP_WORD: {
      openDictionaryWordSelect();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::LOOKUP_HISTORY: {
      int overlayMarginLeft = 0;
      int overlayMarginTop = 0;
      auto page = loadCurrentPageForOverlay(overlayMarginLeft, overlayMarginTop);
      if (!page) {
        requestUpdate();
        break;
      }
      READING_STATS.noteActivity();
      startActivityForResult(
          std::make_unique<DictionaryHistoryActivity>(renderer, mappedInput, page, SETTINGS.getReaderFontId(),
                                                      overlayMarginLeft, overlayMarginTop),
          [this](const ActivityResult&) {
            READING_STATS.resumeSession();
            ReaderUtils::requestReaderUiTransitionRefresh(renderer);
            requestUpdate();
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DICTIONARY: {
      READING_STATS.noteActivity();
      startActivityForResult(std::make_unique<DictionaryActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) {
                               READING_STATS.resumeSession();
                               ReaderUtils::requestReaderUiTransitionRefresh(renderer);
                               requestUpdate();
                             });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::VIEW_HIGHLIGHTS: {
      READING_STATS.noteActivity();
      startActivityForResult(
          std::make_unique<BookmarksActivity>(renderer, mappedInput, bookmarkStore.getAll(), epub, "",
                                              [this](const BookmarkStore::Bookmark& bookmark) {
                                                const bool removed = bookmarkStore.removeItem(bookmark);
                                                if (removed) {
                                                  bookmarkStore.save();
                                                }
                                                return removed;
                                              }),
          [this](const ActivityResult& result) {
            READING_STATS.resumeSession();
            if (!result.isCancelled) {
              const auto& bookmark = std::get<BookmarkResult>(result.data);
              const bool needsAnchorJump = bookmark.hasVisibleTextOffset;
              if (needsAnchorJump && currentSpineIndex == bookmark.spineIndex && section) {
                RenderLock lock(*this);
                if (const auto page = section->getPageForVisibleTextOffset(bookmark.visibleTextOffset)) {
                  clearDeferredReposition();
                  section->currentPage = *page;
                  nextPageNumber = *page;
                  sessionProgressTouched = true;
                  invalidateCurrentOverlayPageCache();
                  requestUpdate();
                  return;
                }
              }
              if (needsAnchorJump || currentSpineIndex != bookmark.spineIndex || !section ||
                  section->currentPage != static_cast<int>(bookmark.page)) {
                RenderLock lock(*this);
                clearDeferredReposition();
                currentSpineIndex = bookmark.spineIndex;
                nextPageNumber = static_cast<int>(bookmark.page);
                pendingVisibleTextOffset =
                    needsAnchorJump ? std::optional<uint32_t>(bookmark.visibleTextOffset) : std::nullopt;
                sessionProgressTouched = true;
                section.reset();
                requestUpdate();
                return;
              }
            }
            updateBookmarkFlag();
            requestUpdate();
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SAVE_PAGE_MARK: {
      READING_STATS.noteActivity();
      saveCurrentPageBookmark();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::HIGHLIGHT_TEXT: {
      int overlayMarginLeft = 0;
      int overlayMarginTop = 0;
      auto page = loadCurrentPageForOverlay(overlayMarginLeft, overlayMarginTop);
      if (!page || !section) {
        requestUpdate();
        break;
      }
      const uint16_t selectionSpine = static_cast<uint16_t>(currentSpineIndex);
      const uint16_t selectionPage = static_cast<uint16_t>(section->currentPage);
      const uint32_t selectionVisibleOffset = page->visibleTextOffset;
      READING_STATS.noteActivity();
      startActivityForResult(
          std::make_unique<DictionaryWordSelectActivity>(renderer, mappedInput, page, SETTINGS.getReaderFontId(),
                                                         overlayMarginLeft, overlayMarginTop, true),
          [this, selectionSpine, selectionPage, selectionVisibleOffset](const ActivityResult& result) {
            READING_STATS.resumeSession();
            if (!result.isCancelled) {
              const auto& highlight = std::get<HighlightResult>(result.data);
              const bool saved =
                  bookmarkStore.addTextHighlight(selectionSpine, selectionPage, selectionPage, highlight.startWordIndex,
                                                 highlight.endWordIndex, highlight.text, selectionVisibleOffset);
              if (saved) {
                bookmarkStore.save();
                if (epub && !READING_STATS.shouldIgnorePath(epub->getPath())) {
                  ACHIEVEMENTS.recordBookmarkAdded();
                }
              }
              GUI.drawPopup(renderer, saved ? tr(STR_HIGHLIGHT_SAVED) : tr(STR_HIGHLIGHT_FAILED));
              renderer.displayBuffer();
              delay(600);
            }
            requestUpdate();
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::NIGHT_MODE:
    case EpubReaderMenuActivity::MenuAction::FRONTLIGHT:
      // Handled in place by EpubReaderMenuActivity / the toolbar's More panel;
      // the dark-mode flip reaches the page through the settings snapshot.
      requestUpdate();
      break;
    case EpubReaderMenuActivity::MenuAction::GO_TO_PERCENT: {
      float bookProgress = 0.0f;
      {
        RenderLock lock(*this);
        if (epub && epub->getBookSize() > 0 && section && section->pageCount > 0) {
          const float chapterProgress =
              static_cast<float>(section->currentPage) / static_cast<float>(section->estimatedTotalPages());
          bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f;
        }
      }
      const int initialPercent = clampPercent(static_cast<int>(bookProgress + 0.5f));
      READING_STATS.noteActivity();
      startActivityForResult(
          std::make_unique<EpubReaderPercentSelectionActivity>(renderer, mappedInput, initialPercent),
          [this](const ActivityResult& result) {
            READING_STATS.resumeSession();
            if (!result.isCancelled) {
              jumpToPercent(std::get<PercentResult>(result.data).percent);
            }
            requestUpdate();
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DISPLAY_QR: {
      if (section && section->currentPage >= 0 && section->currentPage < section->pageCount) {
        std::string fullText = section->getTextFromSectionFile();
        if (!fullText.empty()) {
          READING_STATS.noteActivity();
          startActivityForResult(std::make_unique<QrDisplayActivity>(renderer, mappedInput, fullText),
                                 [this](const ActivityResult&) {
                                   READING_STATS.resumeSession();
                                   requestUpdate();
                                 });
          break;
        }
      }
      // If no text or page loading failed, just close menu
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::GO_HOME: {
      if (tryAutoPushOnClose()) {
        return;
      }
      exitReaderAfterOptionalCompletedMove();
      return;
    }
    case EpubReaderMenuActivity::MenuAction::MARK_AS_FINISHED: {
      const std::string title = epub ? epub->getTitle() : "";
      READING_STATS.noteActivity();
      startActivityForResult(
          std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_MARK_AS_FINISHED_CONFIRM), title),
          [this](const ActivityResult& result) {
            READING_STATS.resumeSession();
            if (!result.isCancelled) {
              markCurrentBookAsFinished();
            } else {
              requestUpdate();
            }
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DELETE_CACHE: {
      {
        RenderLock lock(*this);
        if (epub && section) {
          uint16_t backupSpine = currentSpineIndex;
          uint16_t backupPage = section->currentPage;
          uint16_t backupPageCount = section->pageCount;
          section.reset();
          epub->clearCache();
          epub->setupCacheDir();
          if (!saveProgress(backupSpine, backupPage, backupPageCount)) {
            LOG_ERR("ERS", "Failed to save progress before cache clear");
          }
          if (!bookmarkStore.isEmpty()) {
            bookmarkStore.markDirty();
            bookmarkStore.save();
          }
        }
      }
      exitReaderAfterOptionalCompletedMove();
      return;
    }
    case EpubReaderMenuActivity::MenuAction::SCREENSHOT: {
      {
        RenderLock lock(*this);
        pendingScreenshot = true;
      }
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SYNC: {
      if (KOREADER_STORE.hasCredentials()) {
        READING_STATS.noteActivity();
        launchKOReaderSync(SyncLaunchMode::COMPARE);
      } else {
        requestUpdate();
      }
      break;
    }
  }
}

void EpubReaderActivity::applyOrientation(const uint8_t orientation) {
  // Also runs when SETTINGS already holds the new value but this layout was
  // built for the old one -- that is what an external change looks like here.
  if (SETTINGS.orientation == orientation && appliedOrientation == orientation) {
    return;
  }

  invalidateCurrentOverlayPageCache();
  discardOverlayPage();

  // Preserve current reading position so we can restore after reflow.
  {
    RenderLock lock(*this);
    rememberCurrentLayoutPosition();

    // Persist the selection so the reader keeps the new orientation on next launch.
    if (SETTINGS.orientation != orientation) {
      SETTINGS.orientation = orientation;
      SETTINGS.saveToFile();
    }

    // Update renderer orientation to match the new logical coordinate system.
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    appliedOrientation = orientation;

    // Reset section to force re-layout in the new orientation.
    section.reset();
  }
}

void EpubReaderActivity::toggleAutoPageTurn(const uint8_t selectedPageTurnOption) {
  if (selectedPageTurnOption == 0 || selectedPageTurnOption >= std::size(PAGE_TURN_RATES)) {
    automaticPageTurnActive = false;
    return;
  }

  lastPageTurnTime = millis();
  // calculates page turn duration by dividing by number of pages
  pageTurnDuration = (1UL * 60 * 1000) / PAGE_TURN_RATES[selectedPageTurnOption];
  automaticPageTurnActive = true;

  const uint8_t statusBarHeight = statusBarTemporarilyHidden ? 0 : UITheme::getInstance().getStatusBarHeight();
  // resets cached section so that space is reserved for auto page turn indicator when None or progress bar only
  if (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight()) {
    // Preserve current reading position so we can restore after reflow.
    RenderLock lock(*this);
    rememberCurrentLayoutPosition();
    section.reset();
  }
}

bool EpubReaderActivity::tryAutoPushOnClose() {
  if (!SETTINGS.koSyncAutoPushOnClose || !KOREADER_STORE.hasCredentials() || !epub || !section) {
    return false;
  }

  const int currentPage = section->currentPage;
  const bool positionChanged =
      sessionProgressTouched || currentSpineIndex != sessionStartSpineIndex || currentPage != sessionStartPage;
  if (!positionChanged) {
    return false;
  }

  LOG_DBG("ERS", "Auto-push KOReader sync before closing: spine=%d page=%d", currentSpineIndex, currentPage);
  launchKOReaderSync(SyncLaunchMode::AUTO_PUSH);
  return true;
}

std::string EpubReaderActivity::moveCompletedBookIfEnabled() {
  if (!epub) {
    return "";
  }

  const std::string sourcePath = epub->getPath();
  if (!SETTINGS.moveCompletedBooks) {
    return sourcePath;
  }

  const auto* statsBook = READING_STATS.findBook(!stableBookId.empty() ? stableBookId : sourcePath);
  if (!statsBook || !statsBook->completed) {
    return sourcePath;
  }

  const std::string title = epub->getTitle();
  const std::string author = epub->getAuthor();
  const std::string coverBmpPath = epub->getCoverBmpPath();
  {
    RenderLock lock(*this);
    section.reset();
    ImageBlock::setExtractor(nullptr, nullptr);
    epub.reset();
  }

  const auto moveResult =
      CompletedBookMover::moveCompletedBookIfEnabled(sourcePath, title, author, coverBmpPath, stableBookId);
  return moveResult.moved ? moveResult.destinationPath : sourcePath;
}

void EpubReaderActivity::exitReaderAfterOptionalCompletedMove() {
  const std::string exitPath = moveCompletedBookIfEnabled();
  exitReaderToHomeOrStats(renderer, mappedInput, exitPath);
}

void EpubReaderActivity::markCurrentBookAsFinished() {
  if (!epub) {
    activityManager.goHome();
    return;
  }

  READING_STATS.noteActivity();
  markStatsCompletedAtEnd(*epub, currentSpineIndex);
  exitReaderAfterOptionalCompletedMove();
}

void EpubReaderActivity::pageTurn(bool isForwardTurn) {
  if (!section) {
    nextPageNumber = 0;
    requestUpdate();
    return;
  }

  READING_STATS.noteActivity();
  // Once the user turns a page, that position is authoritative. Do not let a
  // delayed pagination completion restore the session-start page afterward.
  {
    RenderLock lock(*this);
    clearDeferredReposition();
  }
  invalidateCurrentOverlayPageCache();
  const int oldSpineIndex = currentSpineIndex;
  const int oldPage = section ? section->currentPage : nextPageNumber;

  if (isForwardTurn) {
    if (section->currentPage < section->pageCount - 1 || section->isBuilding() || section->isPartial()) {
      section->currentPage++;
    } else {
      // We don't want to delete the section mid-render, so grab the semaphore
      {
        RenderLock lock(*this);
        nextPageNumber = 0;
        currentSpineIndex++;
        section.reset();
      }
    }
  } else {
    if (section->currentPage > 0) {
      section->currentPage--;
    } else if (currentSpineIndex > 0) {
      // We don't want to delete the section mid-render, so grab the semaphore
      {
        RenderLock lock(*this);
        nextPageNumber = UINT16_MAX;
        currentSpineIndex--;
        section.reset();
      }
    }
  }
  const int newPage = section ? section->currentPage : nextPageNumber;
  if (currentSpineIndex != oldSpineIndex || newPage != oldPage) {
    sessionProgressTouched = true;
  }
  lastPageTurnTime = millis();
  requestUpdate();
}

void EpubReaderActivity::renderEndOfBook() {
  markStatsCompletedAtEnd(*epub, currentSpineIndex);
  if (!endOfBookOptions) {
    endOfBookOptions = makeUniqueNoThrow<EndOfBookOptions>(renderer);
    if (!endOfBookOptions) LOG_ERR("ERS", "OOM: EndOfBookOptions");
  }
  renderer.clearScreen();
  if (endOfBookOptions) {
    endOfBookOptions->loadOnce(epub->getPath());
    // Release-publish AFTER loadOnce() so the main task's acquire load can't
    // observe an object whose names/selector are still being populated.
    endOfBookOptionsReady.store(true, std::memory_order_release);
    endOfBookOptions->render(renderer, mappedInput);
  } else {
    renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() * 3 / 8, tr(STR_END_OF_BOOK), true,
                              EpdFontFamily::BOLD);
  }
  renderer.displayBuffer();
  automaticPageTurnActive = false;
}

// TODO: Failure handling
void EpubReaderActivity::render(RenderLock&& lock) {
  currentPageLinks.clear();
  if (!epub) {
    return;
  }

  // edge case handling for sub-zero spine index
  if (currentSpineIndex < 0) {
    currentSpineIndex = 0;
  }
  // based bounds of book, show end of book screen
  if (currentSpineIndex > epub->getSpineItemsCount()) {
    currentSpineIndex = epub->getSpineItemsCount();
  }

  // Show end of book screen
  if (currentSpineIndex == epub->getSpineItemsCount()) {
    discardOverlayPage();
    renderEndOfBook();
    return;
  }

  // Apply screen viewable areas and additional padding
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += SETTINGS.screenMargin;
  orientedMarginLeft += SETTINGS.screenMargin;
  orientedMarginRight += SETTINGS.screenMargin;

  const uint8_t statusBarHeight = statusBarTemporarilyHidden ? 0 : UITheme::getInstance().getStatusBarHeight();

  // reserves space for automatic page turn indicator when no status bar or progress bar only
  if (automaticPageTurnActive &&
      (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight())) {
    orientedMarginBottom +=
        std::max(SETTINGS.screenMargin,
                 static_cast<uint8_t>(statusBarHeight + UITheme::getInstance().getMetrics().statusBarVerticalMargin));
  } else {
    orientedMarginBottom += std::max(SETTINGS.screenMargin, statusBarHeight);
  }

  const uint16_t viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  const uint16_t viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;
  buildViewportWidth = viewportWidth;
  buildViewportHeight = viewportHeight;
  const ReaderRenderSpec renderSpec = makeRenderSpec(viewportWidth, viewportHeight);

  if (!section) {
    const auto filepath = epub->getSpineItem(currentSpineIndex).href;
    LOG_DBG("ERS", "Loading file: %s, index: %d", filepath.c_str(), currentSpineIndex);
    section = std::unique_ptr<Section>(new Section(epub, currentSpineIndex, renderer));
    partialRebuildStartFailed = false;
    const bool cacheLoaded = section->loadSectionFile(renderSpec);
    const bool cacheComplete = cacheLoaded && !section->isPartial();

    const int targetPage =
        pendingPageJump.has_value() ? static_cast<int>(*pendingPageJump) : std::max(nextPageNumber, 0);
    const bool targetAlreadyAvailable = cacheLoaded && targetPage < static_cast<int>(section->pageCount);
    // Implicit content-offset restore (reopen / re-pagination) applies only when
    // nothing else already pins the target page.
    const bool implicitOffsetRestore = !pendingVisibleTextOffset.has_value() && pendingAnchor.empty() &&
                                       !pendingPageJump.has_value() && !pendingParagraphLookup &&
                                       !pendingListItemLookup && !pendingPercentJump &&
                                       currentSpineIndex == cachedSpineIndex && cachedVisibleTextOffset.has_value();
    const bool needsLookup = pendingVisibleTextOffset.has_value() || !pendingAnchor.empty() || pendingParagraphLookup ||
                             pendingListItemLookup || implicitOffsetRestore;
    const auto lookupResolved = [this, implicitOffsetRestore]() {
      if (pendingVisibleTextOffset && section->getPageForVisibleTextOffset(*pendingVisibleTextOffset)) return true;
      if (!pendingAnchor.empty() && section->findAnchor(pendingAnchor)) return true;
      if (pendingListItemLookup && section->getPageForListItemIndex(pendingListItemIndex)) return true;
      if (pendingParagraphLookup && section->getPageForParagraphIndex(pendingParagraphIndex)) return true;
      if (implicitOffsetRestore && section->getPageForVisibleTextOffset(*cachedVisibleTextOffset)) return true;
      return false;
    };
    const bool lookupAlreadyAvailable = cacheLoaded && lookupResolved();
    const bool mustBuildNow =
        !cacheComplete && (!targetAlreadyAvailable || pendingPercentJump || (needsLookup && !lookupAlreadyAvailable));

    if (mustBuildNow) {
      const size_t spineBytes = epub->getCumulativeSpineItemSize(currentSpineIndex) -
                                (currentSpineIndex > 0 ? epub->getCumulativeSpineItemSize(currentSpineIndex - 1) : 0);
      const bool showPopup = pendingPercentJump || targetPage > 20 ||
                             (!section->hasHtmlCache() && spineBytes > 96 * 1024) ||
                             (needsLookup && !implicitOffsetRestore);
      if (showPopup) {
        GUI.drawPopup(renderer, tr(STR_INDEXING));
        pagesUntilFullRefresh = 1;
      }

      bool started = false;
      {
        GfxRenderer::FrameBufferLoan loan(renderer);
        started = section->startBuild(renderSpec);
      }
      if (!started) {
        LOG_ERR("ERS", "Failed to start incremental section build");
        section.reset();
        renderSectionLoadFailure();
        automaticPageTurnActive = false;
        return;
      }

      while (!section->isBuildComplete()) {
        const bool enough = pendingPercentJump
                                ? false
                                : (needsLookup ? lookupResolved() : static_cast<int>(section->pageCount) > targetPage);
        if (enough) break;
        bool built = false;
        {
          GfxRenderer::FrameBufferLoan loan(renderer);
          built = section->buildSomeMore(BUILD_PAGES_PER_CHUNK);
        }
        if (!built) {
          LOG_ERR("ERS", "Failed during incremental section build");
          section.reset();
          renderSectionLoadFailure();
          automaticPageTurnActive = false;
          return;
        }
        // Long synchronous builds must keep feeding the task watchdog (upstream).
        resetTaskWatchdogIfSubscribed();
      }
      releaseReaderSdFontCachesForLowMemory(renderer, "ERS", "incremental section build");
    } else if (cacheComplete) {
      LOG_DBG("ERS", "Finalized cache found, skipping build");
      cachedChapterTotalPageCount = 0;
      pendingPaginationReposition = false;
    } else if (cacheLoaded) {
      LOG_DBG("ERS", "Partial cache covers landing page; extension deferred");
    }

    if (pendingPageJump.has_value()) {
      if (!section->isBuilding() && *pendingPageJump >= section->pageCount && section->pageCount > 0) {
        section->currentPage = section->pageCount - 1;
      } else {
        section->currentPage = *pendingPageJump;
      }
      pendingPageJump.reset();
    } else {
      section->currentPage = nextPageNumber;
      if (section->currentPage < 0) {
        section->currentPage = 0;
      } else if (!section->isBuilding() && section->currentPage >= section->pageCount && section->pageCount > 0) {
        LOG_DBG("ERS", "Clamping cached page %d to %d", section->currentPage, section->pageCount - 1);
        section->currentPage = section->pageCount - 1;
      }
    }

    if (!pendingAnchor.empty()) {
      if (const auto page = section->findAnchor(pendingAnchor)) {
        section->currentPage = *page;
        LOG_DBG("ERS", "Resolved anchor '%s' to page %d", pendingAnchor.c_str(), *page);
      } else {
        LOG_DBG("ERS", "Anchor '%s' not found in section %d", pendingAnchor.c_str(), currentSpineIndex);
      }
      pendingAnchor.clear();
      clearDeferredReposition();
    }

    if (pendingVisibleTextOffset) {
      if (const auto page = section->getPageForVisibleTextOffset(*pendingVisibleTextOffset)) {
        section->currentPage = *page;
        LOG_DBG("ERS", "Resolved visible text offset %lu to page %u",
                static_cast<unsigned long>(*pendingVisibleTextOffset), *page);
      } else {
        LOG_DBG("ERS", "Visible text offset %lu not found in section %d; using saved page",
                static_cast<unsigned long>(*pendingVisibleTextOffset), currentSpineIndex);
      }
      pendingVisibleTextOffset.reset();
      clearDeferredReposition();
    }

    bool resolvedSyncLut = false;
    if (pendingListItemLookup) {
      if (const auto page = section->getPageForListItemIndex(pendingListItemIndex)) {
        section->currentPage = *page;
        resolvedSyncLut = true;
        LOG_DBG("ERS", "Resolved list item %u to page %d", pendingListItemIndex, *page);
      } else {
        LOG_DBG("ERS", "List item %u not found in section %d", pendingListItemIndex, currentSpineIndex);
      }
      pendingListItemLookup = false;
    }

    if (!resolvedSyncLut && pendingParagraphLookup) {
      if (const auto page = section->getPageForParagraphIndex(pendingParagraphIndex)) {
        section->currentPage = *page;
        LOG_DBG("ERS", "Resolved paragraph %u to page %d", pendingParagraphIndex, *page);
      } else {
        LOG_DBG("ERS", "Paragraph %u not found in section %d", pendingParagraphIndex, currentSpineIndex);
      }
    }
    pendingParagraphLookup = false;

    // Content-offset restore of a reopened / re-paginated chapter (upstream):
    // exact even when the page count changed; the page-count ratio remains the
    // fallback inside applyDeferredReposition().
    if (implicitOffsetRestore) {
      if (const auto offsetPage = section->getPageForVisibleTextOffset(*cachedVisibleTextOffset)) {
        section->currentPage = *offsetPage;
        clearDeferredReposition();
      }
    }

    applyDeferredReposition();

    if (pendingPercentJump && section->pageCount > 0) {
      // Apply the pending percent jump now that we know the new section's page count.
      int newPage = static_cast<int>(pendingSpineProgress * static_cast<float>(section->pageCount));
      if (newPage >= section->pageCount) {
        newPage = section->pageCount - 1;
      }
      section->currentPage = newPage;
      pendingPercentJump = false;
    }
  }

  // A page turn may outrun the small background window. Extend synchronously
  // only until the requested page exists; the remainder keeps building in loop().
  while (section->isPartial() && section->currentPage >= static_cast<int>(section->pageCount)) {
    if (!section->isBuilding()) {
      bool started = false;
      {
        GfxRenderer::FrameBufferLoan loan(renderer);
        started = section->startBuild(renderSpec);
      }
      if (!started) {
        section.reset();
        renderSectionLoadFailure();
        return;
      }
    }
    bool built = false;
    {
      GfxRenderer::FrameBufferLoan loan(renderer);
      built = section->buildSomeMore(BUILD_PAGES_PER_CHUNK);
    }
    if (!built) {
      section.reset();
      renderSectionLoadFailure();
      return;
    }
    resetTaskWatchdogIfSubscribed();
  }
  while (section->isBuilding() && !section->isBuildComplete() &&
         section->currentPage >= static_cast<int>(section->pageCount)) {
    bool built = false;
    {
      GfxRenderer::FrameBufferLoan loan(renderer);
      built = section->buildSomeMore(BUILD_PAGES_PER_CHUNK);
    }
    if (!built) {
      section.reset();
      renderSectionLoadFailure();
      return;
    }
    resetTaskWatchdogIfSubscribed();
  }
  if (!section->isBuilding() && section->pageCount > 0 &&
      section->currentPage >= static_cast<int>(section->pageCount)) {
    section->currentPage = section->pageCount - 1;
  }
  applyDeferredReposition();

  renderer.clearScreen();

  if (section->pageCount == 0) {
    LOG_DBG("ERS", "No pages to render");
    renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() * 3 / 8, tr(STR_EMPTY_CHAPTER), true,
                              EpdFontFamily::BOLD);
    renderStatusBar();
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    return;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
    LOG_DBG("ERS", "Page out of bounds: %d (max %d)", section->currentPage, section->pageCount);
    renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() * 3 / 8, tr(STR_OUT_OF_BOUNDS), true,
                              EpdFontFamily::BOLD);
    renderStatusBar();
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    return;
  }

  updateBookmarkFlag();

  {
    auto loadedPage = section->loadPage(section->currentPage);
    if (!loadedPage) {
      LOG_ERR("ERS", "Failed to load page from SD - clearing section cache");
      automaticPageTurnActive = false;
      const bool giveUp = ++pageLoadRetryCount > MAX_PAGE_LOAD_RETRIES;
      section->abandonBuild();
      section->clearCache();
      section.reset();
      if (giveUp) {
        LOG_ERR("ERS", "Page load retry limit reached, aborting");
        pageLoadRetryCount = 0;
        renderer.clearScreen();
        renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() * 3 / 8, tr(STR_PAGE_LOAD_ERROR), true,
                                  EpdFontFamily::BOLD);
        renderer.displayBuffer();
        return;
      }
      requestUpdate();  // Try again after clearing cache
      return;
    }
    pageLoadRetryCount = 0;
    auto page = std::shared_ptr<Page>(std::move(loadedPage));

    currentPageVisibleOffset = page->visibleTextOffset;
    // Collect footnotes and tappable links from the loaded page
    currentPageFootnotes = std::move(page->footnotes);
    currentPageLinks = std::move(page->links);
    currentPageLinkMarginLeft = orientedMarginLeft;
    currentPageLinkMarginTop = orientedMarginTop;
    cacheCurrentPageForOverlay(page, orientedMarginLeft, orientedMarginTop);

    // The overlay and the non-tiled grayscale renderer share the renderer's
    // single stored-BW slot. Release the old page snapshot before
    // renderContents() needs that slot; the overlay re-snapshots below.
    discardOverlayPage();

    const auto start = millis();
    renderContents(page, orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    LOG_DBG("ERS", "Rendered page in %dms", millis() - start);
    lastRenderCompleteMs = millis();
  }
  // Menus, screenshots and overlays can request a render without moving the
  // reader. Avoid several FAT operations for the same position file.
  if (currentSpineIndex != lastSavedSpineIndex || section->currentPage != lastSavedPage ||
      section->estimatedTotalPages() != lastSavedPageCount) {
    saveProgress(currentSpineIndex, section->currentPage, section->estimatedTotalPages());
  }

  if (pendingScreenshot) {
    pendingScreenshot = false;
    ScreenshotUtil::takeScreenshot(renderer);
  }

  // Toolbar menu: overlay the toolbar / panel on top of the freshly rendered page.
  if (overlay != Overlay::None && usesToolbarMenu()) {
    // The page just re-rendered under the overlay: refresh the snapshot that
    // backs panel->toolbar restores (any previous copy is stale).
    overlayPageStored = renderer.storeBwBuffer();
    renderOverlay();
    // An open option picker rides on top of the freshly drawn panel.
    if (overlayPopup.isActive()) overlayPopup.render(renderer);
    // FAST, same as openOverlay: HALF's inverting pass flashes the sheet
    // (white, in night mode) on every repaint under an open panel.
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
}

bool EpubReaderActivity::saveProgress(int spineIndex, int currentPage, int pageCount) {
  if (!epub) return false;
  int progressPercent = 0;
  if (epub->getBookSize() > 0 && pageCount > 0) {
    const float chapterProgress = static_cast<float>(currentPage + 1) / static_cast<float>(pageCount);
    progressPercent =
        clampPercent(static_cast<int>(epub->calculateProgress(spineIndex, chapterProgress) * 100.0f + 0.5f));
  }

  const auto* statsBook = READING_STATS.findBook(!stableBookId.empty() ? stableBookId : epub->getPath());
  const bool alreadyCompleted = statsBook && statsBook->completed;
  if (!alreadyCompleted && progressPercent >= 100) {
    LOG_DBG("ERS", "Deferring EPUB completion until end-book confirmation");
    progressPercent = 99;
  }

  READING_STATS.updateProgress(static_cast<uint8_t>(progressPercent), alreadyCompleted && progressPercent >= 100,
                               getStatsChapterTitle(*epub, spineIndex),
                               getStatsChapterProgressPercent(currentPage, pageCount));

  // Content offset of the saved page (upstream): lets a re-paginated chapter
  // reopen on the same text instead of the same page number.
  std::optional<uint32_t> offset;
  if (section && spineIndex == currentSpineIndex && currentPage >= 0 && currentPage < section->pageCount) {
    offset = (currentPage == section->currentPage && currentPageVisibleOffset.has_value())
                 ? currentPageVisibleOffset
                 : section->getVisibleTextOffsetForPage(static_cast<uint16_t>(currentPage));
  }

  std::string progressPath = getStableProgressPath(stableBookId);
  if (!progressPath.empty()) {
    BookIdentity::ensureStableDataDir(stableBookId);
  } else {
    progressPath = getLegacyProgressPath(*epub);
  }
  if (writeReaderProgressFile(progressPath, spineIndex, currentPage, pageCount, offset)) {
    LOG_DBG("ERS", "Progress saved: Chapter %d, Page %d, offset %lu", spineIndex, currentPage,
            static_cast<unsigned long>(offset.value_or(0)));
    lastSavedSpineIndex = spineIndex;
    lastSavedPage = currentPage;
    lastSavedPageCount = pageCount;
    return true;
  } else {
    LOG_ERR("ERS", "Could not save progress!");
    return false;
  }
}

void EpubReaderActivity::drawTextHighlights(const Page& page, const int orientedMarginTop,
                                            const int orientedMarginLeft) const {
  if (!section || bookmarkStore.isEmpty()) return;

  constexpr size_t MAX_VISIBLE_WORDS = 240;
  std::array<HighlightWordRef, MAX_VISIBLE_WORDS> words{};
  size_t wordCount = 0;
  for (const auto& element : page.elements) {
    if (!element || element->getTag() != TAG_PageLine || wordCount >= words.size()) continue;
    const auto& line = static_cast<const PageLine&>(*element);
    const auto& blockPtr = line.getBlock();
    if (!blockPtr) continue;
    const auto& block = *blockPtr;
    const size_t count = block.wordCount();
    for (size_t index = 0; index < count && wordCount < words.size(); ++index) {
      const char* raw = block.wordText(index);
      const char* visible = raw + (hasEmSpacePrefix(raw) ? 3 : 0);
      bool hasText = false;
      for (const char* cursor = visible; *cursor; ++cursor) {
        if (!std::isspace(static_cast<unsigned char>(*cursor))) {
          hasText = true;
          break;
        }
      }
      if (hasText) {
        words[wordCount++] = HighlightWordRef{&line, &block, static_cast<uint16_t>(index)};
      }
    }
  }
  if (wordCount == 0) return;

  const int currentPage = section->currentPage;
  const int fontId = SETTINGS.getReaderFontId();
  const int lineHeight = renderer.getLineHeight(fontId);
  for (const auto& highlight : bookmarkStore.getAll()) {
    if (!highlight.isTextHighlight || highlight.spineIndex != static_cast<uint16_t>(currentSpineIndex) ||
        highlight.snippet.empty()) {
      continue;
    }
    if (highlight.hasVisibleTextOffset) {
      const auto anchoredPage = section->getPageForVisibleTextOffset(highlight.visibleTextOffset);
      if (!anchoredPage || currentPage != static_cast<int>(*anchoredPage)) continue;
    } else {
      if (currentPage + 3 < highlight.pageNumber || currentPage > static_cast<int>(highlight.endPageNumber) + 3) {
        continue;
      }
    }

    size_t bestStart = wordCount;
    size_t bestEnd = wordCount;
    int bestDistance = std::numeric_limits<int>::max();
    for (size_t candidate = 0; candidate < wordCount; ++candidate) {
      const char* cursor = highlight.snippet.c_str();
      const char* token = nullptr;
      size_t tokenLength = 0;
      size_t pageWord = candidate;
      size_t tokenOffset = 0;
      bool matched = true;
      while (HighlightTextMatcher::nextToken(cursor, token, tokenLength)) {
        bool tokenComplete = false;
        while (pageWord < wordCount) {
          const auto fragment = matchHighlightFragment(words[pageWord], token, tokenLength, tokenOffset);
          if (fragment.match == HighlightTextMatcher::TokenFragmentMatch::MISMATCH) {
            matched = false;
            break;
          }
          ++pageWord;
          tokenOffset += fragment.tokenBytes;
          if (fragment.match == HighlightTextMatcher::TokenFragmentMatch::COMPLETES_TOKEN) {
            tokenOffset = 0;
            tokenComplete = true;
            break;
          }
        }
        if (!matched || !tokenComplete) {
          matched = false;
          break;
        }
      }
      if (!matched || pageWord == candidate) continue;
      const int distance = std::abs(static_cast<int>(candidate) - static_cast<int>(highlight.startWordIndex));
      if (distance < bestDistance) {
        bestDistance = distance;
        bestStart = candidate;
        bestEnd = pageWord - 1;
      }
    }

    if (bestStart == wordCount && currentPage >= highlight.pageNumber && currentPage <= highlight.endPageNumber &&
        highlight.startWordIndex < wordCount) {
      bestStart = highlight.startWordIndex;
      bestEnd = std::min<size_t>(highlight.endWordIndex, wordCount - 1);
    }
    if (bestStart == wordCount) continue;

    for (size_t index = bestStart; index <= bestEnd; ++index) {
      const auto& ref = words[index];
      const char* raw = ref.block->wordText(ref.index);
      const auto style = ref.block->wordStyle(ref.index);
      const bool hasIndent = hasEmSpacePrefix(raw);
      const auto drawStyle = static_cast<EpdFontFamily::Style>(style & ~EpdFontFamily::UNDERLINE);
      const int skipX = hasIndent ? renderer.getTextAdvanceX(fontId, "\xe2\x80\x83", drawStyle) : 0;
      const int x = orientedMarginLeft + ref.line->xPos + ref.block->wordXpos(ref.index) + skipX;
      const int y = orientedMarginTop + ref.line->yPos;
      const int width = renderer.getTextAdvanceX(fontId, raw, style) - skipX;
      if (width <= 0) continue;

      if (SETTINGS.bionicReading == CrossPointSettings::BIONIC_READING_OFF) {
        renderer.fillRectDither(x, y, width, lineHeight, Color::LightGray);
        renderer.drawText(fontId, x, y, raw + (hasIndent ? 3 : 0), true, drawStyle);
      } else {
        renderer.drawLine(x, y + lineHeight - 2, x + width, y + lineHeight - 2, true);
      }
    }
  }
}

void EpubReaderActivity::renderContents(std::shared_ptr<Page> page, const int orientedMarginTop,
                                        const int orientedMarginRight, const int orientedMarginBottom,
                                        const int orientedMarginLeft) {
  const auto t0 = millis();
  const int fontId = SETTINGS.getReaderFontId();
  auto* fcm = renderer.getFontCacheManager();
  fcm->resetStats();

  // Decoded-image render cache slot (upstream): released when this page is done.
  struct PxcSlotGuard {
    ~PxcSlotGuard() { ImageBlock::releaseRenderCache(); }
  } pxcSlotGuard;

  // Font prewarm: scan pass accumulates text, then prewarm, then real render
  const auto heapBefore = MemoryBudget::snapshot();
  auto scope = fcm->createPrewarmScope();
  page->recordFontUsage(*fcm, fontId, SETTINGS.bionicReading);
  // Scan the status bar too: a CJK book/chapter title redirected to the SD
  // fallback font joins the page's single batch prewarm.
  renderStatusBar();
  scope.endScanAndPrewarm();
  const auto heapAfter = MemoryBudget::snapshot();
  fcm->logStats("prewarm");
  const auto tPrewarm = millis();

  LOG_DBG("ERS", "Heap prewarm: free=%u->%u delta=%ld maxAlloc=%u->%u delta=%ld", heapBefore.freeHeap,
          heapAfter.freeHeap, static_cast<int32_t>(heapAfter.freeHeap) - static_cast<int32_t>(heapBefore.freeHeap),
          heapBefore.maxAllocHeap, heapAfter.maxAllocHeap,
          static_cast<int32_t>(heapAfter.maxAllocHeap) - static_cast<int32_t>(heapBefore.maxAllocHeap));

  if (page->hasImagesNeedingDecode()) {
    page->renderWithImagePlaceholders(renderer, fontId, orientedMarginLeft, orientedMarginTop, SETTINGS.bionicReading);
    drawTextHighlights(*page, orientedMarginTop, orientedMarginLeft);
    renderStatusBar();
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    renderer.clearScreen();
  }

  const bool enableTextAA = SETTINGS.textAntiAliasing && !renderer.isDarkMode();
  const bool enableImageGrayscaleOnly = renderer.isDarkMode() && page->hasImages();
  const bool forceFullRefresh = pendingForceFullRefresh;
  pendingForceFullRefresh = false;
  // Force special handling for pages with images when anti-aliasing is on
  const bool imagePageWithAA = page->hasImages() && enableTextAA;
  HalDisplay::RefreshMode configuredRefreshMode = HalDisplay::FAST_REFRESH;
  const bool hasConfiguredRefreshMode = ReaderUtils::getConfiguredReaderRefreshMode(configuredRefreshMode);
  const bool needsGrayscale = enableTextAA || enableImageGrayscaleOnly;
  // Paper Mono only (no other panel combines): defer the B/W base activation so
  // the gray planes join it in a single waveform (upstream). Displaying the base
  // separately makes the gray pass re-drive the whole text body -- a visible
  // flash on every AA page.
  const bool combinedGrayscaleBase = needsGrayscale && !page->hasImages() && renderer.combinesGrayscaleBase() &&
                                     renderer.supportsStripGrayscale() && !hasConfiguredRefreshMode;

  page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop, SETTINGS.bionicReading);
  drawTextHighlights(*page, orientedMarginTop, orientedMarginLeft);
  renderStatusBar();
  fcm->logStats("bw_render");
  const auto tBwRender = millis();

  if (combinedGrayscaleBase) {
    ReaderUtils::displayBaseWithRefreshCycle(renderer, pagesUntilFullRefresh, forceFullRefresh);
  } else if (forceFullRefresh) {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh, true);
  } else if (hasConfiguredRefreshMode) {
    renderer.displayBuffer(configuredRefreshMode);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else if (imagePageWithAA) {
    // Double FAST_REFRESH with selective image blanking (pablohc's technique):
    // HALF_REFRESH sets particles too firmly for the grayscale LUT to adjust.
    // Instead, blank only the image area and do two fast refreshes.
    // Step 1: Display page with image area blanked (text appears, image area white)
    // Step 2: Re-render with images and display again (images appear clean)
    int16_t imgX, imgY, imgW, imgH;
    if (page->getImageBoundingBox(imgX, imgY, imgW, imgH)) {
      // The first page starts with a zero refresh counter. Give image pages
      // the same clean base as the normal cadence before their double-FAST
      // pipeline, especially after returning from KOSync (upstream dadc8ec2).
      if (pagesUntilFullRefresh <= 1) {
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      }
      renderer.fillRect(imgX + orientedMarginLeft, imgY + orientedMarginTop, imgW, imgH, false);
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);

      // Re-render page content to restore images into the blanked area
      // Status bar is not re-rendered here to avoid reading stale dynamic values (e.g. battery %)
      page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop, SETTINGS.bionicReading);
      drawTextHighlights(*page, orientedMarginTop, orientedMarginLeft);
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    } else {
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    }
    // The image's own page is handled above and doesn't count toward the full
    // refresh cadence. But the grayscale pass below leaves gray charge in the
    // image region that a plain fast diff on the *next* page can't clear, so
    // text there ghosts gray (#2190). Force the next ordinary page onto the
    // HALF ghost-cleanup path, which drives every pixel to its target
    // regardless of residue.
    pagesUntilFullRefresh = 1;
  } else {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
  }
  const auto tDisplay = millis();

  ReaderUtils::TiledGrayscaleTimings tiledTimings;
  const bool tiledGrayscale =
      needsGrayscale &&
      ReaderUtils::renderTiledGrayscale(
          renderer, "ERS",
          [&]() {
            if (enableImageGrayscaleOnly) {
              page->renderImages(renderer, fontId, orientedMarginLeft, orientedMarginTop);
            } else {
              page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop, SETTINGS.bionicReading);
            }
            drawTextHighlights(*page, orientedMarginTop, orientedMarginLeft);
            renderStatusBar();
          },
          &tiledTimings);

  if (tiledGrayscale) {
    const auto tEnd = millis();
    fcm->logStats("gray");
    LOG_DBG("ERS",
            "Page render (tiled): prewarm=%lums bw_render=%lums display=%lums "
            "gray_lsb=%lums gray_msb=%lums gray_display=%lums cleanup=%lums total=%lums",
            tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tiledTimings.grayLsb - tDisplay,
            tiledTimings.grayMsb - tiledTimings.grayLsb, tiledTimings.grayDisplay - tiledTimings.grayMsb,
            tiledTimings.cleanup - tiledTimings.grayDisplay, tEnd - t0);
    return;
  }

  // Save BW buffer to reset framebuffer and controller state after grayscale data sync.
  const auto bwStoreHeapBefore = MemoryBudget::snapshot();
  const bool storedBwBuffer = needsGrayscale && renderer.storeBwBuffer();
  const auto bwStoreHeapAfter = MemoryBudget::snapshot();
  const auto tBwStore = millis();
  if (needsGrayscale && !storedBwBuffer) {
    LOG_ERR("ERS", "Skipping grayscale enhancement: failed to store BW backup (free=%u maxAlloc=%u before=%u/%u)",
            bwStoreHeapAfter.freeHeap, bwStoreHeapAfter.maxAllocHeap, bwStoreHeapBefore.freeHeap,
            bwStoreHeapBefore.maxAllocHeap);
    if (combinedGrayscaleBase) {
      // The base activation is still deferred; commit it so the page reaches
      // the panel even without its grays.
      renderer.cleanupGrayscaleWithFrameBuffer();
    }
  }

  // grayscale rendering
  // TODO: Only do this if font supports it
  if (needsGrayscale && storedBwBuffer) {
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    if (enableImageGrayscaleOnly) {
      page->renderImages(renderer, fontId, orientedMarginLeft, orientedMarginTop);
    } else {
      page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop, SETTINGS.bionicReading);
    }
    drawTextHighlights(*page, orientedMarginTop, orientedMarginLeft);
    renderStatusBar();
    renderer.copyGrayscaleLsbBuffers();
    const auto tGrayLsb = millis();

    // Render and copy to MSB buffer
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    if (enableImageGrayscaleOnly) {
      page->renderImages(renderer, fontId, orientedMarginLeft, orientedMarginTop);
    } else {
      page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop, SETTINGS.bionicReading);
    }
    drawTextHighlights(*page, orientedMarginTop, orientedMarginLeft);
    renderStatusBar();
    renderer.copyGrayscaleMsbBuffers();
    const auto tGrayMsb = millis();

    // display grayscale part
    renderer.displayGrayBuffer();
    const auto tGrayDisplay = millis();
    renderer.setRenderMode(GfxRenderer::BW);
    fcm->logStats("gray");
    // restore the bw data
    renderer.restoreBwBuffer();
    const auto tBwRestore = millis();

    const auto tEnd = millis();
    LOG_DBG("ERS",
            "Page render: prewarm=%lums bw_render=%lums display=%lums bw_store=%lums "
            "gray_lsb=%lums gray_msb=%lums gray_display=%lums bw_restore=%lums total=%lums",
            tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tBwStore - tDisplay, tGrayLsb - tBwStore,
            tGrayMsb - tGrayLsb, tGrayDisplay - tGrayMsb, tBwRestore - tGrayDisplay, tEnd - t0);
  } else {
    const auto tEnd = millis();
    LOG_DBG("ERS", "Page render: prewarm=%lums bw_render=%lums display=%lums bw_store=%lums grayscale=%s total=%lums",
            tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tBwStore - tDisplay,
            needsGrayscale ? "skipped" : "off", tEnd - t0);
  }
}

void EpubReaderActivity::renderStatusBar() const {
  if (statusBarTemporarilyHidden || !section || !epub) {
    return;
  }

  // Calculate progress in book
  const int currentPage = section->currentPage + 1;
  const float pageCount = section->estimatedTotalPages();
  const float sectionChapterProg = (pageCount > 0) ? (static_cast<float>(currentPage) / pageCount) : 0;
  const float bookProgress = epub->calculateProgress(currentSpineIndex, sectionChapterProg) * 100;

  std::string title;
  int textYOffset = 0;
  const auto sb = SETTINGS.statusBarSpec();

  if (automaticPageTurnActive) {
    title = tr(STR_AUTO_TURN_ENABLED) + std::to_string(60 * 1000 / pageTurnDuration);

    // calculates textYOffset when rendering title in status bar
    const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();

    // offsets text if no status bar or progress bar only
    if (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight()) {
      textYOffset += UITheme::getInstance().getMetrics().statusBarVerticalMargin;
    }

  } else if (sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::CHAPTER_TITLE) {
    title = tr(STR_UNNAMED);
    const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
    if (tocIndex != -1) {
      const auto tocItem = epub->getTocItem(tocIndex);
      title = tocItem.title;
    }

  } else if (sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::BOOK_TITLE) {
    title = epub->getTitle();
  }

  GUI.drawStatusBar(renderer, bookProgress, currentPage, pageCount, title, 0, textYOffset, true, currentPageBookmarked,
                    section->isBuilding());
}

void EpubReaderActivity::renderSectionLoadFailure() {
  releaseReaderSdFontCachesForLowMemory(renderer, "ERS", "section load failure");
  if (auto* fontCache = renderer.getFontCacheManager()) {
    fontCache->clearCache();
  }

  const auto heap = MemoryBudget::snapshot();
  LOG_DBG("ERS", "Rendering minimal page-load failure screen (free=%u maxAlloc=%u)", heap.freeHeap, heap.maxAllocHeap);

  renderer.clearScreen();
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int boxW = 96;
  const int boxH = 96;
  const int x = (screenW - boxW) / 2;
  const int y = (screenH - boxH) / 2;
  renderer.drawRect(x, y, boxW, boxH, true);
  renderer.drawLine(x + 24, y + 24, x + boxW - 25, y + boxH - 25, 3, true);
  renderer.drawLine(x + boxW - 25, y + 24, x + 24, y + boxH - 25, 3, true);
  renderer.displayBuffer();
}

// ---------------------------------------------------------------------------
// Toolbar reader menu (upstream)
// ---------------------------------------------------------------------------

namespace {
constexpr StrId kTextRowNames[] = {StrId::STR_FONT, StrId::STR_FONT_SIZE, StrId::STR_LINE_SPACING,
                                   StrId::STR_PARA_ALIGNMENT, StrId::STR_FOCUS_READING};
constexpr StrId kSpacingIds[] = {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE, StrId::STR_EXTRA_WIDE};
constexpr StrId kAlignIds[] = {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                               StrId::STR_BOOK_S_STYLE};
constexpr int kTextRowCount = static_cast<int>(std::size(kTextRowNames));
static_assert(std::size(kSpacingIds) == CrossPointSettings::LINE_COMPRESSION_COUNT, "line spacing labels");
static_assert(std::size(kAlignIds) == CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT, "alignment labels");
}  // namespace

bool EpubReaderActivity::usesToolbarMenu() const {
  // Touch-first chrome: button boards always get the classic list menu, even
  // if a settings file (e.g. an SD card moved from a touch board) says Toolbar.
  return mappedInput.hasTouch() && SETTINGS.readerMenuStyle == CrossPointSettings::READER_MENU_TOOLBAR;
}

std::string EpubReaderActivity::currentChapterTitle() const {
  if (!epub) return "";
  const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (tocIndex != -1) {
    return epub->getTocItem(tocIndex).title;
  }
  return tr(STR_UNNAMED);
}

std::string EpubReaderActivity::textRowName(int row) const {
  return row >= 0 && row < kTextRowCount ? I18N.get(kTextRowNames[row]) : "";
}

std::string EpubReaderActivity::textRowValue(int row) const {
  static constexpr StrId kFamily[] = {StrId::STR_NOTO_SERIF, StrId::STR_NOTO_SANS};
  switch (row) {
    case 0:
      if (SETTINGS.sdFontFamilyName[0] != '\0') return SETTINGS.sdFontFamilyName;
      return I18N.get(kFamily[SETTINGS.fontFamily % CrossPointSettings::FONT_FAMILY_COUNT]);
    case 1:
      return std::to_string(SETTINGS.fontPointSize) + " pt";
    case 2:
      return I18N.get(kSpacingIds[SETTINGS.lineSpacing % CrossPointSettings::LINE_COMPRESSION_COUNT]);
    case 3:
      return I18N.get(kAlignIds[SETTINGS.paragraphAlignment % CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT]);
    case 4:
      return SETTINGS.bionicReading != CrossPointSettings::BIONIC_READING_OFF ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    default:
      return "";
  }
}

// Live apply: persist, re-paginate, and let render() redraw the page with
// the open panel back on top -- the book itself is the preview.
void EpubReaderActivity::applyTextSettingLive() {
  applyReaderTextSettings();
  discardOverlayPage();  // the stored page is laid out with the old settings
  requestUpdate();
}

// Settings-style option pickers for the Text panel's enum rows. Every
// selection applies immediately to the page under the sheet.
void EpubReaderActivity::showTextRowPopup(const int row) {
  switch (row) {
    case 1: {
      // The point sizes the active family actually ships.
      const auto sizes = readerFontPointSizes(&sdFontSystem.registry(), SETTINGS.sdFontFamilyName);
      if (sizes.empty()) return;
      std::vector<std::string> labels;
      labels.reserve(sizes.size());
      for (const uint8_t size : sizes) labels.push_back(std::to_string(size) + " pt");
      const uint8_t cur = snapToNearestPointSize(sizes, SETTINGS.fontPointSize);
      int curIdx = 0;
      for (size_t i = 0; i < sizes.size(); ++i) {
        if (sizes[i] == cur) curIdx = static_cast<int>(i);
      }
      overlayPopup.show(StrId::STR_FONT_SIZE, labels, curIdx, [this, sizes](int idx) {
        if (idx < 0 || idx >= static_cast<int>(sizes.size())) return;
        SETTINGS.fontPointSize = sizes[idx];
        applyTextSettingLive();
      });
      break;
    }
    case 2:
      overlayPopup.show(StrId::STR_LINE_SPACING, kSpacingIds, static_cast<int>(std::size(kSpacingIds)),
                        SETTINGS.lineSpacing % CrossPointSettings::LINE_COMPRESSION_COUNT, [this](int idx) {
                          SETTINGS.lineSpacing = static_cast<uint8_t>(idx);
                          applyTextSettingLive();
                        });
      break;
    case 3:
      overlayPopup.show(StrId::STR_PARA_ALIGNMENT, kAlignIds, static_cast<int>(std::size(kAlignIds)),
                        SETTINGS.paragraphAlignment % CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT, [this](int idx) {
                          SETTINGS.paragraphAlignment = static_cast<uint8_t>(idx);
                          applyTextSettingLive();
                        });
      break;
    default:
      return;
  }
  paintOverlayPopup();
}

void EpubReaderActivity::discardOverlayPage() {
  if (!overlayPageStored) return;
  renderer.discardStoredBwBuffer();
  overlayPageStored = false;
}

void EpubReaderActivity::openOverlay(Overlay target) {
  const Overlay previous = overlay;
  overlay = target;
  if (!toolbarUi) {
    toolbarUi = makeUniqueNoThrow<ReaderToolbarUi>(renderer);
    if (!toolbarUi) {
      LOG_ERR("ERS", "OOM: reader toolbar");
      overlay = Overlay::None;
      openReaderMenu();
      return;
    }
  }
  if (previous == Overlay::None) toolbarUi->begin();
  // Buttons show a cursor from the start; touch boards only once a button moves it.
  panelCursorShown = !mappedInput.hasTouch();
  switch (target) {
    case Overlay::Toolbar:
      focusedTool = 0;
      break;
    case Overlay::Contents:
      panelIndex = std::max(0, epub->getTocIndexForSpineIndex(currentSpineIndex));
      // Fresh viewport opening on the current chapter, cursor shown or not.
      toolbarUi->nav().reset(panelIndex);
      toolbarUi->nav().top = panelIndex;
      break;
    case Overlay::Text:
      panelIndex = 0;
      toolbarUi->nav().reset();
      break;
    case Overlay::More:
      panelIndex = 0;
      buildMoreActions();
      toolbarUi->nav().reset();
      break;
    default:
      break;
  }
  panelHoldJumped = false;

  // The page is already on screen and still in the framebuffer, so paint the
  // chrome straight onto it and push one refresh. requestUpdate() would
  // re-render the whole page first: slow, and visibly wrong, since that repaint
  // lands before the overlay does.
  if (section) {
    // Serialize against the render task: render() may be mid-page (status
    // bar included) in the shared framebuffer, and painting the chrome from
    // the loop task at the same time interleaves the two frames.
    RenderLock lock(*this);
    if (previous == Overlay::None) {
      // Snapshot the clean page so stepping back from a panel to the toolbar
      // (and closing, where supported) can restore it without a re-render.
      overlayPageStored = renderer.storeBwBuffer();
    } else if (overlayPageStored) {
      // Overlay -> overlay: wipe the previous chrome back to the clean page so
      // none of it shows around or through the new sheet; re-store for the
      // next transition. No baseline resync: the glass still shows the old
      // chrome, and the differential must keep diffing against it to erase it.
      renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
      overlayPageStored = renderer.storeBwBuffer();
    }
    renderOverlay();
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  } else {
    requestUpdate();  // no page yet: render() draws the overlay once it is
  }
}

// Close the overlay back to the reading page. Without the grayscale AA pass the
// page snapshot is restored with one FAST refresh -- no re-render, no flash;
// with AA (or dark mode's inverted planes) the page is re-rendered so its
// gray planes come back.
void EpubReaderActivity::closeOverlayToPage() {
  overlay = Overlay::None;
  overlayPopup.dismiss();  // an option picker cannot outlive its panel
  toolbarUi.reset();       // ~1 KB of interaction table + props, only needed while open
  const bool needsRerender = SETTINGS.textAntiAliasing || renderer.isDarkMode();
  if (!needsRerender && overlayPageStored) {
    RenderLock lock(*this);  // the render task shares the framebuffer
    // No baseline resync: the glass shows the chrome, and erasing it needs
    // the differential to keep diffing against the last pushed frame.
    renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
    overlayPageStored = false;
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }
  discardOverlayPage();
  requestUpdate();  // redraw the clean page
}

void EpubReaderActivity::renderOverlay() {
  if (!epub || !section || !toolbarUi) return;

  ReaderToolbarUi::Model model;
  // The toolbar's tool pill is the button-navigation cursor: tap-first (same
  // convention as the panel lists), it only shows once a button has moved it.
  // Panels override below: there the pill marks the open panel on every board.
  model.activeTool = (overlay == Overlay::Toolbar && !panelCursorShown) ? -1 : focusedTool;
  // Strings the model points at live here until render() returns.
  std::string chapterTitle, pageInfo;

  if (overlay == Overlay::Toolbar) {
    chapterTitle = currentChapterTitle();
    const int pageCount = section->estimatedTotalPages();
    const float chapterProgress =
        pageCount > 0 ? static_cast<float>(section->currentPage + 1) / static_cast<float>(pageCount) : 0.0f;
    const float bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress);
    pageInfo = std::to_string(section->currentPage + 1) + "/" + std::to_string(pageCount) + "   " +
               std::to_string(clampPercent(static_cast<int>(bookProgress * 100.0f + 0.5f))) + "%";
    model.chapterTitle = chapterTitle.c_str();
    model.pageInfo = pageInfo.c_str();
    model.progressPermille = static_cast<int>(bookProgress * 1000.0f + 0.5f);
    toolbarUi->setModel(model);
    toolbarUi->render();
    return;
  }

  // Panels (Contents / Text / More): a bottom sheet over the page + button hints.
  model.panel = true;
  if (!mappedInput.hasTouch()) {
    model.bottomReserve = UITheme::getInstance().getMetrics().buttonHintsHeight;
    model.denseRows = true;
  }
  // Tap-first: the cursor is only drawn once a button has moved it, so a
  // tapped row does not stay inverted after its action.
  model.selectedIndex = panelCursorShown ? panelIndex : -1;
  if (overlay == Overlay::Contents) {
    model.panelTitle = tr(STR_TOOL_CONTENTS);
    model.itemCount = epub->getTocItemsCount();
    model.rowText = [this](int i) {
      const auto item = epub->getTocItem(i);
      const int depth = item.level > 1 ? (item.level - 1) * 2 : 0;
      return std::string(depth, ' ') + item.title;
    };
  } else if (overlay == Overlay::Text) {
    model.panelTitle = tr(STR_TOOL_TEXT);
    model.itemCount = kTextRowCount;
    model.rowText = [this](int i) { return textRowName(i); };
    model.rowValue = [this](int i) { return textRowValue(i); };
  } else {
    model.panelTitle = tr(STR_TOOL_MORE);
    model.itemCount = static_cast<int>(moreItems.size());
    model.rowText = [this](int i) { return moreRowName(i); };
    model.rowValue = [this](int i) { return moreRowValue(i); };
  }
  toolbarUi->setModel(model);
  toolbarUi->render();

  if (!mappedInput.hasTouch()) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
}

void EpubReaderActivity::handleOverlayInput() {
  if (!toolbarUi) return;

  // A modal option picker over the panel owns all input while open.
  if (overlayPopup.isActive()) {
    overlayPopup.handleInput(mappedInput, [this] {
      if (overlayPopup.isActive()) {
        paintOverlayPopup();  // highlight moved
        return;
      }
      // Dismissed or selected: erase the dialog -- clean page back, then the
      // panel over it (the dialog can overhang the sheet onto the page).
      RenderLock lock(*this);
      if (overlayPageStored) {
        renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
        overlayPageStored = renderer.storeBwBuffer();
        renderOverlay();
        renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      } else {
        requestUpdate();
      }
    });
    return;
  }
  const auto fastRedraw = [this] {
    RenderLock lock(*this);  // the render task shares the framebuffer
    renderOverlay();
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  };

  // Jump to another spine item (chapter scrub). The overlay stays up and is
  // re-drawn over the new page by render().
  const auto gotoSpine = [this](int target) {
    const int spineCount = epub->getSpineItemsCount();
    target = std::clamp(target, 0, spineCount - 1);
    if (target != currentSpineIndex) {
      RenderLock lock(*this);
      clearDeferredReposition();
      invalidateCurrentOverlayPageCache();
      nextPageNumber = 0;
      currentSpineIndex = target;
      sessionProgressTouched = true;
      section.reset();
    }
    requestUpdate();
  };
  const auto toolOverlay = [](int tool) {
    return tool == 0 ? Overlay::Contents : (tool == 1 ? Overlay::Text : Overlay::More);
  };

  // Touch first: FreeInkUI routes the frame against the tap targets the last
  // render registered and hands back the action it mapped to.
  const auto routed = toolbarUi->route(mappedInput);

  // --- Toolbar ---
  if (overlay == Overlay::Toolbar) {
    switch (routed.event) {
      case ReaderToolbarUi::Event::Dismiss:
        closeOverlayToPage();
        return;
      case ReaderToolbarUi::Event::Tool:
        focusedTool = routed.value;
        openOverlay(toolOverlay(focusedTool));
        return;
      case ReaderToolbarUi::Event::PrevChapter:
        gotoSpine(currentSpineIndex - 1);
        return;
      case ReaderToolbarUi::Event::NextChapter:
        gotoSpine(currentSpineIndex + 1);
        return;
      case ReaderToolbarUi::Event::Scrub:
        gotoSpine(static_cast<int>((static_cast<float>(routed.permille) / 1000.0f) *
                                       static_cast<float>(epub->getSpineItemsCount() - 1) +
                                   0.5f));
        return;
      default:
        break;
    }
    if (routed.routed) return;  // a touch frame the chrome consumed (or dead space)

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      closeOverlayToPage();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      focusedTool = (focusedTool + 2) % 3;
      panelCursorShown = true;
      fastRedraw();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      focusedTool = (focusedTool + 1) % 3;
      panelCursorShown = true;
      fastRedraw();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      openOverlay(toolOverlay(focusedTool));
      return;
    }
    const bool prev = mappedInput.wasReleased(MappedInputManager::Button::Up);
    const bool next = mappedInput.wasReleased(MappedInputManager::Button::Down);
    if (prev || next) {
      gotoSpine(currentSpineIndex + (next ? 1 : -1));
    }
    return;
  }

  // --- Panels (Contents / Text / More) ---
  const int count = overlay == Overlay::Contents ? epub->getTocItemsCount()
                    : overlay == Overlay::Text   ? kTextRowCount
                                                 : static_cast<int>(moreItems.size());
  const int pageRows = std::max(1, toolbarUi->visibleRows());

  // Activate the highlighted row: change a value / jump to a chapter / run an
  // action. Shared by the Confirm button and a row tap.
  const auto activateRow = [this, count] {
    if (panelIndex < 0 || panelIndex >= count) return;
    if (overlay == Overlay::Text) {
      if (panelIndex == 0) {
        // Full font picker (built-in + SD fonts, live preview) -- the same
        // screen Settings uses; a popup cannot scroll a long font list.
        overlay = Overlay::None;
        overlayPopup.dismiss();
        discardOverlayPage();
        READING_STATS.noteActivity();
        startActivityForResult(std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                                      TextSettingsActivity::Tab::Family),
                               [this](const ActivityResult&) {
                                 READING_STATS.resumeSession();
                                 applyReaderTextSettings();
                                 overlay = Overlay::Text;  // back to the Text panel
                                 panelIndex = 0;
                                 if (toolbarUi) toolbarUi->begin();  // the picker drew its own FUI screen
                                 requestUpdate();                    // re-render page + Text panel
                               });
      } else if (panelIndex == 4) {
        // Focus (bionic) reading: a tap toggles between off and the normal mode
        // and applies live; the subtle variant stays reachable from Settings.
        SETTINGS.bionicReading = SETTINGS.bionicReading != CrossPointSettings::BIONIC_READING_OFF
                                     ? CrossPointSettings::BIONIC_READING_OFF
                                     : CrossPointSettings::BIONIC_READING_NORMAL;
        applyTextSettingLive();
      } else {
        // Enum rows open the Settings-style option picker.
        showTextRowPopup(panelIndex);
      }
    } else if (overlay == Overlay::Contents) {
      const auto item = epub->getTocItem(panelIndex);
      if (item.spineIndex != -1) {
        RenderLock lock(*this);
        clearDeferredReposition();
        invalidateCurrentOverlayPageCache();
        currentSpineIndex = item.spineIndex;
        pendingAnchor = item.anchor;
        nextPageNumber = 0;
        sessionProgressTouched = true;
        section.reset();
      }
      overlay = Overlay::None;
      discardOverlayPage();
      requestUpdate();
    } else if (overlay == Overlay::More) {
      activateMoreRow(panelIndex);
    }
  };

  // Steps up to the toolbar -- the Back button and a tap on the page above
  // the sheet.
  const auto dismissPanel = [this, &fastRedraw] {
    overlay = Overlay::Toolbar;
    // Restore the snapshotted page under the toolbar instead of re-rendering
    // it (2+ refreshes -> one FAST). Re-store right away so another panel
    // round-trip can restore again.
    if (overlayPageStored) {
      {
        RenderLock lock(*this);  // the render task shares the framebuffer
        renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
        overlayPageStored = renderer.storeBwBuffer();
      }
      fastRedraw();  // takes its own RenderLock
      return;
    }
    requestUpdate();
  };

  // Pages the list by one screen of rows through the nav (measured page size,
  // no-op at the ends). A shown cursor rides along so the buttons continue
  // from what is visible; on touch boards only the viewport moves.
  const auto pageList = [this, count, pageRows, &fastRedraw](int direction) {
    if (count <= 0) return;
    const bool moved = toolbarUi->nav().scrollBy(direction * pageRows, count);
    if (panelCursorShown) {
      panelIndex = std::clamp(panelIndex + direction * pageRows, 0, count - 1);
      fastRedraw();
      return;
    }
    if (moved) fastRedraw();
  };

  switch (routed.event) {
    case ReaderToolbarUi::Event::Dismiss:
      dismissPanel();
      return;
    case ReaderToolbarUi::Event::Tool: {
      // Sheet-bottom tool switcher: hop straight to another panel.
      const Overlay target = toolOverlay(routed.value);
      if (target != overlay) {
        focusedTool = routed.value;
        openOverlay(target);
      }
      return;
    }
    case ReaderToolbarUi::Event::Row:
      // A tap on the right-edge strip pages the sheet instead (upper half =
      // previous page, lower half = next): swipes are unreliable on etched
      // glass, and a long contents list needs a fast way through.
      if (routed.x >= renderer.getScreenWidth() - 44) {
        pageList(routed.y >= renderer.getScreenHeight() - (renderer.getScreenHeight() * 62) / 200 ? 1 : -1);
        return;
      }
      panelIndex = routed.value;
      panelCursorShown = false;
      activateRow();
      return;
    default:
      break;
  }
  // Swipe up/down pages the list. Checked before the routed-frame return:
  // FUI routes every touch frame over the sheet, so a swipe's frames count as
  // routed (without dispatching -- too much travel for a tap) and the gesture
  // would otherwise never be seen.
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    pageList(swipe == MappedInputManager::SwipeDir::Up ? 1 : -1);
    return;
  }
  if (routed.routed) return;  // consumed by the chrome (title band, dead space)

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    dismissPanel();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateRow();
    return;
  }

  // Up/Down (side) and Left/Right (front) move the cursor: a tap steps one
  // row, holding past PANEL_HOLD_MS jumps PANEL_HOLD_STEP rows in one go, which
  // is how you cross a hundreds-of-chapters contents list without a press per
  // row. The jump fires once on the hold and swallows the release that ends it,
  // so it never doubles up with the tap step.
  if (count > 0) {
    const bool up = mappedInput.isPressed(MappedInputManager::Button::Up) ||
                    mappedInput.isPressed(MappedInputManager::Button::Left);
    const bool down = mappedInput.isPressed(MappedInputManager::Button::Down) ||
                      mappedInput.isPressed(MappedInputManager::Button::Right);
    if (!panelHoldJumped && (up || down) && mappedInput.getHeldTime() >= PANEL_HOLD_MS) {
      const int step = down ? PANEL_HOLD_STEP : -PANEL_HOLD_STEP;
      panelIndex = std::clamp(panelIndex + step, 0, count - 1);
      panelHoldJumped = true;
      panelCursorShown = true;
      fastRedraw();
      return;
    }

    const bool releasedUp = mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Left);
    const bool releasedDown = mappedInput.wasReleased(MappedInputManager::Button::Down) ||
                              mappedInput.wasReleased(MappedInputManager::Button::Right);
    if (releasedUp || releasedDown) {
      if (!panelHoldJumped) {
        panelIndex = releasedUp ? ButtonNavigator::previousIndex(panelIndex, count)
                                : ButtonNavigator::nextIndex(panelIndex, count);
        panelCursorShown = true;
        fastRedraw();
      }
      panelHoldJumped = false;
    }
  }
}

// First paint of the option picker over the panel (and highlight repaints).
// The dialog draws over the current framebuffer without clearing; erasing it
// on dismissal is the popup gate's restore in handleOverlayInput().
void EpubReaderActivity::paintOverlayPopup() {
  RenderLock lock(*this);
  overlayPopup.render(renderer);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void EpubReaderActivity::applyReaderTextSettings() {
  SETTINGS.saveToFile();
  // (Re)load or unload the selected SD-card font for the current family/size.
  // The reader otherwise only loads SD fonts on book open, so without this an
  // in-reader font change wouldn't take effect until re-opening the book.
  ensureSdFontLoaded();
  invalidateCurrentOverlayPageCache();
  RenderLock lock(*this);
  rememberCurrentLayoutPosition();
  section.reset();  // force re-pagination with the new settings
}

// The More panel carries everything the classic list menu offers except the
// chapter list, which has its own tool (Contents). The fork's quick settings
// stay: they cover margins, refresh and display options the Text panel lacks.
void EpubReaderActivity::buildMoreActions() {
  using MA = EpubReaderMenuActivity::MenuAction;
  EpubReaderMenuActivity::buildMenuItems(moreItems, !currentPageFootnotes.empty());
  moreItems.erase(std::remove_if(moreItems.begin(), moreItems.end(),
                                 [](const auto& item) { return item.action == MA::SELECT_CHAPTER; }),
                  moreItems.end());
}

std::string EpubReaderActivity::moreRowName(int row) const {
  return row >= 0 && row < static_cast<int>(moreItems.size()) ? I18N.get(moreItems[row].labelId) : "";
}

std::string EpubReaderActivity::moreRowValue(int row) const {
  using MA = EpubReaderMenuActivity::MenuAction;
  static constexpr StrId kOrient[] = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_ORIENTATION_INVERTED,
                                      StrId::STR_LANDSCAPE_CCW};
  static_assert(std::size(kOrient) == CrossPointSettings::ORIENTATION_COUNT, "orientation labels");
  if (row < 0 || row >= static_cast<int>(moreItems.size())) return "";
  switch (moreItems[row].action) {
    case MA::ROTATE_SCREEN:
      return I18N.get(kOrient[SETTINGS.orientation % CrossPointSettings::ORIENTATION_COUNT]);
    case MA::AUTO_PAGE_TURN:
      return (autoTurnOption == 0 || autoTurnOption >= static_cast<int>(std::size(PAGE_TURN_RATES)))
                 ? std::string(tr(STR_STATE_OFF))
                 : std::to_string(PAGE_TURN_RATES[autoTurnOption]);
    case MA::NIGHT_MODE:
      return SETTINGS.darkMode ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case MA::FRONTLIGHT:
      return Frontlight.isOn() ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    default:
      return "";
  }
}

void EpubReaderActivity::activateMoreRow(int row) {
  using MA = EpubReaderMenuActivity::MenuAction;
  if (row < 0 || row >= static_cast<int>(moreItems.size())) return;
  const auto action = moreItems[row].action;
  // In-place toggles keep the panel open and re-render the page beneath it.
  switch (action) {
    case MA::ROTATE_SCREEN: {
      static constexpr StrId kOrientIds[] = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW,
                                             StrId::STR_ORIENTATION_INVERTED, StrId::STR_LANDSCAPE_CCW};
      static_assert(std::size(kOrientIds) == CrossPointSettings::ORIENTATION_COUNT, "orientation options");
      overlayPopup.show(StrId::STR_ORIENTATION, kOrientIds, static_cast<int>(std::size(kOrientIds)),
                        SETTINGS.orientation % CrossPointSettings::ORIENTATION_COUNT, [this](int idx) {
                          if (idx == SETTINGS.orientation) return;
                          applyOrientation(static_cast<uint8_t>(idx));
                          // The stored page is laid out for the old orientation.
                          discardOverlayPage();
                          requestUpdate();
                        });
      paintOverlayPopup();
      return;
    }
    case MA::AUTO_PAGE_TURN: {
      std::vector<std::string> labels;
      labels.reserve(std::size(PAGE_TURN_RATES));
      labels.emplace_back(tr(STR_STATE_OFF));
      for (size_t i = 1; i < std::size(PAGE_TURN_RATES); ++i) labels.push_back(std::to_string(PAGE_TURN_RATES[i]));
      overlayPopup.show(StrId::STR_AUTO_TURN_PAGES_PER_MIN, labels, autoTurnOption, [this](int idx) {
        autoTurnOption = idx;
        toggleAutoPageTurn(static_cast<uint8_t>(idx));
      });
      paintOverlayPopup();
      return;
    }
    case MA::NIGHT_MODE: {
      // One flag with the fork's dark mode; flip the renderer and re-render
      // the page (full refresh) with the panel back on top.
      SETTINGS.darkMode = SETTINGS.darkMode == 0 ? 1 : 0;
      SETTINGS.saveToFile();
      renderer.setDarkMode(SETTINGS.darkMode);
      renderer.requestNextFullRefresh();
      invalidateCurrentOverlayPageCache();
      discardOverlayPage();
      pendingForceFullRefresh = true;
      requestUpdate();
      return;
    }
    case MA::FRONTLIGHT: {
      const bool lightOn = !Frontlight.isOn();
      Frontlight.setOn(lightOn);
      SETTINGS.frontlightOn = lightOn ? 1 : 0;
      SETTINGS.saveToFile();
      {
        RenderLock lock(*this);  // the render task shares the framebuffer
        renderOverlay();
        renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      }
      return;
    }
    default:
      break;
  }
  // Leaf actions open their own screen / perform the action; close the overlay first.
  overlay = Overlay::None;
  overlayPopup.dismiss();
  discardOverlayPage();
  if (action == MA::SAVE_PAGE_MARK) {
    // No child activity here to trigger the re-render the list menu relies on:
    // the fork's page-mark toggle shows its own confirmation popup.
    toggleCurrentPageBookmark();
    return;
  }
  onReaderMenuConfirm(action);
  // Actions that neither open a screen nor leave the reader (a sync with no
  // credentials, say) would otherwise leave the closed panel on screen.
  if (action != MA::GO_HOME && action != MA::DELETE_CACHE) requestUpdate();
}

void EpubReaderActivity::navigateToHref(const std::string& hrefStr, const bool savePosition) {
  if (!epub) return;

  // Push current position onto saved stack
  if (savePosition && section && footnoteDepth < MAX_FOOTNOTE_DEPTH) {
    savedPositions[footnoteDepth] = {currentSpineIndex, section->currentPage};
    footnoteDepth++;
    LOG_DBG("ERS", "Saved position [%d]: spine %d, page %d", footnoteDepth, currentSpineIndex, section->currentPage);
  }

  // Extract fragment anchor (e.g. "#note1" or "chapter2.xhtml#note1")
  std::string anchor;
  const auto hashPos = hrefStr.find('#');
  if (hashPos != std::string::npos && hashPos + 1 < hrefStr.size()) {
    anchor = hrefStr.substr(hashPos + 1);
  }

  // Check for same-file anchor reference (#anchor only)
  bool sameFile = !hrefStr.empty() && hrefStr[0] == '#';

  int targetSpineIndex;
  if (sameFile) {
    targetSpineIndex = currentSpineIndex;
  } else {
    targetSpineIndex = epub->resolveHrefToSpineIndex(hrefStr);
  }

  if (targetSpineIndex < 0) {
    LOG_DBG("ERS", "Could not resolve href: %s", hrefStr.c_str());
    if (savePosition && footnoteDepth > 0) footnoteDepth--;  // undo push
    return;
  }

  {
    RenderLock lock(*this);
    clearDeferredReposition();
    invalidateCurrentOverlayPageCache();
    pendingAnchor = std::move(anchor);
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = 0;
    section.reset();
  }
  requestUpdate();
  LOG_DBG("ERS", "Navigated to spine %d for href: %s", targetSpineIndex, hrefStr.c_str());
}

void EpubReaderActivity::restoreSavedPosition() {
  if (footnoteDepth <= 0) return;
  footnoteDepth--;
  const auto& pos = savedPositions[footnoteDepth];
  LOG_DBG("ERS", "Restoring position [%d]: spine %d, page %d", footnoteDepth, pos.spineIndex, pos.pageNumber);

  {
    RenderLock lock(*this);
    clearDeferredReposition();
    invalidateCurrentOverlayPageCache();
    currentSpineIndex = pos.spineIndex;
    nextPageNumber = pos.pageNumber;
    section.reset();
  }
  requestUpdate();
}

ScreenshotInfo EpubReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Epub;
  if (epub) {
    snprintf(info.title, sizeof(info.title), "%s", epub->getTitle().c_str());
    info.spineIndex = currentSpineIndex;
  }
  if (section) {
    info.currentPage = section->currentPage + 1;
    info.totalPages = section->estimatedTotalPages();
    if (epub && epub->getBookSize() > 0 && section->estimatedTotalPages() > 0) {
      const float chapterProgress =
          static_cast<float>(section->currentPage) / static_cast<float>(section->estimatedTotalPages());
      int pct = static_cast<int>(epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f + 0.5f);
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      info.progressPercent = pct;
    }
  }
  return info;
}

CrossPointPosition EpubReaderActivity::getCurrentPosition() const {
  const int currentPage = section ? section->currentPage : nextPageNumber;
  const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;

  CrossPointPosition localPos{currentSpineIndex, currentPage, totalPages};
  if (section && currentPage >= 0 && currentPage < section->pageCount) {
    // Same paragraph convention as upstream's mapper: the paragraph that carries
    // into this page starts on the previous one.
    const uint16_t paragraphPage =
        currentPage > 0 ? static_cast<uint16_t>(currentPage - 1) : static_cast<uint16_t>(currentPage);
    if (const auto pIdx = section->getParagraphIndexForPage(paragraphPage)) {
      localPos.paragraphIndex = *pIdx;
      localPos.hasParagraphIndex = true;
    }
    if (const auto offset = section->getVisibleTextOffsetForPage(static_cast<uint16_t>(currentPage))) {
      localPos.visibleTextOffset = *offset;
      localPos.hasVisibleTextOffset = true;
    }
  }
  return localPos;
}

void EpubReaderActivity::launchKOReaderSync(const SyncLaunchMode mode) {
  if (!epub) {
    return;
  }

  const int currentPage = section ? section->currentPage : nextPageNumber;
  const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
  KOReaderSyncIntentState syncIntent = KOReaderSyncIntentState::COMPARE;
  if (mode == SyncLaunchMode::PULL_REMOTE) {
    syncIntent = KOReaderSyncIntentState::PULL_REMOTE;
  } else if (mode == SyncLaunchMode::PUSH_LOCAL) {
    syncIntent = KOReaderSyncIntentState::PUSH_LOCAL;
  } else if (mode == SyncLaunchMode::AUTO_PUSH) {
    syncIntent = KOReaderSyncIntentState::AUTO_PUSH;
  }

  auto& sync = APP_STATE.koReaderSyncSession;
  const CrossPointPosition localPos = getCurrentPosition();
  SavedProgressPosition localKoReaderPosition{};
  std::string localChapterLabel;
  const bool needsLocalPosition =
      syncIntent != KOReaderSyncIntentState::PULL_REMOTE && syncIntent != KOReaderSyncIntentState::AUTO_PULL;
  if (needsLocalPosition) {
    localKoReaderPosition = ProgressMapper::toSavedProgress(epub, localPos);
    const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
    localChapterLabel = (tocIndex >= 0) ? epub->getTocItem(tocIndex).title
                                        : (std::string(tr(STR_SECTION_PREFIX)) + std::to_string(currentSpineIndex + 1));
  }

  sync.active = true;
  sync.epubPath = epub->getPath();
  sync.spineIndex = currentSpineIndex;
  sync.page = currentPage;
  sync.totalPagesInSpine = totalPages;
  sync.paragraphIndex = localPos.hasParagraphIndex ? localPos.paragraphIndex : 0;
  sync.hasParagraphIndex = localPos.hasParagraphIndex;
  sync.xhtmlSeekHint = 0;
  sync.intent = syncIntent;
  sync.hasLocalKoReaderPosition = needsLocalPosition && !localKoReaderPosition.xpath.empty();
  sync.localKoReaderProgress = sync.hasLocalKoReaderPosition ? localKoReaderPosition.xpath : std::string();
  sync.localKoReaderPercentage = sync.hasLocalKoReaderPosition ? localKoReaderPosition.percentage : 0.0f;
  sync.localChapterLabel = sync.hasLocalKoReaderPosition ? localChapterLabel : std::string();
  sync.outcome = KOReaderSyncOutcomeState::PENDING;
  sync.resultSpineIndex = 0;
  sync.resultPage = 0;
  sync.resultParagraphIndex = 0;
  sync.resultHasParagraphIndex = false;
  sync.resultLiIndex = 0;
  sync.resultHasLiIndex = false;
  sync.resultHasVisibleTextOffset = false;
  sync.resultVisibleTextOffset = 0;
  sync.exitToHomeAfterSync = mode == SyncLaunchMode::AUTO_PUSH;
  sync.autoPullEpubPath.clear();
  saveProgress(currentSpineIndex, currentPage, totalPages);
  APP_STATE.saveToFile();

  LOG_DBG("ERS", "Standalone sync handoff: spine=%d page=%d/%d", currentSpineIndex, currentPage, totalPages);
  LOG_DBG("KOSync", "Releasing EPUB reader state before sync (heap before: %u)",
          static_cast<unsigned>(ESP.getFreeHeap()));
  {
    RenderLock lock(*this);
    if (section) {
      nextPageNumber = section->currentPage;
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->estimatedTotalPages();
    }
    invalidateCurrentOverlayPageCache();
    discardOverlayPage();
    section.reset();
    ImageBlock::setExtractor(nullptr, nullptr);
    epub.reset();
  }
  LOG_DBG("KOSync", "EPUB reader state released before sync (heap after: %u)",
          static_cast<unsigned>(ESP.getFreeHeap()));
  activityManager.goToKOReaderSync();
}

void EpubReaderActivity::applyPendingSyncSession() {
  auto& sync = APP_STATE.koReaderSyncSession;
  if (!sync.active || !epub || sync.epubPath != epub->getPath()) {
    return;
  }

  LOG_DBG("ERS", "Applying pending sync session outcome=%d path=%s", static_cast<int>(sync.outcome),
          sync.epubPath.c_str());

  if (sync.outcome == KOReaderSyncOutcomeState::UPLOAD_COMPLETE) {
    LOG_DBG("ERS", "Upload-complete: keeping existing progress unchanged");
    sync.clear();
    APP_STATE.saveToFile();
    return;
  }

  if (sync.intent == KOReaderSyncIntentState::AUTO_PULL && sync.outcome != KOReaderSyncOutcomeState::APPLIED_REMOTE) {
    LOG_DBG("ERS", "Auto-pull finished without a remote position; keeping local progress");
    sync.clear();
    APP_STATE.saveToFile();
    return;
  }

  int restoreSpineIndex = sync.spineIndex;
  int restorePage = sync.page;
  std::optional<uint32_t> restoreOffset;
  pendingParagraphLookup = sync.hasParagraphIndex;
  pendingParagraphIndex = sync.paragraphIndex;
  pendingListItemLookup = false;
  pendingListItemIndex = 0;

  if (sync.outcome == KOReaderSyncOutcomeState::APPLIED_REMOTE) {
    restoreSpineIndex = sync.resultSpineIndex;
    restorePage = sync.resultPage;
    if (sync.resultHasVisibleTextOffset) {
      // Content anchor from the upstream mapper: exact, re-pagination proof.
      // The paragraph / list-item hints stay as fallbacks for old records.
      restoreOffset = sync.resultVisibleTextOffset;
      pendingVisibleTextOffset = sync.resultVisibleTextOffset;
      pendingParagraphLookup = false;
      pendingListItemLookup = false;
    } else {
      pendingParagraphLookup = sync.resultHasParagraphIndex;
      pendingParagraphIndex = sync.resultParagraphIndex;
      pendingListItemLookup = sync.resultHasLiIndex;
      pendingListItemIndex = sync.resultLiIndex;
    }
    LOG_DBG("ERS", "Applying remote position: spine=%d page=%d paragraph=%u listItem=%u offset=%lu", restoreSpineIndex,
            restorePage, pendingParagraphIndex, pendingListItemIndex,
            static_cast<unsigned long>(restoreOffset.value_or(0)));
  } else {
    LOG_DBG("ERS", "Restoring local pre-sync position: spine=%d page=%d paragraph=%u", restoreSpineIndex, restorePage,
            pendingParagraphIndex);
  }

  const int restorePageCount = (restoreSpineIndex == sync.spineIndex) ? sync.totalPagesInSpine : 0;
  const std::string restoreBookId = BookIdentity::resolveStableBookId(epub->getPath());
  std::string restoreProgressPath = getStableProgressPath(restoreBookId);
  if (!restoreProgressPath.empty()) {
    BookIdentity::ensureStableDataDir(restoreBookId);
  } else {
    restoreProgressPath = getLegacyProgressPath(*epub);
  }

  if (writeReaderProgressFile(restoreProgressPath, restoreSpineIndex, restorePage, restorePageCount, restoreOffset)) {
    cachedSpineIndex = restoreSpineIndex;
    cachedChapterTotalPageCount = restorePageCount;
    LOG_DBG("ERS", "Prepared progress file for sync restore: spine=%d page=%d/%d", restoreSpineIndex, restorePage,
            sync.totalPagesInSpine);
  } else {
    currentSpineIndex = restoreSpineIndex;
    nextPageNumber = restorePage;
    cachedSpineIndex = restoreSpineIndex;
    cachedChapterTotalPageCount = restorePageCount;
  }

  sync.clear();
  APP_STATE.saveToFile();
}
