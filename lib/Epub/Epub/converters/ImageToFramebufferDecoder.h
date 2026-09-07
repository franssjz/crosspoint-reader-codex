#pragma once
#include <HalStorage.h>

#include <cstdint>
#include <memory>
#include <string>

#include "PixelCacheFormat.h"

class GfxRenderer;

struct ImageDimensions {
  int16_t width;
  int16_t height;
};

struct RenderConfig {
  int x, y;
  int maxWidth, maxHeight;
  bool useGrayscale = true;
  bool useDithering = true;
  bool performanceMode = false;
  bool useExactDimensions = false;  // If true, use maxWidth/maxHeight as exact output size (no recalculation)
  std::string cachePath;            // If non-empty, decoder will write pixel cache to this path
  // Tone calibration the quantized pixels are written for; stored in the cache
  // header so they are not reused under the other waveform.
  PixelCacheVariant cacheVariant = PixelCacheVariant::Differential;
};

class ImageToFramebufferDecoder {
 public:
  virtual ~ImageToFramebufferDecoder() = default;

  virtual bool decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer, const RenderConfig& config) = 0;

  virtual bool getDimensions(const std::string& imagePath, ImageDimensions& dims) const = 0;

  virtual const char* getFormatName() const = 0;

  // Shared by header probes and decoders so dimensions are validated before
  // narrowing to the int16_t layout representation.
  static bool validateAndStoreDimensions(int64_t width, int64_t height, ImageDimensions& out, const char* format);

  // Long image decodes must periodically yield to keep the idle task and its
  // watchdog serviced. The caller owns the timestamp so callbacks stay reentrant.
  static void yieldDuringDecode(uint32_t& lastYieldMs);

 protected:
  void warnUnsupportedFeature(const std::string& feature, const std::string& imagePath);
};
