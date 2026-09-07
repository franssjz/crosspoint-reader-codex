#include "ImageToFramebufferDecoder.h"

#include <Arduino.h>
#include <Logging.h>

#include "ImageDimensionLimits.h"

bool ImageToFramebufferDecoder::validateAndStoreDimensions(const int64_t width, const int64_t height,
                                                           ImageDimensions& out, const char* format) {
  using ImageDimensionLimits::ValidationResult;
  switch (ImageDimensionLimits::validate(width, height)) {
    case ValidationResult::NonPositive:
      LOG_ERR("IMG", "Invalid %s dimensions: %lldx%lld", format, static_cast<long long>(width),
              static_cast<long long>(height));
      return false;
    case ValidationResult::DimensionTooLarge:
      LOG_ERR("IMG", "%s dimensions exceed supported limit: %lldx%lld (max %lld per dimension)", format,
              static_cast<long long>(width), static_cast<long long>(height),
              static_cast<long long>(ImageDimensionLimits::MAX_SOURCE_DIMENSION));
      return false;
    case ValidationResult::PixelCountTooLarge:
      LOG_ERR("IMG", "%s too large (%lldx%lld = %lld pixels), max supported: %lld pixels", format,
              static_cast<long long>(width), static_cast<long long>(height), static_cast<long long>(width * height),
              static_cast<long long>(ImageDimensionLimits::MAX_SOURCE_PIXELS));
      return false;
    case ValidationResult::Valid:
      break;
  }

  out.width = static_cast<int16_t>(width);
  out.height = static_cast<int16_t>(height);
  return true;
}

void ImageToFramebufferDecoder::yieldDuringDecode(uint32_t& lastYieldMs) {
  const uint32_t now = millis();
  if (now - lastYieldMs >= 250) {
    lastYieldMs = now;
    vTaskDelay(1);
  }
}

void ImageToFramebufferDecoder::warnUnsupportedFeature(const std::string& feature, const std::string& imagePath) {
  LOG_ERR("IMG", "Warning: Unsupported feature '%s' in image '%s'. Image may not display correctly.", feature.c_str(),
          imagePath.c_str());
}
