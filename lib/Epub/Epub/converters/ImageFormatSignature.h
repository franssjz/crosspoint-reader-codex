#pragma once

#include <cstddef>
#include <cstdint>

enum class ImageFileFormat : uint8_t {
  Unknown,
  Jpeg,
  Png,
};

constexpr ImageFileFormat detectImageFormatSignature(const uint8_t* data, const size_t size) {
  if (!data) return ImageFileFormat::Unknown;
  if (size >= 8 && data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47 &&
      data[4] == 0x0D && data[5] == 0x0A && data[6] == 0x1A && data[7] == 0x0A) {
    return ImageFileFormat::Png;
  }
  if (size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
    return ImageFileFormat::Jpeg;
  }
  return ImageFileFormat::Unknown;
}
