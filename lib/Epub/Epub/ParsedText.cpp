#include "ParsedText.h"

#include <GfxRenderer.h>
#include <Utf8.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <vector>

#include "RubyCjkLayoutUtils.h"
#include "TokenBoundary.h"
#include "hyphenation/Hyphenator.h"

constexpr int MAX_COST = std::numeric_limits<int>::max();

namespace {

// Soft hyphen byte pattern used throughout EPUBs (UTF-8 for U+00AD).
constexpr char SOFT_HYPHEN_UTF8[] = "\xC2\xAD";
constexpr size_t SOFT_HYPHEN_BYTES = 2;

// Returns the first rendered codepoint of a word (skipping leading soft hyphens).
uint32_t firstCodepoint(const std::string& word) {
  const auto* ptr = reinterpret_cast<const unsigned char*>(word.c_str());
  while (true) {
    const uint32_t cp = utf8NextCodepoint(&ptr);
    if (cp == 0) return 0;
    if (cp != 0x00AD) return cp;  // skip soft hyphens
  }
}

// Returns the last codepoint of a word by scanning backward for the start of the last UTF-8 sequence.
uint32_t lastCodepoint(const std::string& word) {
  if (word.empty()) return 0;
  // UTF-8 continuation bytes start with 10xxxxxx; scan backward to find the leading byte.
  size_t i = word.size() - 1;
  while (i > 0 && (static_cast<uint8_t>(word[i]) & 0xC0) == 0x80) {
    --i;
  }
  const auto* ptr = reinterpret_cast<const unsigned char*>(word.c_str() + i);
  return utf8NextCodepoint(&ptr);
}

int naturalWordGap(const GfxRenderer& renderer, const int fontId, const std::string& left, const std::string& right,
                   const EpdFontFamily::Style style, const uint8_t level) {
  const int natural = renderer.getSpaceAdvance(fontId, lastCodepoint(left), firstCodepoint(right), style);
  // A font-independent step remains visible with narrow-space fonts and keeps
  // level zero bit-for-bit compatible with old pagination.
  return natural + std::min<uint8_t>(level, 4) * 10;
}

bool isCjkIdeograph(const uint32_t cp) {
  return (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
         (cp >= 0x20000 && cp <= 0x3FFFF);
}

bool containsSoftHyphen(const std::string& word) { return word.find(SOFT_HYPHEN_UTF8) != std::string::npos; }

// Removes every soft hyphen in-place so rendered glyphs match measured widths.
void stripSoftHyphensInPlace(std::string& word) {
  size_t pos = 0;
  while ((pos = word.find(SOFT_HYPHEN_UTF8, pos)) != std::string::npos) {
    word.erase(pos, SOFT_HYPHEN_BYTES);
  }
}

// Returns the advance width for a word while ignoring soft hyphen glyphs and optionally appending a visible hyphen.
// Uses advance width (sum of glyph advances + kerning) rather than bounding box width so that italic glyph overhangs
// don't inflate inter-word spacing.
uint16_t measureWordWidth(const GfxRenderer& renderer, const int fontId, const std::string& word,
                          const EpdFontFamily::Style style, const bool appendHyphen = false) {
  if (word.size() == 1 && word[0] == ' ' && !appendHyphen) {
    return renderer.getSpaceWidth(fontId, style);
  }
  const bool hasSoftHyphen = containsSoftHyphen(word);
  if (!hasSoftHyphen && !appendHyphen) {
    return renderer.getTextAdvanceX(fontId, word.c_str(), style);
  }

  std::string sanitized = word;
  if (hasSoftHyphen) {
    stripSoftHyphensInPlace(sanitized);
  }
  if (appendHyphen) {
    sanitized.push_back('-');
  }
  return renderer.getTextAdvanceX(fontId, sanitized.c_str(), style);
}

bool endsWithBreakableHyphen(const std::string& token) {
  return !token.empty() && TokenBoundary::allowsBreakAfterExplicitHyphen(lastCodepoint(token));
}

constexpr size_t FOCUS_PREFIX_BUF_SIZE = 40;

uint16_t measureFocusPrefixAdvance(const GfxRenderer& renderer, const int fontId, const std::string& word,
                                   const EpdFontFamily::Style style, const uint8_t focusBoundary) {
  char prefix[FOCUS_PREFIX_BUF_SIZE];
  const size_t prefixLen = std::min<size_t>(focusBoundary, FOCUS_PREFIX_BUF_SIZE - 1);
  memcpy(prefix, word.data(), prefixLen);
  prefix[prefixLen] = '\0';

  const auto boldStyle = static_cast<EpdFontFamily::Style>(style | EpdFontFamily::BOLD);
  const auto* suffix = reinterpret_cast<const unsigned char*>(word.c_str() + focusBoundary);
  const int kerning = renderer.getKerning(fontId, lastCodepoint(prefix), utf8NextCodepoint(&suffix), boldStyle);
  return static_cast<uint16_t>(renderer.getTextAdvanceX(fontId, prefix, boldStyle) + kerning);
}

uint16_t measureFocusWordWidth(const GfxRenderer& renderer, const int fontId, const std::string& word,
                               const EpdFontFamily::Style style, const uint8_t focusBoundary,
                               const bool appendHyphen = false) {
  if (focusBoundary == 0) return measureWordWidth(renderer, fontId, word, style, appendHyphen);
  if (focusBoundary >= word.size()) {
    return measureWordWidth(renderer, fontId, word, static_cast<EpdFontFamily::Style>(style | EpdFontFamily::BOLD),
                            appendHyphen);
  }
  const uint16_t suffixWidth = appendHyphen
                                   ? measureWordWidth(renderer, fontId, word.substr(focusBoundary), style, true)
                                   : renderer.getTextAdvanceX(fontId, word.c_str() + focusBoundary, style);
  return static_cast<uint16_t>(measureFocusPrefixAdvance(renderer, fontId, word, style, focusBoundary) + suffixWidth);
}

// Checks if a UTF-8 codepoint should be counted as part of a word for Focus Reading
bool isWordCharacter(uint32_t cp) {
  // ASCII range (Catches 95%+ of characters immediately)
  if (cp < 128) {
    // Bitwise trick: (cp | 0x20) converts uppercase ASCII to lowercase.
    // This checks for A-Z and a-z mathematically, avoiding memory lookups and <cctype>
    return ((cp | 0x20) >= 'a' && (cp | 0x20) <= 'z') || cp == '\'';
  }

  // General Punctuation Block, Currency, Math, Arrows, & Symbols (0x2000 - 0x2BFF)
  if (cp >= 0x2000 && cp <= 0x2BFF) {
    // Explicitly allow smart quotes, reject all other general punctuation (em-dashes, etc.)
    return cp == 0x2018 || cp == 0x2019;
  }

  // Latin-1 Punctuation Block (0x00A1 - 0x00BF)
  if (cp >= 0x00A1 && cp <= 0x00BF) {
    // Allow ordinal indicators and micro sign, reject the rest (¡, ¿, «, », etc.)
    return cp == 0x00AA || cp == 0x00B5 || cp == 0x00BA;
  }

  // Rejects Two-em dash, Three-em dash, Double oblique hyphen, etc.
  if (cp >= 0x2E00 && cp <= 0x2E7F) return false;

  // Rejects Modifier Minus (0x02D7), Small Hyphen (0xFE63), and Fullwidth Hyphen (0xFF0D)
  if (cp == 0x02D7 || cp == 0xFE63 || cp == 0xFF0D) return false;
  // Assume all other Unicode ranges (accented letters, Cyrillic, Greek, etc.) are valid

  return true;
}

}  // namespace

uint32_t ParsedText::visibleOffsetBaseAt(const size_t wordIndex) const {
  uint32_t base = visibleOffsetBase;
  for (const auto& rebase : visibleOffsetRebases) {
    if (rebase.wordIndex > wordIndex) break;
    base = rebase.base;
  }
  return base;
}

uint32_t ParsedText::visibleOffsetAt(const size_t wordIndex) const {
  if (wordIndex >= wordVisibleOffsetDeltas.size()) return 0;
  return visibleOffsetBaseAt(wordIndex) + wordVisibleOffsetDeltas[wordIndex];
}

void ParsedText::pushVisibleOffset(const uint32_t offset) {
  uint32_t base = visibleOffsetBase;
  if (wordVisibleOffsetDeltas.empty()) {
    visibleOffsetBase = offset;
    base = offset;
  } else if (!visibleOffsetRebases.empty()) {
    base = visibleOffsetRebases.back().base;
  }

  if (offset < base || offset - base > std::numeric_limits<uint16_t>::max()) {
    visibleOffsetRebases.push_back({wordVisibleOffsetDeltas.size(), offset});
    base = offset;
  }
  wordVisibleOffsetDeltas.push_back(static_cast<uint16_t>(offset - base));
}

void ParsedText::insertVisibleOffset(const size_t wordIndex, const uint32_t offset) {
  const uint32_t base = wordIndex > 0 ? visibleOffsetBaseAt(wordIndex - 1) : visibleOffsetBase;
  for (auto& rebase : visibleOffsetRebases) {
    if (rebase.wordIndex >= wordIndex) ++rebase.wordIndex;
  }

  uint32_t insertionBase = base;
  if (offset < base || offset - base > std::numeric_limits<uint16_t>::max()) {
    const auto rebaseIt = std::find_if(visibleOffsetRebases.begin(), visibleOffsetRebases.end(),
                                       [wordIndex](const auto& rebase) { return rebase.wordIndex > wordIndex; });
    visibleOffsetRebases.insert(rebaseIt, {wordIndex, offset});
    insertionBase = offset;
  }
  wordVisibleOffsetDeltas.insert(wordVisibleOffsetDeltas.begin() + wordIndex,
                                 static_cast<uint16_t>(offset - insertionBase));
}

void ParsedText::eraseVisibleOffsetPrefix(const size_t count) {
  if (count >= wordVisibleOffsetDeltas.size()) {
    wordVisibleOffsetDeltas.clear();
    visibleOffsetRebases.clear();
    visibleOffsetBase = 0;
    return;
  }

  const uint32_t newBase = visibleOffsetBaseAt(count);
  wordVisibleOffsetDeltas.erase(wordVisibleOffsetDeltas.begin(), wordVisibleOffsetDeltas.begin() + count);
  size_t writeIndex = 0;
  for (auto rebase : visibleOffsetRebases) {
    if (rebase.wordIndex <= count) continue;
    rebase.wordIndex -= count;
    visibleOffsetRebases[writeIndex++] = rebase;
  }
  visibleOffsetRebases.resize(writeIndex);
  visibleOffsetBase = newBase;
}

void ParsedText::addWord(std::string word, const EpdFontFamily::Style fontStyle, const bool underline,
                         const bool attachToPrevious, const uint32_t visibleTextOffset) {
  if (word.empty()) return;

  // The device fonts carry no combining-mark positioning, so EPUB text stored in NFD
  // (a base letter followed by separate combining accents -- common for Vietnamese,
  // and used for many EPUB <h1> chapter headings) renders with the marks detached or
  // misplaced. Compose to NFC here, the single funnel every word passes through, so a
  // precomposed glyph is used instead. This runs once per word at layout time (the
  // result is cached in the section file) and is a cheap no-op for mark-free text.
  word = utf8ComposeNfc(word);

  EpdFontFamily::Style baseStyle = fontStyle;
  if (underline) {
    baseStyle = static_cast<EpdFontFamily::Style>(baseStyle | EpdFontFamily::UNDERLINE);
  }

  // Already-bold text should stay fully bold; focus splitting would make its suffix regular later.
  if (!this->focusReadingEnabled || (baseStyle & EpdFontFamily::BOLD) != 0) {
    words.push_back(std::move(word));
    wordStyles.push_back(baseStyle);
    wordContinues.push_back(attachToPrevious);
    wordNoSpaceBefore.push_back(false);
    wordFocusBoundary.push_back(0);
    wordLayoutFlags.push_back(0);
    pushVisibleOffset(visibleTextOffset);
    if (!rubyTexts.empty()) rubyTexts.emplace_back();
    return;
  }

  // --- FOCUS READING LOGIC BELOW ---

  // Reserve for the actual segment upper bound, not every byte in the input.
  // The old byte-count reservation could allocate several times the necessary
  // memory for long UTF-8/CSS-heavy runs and was a common layout OOM trigger.
  size_t maxPossibleNewTokens = 0;
  {
    const unsigned char* scan = reinterpret_cast<const unsigned char*>(word.c_str());
    const unsigned char* const scanEnd = scan + word.length();
    bool haveSegment = false;
    bool previousWasWord = false;
    while (scan < scanEnd) {
      const bool currentIsWord = isWordCharacter(utf8NextCodepoint(&scan));
      if (!haveSegment || currentIsWord != previousWasWord) {
        maxPossibleNewTokens += currentIsWord ? 2 : 1;
        haveSegment = true;
        previousWasWord = currentIsWord;
      }
    }
  }
  const size_t requiredSize = words.size() + maxPossibleNewTokens;

  if (wordStyles.capacity() < requiredSize) {
    // Emulate standard geometric growth (doubling) to ensure we don't reallocate on every word.
    size_t newCapacity = wordStyles.capacity() * 2;

    // Ensure the doubled capacity is actually enough for this specific word
    if (newCapacity < requiredSize) {
      newCapacity = requiredSize;
    }
    // Set a sensible minimum starting size so the first few words don't trigger tiny reallocations
    if (newCapacity < 16) {
      newCapacity = 16;
    }

    wordStyles.reserve(newCapacity);
    wordContinues.reserve(newCapacity);
    wordNoSpaceBefore.reserve(newCapacity);
    wordFocusBoundary.reserve(newCapacity);
    wordLayoutFlags.reserve(newCapacity);
    wordVisibleOffsetDeltas.reserve(newCapacity);
  }

  // Lambda helper to process and push individual sub-segments of the string
  // Use std::string_view to avoid heap allocations when slicing
  auto processSegment = [&](std::string_view segment, bool isWord, bool attach, bool noSpaceBefore) {
    const auto* wordBegin = reinterpret_cast<const unsigned char*>(word.data());
    const auto* segmentBegin = reinterpret_cast<const unsigned char*>(segment.data());
    uint32_t segmentOffset = visibleTextOffset;
    const unsigned char* offsetPtr = wordBegin;
    while (offsetPtr < segmentBegin) {
      utf8NextCodepoint(&offsetPtr);
      ++segmentOffset;
    }
    if (!isWord) {
      // Punctuation and Numbers stay regular
      words.emplace_back(segment);
      wordStyles.push_back(baseStyle);
      wordContinues.push_back(attach);
      wordNoSpaceBefore.push_back(noSpaceBefore);
      wordFocusBoundary.push_back(0);
      wordLayoutFlags.push_back(0);
      pushVisibleOffset(segmentOffset);
      if (!rubyTexts.empty()) rubyTexts.emplace_back();
    } else {
      size_t charCount = 0;
      const unsigned char* countPtr = reinterpret_cast<const unsigned char*>(segment.data());
      const unsigned char* countEnd = countPtr + segment.length();

      while (countPtr < countEnd) {
        utf8NextCodepoint(&countPtr);
        charCount++;
      }

      // Target 45% for 1-bold at 4 chars and 3-bold at 7 chars with floor truncation
      constexpr size_t FOCUS_READING_PERCENT = 45;
      size_t targetBoldChars = (charCount * FOCUS_READING_PERCENT) / 100;
      targetBoldChars = std::clamp<size_t>(targetBoldChars, 1, 9);

      if (targetBoldChars >= charCount) {
        // Whole segment is bold - no suffix split needed
        words.emplace_back(segment);
        wordStyles.push_back(static_cast<EpdFontFamily::Style>(baseStyle | EpdFontFamily::BOLD));
        wordContinues.push_back(attach);
        wordNoSpaceBefore.push_back(noSpaceBefore);
        wordFocusBoundary.push_back(0);
        wordLayoutFlags.push_back(0);
        pushVisibleOffset(segmentOffset);
        if (!rubyTexts.empty()) rubyTexts.emplace_back();
      } else {
        countPtr = reinterpret_cast<const unsigned char*>(segment.data());
        for (size_t i = 0; i < targetBoldChars; ++i) {
          utf8NextCodepoint(&countPtr);
        }
        size_t splitByteOffset = countPtr - reinterpret_cast<const unsigned char*>(segment.data());

        // Keep the original word as one token. TextBlock applies bold only to
        // bytes before this boundary, while the hyphenator sees the full word.
        words.emplace_back(segment);
        wordStyles.push_back(baseStyle);
        wordContinues.push_back(attach);
        wordNoSpaceBefore.push_back(noSpaceBefore);
        wordFocusBoundary.push_back(static_cast<uint8_t>(std::min<size_t>(splitByteOffset, 255)));
        wordLayoutFlags.push_back(0);
        pushVisibleOffset(segmentOffset);
        if (!rubyTexts.empty()) rubyTexts.emplace_back();
      }
    }
  };

  // Tokenize the string by alternating states (Word vs. Non-Word)
  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(word.c_str());
  const unsigned char* end = ptr + word.length();

  const unsigned char* segmentStart = ptr;
  uint32_t firstCp = utf8NextCodepoint(&ptr);  // Consume the first char to determine initial state
  bool inWordSegment = isWordCharacter(firstCp);

  bool isFirstSegment = true;

  while (ptr < end) {
    const unsigned char* currentCpStart = ptr;
    uint32_t cp = utf8NextCodepoint(&ptr);
    bool isWordChar = isWordCharacter(cp);

    // Whenever the character type flips, slice off the segment we just completed and process it
    if (isWordChar != inWordSegment) {
      size_t segmentLen = currentCpStart - segmentStart;
      std::string_view segment(reinterpret_cast<const char*>(segmentStart), segmentLen);

      // A segment after a visible hyphen stays attached when it fits, but may
      // start the next line without receiving justification space.
      const bool breakAfterPrevious = !isFirstSegment && !words.empty() && endsWithBreakableHyphen(words.back());
      processSegment(segment, inWordSegment, isFirstSegment ? attachToPrevious : true, breakAfterPrevious);

      // Setup for the next segment
      segmentStart = currentCpStart;
      inWordSegment = isWordChar;
      isFirstSegment = false;
    }
  }

  // Process the final remaining segment
  size_t segmentLen = end - segmentStart;
  std::string_view segment(reinterpret_cast<const char*>(segmentStart), segmentLen);
  const bool breakAfterPrevious = !isFirstSegment && !words.empty() && endsWithBreakableHyphen(words.back());
  processSegment(segment, inWordSegment, isFirstSegment ? attachToPrevious : true, breakAfterPrevious);
}

void ParsedText::setRubyForWordAt(const size_t index, const std::string& ruby) {
  if (index >= words.size()) return;
  if (rubyTexts.size() < words.size()) rubyTexts.resize(words.size());
  rubyTexts[index] = ruby;
}

void ParsedText::setRubyGroupAt(const size_t startIndex, const size_t count, const std::string& ruby) {
  if (startIndex >= words.size() || count == 0) return;
  if (rubyTexts.size() < words.size()) rubyTexts.resize(words.size());
  rubyTexts[startIndex] = ruby;
  for (size_t i = 1; i < count && startIndex + i < words.size(); ++i) {
    const size_t index = startIndex + i;
    rubyTexts[index].clear();
    wordStyles[index] =
        static_cast<EpdFontFamily::Style>(static_cast<uint8_t>(wordStyles[index]) | EpdFontFamily::RUBY_CONTINUE);
    wordContinues[index] = true;
  }
}

void ParsedText::ensureRubyCapacity() {
  // rubyTexts is a deque so it grows in bounded chunks and needs no reserve.
}
// Consumes data to minimize memory usage
void ParsedText::layoutAndExtractLines(const GfxRenderer& renderer, const int fontId, const uint16_t viewportWidth,
                                       const std::function<void(std::shared_ptr<TextBlock>, uint32_t)>& processLine,
                                       const bool includeLastLine) {
  if (words.empty()) {
    return;
  }

  // Apply fixed transforms before any per-line layout work.
  prepareParagraphIndent(renderer, fontId);

  // Ensure SD card font glyph metrics are loaded before measuring word widths.
  // For flash-based fonts isSdCardFont() returns false and this block is skipped
  // entirely — no heap allocation. For SD card fonts this reads glyph metadata
  // (advanceX only, no bitmaps) for all unique codepoints in this paragraph so
  // that calculateWordWidths() can measure text without on-demand SD I/O.
  if (renderer.isSdCardFont(fontId)) {
    // Style mask: only ask the SD font to load advances for styles actually
    // used in this paragraph. Style index is the low two bits (regular/bold/
    // italic/bold-italic); the underline bit is irrelevant to advance metrics.
    uint8_t styleMask = 0;
    for (auto s : wordStyles) {
      styleMask |= static_cast<uint8_t>(1u << (static_cast<uint8_t>(s) & 0x03));
    }
    if (styleMask == 0) styleMask = 0x01;  // defensive: regular only
    renderer.ensureSdCardFontReady(fontId, words, hyphenationEnabled, styleMask);
  }

  const int pageWidth = viewportWidth;
  auto wordWidths = calculateWordWidths(renderer, fontId);

  std::vector<size_t> lineBreakIndices;
  if (hyphenationEnabled) {
    // Use greedy layout that can split words mid-loop when a hyphenated prefix fits.
    lineBreakIndices =
        computeHyphenatedLineBreaks(renderer, fontId, pageWidth, wordWidths, wordContinues, wordNoSpaceBefore);
  } else {
    lineBreakIndices = computeLineBreaks(renderer, fontId, pageWidth, wordWidths, wordContinues, wordNoSpaceBefore);
  }
  const size_t lineCount = includeLastLine ? lineBreakIndices.size() : lineBreakIndices.size() - 1;

  for (size_t i = 0; i < lineCount; ++i) {
    extractLine(i, pageWidth, wordWidths, wordContinues, wordNoSpaceBefore, lineBreakIndices, processLine, renderer,
                fontId);
  }

  // Remove consumed words so size() reflects only remaining words
  if (lineCount > 0) {
    firstLineIndentPending = false;
    const size_t consumed = lineBreakIndices[lineCount - 1];
    words.erase(words.begin(), words.begin() + consumed);
    wordStyles.erase(wordStyles.begin(), wordStyles.begin() + consumed);
    wordContinues.erase(wordContinues.begin(), wordContinues.begin() + consumed);
    wordNoSpaceBefore.erase(wordNoSpaceBefore.begin(), wordNoSpaceBefore.begin() + consumed);
    wordFocusBoundary.erase(wordFocusBoundary.begin(), wordFocusBoundary.begin() + consumed);
    wordLayoutFlags.erase(wordLayoutFlags.begin(), wordLayoutFlags.begin() + consumed);
    eraseVisibleOffsetPrefix(consumed);
    if (!rubyTexts.empty()) {
      const size_t rubyConsumed = std::min(consumed, rubyTexts.size());
      rubyTexts.erase(rubyTexts.begin(), rubyTexts.begin() + rubyConsumed);
    }
  }
}

int ParsedText::calculateRubyExtraStartOffset(const size_t wordIdx, const size_t maxWordIdx,
                                              const GfxRenderer& renderer, const int fontId) const {
  if (rubyTexts.empty() || wordIdx >= rubyTexts.size() || rubyTexts[wordIdx].empty() ||
      (wordStyles[wordIdx] & EpdFontFamily::RUBY_CONTINUE) != 0) {
    return 0;
  }

  size_t groupWordCount = 1;
  while (wordIdx + groupWordCount < maxWordIdx &&
         (wordStyles[wordIdx + groupWordCount] & EpdFontFamily::RUBY_CONTINUE) != 0) {
    ++groupWordCount;
  }

  int groupActualWidth = 0;
  for (size_t offset = 0; offset < groupWordCount; ++offset) {
    groupActualWidth += measureWordWidth(renderer, fontId, words[wordIdx + offset], wordStyles[wordIdx + offset]);
  }
  const int rubyWidth = renderer.getTextAdvanceX(fontId, rubyTexts[wordIdx].c_str(), EpdFontFamily::SUP);
  return RubyCjkLayoutUtils::edgeReservation(rubyWidth, groupActualWidth);
}

int ParsedText::calculateRubyExtraEndOffset(const size_t lineStartIdx, const size_t lineBreakIdx,
                                            const GfxRenderer& renderer, const int fontId) const {
  if (rubyTexts.empty() || lineBreakIdx == 0 || lineStartIdx >= lineBreakIdx) return 0;

  size_t leaderIdx = lineBreakIdx - 1;
  while (leaderIdx > lineStartIdx && (wordStyles[leaderIdx] & EpdFontFamily::RUBY_CONTINUE) != 0) {
    --leaderIdx;
  }
  if (leaderIdx >= rubyTexts.size() || rubyTexts[leaderIdx].empty() ||
      (wordStyles[leaderIdx] & EpdFontFamily::RUBY_CONTINUE) != 0) {
    return 0;
  }

  int groupActualWidth = 0;
  for (size_t wordIdx = leaderIdx; wordIdx < lineBreakIdx; ++wordIdx) {
    groupActualWidth += measureWordWidth(renderer, fontId, words[wordIdx], wordStyles[wordIdx]);
  }
  const int rubyWidth = renderer.getTextAdvanceX(fontId, rubyTexts[leaderIdx].c_str(), EpdFontFamily::SUP);
  return RubyCjkLayoutUtils::edgeReservation(rubyWidth, groupActualWidth);
}

std::vector<uint16_t> ParsedText::calculateWordWidths(const GfxRenderer& renderer, const int fontId) {
  std::vector<uint16_t> wordWidths;
  wordWidths.reserve(words.size());

  for (size_t i = 0; i < words.size(); ++i) {
    wordWidths.push_back(measureFocusWordWidth(renderer, fontId, words[i], wordStyles[i], wordFocusBoundary[i]));
  }

  // Apply JLReq-style ruby overhang: annotations can partially use adjacent
  // non-ideograph space, while collisions and CJK neighbours reserve room.
  if (!rubyTexts.empty()) {
    struct RubyGroupInfo {
      size_t start;
      size_t count;
      int leftOverlap;
      int rightOverlap;
    };

    // Long CJK paragraphs can carry many annotations; a deque avoids requiring
    // one large contiguous allocation for this temporary layout index.
    std::deque<RubyGroupInfo> groups;
    for (size_t i = 0; i < words.size(); ++i) {
      if (i < rubyTexts.size() && !rubyTexts[i].empty() && (wordStyles[i] & EpdFontFamily::RUBY_CONTINUE) == 0) {
        size_t count = 1;
        int baseWidth = wordWidths[i];
        while (i + count < words.size() && (wordStyles[i + count] & EpdFontFamily::RUBY_CONTINUE) != 0) {
          baseWidth += wordWidths[i + count];
          ++count;
        }
        const int rubyWidth = renderer.getTextAdvanceX(fontId, rubyTexts[i].c_str(), EpdFontFamily::SUP);
        const int overlap = RubyCjkLayoutUtils::edgeReservation(rubyWidth, baseWidth);
        groups.push_back({i, count, overlap, overlap});
        i += count - 1;
      }
    }

    for (size_t groupIdx = 0; groupIdx < groups.size(); ++groupIdx) {
      const auto& group = groups[groupIdx];

      if (group.start > 0) {
        const uint32_t previous = lastCodepoint(words[group.start - 1]);
        wordWidths[group.start - 1] += RubyCjkLayoutUtils::reservedAdjacentOverlap(
            group.leftOverlap, wordWidths[group.start - 1], isCjkIdeograph(previous));
      }

      const size_t nextIdx = group.start + group.count;
      if (nextIdx >= words.size()) continue;

      if (groupIdx + 1 < groups.size() && groups[groupIdx + 1].start == nextIdx) {
        wordWidths[nextIdx - 1] += group.rightOverlap + groups[groupIdx + 1].leftOverlap;
        continue;
      }

      const uint32_t next = firstCodepoint(words[nextIdx]);
      wordWidths[nextIdx - 1] +=
          RubyCjkLayoutUtils::reservedAdjacentOverlap(group.rightOverlap, wordWidths[nextIdx], isCjkIdeograph(next));

      if (groupIdx + 1 < groups.size()) {
        const auto& nextGroup = groups[groupIdx + 1];
        bool onlyNonIdeographs = true;
        int gapWidth = 0;
        for (size_t wordIdx = nextIdx; wordIdx < nextGroup.start; ++wordIdx) {
          if (isCjkIdeograph(firstCodepoint(words[wordIdx]))) {
            onlyNonIdeographs = false;
            break;
          }
          gapWidth += wordWidths[wordIdx];
        }
        if (onlyNonIdeographs) {
          const int allowedRight = std::min(group.rightOverlap, static_cast<int>(wordWidths[nextIdx - 1]) / 2);
          const int allowedLeft =
              std::min(nextGroup.leftOverlap, static_cast<int>(wordWidths[nextGroup.start - 1]) / 2);
          const int collision = allowedRight + allowedLeft - gapWidth;
          if (collision > 0) wordWidths[nextIdx - 1] += collision;
        }
      }
    }
  }

  return wordWidths;
}

std::vector<size_t> ParsedText::computeLineBreaks(const GfxRenderer& renderer, const int fontId, const int pageWidth,
                                                  std::vector<uint16_t>& wordWidths, std::vector<bool>& continuesVec,
                                                  const std::vector<bool>& noSpaceBeforeVec) {
  if (words.empty()) {
    return {};
  }

  // Calculate first line indent (only for left/justified text).
  // Positive text-indent (paragraph indent) is suppressed when extraParagraphSpacing is on.
  // Negative text-indent (hanging indent, e.g. margin-left:3em; text-indent:-1em) always applies —
  // it is structural (positions the bullet/marker), not decorative.
  const int firstLineIndent =
      firstLineIndentPending && blockStyle.textIndentDefined &&
              (blockStyle.textIndent < 0 || !extraParagraphSpacing || forceParagraphIndents) &&
              (blockStyle.alignment == CssTextAlign::Justify || blockStyle.alignment == CssTextAlign::Left)
          ? blockStyle.textIndent
          : 0;

  // Ensure any word that would overflow even as the first entry on a line is split using fallback hyphenation.
  for (size_t i = 0; i < wordWidths.size(); ++i) {
    // First word needs to fit in reduced width if there's an indent
    const int effectiveWidth = i == 0 ? pageWidth - firstLineIndent : pageWidth;
    while (wordWidths[i] > effectiveWidth) {
      if (!hyphenateWordAtIndex(i, effectiveWidth, renderer, fontId, wordWidths, /*allowFallbackBreaks=*/true)) {
        break;
      }
    }
  }

  const size_t totalWordCount = words.size();

  // DP table to store the minimum badness (cost) of lines starting at index i
  std::vector<int> dp(totalWordCount);
  // 'ans[i]' stores the index 'j' of the *last word* in the optimal line starting at 'i'
  std::vector<size_t> ans(totalWordCount);

  // Base Case
  dp[totalWordCount - 1] = 0;
  ans[totalWordCount - 1] = totalWordCount - 1;

  for (int i = totalWordCount - 2; i >= 0; --i) {
    int currlen = 0;
    dp[i] = MAX_COST;

    // First line has reduced width due to text-indent
    const int effectivePageWidth = i == 0 ? pageWidth - firstLineIndent : pageWidth;

    for (size_t j = i; j < totalWordCount; ++j) {
      // Add space before word j, unless it's the first word on the line or a continuation
      int gap = 0;
      if (j > static_cast<size_t>(i) && continuesVec[j]) {
        gap = renderer.getKerning(fontId, lastCodepoint(words[j - 1]), firstCodepoint(words[j]), wordStyles[j - 1]);
      } else if (j > static_cast<size_t>(i) && noSpaceBeforeVec[j]) {
        gap = 0;
      } else if (j > static_cast<size_t>(i)) {
        gap = naturalWordGap(renderer, fontId, words[j - 1], words[j], wordStyles[j - 1], wordSpacing);
      }
      const int extraStartOffset =
          j == static_cast<size_t>(i) ? calculateRubyExtraStartOffset(i, totalWordCount, renderer, fontId) : 0;
      currlen += wordWidths[j] + gap + extraStartOffset;

      if (currlen > effectivePageWidth) {
        break;
      }

      if (j + 1 < totalWordCount && !TokenBoundary::allowsBreak(continuesVec[j + 1], noSpaceBeforeVec[j + 1])) {
        continue;
      }

      const int extraEndOffset = calculateRubyExtraEndOffset(i, j + 1, renderer, fontId);
      if (currlen + extraEndOffset > effectivePageWidth) continue;

      int cost;
      if (j == totalWordCount - 1) {
        cost = 0;  // Last line
      } else {
        const int remainingSpace = effectivePageWidth - currlen;
        // Use long long for the square to prevent overflow
        const long long cost_ll = static_cast<long long>(remainingSpace) * remainingSpace + dp[j + 1];

        if (cost_ll > MAX_COST) {
          cost = MAX_COST;
        } else {
          cost = static_cast<int>(cost_ll);
        }
      }

      // Prefer the longer line when two breakpoints have equal cost. This
      // avoids unnecessarily short lines in Chinese and Japanese text.
      if (RubyCjkLayoutUtils::preferLineBreakCandidate(cost, dp[i])) {
        dp[i] = cost;
        ans[i] = j;  // j is the index of the last word in this optimal line
      }
    }

    // Handle oversized word: if no valid configuration found, force single-word line
    // This prevents cascade failure where one oversized word breaks all preceding words
    if (dp[i] == MAX_COST) {
      ans[i] = i;  // Just this word on its own line
      // Inherit cost from next word to allow subsequent words to find valid configurations
      if (i + 1 < static_cast<int>(totalWordCount)) {
        dp[i] = dp[i + 1];
      } else {
        dp[i] = 0;
      }
    }
  }

  // Stores the index of the word that starts the next line (last_word_index + 1)
  std::vector<size_t> lineBreakIndices;
  size_t currentWordIndex = 0;

  while (currentWordIndex < totalWordCount) {
    size_t nextBreakIndex = ans[currentWordIndex] + 1;

    // Safety check: prevent infinite loop if nextBreakIndex doesn't advance
    if (nextBreakIndex <= currentWordIndex) {
      // Force advance by at least one word to avoid infinite loop
      nextBreakIndex = currentWordIndex + 1;
    }

    lineBreakIndices.push_back(nextBreakIndex);
    currentWordIndex = nextBreakIndex;
  }

  return lineBreakIndices;
}

void ParsedText::prepareParagraphIndent(const GfxRenderer& renderer, const int fontId) {
  if ((extraParagraphSpacing && !forceParagraphIndents) || words.empty()) {
    return;
  }

  if (blockStyle.textIndentDefined) {
    // CSS text-indent is explicitly set (even if 0). The actual indent
    // positioning is handled in extractLine().
  } else if (blockStyle.alignment == CssTextAlign::Justify || blockStyle.alignment == CssTextAlign::Left) {
    // No CSS text-indent defined. Use a pixel offset instead of inserting an
    // em-space glyph, because SD card fonts may not contain U+2003.
    int indent =
        renderer.isSdCardFont(fontId) ? 0 : renderer.getTextAdvanceX(fontId, "\xe2\x80\x83", wordStyles.front());
    if (indent <= 0) {
      indent = renderer.getFontAscenderSize(fontId);
    }
    if (indent > 0) {
      blockStyle.textIndent = static_cast<int16_t>(std::min(indent, static_cast<int>(INT16_MAX)));
      blockStyle.textIndentDefined = true;
    }
  }
}

// Builds break indices while opportunistically splitting the word that would overflow the current line.
std::vector<size_t> ParsedText::computeHyphenatedLineBreaks(const GfxRenderer& renderer, const int fontId,
                                                            const int pageWidth, std::vector<uint16_t>& wordWidths,
                                                            std::vector<bool>& continuesVec,
                                                            const std::vector<bool>& noSpaceBeforeVec) {
  // Calculate first line indent (only for left/justified text).
  // Positive text-indent (paragraph indent) is suppressed when extraParagraphSpacing is on.
  // Negative text-indent (hanging indent, e.g. margin-left:3em; text-indent:-1em) always applies —
  // it is structural (positions the bullet/marker), not decorative.
  const int firstLineIndent =
      firstLineIndentPending && blockStyle.textIndentDefined &&
              (blockStyle.textIndent < 0 || !extraParagraphSpacing || forceParagraphIndents) &&
              (blockStyle.alignment == CssTextAlign::Justify || blockStyle.alignment == CssTextAlign::Left)
          ? blockStyle.textIndent
          : 0;

  std::vector<size_t> lineBreakIndices;
  size_t currentIndex = 0;
  bool isFirstLine = true;

  while (currentIndex < wordWidths.size()) {
    const size_t lineStart = currentIndex;
    int lineWidth = 0;

    // First line has reduced width due to text-indent
    const int effectivePageWidth = isFirstLine ? pageWidth - firstLineIndent : pageWidth;

    // Consume as many words as possible for current line, splitting when prefixes fit
    while (currentIndex < wordWidths.size()) {
      const bool isFirstWord = currentIndex == lineStart;
      int spacing = 0;
      if (!isFirstWord && continuesVec[currentIndex]) {
        spacing = renderer.getKerning(fontId, lastCodepoint(words[currentIndex - 1]),
                                      firstCodepoint(words[currentIndex]), wordStyles[currentIndex - 1]);
      } else if (!isFirstWord && noSpaceBeforeVec[currentIndex]) {
        spacing = 0;
      } else if (!isFirstWord) {
        spacing = naturalWordGap(renderer, fontId, words[currentIndex - 1], words[currentIndex],
                                 wordStyles[currentIndex - 1], wordSpacing);
      }
      const int candidateWidth = spacing + wordWidths[currentIndex];

      // Word fits on current line
      if (lineWidth + candidateWidth <= effectivePageWidth) {
        lineWidth += candidateWidth;
        ++currentIndex;
        continue;
      }

      // Word would overflow — try to split based on hyphenation points
      const int availableWidth = effectivePageWidth - lineWidth - spacing;
      const bool allowFallbackBreaks = isFirstWord;  // Only for first word on line

      if (availableWidth > 0 &&
          hyphenateWordAtIndex(currentIndex, availableWidth, renderer, fontId, wordWidths, allowFallbackBreaks)) {
        // Prefix now fits; append it to this line and move to next line
        lineWidth += spacing + wordWidths[currentIndex];
        ++currentIndex;
        break;
      }

      // Could not split: force at least one word per line to avoid infinite loop
      if (currentIndex == lineStart) {
        lineWidth += candidateWidth;
        ++currentIndex;
      }
      break;
    }

    // Don't break before a continuation word (e.g., orphaned "?" after "question").
    // Backtrack to the start of the continuation group so the whole group moves to the next line.
    while (currentIndex > lineStart + 1 && currentIndex < wordWidths.size() &&
           !TokenBoundary::allowsBreak(continuesVec[currentIndex], noSpaceBeforeVec[currentIndex])) {
      --currentIndex;
    }

    lineBreakIndices.push_back(currentIndex);
    isFirstLine = false;
  }

  return lineBreakIndices;
}

// Splits words[wordIndex] into prefix (adding a hyphen only when needed) and remainder when a legal breakpoint fits the
// available width.
bool ParsedText::hyphenateWordAtIndex(const size_t wordIndex, const int availableWidth, const GfxRenderer& renderer,
                                      const int fontId, std::vector<uint16_t>& wordWidths,
                                      const bool allowFallbackBreaks) {
  // Guard against invalid indices or zero available width before attempting to split.
  if (availableWidth <= 0 || wordIndex >= words.size()) {
    return false;
  }

  const std::string& word = words[wordIndex];
  const auto style = wordStyles[wordIndex];
  const uint8_t focusBoundary = wordFocusBoundary[wordIndex];

  // Collect candidate breakpoints (byte offsets and hyphen requirements).
  auto breakInfos = Hyphenator::breakOffsets(word, allowFallbackBreaks);
  if (breakInfos.empty()) {
    return false;
  }

  size_t chosenOffset = 0;
  int chosenWidth = -1;
  bool chosenNeedsHyphen = true;

  // Iterate over each legal breakpoint and retain the widest prefix that still fits.
  for (const auto& info : breakInfos) {
    const size_t offset = info.byteOffset;
    if (offset == 0 || offset >= word.size()) {
      continue;
    }

    const bool needsHyphen = info.requiresInsertedHyphen;
    const int prefixWidth =
        measureFocusWordWidth(renderer, fontId, word.substr(0, offset), style,
                              TokenBoundary::focusBoundaryBefore(focusBoundary, offset), needsHyphen);
    if (prefixWidth > availableWidth || prefixWidth <= chosenWidth) {
      continue;  // Skip if too wide or not an improvement
    }

    chosenWidth = prefixWidth;
    chosenOffset = offset;
    chosenNeedsHyphen = needsHyphen;
  }

  if (chosenWidth < 0) {
    // No hyphenation point produced a prefix that fits in the remaining space.
    return false;
  }

  // Split the word at the selected breakpoint and append a hyphen if required.
  uint32_t remainderOffset = visibleOffsetAt(wordIndex);
  const unsigned char* offsetPtr = reinterpret_cast<const unsigned char*>(word.data());
  const unsigned char* const splitPtr = offsetPtr + chosenOffset;
  while (offsetPtr < splitPtr) {
    utf8NextCodepoint(&offsetPtr);
    ++remainderOffset;
  }
  std::string remainder = word.substr(chosenOffset);
  words[wordIndex].resize(chosenOffset);
  if (chosenNeedsHyphen) {
    words[wordIndex].push_back('-');
    wordLayoutFlags[wordIndex] = TextBlock::WORD_FLAG_INSERTED_HYPHEN;
  }

  // Insert the remainder word (with matching style and continuation flag) directly after the prefix.
  words.insert(words.begin() + wordIndex + 1, remainder);
  wordStyles.insert(wordStyles.begin() + wordIndex + 1, style);
  insertVisibleOffset(wordIndex + 1, remainderOffset);
  wordFocusBoundary.insert(wordFocusBoundary.begin() + wordIndex + 1,
                           TokenBoundary::focusBoundaryAfter(focusBoundary, chosenOffset));
  wordLayoutFlags.insert(wordLayoutFlags.begin() + wordIndex + 1, 0);
  wordFocusBoundary[wordIndex] = TokenBoundary::focusBoundaryBefore(focusBoundary, chosenOffset);
  if (wordFocusBoundary[wordIndex] >= words[wordIndex].size()) {
    wordStyles[wordIndex] = static_cast<EpdFontFamily::Style>(wordStyles[wordIndex] | EpdFontFamily::BOLD);
    wordFocusBoundary[wordIndex] = 0;
  }
  if (!rubyTexts.empty() && wordIndex + 1 <= rubyTexts.size()) {
    rubyTexts.insert(rubyTexts.begin() + wordIndex + 1, "");
  }

  // Continuation flag handling after splitting a word into prefix + remainder.
  //
  // The prefix keeps the original word's continuation flag so that no-break-space groups
  // stay linked. The remainder always gets continues=false because it starts on the next
  // line and is not attached to the prefix.
  //
  // Example: "200&#xA0;Quadratkilometer" produces tokens:
  //   [0] "200"               continues=false
  //   [1] " "                 continues=true
  //   [2] "Quadratkilometer"  continues=true   <-- the word being split
  //
  // After splitting "Quadratkilometer" at "Quadrat-" / "kilometer":
  //   [0] "200"         continues=false
  //   [1] " "           continues=true
  //   [2] "Quadrat-"    continues=true   (KEPT — still attached to the no-break group)
  //   [3] "kilometer"   continues=false  (NEW — starts fresh on the next line)
  //
  // This lets the backtracking loop keep the entire prefix group ("200 Quadrat-") on one
  // line, while "kilometer" moves to the next line.
  // wordContinues[wordIndex] is intentionally left unchanged — the prefix keeps its original attachment.
  wordContinues.insert(wordContinues.begin() + wordIndex + 1, false);
  wordNoSpaceBefore.insert(wordNoSpaceBefore.begin() + wordIndex + 1, false);

  // Update cached widths to reflect the new prefix/remainder pairing.
  wordWidths[wordIndex] = static_cast<uint16_t>(chosenWidth);
  const uint16_t remainderWidth =
      measureFocusWordWidth(renderer, fontId, remainder, style, wordFocusBoundary[wordIndex + 1]);
  wordWidths.insert(wordWidths.begin() + wordIndex + 1, remainderWidth);
  return true;
}

void ParsedText::extractLine(const size_t breakIndex, const int pageWidth, const std::vector<uint16_t>& wordWidths,
                             const std::vector<bool>& continuesVec, const std::vector<bool>& noSpaceBeforeVec,
                             const std::vector<size_t>& lineBreakIndices,
                             const std::function<void(std::shared_ptr<TextBlock>, uint32_t)>& processLine,
                             const GfxRenderer& renderer, const int fontId) {
  const size_t lineBreak = lineBreakIndices[breakIndex];
  const size_t lastBreakAt = breakIndex > 0 ? lineBreakIndices[breakIndex - 1] : 0;
  const size_t lineWordCount = lineBreak - lastBreakAt;
  const uint32_t lineVisibleOffset = visibleOffsetAt(lastBreakAt);

  // Calculate first line indent (only for left/justified text).
  // Positive text-indent (paragraph indent) is suppressed when extraParagraphSpacing is on.
  // Negative text-indent (hanging indent, e.g. margin-left:3em; text-indent:-1em) always applies —
  // it is structural (positions the bullet/marker), not decorative.
  const bool isFirstLine = firstLineIndentPending && breakIndex == 0;
  const int firstLineIndent =
      isFirstLine && blockStyle.textIndentDefined &&
              (blockStyle.textIndent < 0 || !extraParagraphSpacing || forceParagraphIndents) &&
              (blockStyle.alignment == CssTextAlign::Justify || blockStyle.alignment == CssTextAlign::Left)
          ? blockStyle.textIndent
          : 0;

  // Calculate total word width for this line, count actual word gaps,
  // and accumulate total natural gap widths (including space kerning adjustments).
  int lineWordWidthSum = 0;
  size_t actualGapCount = 0;
  int totalNaturalGaps = 0;

  for (size_t wordIdx = 0; wordIdx < lineWordCount; wordIdx++) {
    lineWordWidthSum += wordWidths[lastBreakAt + wordIdx];
    if (wordIdx == 0) continue;
    const size_t boundaryIdx = lastBreakAt + wordIdx;
    const bool isSpaceToken = words[boundaryIdx] == " ";
    if (TokenBoundary::isJustifiableGap(continuesVec[boundaryIdx], noSpaceBeforeVec[boundaryIdx], isSpaceToken)) {
      actualGapCount++;
    }
    if (continuesVec[boundaryIdx]) {
      totalNaturalGaps += renderer.getKerning(fontId, lastCodepoint(words[boundaryIdx - 1]),
                                              firstCodepoint(words[boundaryIdx]), wordStyles[boundaryIdx - 1]);
    } else if (!noSpaceBeforeVec[boundaryIdx]) {
      totalNaturalGaps += naturalWordGap(renderer, fontId, words[boundaryIdx - 1], words[boundaryIdx],
                                         wordStyles[boundaryIdx - 1], wordSpacing);
    }
  }

  // Calculate spacing (account for indent reducing effective page width on first line)
  const int effectivePageWidth = pageWidth - firstLineIndent;
  const bool isLastLine = breakIndex == lineBreakIndices.size() - 1;

  const int extraStartOffset = calculateRubyExtraStartOffset(lastBreakAt, lineBreak, renderer, fontId);
  const int extraEndOffset = calculateRubyExtraEndOffset(lastBreakAt, lineBreak, renderer, fontId);

  // Keep both edge overhangs out of justification so the annotation remains
  // centered without crossing either page margin.
  const int spareSpace = effectivePageWidth - extraStartOffset - extraEndOffset - lineWordWidthSum - totalNaturalGaps;
  const int justifyExtra = (blockStyle.alignment == CssTextAlign::Justify && !isLastLine && actualGapCount >= 1)
                               ? spareSpace / static_cast<int>(actualGapCount)
                               : 0;

  // Calculate initial x position (first line starts at indent for left/justified text;
  // may be negative for hanging indents, e.g. margin-left:3em; text-indent:-1em).
  auto xpos = static_cast<int16_t>(firstLineIndent + extraStartOffset);
  if (blockStyle.alignment == CssTextAlign::Right) {
    xpos = effectivePageWidth - extraEndOffset - lineWordWidthSum - totalNaturalGaps;
  } else if (blockStyle.alignment == CssTextAlign::Center) {
    xpos = (effectivePageWidth - lineWordWidthSum - totalNaturalGaps + extraStartOffset - extraEndOffset) / 2;
  }

  // Pre-calculate X positions for words
  // Continuation words attach to the previous word with no space before them
  std::vector<int16_t> lineXPos;
  lineXPos.reserve(lineWordCount);

  for (size_t wordIdx = 0; wordIdx < lineWordCount; wordIdx++) {
    lineXPos.push_back(xpos);

    const size_t nextBoundaryIdx = lastBreakAt + wordIdx + 1;
    const bool nextIsContinuation = wordIdx + 1 < lineWordCount && continuesVec[nextBoundaryIdx];
    if (nextIsContinuation) {
      int advance = wordWidths[lastBreakAt + wordIdx];
      // Cross-boundary kerning for continuation words (e.g. nonbreaking spaces, attached punctuation)
      advance +=
          renderer.getKerning(fontId, lastCodepoint(words[lastBreakAt + wordIdx]),
                              firstCodepoint(words[lastBreakAt + wordIdx + 1]), wordStyles[lastBreakAt + wordIdx]);
      // Non-breaking space tokens are stretchable — expand them during justification like normal spaces.
      // Gap accounting skips index 0, so a leading no-break space must not
      // receive justifyExtra or it pushes the last word beyond the margin.
      if (wordIdx > 0 && words[lastBreakAt + wordIdx] == " " && continuesVec[lastBreakAt + wordIdx] &&
          blockStyle.alignment == CssTextAlign::Justify && !isLastLine) {
        advance += justifyExtra;
      }
      xpos += advance;
    } else {
      int gap = 0;
      if (wordIdx + 1 < lineWordCount) {
        if (!noSpaceBeforeVec[nextBoundaryIdx]) {
          gap = naturalWordGap(renderer, fontId, words[lastBreakAt + wordIdx], words[nextBoundaryIdx],
                               wordStyles[lastBreakAt + wordIdx], wordSpacing);
        }
      }
      if (blockStyle.alignment == CssTextAlign::Justify && !isLastLine) {
        gap += justifyExtra;
      }
      xpos += wordWidths[lastBreakAt + wordIdx] + gap;
    }
  }

  // Build line data by moving from the original vectors using index range
  std::vector<std::string> lineWords(std::make_move_iterator(words.begin() + lastBreakAt),
                                     std::make_move_iterator(words.begin() + lineBreak));
  std::vector<EpdFontFamily::Style> lineWordStyles(wordStyles.begin() + lastBreakAt, wordStyles.begin() + lineBreak);
  std::vector<uint8_t> lineLayoutFlags(wordLayoutFlags.begin() + lastBreakAt, wordLayoutFlags.begin() + lineBreak);
  std::vector<std::string> lineRubyTexts(lineWordCount);
  if (!rubyTexts.empty() && lastBreakAt < rubyTexts.size()) {
    const size_t copyCount = std::min(lineBreak, rubyTexts.size()) - lastBreakAt;
    std::copy(rubyTexts.begin() + lastBreakAt, rubyTexts.begin() + lastBreakAt + copyCount, lineRubyTexts.begin());
  }

  for (auto& word : lineWords) {
    if (containsSoftHyphen(word)) {
      stripSoftHyphensInPlace(word);
    }
  }

  // Fast path: TextBlock pays no per-word focus annotation cost for ordinary lines.
  bool lineHasFocusSplit = false;
  for (size_t i = 0; i < lineWordCount; i++) {
    if (wordFocusBoundary[lastBreakAt + i] != 0) {
      lineHasFocusSplit = true;
      break;
    }
  }

  if (!lineHasFocusSplit) {
    auto block = std::shared_ptr<TextBlock>(
        new (std::nothrow) TextBlock(lineWords, lineXPos, lineWordStyles, std::vector<uint8_t>{},
                                     std::vector<uint16_t>{}, lineLayoutFlags, blockStyle, lineRubyTexts));
    processLine(block && block->valid() ? std::move(block) : nullptr, lineVisibleOffset);
    return;
  }

  // Words already remain whole; build only the compact render annotations.
  std::vector<uint8_t> outBoundaries;
  std::vector<uint16_t> outSuffixX;
  outBoundaries.reserve(lineWordCount);
  outSuffixX.reserve(lineWordCount);

  for (size_t i = 0; i < lineWordCount; i++) {
    const uint8_t boundary = wordFocusBoundary[lastBreakAt + i];
    outBoundaries.push_back(boundary);
    outSuffixX.push_back(
        boundary == 0 ? 0 : measureFocusPrefixAdvance(renderer, fontId, lineWords[i], lineWordStyles[i], boundary));
  }

  auto block = std::shared_ptr<TextBlock>(new (std::nothrow) TextBlock(
      lineWords, lineXPos, lineWordStyles, outBoundaries, outSuffixX, lineLayoutFlags, blockStyle, lineRubyTexts));
  processLine(block && block->valid() ? std::move(block) : nullptr, lineVisibleOffset);
}
