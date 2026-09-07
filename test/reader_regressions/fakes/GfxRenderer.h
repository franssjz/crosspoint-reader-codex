#pragma once

#include <EpdFontFamily.h>

#include <cstring>

class GfxRenderer {
 public:
  bool isFontCacheScanning() const { return false; }
  int getFontAscenderSize(int) const { return 20; }
  int getScreenWidth() const { return 800; }
  int getTextAdvanceX(int, const char* text, EpdFontFamily::Style) const {
    return text ? static_cast<int>(std::strlen(text)) : 0;
  }
  int getTextWidth(int, const char* text, EpdFontFamily::Style = EpdFontFamily::REGULAR) const {
    return text ? static_cast<int>(std::strlen(text)) : 0;
  }
  void drawText(int, int, int, const char*, bool = true,
                EpdFontFamily::Style = EpdFontFamily::REGULAR) const {}
  void drawLine(int, int, int, int, int, bool) const {}
};
