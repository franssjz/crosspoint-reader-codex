#pragma once

#include <EpdFontFamily.h>

class FontCacheManager {
 public:
  void recordText(const char*, int, EpdFontFamily::Style) {}
  void recordStyle(int, EpdFontFamily::Style) {}
};
