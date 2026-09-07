#pragma once

#include <stdint.h>

// On-disk format for the .pxc pixel cache (2-bit / 4-level pixels, 4 per byte).
//
// The cache stores *already quantized* pixels, so it bakes in whatever tone
// curve and thresholds were in effect when it was written. That makes it
// firmware-version-sensitive: a cache produced by an older build would
// otherwise survive an update and render with the old calibration, while a
// freshly decoded image next to it renders with the new one — the same book
// showing two different looks depending on what was cached when.
//
// The header therefore carries a magic, a format/calibration version, and the
// tone variant the pixels were quantized for. Any mismatch makes the reader
// treat the file as stale and re-decode, overwriting it.
//
// Layout (little-endian, 8 bytes):
//   uint16 magic
//   uint8  version
//   uint8  variant   (PixelCacheVariant)
//   uint16 width
//   uint16 height
//
// Pre-versioning caches began with the width as their first field. No panel
// width comes near the magic value, so those files fail the magic check and
// are discarded rather than misread.

// 'P','X' little-endian. Far outside any plausible image width.
constexpr uint16_t PXC_MAGIC = 0x5850;

// Bump whenever the quantization written into the cache changes — thresholds,
// the soft-shoulder, the dither matrix, or the adaptive tone curve. Existing
// caches are then rejected instead of mixing old and new output.
//   1: factory-LUT calibrated dithering (soft-shoulder + highlight guard) with
//      adaptive per-image tone mapping.
constexpr uint8_t PXC_VERSION = 1;

constexpr size_t PXC_HEADER_BYTES = 8;

// Which waveform's tone calibration the stored pixels were quantized for.
// Factory and differential drive the panel differently, so pixels calibrated
// for one are not correct for the other; the reader re-decodes on a mismatch.
enum class PixelCacheVariant : uint8_t {
  Differential = 0,
  FactoryLut = 1,
};

// Total file size for a cache of the given geometry.
constexpr size_t pxcExpectedSize(const uint16_t width, const uint16_t height) {
  return PXC_HEADER_BYTES + static_cast<size_t>((width + 3) / 4) * height;
}
