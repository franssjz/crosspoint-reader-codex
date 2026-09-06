/**
 * XtcReaderActivity.h
 *
 * XTC ebook reader activity for CrossPoint Reader
 * Displays pre-rendered XTC pages on e-ink display
 */

#pragma once

#include <Xtc.h>

#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include "EndOfBookOptions.h"
#include "activities/Activity.h"

class XtcReaderActivity final : public Activity {
  std::shared_ptr<Xtc> xtc;
  std::string stableBookId;

  uint32_t currentPage = 0;
  int pagesUntilFullRefresh = 0;
  bool pendingForceFullRefresh = false;
  bool waitingForConfirmSecondClick = false;
  unsigned long firstConfirmClickMs = 0UL;

  // End-of-book next-book suggestions (upstream). Built lazily on the render
  // task; the ready flag is the release/acquire publication point for loop().
  std::unique_ptr<EndOfBookOptions> endOfBookOptions;
  std::atomic<bool> endOfBookOptionsReady{false};

  enum class StatusBarOverlayPosition { Bottom, Top };
  struct StatusBarInfo {
    int currentPage;
    int pageCount;
    std::string title;
  };

  void renderPage();
  void renderEndOfBook();
  void renderStatusBarOverlay(StatusBarOverlayPosition position) const;
  StatusBarInfo getStatusBarInfo() const;
  void saveProgress() const;
  void loadProgress();
  void openChapterSelection();
  bool isAtEndOfBook() const { return xtc && currentPage >= xtc->getPageCount(); }
  void returnFromEndOfBook();
  bool endOfBookMenuActive() const;
  bool handleEndOfBookMenu();
  void clearEndOfBookOptionsIfNeeded();
  bool handleBackNavigation();
  void requestCurrentPageFullRefresh();
  std::string moveCompletedBookIfEnabled();
  void exitReaderAfterOptionalCompletedMove();

 public:
  explicit XtcReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Xtc> xtc,
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
