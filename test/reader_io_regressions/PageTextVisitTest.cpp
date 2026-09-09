#include <Epub/Page.h>
#include <Epub/ParsedText.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <tuple>

namespace {
std::shared_ptr<TextBlock> textLine(const char* word) {
  return std::make_shared<TextBlock>(std::vector<std::string>{word}, std::vector<int16_t>{0},
                                     std::vector<EpdFontFamily::Style>{EpdFontFamily::REGULAR}, std::vector<uint8_t>{0},
                                     std::vector<uint16_t>{0}, std::vector<uint8_t>{0});
}

TEST(PageTextVisit, TableCellsKeepTheirOwnFlowAndRenderedCoordinates) {
  Page page;
  page.elements.push_back(std::make_shared<PageLine>(textLine("before"), 5, 6));
  TableFragmentRow first;
  first.height = 45;
  first.cells = {{false, {textLine("left1"), textLine("left2")}}, {false, {textLine("right")}}};
  TableFragmentRow second;
  second.height = 25;
  second.cells = {{false, {textLine("bottom")}}};
  page.elements.push_back(
      std::make_shared<PageTableFragment>(200, 2, 3, 18, std::vector<TableFragmentRow>{first, second}, 10, 20));
  page.elements.push_back(std::make_shared<PageHorizontalRule>(200, 1, 0, 100));
  page.elements.push_back(std::make_shared<PageLine>(textLine("after"), 5, 120));
  using Visit = std::tuple<std::string, int, int, uint16_t>;
  std::vector<Visit> visits;
  page.forEachTextLine([&](const TextBlock& block, int x, int y, uint16_t flow) {
    visits.emplace_back(block.wordText(0), x, y, flow);
    return true;
  });
  const std::vector<Visit> expected = {{"before", 5, 6, 0},   {"left1", 13, 23, 1},  {"left2", 13, 41, 1},
                                       {"right", 113, 23, 2}, {"bottom", 13, 68, 3}, {"after", 5, 120, 4}};
  EXPECT_EQ(visits, expected);
}

TEST(PageTextVisit, BoundedCallerCanStopInsideTableWithoutVisitingRemainingElements) {
  Page page;
  TableFragmentRow row;
  row.height = 70;
  row.cells = {{false, {textLine("first"), textLine("second"), textLine("third")}}};
  page.elements.push_back(std::make_shared<PageTableFragment>(180, 1, 2, 20, std::vector<TableFragmentRow>{row}, 0, 0));
  page.elements.push_back(std::make_shared<PageLine>(textLine("last"), 0, 90));
  int seen = 0;
  page.forEachTextLine([&](const TextBlock&, int, int, uint16_t) { return ++seen < 2; });
  EXPECT_EQ(seen, 2);
}

TEST(PageTextVisit, InvalidColumnCountsAndNullLinesDoNotBecomeSelectableText) {
  Page page;
  TableFragmentRow row;
  row.cells = {{false, {textLine("hidden")}}};
  page.elements.push_back(std::make_shared<PageTableFragment>(180, 0, 2, 20, std::vector<TableFragmentRow>{row}, 0, 0));
  page.elements.push_back(nullptr);
  page.elements.push_back(std::make_shared<PageLine>(nullptr, 0, 0));
  int seen = 0;
  page.forEachTextLine([&](const TextBlock&, int, int, uint16_t) {
    ++seen;
    return true;
  });
  EXPECT_EQ(seen, 0);
}

TEST(WordSpacing, LevelZeroKeepsLegacyGapAndLevelsAddFontIndependentPixels) {
  GfxRenderer renderer;
  const auto layout = [&](uint8_t level) {
    ParsedText text(false, false, false, false, level);
    text.addWord("alpha", EpdFontFamily::REGULAR);
    text.addWord("beta", EpdFontFamily::REGULAR);
    std::shared_ptr<TextBlock> line;
    text.layoutAndExtractLines(renderer, 0, 300,
                               [&](std::shared_ptr<TextBlock> value, uint32_t) { line = std::move(value); });
    return line;
  };
  const auto legacy = layout(0);
  const auto wider = layout(2);
  ASSERT_TRUE(legacy);
  ASSERT_TRUE(wider);
  ASSERT_EQ(legacy->wordCount(), 2);
  ASSERT_EQ(wider->wordCount(), 2);
  EXPECT_EQ(legacy->wordXpos(1) - legacy->wordXpos(0), 24);
  EXPECT_EQ(wider->wordXpos(1), legacy->wordXpos(1) + 20);
}
}  // namespace
