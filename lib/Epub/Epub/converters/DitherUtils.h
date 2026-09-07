#pragma once

#include <stdint.h>

// 4x4 Bayer matrix for ordered dithering
inline const uint8_t bayer4x4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

// Apply Bayer dithering and quantize to 4 levels (0-3)
// Stateless - works correctly with any pixel processing order
//
// Thresholds are calibrated on hardware for the factory absolute LUT
// (ported from the zgredex fork, commit 999793cd), which EPUB image pages
// render on:
// - Soft-shoulder darkening: factory palette levels are physically lighter
//   than the differential LUT's. A -12 offset on mid-bright pixels onward
//   pulls highlights/midtones back down without crushing shadow detail.
//   The offset ramps 0..12 across gray [0, 64], flat -12 above 64.
// - T12 raised 128 -> 150 so mid-bright source pixels (sRGB 150-170) land in
//   the palette 1/2 dither zone, giving ~50% perceived reflectance via 57/43
//   mixing — the perceptual mid-gray the factory LUT can't reach with
//   palette 2 alone (~70% reflectance).
// - T23 raised 192 -> 210 to keep mid-bright pixels (sRGB 180-210) from
//   blowing out to pure white after the soft-shoulder offset.
// - Highlight guard above 242 (same threshold YACP uses): without it the flat
//   -12 offset drops pure white to 243, which the Bayer dither then pushes
//   under T23 at matrix cells 0 and 1 — rendering 12.5% of white pixels as
//   light gray (~70% reflectance on the factory LUT) and giving paper a faint
//   texture. Skipping the offset here restores the stock guarantee that white
//   stays white, and leaves gray 0-242 bit-identical.
inline uint8_t applyBayerDither4Level(uint8_t gray, int x, int y) {
  int g = gray;
  int offset = (g < 64) ? g * 12 / 64 : 12;
  if (g > 242) offset = 0;
  g -= offset;

  int bayer = bayer4x4[y & 3][x & 3];
  int dither = (bayer - 8) * 5;  // Scale to +/-40 (half of quantization step 85)

  int adjusted = g + dither;
  if (adjusted < 0) adjusted = 0;
  if (adjusted > 255) adjusted = 255;

  if (adjusted < 64) return 0;
  if (adjusted < 150) return 1;
  if (adjusted < 210) return 2;
  return 3;
}
