#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

#include "lib/Epub/Epub/converters/ImageDimensionLimits.h"
#include "lib/Epub/Epub/converters/ImageDimsProbe.h"
#include "lib/Epub/Epub/converters/ImageFormatSignature.h"

TEST(ImageDimensionRegression, AcceptsCommonHighResolutionEbookCovers) {
  using ImageDimensionLimits::ValidationResult;
  EXPECT_EQ(ImageDimensionLimits::validate(1600, 2560), ValidationResult::Valid);
  EXPECT_EQ(ImageDimensionLimits::validate(2000, 3000), ValidationResult::Valid);
  EXPECT_EQ(ImageDimensionLimits::validate(2048, 4096), ValidationResult::Valid);
}

TEST(ImageDimensionRegression, RejectsInvalidAndOverLimitImagesWithoutOverflow) {
  using ImageDimensionLimits::ValidationResult;
  EXPECT_EQ(ImageDimensionLimits::validate(0, 800), ValidationResult::NonPositive);
  EXPECT_EQ(ImageDimensionLimits::validate(4096, 4096), ValidationResult::PixelCountTooLarge);
  EXPECT_EQ(ImageDimensionLimits::validate(std::numeric_limits<int64_t>::max(), 2),
            ValidationResult::DimensionTooLarge);
}

TEST(ImageSignatureRegression, RoutesBaselineAndProgressiveJpegToTheJpegDecoder) {
  constexpr std::array<uint8_t, 12> baselineJpeg = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x04,
                                                    0x00, 0x00, 0xFF, 0xC0, 0x00, 0x08};
  constexpr std::array<uint8_t, 12> progressiveJpeg = {0xFF, 0xD8, 0xFF, 0xE1, 0x00, 0x04,
                                                       0x00, 0x00, 0xFF, 0xC2, 0x00, 0x08};
  EXPECT_EQ(detectImageFormatSignature(baselineJpeg.data(), baselineJpeg.size()), ImageFileFormat::Jpeg);
  EXPECT_EQ(detectImageFormatSignature(progressiveJpeg.data(), progressiveJpeg.size()), ImageFileFormat::Jpeg);
}

TEST(ImageSignatureRegression, DetectsPngIndependentOfExtension) {
  constexpr std::array<uint8_t, 8> png = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  EXPECT_EQ(detectImageFormatSignature(png.data(), png.size()), ImageFileFormat::Png);
}

TEST(ImageSignatureRegression, RejectsTruncatedAndUnknownHeaders) {
  constexpr std::array<uint8_t, 3> truncatedPng = {0x89, 0x50, 0x4E};
  constexpr std::array<uint8_t, 4> unknown = {0x47, 0x49, 0x46, 0x38};
  EXPECT_EQ(detectImageFormatSignature(truncatedPng.data(), truncatedPng.size()), ImageFileFormat::Unknown);
  EXPECT_EQ(detectImageFormatSignature(unknown.data(), unknown.size()), ImageFileFormat::Unknown);
  EXPECT_EQ(detectImageFormatSignature(nullptr, 0), ImageFileFormat::Unknown);
}

TEST(ImageDimensionRegression, StreamsPastLargeJpegMetadataWithoutDecoderAllocation) {
  std::vector<uint8_t> jpeg = {
      0xFF, 0xD8,             // SOI
      0xFF, 0xE1, 0x10, 0x02  // APP1 with a 4096-byte payload
  };
  jpeg.insert(jpeg.end(), 4096, 0x00);
  const std::array<uint8_t, 9> sof = {
      0xFF, 0xC0, 0x00, 0x08, 0x08, 0x02, 0x2D, 0x03, 0x05  // 773x557 baseline JPEG
  };
  jpeg.insert(jpeg.end(), sof.begin(), sof.end());

  ImageDimsProbe probe;
  constexpr size_t chunkSize = 257;
  for (size_t offset = 0; offset < jpeg.size(); offset += chunkSize) {
    const size_t remaining = jpeg.size() - offset;
    const size_t count = remaining < chunkSize ? remaining : chunkSize;
    if (probe.write(jpeg.data() + offset, count) != count) break;
  }

  ImageDimensions dimensions = {};
  ASSERT_TRUE(probe.getDimensions(dimensions));
  EXPECT_EQ(dimensions.width, 773);
  EXPECT_EQ(dimensions.height, 557);
}
