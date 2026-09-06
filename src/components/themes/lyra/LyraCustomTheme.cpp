#include "LyraCustomTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"

namespace {
constexpr int H_PADDING = 8;
constexpr int CORNER_RADIUS = 6;
constexpr int PROGRESS_ROW_TOP = 8;
constexpr int PROGRESS_ROW_GAP = 8;
constexpr int PROGRESS_BAR_HEIGHT = 8;
constexpr int TITLE_TOP_GAP = 10;

uint8_t getBookProgressPercent(const RecentBook& recentBook) {
  for (const auto& book : READING_STATS.getBooks()) {
    if (book.path == recentBook.path) {
      return book.lastProgressPercent;
    }
  }
  return 0;
}

void drawMiniProgressBar(GfxRenderer& renderer, const Rect& rect, const uint8_t progressPercent) {
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height, true);
  const int fillWidth = std::max(0, (rect.width - 4) * std::min<int>(progressPercent, 100) / 100);
  if (fillWidth > 0) {
    renderer.fillRect(rect.x + 2, rect.y + 2, fillWidth, std::max(0, rect.height - 4), true);
  }
}
}  // namespace

void LyraCustomTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                          const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                          bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  const int tileWidth = (rect.width - 2 * LyraCustomMetrics::values.contentSidePadding) / 3;
  const int tileY = rect.y;
  const bool hasContinueReading = !recentBooks.empty();

  if (hasContinueReading) {
    if (!coverRendered) {
      for (int i = 0;
           i < std::min(static_cast<int>(recentBooks.size()), LyraCustomMetrics::values.homeRecentBooksCount); ++i) {
        std::string coverPath = recentBooks[i].coverBmpPath;
        bool hasCover = true;
        const int tileX = LyraCustomMetrics::values.contentSidePadding + tileWidth * i;
        if (coverPath.empty()) {
          hasCover = false;
        } else {
          const std::string coverBmpPath =
              UITheme::getCoverThumbPath(coverPath, LyraCustomMetrics::values.homeCoverHeight);

          HalFile file;
          if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
            Bitmap bitmap(file);
            if (bitmap.parseHeaders() == BmpReaderError::Ok) {
              const float coverHeight = static_cast<float>(bitmap.getHeight());
              const float coverWidth = static_cast<float>(bitmap.getWidth());
              const float ratio = coverWidth / coverHeight;
              const float tileRatio = static_cast<float>(tileWidth - 2 * H_PADDING) /
                                      static_cast<float>(LyraCustomMetrics::values.homeCoverHeight);
              const float cropX = 1.0f - (tileRatio / ratio);

              renderer.drawBitmap(bitmap, tileX + H_PADDING, tileY + H_PADDING, tileWidth - 2 * H_PADDING,
                                  LyraCustomMetrics::values.homeCoverHeight, cropX);
            } else {
              hasCover = false;
            }
            file.close();
          } else {
            hasCover = false;
          }
        }

        renderer.drawRect(tileX + H_PADDING, tileY + H_PADDING, tileWidth - 2 * H_PADDING,
                          LyraCustomMetrics::values.homeCoverHeight, true);

        if (!hasCover) {
          renderer.fillRect(tileX + H_PADDING, tileY + H_PADDING + (LyraCustomMetrics::values.homeCoverHeight / 3),
                            tileWidth - 2 * H_PADDING, 2 * LyraCustomMetrics::values.homeCoverHeight / 3, true);
          renderer.drawIcon(CoverIcon, tileX + H_PADDING + 24, tileY + H_PADDING + 24, 32, 32);
        }
      }

      coverBufferStored = storeCoverBuffer();
      coverRendered = coverBufferStored;
    }

    for (int i = 0; i < std::min(static_cast<int>(recentBooks.size()), LyraCustomMetrics::values.homeRecentBooksCount);
         ++i) {
      const bool bookSelected = (selectorIndex == i);
      const int tileX = LyraCustomMetrics::values.contentSidePadding + tileWidth * i;
      const int maxLineWidth = tileWidth - 2 * H_PADDING;

      const auto titleLines = renderer.wrappedText(SMALL_FONT_ID, recentBooks[i].title.c_str(), maxLineWidth, 3);
      const int titleLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
      const int titleBlockHeight = static_cast<int>(titleLines.size()) * titleLineHeight;
      const uint8_t progressPercent = getBookProgressPercent(recentBooks[i]);
      const std::string progressText = std::to_string(progressPercent) + "%";
      const int progressTextWidth = renderer.getTextWidth(SMALL_FONT_ID, progressText.c_str(), EpdFontFamily::BOLD);
      const int progressRowHeight = std::max(titleLineHeight, PROGRESS_BAR_HEIGHT);
      const int bottomBlockHeight =
          PROGRESS_ROW_TOP + progressRowHeight + TITLE_TOP_GAP + titleBlockHeight + H_PADDING + 5;

      if (bookSelected) {
        renderer.fillRoundedRect(tileX, tileY, tileWidth, H_PADDING, CORNER_RADIUS, true, true, false, false,
                                 Color::LightGray);
        renderer.fillRectDither(tileX, tileY + H_PADDING, H_PADDING, LyraCustomMetrics::values.homeCoverHeight,
                                Color::LightGray);
        renderer.fillRectDither(tileX + tileWidth - H_PADDING, tileY + H_PADDING, H_PADDING,
                                LyraCustomMetrics::values.homeCoverHeight, Color::LightGray);
        renderer.fillRoundedRect(tileX, tileY + LyraCustomMetrics::values.homeCoverHeight + H_PADDING, tileWidth,
                                 bottomBlockHeight, CORNER_RADIUS, false, false, true, true, Color::LightGray);
      }

      const int progressRowY = tileY + LyraCustomMetrics::values.homeCoverHeight + H_PADDING + PROGRESS_ROW_TOP;
      const int progressBarWidth = std::max(16, tileWidth - 2 * H_PADDING - progressTextWidth - PROGRESS_ROW_GAP);
      const int progressBarY = progressRowY + std::max(0, (titleLineHeight - PROGRESS_BAR_HEIGHT) / 2);

      drawMiniProgressBar(renderer, Rect{tileX + H_PADDING, progressBarY, progressBarWidth, PROGRESS_BAR_HEIGHT},
                          progressPercent);
      renderer.drawText(SMALL_FONT_ID, tileX + H_PADDING + progressBarWidth + PROGRESS_ROW_GAP, progressRowY,
                        progressText.c_str(), true, EpdFontFamily::BOLD);

      int currentY = progressRowY + progressRowHeight + TITLE_TOP_GAP;
      for (const auto& line : titleLines) {
        renderer.drawText(SMALL_FONT_ID, tileX + H_PADDING, currentY, line.c_str(), true);
        currentY += titleLineHeight;
      }
    }
  } else {
    drawEmptyRecents(renderer, rect);
  }
}
