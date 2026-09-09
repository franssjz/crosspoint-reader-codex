#pragma once
#include <EpdFontFamily.h>

#include <cstring>
#include <deque>
#include <string>

class GfxRenderer {
 public:
  bool isFontCacheScanning() const { return false; }
  bool isSdCardFont(int) const { return false; }
  bool releaseSdCardFontForLowMemory(int) { return false; }
  int ensureSdCardFontReady(int, const std::deque<std::string>&, bool, uint8_t) const { return 0; }
  int getFontAscenderSize(int) const { return 20; }
  int getLineHeight(int) const { return 24; }
  int getScreenWidth() const { return 800; }
  int getSpaceWidth(int, EpdFontFamily::Style) const { return 4; }
  int getKerning(int, uint32_t, uint32_t, EpdFontFamily::Style) const { return 0; }
  int getSpaceAdvance(int, uint32_t, uint32_t, EpdFontFamily::Style) const { return 4; }
  int getTextAdvanceX(int, const char* text, EpdFontFamily::Style) const {
    return text ? static_cast<int>(std::strlen(text)) * 4 : 0;
  }
  int getTextWidth(int, const char* text, EpdFontFamily::Style = EpdFontFamily::REGULAR) const {
    return text ? static_cast<int>(std::strlen(text)) * 4 : 0;
  }
  void drawText(int, int, int, const char*, bool = true, EpdFontFamily::Style = EpdFontFamily::REGULAR) const {}
  void drawLine(int, int, int, int, int, bool) const {}
};
