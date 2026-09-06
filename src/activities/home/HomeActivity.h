#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "./FileBrowserActivity.h"
#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct Rect;

class HomeActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  bool recentsLoading = false;
  bool recentsLoaded = false;
  bool firstRenderDone = false;
  bool hasOpdsServers = false;
  bool coverRendered = false;      // Track if cover has been rendered once
  bool coverBufferStored = false;  // Track if cover buffer is stored
  uint8_t* coverBuffer = nullptr;  // HomeActivity's own buffer for cover image
  size_t coverBufferSize = 0;
  int coverRectX = 0;
  int coverRectY = 0;
  int coverRectW = 0;
  int coverRectH = 0;
  int lastCarouselBookIndex = 0;
  int residentCarouselFrameIndex = -1;
  int residentCarouselSelectorIndex = -1;
  uint32_t residentCarouselFrameHash = 0;
  bool residentCarouselFrameValid = false;
  int cachedCarouselFrameHashIndex = -1;
  uint32_t cachedCarouselFrameHash = 0;
  bool cachedCarouselFrameHashValid = false;
  std::string carouselCoverLoadAttemptPath;
  bool carouselFramesReady = false;
  std::vector<RecentBook> recentBooks;
  // Menu entry to pre-select on entry (set by ActivityManager::goHome when
  // returning from a sub-screen) and whether the first paint should be a
  // HALF refresh (wake from sleep / home gesture) instead of the default.
  const HomeMenuItem initialMenuItem;
  const bool cleanInitialRefresh;
  void onSelectBook(const std::string& path);
  void onFileBrowserOpen();
  void onAppsOpen();
  void onReadingStatsOpen();
  void onSyncDayOpen();
  void onOpdsBrowserOpen();

  int getMenuItemCount() const;
  bool storeCoverBuffer();    // Store frame buffer for cover image
  bool restoreCoverBuffer();  // Restore frame buffer from stored cover
  void freeCoverBuffer();     // Free the stored cover buffer
  void preRenderCarouselFrames();
  bool renderCarouselFrame(int bookIndex);
  bool loadCarouselFrameFromStorage(int bookIndex);
  bool saveCarouselFrameToStorage(int bookIndex);
  void invalidateResidentCarouselFrame();
  void invalidateCarouselFrameHash();
  void requestFreshHomeRender(bool immediate = false);
  uint32_t getCachedCarouselFrameHash(int bookIndex);
  void scheduleCarouselCoverLoadIfNeeded();
  void loadRecentBooks(int maxBooks);
  void reloadHomeBooks(int maxBooks);
  // Selector index of the Home shortcut matching a HomeMenuItem (0 when absent).
  int indexForMenuItem(HomeMenuItem item) const;
  // Open whatever selectorIndex points at (book, shortcut, or Apps hub).
  void activateSelection();
  void promptRemoveSelectedBook();
  // Touch handling for the cover tile(s) and the shortcut rows/icons.
  // Returns true when the pass was consumed.
  bool handleTouch();
  void loadRecentCovers(int coverHeight);
  bool needsRecentCoverLoad(int coverHeight) const;

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                        HomeMenuItem initialMenuItemValue = HomeMenuItem::NONE, bool cleanInitialRefresh = false)
      : Activity("Home", renderer, mappedInput),
        initialMenuItem(initialMenuItemValue),
        cleanInitialRefresh(cleanInitialRefresh) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }
  uint8_t getUiTransitionRefreshWeight() const override { return UI_TRANSITION_REFRESH_WEIGHT_DENSE; }
};
