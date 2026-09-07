#include <gtest/gtest.h>

#include "DictionaryFontSelection.h"

TEST(DictionaryFontSelectionTest, UsesActiveReaderFontForSdFontDefinitions) {
  constexpr int THAI_READER_FONT_ID = -1234567;

  EXPECT_EQ(DictionaryFontSelection::definitionFontId(THAI_READER_FONT_ID, true, 0), THAI_READER_FONT_ID);
  EXPECT_EQ(DictionaryFontSelection::definitionFontId(THAI_READER_FONT_ID, true, 1), THAI_READER_FONT_ID);
}

TEST(DictionaryFontSelectionTest, KeepsConfiguredUiSizeForBuiltInFonts) {
  EXPECT_EQ(DictionaryFontSelection::definitionFontId(BOOKERLY_14_FONT_ID, false, 0), UI_10_FONT_ID);
  EXPECT_EQ(DictionaryFontSelection::definitionFontId(BOOKERLY_14_FONT_ID, false, 1), UI_12_FONT_ID);
  EXPECT_EQ(DictionaryFontSelection::definitionFontId(BOOKERLY_14_FONT_ID, false, 99), UI_10_FONT_ID);
}
