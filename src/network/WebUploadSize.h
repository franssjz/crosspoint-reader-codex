#pragma once

#include <limits>
#include <string_view>
#include <type_traits>

namespace WebUploadSize {
// Preserve the existing optional '+' syntax, while rejecting overflow before
// the multiply. Arduino String::toInt() is signed and cannot represent a large
// valid size_t upload, nor distinguish numeric overflow from a valid result.
template <class Unsigned>
bool parse(std::string_view text, Unsigned& output) {
  static_assert(std::is_integral_v<Unsigned> && std::is_unsigned_v<Unsigned>);
  if (!text.empty() && text.front() == '+') text.remove_prefix(1);
  if (text.empty()) return false;
  Unsigned value = 0;
  for (char c : text) {
    if (c < '0' || c > '9') return false;
    const Unsigned digit = static_cast<Unsigned>(c - '0');
    if (value > (std::numeric_limits<Unsigned>::max() - digit) / 10) return false;
    value = value * 10 + digit;
  }
  output = value;
  return true;
}
}  // namespace WebUploadSize
