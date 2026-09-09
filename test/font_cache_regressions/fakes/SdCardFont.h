#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

class SdCardFont {
 public:
  struct Call {
    std::string text;
    uint8_t styles;
  };
  std::vector<Call> calls;
  std::array<uint8_t, 4> resolved = {0, 1, 2, 3};
  int result = 0;
  int clears = 0;
  void clearCache() { ++clears; }
  int prewarm(const char* text, uint8_t styles) {
    calls.push_back({text, styles});
    return result;
  }
  uint8_t resolveStyle(uint8_t style) const { return resolved[style]; }
  void logStats(const char*) {}
  void resetStats() {}
};
