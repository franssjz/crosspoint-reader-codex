#include <gtest/gtest.h>

#include "lib/Epub/Epub/TokenBoundary.h"

TEST(TokenBoundaryRegression, DistinguishesNormalAndBreakableAttachments) {
  EXPECT_TRUE(TokenBoundary::allowsBreak(false, false));
  EXPECT_FALSE(TokenBoundary::allowsBreak(true, false));
  EXPECT_TRUE(TokenBoundary::allowsBreak(true, true));

  EXPECT_TRUE(TokenBoundary::isJustifiableGap(false, false, false));
  EXPECT_FALSE(TokenBoundary::isJustifiableGap(true, true, false));
  EXPECT_TRUE(TokenBoundary::isJustifiableGap(true, false, true));
}

TEST(TokenBoundaryRegression, VisibleHyphensBreakButConditionalOnesKeepTheirMeaning) {
  EXPECT_TRUE(TokenBoundary::allowsBreakAfterExplicitHyphen('-'));
  EXPECT_TRUE(TokenBoundary::allowsBreakAfterExplicitHyphen(0x2014));  // em dash
  EXPECT_TRUE(TokenBoundary::allowsBreakAfterExplicitHyphen(0x2010));  // hyphen
  EXPECT_TRUE(TokenBoundary::allowsBreakAfterExplicitHyphen(0x2013));  // en dash
  EXPECT_TRUE(TokenBoundary::allowsBreakAfterExplicitHyphen(0xFF0D));  // full-width hyphen
  EXPECT_FALSE(TokenBoundary::allowsBreakAfterExplicitHyphen(0x00AD));  // soft hyphen
  EXPECT_FALSE(TokenBoundary::allowsBreakAfterExplicitHyphen(0x2011));  // non-breaking hyphen
  EXPECT_FALSE(TokenBoundary::allowsBreakAfterExplicitHyphen('.'));
}

TEST(TokenBoundaryRegression, FocusBoundaryFollowsBothHalvesOfAHyphenatedWord) {
  EXPECT_EQ(TokenBoundary::focusBoundaryBefore(6, 4), 4);
  EXPECT_EQ(TokenBoundary::focusBoundaryAfter(6, 4), 2);
  EXPECT_EQ(TokenBoundary::focusBoundaryBefore(4, 7), 4);
  EXPECT_EQ(TokenBoundary::focusBoundaryAfter(4, 7), 0);
}
