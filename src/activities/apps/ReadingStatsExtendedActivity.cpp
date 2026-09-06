#include "ReadingStatsExtendedActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>
#include <vector>

#include "AppMetricCard.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/ReadingStatsAnalytics.h"
#include "util/TimeUtils.h"

namespace {
constexpr int SUMMARY_CARD_HEIGHT = 76;
constexpr int SUMMARY_GAP = 10;
constexpr int RECENT_CARD_HEIGHT = SUMMARY_CARD_HEIGHT;
constexpr int CHART_HEADER_HEIGHT = 34;
constexpr int CHART_HEIGHT = 180;
constexpr int CHART_TOP_GAP = 10;
constexpr int CHART_BOTTOM_GAP = 10;
constexpr int CHART_SECTION_GAP = 16;
constexpr int CHART_SCROLL_STEP = 110;

struct ChartBar {
  std::string bottomLabel;
  std::string topLabel;
  uint64_t readingMs = 0;
};

void drawMetricCard(GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value,
                    const bool showCheck = false) {
  AppMetricCard::Options options;
  options.showCheck = showCheck;
  AppMetricCard::draw(renderer, rect, label, value, options);
}

void drawRecentWindowCard(GfxRenderer& renderer, const Rect& rect, const char* periodLabel, const std::string& value) {
  drawMetricCard(renderer, rect, periodLabel, value);
}

void civilFromDays(int z, int& year, unsigned& month, unsigned& day) {
  z += 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  year = static_cast<int>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  day = doy - (153 * mp + 2) / 5 + 1;
  month = mp + (mp < 10 ? 3 : -9);
  year += (month <= 2);
}

std::string formatMinutesLabel(const uint64_t readingMs) {
  const uint64_t totalMinutes = readingMs / 60000ULL;
  if (totalMinutes == 0) {
    return "";
  }
  return std::to_string(totalMinutes) + "m";
}

std::string formatRoundedDurationLabel(const uint64_t readingMs) {
  if (readingMs == 0) {
    return "";
  }

  const uint64_t totalMinutes = readingMs / 60000ULL;
  if (totalMinutes < 60ULL) {
    return std::to_string(std::max<uint64_t>(1, totalMinutes)) + "m";
  }

  const uint64_t totalHours = (readingMs + (30ULL * 60ULL * 1000ULL)) / (60ULL * 60ULL * 1000ULL);
  if (totalHours < 24ULL) {
    return std::to_string(std::max<uint64_t>(1, totalHours)) + "h";
  }

  const uint64_t totalDays = (readingMs + (12ULL * 60ULL * 60ULL * 1000ULL)) / (24ULL * 60ULL * 60ULL * 1000ULL);
  return std::to_string(std::max<uint64_t>(1, totalDays)) + "d";
}

std::string formatDayLabel(const uint32_t dayOrdinal) {
  if (dayOrdinal == 0) {
    return "";
  }

  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  civilFromDays(static_cast<int>(dayOrdinal), year, month, day);

  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%02u/%02u", day, month);
  return buffer;
}

std::string formatMonthLabel(const unsigned month) {
  char buffer[8];
  snprintf(buffer, sizeof(buffer), "%02u", month);
  return buffer;
}

uint32_t getDisplayReferenceDayOrdinal() {
  const uint32_t displayTimestamp = READING_STATS.getDisplayTimestamp();
  if (!TimeUtils::isClockValid(displayTimestamp)) {
    return 0;
  }
  return TimeUtils::getLocalDayOrdinal(displayTimestamp);
}

int resolveReferenceYear(const std::vector<ReadingDayStats>& readingDays) {
  uint32_t referenceDayOrdinal = getDisplayReferenceDayOrdinal();
  if (referenceDayOrdinal == 0 && !readingDays.empty()) {
    referenceDayOrdinal = readingDays.back().dayOrdinal;
  }

  if (referenceDayOrdinal == 0) {
    return 0;
  }

  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  civilFromDays(static_cast<int>(referenceDayOrdinal), year, month, day);
  return year;
}

std::vector<ChartBar> getRecentDailyReadingBars() {
  std::vector<ChartBar> bars(7);
  const auto& readingDays = READING_STATS.getReadingDays();
  if (readingDays.empty()) {
    return bars;
  }

  uint32_t referenceDayOrdinal = getDisplayReferenceDayOrdinal();
  if (referenceDayOrdinal == 0) {
    referenceDayOrdinal = readingDays.back().dayOrdinal;
  }

  for (int index = 0; index < 7; ++index) {
    const uint32_t dayOrdinal = referenceDayOrdinal >= static_cast<uint32_t>(6 - index)
                                    ? referenceDayOrdinal - static_cast<uint32_t>(6 - index)
                                    : 0;
    bars[index].bottomLabel = formatDayLabel(dayOrdinal);
    for (const auto& day : readingDays) {
      if (day.dayOrdinal == dayOrdinal) {
        bars[index].readingMs = day.readingMs;
        bars[index].topLabel = formatMinutesLabel(day.readingMs);
        break;
      }
    }
  }

  return bars;
}

std::vector<ChartBar> getAnnualReadingBars(int& year) {
  std::vector<ChartBar> bars(12);
  for (unsigned month = 1; month <= 12; ++month) {
    bars[month - 1].bottomLabel = formatMonthLabel(month);
  }

  const auto& readingDays = READING_STATS.getReadingDays();
  year = resolveReferenceYear(readingDays);
  if (year == 0) {
    return bars;
  }

  for (const auto& day : readingDays) {
    int dayYear = 0;
    unsigned dayMonth = 0;
    unsigned dayNumber = 0;
    civilFromDays(static_cast<int>(day.dayOrdinal), dayYear, dayMonth, dayNumber);
    if (dayYear != year || dayMonth == 0 || dayMonth > 12) {
      continue;
    }
    bars[dayMonth - 1].readingMs += day.readingMs;
  }

  for (auto& bar : bars) {
    bar.topLabel = formatRoundedDurationLabel(bar.readingMs);
  }

  return bars;
}

std::string formatAnnualReadingTitle(const int year) {
  if (year <= 0) {
    return tr(STR_ANNUAL_READING);
  }
  return std::string(tr(STR_ANNUAL_READING)) + " (" + std::to_string(year) + ")";
}

int getScrollableContentBottom(const GfxRenderer&, const ThemeMetrics&) {
  return CHART_HEADER_HEIGHT + CHART_TOP_GAP + CHART_HEIGHT + CHART_SECTION_GAP + CHART_HEADER_HEIGHT + CHART_TOP_GAP +
         CHART_HEIGHT;
}

int getMaxScrollOffset(const GfxRenderer& renderer, const ThemeMetrics& metrics) {
  const int summaryTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int recentTop = summaryTop + SUMMARY_CARD_HEIGHT * 3 + SUMMARY_GAP * 2 + metrics.verticalSpacing;
  const int chartViewportTop = recentTop + RECENT_CARD_HEIGHT + metrics.verticalSpacing;
  const int visibleHeight =
      renderer.getScreenHeight() - metrics.buttonHintsHeight - CHART_BOTTOM_GAP - chartViewportTop;
  return std::max(0, getScrollableContentBottom(renderer, metrics) - visibleHeight);
}

void drawReadingChart(GfxRenderer& renderer, const Rect& rect, const std::vector<ChartBar>& bars,
                      const bool rotateBottomLabels) {
  if (bars.empty()) {
    return;
  }

  const int innerLeft = rect.x + 14;
  const int innerRight = rect.x + rect.width - 14;
  const int topLabelY = rect.y + 2;
  const int chartTop = rect.y + 30;
  const int bottomGap = rotateBottomLabels ? 12 : 10;
  const int bottomLabelAreaHeight = rotateBottomLabels ? 40 : 18;
  const int baselineY = rect.y + rect.height - bottomLabelAreaHeight - bottomGap - 2;
  const int bottomLabelY = baselineY + bottomGap;
  const int chartHeight = std::max(1, baselineY - chartTop);

  const int barCount = static_cast<int>(bars.size());
  const int barGap = barCount <= 7 ? 7 : 4;
  const int minBarWidth = barCount <= 7 ? 12 : 8;
  const int barWidth = std::max(minBarWidth, (innerRight - innerLeft - barGap * (barCount - 1)) / barCount);
  const int usedWidth = barWidth * barCount + barGap * (barCount - 1);
  const int chartLeft = rect.x + (rect.width - usedWidth) / 2;
  uint64_t maxValue = 1;
  for (const auto& bar : bars) {
    maxValue = std::max(maxValue, bar.readingMs);
  }

  renderer.drawLine(innerLeft - 2, baselineY, innerRight + 2, baselineY, 2, true);

  for (int index = 0; index < barCount; ++index) {
    const int barX = chartLeft + index * (barWidth + barGap);
    const uint64_t readingMs = bars[index].readingMs;
    if (!bars[index].topLabel.empty()) {
      const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, bars[index].topLabel.c_str(), EpdFontFamily::REGULAR);
      renderer.drawText(SMALL_FONT_ID, barX + (barWidth - labelWidth) / 2, topLabelY, bars[index].topLabel.c_str());
    }

    int barHeight = static_cast<int>((readingMs * chartHeight) / maxValue);
    if (readingMs > 0 && barHeight < 6) {
      barHeight = 6;
    }

    const int barY = baselineY - barHeight;
    if (barHeight > 0) {
      renderer.fillRectDither(barX + 1, barY + 1, std::max(0, barWidth - 2), std::max(0, barHeight - 2),
                              Color::LightGray);
      renderer.drawRect(barX, barY, barWidth, barHeight);
    } else {
      renderer.drawLine(barX, baselineY - 1, barX + barWidth, baselineY - 1);
    }

    if (bars[index].bottomLabel.empty()) {
      continue;
    }

    if (rotateBottomLabels) {
      const int labelWidth =
          renderer.getTextWidth(SMALL_FONT_ID, bars[index].bottomLabel.c_str(), EpdFontFamily::REGULAR);
      const int rotatedX = barX + (barWidth - renderer.getTextHeight(SMALL_FONT_ID)) / 2;
      const int rotatedY = bottomLabelY + (bottomLabelAreaHeight + labelWidth) / 2;
      renderer.drawTextRotated90CW(SMALL_FONT_ID, rotatedX, rotatedY, bars[index].bottomLabel.c_str());
    } else {
      const int labelWidth =
          renderer.getTextWidth(SMALL_FONT_ID, bars[index].bottomLabel.c_str(), EpdFontFamily::REGULAR);
      renderer.drawText(SMALL_FONT_ID, barX + (barWidth - labelWidth) / 2, bottomLabelY + 2,
                        bars[index].bottomLabel.c_str());
    }
  }
}
}  // namespace

void ReadingStatsExtendedActivity::onEnter() {
  Activity::onEnter();
  scrollOffset = 0;
  requestUpdate();
}

void ReadingStatsExtendedActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int maxScrollOffset = getMaxScrollOffset(renderer, metrics);

  // Touch: swipe up scrolls the charts up (content follows the finger), swipe
  // down scrolls back. Back is the left-edge swipe (Button::Back above).
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    const int delta = swipe == MappedInputManager::SwipeDir::Up ? CHART_SCROLL_STEP : -CHART_SCROLL_STEP;
    const int nextOffset = std::clamp(scrollOffset + delta, 0, maxScrollOffset);
    if (nextOffset != scrollOffset) {
      scrollOffset = nextOffset;
      requestUpdate();
    }
    return;
  }

  buttonNavigator.onPreviousRelease([&]() {
    const int nextOffset = std::max(0, scrollOffset - CHART_SCROLL_STEP);
    if (nextOffset != scrollOffset) {
      scrollOffset = nextOffset;
      requestUpdate();
    }
  });

  buttonNavigator.onNextRelease([&]() {
    const int nextOffset = std::min(maxScrollOffset, scrollOffset + CHART_SCROLL_STEP);
    if (nextOffset != scrollOffset) {
      scrollOffset = nextOffset;
      requestUpdate();
    }
  });
}

void ReadingStatsExtendedActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int sidePadding = metrics.contentSidePadding;
  const int cardWidth = (pageWidth - sidePadding * 2 - SUMMARY_GAP) / 2;
  const int summaryTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int recentTop = summaryTop + SUMMARY_CARD_HEIGHT * 3 + SUMMARY_GAP * 2 + metrics.verticalSpacing;
  const int chartViewportTop = recentTop + RECENT_CARD_HEIGHT + metrics.verticalSpacing;
  const int chartViewportBottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - CHART_BOTTOM_GAP;
  const int maxScrollOffset = getMaxScrollOffset(renderer, metrics);
  scrollOffset = std::clamp(scrollOffset, 0, maxScrollOffset);
  const int dailyChartHeaderTop = chartViewportTop - scrollOffset;
  const int dailyChartTop = dailyChartHeaderTop + CHART_HEADER_HEIGHT + CHART_TOP_GAP;
  const int annualChartHeaderTop = dailyChartTop + CHART_HEIGHT + CHART_SECTION_GAP;
  const int annualChartTop = annualChartHeaderTop + CHART_HEADER_HEIGHT + CHART_TOP_GAP;

  const std::string last7DaysValue = ReadingStatsAnalytics::formatDurationHm(READING_STATS.getRecentReadingMs(7));
  const std::string last30DaysValue = ReadingStatsAnalytics::formatDurationHm(READING_STATS.getRecentReadingMs(30));
  const uint64_t todayReadingMs = READING_STATS.getTodayReadingMs();
  const std::string dailyGoalValue = ReadingStatsAnalytics::formatDurationHm(todayReadingMs) + " / " +
                                     ReadingStatsAnalytics::formatDurationHm(getDailyReadingGoalMs());
  int annualReadingYear = 0;
  const auto annualReadingBars = getAnnualReadingBars(annualReadingYear);

  GUI.drawSubHeader(renderer, Rect{0, dailyChartHeaderTop, pageWidth, CHART_HEADER_HEIGHT}, tr(STR_DAILY_READING),
                    nullptr);
  drawReadingChart(renderer, Rect{sidePadding, dailyChartTop, pageWidth - sidePadding * 2, CHART_HEIGHT},
                   getRecentDailyReadingBars(), true);

  const std::string annualReadingTitle = formatAnnualReadingTitle(annualReadingYear);
  GUI.drawSubHeader(renderer, Rect{0, annualChartHeaderTop, pageWidth, CHART_HEADER_HEIGHT}, annualReadingTitle.c_str(),
                    nullptr);
  drawReadingChart(renderer, Rect{sidePadding, annualChartTop, pageWidth - sidePadding * 2, CHART_HEIGHT},
                   annualReadingBars, false);

  renderer.fillRect(0, 0, pageWidth, chartViewportTop, false);
  if (chartViewportBottom < renderer.getScreenHeight()) {
    renderer.fillRect(0, chartViewportBottom, pageWidth, renderer.getScreenHeight() - chartViewportBottom, false);
  }

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_READING_STATS), tr(STR_MORE_DETAILS));

  drawMetricCard(renderer, Rect{sidePadding, summaryTop, cardWidth, SUMMARY_CARD_HEIGHT}, tr(STR_STREAK),
                 std::to_string(READING_STATS.getCurrentStreakDays()));
  drawMetricCard(renderer, Rect{sidePadding + cardWidth + SUMMARY_GAP, summaryTop, cardWidth, SUMMARY_CARD_HEIGHT},
                 tr(STR_MAX_STREAK), std::to_string(READING_STATS.getMaxStreakDays()));
  drawMetricCard(renderer,
                 Rect{sidePadding, summaryTop + SUMMARY_CARD_HEIGHT + SUMMARY_GAP, cardWidth, SUMMARY_CARD_HEIGHT},
                 tr(STR_DAILY_GOAL), dailyGoalValue, todayReadingMs >= getDailyReadingGoalMs());
  drawMetricCard(renderer,
                 Rect{sidePadding + cardWidth + SUMMARY_GAP, summaryTop + SUMMARY_CARD_HEIGHT + SUMMARY_GAP, cardWidth,
                      SUMMARY_CARD_HEIGHT},
                 tr(STR_READING_TIME), ReadingStatsAnalytics::formatDurationHm(READING_STATS.getTotalReadingMs()));
  drawMetricCard(
      renderer, Rect{sidePadding, summaryTop + (SUMMARY_CARD_HEIGHT + SUMMARY_GAP) * 2, cardWidth, SUMMARY_CARD_HEIGHT},
      tr(STR_BOOKS_FINISHED), std::to_string(READING_STATS.getBooksFinishedCount()));
  drawMetricCard(renderer,
                 Rect{sidePadding + cardWidth + SUMMARY_GAP, summaryTop + (SUMMARY_CARD_HEIGHT + SUMMARY_GAP) * 2,
                      cardWidth, SUMMARY_CARD_HEIGHT},
                 tr(STR_BOOKS_STARTED), std::to_string(READING_STATS.getBooksStartedCount()));

  drawRecentWindowCard(renderer, Rect{sidePadding, recentTop, cardWidth, RECENT_CARD_HEIGHT}, "7D", last7DaysValue);
  drawRecentWindowCard(renderer, Rect{sidePadding + cardWidth + SUMMARY_GAP, recentTop, cardWidth, RECENT_CARD_HEIGHT},
                       "30D", last30DaysValue);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", scrollOffset > 0 ? tr(STR_DIR_UP) : "",
                                            scrollOffset < maxScrollOffset ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
