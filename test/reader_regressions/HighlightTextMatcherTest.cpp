#include <gtest/gtest.h>

#include <string>

#include "activities/reader/HighlightTextMatcher.h"

using HighlightTextMatcher::TokenFragmentMatch;

TEST(HighlightTextMatcherTest, JoinsLayoutInsertedHyphenAcrossFragments) {
  constexpr char token[] = "correctly";
  const auto prefix = HighlightTextMatcher::matchTokenFragment("cor-", true, token, sizeof(token) - 1, 0);
  ASSERT_EQ(prefix.match, TokenFragmentMatch::CONTINUES_TOKEN);
  EXPECT_EQ(prefix.tokenBytes, 3U);

  const auto suffix =
      HighlightTextMatcher::matchTokenFragment("rectly", false, token, sizeof(token) - 1, prefix.tokenBytes);
  EXPECT_EQ(suffix.match, TokenFragmentMatch::COMPLETES_TOKEN);
}

TEST(HighlightTextMatcherTest, PreservesAuthoredHyphens) {
  constexpr char token[] = "wellknown";
  EXPECT_EQ(HighlightTextMatcher::matchTokenFragment("well-", false, token, sizeof(token) - 1, 0).match,
            TokenFragmentMatch::MISMATCH);
}

TEST(HighlightTextMatcherTest, JoinsAdjacentEllipsisFragment) {
  constexpr char token[] = "it\xE2\x80\xA6";
  const auto prefix = HighlightTextMatcher::matchTokenFragment("it", false, token, sizeof(token) - 1, 0);
  ASSERT_EQ(prefix.match, TokenFragmentMatch::CONTINUES_TOKEN);
  EXPECT_EQ(HighlightTextMatcher::matchTokenFragment("\xE2\x80\xA6", false, token, sizeof(token) - 1,
                                                     prefix.tokenBytes)
                .match,
            TokenFragmentMatch::COMPLETES_TOKEN);
}

TEST(HighlightTextMatcherTest, NormalizesNbspBeforeEllipsis) {
  constexpr char token[] = "it \xE2\x80\xA6";
  const auto prefix = HighlightTextMatcher::matchTokenFragment("it", false, token, sizeof(token) - 1, 0);
  ASSERT_EQ(prefix.match, TokenFragmentMatch::CONTINUES_TOKEN);

  const auto ellipsis = HighlightTextMatcher::matchTokenFragment(
      "\xC2\xA0\xE2\x80\xA6", false, token, sizeof(token) - 1, prefix.tokenBytes);
  EXPECT_EQ(ellipsis.match, TokenFragmentMatch::COMPLETES_TOKEN);
  EXPECT_EQ(ellipsis.tokenBytes, 4U);
}

TEST(HighlightTextMatcherTest, TokenizerKeepsSpaceBeforeEllipsisWithItsWord) {
  const char* cursor = "it \xE2\x80\xA6 done";
  const char* token = nullptr;
  size_t tokenLength = 0;
  ASSERT_TRUE(HighlightTextMatcher::nextToken(cursor, token, tokenLength));
  EXPECT_EQ(std::string(token, tokenLength), "it \xE2\x80\xA6");
  ASSERT_TRUE(HighlightTextMatcher::nextToken(cursor, token, tokenLength));
  EXPECT_EQ(std::string(token, tokenLength), "done");
}

TEST(HighlightTextMatcherTest, RejectsInsertedHyphenWithoutRemainder) {
  constexpr char token[] = "cor";
  EXPECT_EQ(HighlightTextMatcher::matchTokenFragment("cor-", true, token, sizeof(token) - 1, 0).match,
            TokenFragmentMatch::MISMATCH);
}
