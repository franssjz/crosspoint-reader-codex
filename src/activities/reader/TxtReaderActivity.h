#pragma once

#include <Txt.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "EndOfBookOptions.h"
#include "activities/Activity.h"

class TxtReaderActivity final : public Activity {
 public:
  struct TextLine {
    struct TextSpan {
      std::string text;
      uint8_t style = 0;
    };

    std::string text;
    std::vector<TextSpan> spans;
    uint8_t style = 0;
    uint8_t alignment = CrossPointSettings::LEFT_ALIGN;
    uint8_t indent = 0;
  };

 private:
  std::unique_ptr<Txt> txt;

  int currentPage = 0;
  int totalPages = 1;
  int pagesUntilFullRefresh = 0;

  // Streaming text reader - stores file offsets for each page
  std::vector<size_t> pageOffsets;  // File offset for start of each page
  std::vector<TextLine> currentPageLines;
  int linesPerPage = 0;
  int viewportWidth = 0;
  bool initialized = false;
  bool statusBarTemporarilyHidden = false;
  std::string stableBookId;
  bool pendingForceFullRefresh = false;
  bool waitingForConfirmSecondClick = false;
  unsigned long firstConfirmClickMs = 0UL;

  // End-of-book next-book suggestions (upstream). Built lazily on the render
  // task; the ready flag is the release/acquire publication point for loop().
  std::unique_ptr<EndOfBookOptions> endOfBookOptions;
  std::atomic<bool> endOfBookOptionsReady{false};

  // Cached settings for cache validation (different fonts/margins require re-indexing)
  int cachedFontId = 0;
  uint8_t cachedScreenMargin = 0;
  uint8_t cachedParagraphAlignment = CrossPointSettings::LEFT_ALIGN;
  int cachedOrientedMarginTop = 0;
  int cachedOrientedMarginRight = 0;
  int cachedOrientedMarginBottom = 0;
  int cachedOrientedMarginLeft = 0;

  void renderPage();
  void renderStatusBar() const;
  void renderEndOfBook();

  void initializeReader();
  bool loadPageAtOffset(size_t offset, std::vector<TextLine>& outLines, size_t& nextOffset);
  void buildPageIndex();
  bool loadPageIndexCache();
  void savePageIndexCache() const;
  void saveProgress() const;
  void loadProgress();
  bool skipPages(int amount);
  bool isAtEndOfBook() const { return initialized && currentPage >= totalPages; }
  void returnFromEndOfBook();
  bool endOfBookMenuActive() const;
  bool handleEndOfBookMenu();
  void clearEndOfBookOptionsIfNeeded();
  bool handleBackNavigation();
  void requestCurrentPageFullRefresh();
  void toggleTemporaryStatusBar();
  std::string moveCompletedBookIfEnabled();
  void exitReaderAfterOptionalCompletedMove();
  void finishBookAndExit();

 public:
  explicit TxtReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Txt> txt,
                             bool allowFastInitialRefresh = false);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool handleForcedRefresh() override {
    {
      RenderLock lock(*this);
      pagesUntilFullRefresh = 1;
      pendingForceFullRefresh = true;
    }
    requestUpdate();
    return true;
  }
  ScreenshotInfo getScreenshotInfo() const override;
};
