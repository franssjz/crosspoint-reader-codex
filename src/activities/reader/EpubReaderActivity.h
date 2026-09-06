#pragma once
#include <Epub.h>
#include <Epub/FootnoteEntry.h>
#include <Epub/PageLink.h>
#include <Epub/Section.h>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

#include "BookmarkStore.h"
#include "EndOfBookOptions.h"
#include "EpubReaderMenuActivity.h"
#include "ProgressMapper.h"
#include "ReaderToolbarUi.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"

class Page;

class EpubReaderActivity final : public Activity {
  std::shared_ptr<Epub> epub;
  std::unique_ptr<Section> section = nullptr;
  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  std::optional<uint16_t> pendingPageJump;
  // Explicit content-offset jump (bookmark, sync result, chapter list): resolved
  // to a page once the target section is loaded.
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
  // Content offset of the page being shown when the layout last changed
  // (settings, orientation, rebuild) or as read from progress.bin. Preferred
  // over the page-count ratio when the chapter is re-paginated (upstream);
  // the ratio remains the fallback.
  std::optional<uint32_t> cachedVisibleTextOffset;
  // Content offset of the currently rendered page (for progress saves / bookmarks).
  std::optional<uint32_t> currentPageVisibleOffset;
  // Page numbers are stable across a progressive-cache rebuild. Remap by percentage only
  // when a settings or viewport change actually caused the chapter to be repaginated.
  bool pendingPaginationReposition = false;
  unsigned long lastPageTurnTime = 0UL;
  unsigned long pageTurnDuration = 0UL;
  // A page turn requested while the render task still owns the page (or too
  // soon after the last one) is queued and replayed once the guard clears.
  int8_t pendingManualTurn = 0;
  // Signals that the next render should reposition within the newly loaded section
  // based on a cross-book percentage jump.
  bool pendingPercentJump = false;
  // Normalized 0.0-1.0 progress within the target spine item, computed from book percentage.
  float pendingSpineProgress = 0.0f;
  std::string stableBookId;
  BookmarkStore bookmarkStore;
  bool pendingScreenshot = false;
  uint8_t pageLoadRetryCount = 0;
  static constexpr uint8_t MAX_PAGE_LOAD_RETRIES = 3;
  bool skipNextButtonCheck = false;  // Skip button processing for one frame after subactivity exit
  bool automaticPageTurnActive = false;
  bool pendingForceFullRefresh = false;
  bool statusBarTemporarilyHidden = false;
  bool waitingForConfirmSecondClick = false;
  unsigned long firstConfirmClickMs = 0UL;
  int sessionStartSpineIndex = 0;
  int sessionStartPage = 0;
  bool sessionProgressTouched = false;
  bool currentPageBookmarked = false;
  bool recentsEntryRemoved = false;
  int idlePrewarmSpine = -1;
  int idlePrewarmPage = -1;
  unsigned long lastRenderCompleteMs = 0;
  std::shared_ptr<Page> currentOverlayPageCache;
  int currentOverlayPageSpineIndex = -1;
  int currentOverlayPageNumber = -1;
  int currentOverlayPageMarginLeft = 0;
  int currentOverlayPageMarginTop = 0;

  // End-of-book next-book suggestions. Built lazily on the render task; the
  // ready flag is the release/acquire publication point for loop().
  std::unique_ptr<EndOfBookOptions> endOfBookOptions;
  std::atomic<bool> endOfBookOptionsReady{false};

  // The orientation the current layout was built for. The control center's
  // orientation tile can move SETTINGS.orientation while this reader sits on
  // the activity stack, and Pop restores it without onEnter(), so the drift has
  // to be noticed here rather than assumed away.
  uint8_t appliedOrientation = 0;

  // Toolbar reader menu (SETTINGS.readerMenuStyle == READER_MENU_TOOLBAR): drawn
  // over the page instead of pushing the full-screen list menu. Select opens the
  // Toolbar; its tools open the Contents/Text/More bottom-sheet panels.
  enum class Overlay { None, Toolbar, Contents, Text, More };
  Overlay overlay = Overlay::None;
  int focusedTool = 0;  // toolbar tool focus: 0=Contents, 1=Text, 2=More
  int panelIndex = 0;   // selected row within the active panel
  // Panel list navigation: a tap steps one row, a hold jumps PANEL_HOLD_STEP rows in one go
  // (a contents list runs to hundreds of chapters). One jump per hold, not a repeat -- every
  // step repaints the panel, so repeating is bounded by the e-ink refresh anyway and reads as
  // sluggish. True once a hold has jumped, so the release that ends it is swallowed.
  static constexpr unsigned long PANEL_HOLD_MS = 1500;
  static constexpr int PANEL_HOLD_STEP = 10;
  bool panelHoldJumped = false;
  // Whether the panel draws its cursor row. Button boards always do; touch
  // boards only once a button has moved it, so a tapped row is not left inverted.
  bool panelCursorShown = false;
  // FreeInkUI chrome + tap targets for the overlay; created when it opens,
  // released when it closes.
  std::unique_ptr<ReaderToolbarUi> toolbarUi;
  // Modal option picker over the panel (same component the Settings screens
  // use), for enum rows: font size / line spacing / alignment / orientation /
  // auto page turn. Toggle rows stay one-tap toggles, as in Settings.
  OptionPopup overlayPopup;
  // True while a clean-page snapshot (renderer.storeBwBuffer) backs the open
  // overlay, letting panel->toolbar steps restore the page without a full
  // re-render. Discarded on close / whenever the page under the overlay changes.
  bool overlayPageStored = false;
  int autoTurnOption = 0;  // current auto page-turn rate index (More panel)
  std::vector<EpubReaderMenuActivity::MenuItem> moreItems;

  struct ReaderSettingsSnapshot {
    uint8_t darkMode = 0;
    uint8_t fadingFix = 0;
    uint8_t refreshFrequency = 0;
    uint8_t fontFamily = 0;
    uint8_t fontPointSize = 0;
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

  // Footnote / link support
  std::vector<FootnoteEntry> currentPageFootnotes;
  std::vector<PageLink> currentPageLinks;
  int currentPageLinkMarginLeft = 0;
  int currentPageLinkMarginTop = 0;
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
  static constexpr size_t RENDER_MIN_FREE_HEAP = 24 * 1024;

  void renderContents(std::shared_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);
  void drawTextHighlights(const Page& page, int orientedMarginTop, int orientedMarginLeft) const;
  void renderStatusBar() const;
  void renderSectionLoadFailure();
  void renderEndOfBook();
  ReaderRenderSpec makeRenderSpec(uint16_t viewportWidth, uint16_t viewportHeight) const;
  bool buildTickHeapGate();
  bool applyDeferredReposition();
  void clearDeferredReposition();
  // Remember the current page's content offset (plus the page-count ratio) so a
  // re-paginated chapter can reopen on the same text.
  void rememberCurrentLayoutPosition();
  bool saveProgress(int spineIndex, int currentPage, int pageCount);
  // Jump to a percentage of the book (0-100), mapping it to spine and page.
  void jumpToPercent(int percent);
  void openReaderMenu();
  void onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action);
  ReaderSettingsSnapshot captureReaderSettingsSnapshot() const;
  void applyReaderSettingsChanges(const ReaderSettingsSnapshot& before);
  void applyOrientation(uint8_t orientation);
  void toggleAutoPageTurn(uint8_t selectedPageTurnOption);
  void saveCurrentPageBookmark();
  void toggleCurrentPageBookmark();
  void updateBookmarkFlag();
  void openDictionaryWordSelect();
  unsigned long confirmLongPressThreshold() const;
  bool runLongPressMenuFunction();
  std::string moveCompletedBookIfEnabled();
  void exitReaderAfterOptionalCompletedMove();
  void markCurrentBookAsFinished();
  void pageTurn(bool isForwardTurn);
  void skipChapter(bool forward);
  void requestCurrentPageFullRefresh();
  void toggleTemporaryStatusBar();
  void cacheCurrentPageForOverlay(const std::shared_ptr<Page>& page, int marginLeft, int marginTop);
  void invalidateCurrentOverlayPageCache();
  std::shared_ptr<Page> loadCurrentPageForOverlay(int& outMarginLeft, int& outMarginTop);
  bool isAtEndOfBook() const;
  void returnFromEndOfBook();
  bool endOfBookMenuActive() const;
  bool handleEndOfBookMenu();
  void clearEndOfBookOptionsIfNeeded();
  bool handleBackNavigation();
  void openFootnotesFromPowerButton();

  // Toolbar reader menu (see Overlay above).
  bool usesToolbarMenu() const;
  void openOverlay(Overlay target);
  void closeOverlayToPage();
  void discardOverlayPage();
  void handleOverlayInput();
  void renderOverlay();
  std::string currentChapterTitle() const;
  // Text panel rows (font, size, line spacing, alignment, focus reading).
  std::string textRowName(int row) const;
  std::string textRowValue(int row) const;
  void showTextRowPopup(int row);
  // Persist + re-paginate + re-render under the open panel (live preview).
  void applyTextSettingLive();
  void paintOverlayPopup();
  // Persist the reader text settings, (re)load the selected SD font, and
  // re-paginate the current chapter so changes apply without re-opening the book.
  void applyReaderTextSettings();
  // More panel rows.
  void buildMoreActions();
  std::string moreRowName(int row) const;
  std::string moreRowValue(int row) const;
  void activateMoreRow(int row);

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
                              std::optional<uint32_t> initialBookmarkVisibleTextOffset = std::nullopt,
                              bool allowFastInitialRefresh = false);
  ~EpubReaderActivity() override;
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  bool skipLoopDelay() override {
    return section && section->isBuilding() && !buildHeapPaused &&
           (section->isPartial() || static_cast<int>(section->pageCount) < section->currentPage + BUILD_WINDOW_AHEAD);
  }
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
  CrossPointPosition getCurrentPosition() const;
};
