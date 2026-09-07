#pragma once
#include <Epub.h>
#include <Epub/FootnoteEntry.h>
#include <Epub/Section.h>

#include <optional>

#include "BookmarkStore.h"
#include "EndOfBookOptions.h"
#include "EpubReaderMenuActivity.h"
#include "activities/Activity.h"

class Page;

class EpubReaderActivity final : public Activity {
  std::shared_ptr<Epub> epub;
  std::unique_ptr<Section> section = nullptr;
  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  std::optional<uint16_t> pendingPageJump;
  std::optional<uint32_t> pendingVisibleTextOffset;
  // Set when navigating to a footnote href with a fragment (e.g. #note1).
  // Cleared on the next render after the new section loads and resolves it to a page.
  std::string pendingAnchor;
  int initialBookmarkSpineIndex = -1;
  int initialBookmarkPage = -1;
  std::optional<uint32_t> initialBookmarkVisibleTextOffset;
  int pagesUntilFullRefresh = 0;
  int cachedSpineIndex = 0;
  int cachedChapterTotalPageCount = 0;
  // Page numbers are stable across a progressive-cache rebuild. Remap by percentage only
  // when a settings or viewport change actually caused the chapter to be repaginated.
  bool pendingPaginationReposition = false;
  unsigned long lastPageTurnTime = 0UL;
  unsigned long pageTurnDuration = 0UL;
  // Signals that the next render should reposition within the newly loaded section
  // based on a cross-book percentage jump.
  bool pendingPercentJump = false;
  // Normalized 0.0-1.0 progress within the target spine item, computed from book percentage.
  float pendingSpineProgress = 0.0f;
  std::string stableBookId;
  BookmarkStore bookmarkStore;
  bool pendingScreenshot = false;
  bool skipNextButtonCheck = false;  // Skip button processing for one frame after subactivity exit
  bool automaticPageTurnActive = false;
  bool pendingForceFullRefresh = false;
  bool statusBarTemporarilyHidden = false;
  bool waitingForConfirmSecondClick = false;
  unsigned long firstConfirmClickMs = 0UL;
  int sessionStartSpineIndex = 0;
  int sessionStartPage = 0;
  bool sessionProgressTouched = false;
  std::shared_ptr<Page> currentOverlayPageCache;
  EndOfBookOptions endOfBookOptions;
  int currentOverlayPageSpineIndex = -1;
  int currentOverlayPageNumber = -1;
  int currentOverlayPageMarginLeft = 0;
  int currentOverlayPageMarginTop = 0;

  struct ReaderSettingsSnapshot {
    uint8_t darkMode = 0;
    uint8_t fadingFix = 0;
    uint8_t refreshFrequency = 0;
    uint8_t fontFamily = 0;
    uint8_t fontSize = 0;
    uint8_t lineSpacing = 0;
    uint8_t screenMargin = 0;
    uint8_t paragraphAlignment = 0;
    uint8_t embeddedStyle = 0;
    uint8_t hyphenationEnabled = 0;
    uint8_t bionicReading = 0;
    uint8_t orientation = 0;
    uint8_t extraParagraphSpacing = 0;
    uint8_t forceParagraphIndents = 0;
    uint8_t textAntiAliasing = 0;
    uint8_t textDarkness = 0;
    uint8_t readerRefreshMode = 0;
    uint8_t imageRendering = 0;
    std::string sdFontFamilyName;
  };

  // Footnote support
  std::vector<FootnoteEntry> currentPageFootnotes;
  struct SavedPosition {
    int spineIndex;
    int pageNumber;
  };
  static constexpr int MAX_FOOTNOTE_DEPTH = 3;
  SavedPosition savedPositions[MAX_FOOTNOTE_DEPTH] = {};
  int footnoteDepth = 0;
  int lastSavedSpineIndex = -1;
  int lastSavedPage = -1;
  int lastSavedPageCount = -1;
  uint16_t buildViewportWidth = 0;
  uint16_t buildViewportHeight = 0;
  bool buildHeapPaused = false;
  bool partialRebuildStartFailed = false;

  static constexpr int BUILD_PAGES_PER_CHUNK = 8;
  static constexpr int BACKGROUND_BUILD_PAGES_PER_TICK = 2;
  static constexpr int BUILD_WINDOW_AHEAD = 5;
  static constexpr int PARTIAL_REBUILD_START_MARGIN = 15;
  static constexpr size_t BACKGROUND_BUILD_MIN_FREE_HEAP = 32 * 1024;
  static constexpr size_t BACKGROUND_BUILD_MIN_MAX_ALLOC = 16 * 1024;

  void renderContents(std::shared_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);
  void drawTextHighlights(const Page& page, int orientedMarginTop, int orientedMarginLeft) const;
  // Render an image page as absolute 2-bit grayscale via the OEM factory LUT
  // (white pre-flash + GRAY2 planes + factory waveform). Returns false without
  // touching the panel if the BW backup allocation fails; caller falls back to
  // the differential overlay path.
  bool renderFactoryGrayPage(Page& page, int orientedMarginLeft, int orientedMarginTop);
  void renderStatusBar() const;
  void renderSectionLoadFailure();
  ReaderRenderSpec makeRenderSpec(uint16_t viewportWidth, uint16_t viewportHeight) const;
  bool buildTickHeapGate();
  bool applyDeferredReposition();
  void clearDeferredReposition();
  bool saveProgress(int spineIndex, int currentPage, int pageCount);
  // Jump to a percentage of the book (0-100), mapping it to spine and page.
  void jumpToPercent(int percent);
  void onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action);
  ReaderSettingsSnapshot captureReaderSettingsSnapshot() const;
  void applyReaderSettingsChanges(const ReaderSettingsSnapshot& before);
  void applyOrientation(uint8_t orientation);
  void toggleAutoPageTurn(uint8_t selectedPageTurnOption);
  void saveCurrentPageBookmark();
  std::string moveCompletedBookIfEnabled();
  void exitReaderAfterOptionalCompletedMove();
  void markCurrentBookAsFinished();
  void pageTurn(bool isForwardTurn);
  void requestCurrentPageFullRefresh();
  void toggleTemporaryStatusBar();
  void cacheCurrentPageForOverlay(const std::shared_ptr<Page>& page, int marginLeft, int marginTop);
  void invalidateCurrentOverlayPageCache();
  std::shared_ptr<Page> loadCurrentPageForOverlay(int& outMarginLeft, int& outMarginTop);

  // Footnote navigation
  void navigateToHref(const std::string& href, bool savePosition = false);
  void restoreSavedPosition();

  // KOReader sync — standalone activity launch and result application
  enum class SyncLaunchMode { COMPARE, PULL_REMOTE, PUSH_LOCAL, AUTO_PUSH };
  bool pendingParagraphLookup = false;
  uint16_t pendingParagraphIndex = 0;
  bool pendingListItemLookup = false;
  uint16_t pendingListItemIndex = 0;
  void launchKOReaderSync(SyncLaunchMode mode);
  void applyPendingSyncSession();
  bool tryAutoPushOnClose();

 public:
  explicit EpubReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Epub> epub,
                              int initialBookmarkSpineIndex = -1, int initialBookmarkPage = -1,
                              std::optional<uint32_t> initialBookmarkVisibleTextOffset = std::nullopt)
      : Activity("EpubReader", renderer, mappedInput),
        epub(std::move(epub)),
        initialBookmarkSpineIndex(initialBookmarkSpineIndex),
        initialBookmarkPage(initialBookmarkPage),
        initialBookmarkVisibleTextOffset(initialBookmarkVisibleTextOffset) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  bool skipLoopDelay() override { return section && section->isBuilding() && !buildHeapPaused; }
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
