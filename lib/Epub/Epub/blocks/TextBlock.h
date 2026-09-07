#pragma once

#include <EpdFontFamily.h>
#include <HalStorage.h>

#include <memory>
#include <string>
#include <vector>

#include "Block.h"
#include "BlockStyle.h"

class FontCacheManager;

// A rendered text line. All per-word arrays, base text and ruby text share one
// allocation to avoid the hundreds of tiny vector/string allocations formerly
// created while loading a page.
class TextBlock final : public Block {
 private:
  BlockStyle blockStyle;
  uint16_t numWords = 0;
  uint16_t textBytes = 0;
  uint16_t rubyTextBytes = 0;
  bool focusPresent = false;
  bool rubyPresent = false;
  bool layoutFlagsPresent = false;
  bool isValid = true;
  std::unique_ptr<uint8_t[]> arena;
  const uint16_t* textOffArr = nullptr;
  const uint16_t* rubyOffArr = nullptr;
  const int16_t* xposArr = nullptr;
  const uint16_t* focusSuffixXArr = nullptr;
  const uint8_t* stylesArr = nullptr;
  const uint8_t* focusBoundaryArr = nullptr;
  const uint8_t* layoutFlagsArr = nullptr;
  const char* textArr = nullptr;
  const char* rubyTextArr = nullptr;

  TextBlock() = default;
  static size_t arenaSize(uint16_t wordCount, bool hasFocus, bool hasRuby, bool hasLayoutFlags, uint16_t textBytes,
                          uint16_t rubyTextBytes);
  void bindArenaPointers();

 public:
  static constexpr uint8_t WORD_FLAG_INSERTED_HYPHEN = 0x01;

  explicit TextBlock(const std::vector<std::string>& words, const std::vector<int16_t>& wordXpos,
                     const std::vector<EpdFontFamily::Style>& wordStyles, const std::vector<uint8_t>& focusBoundary,
                     const std::vector<uint16_t>& focusSuffixX, const std::vector<uint8_t>& layoutFlags,
                     const BlockStyle& blockStyle = BlockStyle(),
                     const std::vector<std::string>& rubyTexts = {});
  ~TextBlock() override = default;
  TextBlock(const TextBlock&) = delete;
  TextBlock& operator=(const TextBlock&) = delete;

  void setBlockStyle(const BlockStyle& style) { blockStyle = style; }
  const BlockStyle& getBlockStyle() const { return blockStyle; }
  bool isEmpty() override { return numWords == 0; }
  bool valid() const { return isValid; }
  uint16_t wordCount() const { return numWords; }
  const char* wordText(uint16_t i) const { return textArr + textOffArr[i]; }
  uint16_t wordTextLen(uint16_t i) const {
    const uint16_t end = i + 1 < numWords ? textOffArr[i + 1] : textBytes;
    return end - textOffArr[i] - 1;
  }
  int16_t wordXpos(uint16_t i) const { return xposArr[i]; }
  EpdFontFamily::Style wordStyle(uint16_t i) const { return static_cast<EpdFontFamily::Style>(stylesArr[i]); }
  uint8_t focusBoundary(uint16_t i) const { return focusPresent ? focusBoundaryArr[i] : 0; }
  uint16_t focusSuffixX(uint16_t i) const { return focusPresent ? focusSuffixXArr[i] : 0; }
  bool wordEndsWithInsertedHyphen(uint16_t i) const {
    return layoutFlagsPresent && (layoutFlagsArr[i] & WORD_FLAG_INSERTED_HYPHEN) != 0;
  }
  const char* rubyText(uint16_t i) const {
    return rubyPresent && rubyOffArr[i] != UINT16_MAX ? rubyTextArr + rubyOffArr[i] : "";
  }
  bool hasRuby() const { return rubyPresent; }
  int getRubyShift(int ascender) const { return rubyPresent ? ascender / 2 : 0; }

  void recordFontUsage(FontCacheManager& manager, int fontId, uint8_t bionicReadingMode = 0) const;
  void render(const GfxRenderer& renderer, int fontId, int x, int y, uint8_t bionicReadingMode = 0) const;
  BlockType getType() override { return TEXT_BLOCK; }
  bool serialize(FsFile& file) const;
  static std::unique_ptr<TextBlock> deserialize(FsFile& file);
};
