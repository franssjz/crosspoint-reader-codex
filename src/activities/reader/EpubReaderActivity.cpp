#include "EpubReaderActivity.h"

#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <MemoryBudget.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
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
#include "HighlightTextMatcher.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncActivity.h"
#include "MappedInputManager.h"
#include "ProgressFile.h"
#include "ProgressMapper.h"
#include "QrDisplayActivity.h"
#include "ReaderPosition.h"
#include "ReaderQuickSettingsActivity.h"
#include "ReaderUtils.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "SdCardFontGlobals.h"
#include "activities/apps/DictionaryActivity.h"
#include "activities/apps/ReadingStatsDetailActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/AchievementPopupUtils.h"
#include "util/BookIdentity.h"
#include "util/CompletedBookMover.h"
#include "util/ScreenshotUtil.h"

namespace {
// pagesPerRefresh now comes from SETTINGS.getRefreshFrequency()
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
  auto page = section.loadPageFromSectionFile();
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

bool writeReaderProgressCache(const std::string& cachePath, const int spineIndex, const int currentPage,
                              const int pageCount) {
  uint8_t data[6];
  data[0] = spineIndex & 0xFF;
  data[1] = (spineIndex >> 8) & 0xFF;
  data[2] = currentPage & 0xFF;
  data[3] = (currentPage >> 8) & 0xFF;
  data[4] = pageCount & 0xFF;
  data[5] = (pageCount >> 8) & 0xFF;
  return ProgressFile::writeAtomic("ERS", cachePath, data, sizeof(data));
}

bool writeReaderProgressFile(const std::string& progressPath, const int spineIndex, const int currentPage,
                             const int pageCount) {
  uint8_t data[6];
  data[0] = spineIndex & 0xFF;
  data[1] = (spineIndex >> 8) & 0xFF;
  data[2] = currentPage & 0xFF;
  data[3] = (currentPage >> 8) & 0xFF;
  data[4] = pageCount & 0xFF;
  data[5] = (pageCount >> 8) & 0xFF;
  return ProgressFile::writeAtomicPath("ERS", progressPath, data, sizeof(data));
}

struct HighlightWordRef {
  const PageLine* line = nullptr;
  const TextBlock* block = nullptr;
  uint16_t index = 0;
};

bool hasEmSpacePrefix(const char* text) {
  return text && static_cast<unsigned char>(text[0]) == 0xE2 &&
         static_cast<unsigned char>(text[1]) == 0x80 && static_cast<unsigned char>(text[2]) == 0x83;
}

HighlightTextMatcher::TokenFragmentResult matchHighlightFragment(const HighlightWordRef& ref, const char* token,
                                                                 const size_t tokenLength,
                                                                 const size_t tokenOffset) {
  const char* rawWord = ref.block->wordText(ref.index);
  const char* word = rawWord + (hasEmSpacePrefix(rawWord) ? 3 : 0);
  return HighlightTextMatcher::matchTokenFragment(word, ref.block->wordEndsWithInsertedHyphen(ref.index), token,
                                                  tokenLength, tokenOffset);
}

}  // namespace

void EpubReaderActivity::onEnter() {
  Activity::onEnter();

  if (!epub) {
    return;
  }

  ImageBlock::clearSessionRenderFailures();

  ensureSdFontLoaded();

  // Configure screen orientation based on settings
  // NOTE: This affects layout math and must be applied before any render calls.
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

  epub->setupCacheDir();
  applyPendingSyncSession();
  stableBookId = BookIdentity::resolveStableBookId(epub->getPath());
  bookmarkStore.load(epub->getCachePath(), stableBookId);

  FsFile f;
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
    uint8_t data[6];
    int dataSize = f.read(data, 6);
    if (dataSize == 4 || dataSize == 6) {
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
    if (dataSize == 6) {
      cachedChapterTotalPageCount = data[4] + (data[5] << 8);
    }
    f.close();
    if (loadedFromLegacy) {
      saveProgress(currentSpineIndex, nextPageNumber, cachedChapterTotalPageCount);
    }
  }
  // Only apply the EPUB text reference on first open; a saved position in spine 0 is valid progress.
  if (!loadedProgress && currentSpineIndex == 0) {
    int textSpineIndex = epub->getSpineIndexForTextReference();
    if (textSpineIndex != 0) {
      currentSpineIndex = textSpineIndex;
      LOG_DBG("ERS", "Opened for first time, navigating to text reference at index %d", textSpineIndex);
    }
  }

  if (initialBookmarkSpineIndex >= 0) {
    const int maxSpineIndex = std::max(0, epub->getSpineItemsCount() - 1);
    currentSpineIndex = std::min(initialBookmarkSpineIndex, maxSpineIndex);
    nextPageNumber = std::max(0, initialBookmarkPage);
    pendingVisibleTextOffset = initialBookmarkVisibleTextOffset;
    cachedSpineIndex = currentSpineIndex;
    cachedChapterTotalPageCount = 0;
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

  // Reset orientation back to portrait for the rest of the UI
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  APP_STATE.readerActivityLoadCount = 0;
  READING_STATS.endSession();
  ACHIEVEMENTS.recordSessionEnded(READING_STATS.getLastSessionSnapshot());
  bookmarkStore.save();
  invalidateCurrentOverlayPageCache();
  section.reset();
  epub.reset();
  APP_STATE.saveToFile();
}

ReaderRenderSpec EpubReaderActivity::makeRenderSpec(const uint16_t viewportWidth, const uint16_t viewportHeight) const {
  ReaderRenderSpec spec;
  spec.fontId = SETTINGS.getReaderFontId();
  spec.lineCompression = SETTINGS.getReaderLineCompression();
  spec.extraParagraphSpacing = SETTINGS.extraParagraphSpacing;
  spec.forceParagraphIndents = SETTINGS.forceParagraphIndents;
  spec.paragraphAlignment = SETTINGS.paragraphAlignment;
  spec.viewportWidth = viewportWidth;
  spec.viewportHeight = viewportHeight;
  spec.hyphenationEnabled = SETTINGS.hyphenationEnabled;
  spec.focusReadingEnabled = SETTINGS.bionicReading == CrossPointSettings::BIONIC_READING_NORMAL;
  spec.embeddedStyle = SETTINGS.embeddedStyle;
  spec.imageRendering = SETTINGS.imageRendering;
  return spec;
}

bool EpubReaderActivity::buildTickHeapGate() {
  buildHeapPaused =
      ESP.getFreeHeap() < BACKGROUND_BUILD_MIN_FREE_HEAP || ESP.getMaxAllocHeap() < BACKGROUND_BUILD_MIN_MAX_ALLOC;
  return !buildHeapPaused;
}

bool EpubReaderActivity::applyDeferredReposition() {
  if (!section || section->isBuilding() || cachedChapterTotalPageCount == 0) return false;
  bool changed = false;
  if (currentSpineIndex == cachedSpineIndex) {
    const int newPage = ReaderPosition::resolveRestoredPage(section->currentPage, cachedChapterTotalPageCount,
                                                            section->pageCount, pendingPaginationReposition);
    changed = newPage != section->currentPage;
    section->currentPage = newPage;
  }
  clearDeferredReposition();
  return changed;
}

void EpubReaderActivity::clearDeferredReposition() {
  cachedChapterTotalPageCount = 0;
  pendingPaginationReposition = false;
}

void EpubReaderActivity::loop() {
  if (!epub) {
    // Should never happen
    finish();
    return;
  }

  READING_STATS.tickActiveSession();
  const unsigned long nowMs = millis();

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

  if (automaticPageTurnActive) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      automaticPageTurnActive = false;
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

  const bool atEndOfBook = currentSpineIndex > 0 && currentSpineIndex >= epub->getSpineItemsCount();
  if (atEndOfBook && endOfBookOptions.menuActive()) {
    std::string openPath;
    switch (endOfBookOptions.handleMenuInput(mappedInput, &openPath)) {
      case EndOfBookOptions::Action::OpenBook:
        markStatsCompletedAtEnd(*epub, currentSpineIndex);
        moveCompletedBookIfEnabled();
        activityManager.goToReader(openPath);
        return;
      case EndOfBookOptions::Action::GoHome:
        markStatsCompletedAtEnd(*epub, currentSpineIndex);
        exitReaderAfterOptionalCompletedMove();
        return;
      case EndOfBookOptions::Action::LastPage:
        currentSpineIndex = std::max(epub->getSpineItemsCount() - 1, 0);
        nextPageNumber = 0;
        pendingPageJump = std::numeric_limits<uint16_t>::max();
        requestUpdate();
        return;
      case EndOfBookOptions::Action::Redraw:
        requestUpdate();
        return;
      case EndOfBookOptions::Action::None:
        break;
    }
  }

  if (ReaderUtils::shouldToggleStatusBar(mappedInput)) {
    toggleTemporaryStatusBar();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() >= bookmarkToggleMs) {
    waitingForConfirmSecondClick = false;
    firstConfirmClickMs = 0UL;
    if (section && section->currentPage >= 0 && section->currentPage < section->pageCount) {
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
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (ReaderUtils::registerConfirmDoubleClick(waitingForConfirmSecondClick, firstConfirmClickMs, nowMs)) {
      requestCurrentPageFullRefresh();
      return;
    }
  }

  // Enter reader menu activity.
  if (ReaderUtils::hasPendingConfirmSingleClickExpired(waitingForConfirmSecondClick, firstConfirmClickMs, nowMs)) {
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
    ReaderUtils::requestReaderUiTransitionRefresh(renderer);
    startActivityForResult(std::make_unique<EpubReaderMenuActivity>(
                               renderer, mappedInput, epub->getTitle(), currentPage, totalPages, bookProgressPercent,
                               SETTINGS.orientation, !currentPageFootnotes.empty()),
                           [this](const ActivityResult& result) {
                             READING_STATS.resumeSession();
                             // Always apply orientation change even if the menu was cancelled
                             const auto& menu = std::get<MenuResult>(result.data);
                             applyOrientation(menu.orientation);
                             toggleAutoPageTurn(menu.pageTurnOption);
                             if (!result.isCancelled) {
                               onReaderMenuConfirm(static_cast<EpubReaderMenuActivity::MenuAction>(menu.action));
                             }
                           });
  }

  // Long press BACK (1s+) goes to file selection
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= ReaderUtils::GO_HOME_MS) {
    const std::string fileBrowserPath = moveCompletedBookIfEnabled();
    READING_STATS.endSession();
    ACHIEVEMENTS.recordSessionEnded(READING_STATS.getLastSessionSnapshot());
    showPendingAchievementPopups(renderer);
    activityManager.goToFileBrowser(fileBrowserPath);
    return;
  }

  // Short press BACK goes directly to home (or restores position if viewing footnote)
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() < ReaderUtils::GO_HOME_MS) {
    if (footnoteDepth > 0) {
      restoreSavedPosition();
      return;
    }
    if (tryAutoPushOnClose()) {
      return;
    }
    exitReaderAfterOptionalCompletedMove();
    return;
  }

  auto [prevTriggered, nextTriggered, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);
  if (!prevTriggered && !nextTriggered) {
    return;
  }
  if (fromTilt) {
    waitingForConfirmSecondClick = false;
    firstConfirmClickMs = 0UL;
  }

  // At end of the book, forward button goes home and back button returns to last page
  if (currentSpineIndex > 0 && currentSpineIndex >= epub->getSpineItemsCount()) {
    if (endOfBookOptions.menuActive()) return;
    if (nextTriggered) {
      markStatsCompletedAtEnd(*epub, currentSpineIndex);
      if (tryAutoPushOnClose()) {
        return;
      }
      exitReaderAfterOptionalCompletedMove();
    } else {
      currentSpineIndex = epub->getSpineItemsCount() - 1;
      nextPageNumber = 0;
      pendingPageJump = std::numeric_limits<uint16_t>::max();
      requestUpdate();
    }
    return;
  }

  const bool longPress = !fromTilt && mappedInput.getHeldTime() > ReaderUtils::SKIP_HOLD_MS;

  // Don't skip chapter after screenshot
  if (gpio.wasReleased(HalGPIO::BTN_POWER) && gpio.wasReleased(HalGPIO::BTN_DOWN)) {
    return;
  }

  if (longPress && SETTINGS.longPressButtonBehavior == CrossPointSettings::LONG_PRESS_CHAPTER_SKIP) {
    READING_STATS.noteActivity();
    lastPageTurnTime = millis();

    if (!nextTriggered && section && section->currentPage > 0) {
      section->currentPage = 0;
      nextPageNumber = 0;
      sessionProgressTouched = true;
      requestUpdate();
      return;
    }

    if (!nextTriggered && currentSpineIndex <= 0) {
      return;
    }

    // We don't want to delete the section mid-render, so grab the semaphore
    {
      RenderLock lock(*this);
      nextPageNumber = 0;
      if (nextTriggered) {
        currentSpineIndex++;
      } else if (currentSpineIndex > 0) {
        pendingPageJump = std::numeric_limits<uint16_t>::max();
        currentSpineIndex--;
      }
      sessionProgressTouched = true;
      section.reset();
    }
    requestUpdate();
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

  if (prevTriggered) {
    pageTurn(false);
  } else {
    pageTurn(true);
  }
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
  if (section) {
    cachedSpineIndex = currentSpineIndex;
    cachedChapterTotalPageCount = section->estimatedTotalPages();
    pendingPaginationReposition = true;
    nextPageNumber = section->currentPage;
  }
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

  auto page = section->loadPageFromSectionFile();
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
  requestUpdate();
}

// Translate an absolute percent into a spine index plus a normalized position
// within that spine so we can jump after the section is loaded.
void EpubReaderActivity::jumpToPercent(int percent) {
  if (!epub) {
    return;
  }
  clearDeferredReposition();

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
  if (pendingSpineProgress < 0.0f) {
    pendingSpineProgress = 0.0f;
  } else if (pendingSpineProgress > 1.0f) {
    pendingSpineProgress = 1.0f;
  }

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
      SETTINGS.fontSize,
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
  const bool fontChanged = before.fontFamily != SETTINGS.fontFamily || before.fontSize != SETTINGS.fontSize ||
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
    if (section) {
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->estimatedTotalPages();
      pendingPaginationReposition = true;
      nextPageNumber = section->currentPage;
    }
    if (orientationChanged) {
      ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
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
      const std::string path = epub->getPath();
      READING_STATS.noteActivity();
      startActivityForResult(
          std::make_unique<EpubReaderChapterSelectionActivity>(renderer, mappedInput, epub, path, spineIdx),
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
      int overlayMarginLeft = 0;
      int overlayMarginTop = 0;
      auto page = loadCurrentPageForOverlay(overlayMarginLeft, overlayMarginTop);
      if (!page) {
        requestUpdate();
        break;
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
                pendingVisibleTextOffset = needsAnchorJump
                                               ? std::optional<uint32_t>(bookmark.visibleTextOffset)
                                               : std::nullopt;
                sessionProgressTouched = true;
                section.reset();
                requestUpdate();
              }
            }
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
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DISPLAY_QR: {
      if (section && section->currentPage >= 0 && section->currentPage < section->pageCount) {
        auto p = section->loadPageFromSectionFile();
        if (p) {
          std::string fullText;
          for (const auto& el : p->elements) {
            if (el->getTag() == TAG_PageLine) {
              const auto& line = static_cast<const PageLine&>(*el);
              if (line.getBlock()) {
                for (uint16_t i = 0; i < line.getBlock()->wordCount(); ++i) {
                  if (!fullText.empty()) fullText += " ";
                  fullText += line.getBlock()->wordText(i);
                }
              }
            }
          }
          if (!fullText.empty()) {
            READING_STATS.noteActivity();
            startActivityForResult(std::make_unique<QrDisplayActivity>(renderer, mappedInput, fullText),
                                   [this](const ActivityResult& result) { READING_STATS.resumeSession(); });
            break;
          }
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
          saveProgress(backupSpine, backupPage, backupPageCount);
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
      }
      break;
    }
  }
}

void EpubReaderActivity::applyOrientation(const uint8_t orientation) {
  // No-op if the selected orientation matches current settings.
  if (SETTINGS.orientation == orientation) {
    return;
  }

  // Preserve current reading position so we can restore after reflow.
  {
    RenderLock lock(*this);
    if (section) {
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->estimatedTotalPages();
      pendingPaginationReposition = true;
      nextPageNumber = section->currentPage;
    }

    // Persist the selection so the reader keeps the new orientation on next launch.
    SETTINGS.orientation = orientation;
    SETTINGS.saveToFile();

    // Update renderer orientation to match the new logical coordinate system.
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

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
    if (section) {
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->estimatedTotalPages();
      pendingPaginationReposition = true;
      nextPageNumber = section->currentPage;
    }
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
  clearDeferredReposition();
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

// TODO: Failure handling
void EpubReaderActivity::render(RenderLock&& lock) {
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
    markStatsCompletedAtEnd(*epub, currentSpineIndex);
    endOfBookOptions.loadOnce(epub->getPath());
    renderer.clearScreen();
    endOfBookOptions.render(renderer, mappedInput);
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    return;
  }

  // Apply screen viewable areas and additional padding
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += SETTINGS.screenMargin;
  orientedMarginLeft += SETTINGS.screenMargin;
  orientedMarginRight += SETTINGS.screenMargin;

  const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();

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
    const bool needsLookup = pendingVisibleTextOffset.has_value() || !pendingAnchor.empty() || pendingParagraphLookup ||
                             pendingListItemLookup;
    const bool lookupAlreadyAvailable =
        (pendingVisibleTextOffset && section->getPageForVisibleTextOffset(*pendingVisibleTextOffset).has_value()) ||
        (!pendingAnchor.empty() && section->findAnchor(pendingAnchor).has_value()) ||
        (pendingListItemLookup && section->getPageForListItemIndex(pendingListItemIndex).has_value()) ||
        (pendingParagraphLookup && section->getPageForParagraphIndex(pendingParagraphIndex).has_value());
    const bool mustBuildNow =
        !cacheComplete && (!targetAlreadyAvailable || pendingPercentJump || (needsLookup && !lookupAlreadyAvailable));

    if (mustBuildNow) {
      const size_t spineBytes = epub->getCumulativeSpineItemSize(currentSpineIndex) -
                                (currentSpineIndex > 0 ? epub->getCumulativeSpineItemSize(currentSpineIndex - 1) : 0);
      const bool showPopup =
          pendingPercentJump || targetPage > 20 || (!section->hasHtmlCache() && spineBytes > 96 * 1024) || needsLookup;
      if (showPopup) GUI.drawPopup(renderer, tr(STR_INDEXING));

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

      const auto lookupResolved = [this]() {
        if (pendingVisibleTextOffset && section->getPageForVisibleTextOffset(*pendingVisibleTextOffset)) return true;
        if (!pendingAnchor.empty() && section->findAnchor(pendingAnchor)) return true;
        if (pendingListItemLookup && section->getPageForListItemIndex(pendingListItemIndex)) return true;
        if (pendingParagraphLookup && section->getPageForParagraphIndex(pendingParagraphIndex)) return true;
        return false;
      };
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
  }
  if (!section->isBuilding() && section->pageCount > 0 &&
      section->currentPage >= static_cast<int>(section->pageCount)) {
    section->currentPage = section->pageCount - 1;
  }
  applyDeferredReposition();

  renderer.clearScreen();

  if (section->pageCount == 0) {
    LOG_DBG("ERS", "No pages to render");
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_CHAPTER), true, EpdFontFamily::BOLD);
    renderStatusBar();
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    return;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
    LOG_DBG("ERS", "Page out of bounds: %d (max %d)", section->currentPage, section->pageCount);
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_OUT_OF_BOUNDS), true, EpdFontFamily::BOLD);
    renderStatusBar();
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    return;
  }

  {
    auto loadedPage = section->loadPageFromSectionFile();
    if (!loadedPage) {
      LOG_ERR("ERS", "Failed to load page from SD - clearing section cache");
      section->abandonBuild();
      section->clearCache();
      section.reset();
      requestUpdate();  // Try again after clearing cache
                        // TODO: prevent infinite loop if the page keeps failing to load for some reason
      automaticPageTurnActive = false;
      return;
    }
    auto page = std::shared_ptr<Page>(std::move(loadedPage));

    // Collect footnotes from the loaded page
    currentPageFootnotes = std::move(page->footnotes);
    cacheCurrentPageForOverlay(page, orientedMarginLeft, orientedMarginTop);

    const auto start = millis();
    renderContents(page, orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    LOG_DBG("ERS", "Rendered page in %dms", millis() - start);
  }
  // Menus, screenshots and overlays can request a render without moving the
  // reader. Avoid several FAT operations for the same six-byte position file.
  if (currentSpineIndex != lastSavedSpineIndex || section->currentPage != lastSavedPage ||
      section->estimatedTotalPages() != lastSavedPageCount) {
    saveProgress(currentSpineIndex, section->currentPage, section->estimatedTotalPages());
  }

  if (pendingScreenshot) {
    pendingScreenshot = false;
    ScreenshotUtil::takeScreenshot(renderer);
  }
}

bool EpubReaderActivity::saveProgress(int spineIndex, int currentPage, int pageCount) {
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

  std::string progressPath = getStableProgressPath(stableBookId);
  if (!progressPath.empty()) {
    BookIdentity::ensureStableDataDir(stableBookId);
  } else {
    progressPath = getLegacyProgressPath(*epub);
  }
  if (writeReaderProgressFile(progressPath, spineIndex, currentPage, pageCount)) {
    LOG_DBG("ERS", "Progress saved: Chapter %d, Page %d", spineIndex, currentPage);
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
  auto* fcm = renderer.getFontCacheManager();
  fcm->resetStats();

  // Font prewarm: scan pass accumulates text, then prewarm, then real render
  const auto heapBefore = MemoryBudget::snapshot();
  auto scope = fcm->createPrewarmScope();
  page->recordFontUsage(*fcm, SETTINGS.getReaderFontId(), SETTINGS.bionicReading);
  scope.endScanAndPrewarm();
  const auto heapAfter = MemoryBudget::snapshot();
  fcm->logStats("prewarm");
  const auto tPrewarm = millis();

  LOG_DBG("ERS", "Heap prewarm: free=%u->%u delta=%ld maxAlloc=%u->%u delta=%ld", heapBefore.freeHeap,
          heapAfter.freeHeap, static_cast<int32_t>(heapAfter.freeHeap) - static_cast<int32_t>(heapBefore.freeHeap),
          heapBefore.maxAllocHeap, heapAfter.maxAllocHeap,
          static_cast<int32_t>(heapAfter.maxAllocHeap) - static_cast<int32_t>(heapBefore.maxAllocHeap));

  if (page->hasImagesNeedingDecode()) {
    page->renderWithImagePlaceholders(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop,
                                      SETTINGS.bionicReading);
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
  bool imagePageWithAA = page->hasImages() && enableTextAA;
  HalDisplay::RefreshMode configuredRefreshMode = HalDisplay::FAST_REFRESH;
  const bool hasConfiguredRefreshMode = ReaderUtils::getConfiguredReaderRefreshMode(configuredRefreshMode);

  // Decide the tone calibration before anything decodes an image: the factory
  // path only runs when no refresh mode is forced, and image pixels are cached
  // quantized for whichever waveform is in play.
  const bool willUseFactoryGray = imagePageWithAA && !forceFullRefresh && !hasConfiguredRefreshMode;
  renderer.setImageToneMode(willUseFactoryGray ? GfxRenderer::GrayscaleMode::FactoryQuality
                                               : GfxRenderer::GrayscaleMode::Differential);

  page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop, SETTINGS.bionicReading);
  drawTextHighlights(*page, orientedMarginTop, orientedMarginLeft);
  renderStatusBar();
  fcm->logStats("bw_render");
  const auto tBwRender = millis();

  if (forceFullRefresh) {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh, true);
  } else if (hasConfiguredRefreshMode) {
    renderer.displayBuffer(configuredRefreshMode);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else if (imagePageWithAA) {
    // Factory absolute 2-bit grayscale (zgredex technique): white pre-flash,
    // then the whole page — text and images — is driven by the OEM factory
    // LUT in one pass. Better tonal range than the differential overlay and
    // no double-fast blanking dance. Falls through to the differential path
    // below only if the BW backup allocation fails.
    if (renderFactoryGrayPage(*page, orientedMarginLeft, orientedMarginTop)) {
      // Factory pass leaves gray charge a plain fast diff on the next page
      // can't clear (#2190) — force the HALF ghost-cleanup path next.
      pagesUntilFullRefresh = 1;
      fcm->logStats("gray_factory_quality");
      const auto tEnd = millis();
      LOG_DBG("ERS", "Page render (factory): prewarm=%lums bw_render=%lums factory=%lums total=%lums",
              tPrewarm - t0, tBwRender - tPrewarm, tEnd - tBwRender, tEnd - t0);
      return;
    }
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
      page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop, SETTINGS.bionicReading);
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

  const bool needsGrayscale = enableTextAA || enableImageGrayscaleOnly;
  ReaderUtils::TiledGrayscaleTimings tiledTimings;
  const bool tiledGrayscale =
      needsGrayscale && ReaderUtils::renderTiledGrayscale(
                            renderer, "ERS",
                            [&]() {
                              if (enableImageGrayscaleOnly) {
                                page->renderImages(renderer, orientedMarginLeft, orientedMarginTop);
                              } else {
                                page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft,
                                             orientedMarginTop, SETTINGS.bionicReading);
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
  }

  // grayscale rendering
  // TODO: Only do this if font supports it
  if (needsGrayscale && storedBwBuffer) {
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    if (enableImageGrayscaleOnly) {
      page->renderImages(renderer, orientedMarginLeft, orientedMarginTop);
    } else {
      page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop, SETTINGS.bionicReading);
    }
    drawTextHighlights(*page, orientedMarginTop, orientedMarginLeft);
    renderStatusBar();
    renderer.copyGrayscaleLsbBuffers();
    const auto tGrayLsb = millis();

    // Render and copy to MSB buffer
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    if (enableImageGrayscaleOnly) {
      page->renderImages(renderer, orientedMarginLeft, orientedMarginTop);
    } else {
      page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop, SETTINGS.bionicReading);
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

bool EpubReaderActivity::renderFactoryGrayPage(Page& page, const int orientedMarginLeft,
                                               const int orientedMarginTop) {
  // Preserve the BW page across the factory render: the pre-flash clears the
  // framebuffer, and the controller RAM must be re-synced to the BW page
  // afterwards (the factory pass leaves both RAM planes gray-encoded).
  if (!renderer.storeBwBuffer()) {
    LOG_ERR("ERS", "Factory gray: BW backup alloc failed, using differential path");
    return false;
  }

  // Pre-flash to white so the factory LUT drives particles from a known
  // state — from white only downward transitions are needed, which the LUT
  // handles cleanly. Must be HALF_REFRESH: BYPASS_RED guarantees true white
  // regardless of RED RAM sync state; FAST_REFRESH diffs against RED RAM,
  // which may be stale after any prior grayscale operation.
  renderer.clearNextRefreshOverride();  // a pending override must not hijack the pre-flash
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);

  // The factory pass is absolute: it replaces the whole screen from the GRAY2
  // planes, so everything drawn in the BW base — including highlights — must
  // be re-rendered here or it disappears.
  const auto renderFn = [&]() {
    page.render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop, SETTINGS.bionicReading);
    drawTextHighlights(page, orientedMarginTop, orientedMarginLeft);
    renderStatusBar();
  };

  if (!ReaderUtils::renderTiledGrayscale(renderer, "ERS", renderFn, nullptr,
                                         GfxRenderer::GrayscaleMode::FactoryQuality,
                                         /*syncControllerAfter=*/false)) {
    // Strip scratch unavailable: render the planes in the framebuffer itself
    // (safe — the BW page is held in the chunked backup).
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAY2_LSB);
    renderFn();
    renderer.copyGrayscaleLsbBuffers();

    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAY2_MSB);
    renderFn();
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer(GfxRenderer::grayscaleLutFor(GfxRenderer::GrayscaleMode::FactoryQuality), true);
    renderer.setRenderMode(GfxRenderer::BW);
  }

  // Restore the BW page into the framebuffer and sync controller RAM to it so
  // the next BW page turn diffs against the right base.
  renderer.restoreBwBuffer();
  return true;
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

  if (automaticPageTurnActive) {
    title = tr(STR_AUTO_TURN_ENABLED) + std::to_string(60 * 1000 / pageTurnDuration);

    // calculates textYOffset when rendering title in status bar
    const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();

    // offsets text if no status bar or progress bar only
    if (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight()) {
      textYOffset += UITheme::getInstance().getMetrics().statusBarVerticalMargin;
    }

  } else if (SETTINGS.statusBarTitle == CrossPointSettings::STATUS_BAR_TITLE::CHAPTER_TITLE) {
    title = tr(STR_UNNAMED);
    const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
    if (tocIndex != -1) {
      const auto tocItem = epub->getTocItem(tocIndex);
      title = tocItem.title;
    }

  } else if (SETTINGS.statusBarTitle == CrossPointSettings::STATUS_BAR_TITLE::BOOK_TITLE) {
    title = epub->getTitle();
  }

  GUI.drawStatusBar(renderer, bookProgress, currentPage, pageCount, title, 0, textYOffset);
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
  KOReaderPosition localKoReaderPosition{};
  std::string localChapterLabel;
  const bool needsLocalPosition =
      syncIntent != KOReaderSyncIntentState::PULL_REMOTE && syncIntent != KOReaderSyncIntentState::AUTO_PULL;
  if (needsLocalPosition) {
    CrossPointPosition localPos = {currentSpineIndex, currentPage, totalPages, 0, false};
    if (section) {
      if (const auto pIdx = section->getParagraphIndexForPage(static_cast<uint16_t>(currentPage))) {
        localPos.paragraphIndex = *pIdx;
        localPos.hasParagraphIndex = true;
        if (const auto hint = section->getXhtmlByteOffsetForPage(static_cast<uint16_t>(currentPage))) {
          localPos.xhtmlSeekHint = *hint;
        }
      }
    }
    localKoReaderPosition = ProgressMapper::toKOReader(epub, localPos);
    const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
    localChapterLabel = (tocIndex >= 0) ? epub->getTocItem(tocIndex).title
                                        : (std::string(tr(STR_SECTION_PREFIX)) + std::to_string(currentSpineIndex + 1));
  }

  sync.active = true;
  sync.epubPath = epub->getPath();
  sync.spineIndex = currentSpineIndex;
  sync.page = currentPage;
  sync.totalPagesInSpine = totalPages;
  if (section) {
    if (const auto pIdx = section->getParagraphIndexForPage(static_cast<uint16_t>(currentPage))) {
      sync.paragraphIndex = *pIdx;
      sync.hasParagraphIndex = true;
      if (const auto hint = section->getXhtmlByteOffsetForPage(static_cast<uint16_t>(currentPage))) {
        sync.xhtmlSeekHint = *hint;
      } else {
        sync.xhtmlSeekHint = 0;
      }
    } else {
      sync.paragraphIndex = 0;
      sync.hasParagraphIndex = false;
      sync.xhtmlSeekHint = 0;
    }
  } else {
    sync.paragraphIndex = 0;
    sync.hasParagraphIndex = false;
    sync.xhtmlSeekHint = 0;
  }
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
  sync.resultListItemIndex = 0;
  sync.resultHasListItemIndex = false;
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
    section.reset();
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
  pendingParagraphLookup = sync.hasParagraphIndex;
  pendingParagraphIndex = sync.paragraphIndex;
  pendingListItemLookup = false;
  pendingListItemIndex = 0;

  if (sync.outcome == KOReaderSyncOutcomeState::APPLIED_REMOTE) {
    restoreSpineIndex = sync.resultSpineIndex;
    restorePage = sync.resultPage;
    pendingParagraphLookup = sync.resultHasParagraphIndex;
    pendingParagraphIndex = sync.resultParagraphIndex;
    pendingListItemLookup = sync.resultHasListItemIndex;
    pendingListItemIndex = sync.resultListItemIndex;
    LOG_DBG("ERS", "Applying remote position: spine=%d page=%d paragraph=%u listItem=%u", restoreSpineIndex,
            restorePage, pendingParagraphIndex, pendingListItemIndex);
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

  if (writeReaderProgressFile(restoreProgressPath, restoreSpineIndex, restorePage, restorePageCount)) {
    cachedSpineIndex = restoreSpineIndex;
    cachedChapterTotalPageCount = restorePageCount;
    LOG_DBG("ERS", "Prepared progress.bin for sync restore: spine=%d page=%d/%d", restoreSpineIndex, restorePage,
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
