#pragma once
#include <EpdFontData.h>

#include <string>
#include <vector>

class FontDecompressor {
 public:
  struct Call {
    const EpdFontData* font;
    std::string text;
  };
  std::vector<Call> calls;
  int result = 0;
  int clears = 0;
  void clearCache() { ++clears; }
  int prewarmCache(const EpdFontData* font, const char* text) {
    calls.push_back({font, text});
    return result;
  }
  void logStats(const char*) {}
  void resetStats() {}
};
