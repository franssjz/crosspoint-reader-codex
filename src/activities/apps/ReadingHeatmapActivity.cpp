#include "ReadingHeatmapActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <array>
#include <ctime>
#include <string>

#include "AppMetricCard.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "ReadingDayDetailActivity.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/ReadingStatsAnalytics.h"
#include "util/TimeUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_MONTH = 1;  // value = -1 previous / +1 next
constexpr fui::ActionId ACTION_GRID = 2;   // the tapped cell is resolved from the tap position
constexpr int SECTION_GAP = 10;
constexpr int MONTH_HEADER_HEIGHT = 34;
constexpr int SUMMARY_CARD_HEIGHT = 66;
constexpr int SUMMARY_CARD_GAP = 8;
constexpr int HEATMAP_GRID_GAP = 6;
constexpr int LEGEND_HEIGHT = 30;
constexpr int LEGEND_SWATCH_SIZE = 16;

struct HeatmapCell {
  uint32_t dayOrdinal = 0;
  uint64_t readingMs = 0;
  unsigned day = 0;
  bool inViewedMonth = false;
  bool isReferenceDay = false;
  bool isSelected = false;
};

struct MonthSummary {
  uint64_t totalReadingMs = 0;
  uint64_t bestDayReadingMs = 0;
  uint32_t daysRead = 0;
  unsigned bestDayOfMonth = 0;
};

bool isLeapYear(const int year) { return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0); }

unsigned getDaysInMonth(const int year, const unsigned month) {
  static constexpr unsigned DAYS_PER_MONTH[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2) {
    return isLeapYear(year) ? 29U : 28U;
  }
  if (month < 1 || month > 12) {
    return 30;
  }
  return DAYS_PER_MONTH[month - 1];
}

unsigned clampDayToMonth(const int year, const unsigned month, const unsigned preferredDay) {
  const unsigned daysInMonth = getDaysInMonth(year, month);
  if (preferredDay == 0) {
    return 1;
  }
  return std::min(preferredDay, daysInMonth);
}

uint32_t getReferenceDisplayTimestamp() {
  const uint32_t now = static_cast<uint32_t>(time(nullptr));
  if (TimeUtils::isClockValid(now)) {
    return now;
  }

  if (TimeUtils::isClockValid(APP_STATE.lastKnownValidTimestamp)) {
    return APP_STATE.lastKnownValidTimestamp;
  }

  return READING_STATS.getDisplayTimestamp();
}

void resolveReferenceMonth(int& year, unsigned& month, uint32_t& dayOrdinal) {
  const uint32_t referenceTimestamp = getReferenceDisplayTimestamp();
  if (TimeUtils::isClockValid(referenceTimestamp)) {
    dayOrdinal = TimeUtils::getLocalDayOrdinal(referenceTimestamp);
  } else if (READING_STATS.hasReadingDays()) {
    dayOrdinal = READING_STATS.getReadingDays().back().dayOrdinal;
  } else {
    dayOrdinal = 0;
  }

  unsigned day = 0;
  if (dayOrdinal == 0 || !TimeUtils::getDateFromDayOrdinal(dayOrdinal, year, month, day)) {
    year = 2026;
    month = 1;
    dayOrdinal = 0;
  }
}

std::string formatMonthLabel(const int year, const unsigned month) {
  return ReadingStatsAnalytics::formatMonthLabel(year, month);
}

int getHeatLevel(const uint64_t readingMs) {
  if (readingMs == 0) {
    return 0;
  }

  const uint64_t totalMinutes = readingMs / 60000ULL;
  if (totalMinutes < 15ULL) {
    return 1;
  }
  if (totalMinutes < 30ULL) {
    return 1;
  }
  if (totalMinutes < 60ULL) {
    return 2;
  }
  if (totalMinutes < 120ULL) {
    return 3;
  }
  if (totalMinutes < 240ULL) {
    return 4;
  }
  return 5;
}

void drawMetricCard(GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value) {
  AppMetricCard::Options options;
  options.paddingX = 10;
  options.contentInset = 20;
  options.valueLargeY = 13;
  options.valueSmallY = 16;
  options.labelY = 39;
  AppMetricCard::draw(renderer, rect, label, value, options);
}

void drawGoalCheckBadge(GfxRenderer& renderer, const Rect& rect, const bool darkBackground) {
  constexpr int checkWidth = 20;
  constexpr int checkHeight = 16;
  constexpr int paddingRight = 7;
  constexpr int paddingBottom = 7;

  const int checkX = rect.x + rect.width - checkWidth - paddingRight;
  const int checkY = rect.y + rect.height - checkHeight - paddingBottom;
  const bool checkColor = darkBackground ? false : true;

  renderer.drawLine(checkX, checkY + 8, checkX + 5, checkY + 13, 4, checkColor);
  renderer.drawLine(checkX + 5, checkY + 13, checkX + 17, checkY + 1, 4, checkColor);
}

void drawHeatCell(GfxRenderer& renderer, const Rect& rect, const HeatmapCell& cell) {
  const int level = cell.inViewedMonth ? getHeatLevel(cell.readingMs) : 0;
  const Rect fillRect{rect.x + 1, rect.y + 1, std::max(0, rect.width - 2), std::max(0, rect.height - 2)};
  bool textBlack = true;

  switch (level) {
    case 1:
      renderer.fillRectDither(fillRect.x, fillRect.y, fillRect.width, fillRect.height, Color::LightGray);
      break;
    case 2:
      renderer.fillRectDither(fillRect.x, fillRect.y, fillRect.width, fillRect.height, Color::MediumGray);
      break;
    case 3:
      renderer.fillRectDither(fillRect.x, fillRect.y, fillRect.width, fillRect.height, Color::DarkGray);
      break;
    case 4:
      renderer.fillRectDither(fillRect.x, fillRect.y, fillRect.width, fillRect.height, Color::ExtraDarkGray);
      textBlack = false;
      break;
    case 5:
      renderer.fillRect(fillRect.x, fillRect.y, fillRect.width, fillRect.height);
      textBlack = false;
      break;
    default:
      break;
  }

  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);

  const std::string dayText = cell.day == 0 ? "" : std::to_string(cell.day);
  if (!dayText.empty()) {
    renderer.drawText(SMALL_FONT_ID, rect.x + 6, rect.y + 5, dayText.c_str(), textBlack,
                      cell.inViewedMonth ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }

  if (cell.inViewedMonth && cell.readingMs >= getDailyReadingGoalMs()) {
    drawGoalCheckBadge(renderer, rect, level >= 4);
  }

  if (cell.isReferenceDay) {
    renderer.drawRect(rect.x + 2, rect.y + 2, rect.width - 4, rect.height - 4, level >= 4 ? false : true);
  }
  if (cell.isSelected) {
    renderer.drawRect(rect.x + 4, rect.y + 4, std::max(0, rect.width - 8), std::max(0, rect.height - 8),
                      level >= 4 ? false : true);
  }
}

void drawLegendSwatch(GfxRenderer& renderer, const Rect& rect, const int level) {
  const Rect heatRect{rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2};

  switch (level) {
    case 1:
      renderer.fillRectDither(heatRect.x, heatRect.y, heatRect.width, heatRect.height, Color::LightGray);
      break;
    case 2:
      renderer.fillRectDither(heatRect.x, heatRect.y, heatRect.width, heatRect.height, Color::MediumGray);
      break;
    case 3:
      renderer.fillRectDither(heatRect.x, heatRect.y, heatRect.width, heatRect.height, Color::DarkGray);
      break;
    case 4:
      renderer.fillRectDither(heatRect.x, heatRect.y, heatRect.width, heatRect.height, Color::ExtraDarkGray);
      break;
    case 5:
      renderer.fillRect(heatRect.x, heatRect.y, heatRect.width, heatRect.height);
      break;
    default:
      break;
  }

  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
}

// Page geometry shared by render() (drawing), buildHeatmapScreen() (hit
// rects) and handleGridTap() (cell lookup), so the three never disagree.
struct HeatmapLayout {
  int pageWidth = 0;
  int sidePadding = 0;
  int contentTop = 0;
  int summaryTop = 0;
  int cardWidth = 0;
  int gridTop = 0;
  int cellWidth = 0;
  int cellHeight = 0;
  int legendTop = 0;
};

HeatmapLayout computeLayout(const GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  HeatmapLayout layout;
  layout.pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  layout.sidePadding = metrics.contentSidePadding;
  layout.contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  layout.summaryTop = layout.contentTop + MONTH_HEADER_HEIGHT + 4;
  layout.cardWidth = (layout.pageWidth - layout.sidePadding * 2 - SUMMARY_CARD_GAP) / 2;
  layout.gridTop = layout.summaryTop + (SUMMARY_CARD_HEIGHT + SUMMARY_CARD_GAP) * 2 + SECTION_GAP;
  layout.legendTop = pageHeight - metrics.buttonHintsHeight - LEGEND_HEIGHT - 4;
  const int gridHeight = std::max(120, layout.legendTop - layout.gridTop - SECTION_GAP);
  layout.cellWidth = (layout.pageWidth - layout.sidePadding * 2 - HEATMAP_GRID_GAP * 6) / 7;
  layout.cellHeight = (gridHeight - HEATMAP_GRID_GAP * 5) / 6;
  return layout;
}

// Day ordinal of the grid's top-left cell (the Monday on or before the 1st).
uint32_t gridStartOrdinal(const int year, const unsigned month) {
  const uint32_t firstDayOrdinal = TimeUtils::getDayOrdinalForDate(year, month, 1);
  const int firstWeekday = static_cast<int>((firstDayOrdinal + 3U) % 7U);  // Monday = 0
  return firstDayOrdinal - static_cast<uint32_t>(firstWeekday);
}

MonthSummary buildMonthSummary(const int year, const unsigned month) {
  MonthSummary summary;
  const uint32_t monthStart = TimeUtils::getDayOrdinalForDate(year, month, 1);
  const uint32_t monthEnd = TimeUtils::getDayOrdinalForDate(year, month, getDaysInMonth(year, month));
  for (const auto& day : READING_STATS.getReadingDays()) {
    if (day.dayOrdinal < monthStart || day.dayOrdinal > monthEnd) {
      continue;
    }

    summary.totalReadingMs += day.readingMs;
    if (day.readingMs > 0) {
      summary.daysRead++;
    }
    if (day.readingMs > summary.bestDayReadingMs) {
      int dayYear = 0;
      unsigned dayMonth = 0;
      unsigned dayOfMonth = 0;
      TimeUtils::getDateFromDayOrdinal(day.dayOrdinal, dayYear, dayMonth, dayOfMonth);
      summary.bestDayReadingMs = day.readingMs;
      summary.bestDayOfMonth = dayOfMonth;
    }
  }
  return summary;
}

std::array<HeatmapCell, 42> buildHeatmapCells(const int year, const unsigned month, const uint32_t referenceDayOrdinal,
                                              const uint32_t selectedDayOrdinal) {
  std::array<HeatmapCell, 42> cells{};
  const uint32_t startOrdinal = gridStartOrdinal(year, month);

  for (size_t index = 0; index < cells.size(); ++index) {
    auto& cell = cells[index];
    cell.dayOrdinal = startOrdinal + static_cast<uint32_t>(index);
    int cellYear = 0;
    unsigned cellMonth = 0;
    unsigned cellDay = 0;
    TimeUtils::getDateFromDayOrdinal(cell.dayOrdinal, cellYear, cellMonth, cellDay);
    cell.day = cellDay;
    cell.inViewedMonth = cellYear == year && cellMonth == month;
    cell.isReferenceDay = cell.inViewedMonth && referenceDayOrdinal != 0 && cell.dayOrdinal == referenceDayOrdinal;
    cell.isSelected = cell.inViewedMonth && selectedDayOrdinal != 0 && cell.dayOrdinal == selectedDayOrdinal;
  }

  size_t readingIndex = 0;
  const auto& readingDays = READING_STATS.getReadingDays();
  for (auto& cell : cells) {
    while (readingIndex < readingDays.size() && readingDays[readingIndex].dayOrdinal < cell.dayOrdinal) {
      readingIndex++;
    }
    if (readingIndex < readingDays.size() && readingDays[readingIndex].dayOrdinal == cell.dayOrdinal) {
      cell.readingMs = readingDays[readingIndex].readingMs;
    }
  }

  return cells;
}

void drawLegend(GfxRenderer& renderer, const Rect& rect) {
  struct LegendLevel {
    int level;
    const char* label;
  };
  static constexpr LegendLevel LEVELS[] = {{1, ">0"}, {2, "30m+"}, {3, "60m+"}, {4, "120m+"}, {5, "240m+"}};
  constexpr int LEVEL_COUNT = sizeof(LEVELS) / sizeof(LEVELS[0]);

  const int itemWidth = rect.width / LEVEL_COUNT;
  for (int index = 0; index < LEVEL_COUNT; ++index) {
    const int itemX = rect.x + index * itemWidth;
    const Rect swatch{itemX + 6, rect.y + 3, LEGEND_SWATCH_SIZE, LEGEND_SWATCH_SIZE};
    drawLegendSwatch(renderer, swatch, LEVELS[index].level);
    renderer.drawText(SMALL_FONT_ID, itemX + 28, rect.y + 6, LEVELS[index].label);
  }
}
}  // namespace

void ReadingHeatmapActivity::onEnter() {
  Activity::onEnter();

  uint32_t referenceDayOrdinal = 0;
  resolveReferenceMonth(viewedYear, viewedMonth, referenceDayOrdinal);
  if (viewedMonth == 0) {
    viewedYear = 2026;
    viewedMonth = 1;
  }

  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  resetSelectedDay();

  resetUi();
  app.on(ACTION_MONTH, &ReadingHeatmapActivity::onMonthEvent, this);
  app.setScreen(&ReadingHeatmapActivity::heatmapScreen, this);
  requestUpdate();
}

void ReadingHeatmapActivity::heatmapScreen(UiScreen& screen, void* user) {
  static_cast<ReadingHeatmapActivity*>(user)->buildHeatmapScreen(screen);
}

void ReadingHeatmapActivity::onMonthEvent(const fui::ActionEvent& event, void* user) {
  static_cast<ReadingHeatmapActivity*>(user)->goToAdjacentMonth(event.value);
}

// Touch hit rects over the hand-drawn page (the builder draws nothing).
void ReadingHeatmapActivity::buildHeatmapScreen(UiScreen& screen) {
  const HeatmapLayout layout = computeLayout(renderer);
  const int halfWidth = layout.pageWidth / 2;
  screen.frame().hit(fui::makeRect(0, layout.contentTop, halfWidth, MONTH_HEADER_HEIGHT), ACTION_MONTH, -1,
                     fui::InputTouch);
  screen.frame().hit(fui::makeRect(halfWidth, layout.contentTop, layout.pageWidth - halfWidth, MONTH_HEADER_HEIGHT),
                     ACTION_MONTH, +1, fui::InputTouch);
  // One rect for the whole 7x6 grid (42 per-cell rects would overflow the
  // shared interaction table); loop() resolves the cell from the tap point.
  const int gridWidth = layout.cellWidth * 7 + HEATMAP_GRID_GAP * 6;
  const int gridHeight = layout.cellHeight * 6 + HEATMAP_GRID_GAP * 5;
  screen.frame().hit(fui::makeRect(layout.sidePadding, layout.gridTop, gridWidth, gridHeight), ACTION_GRID, 0,
                     fui::InputTouch);
}

void ReadingHeatmapActivity::handleGridTap(const int x, const int y) {
  const HeatmapLayout layout = computeLayout(renderer);
  const int col = (x - layout.sidePadding) / (layout.cellWidth + HEATMAP_GRID_GAP);
  const int row = (y - layout.gridTop) / (layout.cellHeight + HEATMAP_GRID_GAP);
  if (x < layout.sidePadding || y < layout.gridTop || col < 0 || col > 6 || row < 0 || row > 5) {
    return;
  }
  // Ignore taps in the gap band between cells.
  if ((x - layout.sidePadding) % (layout.cellWidth + HEATMAP_GRID_GAP) >= layout.cellWidth ||
      (y - layout.gridTop) % (layout.cellHeight + HEATMAP_GRID_GAP) >= layout.cellHeight) {
    return;
  }

  const uint32_t dayOrdinal = gridStartOrdinal(viewedYear, viewedMonth) + static_cast<uint32_t>(row * 7 + col);
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  if (!TimeUtils::getDateFromDayOrdinal(dayOrdinal, year, month, day)) {
    return;
  }
  if (year != viewedYear || month != viewedMonth) {
    // A leading/trailing day of the neighbouring month: page to that month
    // with the tapped day selected (what the Left/Right buttons do across a
    // month boundary).
    viewedYear = year;
    viewedMonth = month;
    selectedDayOrdinal = dayOrdinal;
    requestUpdate();
    return;
  }
  selectedDayOrdinal = dayOrdinal;
  openSelectedDay();
}

void ReadingHeatmapActivity::openSelectedDay() {
  app.clearTapFlash();  // leaving for the day detail screen
  startActivityForResult(std::make_unique<ReadingDayDetailActivity>(renderer, mappedInput, selectedDayOrdinal),
                         [this](const ActivityResult&) { requestUpdate(); });
}

void ReadingHeatmapActivity::onExit() {
  renderer.requestNextRefresh(HalDisplay::HALF_REFRESH);
  Activity::onExit();
}

void ReadingHeatmapActivity::goToAdjacentMonth(const int delta) {
  int currentYear = 0;
  unsigned currentMonth = 0;
  unsigned currentDay = 1;
  if (selectedDayOrdinal != 0) {
    TimeUtils::getDateFromDayOrdinal(selectedDayOrdinal, currentYear, currentMonth, currentDay);
  }

  int month = static_cast<int>(viewedMonth) + delta;
  int year = viewedYear;
  while (month < 1) {
    month += 12;
    year--;
  }
  while (month > 12) {
    month -= 12;
    year++;
  }
  viewedYear = year;
  viewedMonth = static_cast<unsigned>(month);
  const unsigned targetDay = clampDayToMonth(viewedYear, viewedMonth, currentDay);
  selectedDayOrdinal = TimeUtils::getDayOrdinalForDate(viewedYear, viewedMonth, targetDay);
  requestUpdate();
}

void ReadingHeatmapActivity::goToReferenceMonth() {
  uint32_t referenceDayOrdinal = 0;
  int year = 0;
  unsigned month = 0;
  resolveReferenceMonth(year, month, referenceDayOrdinal);
  if (year == viewedYear && month == viewedMonth) {
    return;
  }
  viewedYear = year;
  viewedMonth = month;
  resetSelectedDay();
  requestUpdate();
}

void ReadingHeatmapActivity::resetSelectedDay() {
  uint32_t referenceDayOrdinal = 0;
  resolveReferenceMonth(viewedYear, viewedMonth, referenceDayOrdinal);

  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  if (referenceDayOrdinal != 0 && TimeUtils::getDateFromDayOrdinal(referenceDayOrdinal, year, month, day) &&
      year == viewedYear && month == viewedMonth) {
    selectedDayOrdinal = referenceDayOrdinal;
    return;
  }

  for (const auto& readingDay : READING_STATS.getReadingDays()) {
    if (readingDay.readingMs == 0) {
      continue;
    }
    if (!TimeUtils::getDateFromDayOrdinal(readingDay.dayOrdinal, year, month, day)) {
      continue;
    }
    if (year == viewedYear && month == viewedMonth) {
      selectedDayOrdinal = readingDay.dayOrdinal;
      return;
    }
  }

  selectedDayOrdinal = TimeUtils::getDayOrdinalForDate(viewedYear, viewedMonth, 1);
}

void ReadingHeatmapActivity::moveSelection(const int delta) {
  if (selectedDayOrdinal == 0) {
    resetSelectedDay();
    requestUpdate();
    return;
  }

  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  if (!TimeUtils::getDateFromDayOrdinal(selectedDayOrdinal, year, month, day) || year != viewedYear ||
      month != viewedMonth) {
    resetSelectedDay();
    requestUpdate();
    return;
  }

  const uint32_t firstDayOrdinal = TimeUtils::getDayOrdinalForDate(viewedYear, viewedMonth, 1);
  const uint32_t lastDayOrdinal =
      TimeUtils::getDayOrdinalForDate(viewedYear, viewedMonth, getDaysInMonth(viewedYear, viewedMonth));
  int32_t nextOrdinal = static_cast<int32_t>(selectedDayOrdinal) + delta;
  if (nextOrdinal < 1) {
    nextOrdinal = 1;
  }

  if (nextOrdinal < static_cast<int32_t>(firstDayOrdinal) || nextOrdinal > static_cast<int32_t>(lastDayOrdinal)) {
    int nextYear = 0;
    unsigned nextMonth = 0;
    unsigned nextDay = 0;
    if (TimeUtils::getDateFromDayOrdinal(static_cast<uint32_t>(nextOrdinal), nextYear, nextMonth, nextDay)) {
      viewedYear = nextYear;
      viewedMonth = nextMonth;
    }
  }

  selectedDayOrdinal = static_cast<uint32_t>(nextOrdinal);
  requestUpdate();
}

void ReadingHeatmapActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (waitForConfirmRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      waitForConfirmRelease = false;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelectedDay();
    return;
  }

  // Touch goes through the FreeInkApp: render() registered the month-header
  // and grid hit rects. The month handler dispatches itself; the grid tap
  // needs the contact point, which the route snapshot carries.
  const auto route = routeTouch(mappedInput);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) {
    if (route.event.action == ACTION_GRID) {
      handleGridTap(route.snap.touchX, route.snap.touchY);
    }
    return;
  }

  // Swipe left/right pages months like the Up/Down buttons.
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left) {
    goToAdjacentMonth(1);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Right) {
    goToAdjacentMonth(-1);
    return;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { moveSelection(-1); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { moveSelection(1); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this] { goToAdjacentMonth(-1); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this] { goToAdjacentMonth(1); });
}

void ReadingHeatmapActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const HeatmapLayout layout = computeLayout(renderer);
  const int pageWidth = layout.pageWidth;
  const int sidePadding = layout.sidePadding;
  const int contentTop = layout.contentTop;

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_READING_HEATMAP));

  const uint32_t referenceTimestamp = getReferenceDisplayTimestamp();
  const uint32_t referenceDayOrdinal =
      TimeUtils::isClockValid(referenceTimestamp)
          ? TimeUtils::getLocalDayOrdinal(referenceTimestamp)
          : (READING_STATS.hasReadingDays() ? READING_STATS.getReadingDays().back().dayOrdinal : 0);
  const auto monthSummary = buildMonthSummary(viewedYear, viewedMonth);
  const auto cells = buildHeatmapCells(viewedYear, viewedMonth, referenceDayOrdinal, selectedDayOrdinal);
  // Touch boards get "< Month >" so the header reads as the month pager it is
  // (tap the left/right half); button boards keep the plain label.
  const std::string monthLabel = mappedInput.hasTouch() ? "< " + formatMonthLabel(viewedYear, viewedMonth) + " >"
                                                        : formatMonthLabel(viewedYear, viewedMonth);
  const std::string selectedDateLabel = ReadingStatsAnalytics::formatDayOrdinalLabel(selectedDayOrdinal);

  GUI.drawSubHeader(renderer, Rect{0, contentTop, pageWidth, MONTH_HEADER_HEIGHT}, monthLabel.c_str(),
                    selectedDateLabel.empty() ? nullptr : selectedDateLabel.c_str());

  const int summaryTop = layout.summaryTop;
  const int cardWidth = layout.cardWidth;
  const std::string bestDayValue = monthSummary.bestDayOfMonth > 0
                                       ? ReadingStatsAnalytics::formatDurationHm(monthSummary.bestDayReadingMs) + " (" +
                                             std::to_string(monthSummary.bestDayOfMonth) + ")"
                                       : ReadingStatsAnalytics::formatDurationHm(monthSummary.bestDayReadingMs);
  drawMetricCard(renderer, Rect{sidePadding, summaryTop, cardWidth, SUMMARY_CARD_HEIGHT}, tr(STR_MONTH_TOTAL),
                 ReadingStatsAnalytics::formatDurationHm(monthSummary.totalReadingMs));
  drawMetricCard(renderer, Rect{sidePadding + cardWidth + SUMMARY_CARD_GAP, summaryTop, cardWidth, SUMMARY_CARD_HEIGHT},
                 tr(STR_DAYS_READ), std::to_string(monthSummary.daysRead));
  drawMetricCard(renderer,
                 Rect{sidePadding, summaryTop + SUMMARY_CARD_HEIGHT + SUMMARY_CARD_GAP, cardWidth, SUMMARY_CARD_HEIGHT},
                 tr(STR_BEST_DAY), bestDayValue);
  drawMetricCard(renderer,
                 Rect{sidePadding + cardWidth + SUMMARY_CARD_GAP, summaryTop + SUMMARY_CARD_HEIGHT + SUMMARY_CARD_GAP,
                      cardWidth, SUMMARY_CARD_HEIGHT},
                 tr(STR_STREAK), std::to_string(READING_STATS.getCurrentStreakDays()));

  for (int index = 0; index < 42; ++index) {
    const int row = index / 7;
    const int col = index % 7;
    const int x = sidePadding + col * (layout.cellWidth + HEATMAP_GRID_GAP);
    const int y = layout.gridTop + row * (layout.cellHeight + HEATMAP_GRID_GAP);
    drawHeatCell(renderer, Rect{x, y, layout.cellWidth, layout.cellHeight}, cells[static_cast<size_t>(index)]);
  }

  drawLegend(renderer, Rect{sidePadding, layout.legendTop, pageWidth - sidePadding * 2, LEGEND_HEIGHT});

  // Rebuild the app's touch hit rects over the page (the screen builder draws
  // nothing, so the visuals above are untouched).
  renderUi();

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
