#pragma once

#include <EpdFontFamily.h>
#include <HalStorage.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Block.h"
#include "BlockStyle.h"
#include "Epub/FootnoteEntry.h"

class FontCacheManager;

// A rendered text line. All per-word arrays, base text and ruby text share one
// allocation (the arena) to avoid the hundreds of tiny vector/string
// allocations formerly created while loading a page.
//
// Arena layout, in order (2-byte alignment holds by construction: all 16-bit
// arrays come first and the arena base is allocator-aligned; RISC-V faults on
// unaligned multi-byte access):
//   uint16_t textOff[wordCount]        byte offset of word i's text in text[]
//   uint16_t rubyOff[wordCount]        present only when rubyPresent (UINT16_MAX = none)
//   int16_t  xpos[wordCount]
//   uint16_t focusSuffixX[wordCount]   present only when focusPresent
//   uint8_t  styles[wordCount]
//   uint8_t  focusBoundary[wordCount]  present only when focusPresent
//   uint8_t  layoutFlags[wordCount]    present only when layoutFlagsPresent
//   char     text[textBytes]           all words back to back, NUL-terminated
//   char     rubyText[rubyTextBytes]   present only when rubyPresent
//
// Each word is stored NUL-terminated so render() can hand `text + textOff[i]`
// straight to C APIs (drawText) with no std::string materialization.
class TextBlock final : public Block {
 public:
  struct LinkSpan {
    char href[FOOTNOTE_HREF_LEN];
    int16_t x;
    int16_t width;
    int16_t topLift;
  };

 private:
  BlockStyle blockStyle;
  uint16_t numWords = 0;
  uint16_t textBytes = 0;
  uint16_t rubyTextBytes = 0;
  bool focusPresent = false;
  bool rubyPresent = false;
  bool layoutFlagsPresent = false;
  bool isValid = true;
  // The ONLY allocation: makeUniqueNoThrow, so OOM yields an invalid block
  // instead of abort() (bare new is not nothrow with -fno-exceptions).
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
  // Layout-only metadata. ChapterHtmlSlimParser moves it into Page::links
  // immediately; cached TextBlocks therefore keep the same compact format.
  std::vector<LinkSpan> linkSpans;

  TextBlock() = default;
  static size_t arenaSize(uint16_t wordCount, bool hasFocus, bool hasRuby, bool hasLayoutFlags, uint16_t textBytes,
                          uint16_t rubyTextBytes);
  void bindArenaPointers();

 public:
  static constexpr uint8_t WORD_FLAG_INSERTED_HYPHEN = 0x01;

  // Flatten-on-construct: copies the layout-time vectors into the arena; the
  // vectors die with the caller. On arena OOM the block is empty and valid()
  // is false -- callers must check and fail the line instead of using it.
  explicit TextBlock(const std::vector<std::string>& words, const std::vector<int16_t>& wordXpos,
                     const std::vector<EpdFontFamily::Style>& wordStyles, const std::vector<uint8_t>& focusBoundary,
                     const std::vector<uint16_t>& focusSuffixX, const std::vector<uint8_t>& layoutFlags,
                     const BlockStyle& blockStyle = BlockStyle(), const std::vector<std::string>& rubyTexts = {},
                     std::vector<LinkSpan> linkSpans = {});
  ~TextBlock() override = default;
  TextBlock(const TextBlock&) = delete;
  TextBlock& operator=(const TextBlock&) = delete;

  void setBlockStyle(const BlockStyle& style) { blockStyle = style; }
  const BlockStyle& getBlockStyle() const { return blockStyle; }
  bool isEmpty() override { return numWords == 0; }
  bool valid() const { return isValid; }
  uint16_t wordCount() const { return numWords; }
  // NUL-terminated by construction; safe to pass to C APIs directly.
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
  std::vector<LinkSpan> takeLinkSpans() { return std::move(linkSpans); }

  void recordFontUsage(FontCacheManager& manager, int fontId, uint8_t bionicReadingMode = 0) const;
  void render(const GfxRenderer& renderer, int fontId, int x, int y, uint8_t bionicReadingMode = 0) const;
  BlockType getType() override { return TEXT_BLOCK; }
  bool serialize(HalFile& file) const;
  static std::unique_ptr<TextBlock> deserialize(HalFile& file);
};
