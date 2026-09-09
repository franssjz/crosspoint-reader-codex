#pragma once

#include <cstddef>
#include <cstdint>

namespace xtc {

// XTH gray levels are not a linear two-bit ramp.
constexpr uint8_t xtchGray(const uint8_t pixel) {
  constexpr uint8_t values[] = {255, 85, 170, 0};
  return values[pixel & 3];
}

constexpr uint8_t xtchBmpIndex(const uint8_t pixel) {
  constexpr uint8_t values[] = {3, 1, 2, 0};
  return values[pixel & 3];
}

enum class XtchRenderPass : uint8_t { Base, Lsb, Msb };

// A set mask bit is black on Base, white on the gray-effect planes.
constexpr uint8_t xtchPassMask(const uint8_t first, const uint8_t second, const XtchRenderPass pass) {
  switch (pass) {
    case XtchRenderPass::Base:
      return first | second;
    case XtchRenderPass::Lsb:
      return static_cast<uint8_t>(~first) & second;
    case XtchRenderPass::Msb:
      return first ^ second;
  }
  return 0;
}

// Check the storage layout before any division, buffer sizing, or pixel read.
// XTCH columns each contain ceil(height / 8) bytes, including bottom padding.
inline bool bitmapPayloadSize(const uint16_t width, const uint16_t height, const uint8_t bitDepth, size_t& size) {
  if (width == 0 || height == 0 || (bitDepth != 1 && bitDepth != 2)) return false;
  const uint64_t bytes = bitDepth == 2 ? static_cast<uint64_t>(width) * ((height + 7U) / 8U) * 2U
                                       : static_cast<uint64_t>((width + 7U) / 8U) * height;
  if (bytes > SIZE_MAX) return false;
  size = static_cast<size_t>(bytes);
  return true;
}

}  // namespace xtc
