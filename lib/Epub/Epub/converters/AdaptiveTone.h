#pragma once

#include <stdint.h>

// Per-image adaptive tone mapping for 4-level e-ink output.
//
// The factory LUT can only show four reflectance levels, so an image whose
// content occupies a narrow slice of the 0-255 range (typical of scanned
// plates and flat-lit photos) collapses into two or three of them and reads
// as muddy. Stretching that slice back across the full range before
// quantization recovers highlight and midtone separation. Approach follows
// YACP's sleep-image tone mapping, adapted here for EPUB images.
//
// This is complementary to the fixed soft-shoulder in DitherUtils.h, and the
// two compose: this normalizes the *content* range (per image), the
// soft-shoulder corrects the *panel* response (constant). Apply this first,
// then dither.
//
// The result is baked into a 256-entry LUT so the decode hot path costs one
// load per pixel and no branches. An inactive instance holds the identity
// map, so callers can index unconditionally.
struct AdaptiveTone {
  // Percentile pair used as the black/white points, in permille. Clipping the
  // extreme 1% keeps a few stray specular or shadow pixels from defining the
  // range for the whole image.
  static constexpr uint32_t LOW_PERMILLE = 10;
  static constexpr uint32_t HIGH_PERMILLE = 990;
  // Refuse to stretch a very narrow range. Below this the gain needed to
  // reach full scale (4x and up even after blending) amplifies sensor noise
  // and JPEG ringing into visible artifacts, which costs more than the extra
  // separation buys. Images wider than this get a proportionally gentler
  // stretch, and an already-full-range image lands on a near-identity curve,
  // so the guard only has to catch the destructive end.
  static constexpr int MIN_RANGE = 96;
  // Blend the stretched curve with the original rather than applying it fully
  // (3/4 stretched, 1/4 original). A full stretch looks harsh once quantized
  // to four levels.
  static constexpr int BLEND_NUM = 3;
  static constexpr int BLEND_DEN = 4;
  // Never darken near-white. Paper white must stay at level 3; the same
  // threshold guards the soft-shoulder in DitherUtils.h.
  static constexpr int HIGHLIGHT_KEEP = 242;

  uint8_t lut[256];
  bool active;

  AdaptiveTone() { reset(); }

  void reset() {
    for (int i = 0; i < 256; i++) lut[i] = static_cast<uint8_t>(i);
    active = false;
  }

  // Build the curve from a 256-bin luminance histogram. Returns false and
  // leaves the identity map in place when the image does not benefit (empty
  // histogram, or dynamic range already wide enough).
  bool buildFromHistogram(const uint32_t* histogram, uint32_t total) {
    reset();
    if (!histogram || total == 0) return false;

    const uint32_t lowTarget = static_cast<uint32_t>(static_cast<uint64_t>(total) * LOW_PERMILLE / 1000);
    const uint32_t highTarget = static_cast<uint32_t>(static_cast<uint64_t>(total) * HIGH_PERMILLE / 1000);

    uint32_t cumulative = 0;
    int low = 0;
    int high = 255;
    bool foundLow = false;
    for (int i = 0; i < 256; i++) {
      cumulative += histogram[i];
      if (!foundLow && cumulative >= lowTarget) {
        low = i;
        foundLow = true;
      }
      if (cumulative >= highTarget) {
        high = i;
        break;
      }
    }

    if (high - low < MIN_RANGE) return false;

    for (int v = 0; v < 256; v++) {
      int leveled;
      if (v <= low) {
        leveled = 0;
      } else if (v >= high) {
        leveled = 255;
      } else {
        leveled = ((v - low) * 255) / (high - low);
      }

      int adjusted = (v * (BLEND_DEN - BLEND_NUM) + leveled * BLEND_NUM) / BLEND_DEN;
      if (v > HIGHLIGHT_KEEP && adjusted < v) adjusted = v;
      if (adjusted < 0) adjusted = 0;
      if (adjusted > 255) adjusted = 255;
      lut[v] = static_cast<uint8_t>(adjusted);
    }

    active = true;
    return true;
  }
};
