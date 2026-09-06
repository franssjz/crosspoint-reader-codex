#include "LyraCarouselTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "components/icons/book24.h"
#include "components/icons/cover.h"
#include "components/icons/file24.h"
#include "components/icons/folder.h"
#include "components/icons/folder24.h"
#include "components/icons/heart.h"
#include "components/icons/heart24.h"
#include "components/icons/hotspot.h"
#include "components/icons/image24.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings.h"
#include "components/icons/settings2.h"
#include "components/icons/text24.h"
#include "components/icons/transfer.h"
#include "components/icons/trophy.h"
#include "components/icons/trophy24.h"
#include "components/icons/wifi.h"
#include "fontIds.h"

namespace {
constexpr int kOverlap = 60;
constexpr int kCoverTopPad = 10;
constexpr int kTitleFontId = UI_12_FONT_ID;
constexpr int kDotSize = 8;
constexpr int kDotGap = 6;
constexpr int kCornerRadius = 6;
constexpr int kThinOutlineW = 1;
constexpr int kSelectionLineW = 3;
constexpr int kCenterOutlineW = 4;
constexpr int kProgressBadgePadX = 8;
constexpr int kProgressBadgePadY = 4;
constexpr int kProgressBadgeInset = 8;
constexpr int kProgressBadgeRadius = 4;
constexpr int kHighlightPad = 12;

int lastCarouselSelectorIndex = -1;

const uint8_t* iconForName(UIIcon icon, int size) {
  if (size == 24) {
    switch (icon) {
      case UIIcon::Folder:
        return Folder24Icon;
      case UIIcon::Text:
        return Text24Icon;
      case UIIcon::Image:
        return Image24Icon;
      case UIIcon::Book:
        return Book24Icon;
      case UIIcon::File:
        return File24Icon;
      case UIIcon::Trophy:
        return Trophy24Icon;
      case UIIcon::Heart:
        return Heart24Icon;
      default:
        return nullptr;
    }
  }

  if (size == 32) {
    switch (icon) {
      case UIIcon::Folder:
        return FolderIcon;
      case UIIcon::Book:
        return BookIcon;
      case UIIcon::Recent:
        return RecentIcon;
      case UIIcon::Settings:
        return Settings2Icon;
      case UIIcon::Apps:
        return SettingsIcon;
      case UIIcon::Transfer:
        return TransferIcon;
      case UIIcon::Library:
        return LibraryIcon;
      case UIIcon::Trophy:
        return TrophyIcon;
      case UIIcon::Wifi:
        return WifiIcon;
      case UIIcon::Hotspot:
        return HotspotIcon;
      case UIIcon::Heart:
        return HeartIcon;
      default:
        return nullptr;
    }
  }

  return nullptr;
}

void drawCoverPlaceholder(GfxRenderer& renderer, int x, int y, int maxW, int maxH) {
  renderer.drawRoundedRect(x, y, maxW, maxH, 1, kCornerRadius, true);
  renderer.fillRoundedRect(x, y + maxH / 3, maxW, 2 * maxH / 3, kCornerRadius, false, false, true, true, Color::Black);
  renderer.drawIcon(CoverIcon, x + maxW / 2 - 16, y + 8, 32, 32);
}

uint8_t getBookProgressPercent(const RecentBook& recentBook) {
  const ReadingBookStats* stats = nullptr;
  if (!recentBook.bookId.empty()) {
    stats = READING_STATS.findBook(recentBook.bookId);
  }
  if (stats == nullptr) {
    stats = READING_STATS.findBook(recentBook.path);
  }
  if (stats == nullptr) {
    return 0;
  }
  return std::min<uint8_t>(stats->lastProgressPercent, 100);
}

void drawProgressBadge(GfxRenderer& renderer, const RecentBook& book, const int coverX, const int coverY,
                       const int coverW, const int coverH) {
  const std::string progressText = std::to_string(getBookProgressPercent(book)) + "%";
  const int textW = renderer.getTextWidth(SMALL_FONT_ID, progressText.c_str(), EpdFontFamily::BOLD);
  const int textH = renderer.getLineHeight(SMALL_FONT_ID);
  const int badgeW = textW + 2 * kProgressBadgePadX;
  const int badgeH = textH + 2 * kProgressBadgePadY;
  const int badgeX = coverX + coverW - badgeW - kProgressBadgeInset;
  const int badgeY = coverY + coverH - badgeH - kProgressBadgeInset;

  renderer.fillRoundedRect(badgeX, badgeY, badgeW, badgeH, kProgressBadgeRadius, Color::Black);
  renderer.drawText(SMALL_FONT_ID, badgeX + kProgressBadgePadX, badgeY + kProgressBadgePadY - 1, progressText.c_str(),
                    false, EpdFontFamily::BOLD);
}
}  // namespace

void LyraCarouselTheme::setPreRenderIndex(int index) { lastCarouselSelectorIndex = index; }

Rect LyraCarouselTheme::menuBandRect(const GfxRenderer& renderer) {
  const int screenW = renderer.getScreenWidth();
  const int rowY = renderer.getScreenHeight() - LyraCarouselMetrics::values.buttonHintsHeight - kMenuTileHeight;
  return Rect{0, rowY, screenW, kMenuTileHeight};
}

int LyraCarouselTheme::menuWindowStart(const int buttonCount, const int selectedIndex) {
  if (buttonCount <= 0) return 0;
  const int visibleCount = std::min(buttonCount, kVisibleMenuSlots);
  const int maxWindowStart = std::max(0, buttonCount - visibleCount);
  if (selectedIndex < 0 || selectedIndex >= buttonCount) return 0;
  return std::clamp(selectedIndex - visibleCount / 2, 0, maxWindowStart);
}

int LyraCarouselTheme::coverColumnAt(const GfxRenderer& renderer, const int x) {
  const int centerX = (renderer.getScreenWidth() - kCenterCoverW) / 2;
  if (x < centerX) return -1;
  if (x >= centerX + kCenterCoverW) return 1;
  return 0;
}

int LyraCarouselTheme::getMenuRowHeight(const GfxRenderer&) const { return kMenuTileHeight; }

void LyraCarouselTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect,
                                            const std::vector<RecentBook>& recentBooks, const int selectorIndex,
                                            bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                                            std::function<bool()> storeCoverBuffer) const {
  (void)bufferRestored;
  if (recentBooks.empty()) {
    drawEmptyRecents(renderer, rect);
    return;
  }

  const int bookCount = static_cast<int>(recentBooks.size());
  const bool inCarouselRow = selectorIndex < bookCount;
  int centerIdx = inCarouselRow ? selectorIndex : (lastCarouselSelectorIndex >= 0 ? lastCarouselSelectorIndex : 0);
  centerIdx = std::max(0, std::min(centerIdx, bookCount - 1));
  if (centerIdx != lastCarouselSelectorIndex) {
    coverRendered = false;
    coverBufferStored = false;
  }

  const int screenW = renderer.getScreenWidth();
  const int centerTileY = rect.y + kCoverTopPad;
  const int sideTileY = centerTileY + (kCenterCoverH - kSideCoverH) / 2;
  const int centerX = (screenW - kCenterCoverW) / 2;
  const int leftX = centerX - kSideCoverW + kOverlap;
  const int rightX = centerX + kCenterCoverW - kOverlap;

  auto drawCover = [&](int bookIdx, int x, int y, int maxW, int maxH) -> bool {
    if (bookIdx < 0 || bookIdx >= bookCount) return false;
    const RecentBook& book = recentBooks[bookIdx];
    bool hasCover = false;
    if (!book.coverBmpPath.empty()) {
      std::string thumbPath = UITheme::getCoverThumbPath(book.coverBmpPath, maxW, maxH);
      const std::string centerThumbPath = UITheme::getCoverThumbPath(book.coverBmpPath, kCenterCoverW, kCenterCoverH);
      const std::string legacyThumbPath =
          UITheme::getCoverThumbPath(book.coverBmpPath, LyraCarouselMetrics::values.homeCoverHeight);
      if (!Storage.exists(thumbPath.c_str())) {
        if (Storage.exists(centerThumbPath.c_str())) {
          thumbPath = centerThumbPath;
        } else if (Storage.exists(legacyThumbPath.c_str())) {
          thumbPath = legacyThumbPath;
        }
      }
      HalFile file;
      if (Storage.openFileForRead("HOME", thumbPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          const float bmpRatio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
          const float tileRatio = static_cast<float>(maxW) / static_cast<float>(maxH);
          const float cropX = (bmpRatio > tileRatio) ? (1.0f - tileRatio / bmpRatio) : 0.0f;
          renderer.drawBitmap(bitmap, x, y, maxW, maxH, cropX, 0.0f);
          renderer.maskRoundedRectOutsideCorners(x, y, maxW, maxH, kCornerRadius, Color::White);
          hasCover = true;
        }
        file.close();
      }
    }
    if (!hasCover) {
      drawCoverPlaceholder(renderer, x, y, maxW, maxH);
    }
    return true;
  };

  if (!coverRendered) {
    lastCarouselSelectorIndex = centerIdx;
    renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);

    const int prevIdx = (centerIdx + bookCount - 1) % bookCount;
    const int nextIdx = (centerIdx + 1) % bookCount;
    if (bookCount >= 3 && drawCover(prevIdx, leftX, sideTileY, kSideCoverW, kSideCoverH)) {
      renderer.drawRoundedRect(leftX, sideTileY, kSideCoverW, kSideCoverH, 1, kCornerRadius, true);
    }
    if (bookCount >= 2 && drawCover(nextIdx, rightX, sideTileY, kSideCoverW, kSideCoverH)) {
      renderer.drawRoundedRect(rightX, sideTileY, kSideCoverW, kSideCoverH, 1, kCornerRadius, true);
    }

    renderer.fillRect(centerX - kCenterOutlineW, centerTileY - kCenterOutlineW, kCenterCoverW + 2 * kCenterOutlineW,
                      kCenterCoverH + 2 * kCenterOutlineW, false);
    drawCover(centerIdx, centerX, centerTileY, kCenterCoverW, kCenterCoverH);
    drawProgressBadge(renderer, recentBooks[centerIdx], centerX, centerTileY, kCenterCoverW, kCenterCoverH);

    const int dotsY = centerTileY + kCenterCoverH + 8;
    const int totalDotsW = bookCount * kDotSize + (bookCount - 1) * kDotGap;
    int dotX = centerX + (kCenterCoverW - totalDotsW) / 2;
    for (int i = 0; i < bookCount; ++i) {
      if (i == centerIdx) {
        renderer.fillRect(dotX, dotsY, kDotSize, kDotSize, true);
      } else {
        renderer.drawRect(dotX, dotsY, kDotSize, kDotSize, true);
      }
      dotX += kDotSize + kDotGap;
    }

    const int authorY = dotsY + kDotSize + 6;
    const std::string authorTrunc =
        renderer.truncatedText(kTitleFontId, recentBooks[centerIdx].author.c_str(), kCenterCoverW);
    const int authorW = renderer.getTextWidth(kTitleFontId, authorTrunc.c_str());
    renderer.drawText(kTitleFontId, centerX + (kCenterCoverW - authorW) / 2, authorY, authorTrunc.c_str(), true);

    const int titleY = authorY + renderer.getLineHeight(kTitleFontId) + 2;
    const std::string titleTrunc =
        renderer.truncatedText(kTitleFontId, recentBooks[centerIdx].title.c_str(), kCenterCoverW);
    const int titleW = renderer.getTextWidth(kTitleFontId, titleTrunc.c_str());
    renderer.drawText(kTitleFontId, centerX + (kCenterCoverW - titleW) / 2, titleY, titleTrunc.c_str(), true);

    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  }

  const int outlineW = inCarouselRow ? kSelectionLineW : kThinOutlineW;
  renderer.drawRoundedRect(centerX, centerTileY, kCenterCoverW, kCenterCoverH, outlineW, kCornerRadius, true);
}

void LyraCarouselTheme::drawCarouselBorder(GfxRenderer& renderer, Rect rect, bool inCarouselRow) const {
  // The pre-rendered frame already carries the thin outline. Only overlay the
  // thick selection border when the carousel row is actually focused; otherwise
  // there is nothing to do (e-ink pixels can only be set black, never cleared,
  // so drawing the same thin line again would be a no-op that wastes time).
  if (!inCarouselRow) return;
  const int centerTileY = rect.y + kCoverTopPad;
  const int centerX = (renderer.getScreenWidth() - kCenterCoverW) / 2;
  renderer.drawRoundedRect(centerX, centerTileY, kCenterCoverW, kCenterCoverH, kSelectionLineW, kCornerRadius, true);
}

void LyraCarouselTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                       const std::function<std::string(int index)>& buttonLabel,
                                       const std::function<UIIcon(int index)>& rowIcon,
                                       const std::function<std::string(int index)>& buttonSubtitle,
                                       const std::function<bool(int index)>& showAccessory) const {
  (void)rect;
  (void)buttonLabel;
  (void)buttonSubtitle;
  (void)showAccessory;
  if (buttonCount <= 0) return;

  const int visibleCount = std::min(buttonCount, kVisibleMenuSlots);
  const int safeSelectedIndex = (selectedIndex >= 0 && selectedIndex < buttonCount) ? selectedIndex : -1;
  const int windowStart = menuWindowStart(buttonCount, selectedIndex);

  // Same geometry HomeActivity hit-tests against (menuBandRect / tile width).
  const Rect band = menuBandRect(renderer);
  const int screenW = band.width;
  const int tileH = band.height;
  const int tileW = screenW / visibleCount;
  const int rowY = band.y;

  renderer.fillRect(0, rowY, screenW, tileH, false);

  for (int slot = 0; slot < visibleCount; ++slot) {
    const int i = windowStart + slot;
    const int tileX = slot * tileW;
    const int iconX = tileX + (tileW - kMenuIconSize) / 2;
    const int iconY = rowY + kMenuIconPad;

    if (safeSelectedIndex == i) {
      const int highlightSize = kMenuIconSize + 2 * kHighlightPad;
      const int highlightY = rowY + (tileH - highlightSize) / 2;
      renderer.fillRoundedRect(iconX - kHighlightPad, highlightY, highlightSize, highlightSize, kCornerRadius,
                               Color::Black);
    }

    if (rowIcon != nullptr) {
      const uint8_t* bmp = iconForName(rowIcon(i), kMenuIconSize);
      if (bmp != nullptr) {
        if (safeSelectedIndex == i) {
          if (renderer.isDarkMode()) {
            renderer.drawIconBlack(bmp, iconX, iconY, kMenuIconSize, kMenuIconSize);
          } else {
            renderer.drawIconInverted(bmp, iconX, iconY, kMenuIconSize, kMenuIconSize);
          }
        } else {
          renderer.drawIcon(bmp, iconX, iconY, kMenuIconSize, kMenuIconSize);
        }
      }
    }
  }

  if (buttonCount > visibleCount) {
    const int midY = rowY + tileH / 2;
    if (windowStart > 0) {
      renderer.drawLine(10, midY, 20, midY - 9, 2, true);
      renderer.drawLine(10, midY, 20, midY + 9, 2, true);
    }
    if (windowStart + visibleCount < buttonCount) {
      renderer.drawLine(screenW - 10, midY, screenW - 20, midY - 9, 2, true);
      renderer.drawLine(screenW - 10, midY, screenW - 20, midY + 9, 2, true);
    }
  }
}

void LyraCarouselTheme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                                 const std::function<std::string(int index)>& rowTitle,
                                 const std::function<std::string(int index)>& rowSubtitle,
                                 const std::function<UIIcon(int index)>& rowIcon,
                                 const std::function<std::string(int index)>& rowValue, bool highlightValue,
                                 const std::function<bool(int index)>& rowCompleted) const {
  LyraTheme::drawList(renderer, rect, itemCount, selectedIndex, rowTitle, rowSubtitle, rowIcon, rowValue,
                      highlightValue, rowCompleted);
}

void LyraCarouselTheme::drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                                   bool selected) const {
  LyraTheme::drawTabBar(renderer, rect, tabs, selected);
}
