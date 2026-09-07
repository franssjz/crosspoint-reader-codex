#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "hyphenation/HyphenationCommon.h"

// ParsedText stores each boundary in two bit-packed vectors. Together the
// flags distinguish ordinary gaps, unbreakable attachments, and breakable
// zero-width attachments after visible hyphens/dashes.
namespace TokenBoundary {

constexpr bool allowsBreak(const bool continues, const bool noSpaceBefore) {
  return !continues || noSpaceBefore;
}

constexpr bool isJustifiableGap(const bool continues, const bool noSpaceBefore, const bool isSpaceToken) {
  return !continues || (isSpaceToken && !noSpaceBefore);
}

inline bool allowsBreakAfterExplicitHyphen(const uint32_t cp) {
  constexpr uint32_t NON_BREAKING_HYPHEN_CP = 0x2011;
  return isExplicitHyphen(cp) && !isSoftHyphen(cp) && cp != NON_BREAKING_HYPHEN_CP;
}

constexpr uint8_t focusBoundaryBefore(const uint8_t boundary, const size_t splitOffset) {
  return static_cast<uint8_t>(std::min<size_t>(boundary, splitOffset));
}

constexpr uint8_t focusBoundaryAfter(const uint8_t boundary, const size_t splitOffset) {
  return boundary > splitOffset ? static_cast<uint8_t>(boundary - splitOffset) : 0;
}

}  // namespace TokenBoundary
