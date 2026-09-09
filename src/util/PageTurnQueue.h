#pragma once
#include <array>
#include <cstddef>

// Main-loop owned. Keep individual directions, not a net delta: forward/back
// across a chapter boundary cannot safely be combined arithmetically.
class PageTurnQueue {
  std::array<bool, 8> turns{};
  size_t begin = 0;
  size_t count = 0;

 public:
  bool push(bool forward) {
    if (count && turns[(begin + count - 1) % turns.size()] != forward) {
      // A reversal expresses the user's latest intent. Discard the stale burst
      // so it cannot cross a chapter boundary before the new direction runs.
      clear();
    }
    if (count == turns.size()) return false;
    turns[(begin + count++) % turns.size()] = forward;
    return true;
  }
  bool pop(bool& forward) {
    if (!count) return false;
    forward = turns[begin];
    begin = (begin + 1) % turns.size();
    --count;
    return true;
  }
  bool empty() const { return count == 0; }
  void clear() { begin = count = 0; }
};
