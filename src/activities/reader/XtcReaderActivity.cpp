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
        moveCompletedBookIfEnabled();
        activityManager.goToReader(openPath);
        return;
      case EndOfBookOptions::Action::GoHome:
        markStatsCompletedAtEnd(*xtc);
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
      startActivityForResult(
          std::make_unique<XtcReaderChapterSelectionActivity>(renderer, mappedInput, xtc, currentPage),
          [this](const ActivityResult& result) {
            READING_STATS.resumeSession();
            if (!result.isCancelled) {
              currentPage = std::get<PageResult>(result.data).page;
            }
          });
    }
  }

  // Long press BACK (1s+) goes to file selection
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= goHomeMs) {
    const std::string fileBrowserPath = moveCompletedBookIfEnabled();
    READING_STATS.endSession();
    ACHIEVEMENTS.recordSessionEnded(READING_STATS.getLastSessionSnapshot());
    showPendingAchievementPopups(renderer);
    activityManager.goToFileBrowser(fileBrowserPath);
    return;
  }

  // Short press BACK goes directly to home
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && mappedInput.getHeldTime() < goHomeMs) {
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
      exitReaderAfterOptionalCompletedMove();
    } else {
      currentPage = xtc->getPageCount() - 1;
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
  if (!xtc) {
    return;
  }

  // Bounds check
  if (currentPage >= xtc->getPageCount()) {
    // Show end of book screen
    markStatsCompletedAtEnd(*xtc);
    endOfBookOptions.loadOnce(xtc->getPath());
    renderer.clearScreen();
    endOfBookOptions.render(renderer, mappedInput);
    renderer.displayBuffer();
    return;
  }

  renderPage();
  saveProgress();
}

void XtcReaderActivity::renderPage() {
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

    const size_t srcRowBytes = (pageWidth + 7) / 8;
    const xtc::XtcError streamResult = xtc->loadPageStreaming(
        currentPage,
        [&](const uint8_t* data, size_t size, size_t offset) {
          for (size_t i = 0; i < size; i++) {
            const size_t absoluteOffset = offset + i;
            const uint16_t srcY = static_cast<uint16_t>(absoluteOffset / srcRowBytes);
            if (srcY >= pageHeight) {
              continue;
            }
            const uint16_t byteX = static_cast<uint16_t>(absoluteOffset % srcRowBytes);
            const uint16_t baseX = static_cast<uint16_t>(byteX * 8);
            const uint8_t packed = data[i];
            for (uint8_t bit = 0; bit < 8; bit++) {
              const uint16_t srcX = static_cast<uint16_t>(baseX + bit);
              if (srcX >= pageWidth) {
                break;
              }
              const bool isBlack = !((packed >> (7 - bit)) & 1);  // XTC: 0 = black, 1 = white
              if (isBlack) {
                renderer.drawPixel(srcX, srcY, true);
              }
            }
          }
        },
        1024);

    if (streamResult != xtc::XtcError::OK) {
      LOG_ERR("XTR", "Failed to stream page %lu: bitDepth=%u error=%s", currentPage, bitDepth,
              xtc::errorToString(streamResult));
      renderer.clearScreen();
      renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_PAGE_LOAD_ERROR), true, EpdFontFamily::BOLD);
      renderer.displayBuffer();
      return;
    }

    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh, forceFullRefresh);
    LOG_DBG("XTR", "Rendered page %lu/%lu (1-bit streaming)", currentPage + 1, xtc->getPageCount());
    return;
  }

  // Calculate buffer size for one page
  // XTG (1-bit): Row-major, ((width+7)/8) * height bytes
  // XTH (2-bit): Two bit planes, column-major, ((width * height + 7) / 8) * 2 bytes
  size_t pageBufferSize;
  if (bitDepth == 2) {
    pageBufferSize = ((static_cast<size_t>(pageWidth) * pageHeight + 7) / 8) * 2;
  } else {
    pageBufferSize = ((pageWidth + 7) / 8) * pageHeight;
  }

  // Allocate page buffer
  uint8_t* pageBuffer = static_cast<uint8_t*>(malloc(pageBufferSize));
  if (!pageBuffer) {
    LOG_ERR("XTR", "Failed to allocate page buffer (%lu bytes)", pageBufferSize);
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_MEMORY_ERROR), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  // Load page data
  size_t bytesRead = xtc->loadPage(currentPage, pageBuffer, pageBufferSize);
  if (bytesRead == 0) {
    LOG_ERR("XTR", "Failed to load page %lu: bufferSize=%lu bitDepth=%u error=%s", currentPage, pageBufferSize,
            bitDepth, xtc::errorToString(xtc->getLastError()));
    free(pageBuffer);
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_PAGE_LOAD_ERROR), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  // Clear screen first
  renderer.clearScreen();

  // Copy page bitmap using GfxRenderer's drawPixel
  // XTC/XTCH pages are pre-rendered with status bar included, so render full page
  const uint16_t maxSrcY = pageHeight;

  if (bitDepth == 2) {
    // XTH 2-bit mode: Two bit planes, column-major order
    // - Columns scanned right to left (x = width-1 down to 0)
    // - 8 vertical pixels per byte (MSB = topmost pixel in group)
    // - First plane: Bit1, Second plane: Bit2
    // - Pixel value = (bit1 << 1) | bit2
    // - Grayscale: 0=White, 1=Dark Grey, 2=Light Grey, 3=Black

    const size_t planeSize = (static_cast<size_t>(pageWidth) * pageHeight + 7) / 8;
    const uint8_t* plane1 = pageBuffer;              // Bit1 plane
    const uint8_t* plane2 = pageBuffer + planeSize;  // Bit2 plane
    const size_t colBytes = (pageHeight + 7) / 8;    // Bytes per column (100 for 800 height)

    // Lambda to get pixel value at (x, y)
    auto getPixelValue = [&](uint16_t x, uint16_t y) -> uint8_t {
      const size_t colIndex = pageWidth - 1 - x;
      const size_t byteInCol = y / 8;
      const size_t bitInByte = 7 - (y % 8);
      const size_t byteOffset = colIndex * colBytes + byteInCol;
      const uint8_t bit1 = (plane1[byteOffset] >> bitInByte) & 1;
      const uint8_t bit2 = (plane2[byteOffset] >> bitInByte) & 1;
      return (bit1 << 1) | bit2;
    };

    // Optimized grayscale rendering without storeBwBuffer (saves 48KB peak memory)
    // Flow: BW display → LSB/MSB passes → grayscale display → re-render BW for next frame

    // Count pixel distribution for debugging
    uint32_t pixelCounts[4] = {0, 0, 0, 0};
    for (uint16_t y = 0; y < pageHeight; y++) {
      for (uint16_t x = 0; x < pageWidth; x++) {
        pixelCounts[getPixelValue(x, y)]++;
      }
    }
    LOG_DBG("XTR", "Pixel distribution: White=%lu, DarkGrey=%lu, LightGrey=%lu, Black=%lu", pixelCounts[0],
            pixelCounts[1], pixelCounts[2], pixelCounts[3]);

    // Pass 1: BW buffer - draw all non-white pixels as black
    for (uint16_t y = 0; y < pageHeight; y++) {
      for (uint16_t x = 0; x < pageWidth; x++) {
        if (getPixelValue(x, y) >= 1) {
          renderer.drawPixel(x, y, true);
        }
      }
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
      pagesUntilFullRefresh--;
    }

    // Pass 2: LSB buffer - mark DARK gray only (XTH value 1)
    // In LUT: 0 bit = apply gray effect, 1 bit = untouched
    renderer.clearScreen(0x00);
    for (uint16_t y = 0; y < pageHeight; y++) {
      for (uint16_t x = 0; x < pageWidth; x++) {
        if (getPixelValue(x, y) == 1) {  // Dark grey only
          renderer.drawPixel(x, y, false);
        }
      }
    }
    renderer.copyGrayscaleLsbBuffers();

    // Pass 3: MSB buffer - mark LIGHT AND DARK gray (XTH value 1 or 2)
    // In LUT: 0 bit = apply gray effect, 1 bit = untouched
    renderer.clearScreen(0x00);
    for (uint16_t y = 0; y < pageHeight; y++) {
      for (uint16_t x = 0; x < pageWidth; x++) {
        const uint8_t pv = getPixelValue(x, y);
        if (pv == 1 || pv == 2) {  // Dark grey or Light grey
          renderer.drawPixel(x, y, false);
        }
      }
    }
    renderer.copyGrayscaleMsbBuffers();

    // Display grayscale overlay
    renderer.displayGrayBuffer();

    // Pass 4: Re-render BW to framebuffer (restore for next frame, instead of restoreBwBuffer)
    renderer.clearScreen();
    for (uint16_t y = 0; y < pageHeight; y++) {
      for (uint16_t x = 0; x < pageWidth; x++) {
        if (getPixelValue(x, y) >= 1) {
          renderer.drawPixel(x, y, true);
        }
      }
    }

    // Cleanup grayscale buffers with current frame buffer
    renderer.cleanupGrayscaleWithFrameBuffer();

    free(pageBuffer);

    LOG_DBG("XTR", "Rendered page %lu/%lu (2-bit grayscale)", currentPage + 1, xtc->getPageCount());
    return;
  } else {
    // 1-bit mode: 8 pixels per byte, MSB first
    const size_t srcRowBytes = (pageWidth + 7) / 8;  // 60 bytes for 480 width

    for (uint16_t srcY = 0; srcY < maxSrcY; srcY++) {
      const size_t srcRowStart = srcY * srcRowBytes;

      for (uint16_t srcX = 0; srcX < pageWidth; srcX++) {
        // Read source pixel (MSB first, bit 7 = leftmost pixel)
        const size_t srcByte = srcRowStart + srcX / 8;
        const size_t srcBit = 7 - (srcX % 8);
        const bool isBlack = !((pageBuffer[srcByte] >> srcBit) & 1);  // XTC: 0 = black, 1 = white

        if (isBlack) {
          renderer.drawPixel(srcX, srcY, true);
        }
      }
    }
  }
  // White pixels are already cleared by clearScreen()

  free(pageBuffer);

  // XTC pages already have status bar pre-rendered, no need to add our own

  // Display with the configured reader refresh policy
  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh, forceFullRefresh);

  LOG_DBG("XTR", "Rendered page %lu/%lu (%u-bit)", currentPage + 1, xtc->getPageCount(), bitDepth);
}

void XtcReaderActivity::saveProgress() const {
  READING_STATS.updateProgress(xtc->calculateProgress(currentPage), currentPage + 1 >= xtc->getPageCount(),
                               getChapterTitleForStats(*xtc, currentPage),
                               getChapterProgressForStats(*xtc, currentPage));

  std::string progressPath = getStableProgressPath(stableBookId);
  if (!progressPath.empty()) {
    BookIdentity::ensureStableDataDir(stableBookId);
  } else {
    progressPath = getLegacyProgressPath(*xtc);
  }
  uint8_t data[4];
  data[0] = currentPage & 0xFF;
  data[1] = (currentPage >> 8) & 0xFF;
  data[2] = (currentPage >> 16) & 0xFF;
  data[3] = (currentPage >> 24) & 0xFF;
  ProgressFile::writeAtomicPath("XTR", progressPath, data, sizeof(data));
}

void XtcReaderActivity::loadProgress() {
  FsFile f;
  bool loadedFromLegacy = false;
  const std::string stableProgressPath = getStableProgressPath(stableBookId);
  const std::string legacyProgressPath = getLegacyProgressPath(*xtc);
  const std::string progressPath = (!stableProgressPath.empty() && Storage.exists(stableProgressPath.c_str()))
                                       ? stableProgressPath
                                       : legacyProgressPath;
  if (progressPath == legacyProgressPath) {
    loadedFromLegacy = !stableProgressPath.empty() && Storage.exists(legacyProgressPath.c_str());
  }
  if (Storage.openFileForRead("XTR", progressPath, f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      currentPage = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
      LOG_DBG("XTR", "Loaded progress: page %lu", currentPage);

      // Validate page number
      if (currentPage >= xtc->getPageCount()) {
        currentPage = 0;
      }
    }
    f.close();
    if (loadedFromLegacy) {
      saveProgress();
    }
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
