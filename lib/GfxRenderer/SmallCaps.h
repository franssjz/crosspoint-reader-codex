#pragma once

#include <EpdFontFamily.h>

#include <cstdint>

namespace smallcaps {

inline bool isLowercase(const EpdFontFamily::Style style, const uint32_t cp) {
  if ((style & EpdFontFamily::SMALL_CAPS) == 0) return false;
  if (cp >= 'a' && cp <= 'z') return true;
  if ((cp >= 0x00E0 && cp <= 0x00F6) || (cp >= 0x00F8 && cp <= 0x00FE) || cp == 0x00FF) return true;
  if (cp >= 0x0101 && cp <= 0x0177 && (cp & 1) != 0) return true;
  if (cp == 0x017A || cp == 0x017C || cp == 0x017E) return true;
  if (cp >= 0x03B1 && cp <= 0x03C9) return true;
  return (cp >= 0x0430 && cp <= 0x045F) || cp == 0x0491;
}

inline uint32_t toUppercase(const uint32_t cp) {
  if (cp >= 'a' && cp <= 'z') return cp - ('a' - 'A');
  if (cp == 0x00FF) return 0x0178;
  if (cp >= 0x00E0 && cp <= 0x00FE) return cp - 0x20;
  if (cp >= 0x0101 && cp <= 0x0177) return cp - 1;
  if (cp == 0x017A || cp == 0x017C || cp == 0x017E) return cp - 1;
  if (cp == 0x03C2) return 0x03A3;
  if (cp >= 0x03B1 && cp <= 0x03C9) return cp - 0x20;
  if (cp == 0x0491) return 0x0490;
  if (cp >= 0x0450 && cp <= 0x045F) return cp - 0x50;
  return cp - 0x20;
}

inline int32_t scaleAdvance(const int32_t value) { return value >= 0 ? (value * 3 + 2) / 4 : (value * 3 - 2) / 4; }

}  // namespace smallcaps
