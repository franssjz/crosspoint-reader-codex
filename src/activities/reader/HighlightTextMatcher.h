#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace HighlightTextMatcher {

enum class TokenFragmentMatch : uint8_t {
  MISMATCH,
  CONTINUES_TOKEN,
  COMPLETES_TOKEN,
};

struct TokenFragmentResult {
  TokenFragmentMatch match = TokenFragmentMatch::MISMATCH;
  size_t tokenBytes = 0;
};

inline bool isNonBreakingSpace(const char* text, const size_t length, const size_t offset) {
  return offset + 1 < length && static_cast<unsigned char>(text[offset]) == 0xC2 &&
         static_cast<unsigned char>(text[offset + 1]) == 0xA0;
}

inline bool isUtf8Ellipsis(const char* text) {
  return text && static_cast<unsigned char>(text[0]) == 0xE2 &&
         static_cast<unsigned char>(text[1]) == 0x80 && static_cast<unsigned char>(text[2]) == 0xA6;
}

inline bool nextToken(const char*& cursor, const char*& start, size_t& length) {
  while (*cursor && std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
  if (!*cursor) return false;
  start = cursor;
  while (*cursor) {
    if (!std::isspace(static_cast<unsigned char>(*cursor))) {
      ++cursor;
      continue;
    }
    // Layout can split "word …" into adjacent rendered fragments where the
    // second starts with NBSP. Preserve that separator inside this token.
    if (*cursor == ' ' && isUtf8Ellipsis(cursor + 1)) {
      cursor += 4;
      continue;
    }
    break;
  }
  length = static_cast<size_t>(cursor - start);
  return length > 0;
}

// Match one rendered fragment against a persisted highlight token. Reflow may
// insert a hyphen or split punctuation into adjacent display fragments; NBSP
// is treated as equivalent to the ordinary space stored in the snippet.
inline TokenFragmentResult matchTokenFragment(const char* word, const bool endsWithInsertedHyphen, const char* token,
                                              const size_t tokenLength, const size_t tokenOffset) {
  if (!word || !token || tokenLength == 0 || tokenOffset >= tokenLength) return {};

  size_t wordLength = std::strlen(word);
  if (endsWithInsertedHyphen) {
    if (wordLength == 0 || word[wordLength - 1] != '-') return {};
    --wordLength;
  }

  size_t wordOffset = 0;
  size_t matchedTokenBytes = 0;
  while (wordOffset < wordLength && tokenOffset + matchedTokenBytes < tokenLength) {
    if (isNonBreakingSpace(word, wordLength, wordOffset) && token[tokenOffset + matchedTokenBytes] == ' ') {
      wordOffset += 2;
      ++matchedTokenBytes;
      continue;
    }
    if (word[wordOffset] != token[tokenOffset + matchedTokenBytes]) return {};
    ++wordOffset;
    ++matchedTokenBytes;
  }
  if (wordOffset != wordLength) return {};

  const bool completesToken = tokenOffset + matchedTokenBytes == tokenLength;
  if (completesToken && endsWithInsertedHyphen) return {};
  return {completesToken ? TokenFragmentMatch::COMPLETES_TOKEN : TokenFragmentMatch::CONTINUES_TOKEN,
          matchedTokenBytes};
}

}  // namespace HighlightTextMatcher
