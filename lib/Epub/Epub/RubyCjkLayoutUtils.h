#pragma once

#include <algorithm>

namespace RubyCjkLayoutUtils {

inline int edgeReservation(const int rubyWidth, const int baseWidth) {
  return rubyWidth > baseWidth ? (rubyWidth - baseWidth) / 2 : 0;
}

inline int reservedAdjacentOverlap(const int overlap, const int adjacentWidth, const bool adjacentIsCjk) {
  return adjacentIsCjk ? overlap : std::max(0, overlap - adjacentWidth / 2);
}

inline bool preferLineBreakCandidate(const int candidateCost, const int bestCost) {
  return candidateCost <= bestCost;
}

}  // namespace RubyCjkLayoutUtils
