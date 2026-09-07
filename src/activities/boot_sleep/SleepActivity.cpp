#include "SleepActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <PNGdec.h>
#include <Txt.h>
#include <Xtc.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
  auto dir = sleepDir.empty() ? FsFile{} : Storage.open(sleepDir.c_str());

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

  FsFile file;
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
      FsFile file;
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

  FsFile file;
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

void SleepActivity::renderBitmapSleepScreen(const Bitmap& bitmap, const std::string& sourcePath) const {
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
  renderer.clearScreen();

  const bool hasGreyscale = bitmap.hasGreyscale() &&
                            SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;

  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);

  if (SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  if (!sourcePath.empty() && canUseSleepCache(bitmap)) {
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

  FsFile file;
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

  FsFile file;
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
