#include "LyraTheme.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "components/icons/book24.h"
#include "components/icons/bookmark.h"
#include "components/icons/cover.h"
#include "components/icons/file.h"
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
#include "components/icons/text.h"
#include "components/icons/text24.h"
#include "components/icons/transfer.h"
#include "components/icons/trophy.h"
#include "components/icons/trophy24.h"
#include "components/icons/wifi.h"
#include "fontIds.h"
#include "util/ReadingStatsAnalytics.h"

// Internal constants
namespace {
constexpr int hPaddingInSelection = 8;
constexpr int cornerRadius = 6;
constexpr int topHintButtonY = 345;
constexpr int maxListValueWidth = 200;
constexpr int mainMenuIconSize = 32;
constexpr int listIconSize = 24;
constexpr int mainMenuColumns = 2;
constexpr int progressRowGap = 8;
constexpr int progressBarHeight = 8;
int coverWidth = 0;

std::vector<int> allocateTabTextWidths(const std::vector<int>& desiredWidths, int textBudget) {
  const int tabCount = static_cast<int>(desiredWidths.size());
  std::vector<int> widths(tabCount, 0);
  if (tabCount == 0 || textBudget <= 0) {
    return widths;
  }

  std::vector<bool> fixed(tabCount, false);
  int remainingBudget = textBudget;
  int remainingCount = tabCount;
  while (remainingCount > 0) {
    const int fairWidth = std::max(0, remainingBudget / remainingCount);
    bool fixedAny = false;
    for (int i = 0; i < tabCount; ++i) {
      if (!fixed[i] && desiredWidths[i] <= fairWidth) {
        widths[i] = desiredWidths[i];
        fixed[i] = true;
        remainingBudget -= desiredWidths[i];
        --remainingCount;
        fixedAny = true;
      }
    }
    if (!fixedAny) {
      break;
    }
  }

  if (remainingCount > 0) {
    const int fairWidth = std::max(0, remainingBudget / remainingCount);
    for (int i = 0; i < tabCount; ++i) {
      if (!fixed[i]) {
        widths[i] = fairWidth;
      }
    }
  }
  return widths;
}

std::string fitTabTextToWidth(const GfxRenderer& renderer, const int fontId, const char* text, const int maxWidth,
                              const EpdFontFamily::Style style) {
  if (!text || maxWidth <= 0) {
    return {};
  }
  if (renderer.getTextWidth(fontId, text, style) <= maxWidth) {
    return text;
  }

  std::vector<size_t> charEnds;
  const auto* start = reinterpret_cast<const unsigned char*>(text);
  const auto* cursor = start;
  while (*cursor != '\0') {
    utf8NextCodepoint(&cursor);
    charEnds.push_back(static_cast<size_t>(cursor - start));
  }

  size_t low = 0;
  size_t high = charEnds.size();
  while (low < high) {
    const size_t mid = low + (high - low + 1) / 2;
    const std::string candidate(text, charEnds[mid - 1]);
    if (renderer.getTextWidth(fontId, candidate.c_str(), style) <= maxWidth) {
      low = mid;
    } else {
      high = mid - 1;
    }
  }

  return low == 0 ? std::string() : std::string(text, charEnds[low - 1]);
}

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
  } else if (size == 32) {
    switch (icon) {
      case UIIcon::Folder:
        return FolderIcon;
      case UIIcon::Book:
        return BookIcon;
      case UIIcon::File:
        return FileIcon;
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
      case UIIcon::Text:
        return TextIcon;
      case UIIcon::Trophy:
        return TrophyIcon;
      case UIIcon::Wifi:
        return WifiIcon;
      case UIIcon::Hotspot:
        return HotspotIcon;
      case UIIcon::Heart:
        return HeartIcon;
      case UIIcon::Bookmark:
        return BookmarkIcon;
      default:
        return nullptr;
    }
  }
  return nullptr;
}

bool drawUiIcon(const GfxRenderer& renderer, const UIIcon icon, const int x, const int y, const int size) {
  if (const uint8_t* iconBitmap = iconForName(icon, size); iconBitmap != nullptr) {
    renderer.drawIcon(iconBitmap, x, y, size, size);
    return true;
  }

  if (size > 24) {
    if (const uint8_t* fallbackBitmap = iconForName(icon, 24); fallbackBitmap != nullptr) {
      const int inset = (size - 24) / 2;
      renderer.drawIcon(fallbackBitmap, x + inset, y + inset, 24, 24);
      return true;
    }
  }

  return false;
}

void drawCompletedListBadge(const GfxRenderer& renderer, const int x, const int y, const int size) {
  const int safeSize = std::max(12, size);
  const int strokeWidth = safeSize >= 16 ? 2 : 1;

  renderer.fillRect(x, y, safeSize, safeSize, true);
  renderer.drawRect(x, y, safeSize, safeSize, false);

  const int leftX = x + safeSize * 3 / 16;
  const int midX = x + safeSize * 7 / 16;
  const int rightX = x + safeSize * 13 / 16;
  const int leftY = y + safeSize * 9 / 16;
  const int midY = y + safeSize * 12 / 16;
  const int rightY = y + safeSize * 4 / 16;

  renderer.drawLine(leftX, leftY, midX, midY, strokeWidth, false);
  renderer.drawLine(midX, midY, rightX, rightY, strokeWidth, false);
}

const ReadingBookStats* getRecentBookStats(const RecentBook& recentBook) {
  if (!recentBook.bookId.empty()) {
    if (const ReadingBookStats* stats = READING_STATS.findBook(recentBook.bookId)) {
      return stats;
    }
  }
  return READING_STATS.findBook(recentBook.path);
}

void drawLyraProgressBar(GfxRenderer& renderer, const Rect& rect, const uint8_t progressPercent) {
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height, true);
  const int fillWidth = std::max(0, (rect.width - 4) * std::min<int>(progressPercent, 100) / 100);
  if (fillWidth > 0) {
    renderer.fillRect(rect.x + 2, rect.y + 2, fillWidth, std::max(0, rect.height - 4), true);
  }
}
}  // namespace

void LyraTheme::fillBatteryIcon(const GfxRenderer& renderer, Rect rect, uint16_t percentage) const {
  const bool charging = gpio.isUsbConnected();

  if (charging) {
    // Solid fill when charging so lightning bolt is visible
    renderer.fillRect(rect.x + 2, rect.y + 2, rect.width - 5, rect.height - 4);
    drawBatteryLightningBolt(renderer, rect.x + 4, rect.y + 2);
  } else {
    if (percentage > 10) {
      renderer.fillRect(rect.x + 2, rect.y + 2, 3, rect.height - 4);
    }
    if (percentage > 40) {
      renderer.fillRect(rect.x + 6, rect.y + 2, 3, rect.height - 4);
    }
    if (percentage > 70) {
      renderer.fillRect(rect.x + 10, rect.y + 2, 3, rect.height - 4);
    }
  }
}

void LyraTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle) const {
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);

  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  // Position icon at right edge, drawBatteryRight will place text to the left
  const int batteryX = rect.x + rect.width - 12 - LyraMetrics::values.batteryWidth;
  drawBatteryRight(renderer,
                   Rect{batteryX, rect.y + 5, LyraMetrics::values.batteryWidth, LyraMetrics::values.batteryHeight},
                   showBatteryPercentage);

  int maxTitleWidth = title != nullptr ? renderer.getTextWidth(UI_12_FONT_ID, title, EpdFontFamily::BOLD) : 0;
  int maxSubtitleWidth =
      subtitle != nullptr ? renderer.getTextWidth(SMALL_FONT_ID, subtitle, EpdFontFamily::REGULAR) : 0;

  // Available space is the distance between the side paddings, and a with side padding between title and subtitle.
  const int availableSpace = rect.width - LyraMetrics::values.contentSidePadding * 3;

  if (maxTitleWidth + maxSubtitleWidth > availableSpace) {
    if ((maxTitleWidth > availableSpace / 2) && (maxSubtitleWidth > availableSpace / 2)) {
      // Both are wider then half the space, truncate both.
      maxTitleWidth = availableSpace / 2;
      maxSubtitleWidth = availableSpace / 2;
    } else {
      // Truncate the the longest one
      if (maxTitleWidth > maxSubtitleWidth) {
        maxTitleWidth = availableSpace - maxSubtitleWidth;
      } else {
        maxSubtitleWidth = availableSpace - maxTitleWidth;
      }
    }
  }

  if (title) {
    auto truncatedTitle = renderer.truncatedText(UI_12_FONT_ID, title, maxTitleWidth, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, rect.x + LyraMetrics::values.contentSidePadding,
                      rect.y + LyraMetrics::values.batteryBarHeight + 3, truncatedTitle.c_str(), true,
                      EpdFontFamily::BOLD);
    renderer.drawLine(rect.x, rect.y + rect.height - 3, rect.x + rect.width - 1, rect.y + rect.height - 3, 3, true);
  }

  if (subtitle) {
    auto truncatedSubtitle = renderer.truncatedText(SMALL_FONT_ID, subtitle, maxSubtitleWidth, EpdFontFamily::REGULAR);
    int truncatedSubtitleWidth = renderer.getTextWidth(SMALL_FONT_ID, truncatedSubtitle.c_str());
    renderer.drawText(SMALL_FONT_ID,
                      rect.x + rect.width - LyraMetrics::values.contentSidePadding - truncatedSubtitleWidth,
                      rect.y + 50, truncatedSubtitle.c_str(), true);
  }
}

void LyraTheme::drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label, const char* rightLabel) const {
  const int contentWidth = std::max(0, rect.width - LyraMetrics::values.contentSidePadding * 2);

  int labelWidth = contentWidth;
  if (rightLabel) {
    auto truncatedRightLabel = renderer.truncatedText(SMALL_FONT_ID, rightLabel, contentWidth, EpdFontFamily::REGULAR);
    const int rightLabelWidth = renderer.getTextWidth(SMALL_FONT_ID, truncatedRightLabel.c_str());
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - LyraMetrics::values.contentSidePadding - rightLabelWidth,
                      rect.y + 7, truncatedRightLabel.c_str());
    labelWidth = std::max(0, contentWidth - rightLabelWidth - hPaddingInSelection);
  }

  if (labelWidth > 0) {
    auto truncatedLabel = renderer.truncatedText(UI_10_FONT_ID, label, labelWidth, EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, rect.x + LyraMetrics::values.contentSidePadding, rect.y + 6,
                      truncatedLabel.c_str(), true, EpdFontFamily::REGULAR);
  }

  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
}

void LyraTheme::drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                           bool selected) const {
  const int tabCount = static_cast<int>(tabs.size());
  if (tabCount == 0) {
    return;
  }

  std::vector<int> fontIds;
  std::vector<int> desiredWidths;
  fontIds.reserve(tabCount);
  desiredWidths.reserve(tabCount);

  for (const auto& tab : tabs) {
    const int fontId = tab.compact ? SMALL_FONT_ID : UI_10_FONT_ID;
    fontIds.push_back(fontId);
    desiredWidths.push_back(renderer.getTextWidth(fontId, tab.label, EpdFontFamily::REGULAR));
  }

  const int availableWidth = std::max(0, rect.width - LyraMetrics::values.contentSidePadding * 2);
  const int totalGapWidth = LyraMetrics::values.tabSpacing * std::max(0, tabCount - 1);
  const int totalPaddingWidth = 2 * hPaddingInSelection * tabCount;
  const std::vector<int> textWidths =
      allocateTabTextWidths(desiredWidths, availableWidth - totalGapWidth - totalPaddingWidth);

  int currentX = rect.x + LyraMetrics::values.contentSidePadding;

  if (selected) {
    renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
  }

  for (int i = 0; i < tabCount; ++i) {
    const auto& tab = tabs[i];
    const int fontId = fontIds[i];
    const std::string label =
        textWidths[i] > 0 ? fitTabTextToWidth(renderer, fontId, tab.label, textWidths[i], EpdFontFamily::REGULAR)
                          : std::string();
    const int textWidth = label.empty() ? 0 : renderer.getTextWidth(fontId, label.c_str(), EpdFontFamily::REGULAR);

    if (tab.selected) {
      if (selected) {
        renderer.fillRoundedRect(currentX, rect.y + 1, textWidth + 2 * hPaddingInSelection, rect.height - 4,
                                 cornerRadius, Color::Black);
      } else {
        renderer.fillRectDither(currentX, rect.y, textWidth + 2 * hPaddingInSelection, rect.height - 3,
                                Color::LightGray);
        renderer.drawLine(currentX, rect.y + rect.height - 3, currentX + textWidth + 2 * hPaddingInSelection,
                          rect.y + rect.height - 3, 2, true);
      }
    }

    if (!label.empty()) {
      renderer.drawText(fontId, currentX + hPaddingInSelection, rect.y + (tab.compact ? 8 : 6), label.c_str(),
                        !(tab.selected && selected), EpdFontFamily::REGULAR);
    }

    currentX += textWidth + 2 * hPaddingInSelection;
    if (i < tabCount - 1) {
      currentX += LyraMetrics::values.tabSpacing;
    }
  }

  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
}

void LyraTheme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                         const std::function<std::string(int index)>& rowTitle,
                         const std::function<std::string(int index)>& rowSubtitle,
                         const std::function<UIIcon(int index)>& rowIcon,
                         const std::function<std::string(int index)>& rowValue, bool highlightValue,
                         const std::function<bool(int index)>& rowCompleted) const {
  int rowHeight =
      (rowSubtitle != nullptr) ? LyraMetrics::values.listWithSubtitleRowHeight : LyraMetrics::values.listRowHeight;
  int pageItems = rect.height / rowHeight;

  const int totalPages = (itemCount + pageItems - 1) / pageItems;
  if (totalPages > 1) {
    const int scrollAreaHeight = rect.height;

    // Draw scroll bar
    const int scrollBarHeight = (scrollAreaHeight * pageItems) / itemCount;
    const int currentPage = selectedIndex / pageItems;
    const int scrollBarY = rect.y + ((scrollAreaHeight - scrollBarHeight) * currentPage) / (totalPages - 1);
    const int scrollBarX = rect.x + rect.width - LyraMetrics::values.scrollBarRightOffset;
    renderer.drawLine(scrollBarX, rect.y, scrollBarX, rect.y + scrollAreaHeight, true);
    renderer.fillRect(scrollBarX - LyraMetrics::values.scrollBarWidth, scrollBarY, LyraMetrics::values.scrollBarWidth,
                      scrollBarHeight, true);
  }

  // Draw selection
  int contentWidth =
      rect.width -
      (totalPages > 1 ? (LyraMetrics::values.scrollBarWidth + LyraMetrics::values.scrollBarRightOffset) : 1);
  if (selectedIndex >= 0) {
    renderer.fillRoundedRect(LyraMetrics::values.contentSidePadding, rect.y + selectedIndex % pageItems * rowHeight,
                             contentWidth - LyraMetrics::values.contentSidePadding * 2, rowHeight, cornerRadius,
                             Color::LightGray);
  }

  int textX = rect.x + LyraMetrics::values.contentSidePadding + hPaddingInSelection;
  int textWidth = contentWidth - LyraMetrics::values.contentSidePadding * 2 - hPaddingInSelection * 2;
  int iconSize;
  if (rowIcon != nullptr) {
    iconSize = (rowSubtitle != nullptr) ? mainMenuIconSize : listIconSize;
    textX += iconSize + hPaddingInSelection;
    textWidth -= iconSize + hPaddingInSelection;
  }

  // Draw all items
  const auto pageStartIndex = selectedIndex / pageItems * pageItems;
  int iconY = (rowSubtitle != nullptr) ? 16 : 10;
  for (int i = pageStartIndex; i < itemCount && i < pageStartIndex + pageItems; i++) {
    const int itemY = rect.y + (i % pageItems) * rowHeight;
    int rowTextWidth = textWidth;

    // Draw name
    int valueWidth = 0;
    std::string valueText = "";
    if (rowValue != nullptr) {
      valueText = rowValue(i);
      valueText = renderer.truncatedText(UI_10_FONT_ID, valueText.c_str(), maxListValueWidth);
      valueWidth = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str()) + hPaddingInSelection;
      rowTextWidth -= valueWidth;
    }

    auto itemName = rowTitle(i);
    auto item = renderer.truncatedText(UI_10_FONT_ID, itemName.c_str(), rowTextWidth);
    renderer.drawText(UI_10_FONT_ID, textX, itemY + 7, item.c_str(), true);

    if (rowIcon != nullptr) {
      const int iconBaseX = rect.x + LyraMetrics::values.contentSidePadding + hPaddingInSelection;
      const int iconBaseY = itemY + iconY;

      if (rowCompleted != nullptr && rowCompleted(i)) {
        const int badgeSize = iconSize >= 32 ? 18 : 14;
        const int badgeX = iconBaseX + std::max(0, (iconSize - badgeSize) / 2);
        const int badgeY = iconBaseY + std::max(0, (iconSize - badgeSize) / 2);
        drawCompletedListBadge(renderer, badgeX, badgeY, badgeSize);
      } else {
        UIIcon icon = rowIcon(i);
        (void)drawUiIcon(renderer, icon, iconBaseX, iconBaseY, iconSize);
      }
    }

    if (rowSubtitle != nullptr) {
      // Draw subtitle
      std::string subtitleText = rowSubtitle(i);
      auto subtitle = renderer.truncatedText(SMALL_FONT_ID, subtitleText.c_str(), rowTextWidth);
      renderer.drawText(SMALL_FONT_ID, textX, itemY + 30, subtitle.c_str(), true);
    }

    // Draw value
    if (!valueText.empty()) {
      if (i == selectedIndex && highlightValue) {
        renderer.fillRoundedRect(
            contentWidth - LyraMetrics::values.contentSidePadding - hPaddingInSelection - valueWidth, itemY,
            valueWidth + hPaddingInSelection, rowHeight, cornerRadius, Color::Black);
      }

      renderer.drawText(UI_10_FONT_ID, rect.x + contentWidth - LyraMetrics::values.contentSidePadding - valueWidth,
                        itemY + 6, valueText.c_str(), !(i == selectedIndex && highlightValue));
    }
  }
}

void LyraTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                const char* btn4) const {
  if (gpio.hasTouch()) {
    return;
  }

  const GfxRenderer::Orientation orig_orientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageHeight = renderer.getScreenHeight();
  constexpr int buttonWidth = 80;
  constexpr int smallButtonHeight = 15;
  constexpr int buttonHeight = LyraMetrics::values.buttonHintsHeight;
  constexpr int buttonY = LyraMetrics::values.buttonHintsHeight;  // Distance from bottom
  constexpr int textYOffset = 7;                                  // Distance from top of button to text baseline
  // Keyed to the portrait panel width: the 528-wide X3 gets more spacing than
  // the 480-wide boards (X4, X4 Pro, and the other 800x480 panels).
  constexpr int narrowButtonPositions[] = {58, 146, 254, 342};
  constexpr int wideButtonPositions[] = {65, 157, 291, 383};
  const int* buttonPositions = renderer.getScreenWidth() >= 528 ? wideButtonPositions : narrowButtonPositions;
  const char* labels[] = {btn1, btn2, btn3, btn4};

  for (int i = 0; i < 4; i++) {
    const int x = buttonPositions[i];
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      // Draw the filled background and border for a FULL-sized button
      renderer.fillRoundedRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, cornerRadius, Color::White);
      renderer.drawRoundedRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, 1, cornerRadius, true, true, false,
                               false, true);
      drawHintLabel(renderer, SMALL_FONT_ID, labels[i], x, buttonWidth, pageHeight - buttonY, buttonHeight,
                    textYOffset);
    } else {
      // Draw the filled background and border for a SMALL-sized button
      renderer.fillRoundedRect(x, pageHeight - smallButtonHeight, buttonWidth, smallButtonHeight, cornerRadius,
                               Color::White);
      renderer.drawRoundedRect(x, pageHeight - smallButtonHeight, buttonWidth, smallButtonHeight, 1, cornerRadius, true,
                               true, false, false, true);
    }
  }

  renderer.setOrientation(orig_orientation);
}

void LyraTheme::drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const {
  if (gpio.hasTouch()) {
    return;
  }

  const int screenWidth = renderer.getScreenWidth();
  constexpr int buttonWidth = LyraMetrics::values.sideButtonHintsWidth;  // Width on screen (height when rotated)
  constexpr int buttonHeight = 78;                                       // Height on screen (width when rotated)
  constexpr int buttonMargin = 0;

  if (gpio.hasEdgeSideButtons()) {
    // Edge-button layout (X3, X4 Pro): Up on left side, Down on right side, positioned higher
    constexpr int x3ButtonY = 155;

    if (topBtn != nullptr && topBtn[0] != '\0') {
      renderer.drawRoundedRect(buttonMargin, x3ButtonY, buttonWidth, buttonHeight, 1, cornerRadius, false, true, false,
                               true, true);
      const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, topBtn);
      renderer.drawTextRotated90CW(SMALL_FONT_ID, buttonMargin, x3ButtonY + (buttonHeight + textWidth) / 2, topBtn);
    }

    if (bottomBtn != nullptr && bottomBtn[0] != '\0') {
      const int rightX = screenWidth - buttonWidth;
      renderer.drawRoundedRect(rightX, x3ButtonY, buttonWidth, buttonHeight, 1, cornerRadius, true, false, true, false,
                               true);
      const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, bottomBtn);
      renderer.drawTextRotated90CW(SMALL_FONT_ID, rightX, x3ButtonY + (buttonHeight + textWidth) / 2, bottomBtn);
    }
  } else {
    // X4 layout: Both buttons stacked on right side
    const char* labels[] = {topBtn, bottomBtn};
    const int x = screenWidth - buttonWidth;

    if (topBtn != nullptr && topBtn[0] != '\0') {
      renderer.drawRoundedRect(x, topHintButtonY, buttonWidth, buttonHeight, 1, cornerRadius, true, false, true, false,
                               true);
    }

    if (bottomBtn != nullptr && bottomBtn[0] != '\0') {
      renderer.drawRoundedRect(x, topHintButtonY + buttonHeight + 5, buttonWidth, buttonHeight, 1, cornerRadius, true,
                               false, true, false, true);
    }

    for (int i = 0; i < 2; i++) {
      if (labels[i] != nullptr && labels[i][0] != '\0') {
        const int y = topHintButtonY + (i * buttonHeight) + 5;
        const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);
        renderer.drawTextRotated90CW(SMALL_FONT_ID, x, y + (buttonHeight + textWidth) / 2, labels[i]);
      }
    }
  }
}

void LyraTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                    const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                    bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  const int tileWidth = rect.width - 2 * LyraMetrics::values.contentSidePadding;
  const int tileHeight = rect.height;
  const int tileY = rect.y;
  const bool hasContinueReading = !recentBooks.empty();
  if (coverWidth == 0) {
    coverWidth = LyraMetrics::values.homeCoverHeight * 0.6;
  }

  // Draw book card regardless, fill with message based on `hasContinueReading`
  // Draw cover image as background if available (inside the box)
  // Only load from SD on first render, then use stored buffer
  if (hasContinueReading) {
    RecentBook book = recentBooks[0];
    if (!coverRendered) {
      std::string coverPath = book.coverBmpPath;
      bool hasCover = true;
      int tileX = LyraMetrics::values.contentSidePadding;
      if (coverPath.empty()) {
        hasCover = false;
      } else {
        const std::string coverBmpPath = UITheme::getCoverThumbPath(coverPath, LyraMetrics::values.homeCoverHeight);

        // First time: load cover from SD and render
        HalFile file;
        if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
          Bitmap bitmap(file);
          if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            coverWidth = bitmap.getWidth();
            renderer.drawBitmap(bitmap, tileX + hPaddingInSelection, tileY + hPaddingInSelection, coverWidth,
                                LyraMetrics::values.homeCoverHeight);
          } else {
            hasCover = false;
          }
          file.close();
        }
      }

      // Draw either way
      renderer.drawRect(tileX + hPaddingInSelection, tileY + hPaddingInSelection, coverWidth,
                        LyraMetrics::values.homeCoverHeight, true);

      if (!hasCover) {
        // Render empty cover
        renderer.fillRect(tileX + hPaddingInSelection,
                          tileY + hPaddingInSelection + (LyraMetrics::values.homeCoverHeight / 3), coverWidth,
                          2 * LyraMetrics::values.homeCoverHeight / 3, true);
        renderer.drawIcon(CoverIcon, tileX + hPaddingInSelection + 24, tileY + hPaddingInSelection + 24, 32, 32);
      }

      coverBufferStored = storeCoverBuffer();
      coverRendered = coverBufferStored;  // Only consider it rendered if we successfully stored the buffer
    }

    bool bookSelected = (selectorIndex == 0);

    int tileX = LyraMetrics::values.contentSidePadding;
    int textWidth = tileWidth - 2 * hPaddingInSelection - LyraMetrics::values.verticalSpacing - coverWidth;

    if (bookSelected) {
      // Draw selection box
      renderer.fillRoundedRect(tileX, tileY, tileWidth, hPaddingInSelection, cornerRadius, true, true, false, false,
                               Color::LightGray);
      renderer.fillRectDither(tileX, tileY + hPaddingInSelection, hPaddingInSelection,
                              LyraMetrics::values.homeCoverHeight, Color::LightGray);
      renderer.fillRectDither(tileX + hPaddingInSelection + coverWidth, tileY + hPaddingInSelection,
                              tileWidth - hPaddingInSelection - coverWidth, LyraMetrics::values.homeCoverHeight,
                              Color::LightGray);
      renderer.fillRoundedRect(tileX, tileY + LyraMetrics::values.homeCoverHeight + hPaddingInSelection, tileWidth,
                               hPaddingInSelection, cornerRadius, false, false, true, true, Color::LightGray);
    }

    const ReadingBookStats* stats = getRecentBookStats(book);
    const uint8_t progressPercent =
        stats != nullptr ? (stats->completed ? 100 : std::min<uint8_t>(stats->lastProgressPercent, 100)) : 0;
    const std::string progressText = std::to_string(progressPercent) + "%";
    const std::string statsText = stats != nullptr ? ReadingStatsAnalytics::formatDurationHm(stats->totalReadingMs) +
                                                         " | " + std::to_string(stats->sessions) + "x"
                                                   : std::string("0m | 0x");

    auto titleLines = renderer.wrappedText(UI_12_FONT_ID, book.title.c_str(), textWidth, 3, EpdFontFamily::BOLD);

    auto author = renderer.truncatedText(UI_10_FONT_ID, book.author.c_str(), textWidth);
    const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int titleBlockHeight = titleLineHeight * static_cast<int>(titleLines.size());
    const int authorHeight = book.author.empty() ? 0 : (renderer.getLineHeight(UI_10_FONT_ID) * 3 / 2);
    const int smallLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const int progressRowHeight = std::max(smallLineHeight, progressBarHeight);
    const int statsLineHeight = smallLineHeight;
    const int progressTopGap = 14;
    const int statsTopGap = 7;
    const int totalBlockHeight =
        titleBlockHeight + authorHeight + progressTopGap + progressRowHeight + statsTopGap + statsLineHeight;
    int currentY = tileY + tileHeight / 2 - totalBlockHeight / 2;
    const int textX = tileX + hPaddingInSelection + coverWidth + LyraMetrics::values.verticalSpacing;
    for (const auto& line : titleLines) {
      renderer.drawText(UI_12_FONT_ID, textX, currentY, line.c_str(), true, EpdFontFamily::BOLD);
      currentY += titleLineHeight;
    }
    if (!book.author.empty()) {
      currentY += renderer.getLineHeight(UI_10_FONT_ID) / 2;
      renderer.drawText(UI_10_FONT_ID, textX, currentY, author.c_str(), true);
      currentY += renderer.getLineHeight(UI_10_FONT_ID);
    }

    currentY += progressTopGap;
    const int progressTextWidth = renderer.getTextWidth(SMALL_FONT_ID, progressText.c_str(), EpdFontFamily::BOLD);
    const int progressBarWidth = std::max(24, textWidth - progressTextWidth - progressRowGap);
    const int progressBarY = currentY + std::max(0, (smallLineHeight - progressBarHeight) / 2);
    drawLyraProgressBar(renderer, Rect{textX, progressBarY, progressBarWidth, progressBarHeight}, progressPercent);
    renderer.drawText(SMALL_FONT_ID, textX + progressBarWidth + progressRowGap, currentY, progressText.c_str(), true,
                      EpdFontFamily::BOLD);

    currentY += progressRowHeight + statsTopGap;
    auto statsLine = renderer.truncatedText(SMALL_FONT_ID, statsText.c_str(), textWidth);
    renderer.drawText(SMALL_FONT_ID, textX, currentY, statsLine.c_str(), true);
  } else {
    drawEmptyRecents(renderer, rect);
  }
}

void LyraTheme::drawEmptyRecents(const GfxRenderer& renderer, const Rect rect) const {
  constexpr int padding = 48;
  renderer.drawText(UI_12_FONT_ID, rect.x + padding,
                    rect.y + rect.height / 2 - renderer.getLineHeight(UI_12_FONT_ID) - 2, tr(STR_NO_OPEN_BOOK), true,
                    EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, rect.x + padding, rect.y + rect.height / 2 + 2, tr(STR_START_READING), true);
}

void LyraTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                               const std::function<std::string(int index)>& buttonLabel,
                               const std::function<UIIcon(int index)>& rowIcon,
                               const std::function<std::string(int index)>& buttonSubtitle,
                               const std::function<bool(int index)>& showAccessory) const {
  const int availableHeight = std::max(0, rect.height);
  const int gap = LyraMetrics::values.menuSpacing;
  const int rowHeight = buttonCount > 0 ? std::min(LyraMetrics::values.menuRowHeight,
                                                   (availableHeight - gap * std::max(0, buttonCount - 1)) / buttonCount)
                                        : LyraMetrics::values.menuRowHeight;

  for (int i = 0; i < buttonCount; ++i) {
    int tileWidth = rect.width - LyraMetrics::values.contentSidePadding * 2;
    Rect tileRect =
        Rect{rect.x + LyraMetrics::values.contentSidePadding, rect.y + i * (rowHeight + gap), tileWidth, rowHeight};

    const bool selected = selectedIndex == i;

    if (selected) {
      renderer.fillRoundedRect(tileRect.x, tileRect.y, tileRect.width, tileRect.height, cornerRadius, Color::LightGray);
    }

    std::string labelStr = buttonLabel(i);
    const char* label = labelStr.c_str();
    int textX = tileRect.x + 16;
    const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);

    if (rowIcon != nullptr) {
      UIIcon icon = rowIcon(i);
      const int iconY = tileRect.y + (tileRect.height - mainMenuIconSize) / 2 + 1;
      if (drawUiIcon(renderer, icon, textX, iconY, mainMenuIconSize)) {
        textX += mainMenuIconSize + hPaddingInSelection + 2;
      }
    }

    const std::string subtitle = buttonSubtitle ? buttonSubtitle(i) : std::string();
    const bool hasSubtitle = !subtitle.empty();
    if (hasSubtitle) {
      renderer.drawText(UI_12_FONT_ID, textX, tileRect.y + 8, label, true);

      const int badgeWidth = showAccessory && showAccessory(i) ? 20 : 0;
      const int subtitleMaxWidth = tileRect.width - (textX - tileRect.x) - 18 - badgeWidth;
      std::string subtitleStr =
          renderer.truncatedText(SMALL_FONT_ID, subtitle.c_str(), subtitleMaxWidth, EpdFontFamily::REGULAR);
      const int subtitleY = tileRect.y + 36;
      renderer.drawText(SMALL_FONT_ID, textX, subtitleY, subtitleStr.c_str(), true);

      if (showAccessory && showAccessory(i)) {
        const int badgeSize = 16;
        const int subtitleWidth = renderer.getTextWidth(SMALL_FONT_ID, subtitleStr.c_str());
        const int badgeX = std::min(tileRect.x + tileRect.width - badgeSize - 10, textX + subtitleWidth + 8);
        const int badgeY = subtitleY - 1;
        renderer.fillRect(badgeX, badgeY, badgeSize, badgeSize, true);
        renderer.drawLine(badgeX + 3, badgeY + 8, badgeX + 6, badgeY + 11, 2, false);
        renderer.drawLine(badgeX + 6, badgeY + 11, badgeX + 12, badgeY + 4, 2, false);
      }
    } else {
      const int textY = tileRect.y + (rowHeight - titleLineHeight) / 2;
      renderer.drawText(UI_12_FONT_ID, textX, textY, label, true);
    }
  }
}

void LyraTheme::drawKeyboardKey(const GfxRenderer& renderer, Rect rect, const char* label, const bool isSelected,
                                const char* secondaryLabel, const KeyboardKeyType keyType,
                                const bool inactiveSelection) const {
  if (isSelected) {
    if (inactiveSelection) {
      renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, cornerRadius, Color::LightGray);
    } else if (keyType == KeyboardKeyType::Disabled) {
      renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, cornerRadius, Color::LightGray);
    } else {
      renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, cornerRadius, Color::Black);
    }
  } else if (keyType == KeyboardKeyType::Shift || keyType == KeyboardKeyType::Mode || keyType == KeyboardKeyType::Del ||
             keyType == KeyboardKeyType::Space || keyType == KeyboardKeyType::Ok ||
             keyType == KeyboardKeyType::Disabled) {
    renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 1, cornerRadius, true);
  }

  const bool invert = isSelected && !inactiveSelection;

  if (keyType == KeyboardKeyType::Space) {
    const int lineHalfWidth = rect.width * 3 / 10;
    const int centerX = rect.x + rect.width / 2;
    const int lineY = rect.y + rect.height / 2 + 3;
    renderer.drawLine(centerX - lineHalfWidth, lineY, centerX + lineHalfWidth, lineY, 3, !invert);
    return;
  }

  if (keyType == KeyboardKeyType::Del) {
    const int centerX = rect.x + rect.width / 2;
    const int centerY = rect.y + rect.height / 2;
    const int arrowLen = rect.width / 4;
    const int arrowHead = arrowLen / 2;
    renderer.drawLine(centerX - arrowLen / 2, centerY, centerX + arrowLen / 2, centerY, 3, !invert);
    renderer.drawLine(centerX - arrowLen / 2, centerY, centerX - arrowLen / 2 + arrowHead, centerY - arrowHead, 3,
                      !invert);
    renderer.drawLine(centerX - arrowLen / 2, centerY, centerX - arrowLen / 2 + arrowHead, centerY + arrowHead, 3,
                      !invert);
    return;
  }

  const bool hasSecondary = secondaryLabel != nullptr && secondaryLabel[0] != '\0';
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, label);
  const int textX = rect.x + (rect.width - textWidth) / 2;
  const int textY = rect.y + (rect.height - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
  renderer.drawText(UI_12_FONT_ID, textX, textY, label, !invert);

  if (hasSecondary) {
    const int secWidth = renderer.getTextWidth(SMALL_FONT_ID, secondaryLabel);
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - secWidth - 1, rect.y, secondaryLabel, !invert);
  }
}
