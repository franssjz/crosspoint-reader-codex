#pragma once

#include <cstdint>

// Bit polarity for the optimized rectangle fills in GfxRenderer.
//
// The BW framebuffer stores 1 = white, 0 = black, and fills start from a
// 0xFF (white) background. The GRAY2 factory-LUT planes invert that: each
// plane starts cleared and a set bit *marks* the pixel for that plane, so a
// "black" fill sets bits where BW clears them.
//
// These live in their own header, free of Arduino/HAL includes, so the
// polarity rules can be unit-tested without standing up a GfxRenderer.

// True when the fill should OR bits into the target, false when it should
// clear them. `fillBlack` is the colour intent after any dark-mode inversion.
constexpr bool fillSetsBits(const bool fillBlack, const bool gray2) { return gray2 ? fillBlack : !fillBlack; }

// Row pattern for a dithered (non-solid) fill. `blackMask` has a set bit for
// every pixel the dither pattern wants black. A dithered fill is a pure
// black/white pattern, so in GRAY2 both planes receive this same mask —
// marked pixels resolve to black, unmarked to white, which is what
// drawPixelDither() produces one pixel at a time.
constexpr uint8_t ditherPatternMask(const uint8_t blackMask, const bool gray2) {
  return gray2 ? blackMask : static_cast<uint8_t>(~blackMask);
}
