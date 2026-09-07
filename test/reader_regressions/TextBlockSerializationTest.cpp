#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include <HalStorage.h>

#include "lib/Epub/Epub/blocks/TextBlock.h"

namespace {

class TemporaryFile {
 public:
  TemporaryFile()
      : path(std::filesystem::temp_directory_path() /
             ("cpr-vcodex-text-block-" +
              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin")) {}
  ~TemporaryFile() { std::filesystem::remove(path); }

  std::filesystem::path path;
};

TEST(TextBlockSerialization, PreservesInsertedHyphenRubyFocusAndStyle) {
  const std::vector<std::string> words{"inter-", "national", "漢字"};
  const std::vector<int16_t> positions{8, 76, 180};
  const std::vector<EpdFontFamily::Style> styles{EpdFontFamily::REGULAR, EpdFontFamily::BOLD,
                                                 EpdFontFamily::RUBY_CONTINUE};
  const std::vector<uint8_t> focusBoundaries{3, 4, 0};
  const std::vector<uint16_t> focusSuffixes{28, 37, 0};
  const std::vector<uint8_t> layoutFlags{TextBlock::WORD_FLAG_INSERTED_HYPHEN, 0, 0};
  const std::vector<std::string> ruby{"", "かんじ", ""};
  BlockStyle style;
  style.alignment = CssTextAlign::Center;
  style.textAlignDefined = true;
  style.marginLeft = 11;
  style.paddingRight = 7;
  style.textIndent = 24;
  style.textIndentDefined = true;

  TextBlock original(words, positions, styles, focusBoundaries, focusSuffixes, layoutFlags, style, ruby);
  ASSERT_TRUE(original.valid());

  TemporaryFile temporary;
  FsFile output;
  ASSERT_TRUE(Storage.openFileForWrite("test", temporary.path.string(), output));
  ASSERT_TRUE(original.serialize(output));
  ASSERT_TRUE(output.close());

  FsFile input;
  ASSERT_TRUE(Storage.openFileForRead("test", temporary.path.string(), input));
  auto restored = TextBlock::deserialize(input);
  ASSERT_TRUE(input.close());
  ASSERT_NE(restored, nullptr);
  ASSERT_TRUE(restored->valid());
  ASSERT_EQ(restored->wordCount(), words.size());

  for (uint16_t i = 0; i < restored->wordCount(); ++i) {
    EXPECT_EQ(restored->wordText(i), words[i]);
    EXPECT_EQ(restored->wordXpos(i), positions[i]);
    EXPECT_EQ(restored->wordStyle(i), styles[i]);
    EXPECT_EQ(restored->focusBoundary(i), focusBoundaries[i]);
    EXPECT_EQ(restored->focusSuffixX(i), focusSuffixes[i]);
  }
  EXPECT_TRUE(restored->wordEndsWithInsertedHyphen(0));
  EXPECT_FALSE(restored->wordEndsWithInsertedHyphen(1));
  EXPECT_STREQ(restored->rubyText(1), "かんじ");
  EXPECT_EQ(restored->getBlockStyle().alignment, CssTextAlign::Center);
  EXPECT_TRUE(restored->getBlockStyle().textAlignDefined);
  EXPECT_EQ(restored->getBlockStyle().marginLeft, 11);
  EXPECT_EQ(restored->getBlockStyle().paddingRight, 7);
  EXPECT_EQ(restored->getBlockStyle().textIndent, 24);
  EXPECT_TRUE(restored->getBlockStyle().textIndentDefined);
}

TEST(TextBlockSerialization, KeepsLayoutFlagsAbsentForOrdinaryLines) {
  TextBlock original({"plain", "line"}, {0, 48}, {EpdFontFamily::REGULAR, EpdFontFamily::ITALIC}, {}, {}, {},
                     BlockStyle{});
  ASSERT_TRUE(original.valid());

  TemporaryFile temporary;
  FsFile output;
  ASSERT_TRUE(Storage.openFileForWrite("test", temporary.path.string(), output));
  ASSERT_TRUE(original.serialize(output));
  ASSERT_TRUE(output.close());

  FsFile input;
  ASSERT_TRUE(Storage.openFileForRead("test", temporary.path.string(), input));
  auto restored = TextBlock::deserialize(input);
  ASSERT_TRUE(input.close());
  ASSERT_NE(restored, nullptr);
  EXPECT_FALSE(restored->wordEndsWithInsertedHyphen(0));
  EXPECT_FALSE(restored->wordEndsWithInsertedHyphen(1));
  EXPECT_FALSE(restored->hasRuby());
}

}  // namespace
