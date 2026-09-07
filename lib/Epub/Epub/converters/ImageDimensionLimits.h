#pragma once

#include <cstdint>

namespace ImageDimensionLimits {

constexpr int64_t MAX_SOURCE_DIMENSION = INT16_MAX;
// Both decoders stream their source and periodically yield while decoding.
// This admits common ebook covers such as 1600x2560 and 2000x3000 without
// allowing unbounded decode time on the ESP32-C3.
constexpr int64_t MAX_SOURCE_PIXELS = 8388608;  // 8 MP (for example 2048x4096)

enum class ValidationResult : uint8_t {
  Valid,
  NonPositive,
  DimensionTooLarge,
  PixelCountTooLarge,
};

constexpr ValidationResult validate(const int64_t width, const int64_t height) {
  if (width <= 0 || height <= 0) return ValidationResult::NonPositive;
  // Check individual dimensions before multiplying so malformed values cannot
  // overflow the pixel-count calculation.
  if (width > MAX_SOURCE_DIMENSION || height > MAX_SOURCE_DIMENSION) {
    return ValidationResult::DimensionTooLarge;
  }
  return width * height <= MAX_SOURCE_PIXELS ? ValidationResult::Valid : ValidationResult::PixelCountTooLarge;
}

}  // namespace ImageDimensionLimits
