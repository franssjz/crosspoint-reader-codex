#include "TextBlock.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Serialization.h>

#include <algorithm>
#include <cstring>
#include <new>

namespace {
constexpr uint8_t BIONIC_READING_OFF = 0;
constexpr uint8_t BIONIC_READING_NORMAL = 1;
constexpr uint8_t BIONIC_READING_SUBTLE = 2;
constexpr int DECORATION_LINE_THICKNESS = 4;
constexpr int STRIKETHROUGH_ASCENDER_PERCENT = 66;
constexpr int UNDERLINE_BASELINE_OFFSET_PX = 6;
constexpr uint16_t MAX_SERIALIZED_LINE_WORDS = 512;

// Bionic Reading helpers — no heap, no std::string, stack-only slicing.

// Faithful port of metaguiding.py:78 — midpoint = 1 if n in (1,3) else ceil(n/2)
static constexpr int bionicMidpoint(int n) { return (n == 1 || n == 3) ? 1 : (n + 1) / 2; }

// Count UTF-8 codepoints in [begin, end) by skipping continuation bytes.
static int utf8CodepointCount(const char* begin, const char* end) {
  int n = 0;
  for (const char* p = begin; p < end; ++p) {
    if ((static_cast<uint8_t>(*p) & 0xC0) != 0x80) ++n;
  }
  return n;
}

// Mirrors Python's \w under re.UNICODE: ASCII alnum/underscore + all non-ASCII bytes (UTF-8).
static inline bool isWordByte(uint8_t b) {
  if (b >= 0x80) return true;
  return (b >= '0' && b <= '9') || (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') || (b == '_');
}

struct TextDecorationMetrics {
  int startX = 0;
  int width = 0;
};

TextDecorationMetrics getDecorationMetrics(const GfxRenderer& renderer, const int fontId, const int wordX,
                                           const char* word, const EpdFontFamily::Style style) {
  const size_t wordLen = strlen(word);
  TextDecorationMetrics metrics{wordX, renderer.getTextWidth(fontId, word, style)};

  // If word starts with em-space ("\xe2\x80\x83"), account for the additional indent
  // before drawing decoration lines.
  if (wordLen >= 3 && static_cast<uint8_t>(word[0]) == 0xE2 && static_cast<uint8_t>(word[1]) == 0x80 &&
      static_cast<uint8_t>(word[2]) == 0x83) {
    const char* visiblePtr = word + 3;
    const int prefixWidth = renderer.getTextAdvanceX(fontId, "\xe2\x80\x83", style);
    metrics.startX = wordX + prefixWidth;
    metrics.width = renderer.getTextWidth(fontId, visiblePtr, style);
  }

  return metrics;
}

void drawDecorationLine(const GfxRenderer& renderer, const int startX, const int centerY, const int width) {
  if (width <= 0) {
    return;
  }
  const int lineY = centerY - DECORATION_LINE_THICKNESS / 2;
  renderer.drawLine(startX, lineY, startX + width - 1, lineY, DECORATION_LINE_THICKNESS, true);
}
}  // namespace

size_t TextBlock::arenaSize(const uint16_t wc, const bool hasFocus, const bool hasRuby, const bool hasLayoutFlags,
                            const uint16_t baseBytes, const uint16_t rubyBytes) {
  size_t size = static_cast<size_t>(wc) * (sizeof(uint16_t) + sizeof(int16_t) + sizeof(uint8_t));
  if (hasRuby) size += static_cast<size_t>(wc) * sizeof(uint16_t);
  if (hasFocus) size += static_cast<size_t>(wc) * (sizeof(uint16_t) + sizeof(uint8_t));
  if (hasLayoutFlags) size += static_cast<size_t>(wc) * sizeof(uint8_t);
  return size + baseBytes + rubyBytes;
}

void TextBlock::bindArenaPointers() {
  uint8_t* base = arena.get();
  const size_t wc = numWords;
  size_t off = 0;
  textOffArr = reinterpret_cast<const uint16_t*>(base + off);
  off += wc * sizeof(uint16_t);
  if (rubyPresent) {
    rubyOffArr = reinterpret_cast<const uint16_t*>(base + off);
    off += wc * sizeof(uint16_t);
  }
  xposArr = reinterpret_cast<const int16_t*>(base + off);
  off += wc * sizeof(int16_t);
  if (focusPresent) {
    focusSuffixXArr = reinterpret_cast<const uint16_t*>(base + off);
    off += wc * sizeof(uint16_t);
  }
  stylesArr = base + off;
  off += wc;
  if (focusPresent) {
    focusBoundaryArr = base + off;
    off += wc;
  }
  if (layoutFlagsPresent) {
    layoutFlagsArr = base + off;
    off += wc;
  }
  textArr = reinterpret_cast<const char*>(base + off);
  off += textBytes;
  rubyTextArr = rubyPresent ? reinterpret_cast<const char*>(base + off) : nullptr;
}

TextBlock::TextBlock(const std::vector<std::string>& words, const std::vector<int16_t>& wordXpos,
                     const std::vector<EpdFontFamily::Style>& wordStyles, const std::vector<uint8_t>& focusBoundary,
                     const std::vector<uint16_t>& focusSuffixX, const std::vector<uint8_t>& layoutFlags,
                     const BlockStyle& style, const std::vector<std::string>& rubyTexts)
    : blockStyle(style) {
  const bool hasFocus = !focusBoundary.empty();
  const bool hasRuby = std::any_of(rubyTexts.begin(), rubyTexts.end(), [](const std::string& s) { return !s.empty(); });
  const bool hasLayoutFlags =
      std::any_of(layoutFlags.begin(), layoutFlags.end(), [](const uint8_t flags) { return flags != 0; });
  if (words.size() != wordXpos.size() || words.size() != wordStyles.size() ||
      words.size() > MAX_SERIALIZED_LINE_WORDS ||
      (hasFocus && (words.size() != focusBoundary.size() || words.size() != focusSuffixX.size())) ||
      (!layoutFlags.empty() && words.size() != layoutFlags.size()) || (hasRuby && rubyTexts.size() != words.size())) {
    LOG_ERR("TXB", "Construction failed: inconsistent word arrays");
    isValid = false;
    return;
  }
  numWords = static_cast<uint16_t>(words.size());
  focusPresent = hasFocus;
  rubyPresent = hasRuby;
  layoutFlagsPresent = hasLayoutFlags;
  size_t baseTotal = 0;
  size_t rubyTotal = 0;
  for (const auto& word : words) baseTotal += word.size() + 1;
  if (hasRuby)
    for (const auto& ruby : rubyTexts)
      if (!ruby.empty()) rubyTotal += ruby.size() + 1;
  if (baseTotal > UINT16_MAX || rubyTotal > UINT16_MAX) {
    LOG_ERR("TXB", "Construction failed: text arena exceeds 16-bit offsets");
    numWords = 0;
    isValid = false;
    return;
  }
  textBytes = static_cast<uint16_t>(baseTotal);
  rubyTextBytes = static_cast<uint16_t>(rubyTotal);
  if (numWords == 0) return;
  const size_t size = arenaSize(numWords, focusPresent, rubyPresent, layoutFlagsPresent, textBytes, rubyTextBytes);
  arena.reset(new (std::nothrow) uint8_t[size]);
  if (!arena) {
    LOG_ERR("TXB", "OOM: text arena %u bytes", static_cast<uint32_t>(size));
    numWords = textBytes = rubyTextBytes = 0;
    isValid = false;
    return;
  }
  bindArenaPointers();
  auto* offsets = const_cast<uint16_t*>(textOffArr);
  auto* rubyOffsets = const_cast<uint16_t*>(rubyOffArr);
  auto* xpos = const_cast<int16_t*>(xposArr);
  auto* styles = const_cast<uint8_t*>(stylesArr);
  auto* text = const_cast<char*>(textArr);
  auto* rubyText = const_cast<char*>(rubyTextArr);
  uint16_t textOff = 0;
  uint16_t rubyOff = 0;
  for (uint16_t i = 0; i < numWords; ++i) {
    offsets[i] = textOff;
    xpos[i] = wordXpos[i];
    styles[i] = static_cast<uint8_t>(wordStyles[i]);
    memcpy(text + textOff, words[i].c_str(), words[i].size() + 1);
    textOff += static_cast<uint16_t>(words[i].size() + 1);
    if (rubyPresent) {
      if (rubyTexts[i].empty()) {
        rubyOffsets[i] = UINT16_MAX;
      } else {
        rubyOffsets[i] = rubyOff;
        memcpy(rubyText + rubyOff, rubyTexts[i].c_str(), rubyTexts[i].size() + 1);
        rubyOff += static_cast<uint16_t>(rubyTexts[i].size() + 1);
      }
    }
  }
  if (focusPresent) {
    auto* suffix = const_cast<uint16_t*>(focusSuffixXArr);
    auto* boundary = const_cast<uint8_t*>(focusBoundaryArr);
    for (uint16_t i = 0; i < numWords; ++i) {
      suffix[i] = focusSuffixX[i];
      boundary[i] = focusBoundary[i];
    }
  }
  if (layoutFlagsPresent) {
    auto* flags = const_cast<uint8_t*>(layoutFlagsArr);
    memcpy(flags, layoutFlags.data(), numWords);
  }
}

void TextBlock::recordFontUsage(FontCacheManager& fontCacheManager, const int fontId,
                                const uint8_t bionicReadingMode) const {
  if (!isValid) return;
  for (uint16_t i = 0; i < numWords; i++) {
    const EpdFontFamily::Style style = wordStyle(i);
    fontCacheManager.recordText(wordText(i), fontId, style);
    if (bionicReadingMode == BIONIC_READING_NORMAL && (style & EpdFontFamily::BOLD) == 0) {
      fontCacheManager.recordText(wordText(i), fontId, static_cast<EpdFontFamily::Style>(style | EpdFontFamily::BOLD));
    }
    if (rubyText(i)[0] != '\0') {
      fontCacheManager.recordText(rubyText(i), fontId, EpdFontFamily::SUP);
    }
  }
}

void TextBlock::render(const GfxRenderer& renderer, const int fontId, const int x, const int y,
                       const uint8_t bionicReadingMode) const {
  if (!isValid) {
    LOG_ERR("TXB", "Render skipped: invalid block");
    return;
  }

  const bool scanning = renderer.isFontCacheScanning();
  const int ascender = renderer.getFontAscenderSize(fontId);
  const int rubyShift = getRubyShift(ascender);
  size_t rubyGroupEnd = 0;
  for (uint16_t i = 0; i < numWords; i++) {
    int rubyX = 0;
    if (i >= rubyGroupEnd) {
      rubyGroupEnd = i;
      if (rubyText(i)[0] != '\0' && (wordStyle(i) & EpdFontFamily::RUBY_CONTINUE) == 0) {
        size_t count = 1;
        int baseWidth = renderer.getTextAdvanceX(fontId, wordText(i), wordStyle(i));
        while (i + count < numWords && (wordStyle(i + count) & EpdFontFamily::RUBY_CONTINUE) != 0) {
          baseWidth += renderer.getTextAdvanceX(fontId, wordText(i + count), wordStyle(i + count));
          ++count;
        }
        const int rubyWidth = renderer.getTextAdvanceX(fontId, rubyText(i), EpdFontFamily::SUP);
        rubyGroupEnd = i + count;
        rubyX = wordXpos(i) + x - (rubyWidth - baseWidth) / 2;
        rubyX = std::max(0, std::min(rubyX, renderer.getScreenWidth() - rubyWidth));
      }
    }

    const int wordX = wordXpos(i) + x;
    const EpdFontFamily::Style currentStyle = wordStyle(i);
    const char* w = wordText(i);
    const size_t wLen = wordTextLen(i);

    // SUP/SUB shifts are relative to the full-size ascender; glyphs are scaled in drawText.
    int wordY = y + rubyShift;
    if ((currentStyle & EpdFontFamily::SUP) != 0) {
      wordY -= ascender * 2 / 5;
    } else if ((currentStyle & EpdFontFamily::SUB) != 0) {
      wordY += ascender / 4;
    }

    if (rubyText(i)[0] != '\0' && (wordStyle(i) & EpdFontFamily::RUBY_CONTINUE) == 0) {
      renderer.drawText(fontId, rubyX, wordY - ascender, rubyText(i), true, EpdFontFamily::SUP);
    }

    // Normal uses layout-time focus annotations; Subtle remains render-only.
    const bool alreadyBold = (currentStyle & EpdFontFamily::BOLD) != 0;
    const bool bionicEnabled = bionicReadingMode == BIONIC_READING_NORMAL || bionicReadingMode == BIONIC_READING_SUBTLE;
    const bool bionicNormal = bionicReadingMode == BIONIC_READING_NORMAL;
    const uint8_t focusSplit = focusPresent && bionicNormal && !alreadyBold ? focusBoundary(i) : 0;
    if (bionicNormal) {
      if (focusSplit > 0) {
        char buf[40];
        size_t splitByte = std::min<size_t>({static_cast<size_t>(focusSplit), wLen, sizeof(buf) - 1});
        while (splitByte > 0 && splitByte < wLen && (static_cast<uint8_t>(w[splitByte]) & 0xC0) == 0x80) {
          --splitByte;
        }
        if (splitByte > 0 && splitByte < wLen) {
          memcpy(buf, w, splitByte);
          buf[splitByte] = '\0';
          const EpdFontFamily::Style boldStyle = static_cast<EpdFontFamily::Style>(currentStyle | EpdFontFamily::BOLD);
          renderer.drawText(fontId, wordX, wordY, buf, true, boldStyle);
          renderer.drawText(fontId, wordX + focusSuffixX(i), wordY, w + splitByte, true, currentStyle);
        } else {
          renderer.drawText(fontId, wordX, wordY, w, true, currentStyle);
        }
      } else {
        renderer.drawText(fontId, wordX, wordY, w, true, currentStyle);
      }
    } else if (bionicReadingMode == BIONIC_READING_OFF || !bionicEnabled || alreadyBold || wLen >= 128) {
      renderer.drawText(fontId, wordX, wordY, w, true, currentStyle);
    } else {
      // Stack slice buffer (<128 bytes, well within CLAUDE.md <256 byte rule).
      char buf[128];
      int cursorX = wordX;
      size_t i0 = 0;

      while (i0 < wLen) {
        // Non-word run: draw in original style, advance cursor.
        size_t j = i0;
        while (j < wLen && !isWordByte(static_cast<uint8_t>(w[j]))) ++j;
        if (j > i0) {
          const size_t n = j - i0;
          memcpy(buf, w + i0, n);
          buf[n] = '\0';
          renderer.drawText(fontId, cursorX, wordY, buf, true, currentStyle);
          cursorX += renderer.getTextAdvanceX(fontId, buf, currentStyle);
          i0 = j;
          if (i0 >= wLen) break;
        }

        // Word run: emphasize the first M codepoints, regular for the rest.
        size_t k = i0;
        while (k < wLen && isWordByte(static_cast<uint8_t>(w[k]))) ++k;

        const int ncp = utf8CodepointCount(w + i0, w + k);
        const int mcp = bionicMidpoint(ncp);

        // Find byte boundary after the M-th codepoint.
        size_t splitByte = i0;
        {
          size_t p = i0;
          int seen = 0;
          while (p < k && seen < mcp) {
            ++p;
            while (p < k && (static_cast<uint8_t>(w[p]) & 0xC0) == 0x80) ++p;
            ++seen;
          }
          splitByte = p;
        }

        // Emphasized prefix.
        {
          const size_t n = splitByte - i0;
          memcpy(buf, w + i0, n);
          buf[n] = '\0';
          if (bionicReadingMode == BIONIC_READING_SUBTLE) {
            renderer.drawText(fontId, cursorX, wordY, buf, true, currentStyle);
            renderer.drawText(fontId, cursorX + 1, wordY, buf, true, currentStyle);
            cursorX += renderer.getTextAdvanceX(fontId, buf, currentStyle);
          } else {
            const EpdFontFamily::Style boldStyle =
                static_cast<EpdFontFamily::Style>(currentStyle | EpdFontFamily::BOLD);
            renderer.drawText(fontId, cursorX, wordY, buf, true, boldStyle);
            cursorX += renderer.getTextAdvanceX(fontId, buf, boldStyle);
          }
        }

        // Regular suffix (if any).
        if (splitByte < k) {
          const size_t n = k - splitByte;
          memcpy(buf, w + splitByte, n);
          buf[n] = '\0';
          renderer.drawText(fontId, cursorX, wordY, buf, true, currentStyle);
          cursorX += renderer.getTextAdvanceX(fontId, buf, currentStyle);
        }

        i0 = k;
      }
    }

    const bool hasUnderline = (currentStyle & EpdFontFamily::UNDERLINE) != 0;
    const bool hasStrikethrough = (currentStyle & EpdFontFamily::STRIKETHROUGH) != 0;
    if (!scanning && (hasUnderline || hasStrikethrough)) {
      auto decoration = getDecorationMetrics(renderer, fontId, wordX, w, currentStyle);
      // SUP/SUB glyphs are rendered at 50% scale, while the font metrics above
      // report their full-size width. Keep underline/strikethrough aligned with
      // the visible glyphs instead of drawing them roughly twice as long.
      if ((currentStyle & (EpdFontFamily::SUP | EpdFontFamily::SUB)) != 0) {
        decoration.width = (decoration.width + 1) / 2;
      }
      if (decoration.width <= 0) {
        continue;
      }
      if (hasStrikethrough) {
        const int strikeY = wordY + ascender * STRIKETHROUGH_ASCENDER_PERCENT / 100;
        drawDecorationLine(renderer, decoration.startX, strikeY, decoration.width);
      }
      if (hasUnderline) {
        // y is the top of the text line; add ascender to reach baseline, then offset below.
        const int underlineY = wordY + ascender + UNDERLINE_BASELINE_OFFSET_PX;
        drawDecorationLine(renderer, decoration.startX, underlineY, decoration.width);
      }
    }
  }
}

bool TextBlock::serialize(FsFile& file) const {
  if (!isValid) return false;
  serialization::writePod(file, numWords);
  serialization::writePod(file, static_cast<uint8_t>(focusPresent));
  serialization::writePod(file, static_cast<uint8_t>(rubyPresent));
  serialization::writePod(file, static_cast<uint8_t>(layoutFlagsPresent));
  serialization::writePod(file, textBytes);
  serialization::writePod(file, rubyTextBytes);
  if (numWords > 0) {
    const size_t size = arenaSize(numWords, focusPresent, rubyPresent, layoutFlagsPresent, textBytes, rubyTextBytes);
    if (file.write(arena.get(), size) != size) return false;
  }

  // Style (alignment + margins/padding/indent)
  serialization::writePod(file, blockStyle.alignment);
  serialization::writePod(file, blockStyle.textAlignDefined);
  serialization::writePod(file, blockStyle.marginTop);
  serialization::writePod(file, blockStyle.marginBottom);
  serialization::writePod(file, blockStyle.marginLeft);
  serialization::writePod(file, blockStyle.marginRight);
  serialization::writePod(file, blockStyle.paddingTop);
  serialization::writePod(file, blockStyle.paddingBottom);
  serialization::writePod(file, blockStyle.paddingLeft);
  serialization::writePod(file, blockStyle.paddingRight);
  serialization::writePod(file, blockStyle.textIndent);
  serialization::writePod(file, blockStyle.textIndentDefined);

  return true;
}

std::unique_ptr<TextBlock> TextBlock::deserialize(FsFile& file) {
  uint16_t wc = 0;
  uint8_t hasFocus = 0;
  uint8_t hasRuby = 0;
  uint8_t hasLayoutFlags = 0;
  uint16_t baseBytes = 0;
  uint16_t rubyBytes = 0;
  serialization::readPod(file, wc);
  serialization::readPod(file, hasFocus);
  serialization::readPod(file, hasRuby);
  serialization::readPod(file, hasLayoutFlags);
  serialization::readPod(file, baseBytes);
  serialization::readPod(file, rubyBytes);
  if (wc > MAX_SERIALIZED_LINE_WORDS || (wc == 0 && (baseBytes != 0 || rubyBytes != 0)) || (wc > 0 && baseBytes < wc) ||
      (hasRuby == 0 && rubyBytes != 0))
    return nullptr;

  std::unique_ptr<TextBlock> block(new (std::nothrow) TextBlock());
  if (!block) return nullptr;
  block->numWords = wc;
  block->focusPresent = hasFocus != 0;
  block->rubyPresent = hasRuby != 0;
  block->layoutFlagsPresent = hasLayoutFlags != 0;
  block->textBytes = baseBytes;
  block->rubyTextBytes = rubyBytes;
  if (wc > 0) {
    const size_t size =
        arenaSize(wc, block->focusPresent, block->rubyPresent, block->layoutFlagsPresent, baseBytes, rubyBytes);
    block->arena.reset(new (std::nothrow) uint8_t[size]);
    if (!block->arena || file.read(block->arena.get(), size) != size) return nullptr;
    block->bindArenaPointers();
    if (block->textOffArr[0] != 0 || block->textArr[baseBytes - 1] != '\0') return nullptr;
    for (uint16_t i = 1; i < wc; ++i) {
      if (block->textOffArr[i] <= block->textOffArr[i - 1] || block->textOffArr[i] >= baseBytes ||
          block->textArr[block->textOffArr[i] - 1] != '\0')
        return nullptr;
    }
    if (block->rubyPresent) {
      for (uint16_t i = 0; i < wc; ++i) {
        const uint16_t off = block->rubyOffArr[i];
        if (off != UINT16_MAX && off >= rubyBytes) return nullptr;
      }
    }
  }

  // Style (alignment + margins/padding/indent)
  BlockStyle& blockStyle = block->blockStyle;
  serialization::readPod(file, blockStyle.alignment);
  serialization::readPod(file, blockStyle.textAlignDefined);
  serialization::readPod(file, blockStyle.marginTop);
  serialization::readPod(file, blockStyle.marginBottom);
  serialization::readPod(file, blockStyle.marginLeft);
  serialization::readPod(file, blockStyle.marginRight);
  serialization::readPod(file, blockStyle.paddingTop);
  serialization::readPod(file, blockStyle.paddingBottom);
  serialization::readPod(file, blockStyle.paddingLeft);
  serialization::readPod(file, blockStyle.paddingRight);
  serialization::readPod(file, blockStyle.textIndent);
  serialization::readPod(file, blockStyle.textIndentDefined);

  return block;
}
