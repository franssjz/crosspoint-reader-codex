#include "ImageBlock.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Serialization.h>
#include <ZipFile.h>

#include <cstdlib>
#include <new>
#include <utility>

#include "../converters/DirectPixelWriter.h"
#include "../converters/ImageDecoderFactory.h"
#include "../converters/PixelCacheFormat.h"

// Cache file format:
// - uint16_t width
// - uint16_t height
// - uint8_t pixels[...] - 2 bits per pixel, packed (4 pixels per byte), row-major order

ImageBlock::ImageBlock(const std::string& imagePath, int16_t width, int16_t height)
    : imagePath(imagePath), width(width), height(height) {}

ImageBlock::ImageBlock(const std::string& imagePath, int16_t width, int16_t height, std::string sourceEpubPath,
                       std::string sourceItemHref)
    : imagePath(imagePath),
      sourceEpubPath(std::move(sourceEpubPath)),
      sourceItemHref(std::move(sourceItemHref)),
      width(width),
      height(height) {}

bool ImageBlock::imageExists() const { return Storage.exists(imagePath.c_str()); }

namespace {

std::string getCachePath(const std::string& imagePath) {
  // Replace extension with .pxc (pixel cache)
  size_t dotPos = imagePath.rfind('.');
  if (dotPos != std::string::npos) {
    return imagePath.substr(0, dotPos) + ".pxc";
  }
  return imagePath + ".pxc";
}

// `expectedVariant` may be null to accept any tone variant, for callers that
// only need to know whether a structurally valid cache exists and have no
// render context to compare against. A variant mismatch is then caught at
// render time, which re-decodes.
bool readValidCacheHeader(FsFile& cacheFile, const int expectedWidth, const int expectedHeight,
                          const PixelCacheVariant* expectedVariant, uint16_t& cachedWidth, uint16_t& cachedHeight) {
  uint16_t magic = 0;
  uint8_t version = 0;
  uint8_t variant = 0;
  if (cacheFile.read(&magic, 2) != 2 || cacheFile.read(&version, 1) != 1 || cacheFile.read(&variant, 1) != 1 ||
      cacheFile.read(&cachedWidth, 2) != 2 || cacheFile.read(&cachedHeight, 2) != 2) {
    return false;
  }

  // A pre-versioning cache starts with its width here, which cannot collide
  // with the magic — so those files are rejected and re-decoded rather than
  // rendering with the calibration of an older firmware.
  if (magic != PXC_MAGIC || version != PXC_VERSION) return false;

  // Pixels calibrated for one waveform are not correct for the other.
  if (expectedVariant != nullptr && static_cast<PixelCacheVariant>(variant) != *expectedVariant) return false;

  const int widthDiff = abs(cachedWidth - expectedWidth);
  const int heightDiff = abs(cachedHeight - expectedHeight);
  if (widthDiff > 1 || heightDiff > 1) return false;

  return cacheFile.size() >= pxcExpectedSize(cachedWidth, cachedHeight);
}

constexpr size_t MAX_SESSION_IMAGE_FAILURES = 16;
uint64_t failedImageHashes[MAX_SESSION_IMAGE_FAILURES];
size_t failedImageCount = 0;

uint64_t imagePathHash(const std::string& path) {
  uint64_t hash = 14695981039346656037ull;
  for (const char c : path) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 1099511628211ull;
  }
  return hash;
}

bool imageFailedThisSession(const std::string& path) {
  const uint64_t hash = imagePathHash(path);
  for (size_t i = 0; i < failedImageCount; i++) {
    if (failedImageHashes[i] == hash) return true;
  }
  return false;
}

void rememberImageFailure(const std::string& path) {
  if (failedImageCount == MAX_SESSION_IMAGE_FAILURES || imageFailedThisSession(path)) return;
  failedImageHashes[failedImageCount++] = imagePathHash(path);
}

bool renderFromCache(GfxRenderer& renderer, const std::string& cachePath, int x, int y, int expectedWidth,
                     int expectedHeight, const PixelCacheVariant expectedVariant) {
  FsFile cacheFile;
  if (!Storage.openFileForRead("IMG", cachePath, cacheFile)) {
    return false;
  }

  uint16_t cachedWidth, cachedHeight;
  if (!readValidCacheHeader(cacheFile, expectedWidth, expectedHeight, &expectedVariant, cachedWidth, cachedHeight)) {
    // Stale (older firmware, or written for the other waveform). Fall through
    // to a fresh decode, which overwrites it.
    LOG_DBG("IMG", "Discarding stale image cache: %s", cachePath.c_str());
    return false;
  }

  // Use cached dimensions for rendering (they're the actual decoded size)
  expectedWidth = cachedWidth;
  expectedHeight = cachedHeight;

  LOG_DBG("IMG", "Loading from cache: %s (%dx%d)", cachePath.c_str(), cachedWidth, cachedHeight);

  // Read several rows per SD access. A full-page image is re-rendered on every
  // grayscale strip pass (~14x per page), and a one-row-per-read loop here means
  // cachedHeight (~728) tiny reads through the storage mutex + SdFat each time —
  // the dominant cost of displaying an image page. Batching rows into a ~4KB
  // buffer cuts that to ~20 reads per pass without holding the whole image.
  const int bytesPerRow = (cachedWidth + 3) / 4;  // 2 bits per pixel, 4 pixels per byte
  int rowsPerRead = 4096 / bytesPerRow;
  if (rowsPerRead < 1) rowsPerRead = 1;
  if (rowsPerRead > cachedHeight) rowsPerRead = cachedHeight;
  uint8_t* readBuffer = (uint8_t*)malloc((size_t)rowsPerRead * bytesPerRow);
  if (!readBuffer) {
    // Fall back to a single-row buffer under memory pressure.
    rowsPerRead = 1;
    readBuffer = (uint8_t*)malloc(bytesPerRow);
  }
  if (!readBuffer) {
    LOG_ERR("IMG", "Failed to allocate row buffer");
    return false;
  }

  DirectPixelWriter pw;
  pw.init(renderer);

  int rowsInBuffer = 0;
  int bufferRow = 0;
  for (int row = 0; row < cachedHeight; row++) {
    if (bufferRow >= rowsInBuffer) {
      const int toRead = (cachedHeight - row < rowsPerRead) ? (cachedHeight - row) : rowsPerRead;
      const size_t bytes = (size_t)toRead * bytesPerRow;
      if (cacheFile.read(readBuffer, bytes) != static_cast<int>(bytes)) {
        LOG_ERR("IMG", "Cache read error at row %d", row);
        free(readBuffer);
        return false;
      }
      rowsInBuffer = toRead;
      bufferRow = 0;
    }

    const uint8_t* rowBuffer = readBuffer + (size_t)bufferRow * bytesPerRow;
    bufferRow++;

    const int destY = y + row;
    pw.beginRow(destY);
    // On a grayscale strip pass only a narrow column window of the image is in
    // the active band; skip the rest instead of unpacking+clipping every pixel.
    int colStart, colEnd;
    pw.bandColRange(x, cachedWidth, colStart, colEnd);
    for (int col = colStart; col < colEnd; col++) {
      const int byteIdx = col >> 2;            // col / 4
      const int bitShift = 6 - (col & 3) * 2;  // MSB first within byte
      uint8_t pixelValue = (rowBuffer[byteIdx] >> bitShift) & 0x03;

      pw.writePixel(x + col, pixelValue);
    }
  }

  free(readBuffer);
  LOG_DBG("IMG", "Cache render complete");
  return true;
}

}  // namespace

bool ImageBlock::hasValidCache() const {
  FsFile cacheFile;
  if (!Storage.openFileForRead("IMG", getCachePath(imagePath), cacheFile)) return false;
  uint16_t cachedWidth, cachedHeight;
  return readValidCacheHeader(cacheFile, width, height, /*expectedVariant=*/nullptr, cachedWidth, cachedHeight);
}

bool ImageBlock::needsDecode() const { return !imageFailedThisSession(imagePath) && !hasValidCache(); }

void ImageBlock::clearSessionRenderFailures() { failedImageCount = 0; }

void ImageBlock::renderPlaceholder(GfxRenderer& renderer, const int x, const int y) const {
  renderer.fillRect(x, y, width, height, true);
  if (width > 2 && height > 2) renderer.fillRect(x + 1, y + 1, width - 2, height - 2, false);
}

bool ImageBlock::extractLazyImageIfNeeded() {
  if (Storage.exists(imagePath.c_str())) {
    FsFile existing;
    if (Storage.openFileForRead("IMG", imagePath, existing)) {
      const bool valid = existing.size() > 0;
      existing.close();
      if (valid) {
        return true;
      }
    }
    Storage.remove(imagePath.c_str());
  }

  if (sourceEpubPath.empty() || sourceItemHref.empty()) {
    return false;
  }

  FsFile cachedImageFile;
  if (!Storage.openFileForWrite("IMG", imagePath, cachedImageFile)) {
    LOG_ERR("IMG", "Failed to open lazy image cache: %s", imagePath.c_str());
    return false;
  }

  const bool extracted = ZipFile(sourceEpubPath).readFileToStream(sourceItemHref.c_str(), cachedImageFile, 4096);
  cachedImageFile.flush();
  cachedImageFile.close();
  if (!extracted) {
    LOG_ERR("IMG", "Failed to lazy-extract image: %s", sourceItemHref.c_str());
    Storage.remove(imagePath.c_str());
    return false;
  }

  LOG_DBG("IMG", "Lazy image extracted: %s", imagePath.c_str());
  return true;
}

void ImageBlock::render(GfxRenderer& renderer, const int x, const int y) {
  // The font-prewarm scan pass only accumulates glyphs; an image contributes
  // none, and its DirectPixelWriter output bypasses the renderer's scan-mode
  // suppression, so it would otherwise do a full (discarded) cache render every
  // page view. Skip it here. The image still draws in the real BW/grayscale
  // passes; on first view this just moves the one-time decode to the BW pass.
  FontCacheManager* fcm = renderer.getFontCacheManager();
  if (fcm && fcm->isScanning()) return;

  LOG_DBG("IMG", "Rendering image at %d,%d: %s (%dx%d)", x, y, imagePath.c_str(), width, height);

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();

  // Bounds check render position using logical screen dimensions
  if (x < 0 || y < 0 || x + width > screenWidth || y + height > screenHeight) {
    LOG_ERR("IMG", "Invalid render position: (%d,%d) size (%dx%d) screen (%dx%d)", x, y, width, height, screenWidth,
            screenHeight);
    return;
  }

  if (!renderer.glyphIntersectsStrip(x, y, x + width - 1, y + height - 1)) {
    return;
  }

  if (imageFailedThisSession(imagePath)) {
    renderPlaceholder(renderer, x, y);
    return;
  }

  // Pixels are quantized for a specific waveform's tone response, so the cache
  // is only reusable under the same one.
  const PixelCacheVariant cacheVariant = renderer.getImageToneMode() == GfxRenderer::GrayscaleMode::Differential
                                             ? PixelCacheVariant::Differential
                                             : PixelCacheVariant::FactoryLut;

  // Try to render from cache first
  std::string cachePath = getCachePath(imagePath);
  if (renderFromCache(renderer, cachePath, x, y, width, height, cacheVariant)) {
    return;  // Successfully rendered from cache
  }

  // No pixel cache - need to decode the image. If the image was deferred during
  // EPUB indexing, extract it from the EPUB now, only for the page being shown.
  if (!extractLazyImageIfNeeded()) {
    LOG_ERR("IMG", "Image file not found: %s", imagePath.c_str());
    rememberImageFailure(imagePath);
    renderPlaceholder(renderer, x, y);
    return;
  }

  FsFile file;
  if (!Storage.openFileForRead("IMG", imagePath, file)) {
    LOG_ERR("IMG", "Image file not readable: %s", imagePath.c_str());
    rememberImageFailure(imagePath);
    renderPlaceholder(renderer, x, y);
    return;
  }
  size_t fileSize = file.size();
  file.close();

  if (fileSize == 0) {
    LOG_ERR("IMG", "Image file is empty: %s", imagePath.c_str());
    rememberImageFailure(imagePath);
    renderPlaceholder(renderer, x, y);
    return;
  }

  LOG_DBG("IMG", "Decoding and caching: %s", imagePath.c_str());

  RenderConfig config;
  config.x = x;
  config.y = y;
  config.maxWidth = width;
  config.maxHeight = height;
  config.useGrayscale = true;
  config.useDithering = true;
  config.performanceMode = false;
  config.useExactDimensions = true;  // Use pre-calculated dimensions to avoid rounding mismatches
  config.cachePath = cachePath;      // Enable caching during decode
  config.cacheVariant = cacheVariant;

  ImageToFramebufferDecoder* decoder = ImageDecoderFactory::getDecoder(imagePath);
  if (!decoder) {
    LOG_ERR("IMG", "No decoder found for image: %s", imagePath.c_str());
    rememberImageFailure(imagePath);
    renderPlaceholder(renderer, x, y);
    return;
  }

  LOG_DBG("IMG", "Using %s decoder", decoder->getFormatName());

  bool success = decoder->decodeToFramebuffer(imagePath, renderer, config);
  if (!success) {
    LOG_ERR("IMG", "Failed to decode image: %s", imagePath.c_str());
    rememberImageFailure(imagePath);
    renderPlaceholder(renderer, x, y);
    return;
  }

  LOG_DBG("IMG", "Decode successful");
}

bool ImageBlock::serialize(FsFile& file) {
  serialization::writeString(file, imagePath);
  serialization::writePod(file, width);
  serialization::writePod(file, height);
  const bool hasLazySource = !sourceEpubPath.empty() && !sourceItemHref.empty();
  serialization::writePod(file, hasLazySource);
  if (hasLazySource) {
    serialization::writeString(file, sourceEpubPath);
    serialization::writeString(file, sourceItemHref);
  }
  return true;
}

std::unique_ptr<ImageBlock> ImageBlock::deserialize(FsFile& file) {
  std::string path;
  serialization::readString(file, path);
  int16_t w, h;
  serialization::readPod(file, w);
  serialization::readPod(file, h);
  if (path.empty() || w <= 0 || h <= 0) {
    LOG_ERR("IMG", "Deserialization failed: invalid image metadata path=%s size=%dx%d", path.c_str(), w, h);
    return nullptr;
  }
  bool hasLazySource = false;
  serialization::readPod(file, hasLazySource);
  ImageBlock* block = nullptr;
  if (hasLazySource) {
    std::string sourceEpubPath;
    std::string sourceItemHref;
    serialization::readString(file, sourceEpubPath);
    serialization::readString(file, sourceItemHref);
    block = new (std::nothrow) ImageBlock(path, w, h, std::move(sourceEpubPath), std::move(sourceItemHref));
  } else {
    block = new (std::nothrow) ImageBlock(path, w, h);
  }
  if (!block) {
    LOG_ERR("IMG", "Deserialization failed: could not allocate ImageBlock");
    return nullptr;
  }
  return std::unique_ptr<ImageBlock>(block);
}
