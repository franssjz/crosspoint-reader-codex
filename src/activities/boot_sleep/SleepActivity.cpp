#include "SleepActivity.h"

#include <Epub.h>
#include <Epub/converters/PngToFramebufferConverter.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <PNGdec.h>
#include <Txt.h>
#include <Xtc.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "AchievementsStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "ReadingStatsStore.h"
#include "SdCardFontGlobals.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/Logo.h"
#include "images/MoonIcon.h"
#include "util/PngSleepRenderer.h"
#include "util/ReadingStatsAnalytics.h"
#include "util/SleepImageUtils.h"
#include "util/SleepScreenCache.h"

namespace {
bool canUseSleepCache(const Bitmap& bitmap) {
  return !(bitmap.hasGreyscale() &&
           SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER);
}

bool usesCustomSleepImages() {
  return SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM ||
         (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM &&
          !APP_STATE.lastSleepFromReader);
}

HalDisplay::RefreshMode sleepRefreshMode() {
  return SETTINGS.cleanSleepRefresh ? HalDisplay::FULL_REFRESH : HalDisplay::HALF_REFRESH;
}

void displaySleepBuffer(const GfxRenderer& renderer) {
  renderer.clearNextRefreshOverride();
  renderer.displayBuffer(sleepRefreshMode());
}

void displaySleepGrayscaleBase(const GfxRenderer& renderer) {
  renderer.clearNextRefreshOverride();
  const auto mode = sleepRefreshMode();
  if (mode == HalDisplay::FULL_REFRESH) {
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    renderer.preconditionGrayscale();
    return;
  }
  renderer.displayGrayscaleBase(mode);
}

template <typename RenderFn>
void renderSleepGrayscaleOverlay(GfxRenderer& renderer, RenderFn&& renderFn) {
  displaySleepGrayscaleBase(renderer);

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  renderFn();
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderFn();
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
}

int percentOf(const uint64_t value, const uint64_t target) {
  if (target == 0) {
    return 0;
  }
  return static_cast<int>(std::min<uint64_t>(100, (value * 100ULL + target / 2ULL) / target));
}

std::string formatPercent(const int percent) { return std::to_string(std::clamp(percent, 0, 100)) + "%"; }

std::string formatBookTitleFromPath(const std::string& path) {
  std::string name = path;
  const size_t slash = name.find_last_of('/');
  if (slash != std::string::npos) {
    name = name.substr(slash + 1);
  }
  const size_t dot = name.find_last_of('.');
  if (dot != std::string::npos && dot > 0) {
    name = name.substr(0, dot);
  }
  return name.empty() ? std::string(tr(STR_READING_TIME)) : name;
}

const ReadingBookStats* getCurrentSleepBook() {
  if (APP_STATE.openEpubPath.empty()) {
    return nullptr;
  }
  return READING_STATS.findMatchingBookForPath(APP_STATE.openEpubPath);
}

std::string getCurrentBookTitle() {
  if (const auto* book = getCurrentSleepBook()) {
    if (!book->title.empty()) {
      return book->title;
    }
  }
  return formatBookTitleFromPath(APP_STATE.openEpubPath);
}

uint8_t getCurrentBookProgress() {
  if (const auto* book = getCurrentSleepBook()) {
    return book->lastProgressPercent;
  }
  return 0;
}

std::string getSleepBookTitle(const ReadingBookStats& book) {
  if (!book.title.empty()) {
    return book.title;
  }
  return formatBookTitleFromPath(book.path);
}

std::string getSleepBookSubtitle(const ReadingBookStats& book) {
  if (!book.author.empty()) {
    return book.author;
  }
  return book.completed ? std::string(tr(STR_DONE)) : std::string(tr(STR_IN_PROGRESS));
}

std::vector<const ReadingBookStats*> getRecentSleepBooks(const size_t limit) {
  std::vector<const ReadingBookStats*> books;
  for (const auto& book : READING_STATS.getBooks()) {
    if (book.lastReadAt == 0 && book.totalReadingMs == 0 && book.lastProgressPercent == 0) {
      continue;
    }
    books.push_back(&book);
  }

  std::sort(books.begin(), books.end(), [](const ReadingBookStats* a, const ReadingBookStats* b) {
    if (a->lastReadAt != b->lastReadAt) {
      return a->lastReadAt > b->lastReadAt;
    }
    if (a->totalReadingMs != b->totalReadingMs) {
      return a->totalReadingMs > b->totalReadingMs;
    }
    return getSleepBookTitle(*a) < getSleepBookTitle(*b);
  });

  if (books.size() > limit) {
    books.resize(limit);
  }
  return books;
}

void drawTextClipped(const GfxRenderer& renderer, const int fontId, const int x, const int y, const std::string& text,
                     const int maxWidth, const bool black = true,
                     const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  renderer.drawText(fontId, x, y, renderer.truncatedText(fontId, text.c_str(), maxWidth, style).c_str(), black, style);
}

void drawRightText(const GfxRenderer& renderer, const int fontId, const int right, const int y, const std::string& text,
                   const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  renderer.drawText(fontId, right - renderer.getTextWidth(fontId, text.c_str(), style), y, text.c_str(), true, style);
}

void drawTextWithRightValue(const GfxRenderer& renderer, const int fontId, const int x, const int right, const int y,
                            const std::string& text, const std::string& value,
                            const EpdFontFamily::Style textStyle = EpdFontFamily::REGULAR,
                            const EpdFontFamily::Style valueStyle = EpdFontFamily::REGULAR) {
  const int valueWidth = renderer.getTextWidth(fontId, value.c_str(), valueStyle);
  const int textWidth = std::max(0, right - x - valueWidth - 8);
  drawTextClipped(renderer, fontId, x, y, text, textWidth, true, textStyle);
  drawRightText(renderer, fontId, right, y, value, valueStyle);
}

void drawProgressBar(const GfxRenderer& renderer, const Rect& rect, const int percent, const int lineWidth = 2) {
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height, lineWidth, true);
  const int innerX = rect.x + lineWidth + 2;
  const int innerY = rect.y + lineWidth + 2;
  const int innerW = std::max(0, rect.width - 2 * (lineWidth + 2));
  const int innerH = std::max(0, rect.height - 2 * (lineWidth + 2));
  const int fillW = std::clamp((innerW * std::clamp(percent, 0, 100) + 50) / 100, 0, innerW);
  if (fillW > 0 && innerH > 0) {
    renderer.fillRect(innerX, innerY, fillW, innerH, true);
  }
}

void drawCheckBox(const GfxRenderer& renderer, const int x, const int y, const bool checked) {
  renderer.drawRect(x, y, 16, 16, 1, true);
  if (!checked) {
    return;
  }

  renderer.fillRect(x, y, 16, 16, true);
  renderer.drawLine(x + 4, y + 9, x + 7, y + 12, 2, false);
  renderer.drawLine(x + 7, y + 12, x + 12, y + 5, 2, false);
}

void drawMetricPanel(const GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value) {
  renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 2, 7, true);
  drawTextClipped(renderer, SMALL_FONT_ID, rect.x + 13, rect.y + 15, label, rect.width - 26);
  drawTextClipped(renderer, UI_12_FONT_ID, rect.x + 13, rect.y + 39, value, rect.width - 26, true, EpdFontFamily::BOLD);
}

std::string formatAchievementProgress(const AchievementView& view) {
  const uint64_t progress = std::min(view.progress, view.target);
  if (view.definition == nullptr) {
    return "";
  }
  switch (view.definition->metric) {
    case AchievementMetric::TotalReadingMs:
    case AchievementMetric::MaxSessionMs:
      return ReadingStatsAnalytics::formatDurationHm(progress) + " / " +
             ReadingStatsAnalytics::formatDurationHm(view.target);
    default:
      return std::to_string(progress) + " / " + std::to_string(view.target);
  }
}

struct AchievementSleepLine {
  std::string label;
  std::string title;
  std::string progress;
  int percent = 0;
};

AchievementSleepLine getAchievementSleepLine() {
  AchievementSleepLine line;
  line.label = tr(STR_ACHIEVEMENTS);
  if (!SETTINGS.achievementsEnabled) {
    line.title = tr(STR_STATE_OFF);
    return line;
  }

  const auto views = ACHIEVEMENTS.buildViews();
  const AchievementView* latestUnlocked = nullptr;
  const AchievementView* nextLocked = nullptr;

  for (const auto& view : views) {
    if (!view.definition) {
      continue;
    }
    if (view.state.unlocked) {
      if (latestUnlocked == nullptr || view.state.unlockedAt > latestUnlocked->state.unlockedAt) {
        latestUnlocked = &view;
      }
      continue;
    }
    if (nextLocked == nullptr ||
        percentOf(view.progress, view.target) > percentOf(nextLocked->progress, nextLocked->target)) {
      nextLocked = &view;
    }
  }

  if (nextLocked) {
    line.label = tr(STR_NEXT_ACHIEVEMENT);
    line.title = ACHIEVEMENTS.getTitle(nextLocked->definition->id);
    line.progress = formatAchievementProgress(*nextLocked);
    line.percent = percentOf(nextLocked->progress, nextLocked->target);
  } else if (latestUnlocked) {
    line.label = tr(STR_LATEST_ACHIEVEMENT);
    line.title = ACHIEVEMENTS.getTitle(latestUnlocked->definition->id);
    line.percent = 100;
  } else {
    line.title = tr(STR_NO_PENDING_ACHIEVEMENTS);
  }
  return line;
}

void drawAchievementPanel(const GfxRenderer& renderer, const Rect& rect, const bool compact) {
  const auto achievement = getAchievementSleepLine();
  renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 2, 8, true);
  drawTextClipped(renderer, SMALL_FONT_ID, rect.x + 16, rect.y + 17, achievement.label, rect.width - 32, true,
                  EpdFontFamily::BOLD);

  if (compact) {
    drawTextClipped(renderer, UI_10_FONT_ID, rect.x + 16, rect.y + 47, achievement.title, rect.width - 32, true,
                    EpdFontFamily::BOLD);
    if (!achievement.progress.empty()) {
      drawProgressBar(renderer, Rect{rect.x + 16, rect.y + 80, rect.width - 32, 14}, achievement.percent, 1);
      drawRightText(renderer, SMALL_FONT_ID, rect.x + rect.width - 16, rect.y + 101, achievement.progress);
    }
    return;
  }

  const auto titleLines =
      renderer.wrappedText(UI_10_FONT_ID, achievement.title.c_str(), rect.width - 32, 2, EpdFontFamily::BOLD);
  int textY = rect.y + 42;
  for (const auto& line : titleLines) {
    renderer.drawText(UI_10_FONT_ID, rect.x + 16, textY, line.c_str(), true, EpdFontFamily::BOLD);
    textY += 22;
  }
  if (!achievement.progress.empty()) {
    const int barY = std::max(textY + 4, rect.y + rect.height - 59);
    drawProgressBar(renderer, Rect{rect.x + 16, barY, rect.width - 32, 14}, achievement.percent, 1);
    drawRightText(renderer, SMALL_FONT_ID, rect.x + rect.width - 16, barY + 22, achievement.progress);
  }
}

void drawLatestBookPanel(const GfxRenderer& renderer, const Rect& rect) {
  renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 2, 8, true);

  // Prefer the book currently open in the reader; fall back to latest stats when
  // the dashboard is shown without a current book context.
  const ReadingBookStats* selectedBook = getCurrentSleepBook();
  if (!selectedBook) {
    const auto books = getRecentSleepBooks(1);
    if (!books.empty()) {
      selectedBook = books.front();
    }
  }

  if (!selectedBook) {
    renderer.drawCenteredText(SMALL_FONT_ID, rect.y + rect.height / 2 - 6, tr(STR_NO_READING_STATS));
    return;
  }

  const int sidePadding = 14;
  const int innerX = rect.x + sidePadding;
  const int innerW = rect.width - sidePadding * 2;
  const ReadingBookStats& book = *selectedBook;

  drawTextClipped(renderer, UI_12_FONT_ID, innerX, rect.y + 22, getSleepBookTitle(book), innerW - 58, true,
                  EpdFontFamily::BOLD);
  drawRightText(renderer, UI_10_FONT_ID, rect.x + rect.width - sidePadding, rect.y + 24,
                formatPercent(book.lastProgressPercent), EpdFontFamily::BOLD);
  drawTextClipped(renderer, UI_10_FONT_ID, innerX, rect.y + 51, getSleepBookSubtitle(book), innerW);

  drawTextClipped(renderer, SMALL_FONT_ID, innerX, rect.y + 86, tr(STR_BOOK_PROGRESS), innerW - 58);
  drawRightText(renderer, SMALL_FONT_ID, rect.x + rect.width - sidePadding, rect.y + 86,
                formatPercent(book.lastProgressPercent));
  drawProgressBar(renderer, Rect{innerX, rect.y + 108, innerW, 13}, book.lastProgressPercent, 1);

  const std::string chapterTitle = book.chapterTitle.empty() ? std::string(tr(STR_CURRENT_CHAPTER)) : book.chapterTitle;
  drawTextClipped(renderer, SMALL_FONT_ID, innerX, rect.y + 140, tr(STR_CHAPTER_PROGRESS), innerW - 58);
  drawRightText(renderer, SMALL_FONT_ID, rect.x + rect.width - sidePadding, rect.y + 140,
                formatPercent(book.chapterProgressPercent));
  drawTextClipped(renderer, UI_10_FONT_ID, innerX, rect.y + 164, chapterTitle, innerW, true, EpdFontFamily::BOLD);
  drawProgressBar(renderer, Rect{innerX, rect.y + 192, innerW, 13}, book.chapterProgressPercent, 1);
}

void drawCoverStatsFooter(const GfxRenderer& renderer, const Rect& rect) {
  const int sideBarWidth = 10;
  const int padX = 16;
  const std::string dailyGoal = ReadingStatsAnalytics::formatDurationHm(READING_STATS.getTodayReadingMs()) + "/" +
                                ReadingStatsAnalytics::formatDurationHm(getDailyReadingGoalMs());
  const bool dailyGoalMet = getDailyReadingGoalMs() > 0 && READING_STATS.getTodayReadingMs() >= getDailyReadingGoalMs();
  const std::string streak = std::to_string(READING_STATS.getCurrentStreakDays()) + "d";
  const int globalX = rect.x + sideBarWidth + padX;
  const int globalRight = rect.x + rect.width - 14;

  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height, 1, true);
  renderer.fillRect(rect.x, rect.y, sideBarWidth, rect.height, true);

  const int goalCheckX = globalRight - 16;
  const int goalValueRight = goalCheckX - 8;
  const int goalValueWidth = renderer.getTextWidth(SMALL_FONT_ID, dailyGoal.c_str());
  const int goalLabelWidth = std::max(0, goalValueRight - globalX - goalValueWidth - 8);
  drawTextClipped(renderer, SMALL_FONT_ID, globalX, rect.y + 20, tr(STR_DAILY_GOAL), goalLabelWidth, true,
                  EpdFontFamily::BOLD);
  drawRightText(renderer, SMALL_FONT_ID, goalValueRight, rect.y + 20, dailyGoal);
  drawCheckBox(renderer, goalCheckX, rect.y + 12, dailyGoalMet);
  drawTextWithRightValue(renderer, SMALL_FONT_ID, globalX, globalRight, rect.y + 51, tr(STR_STREAK), streak,
                         EpdFontFamily::BOLD);
}

void drawCoverStatsOverlay(const GfxRenderer& renderer, const Rect& rect, const ReadingBookStats* book) {
  const int sideBarWidth = 10;
  const int padX = 16;
  const int bookProgress = book ? book->lastProgressPercent : getCurrentBookProgress();
  const int chapterProgress = book ? book->chapterProgressPercent : 0;
  const std::string title = book ? getSleepBookTitle(*book) : getCurrentBookTitle();
  const std::string author = book ? getSleepBookSubtitle(*book) : std::string(tr(STR_NOT_SET));
  const std::string chapterTitle =
      book && !book->chapterTitle.empty() ? book->chapterTitle : std::string(tr(STR_NOT_SET));
  const Rect bookRect{rect.x, rect.y, rect.width, 222};
  const Rect globalRect{rect.x, rect.y + bookRect.height + 12, rect.width, 84};
  const int bookX = bookRect.x + sideBarWidth + padX;
  const int bookRight = bookRect.x + bookRect.width - 14;
  const int bookWidth = bookRight - bookX;

  renderer.fillRect(bookRect.x, bookRect.y, bookRect.width, bookRect.height, false);
  renderer.drawRect(bookRect.x, bookRect.y, bookRect.width, bookRect.height, 1, true);
  renderer.fillRect(bookRect.x, bookRect.y, sideBarWidth, bookRect.height, true);

  drawTextClipped(renderer, UI_10_FONT_ID, bookX, bookRect.y + 24, title, bookWidth, true, EpdFontFamily::BOLD);
  drawTextClipped(renderer, SMALL_FONT_ID, bookX, bookRect.y + 53, author, bookWidth);
  renderer.drawLine(bookX, bookRect.y + 76, bookRight, bookRect.y + 76, true);

  drawTextWithRightValue(renderer, SMALL_FONT_ID, bookX, bookRight, bookRect.y + 101, tr(STR_BOOK_PROGRESS),
                         formatPercent(bookProgress));
  drawProgressBar(renderer, Rect{bookX, bookRect.y + 123, bookWidth, 11}, bookProgress, 1);

  drawTextClipped(renderer, SMALL_FONT_ID, bookX, bookRect.y + 155, tr(STR_CURRENT_CHAPTER), bookWidth);
  drawTextWithRightValue(renderer, SMALL_FONT_ID, bookX, bookRight, bookRect.y + 180, chapterTitle,
                         formatPercent(chapterProgress), EpdFontFamily::BOLD, EpdFontFamily::BOLD);
  drawProgressBar(renderer, Rect{bookX, bookRect.y + 204, bookWidth, 10}, chapterProgress, 1);

  drawCoverStatsFooter(renderer, globalRect);
}

void drawCoverStatsPanel(const GfxRenderer& renderer, const Rect& rect, const ReadingBookStats* book,
                         const bool footerOnly) {
  if (footerOnly) {
    drawCoverStatsFooter(renderer, rect);
  } else {
    drawCoverStatsOverlay(renderer, rect, book);
  }
}

struct BitmapPlacement {
  int x = 0;
  int y = 0;
  float cropX = 0.0f;
  float cropY = 0.0f;
};

// ---------------------------------------------------------------------------
// Transparent overlay sleep (upstream): alpha BMP/PNG art composited over the
// retained screen. Kept separate from /sleep.bmp and /.sleep so alpha-overlay
// art does not mix with full-screen wallpapers.
// ---------------------------------------------------------------------------
constexpr char TRANSPARENT_SLEEP_ROOT_BMP[] = "/sleep-overlay.bmp";
constexpr char TRANSPARENT_SLEEP_ROOT_PNG[] = "/sleep-overlay.png";
constexpr char TRANSPARENT_SLEEP_DIR[] = "/.sleep-overlay";
constexpr char TRANSPARENT_SLEEP_LEGACY_DIR[] = "/sleep-overlay";
constexpr size_t MAX_SLEEP_FILE_NAME_LEN = 256;
constexpr uint8_t MIN_VISIBLE_ALPHA = 8;

struct OverlayBmpInfo {
  int width = 0;
  int height = 0;
  bool topDown = false;
  uint32_t dataOffset = 0;
  uint32_t rowBytes = 0;
};

uint16_t readLE16(HalFile& file) {
  const int c0 = file.read();
  const int c1 = file.read();
  const auto b0 = static_cast<uint8_t>(c0 < 0 ? 0 : c0);
  const auto b1 = static_cast<uint8_t>(c1 < 0 ? 0 : c1);
  return static_cast<uint16_t>(b0) | (static_cast<uint16_t>(b1) << 8);
}

uint32_t readLE32(HalFile& file) {
  const int c0 = file.read();
  const int c1 = file.read();
  const int c2 = file.read();
  const int c3 = file.read();
  const auto b0 = static_cast<uint8_t>(c0 < 0 ? 0 : c0);
  const auto b1 = static_cast<uint8_t>(c1 < 0 ? 0 : c1);
  const auto b2 = static_cast<uint8_t>(c2 < 0 ? 0 : c2);
  const auto b3 = static_cast<uint8_t>(c3 < 0 ? 0 : c3);
  return static_cast<uint32_t>(b0) | (static_cast<uint32_t>(b1) << 8) | (static_cast<uint32_t>(b2) << 16) |
         (static_cast<uint32_t>(b3) << 24);
}

uint32_t readBE32(HalFile& file) {
  const int c0 = file.read();
  const int c1 = file.read();
  const int c2 = file.read();
  const int c3 = file.read();
  if (c0 < 0 || c1 < 0 || c2 < 0 || c3 < 0) return 0;
  return (static_cast<uint32_t>(c0) << 24) | (static_cast<uint32_t>(c1) << 16) | (static_cast<uint32_t>(c2) << 8) |
         static_cast<uint32_t>(c3);
}

bool isValidPngHeader(HalFile& file) {
  static constexpr uint8_t PNG_SIGNATURE[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  static constexpr uint32_t MAX_SOURCE_PIXELS = 2048u * 1536u;
  uint8_t signature[8];
  if (!file.seek(0) || file.read(signature, sizeof(signature)) != static_cast<int>(sizeof(signature)) ||
      !std::equal(std::begin(signature), std::end(signature), std::begin(PNG_SIGNATURE))) {
    return false;
  }

  const uint32_t ihdrLength = readBE32(file);
  char chunkType[4];
  if (file.read(reinterpret_cast<uint8_t*>(chunkType), sizeof(chunkType)) != static_cast<int>(sizeof(chunkType)) ||
      ihdrLength != 13 || !std::equal(std::begin(chunkType), std::end(chunkType), "IHDR")) {
    return false;
  }

  const uint32_t width = readBE32(file);
  const uint32_t height = readBE32(file);
  const int bitDepth = file.read();
  const int colorType = file.read();
  const int compression = file.read();
  const int filter = file.read();
  const int interlace = file.read();

  const bool supportedBitDepth =
      bitDepth == 8 || ((colorType == PNG_PIXEL_GRAYSCALE || colorType == PNG_PIXEL_INDEXED) &&
                        (bitDepth == 1 || bitDepth == 2 || bitDepth == 4));
  const bool supportedColorType = colorType == PNG_PIXEL_GRAYSCALE || colorType == PNG_PIXEL_TRUECOLOR ||
                                  colorType == PNG_PIXEL_INDEXED || colorType == PNG_PIXEL_GRAY_ALPHA ||
                                  colorType == PNG_PIXEL_TRUECOLOR_ALPHA;
  return width > 0 && height > 0 && width <= 2048 && height <= 3072 && width * height <= MAX_SOURCE_PIXELS &&
         supportedBitDepth && supportedColorType && compression == 0 && filter == 0 && interlace == 0;
}

BitmapPlacement calculateBitmapPlacement(const int bitmapWidth, const int bitmapHeight, const GfxRenderer& renderer) {
  BitmapPlacement placement;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (bitmapWidth > pageWidth || bitmapHeight > pageHeight) {
    float ratio = static_cast<float>(bitmapWidth) / static_cast<float>(bitmapHeight);
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    if (ratio > screenRatio) {
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        placement.cropX = 1.0f - (screenRatio / ratio);
        ratio = (1.0f - placement.cropX) * static_cast<float>(bitmapWidth) / static_cast<float>(bitmapHeight);
      }
      placement.x = 0;
      placement.y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
    } else {
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        placement.cropY = 1.0f - (ratio / screenRatio);
        ratio = static_cast<float>(bitmapWidth) / ((1.0f - placement.cropY) * static_cast<float>(bitmapHeight));
      }
      placement.x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      placement.y = 0;
    }
  } else {
    placement.x = (pageWidth - bitmapWidth) / 2;
    placement.y = (pageHeight - bitmapHeight) / 2;
  }

  return placement;
}

bool parseOverlayBmpHeader(HalFile& file, OverlayBmpInfo& info, const bool logErrors) {
  if (!file) return false;
  if (!file.seek(0)) return false;

  if (readLE16(file) != 0x4D42) {
    if (logErrors) LOG_ERR("SLP", "Transparent overlay is not a BMP");
    return false;
  }

  file.seekCur(8);
  info.dataOffset = readLE32(file);

  const uint32_t dibSize = readLE32(file);
  if (dibSize < 40) {
    if (logErrors) LOG_ERR("SLP", "Unsupported BMP DIB header: %u", static_cast<unsigned>(dibSize));
    return false;
  }

  info.width = static_cast<int32_t>(readLE32(file));
  const auto rawHeight = static_cast<int32_t>(readLE32(file));
  if (rawHeight == std::numeric_limits<int32_t>::min()) {
    if (logErrors) LOG_ERR("SLP", "Bad transparent overlay dimensions: %dx%d", info.width, rawHeight);
    return false;
  }
  info.topDown = rawHeight < 0;
  info.height = info.topDown ? -rawHeight : rawHeight;

  const uint16_t planes = readLE16(file);
  const uint16_t bpp = readLE16(file);
  const uint32_t compression = readLE32(file);

  // Match Bitmap::parseHeaders(): accept BI_RGB (0) and 32bpp BI_BITFIELDS (3), but keep the same
  // byte-layout assumption as custom sleep BMPs. The renderer below treats pixels as BGRA and does not parse masks.
  if (planes != 1 || bpp != 32 || !(compression == 0 || compression == 3)) {
    if (logErrors) {
      LOG_ERR("SLP", "Transparent overlay must be 32-bit BGRA BMP (planes=%u bpp=%u comp=%u)", planes, bpp,
              static_cast<unsigned>(compression));
    }
    return false;
  }

  constexpr int MAX_IMAGE_WIDTH = 2048;
  constexpr int MAX_IMAGE_HEIGHT = 3072;
  if (info.width <= 0 || info.height <= 0 || info.width > MAX_IMAGE_WIDTH || info.height > MAX_IMAGE_HEIGHT) {
    if (logErrors) LOG_ERR("SLP", "Bad transparent overlay dimensions: %dx%d", info.width, info.height);
    return false;
  }

  info.rowBytes = static_cast<uint32_t>(info.width) * 4u;
  if (!file.seek(info.dataOffset)) {
    if (logErrors) LOG_ERR("SLP", "Failed to seek transparent overlay pixel data");
    return false;
  }

  return true;
}

uint8_t bayerThreshold4x4(const int x, const int y) {
  static constexpr uint8_t BAYER_4X4[16] = {0, 128, 32, 160, 192, 64, 224, 96, 48, 176, 16, 144, 240, 112, 208, 80};
  return BAYER_4X4[((y & 0x03) << 2) | (x & 0x03)];
}

enum class TransparentOverlayPass : uint8_t { BW, GrayscaleLsb, GrayscaleMsb };

uint8_t quantizeOverlayLum(const uint8_t lum) {
  // Match Bitmap's native-palette path: 0, 85, 170, 255 map directly to levels 0..3.
  return lum >> 6;
}

bool renderTransparentOverlayPass(HalFile& file, const OverlayBmpInfo& info, const BitmapPlacement& placement,
                                  const GfxRenderer& renderer, uint8_t* row, const TransparentOverlayPass pass) {
  if (!file.seek(info.dataOffset)) {
    LOG_ERR("SLP", "Failed to seek transparent overlay pixel data");
    return false;
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int cropPixX = std::floor(info.width * placement.cropX / 2.0f);
  const int cropPixY = std::floor(info.height * placement.cropY / 2.0f);
  const float croppedWidth = (1.0f - placement.cropX) * static_cast<float>(info.width);
  const float croppedHeight = (1.0f - placement.cropY) * static_cast<float>(info.height);

  float scale = 1.0f;
  if (croppedWidth > 0.0f && croppedHeight > 0.0f) {
    const float widthScale = static_cast<float>(pageWidth) / croppedWidth;
    const float heightScale = static_cast<float>(pageHeight) / croppedHeight;
    scale = std::min(widthScale, heightScale);
    if (scale > 1.0f) scale = 1.0f;
  }
  const bool isScaled = scale < 1.0f;

  for (int bmpY = 0; bmpY < info.height; bmpY++) {
    if (file.read(row, info.rowBytes) != static_cast<int>(info.rowBytes)) {
      LOG_ERR("SLP", "Short read in transparent overlay row %d", bmpY);
      return false;
    }

    int screenY = -cropPixY + (info.topDown ? bmpY : info.height - 1 - bmpY);
    if (isScaled) screenY = std::floor(screenY * scale);
    screenY += placement.y;

    if (screenY >= pageHeight) {
      if (info.topDown) break;
      continue;
    }
    if (screenY < 0) {
      if (!info.topDown) break;
      continue;
    }

    for (int bmpX = cropPixX; bmpX < info.width - cropPixX; bmpX++) {
      int screenX = bmpX - cropPixX;
      if (isScaled) screenX = std::floor(screenX * scale);
      screenX += placement.x;

      if (screenX >= renderer.getScreenWidth()) break;
      if (screenX < 0) continue;

      const uint8_t* pixel = row + (static_cast<size_t>(bmpX) * 4u);
      const uint8_t alpha = pixel[3];
      if (alpha < MIN_VISIBLE_ALPHA || alpha <= bayerThreshold4x4(screenX, screenY)) continue;

      const uint8_t lum = (77u * pixel[2] + 150u * pixel[1] + 29u * pixel[0]) >> 8;
      const uint8_t level = quantizeOverlayLum(lum);

      switch (pass) {
        case TransparentOverlayPass::BW:
          // Same first pass as custom bitmap sleep: all non-white levels are painted black.
          // Transparent overlay's only difference is that opaque white explicitly erases underlying text.
          renderer.drawPixel(screenX, screenY, level < 3);
          break;
        case TransparentOverlayPass::GrayscaleLsb:
          if (level == 1) renderer.drawPixel(screenX, screenY, false);
          break;
        case TransparentOverlayPass::GrayscaleMsb:
          if (level == 1 || level == 2) renderer.drawPixel(screenX, screenY, false);
          break;
      }
    }
  }

  return true;
}

enum class AlphaOverlayResult : uint8_t { Rendered, NotAlphaOverlay, Error };
enum class AlphaScanResult : uint8_t { Useful, NotUseful, Error };

AlphaScanResult scanForUsefulAlpha(HalFile& file, const OverlayBmpInfo& info, uint8_t* row) {
  if (!file.seek(info.dataOffset)) {
    LOG_ERR("SLP", "Failed to seek transparent overlay pixel data");
    return AlphaScanResult::Error;
  }

  bool hasVisiblePixel = false;
  bool hasNonOpaquePixel = false;
  for (int bmpY = 0; bmpY < info.height; bmpY++) {
    if (file.read(row, info.rowBytes) != static_cast<int>(info.rowBytes)) {
      LOG_ERR("SLP", "Short read while checking transparent overlay row %d", bmpY);
      return AlphaScanResult::Error;
    }

    for (int bmpX = 0; bmpX < info.width; bmpX++) {
      const uint8_t alpha = row[static_cast<size_t>(bmpX) * 4u + 3u];
      hasVisiblePixel |= alpha >= MIN_VISIBLE_ALPHA;
      hasNonOpaquePixel |= alpha < 255;
      if (hasVisiblePixel && hasNonOpaquePixel) return AlphaScanResult::Useful;
    }
  }

  return AlphaScanResult::NotUseful;
}

AlphaOverlayResult tryRenderTransparentOverlayBmp(HalFile& file, GfxRenderer& renderer, const char* pathForLog) {
  OverlayBmpInfo info;
  if (!parseOverlayBmpHeader(file, info, false)) return AlphaOverlayResult::NotAlphaOverlay;

  const auto placement = calculateBitmapPlacement(info.width, info.height, renderer);
  auto row = makeUniqueNoThrow<uint8_t[]>(info.rowBytes);
  if (!row) {
    LOG_ERR("SLP", "OOM: transparent overlay row (%u bytes)", static_cast<unsigned>(info.rowBytes));
    return AlphaOverlayResult::Error;
  }

  const auto alphaScanResult = scanForUsefulAlpha(file, info, row.get());
  if (alphaScanResult == AlphaScanResult::Error) return AlphaOverlayResult::Error;
  if (alphaScanResult == AlphaScanResult::NotUseful) return AlphaOverlayResult::NotAlphaOverlay;

  LOG_DBG("SLP", "Rendering transparent overlay: %s (%dx%d)", pathForLog, info.width, info.height);

  if (!renderTransparentOverlayPass(file, info, placement, renderer, row.get(), TransparentOverlayPass::BW))
    return AlphaOverlayResult::Error;
  displaySleepGrayscaleBase(renderer);

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  if (!renderTransparentOverlayPass(file, info, placement, renderer, row.get(), TransparentOverlayPass::GrayscaleLsb)) {
    renderer.setRenderMode(GfxRenderer::BW);
    // The BW composite is already on the panel. Keep it instead of falling
    // through to another overlay with this grayscale work buffer cleared.
    return AlphaOverlayResult::Rendered;
  }
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  if (!renderTransparentOverlayPass(file, info, placement, renderer, row.get(), TransparentOverlayPass::GrayscaleMsb)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return AlphaOverlayResult::Rendered;
  }
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  return AlphaOverlayResult::Rendered;
}

bool findNextValidOverlayImage(HalFile& dir, char* name) {
  for (auto dirFile = dir.openNextFile(); dirFile; dirFile = dir.openNextFile()) {
    if (dirFile.isDirectory()) {
      dirFile.close();
      continue;
    }

    dirFile.getName(name, MAX_SLEEP_FILE_NAME_LEN);
    if (name[0] == '\0' || name[0] == '.') {
      dirFile.close();
      continue;
    }

    const bool isBmp = FsHelpers::hasBmpExtension(name);
    const bool isPng = FsHelpers::hasPngExtension(std::string_view{name});
    if (!isBmp && !isPng) {
      LOG_DBG("SLP", "Skipping unsupported sleep overlay: %s", name);
      dirFile.close();
      continue;
    }

    const bool isValid = isBmp ? [&dirFile]() {
      Bitmap bitmap(dirFile);
      return bitmap.parseHeaders() == BmpReaderError::Ok;
    }()
                               : isValidPngHeader(dirFile);
    dirFile.close();
    if (!isValid) {
      LOG_DBG("SLP", "Skipping invalid sleep overlay: %s", name);
      continue;
    }
    return true;
  }
  return false;
}

// Picks an overlay from dirPath, excluding recently shown ones (separate recent
// ring from the full-screen wallpapers). Two passes over the directory instead
// of collecting names, so the selection never allocates a filename vector.
bool selectRandomOverlayFile(const char* dirPath, std::string& selectedPath) {
  auto dir = Storage.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }

  auto name = makeUniqueNoThrow<char[]>(MAX_SLEEP_FILE_NAME_LEN);
  if (!name) {
    LOG_ERR("SLP", "OOM: sleep filename buffer");
    dir.close();
    return false;
  }

  uint16_t fileCount = 0;
  while (fileCount < UINT16_MAX && findNextValidOverlayImage(dir, name.get())) ++fileCount;
  if (fileCount == 0) {
    dir.close();
    return false;
  }

  const uint8_t window = static_cast<uint8_t>(std::min<uint16_t>(APP_STATE.recentOverlaySleepFill, fileCount - 1));
  auto randomFileIndex = static_cast<uint16_t>(random(fileCount));
  for (uint8_t attempt = 0; attempt < 20 && APP_STATE.isRecentOverlaySleep(randomFileIndex, window); attempt++) {
    randomFileIndex = static_cast<uint16_t>(random(fileCount));
  }

  dir.rewindDirectory();
  for (uint16_t index = 0; index <= randomFileIndex; ++index) {
    if (!findNextValidOverlayImage(dir, name.get())) {
      dir.close();
      return false;
    }
  }
  dir.close();

  selectedPath.reserve(strlen(dirPath) + 1 + strlen(name.get()));
  selectedPath = dirPath;
  selectedPath += "/";
  selectedPath += name.get();
  APP_STATE.pushRecentOverlaySleep(randomFileIndex);
  APP_STATE.saveToFile();
  return true;
}

void releaseSdFontCachesForDecode(const GfxRenderer& renderer) {
  if (auto* fcm = renderer.getFontCacheManager()) {
    LOG_DBG("SLP", "Free heap before SD font cache release: %d bytes", ESP.getFreeHeap());
    fcm->releaseSdFontCaches();
    LOG_DBG("SLP", "Free heap before sleep image decode: %d bytes", ESP.getFreeHeap());
  }
}

struct CustomSleepImage {
  std::string path;
  bool isPng = false;
  uint16_t index = UINT16_MAX;
};

void commitCustomSleepImage(const CustomSleepImage& selected) {
  if (selected.index == UINT16_MAX) {
    return;
  }
  APP_STATE.pushRecentSleep(selected.index);
  APP_STATE.saveToFile();
}

void releasePngSleepMemory(GfxRenderer& renderer, const bool releaseReadingStats) {
  // Sleep never returns to the current process, so reader-only SD-font state
  // can be discarded instead of merely trimming its caches. PNGdec still needs
  // a large contiguous decoder block plus separately allocated scanline storage.
  if (Storage.ready()) {
    sdFontSystem.releaseForNetwork(renderer);
  }
  if (releaseReadingStats && !READING_STATS.releaseMemoryForNetwork()) {
    LOG_ERR("SLP", "Failed to release reading stats before PNG sleep image");
  }
}

void showRestorableSleepPopup(GfxRenderer& renderer) {
  // Every currently selectable UI theme derives its popup layout from LyraTheme.
  // Keep this calculation local to sleep so the theme hierarchy and Home rendering
  // remain completely untouched by the PNG transition.
  constexpr int marginX = 16;
  constexpr int marginY = 12;
  constexpr int outline = 2;
  const char* message = tr(STR_ENTERING_SLEEP);
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, message, EpdFontFamily::REGULAR);
  const int textHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int width = textWidth + marginX * 2;
  const int height = textHeight + marginY * 2;
  const Rect popup{(renderer.getScreenWidth() - width) / 2, static_cast<int>(renderer.getScreenHeight() * 0.165f),
                   width, height};
  const Rect saved{popup.x - outline, popup.y - outline, popup.width + outline * 2, popup.height + outline * 2};
  const size_t savedSize = renderer.getRegionByteSize(saved.x, saved.y, saved.width, saved.height);
  if (savedSize == 0) {
    return;
  }

  uint8_t* savedPixels = static_cast<uint8_t*>(malloc(savedSize));
  if (!savedPixels) {
    LOG_ERR("SLP", "Skipping sleep popup: could not allocate %u-byte background", static_cast<unsigned>(savedSize));
    return;
  }
  if (!renderer.copyRegionToBuffer(saved.x, saved.y, saved.width, saved.height, savedPixels, savedSize)) {
    LOG_ERR("SLP", "Skipping sleep popup: could not save background");
    free(savedPixels);
    return;
  }

  GUI.drawPopup(renderer, message);
  delay(100);
  if (!renderer.copyBufferToRegion(saved.x, saved.y, saved.width, saved.height, savedPixels, savedSize)) {
    LOG_ERR("SLP", "Could not restore background after sleep popup");
  }
  free(savedPixels);
}

BitmapPlacement getBitmapPlacement(const Bitmap& bitmap, const Rect& target, const bool crop) {
  BitmapPlacement placement;
  placement.x = target.x;
  placement.y = target.y;

  float sourceW = static_cast<float>(bitmap.getWidth());
  float sourceH = static_cast<float>(bitmap.getHeight());
  if (sourceW <= 0.0f || sourceH <= 0.0f || target.width <= 0 || target.height <= 0) {
    return placement;
  }

  if (crop) {
    const float sourceRatio = sourceW / sourceH;
    const float targetRatio = static_cast<float>(target.width) / static_cast<float>(target.height);
    if (sourceRatio > targetRatio) {
      placement.cropX = 1.0f - (targetRatio / sourceRatio);
      sourceW *= 1.0f - placement.cropX;
    } else if (sourceRatio < targetRatio) {
      placement.cropY = 1.0f - (sourceRatio / targetRatio);
      sourceH *= 1.0f - placement.cropY;
    }
  }

  const float scale =
      std::min({1.0f, static_cast<float>(target.width) / sourceW, static_cast<float>(target.height) / sourceH});
  const int drawnW = static_cast<int>(std::round(sourceW * scale));
  const int drawnH = static_cast<int>(std::round(sourceH * scale));
  placement.x = target.x + std::max(0, (target.width - drawnW) / 2);
  placement.y = target.y + std::max(0, (target.height - drawnH) / 2);
  return placement;
}

void drawCoverBitmapInRect(const GfxRenderer& renderer, const Bitmap& bitmap, const Rect& target) {
  const bool crop = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;
  const BitmapPlacement placement = getBitmapPlacement(bitmap, target, crop);
  renderer.drawBitmap(bitmap, placement.x, placement.y, target.width, target.height, placement.cropX, placement.cropY);
}

BitmapPlacement getFullScreenBitmapPlacement(const Bitmap& bitmap, const int pageWidth, const int pageHeight) {
  BitmapPlacement placement;
  float cropX = 0.0f;
  float cropY = 0.0f;
  int x = 0;
  int y = 0;

  if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
    float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    if (ratio > screenRatio) {
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropX = 1.0f - (screenRatio / ratio);
        ratio = (1.0f - cropX) * static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
      }
      x = 0;
      y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
    } else {
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropY = 1.0f - (ratio / screenRatio);
        ratio = static_cast<float>(bitmap.getWidth()) / ((1.0f - cropY) * static_cast<float>(bitmap.getHeight()));
      }
      x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      y = 0;
    }
  } else {
    x = (pageWidth - bitmap.getWidth()) / 2;
    y = (pageHeight - bitmap.getHeight()) / 2;
  }

  placement.x = x;
  placement.y = y;
  placement.cropX = cropX;
  placement.cropY = cropY;
  return placement;
}

void drawFullScreenCoverBitmap(const GfxRenderer& renderer, const Bitmap& bitmap) {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const BitmapPlacement placement = getFullScreenBitmapPlacement(bitmap, pageWidth, pageHeight);
  renderer.drawBitmap(bitmap, placement.x, placement.y, pageWidth, pageHeight, placement.cropX, placement.cropY);
}

bool selectConfiguredCustomSleepImage(CustomSleepImage& selected) {
  const std::string sleepDir = SleepImageUtils::resolveConfiguredSleepDirectory();
  auto dir = sleepDir.empty() ? HalFile{} : Storage.open(sleepDir.c_str());

  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return false;
  }

  std::vector<std::string> files;
  // Most sleep collections are small; reserving 64 entries covers this common
  // case (including the reported 35-image folder) without repeated vector
  // growth and heap holes immediately before the PNG decoder allocation.
  files.reserve(64);
  char name[500];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (file.isDirectory()) {
      file.close();
      continue;
    }
    file.getName(name, sizeof(name));
    auto filename = std::string(name);
    if (filename.empty() || filename[0] == '.') {
      file.close();
      continue;
    }

    const bool isBmp = FsHelpers::hasBmpExtension(filename);
    const bool isPng = FsHelpers::hasPngExtension(filename);
    if (!isBmp && !isPng) {
      LOG_DBG("SLP", "Skipping unsupported sleep image: %s", name);
      file.close();
      continue;
    }

    if (isBmp) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() != BmpReaderError::Ok) {
        LOG_DBG("SLP", "Skipping invalid BMP file: %s", name);
        file.close();
        continue;
      }
    }

    files.emplace_back(filename);
    file.close();
  }
  dir.close();

  const auto numFiles = files.size();
  if (numFiles == 0) {
    return false;
  }

  uint16_t fileIndex = 0;
  const uint16_t recentIndex = APP_STATE.getMostRecentSleepIndex();
  if (SETTINGS.sleepImageOrder == CrossPointSettings::SLEEP_IMAGE_SEQUENTIAL) {
    if (recentIndex == UINT16_MAX || recentIndex >= numFiles - 1) {
      fileIndex = 0;
    } else {
      fileIndex = static_cast<uint16_t>(recentIndex + 1);
    }
  } else {
    const uint16_t fileCount = static_cast<uint16_t>(std::min(numFiles, static_cast<size_t>(UINT16_MAX)));
    const uint8_t window = static_cast<uint8_t>(std::min(static_cast<size_t>(APP_STATE.recentSleepFill), numFiles - 1));
    fileIndex = static_cast<uint16_t>(random(fileCount));
    for (uint8_t attempt = 0; attempt < 20 && APP_STATE.isRecentSleep(fileIndex, window); attempt++) {
      fileIndex = static_cast<uint16_t>(random(fileCount));
    }
  }

  selected.path = sleepDir + "/" + files[static_cast<size_t>(fileIndex)];
  selected.isPng = FsHelpers::hasPngExtension(files[static_cast<size_t>(fileIndex)]);
  selected.index = fileIndex;
  return !selected.path.empty();
}

bool drawPngSleepBackground(const GfxRenderer& renderer, const std::string& sourcePath) {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  return PngSleepRenderer::drawTransparentPng(sourcePath, renderer, 0, 0, pageWidth, pageHeight);
}

bool renderBitmapStatsSleepScreen(GfxRenderer& renderer, const std::string& sourcePath, const Rect& statsPanel,
                                  const ReadingBookStats* book, const bool footerOnly) {
  if (SleepScreenCache::load(renderer, sourcePath)) {
    drawCoverStatsPanel(renderer, statsPanel, book, footerOnly);
    displaySleepBuffer(renderer);
    return true;
  }

  HalFile file;
  if (!Storage.openFileForRead("SLP", sourcePath, file)) {
    return false;
  }

  Bitmap bitmap(file, true);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    file.close();
    return false;
  }

  renderer.clearScreen();
  drawFullScreenCoverBitmap(renderer, bitmap);
  if (SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }
  if (canUseSleepCache(bitmap)) {
    SleepScreenCache::save(renderer, sourcePath);
  }
  drawCoverStatsPanel(renderer, statsPanel, book, footerOnly);

  const bool hasGreyscale = bitmap.hasGreyscale() &&
                            SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;
  if (hasGreyscale) {
    renderSleepGrayscaleOverlay(renderer, [&]() {
      bitmap.rewindToData();
      drawFullScreenCoverBitmap(renderer, bitmap);
      drawCoverStatsPanel(renderer, statsPanel, book, footerOnly);
    });
  } else {
    displaySleepBuffer(renderer);
  }

  file.close();
  return true;
}
}  // namespace

void SleepActivity::onEnter() {
  Activity::onEnter();
  renderer.clearNextRefreshOverride();
  const bool restoreDarkMode = renderer.isDarkMode();
  if (restoreDarkMode) {
    renderer.setDarkMode(false);
  }

  // Sleep screens always use normal output polarity. This activity draws
  // directly from onEnter (outside ActivityManager's per-render polarity
  // resolution), so clear any inversion left over from a night-mode render.
  const bool frameWasInverted = display.isInverted();
  display.setInverted(false);

  // Quick Resume keeps the last screen (main.cpp persists the framebuffer after
  // this render, so the moon marker below is part of the saved frame).
  const bool renderQuickResume =
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME ||
      (fromTimeout &&
       SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT);
  if (renderQuickResume) {
    // Materialize any output-level inversion so the retained content keeps
    // its visible polarity after the display driver returns to normal.
    if (frameWasInverted) renderer.invertScreen();
    renderLastScreenSleepScreen();
    if (restoreDarkMode) {
      renderer.setDarkMode(true);
    }
    return;
  }

  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::TRANSPARENT_CUSTOM) {
    // Transparent mode retains the current framebuffer and composites the
    // overlay on top; the popup is drawn and undone so it never bakes in.
    if (frameWasInverted) renderer.invertScreen();
    if (APP_STATE.lastSleepFromReader) {
      ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    }
    showRestorableSleepPopup(renderer);
    if (APP_STATE.lastSleepFromReader) {
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
    }
    releasePngSleepMemory(renderer, true);
    releaseSdFontCachesForDecode(renderer);
    renderTransparentCustomSleepScreen();
    if (restoreDarkMode) {
      renderer.setDarkMode(true);
    }
    return;
  }

  if (APP_STATE.lastSleepFromReader) {
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    if (!usesCustomSleepImages()) {
      GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
    }
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  } else {
    if (!usesCustomSleepImages()) {
      GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
    }
  }

  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::BLANK):
      renderBlankSleepScreen();
      break;
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM):
      renderCustomSleepScreen();
      break;
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER):
      renderCoverSleepScreen();
      break;
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      if (APP_STATE.lastSleepFromReader) {
        renderCoverSleepScreen();
      } else {
        renderCustomSleepScreen();
      }
      break;
    case (CrossPointSettings::SLEEP_SCREEN_MODE::READING_DASHBOARD):
      renderReadingDashboardSleepScreen();
      break;
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_STATS):
      renderCoverStatsSleepScreen();
      break;
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_STATS_V2):
      renderCoverStatsSleepScreen(true);
      break;
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM_STATS):
      renderCustomStatsSleepScreen();
      break;
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM_STATS_V2):
      renderCustomStatsSleepScreen(true);
      break;
    default:
      renderDefaultSleepScreen();
      break;
  }

  if (restoreDarkMode) {
    renderer.setDarkMode(true);
  }
}

void SleepActivity::renderCustomSleepScreen() const {
  // Free reader-owned global memory before enumerating the custom directory;
  // otherwise the directory strings and state serialization can fragment the
  // contiguous block PNGdec needs after leaving a book.
  releasePngSleepMemory(renderer, true);

  CustomSleepImage selected;
  if (selectConfiguredCustomSleepImage(selected)) {
    if (selected.isPng) {
      showRestorableSleepPopup(renderer);
      if (renderPngSleepScreen(selected.path)) {
        commitCustomSleepImage(selected);
        return;
      }
    } else {
      GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
      HalFile file;
      if (SleepScreenCache::load(renderer, selected.path)) {
        displaySleepBuffer(renderer);
        commitCustomSleepImage(selected);
        return;
      }
      if (Storage.openFileForRead("SLP", selected.path, file)) {
        LOG_DBG("SLP", "Loading sleep image: %s", selected.path.c_str());
        delay(100);
        Bitmap bitmap(file, true);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          renderBitmapSleepScreen(bitmap, selected.path);
          file.close();
          commitCustomSleepImage(selected);
          return;
        }
        file.close();
      }
    }
  }

  HalFile file;
  if (Storage.openFileForRead("SLP", "/sleep.bmp", file)) {
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
    Bitmap bitmap(file, true);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Loading: /sleep.bmp");
      if (SleepScreenCache::load(renderer, "/sleep.bmp")) {
        displaySleepBuffer(renderer);
        file.close();
        return;
      }
      renderBitmapSleepScreen(bitmap, "/sleep.bmp");
      file.close();
      return;
    }
    file.close();
  }

  if (renderPngSleepScreen("/sleep.png")) {
    return;
  }

  renderDefaultSleepScreen();
}

void SleepActivity::renderDefaultSleepScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  constexpr int logoWidth = 174;
  constexpr int logoHeight = 24;
  constexpr int logoTextGap = 10;
  constexpr int subtitleGap = 25;
  const int logoX = (pageWidth - logoWidth) / 2;
  const int logoY = (pageHeight - logoHeight) / 2;
  const int titleY = logoY + logoHeight + logoTextGap;
  const int subtitleY = titleY + subtitleGap;

  renderer.clearScreen();
  renderer.drawIcon(Logo, logoX, logoY, logoWidth, logoHeight);
  renderer.drawCenteredText(UI_10_FONT_ID, titleY, tr(STR_CPR_VCODEX), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, subtitleY, tr(STR_SLEEPING));

  if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT) {
    renderer.invertScreen();
  }

  displaySleepBuffer(renderer);
}

void SleepActivity::renderBitmapSleepScreen(const Bitmap& bitmap, const std::string& sourcePath,
                                            const bool preserveBackground) const {
  int x, y;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  float cropX = 0;
  float cropY = 0;

  LOG_DBG("SLP", "bitmap %d x %d, screen %d x %d", bitmap.getWidth(), bitmap.getHeight(), pageWidth, pageHeight);
  if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
    float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    LOG_DBG("SLP", "bitmap ratio: %f, screen ratio: %f", ratio, screenRatio);
    if (ratio > screenRatio) {
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropX = 1.0f - (screenRatio / ratio);
        LOG_DBG("SLP", "Cropping bitmap x: %f", cropX);
        ratio = (1.0f - cropX) * static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
      }
      x = 0;
      y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
      LOG_DBG("SLP", "Centering with ratio %f to y=%d", ratio, y);
    } else {
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropY = 1.0f - (ratio / screenRatio);
        LOG_DBG("SLP", "Cropping bitmap y: %f", cropY);
        ratio = static_cast<float>(bitmap.getWidth()) / ((1.0f - cropY) * static_cast<float>(bitmap.getHeight()));
      }
      x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      y = 0;
      LOG_DBG("SLP", "Centering with ratio %f to x=%d", ratio, x);
    }
  } else {
    x = (pageWidth - bitmap.getWidth()) / 2;
    y = (pageHeight - bitmap.getHeight()) / 2;
  }

  LOG_DBG("SLP", "drawing to %d x %d", x, y);
  // Overlays draw over the retained frame: drawBitmap leaves white pixels
  // untouched, so skipping the clear makes them transparent.
  if (!preserveBackground) renderer.clearScreen();

  const bool hasGreyscale =
      bitmap.hasGreyscale() && (preserveBackground || SETTINGS.sleepScreenCoverFilter ==
                                                          CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER);

  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);

  if (!preserveBackground &&
      SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  if (!preserveBackground && !sourcePath.empty() && canUseSleepCache(bitmap)) {
    SleepScreenCache::save(renderer, sourcePath);
  }

  if (hasGreyscale) {
    renderSleepGrayscaleOverlay(renderer, [&]() {
      bitmap.rewindToData();
      renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    });
  } else {
    displaySleepBuffer(renderer);
  }
}

bool SleepActivity::renderPngSleepScreen(const std::string& sourcePath) const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  releasePngSleepMemory(renderer, true);
  if (!PngSleepRenderer::drawTransparentPng(sourcePath, renderer, 0, 0, pageWidth, pageHeight)) {
    return false;
  }

  displaySleepBuffer(renderer);
  return true;
}

bool SleepActivity::renderSleepOverlayFile(HalFile& file, const char* pathForLog) const {
  const auto alphaResult = tryRenderTransparentOverlayBmp(file, renderer, pathForLog);
  if (alphaResult == AlphaOverlayResult::Rendered) return true;
  if (alphaResult == AlphaOverlayResult::Error) return false;

  Bitmap bitmap(file);
  const auto parseResult = bitmap.parseHeaders();
  if (parseResult != BmpReaderError::Ok) {
    LOG_ERR("SLP", "Invalid sleep overlay BMP %s: %s", pathForLog, Bitmap::errorToString(parseResult));
    return false;
  }

  LOG_DBG("SLP", "Rendering regular BMP sleep overlay: %s (%dx%d)", pathForLog, bitmap.getWidth(), bitmap.getHeight());
  renderBitmapSleepScreen(bitmap, "", /*preserveBackground=*/true);
  return true;
}

bool SleepActivity::renderTransparentOverlayPng(const std::string& path) const {
  ImageDimensions dimensions;
  if (!PngToFramebufferConverter::getDimensionsStatic(path, dimensions)) return false;

  const auto placement = calculateBitmapPlacement(dimensions.width, dimensions.height, renderer);
  RenderConfig config;
  config.x = placement.x;
  config.y = placement.y;
  config.maxWidth = renderer.getScreenWidth();
  config.maxHeight = renderer.getScreenHeight();
  config.useDithering = false;
  config.sourceCropX = placement.cropX;
  config.sourceCropY = placement.cropY;
  config.useExactDimensions = placement.cropX > 0.0f || placement.cropY > 0.0f;
  config.preserveAlpha = true;

  PngToFramebufferConverter converter;
  LOG_DBG("SLP", "Rendering transparent PNG overlay: %s (%dx%d)", path.c_str(), dimensions.width, dimensions.height);

  if (!converter.decodeToFramebuffer(path, renderer, config)) return false;
  displaySleepGrayscaleBase(renderer);

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  if (!converter.decodeToFramebuffer(path, renderer, config)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return true;
  }
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  if (!converter.decodeToFramebuffer(path, renderer, config)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return true;
  }
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  return true;
}

bool SleepActivity::renderSleepOverlayPath(const std::string& path) const {
  if (FsHelpers::hasPngExtension(path)) {
    return Storage.exists(path.c_str()) && renderTransparentOverlayPng(path);
  }

  HalFile file;
  if (!Storage.openFileForRead("SLP", path, file)) return false;
  const bool rendered = renderSleepOverlayFile(file, path.c_str());
  file.close();
  return rendered;
}

void SleepActivity::renderTransparentCustomSleepScreen() const {
  if (renderSleepOverlayPath(TRANSPARENT_SLEEP_ROOT_BMP)) return;
  if (renderSleepOverlayPath(TRANSPARENT_SLEEP_ROOT_PNG)) return;

  std::string selectedPath;
  if (!selectRandomOverlayFile(TRANSPARENT_SLEEP_DIR, selectedPath)) {
    selectRandomOverlayFile(TRANSPARENT_SLEEP_LEGACY_DIR, selectedPath);
  }

  if (!selectedPath.empty() && renderSleepOverlayPath(selectedPath)) return;

  LOG_ERR("SLP", "No valid transparent sleep overlay found");
  renderDefaultSleepScreen();
}

void SleepActivity::renderLastScreenSleepScreen() const {
  const auto pageHeight = renderer.getScreenHeight();
  renderer.drawImage(MoonIcon, 0, pageHeight - MOONICON_HEIGHT, MOONICON_WIDTH, MOONICON_HEIGHT);
  if (gpio.deviceIsX3()) {
    // The controller still holds the displayed page, so its differential base
    // waveform can add the moon without a full-screen flash.
    renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
}

bool SleepActivity::resolveLastBookCoverPath(std::string& coverBmpPath) const {
  if (APP_STATE.openEpubPath.empty()) {
    return false;
  }

  const bool cropped = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;

  if (FsHelpers::hasXtcExtension(APP_STATE.openEpubPath)) {
    Xtc lastXtc(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastXtc.load()) {
      LOG_ERR("SLP", "Failed to load last XTC");
      return false;
    }

    if (!lastXtc.generateCoverBmp()) {
      LOG_ERR("SLP", "Failed to generate XTC cover bmp");
      return false;
    }

    coverBmpPath = lastXtc.getCoverBmpPath();
  } else if (FsHelpers::hasTxtExtension(APP_STATE.openEpubPath)) {
    Txt lastTxt(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastTxt.load()) {
      LOG_ERR("SLP", "Failed to load last TXT");
      return false;
    }

    if (!lastTxt.generateCoverBmp()) {
      LOG_ERR("SLP", "No cover image found for TXT file");
      return false;
    }

    coverBmpPath = lastTxt.getCoverBmpPath();
  } else if (FsHelpers::hasEpubExtension(APP_STATE.openEpubPath)) {
    Epub lastEpub(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastEpub.load(true, true)) {
      LOG_ERR("SLP", "Failed to load last epub");
      return false;
    }

    if (!lastEpub.generateCoverBmp(cropped)) {
      LOG_ERR("SLP", "Failed to generate cover bmp");
      return false;
    }

    coverBmpPath = lastEpub.getCoverBmpPath(cropped);
  } else {
    return false;
  }

  return !coverBmpPath.empty();
}

void SleepActivity::renderCoverSleepScreen() const {
  void (SleepActivity::*renderNoCoverSleepScreen)() const;
  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      renderNoCoverSleepScreen = &SleepActivity::renderCustomSleepScreen;
      break;
    default:
      renderNoCoverSleepScreen = &SleepActivity::renderDefaultSleepScreen;
      break;
  }

  std::string coverBmpPath;
  if (!resolveLastBookCoverPath(coverBmpPath)) {
    return (this->*renderNoCoverSleepScreen)();
  }

  HalFile file;
  if (SleepScreenCache::load(renderer, coverBmpPath)) {
    displaySleepBuffer(renderer);
    return;
  }
  if (Storage.openFileForRead("SLP", coverBmpPath, file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Rendering sleep cover: %s", coverBmpPath.c_str());
      renderBitmapSleepScreen(bitmap, coverBmpPath);
      file.close();
      return;
    }
    file.close();
  }

  return (this->*renderNoCoverSleepScreen)();
}

void SleepActivity::renderReadingDashboardSleepScreen() const {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int side = 32;
  const int contentWidth = pageWidth - side * 2;

  const uint64_t todayMs = READING_STATS.getTodayReadingMs();
  const uint64_t goalMs = getDailyReadingGoalMs();
  const int goalPercent = percentOf(todayMs, goalMs);
  const std::string todayValue =
      ReadingStatsAnalytics::formatDurationHm(todayMs) + " / " + ReadingStatsAnalytics::formatDurationHm(goalMs);

  renderer.clearScreen();
  renderer.drawText(SMALL_FONT_ID, side, 32, tr(STR_CPR_VCODEX));
  drawRightText(renderer, SMALL_FONT_ID, pageWidth - side, 32, tr(STR_SLEEPING));
  renderer.drawLine(side, 62, pageWidth - side, 62, true);

  const Rect goalPanel{side, 78, contentWidth, 104};
  renderer.drawRoundedRect(goalPanel.x, goalPanel.y, goalPanel.width, goalPanel.height, 2, 8, true);
  renderer.drawText(SMALL_FONT_ID, goalPanel.x + 18, goalPanel.y + 18, tr(STR_DAILY_GOAL), true, EpdFontFamily::BOLD);
  drawRightText(renderer, SMALL_FONT_ID, goalPanel.x + goalPanel.width - 18, goalPanel.y + 18,
                formatPercent(goalPercent));
  drawTextClipped(renderer, UI_12_FONT_ID, goalPanel.x + 18, goalPanel.y + 43, todayValue, goalPanel.width - 36, true,
                  EpdFontFamily::BOLD);
  drawProgressBar(renderer, Rect{goalPanel.x + 18, goalPanel.y + 78, goalPanel.width - 36, 13}, goalPercent, 1);

  const int cardGap = 10;
  const int cardWidth = (contentWidth - cardGap) / 2;
  const int cardHeight = 70;
  const int metricsTop = 198;
  drawMetricPanel(renderer, Rect{side, metricsTop, cardWidth, cardHeight}, tr(STR_STREAK),
                  std::to_string(READING_STATS.getCurrentStreakDays()) + "d");
  drawMetricPanel(renderer, Rect{side + cardWidth + cardGap, metricsTop, cardWidth, cardHeight}, tr(STR_LAST_7D),
                  ReadingStatsAnalytics::formatDurationHm(READING_STATS.getRecentReadingMs(7)));
  drawMetricPanel(renderer, Rect{side, metricsTop + cardHeight + cardGap, cardWidth, cardHeight}, tr(STR_TOTAL_TIME),
                  ReadingStatsAnalytics::formatDurationHm(READING_STATS.getTotalReadingMs()));
  drawMetricPanel(renderer, Rect{side + cardWidth + cardGap, metricsTop + cardHeight + cardGap, cardWidth, cardHeight},
                  tr(STR_BOOKS_FINISHED), std::to_string(READING_STATS.getBooksFinishedCount()));

  drawAchievementPanel(renderer, Rect{side, 370, contentWidth, 138}, false);
  drawLatestBookPanel(renderer, Rect{side, 530, contentWidth, pageHeight - 578});

  displaySleepBuffer(renderer);
}

void SleepActivity::renderCoverStatsSleepScreen(bool footerOnly) const {
  std::string coverBmpPath;
  if (!resolveLastBookCoverPath(coverBmpPath)) {
    renderReadingDashboardSleepScreen();
    return;
  }

  HalFile file;
  if (!Storage.openFileForRead("SLP", coverBmpPath, file)) {
    renderReadingDashboardSleepScreen();
    return;
  }

  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    file.close();
    renderReadingDashboardSleepScreen();
    return;
  }

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int overlayWidth = std::min(pageWidth - 156, 430);
  const int overlayHeight = footerOnly ? 84 : 318;
  const Rect statsPanel{(pageWidth - overlayWidth) / 2, pageHeight - overlayHeight - 42, overlayWidth, overlayHeight};
  const ReadingBookStats* book = footerOnly ? nullptr : getCurrentSleepBook();
  const bool hasGreyscale = bitmap.hasGreyscale() &&
                            SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;

  renderer.clearScreen();
  drawFullScreenCoverBitmap(renderer, bitmap);
  if (SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }
  drawCoverStatsPanel(renderer, statsPanel, book, footerOnly);

  if (hasGreyscale) {
    renderSleepGrayscaleOverlay(renderer, [&]() {
      bitmap.rewindToData();
      drawFullScreenCoverBitmap(renderer, bitmap);
      drawCoverStatsPanel(renderer, statsPanel, book, footerOnly);
    });
  } else {
    displaySleepBuffer(renderer);
  }

  file.close();
}

void SleepActivity::renderCustomStatsSleepScreen(bool footerOnly) const {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int overlayWidth = std::min(pageWidth - 156, 430);
  const int overlayHeight = footerOnly ? 84 : 318;
  const Rect statsPanel{(pageWidth - overlayWidth) / 2, pageHeight - overlayHeight - 42, overlayWidth, overlayHeight};
  const ReadingBookStats* book = footerOnly ? nullptr : getCurrentSleepBook();

  CustomSleepImage selected;
  if (selectConfiguredCustomSleepImage(selected)) {
    if (selected.isPng) {
      renderer.clearScreen();
      releasePngSleepMemory(renderer, false);
      if (drawPngSleepBackground(renderer, selected.path)) {
        drawCoverStatsPanel(renderer, statsPanel, book, footerOnly);
        displaySleepBuffer(renderer);
        commitCustomSleepImage(selected);
        return;
      }
    } else {
      if (renderBitmapStatsSleepScreen(renderer, selected.path, statsPanel, book, footerOnly)) {
        commitCustomSleepImage(selected);
        return;
      }
    }
  }

  if (renderBitmapStatsSleepScreen(renderer, "/sleep.bmp", statsPanel, book, footerOnly)) {
    return;
  }
  renderer.clearScreen();
  if (drawPngSleepBackground(renderer, "/sleep.png")) {
    drawCoverStatsPanel(renderer, statsPanel, book, footerOnly);
    displaySleepBuffer(renderer);
    return;
  }

  renderReadingDashboardSleepScreen();
}

void SleepActivity::renderBlankSleepScreen() const {
  renderer.clearScreen();
  displaySleepBuffer(renderer);
}
