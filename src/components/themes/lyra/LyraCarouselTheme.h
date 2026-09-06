#pragma once

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

namespace LyraCarouselMetrics {
constexpr ThemeMetrics values = {.batteryWidth = 16,
                                 .batteryHeight = 12,
                                 .topPadding = 5,
                                 .batteryBarHeight = 40,
                                 .headerHeight = 84,
                                 .verticalSpacing = 16,
                                 .previewPadding = 12,
                                 .previewHeightPercent = 30,
                                 .contentSidePadding = 20,
                                 .listRowHeight = 40,
                                 .listWithSubtitleRowHeight = 60,
                                 .listRowGap = 0,
                                 .listRowRadius = 6,
                                 .listInset = 20,
                                 .listSidePadding = 8,
                                 .listSelectionStyle = 1,  // light pill
                                 .listScrollWidth = 4,
                                 .listScrollSide = 0,
                                 .listTitleBold = false,
                                 .headerSidePadding = 18,
                                 .headerUnderlineSize = 3,
                                 .headerTitleAlign = 0,  // left
                                 .headerBatterySide = 0,
                                 .headerBatteryDetached = true,
                                 .menuRowHeight = 64,
                                 .menuSpacing = 8,
                                 .tabSpacing = 8,
                                 .tabBarHeight = 40,
                                 .scrollBarWidth = 4,
                                 .scrollBarRightOffset = 5,
                                 .homeTopPadding = 56,
                                 .homeCoverHeight = 600,
                                 .homeCoverTileHeight = 660,
                                 .homeRecentBooksCount = 3,
                                 .homeContinueReadingInMenu = false,
                                 .homeMenuTopOffset = 16,
                                 .buttonHintsHeight = 40,
                                 .sideButtonHintsWidth = 30,
                                 .progressBarHeight = 16,
                                 .progressBarMarginTop = 1,
                                 .statusBarHorizontalMargin = 5,
                                 .statusBarVerticalMargin = 19,
                                 .keyboardKeyWidth = 31,
                                 .keyboardKeyHeight = 50,
                                 .keyboardKeySpacing = 0,
                                 .keyboardBottomKeyHeight = 35,
                                 .keyboardBottomKeySpacing = 5,
                                 .keyboardBottomAligned = true,
                                 .keyboardCenteredText = true,
                                 .keyboardVerticalOffset = -7,
                                 .keyboardTextFieldWidthPercent = 85,
                                 .keyboardWidthPercent = 90,
                                 .popupTopOffsetRatio = 0.165f,
                                 .popupMarginX = 16,
                                 .popupMarginY = 12,
                                 .popupFrameThickness = 2,
                                 .popupCornerRadius = 6,
                                 .popupTextBold = false,
                                 .popupTextInverted = false,
                                 .popupTextBaselineOffsetY = -2,
                                 .popupProgressBarHeight = 4,
                                 .popupProgressDrawOutline = false,
                                 .popupProgressClampPercent = false,
                                 .popupProgressFillInverted = false,
                                 .popupProgressOutlineInverted = false,
                                 .optionPopupItemSpacing = 8,
                                 .optionPopupInnerPadding = 20,
                                 .optionPopupSelectionVPadding = 12,
                                 .optionPopupDialogSideMargin = 20,
                                 .textFieldHorizontalPadding = 8,
                                 .textFieldNormalThickness = 1,
                                 .textFieldCursorThickness = 3,
                                 .textFieldLineEndOffset = 0,
                                 .controlRadius = 6,
                                 .sheetRadius = 6,
                                 .capsuleRadius = 6};
}

class LyraCarouselTheme : public LyraTheme {
 public:
  static constexpr int kCenterCoverW = 340;
  static constexpr int kCenterCoverH = LyraCarouselMetrics::values.homeCoverHeight - 60;
  static constexpr int kSideCoverW = 200;
  static constexpr int kSideCoverH = LyraCarouselMetrics::values.homeCoverHeight - 210;

  // Bottom shortcut band geometry, shared with HomeActivity's touch grid so
  // its hit bands match the visuals: a strip of up to kVisibleMenuSlots icon
  // tiles, kMenuTileHeight tall, sitting just above the button hints.
  static constexpr int kMenuIconSize = 32;
  static constexpr int kMenuIconPad = 14;
  static constexpr int kMenuTileHeight = kMenuIconPad + kMenuIconSize + kMenuIconPad;
  static constexpr int kVisibleMenuSlots = 5;

  static void setPreRenderIndex(int index);

  // Screen rect of the icon band drawButtonMenu() paints (it ignores the rect
  // it is handed and anchors to the bottom of the screen).
  static Rect menuBandRect(const GfxRenderer& renderer);
  // First shortcut shown in the band: a sliding window of visible slots
  // centred on the selection (-1 / out of range = window at 0).
  static int menuWindowStart(int buttonCount, int selectedIndex);
  // Carousel column under screen x: -1 = the previous cover (left of the
  // centre tile), 0 = the centre cover, +1 = the next cover.
  static int coverColumnAt(const GfxRenderer& renderer, int x);
  // The icon band is the "menu row" this theme draws.
  int getMenuRowHeight(const GfxRenderer& renderer) const override;

  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
  void drawCarouselBorder(GfxRenderer& renderer, Rect rect, bool inCarouselRow) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon,
                      const std::function<std::string(int index)>& buttonSubtitle = nullptr,
                      const std::function<bool(int index)>& showAccessory = nullptr) const override;
  void drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                const std::function<std::string(int index)>& rowTitle,
                const std::function<std::string(int index)>& rowSubtitle,
                const std::function<UIIcon(int index)>& rowIcon, const std::function<std::string(int index)>& rowValue,
                bool highlightValue, const std::function<bool(int index)>& rowCompleted = nullptr) const override;
  void drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                  bool selected) const override;
};
