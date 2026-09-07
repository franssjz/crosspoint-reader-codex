#include <gtest/gtest.h>

#include "lib/Epub/Epub/RubyCjkLayoutUtils.h"

TEST(RubyLayoutRegression, ReservesBothMarginOverhangs) {
  EXPECT_EQ(RubyCjkLayoutUtils::edgeReservation(30, 20), 5);
  EXPECT_EQ(RubyCjkLayoutUtils::edgeReservation(20, 30), 0);
}

TEST(RubyLayoutRegression, OddOverhangRoundsDownAndNeverTurnsNegative) {
  EXPECT_EQ(RubyCjkLayoutUtils::edgeReservation(31, 20), 5);
  EXPECT_EQ(RubyCjkLayoutUtils::edgeReservation(0, 20), 0);
}

TEST(RubyLayoutRegression, NonCjkNeighbourCanAbsorbHalfItsWidth) {
  EXPECT_EQ(RubyCjkLayoutUtils::reservedAdjacentOverlap(8, 10, false), 3);
  EXPECT_EQ(RubyCjkLayoutUtils::reservedAdjacentOverlap(8, 10, true), 8);
}

TEST(RubyLayoutRegression, AdjacentOverlapIsClampedForNarrowLatinGlyphs) {
  EXPECT_EQ(RubyCjkLayoutUtils::reservedAdjacentOverlap(2, 10, false), 0);
  EXPECT_EQ(RubyCjkLayoutUtils::reservedAdjacentOverlap(6, 1, false), 6);
  EXPECT_EQ(RubyCjkLayoutUtils::reservedAdjacentOverlap(0, 8, true), 0);
}

TEST(CjkLayoutRegression, EqualCostPrefersTheLongerCandidate) {
  EXPECT_TRUE(RubyCjkLayoutUtils::preferLineBreakCandidate(100, 100));
  EXPECT_FALSE(RubyCjkLayoutUtils::preferLineBreakCandidate(101, 100));
}

TEST(CjkLayoutRegression, LowerCostAlwaysWins) {
  EXPECT_TRUE(RubyCjkLayoutUtils::preferLineBreakCandidate(99, 100));
  EXPECT_FALSE(RubyCjkLayoutUtils::preferLineBreakCandidate(102, 100));
}
