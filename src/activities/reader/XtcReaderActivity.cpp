/**
 * XtcReaderActivity.cpp
 *
 * XTC ebook reader activity implementation
 * Displays pre-rendered XTC pages on e-ink display
 */

#include "XtcReaderActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Xtc/XtcBitmapUtils.h>

#include "AchievementsStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "ProgressFile.h"
#include "ReaderUtils.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "XtcReaderChapterSelectionActivity.h"
#include "activities/apps/ReadingStatsDetailActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/AchievementPopupUtils.h"
#include "util/BookIdentity.h"
#include "util/CompletedBookMover.h"

namespace {
constexpr unsigned long goHomeMs = 1000;

std::string getStableProgressPath(const std::string& bookId) {
  return BookIdentity::getStableDataFilePath(bookId, "xtc_progress.bin");
}

std::string getLegacyProgressPath(Xtc& xtc) { return xtc.getCachePath() + "/progress.bin"; }

const xtc::ChapterInfo* findCurrentChapter(Xtc& xtc, const uint32_t currentPage) {
  if (!xtc.hasChapters()) {
    return nullptr;
  }

  const auto& chapters = xtc.getChapters();
  for (const auto& chapter : chapters) {
    if (currentPage >= chapter.startPage && currentPage <= chapter.endPage) {
      return &chapter;
    }
  }
  return nullptr;
}

std::string getChapterTitleForStats(Xtc& xtc, const uint32_t currentPage) {
  const auto* chapter = findCurrentChapter(xtc, currentPage);
  if (!chapter) {
    return "";
  }
  return chapter->name;
}

uint8_t getChapterProgressForStats(Xtc& xtc, const uint32_t currentPage) {
  const auto* chapter = findCurrentChapter(xtc, currentPage);
  if (!chapter || chapter->endPage < chapter->startPage) {
    return 0;
  }

  const uint32_t chapterLength = static_cast<uint32_t>(chapter->endPage - chapter->startPage + 1);
  if (chapterLength == 0) {
    return 0;
  }

  const uint32_t pageOffset = static_cast<uint32_t>(currentPage - chapter->startPage + 1);
  return static_cast<uint8_t>(std::min<uint32_t>(100, (pageOffset * 100 + chapterLength / 2) / chapterLength));
}

void markStatsCompletedAtEnd(Xtc& xtc) {
  if (xtc.getPageCount() == 0) {
    READING_STATS.updateProgress(100, true, "", 100);
    return;
  }

  const uint32_t lastPage = xtc.getPageCount() - 1;
  READING_STATS.updateProgress(100, true, getChapterTitleForStats(xtc, lastPage), 100);
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
bool streamXtchRenderPass(const Xtc& book, const uint32_t pageIndex, const uint16_t width, const uint16_t height,
                          GfxRenderer& renderer, const xtc::XtchRenderPass pass) {
  struct Context {
    GfxRenderer& renderer;
    uint16_t width;
    uint16_t height;
    xtc::XtchRenderPass pass;
  } context{renderer, width, height, pass};
  const auto callback = [](void* raw, const uint8_t* first, const uint8_t* second, const size_t size,
                           const size_t offset) {
    auto& ctx = *static_cast<Context*>(raw);
    const size_t columnBytes = (ctx.height + 7U) / 8U;
    for (size_t i = 0; i < size; ++i) {
      const size_t column = (offset + i) / columnBytes;
      if (column >= ctx.width) continue;
      const uint16_t x = static_cast<uint16_t>(ctx.width - 1 - column);
      const uint16_t yBase = static_cast<uint16_t>(((offset + i) % columnBytes) * 8);
      const uint8_t mask = xtc::xtchPassMask(first[i], second[i], ctx.pass);
      for (uint8_t bit = 0; bit < 8 && yBase + bit < ctx.height; ++bit) {
        if ((mask >> (7 - bit)) & 1) ctx.renderer.drawPixel(x, yBase + bit, ctx.pass == xtc::XtchRenderPass::Base);
      }
    }
  };
  const auto error = book.loadPagePlanePairs(pageIndex, callback, &context);
  if (error == xtc::XtcError::OK) return true;
  LOG_ERR("XTR", "Failed to stream XTCH page %lu: %s", pageIndex, xtc::errorToString(error));
  return false;
}
}  // namespace

void XtcReaderActivity::onEnter() {
  Activity::onEnter();

  if (!xtc) {
    return;
  }

  xtc->setupCacheDir();
  stableBookId = BookIdentity::resolveStableBookId(xtc->getPath());

  // Load saved progress
  loadProgress();
  if (progressPersistenceBlocked) {
    activityManager.goToFullScreenMessage(tr(STR_PAGE_LOAD_ERROR));
    return;
  }

  // Save current XTC as last opened book and add to recent books
  APP_STATE.openEpubPath = xtc->getPath();
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(xtc->getPath(), xtc->getTitle(), xtc->getAuthor(), xtc->getThumbBmpPath(), stableBookId);
  READING_STATS.beginSession(xtc->getPath(), xtc->getTitle(), xtc->getAuthor(), xtc->getCoverBmpPath(),
                             xtc->calculateProgress(currentPage), getChapterTitleForStats(*xtc, currentPage),
                             getChapterProgressForStats(*xtc, currentPage));

  // Trigger first update
  requestUpdate();
}

void XtcReaderActivity::onExit() {
  Activity::onExit();

  ReaderUtils::requestReaderUiTransitionRefresh(renderer);

  APP_STATE.readerActivityLoadCount = 0;
  READING_STATS.endSession();
  ACHIEVEMENTS.recordSessionEnded(READING_STATS.getLastSessionSnapshot());
  xtc.reset();
  APP_STATE.saveToFile();
}

void XtcReaderActivity::loop() {
  RenderLock lock(*this);
  READING_STATS.tickActiveSession();
  if (!xtc) {
    return;
  }

  const unsigned long nowMs = millis();

  const bool atEndOfBook = currentPage >= xtc->getPageCount();
  if (atEndOfBook && endOfBookOptions.menuActive()) {
    std::string openPath;
    switch (endOfBookOptions.handleMenuInput(mappedInput, &openPath)) {
      case EndOfBookOptions::Action::OpenBook:
        markStatsCompletedAtEnd(*xtc);
        lock.unlock();
        moveCompletedBookIfEnabled();
        activityManager.goToReader(openPath);
        return;
      case EndOfBookOptions::Action::GoHome:
        markStatsCompletedAtEnd(*xtc);
        lock.unlock();
        exitReaderAfterOptionalCompletedMove();
        return;
      case EndOfBookOptions::Action::LastPage:
        currentPage = xtc->getPageCount() > 0 ? xtc->getPageCount() - 1 : 0;
        requestUpdate();
        return;
      case EndOfBookOptions::Action::Redraw:
        requestUpdate();
        return;
      case EndOfBookOptions::Action::None:
        break;
    }
  }

  if (waitingForConfirmSecondClick && ReaderUtils::hasNonConfirmNavigationInput(mappedInput)) {
    waitingForConfirmSecondClick = false;
    firstConfirmClickMs = 0UL;
  }

  // Enter chapter selection activity
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (ReaderUtils::registerConfirmDoubleClick(waitingForConfirmSecondClick, firstConfirmClickMs, nowMs)) {
      requestCurrentPageFullRefresh();
      return;
    }
  }

  if (ReaderUtils::hasPendingConfirmSingleClickExpired(waitingForConfirmSecondClick, firstConfirmClickMs, nowMs)) {
    waitingForConfirmSecondClick = false;
    firstConfirmClickMs = 0UL;
    if (xtc && xtc->hasChapters() && !xtc->getChapters().empty()) {
      READING_STATS.noteActivity();
      ReaderUtils::requestReaderUiTransitionRefresh(renderer);
      auto chapterSelection =
          std::make_unique<XtcReaderChapterSelectionActivity>(renderer, mappedInput, xtc, currentPage);
      lock.unlock();
      startActivityForResult(std::move(chapterSelection), [this](const ActivityResult& result) {
        RenderLock resultLock(*this);
        READING_STATS.resumeSession();
        if (!result.isCancelled) {
          currentPage = std::get<PageResult>(result.data).page;
        }
      });
      return;
    }
  }

  // Long press BACK (1s+) goes to file selection
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= goHomeMs) {
    lock.unlock();
    const std::string fileBrowserPath = moveCompletedBookIfEnabled();
    READING_STATS.endSession();
    ACHIEVEMENTS.recordSessionEnded(READING_STATS.getLastSessionSnapshot());
    showPendingAchievementPopups(renderer);
    activityManager.goToFileBrowser(fileBrowserPath);
    return;
  }

  // Short press BACK goes directly to home
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && mappedInput.getHeldTime() < goHomeMs) {
    lock.unlock();
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
  if (currentPage >= xtc->getPageCount()) {
    if (endOfBookOptions.menuActive()) return;
    if (nextTriggered) {
      lock.unlock();
      exitReaderAfterOptionalCompletedMove();
    } else {
      currentPage = xtc->getPageCount() > 0 ? xtc->getPageCount() - 1 : 0;
      requestUpdate();
    }
    return;
  }

  const bool skipPages = !fromTilt && SETTINGS.longPressButtonBehavior == CrossPointSettings::LONG_PRESS_CHAPTER_SKIP &&
                         mappedInput.getHeldTime() > ReaderUtils::SKIP_HOLD_MS;
  const int skipAmount = skipPages ? 10 : 1;

  if (prevTriggered) {
    READING_STATS.noteActivity();
    if (currentPage >= static_cast<uint32_t>(skipAmount)) {
      currentPage -= skipAmount;
    } else {
      currentPage = 0;
    }
    requestUpdate();
  } else if (nextTriggered) {
    READING_STATS.noteActivity();
    currentPage += skipAmount;
    if (currentPage >= xtc->getPageCount()) {
      currentPage = xtc->getPageCount();  // Allow showing "End of book"
    }
    requestUpdate();
  }
}

void XtcReaderActivity::requestCurrentPageFullRefresh() {
  READING_STATS.noteActivity();
  pendingForceFullRefresh = true;
  requestUpdate();
}

std::string XtcReaderActivity::moveCompletedBookIfEnabled() {
  RenderLock lock(*this);
  // Stop queued redraws before releasing parser data or ending the session.
  exitingReader = true;
  if (!xtc) {
    return "";
  }

  const std::string sourcePath = xtc->getPath();
  if (!SETTINGS.moveCompletedBooks) {
    return sourcePath;
  }

  const auto* statsBook = READING_STATS.findBook(!stableBookId.empty() ? stableBookId : sourcePath);
  if (!statsBook || !statsBook->completed) {
    return sourcePath;
  }

  const std::string title = xtc->getTitle();
  const std::string author = xtc->getAuthor();
  const std::string coverBmpPath = xtc->getCoverBmpPath();
  xtc.reset();

  const auto moveResult =
      CompletedBookMover::moveCompletedBookIfEnabled(sourcePath, title, author, coverBmpPath, stableBookId);
  return moveResult.moved ? moveResult.destinationPath : sourcePath;
}

void XtcReaderActivity::exitReaderAfterOptionalCompletedMove() {
  const std::string exitPath = moveCompletedBookIfEnabled();
  exitReaderToHomeOrStats(renderer, mappedInput, exitPath);
}

void XtcReaderActivity::render(RenderLock&&) {
  if (!xtc || exitingReader) {
    return;
  }

  const uint32_t pageToRender = currentPage;
  // Bounds check
  if (pageToRender >= xtc->getPageCount()) {
    // Show end of book screen
    markStatsCompletedAtEnd(*xtc);
    endOfBookOptions.loadOnce(xtc->getPath());
    renderer.clearScreen();
    endOfBookOptions.render(renderer, mappedInput);
    renderer.displayBuffer();
    return;
  }

  // A failed page must never overwrite the previous valid progress or statistics.
  if (renderPage(pageToRender)) saveProgress(pageToRender);
}

bool XtcReaderActivity::renderPage(const uint32_t pageToRender) {
  struct DarkModeScope {
    GfxRenderer& renderer;
    bool restore;
    ~DarkModeScope() {
      if (restore) {
        renderer.setDarkMode(true);
      }
    }
  };

  const bool restoreDarkMode = renderer.isDarkMode();
  if (restoreDarkMode) {
    renderer.setDarkMode(false);
  }
  DarkModeScope darkModeScope{renderer, restoreDarkMode};
  const bool forceFullRefresh = pendingForceFullRefresh;
  pendingForceFullRefresh = false;

  const uint16_t pageWidth = xtc->getPageWidth();
  const uint16_t pageHeight = xtc->getPageHeight();
  const uint8_t bitDepth = xtc->getBitDepth();

  if (bitDepth == 1) {
    renderer.clearScreen();

    struct PageContext {
      GfxRenderer& renderer;
      uint16_t width;
      uint16_t height;
    } context{renderer, pageWidth, pageHeight};
    const auto drawChunk = [](void* raw, const uint8_t* data, const size_t size, const size_t offset) {
      auto& ctx = *static_cast<PageContext*>(raw);
      const size_t rowBytes = (ctx.width + 7U) / 8U;
      for (size_t i = 0; i < size; ++i) {
        const size_t srcY = (offset + i) / rowBytes;
        if (srcY >= ctx.height) continue;
        const size_t baseX = ((offset + i) % rowBytes) * 8;
        for (uint8_t bit = 0; bit < 8 && baseX + bit < ctx.width; ++bit) {
          if (((data[i] >> (7 - bit)) & 1) == 0) ctx.renderer.drawPixel(baseX + bit, srcY, true);
        }
      }
    };
    const auto streamResult = xtc->loadPageStreaming(pageToRender, drawChunk, &context);

    if (streamResult != xtc::XtcError::OK) {
      LOG_ERR("XTR", "Failed to stream page %lu: bitDepth=%u error=%s", pageToRender, bitDepth,
              xtc::errorToString(streamResult));
      renderer.clearScreen();
      renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_PAGE_LOAD_ERROR), true, EpdFontFamily::BOLD);
      renderer.displayBuffer();
      return false;
    }

    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh, forceFullRefresh);
    LOG_DBG("XTR", "Rendered page %lu/%lu (1-bit streaming)", pageToRender + 1, xtc->getPageCount());
    return true;
  }

  if (bitDepth != 2) return false;
  const auto showStreamError = [&]() {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_PAGE_LOAD_ERROR), true,
                              EpdFontFamily::BOLD);
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    renderer.cleanupGrayscaleWithFrameBuffer();
    pagesUntilFullRefresh = 0;
    return false;
  };
  // A reusable 1 KiB parser buffer replaces the 96 KiB XTCH page allocation.
  // Re-read the paired planes for each pass and restore the BW framebuffer last.
  renderer.clearScreen();
  if (!streamXtchRenderPass(*xtc, pageToRender, pageWidth, pageHeight, renderer, xtc::XtchRenderPass::Base)) {
    return showStreamError();
  }
  HalDisplay::RefreshMode configuredRefreshMode = HalDisplay::FAST_REFRESH;
  const bool hasConfiguredRefreshMode = ReaderUtils::getConfiguredReaderRefreshMode(configuredRefreshMode);
  if (forceFullRefresh) {
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    renderer.preconditionGrayscale();
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else if (hasConfiguredRefreshMode) {
    renderer.displayBuffer(configuredRefreshMode);
    renderer.preconditionGrayscale();
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else if (pagesUntilFullRefresh <= 1) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    renderer.preconditionGrayscale();
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
    --pagesUntilFullRefresh;
  }
  renderer.clearScreen(0x00);
  if (!streamXtchRenderPass(*xtc, pageToRender, pageWidth, pageHeight, renderer, xtc::XtchRenderPass::Lsb)) {
    return showStreamError();
  }
  renderer.copyGrayscaleLsbBuffers();
  renderer.clearScreen(0x00);
  if (!streamXtchRenderPass(*xtc, pageToRender, pageWidth, pageHeight, renderer, xtc::XtchRenderPass::Msb)) {
    return showStreamError();
  }
  renderer.copyGrayscaleMsbBuffers();
  renderer.displayGrayBuffer();
  renderer.clearScreen();
  if (!streamXtchRenderPass(*xtc, pageToRender, pageWidth, pageHeight, renderer, xtc::XtchRenderPass::Base)) {
    return showStreamError();
  }
  renderer.cleanupGrayscaleWithFrameBuffer();
  LOG_DBG("XTR", "Rendered page %lu/%lu (2-bit streaming)", pageToRender + 1, xtc->getPageCount());
  return true;
}

void XtcReaderActivity::saveProgress(const uint32_t pageToSave, const bool updateStats) const {
  if (progressPersistenceBlocked) return;
  if (updateStats) {
    READING_STATS.updateProgress(xtc->calculateProgress(pageToSave), pageToSave + 1 >= xtc->getPageCount(),
                                 getChapterTitleForStats(*xtc, pageToSave),
                                 getChapterProgressForStats(*xtc, pageToSave));
  }

  std::string progressPath = getStableProgressPath(stableBookId);
  if (!progressPath.empty()) {
    BookIdentity::ensureStableDataDir(stableBookId);
  } else {
    progressPath = getLegacyProgressPath(*xtc);
  }
  uint8_t data[4];
  data[0] = pageToSave & 0xFF;
  data[1] = (pageToSave >> 8) & 0xFF;
  data[2] = (pageToSave >> 16) & 0xFF;
  data[3] = (pageToSave >> 24) & 0xFF;
  ProgressFile::writeAtomicPath("XTR", progressPath, data, sizeof(data));
}

void XtcReaderActivity::loadProgress() {
  FsFile f;
  bool loadedFromLegacy = false;
  const std::string stableProgressPath = getStableProgressPath(stableBookId);
  const std::string legacyProgressPath = getLegacyProgressPath(*xtc);
  progressPersistenceBlocked = !ProgressFile::recover(stableProgressPath);
  if (!progressPersistenceBlocked && (stableProgressPath.empty() || !Storage.exists(stableProgressPath.c_str()))) {
    progressPersistenceBlocked = !ProgressFile::recover(legacyProgressPath);
  }
  const std::string progressPath = (!stableProgressPath.empty() && Storage.exists(stableProgressPath.c_str()))
                                       ? stableProgressPath
                                       : legacyProgressPath;
  if (progressPath == legacyProgressPath) {
    loadedFromLegacy = !stableProgressPath.empty() && Storage.exists(legacyProgressPath.c_str());
  }
  if (!progressPersistenceBlocked && Storage.openFileForRead("XTR", progressPath, f)) {
    uint8_t data[4];
    const int readSize = f.read(data, 4);
    progressPersistenceBlocked = readSize != 4;
    if (!progressPersistenceBlocked) {
      currentPage = static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
                    (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
      LOG_DBG("XTR", "Loaded progress: page %lu", currentPage);

      // Validate page number
      if (currentPage >= xtc->getPageCount()) {
        currentPage = 0;
      }
    }
    f.close();
    if (loadedFromLegacy && !progressPersistenceBlocked) {
      saveProgress(currentPage, false);
    }
  } else if (Storage.exists(progressPath.c_str())) {
    progressPersistenceBlocked = true;
  }
}

ScreenshotInfo XtcReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Xtc;
  if (xtc) {
    const std::string t = xtc->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
    const uint32_t pageCount = xtc->getPageCount();
    info.totalPages = pageCount;
    // Clamp to last valid page to avoid sentinel value (currentPage == pageCount)
    uint32_t clampedPage = (pageCount > 0 && currentPage >= pageCount) ? pageCount - 1 : currentPage;
    info.progressPercent = pageCount > 0 ? xtc->calculateProgress(clampedPage) : 0;
    info.currentPage = static_cast<int>(clampedPage) + 1;
  } else {
    info.currentPage = currentPage + 1;
  }
  return info;
}
