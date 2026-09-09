/**
 * Xtc.cpp
 *
 * Main XTC ebook class implementation
 * XTC ebook support for CrossPoint Reader
 */

#include "Xtc.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

#include "Xtc/XtcBitmapUtils.h"

namespace {
constexpr size_t BMP_2BIT_HEADER_SIZE = 70;

void writeLe16(uint8_t* out, const uint16_t value) {
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8);
}

void writeLe32(uint8_t* out, const uint32_t value) {
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8);
  out[2] = static_cast<uint8_t>(value >> 16);
  out[3] = static_cast<uint8_t>(value >> 24);
}

void create2BitBmpHeader(uint8_t (&header)[BMP_2BIT_HEADER_SIZE], const uint16_t width, const uint16_t height,
                         const uint32_t rowSize) {
  memset(header, 0, sizeof(header));
  const uint32_t imageSize = rowSize * height;
  header[0] = 'B';
  header[1] = 'M';
  writeLe32(header + 2, static_cast<uint32_t>(sizeof(header)) + imageSize);
  writeLe32(header + 10, sizeof(header));
  writeLe32(header + 14, 40);  // BITMAPINFOHEADER
  writeLe32(header + 18, width);
  writeLe32(header + 22, static_cast<uint32_t>(-static_cast<int32_t>(height)));
  writeLe16(header + 26, 1);
  writeLe16(header + 28, 2);
  writeLe32(header + 34, imageSize);
  writeLe32(header + 38, 2835);
  writeLe32(header + 42, 2835);
  writeLe32(header + 46, 4);
  writeLe32(header + 50, 4);
  for (uint8_t i = 0; i < 4; ++i) {
    const uint8_t gray = static_cast<uint8_t>(i * 85);
    const size_t paletteOffset = 54 + static_cast<size_t>(i) * 4;
    header[paletteOffset] = gray;
    header[paletteOffset + 1] = gray;
    header[paletteOffset + 2] = gray;
  }
}

void yieldDuringThumbnail(uint8_t& rowsSinceYield) {
  if (++rowsSinceYield < 8) return;
  rowsSinceYield = 0;
  delay(1);
}

constexpr char XTCH_SOURCE_CACHE_NAME[] = "cover_src_xtch_v1.bin";
constexpr char XTCH_SOURCE_CACHE_TMP_NAME[] = "cover_src_xtch_v1.tmp";
constexpr uint8_t XTCH_SOURCE_CACHE_SCHEMA = 1;
constexpr uint16_t XTCH_SOURCE_BAND_ROWS = 8;
constexpr size_t XTCH_SOURCE_BAND_MAX_BYTES = 2048;

#pragma pack(push, 1)
struct XtchSourceCacheHeader {
  char magic[4];
  uint8_t schema;
  uint8_t bitDepth;
  uint16_t width;
  uint16_t height;
  uint16_t rowBytes;
  uint32_t payloadBytes;
};
#pragma pack(pop)
static_assert(sizeof(XtchSourceCacheHeader) == 16);

bool writeExact(FsFile& file, const uint8_t* data, const size_t size) { return file.write(data, size) == size; }

bool readExact(FsFile& file, uint8_t* data, const size_t size) { return file.read(data, size) == size; }

std::string xtchSourceCachePath(const Xtc& xtc) { return xtc.getCachePath() + "/" + XTCH_SOURCE_CACHE_NAME; }

bool hasValidXtchSourceCache(const std::string& path, const xtc::PageInfo& pageInfo, const size_t rowBytes,
                             const size_t payloadBytes) {
  FsFile source;
  if (!Storage.openFileForRead("XTC", path, source)) return false;

  XtchSourceCacheHeader header{};
  const bool valid = readExact(source, reinterpret_cast<uint8_t*>(&header), sizeof(header)) &&
                     memcmp(header.magic, "XCS1", sizeof(header.magic)) == 0 &&
                     header.schema == XTCH_SOURCE_CACHE_SCHEMA && header.bitDepth == 2 &&
                     header.width == pageInfo.width && header.height == pageInfo.height &&
                     header.rowBytes == rowBytes && header.payloadBytes == payloadBytes &&
                     source.size() == sizeof(header) + payloadBytes;
  source.close();
  return valid;
}

bool ensureXtchSourceCache(const Xtc& xtc, const xtc::PageInfo& pageInfo, std::string& sourcePath) {
  const size_t rowBytes = (static_cast<size_t>(pageInfo.width) + 3) / 4;
  const size_t payloadBytes = rowBytes * pageInfo.height;
  if (pageInfo.width == 0 || pageInfo.height == 0 || rowBytes > XTCH_SOURCE_BAND_MAX_BYTES) {
    LOG_ERR("XTC", "XTCH source row is too wide for bounded conversion");
    return false;
  }
  const uint16_t bandRows = std::min<size_t>(XTCH_SOURCE_BAND_ROWS, XTCH_SOURCE_BAND_MAX_BYTES / rowBytes);
  sourcePath = xtchSourceCachePath(xtc);
  if (hasValidXtchSourceCache(sourcePath, pageInfo, rowBytes, payloadBytes)) return true;

  if (Storage.exists(sourcePath.c_str()) && !Storage.remove(sourcePath.c_str())) {
    LOG_ERR("XTC", "Failed to remove stale XTCH source cache");
    return false;
  }

  const std::string tmpPath = xtc.getCachePath() + "/" + XTCH_SOURCE_CACHE_TMP_NAME;
  if (Storage.exists(tmpPath.c_str())) Storage.remove(tmpPath.c_str());
  FsFile output;
  if (!Storage.openFileForWrite("XTC", tmpPath, output)) {
    LOG_ERR("XTC", "Failed to create XTCH source cache");
    return false;
  }

  XtchSourceCacheHeader header{{'X', 'C', 'S', '1'},
                               XTCH_SOURCE_CACHE_SCHEMA,
                               2,
                               pageInfo.width,
                               pageInfo.height,
                               static_cast<uint16_t>(rowBytes),
                               static_cast<uint32_t>(payloadBytes)};
  bool success = writeExact(output, reinterpret_cast<const uint8_t*>(&header), sizeof(header));
  // Up to eight packed rows, capped at 2 KiB even for landscape pages.
  // The fallible heap band is reused because callback task stack space is limited.
  auto band = makeUniqueNoThrow<uint8_t[]>(rowBytes * bandRows);
  if (!band) {
    LOG_ERR("XTC", "Failed to allocate XTCH source band (%u bytes)", static_cast<unsigned int>(rowBytes * bandRows));
    success = false;
  }

  struct BandContext {
    uint8_t* band;
    uint16_t width;
    uint16_t height;
    size_t rowBytes;
    uint32_t y;
    uint16_t rows;
  } context{band.get(), pageInfo.width, pageInfo.height, rowBytes, 0, 0};
  const auto collectBand = [](void* raw, const uint8_t* first, const uint8_t* second, const size_t size,
                              const size_t offset) {
    auto& ctx = *static_cast<BandContext*>(raw);
    const size_t columnBytes = (ctx.height + 7U) / 8U;
    for (size_t i = 0; i < size; ++i) {
      const size_t column = (offset + i) / columnBytes;
      if (column >= ctx.width) continue;
      const uint16_t yBase = static_cast<uint16_t>(((offset + i) % columnBytes) * 8);
      if (yBase >= ctx.y + ctx.rows || yBase + 8U <= ctx.y) continue;
      const uint16_t x = static_cast<uint16_t>(ctx.width - 1 - column);
      for (uint8_t bit = 0; bit < 8 && yBase + bit < ctx.height; ++bit) {
        const uint32_t y = yBase + bit;
        if (y < ctx.y || y >= ctx.y + ctx.rows) continue;
        const uint8_t pixel = (((first[i] >> (7 - bit)) & 1) << 1) | ((second[i] >> (7 - bit)) & 1);
        ctx.band[(y - ctx.y) * ctx.rowBytes + x / 4] |= static_cast<uint8_t>(pixel << (6 - (x % 4) * 2));
      }
    }
  };
  uint8_t rowsSinceYield = 0;
  for (uint32_t bandY = 0; success && bandY < pageInfo.height; bandY += bandRows) {
    const uint16_t bandHeight = std::min<uint16_t>(bandRows, pageInfo.height - bandY);
    context.y = bandY;
    context.rows = bandHeight;
    memset(band.get(), 0, rowBytes * bandRows);
    const xtc::XtcError error = xtc.loadPagePlanePairs(0, collectBand, &context);
    if (error != xtc::XtcError::OK || !writeExact(output, band.get(), rowBytes * bandHeight)) {
      LOG_ERR("XTC", "Failed to build XTCH source cache");
      success = false;
      break;
    }
    yieldDuringThumbnail(rowsSinceYield);
  }

  const bool closed = output.close();
  if (!success || !closed || !Storage.rename(tmpPath.c_str(), sourcePath.c_str())) {
    if (success) LOG_ERR("XTC", "Failed to finalize XTCH source cache");
    Storage.remove(tmpPath.c_str());
    return false;
  }
  return true;
}

bool openXtchSourceCache(const Xtc& xtc, const xtc::PageInfo& pageInfo, FsFile& source, size_t& rowBytes) {
  std::string sourcePath;
  if (!ensureXtchSourceCache(xtc, pageInfo, sourcePath)) return false;
  rowBytes = (static_cast<size_t>(pageInfo.width) + 3) / 4;
  if (!Storage.openFileForRead("XTC", sourcePath, source) || !source.seek(sizeof(XtchSourceCacheHeader))) {
    LOG_ERR("XTC", "Failed to open XTCH source cache");
    source.close();
    return false;
  }
  return true;
}

bool readXtchSourceRow(FsFile& source, const size_t rowBytes, const uint16_t y, uint8_t* row) {
  const uint32_t offset = sizeof(XtchSourceCacheHeader) + static_cast<uint32_t>(y) * rowBytes;
  return source.seek(offset) && readExact(source, row, rowBytes);
}

bool replaceGeneratedBmp(const std::string& tmpPath, const std::string& finalPath) {
  const std::string backupPath = finalPath + ".bak";
  const bool hadPrevious = Storage.exists(finalPath.c_str());
  if (hadPrevious) {
    if (Storage.exists(backupPath.c_str()) && !Storage.remove(backupPath.c_str())) return false;
    if (!Storage.rename(finalPath.c_str(), backupPath.c_str())) return false;
  }
  if (!Storage.rename(tmpPath.c_str(), finalPath.c_str())) {
    if (hadPrevious) Storage.rename(backupPath.c_str(), finalPath.c_str());
    Storage.remove(tmpPath.c_str());
    LOG_ERR("XTC", "Failed to finalize generated BMP");
    return false;
  }
  if (hadPrevious) Storage.remove(backupPath.c_str());
  return true;
}

bool recoverGeneratedBmp(const std::string& path) {
  const std::string backup = path + ".bak";
  if (!Storage.exists(path.c_str()) && Storage.exists(backup.c_str())) {
    if (!Storage.rename(backup.c_str(), path.c_str())) {
      LOG_ERR("XTC", "Failed to recover generated BMP");
      return false;
    }
  }
  return true;
}

// Only the generated BMP cache is migrated; book identity and progress paths stay unchanged.
constexpr uint16_t XTCH_THUMB_CACHE_VERSION = 0x5801;

bool readBmpHeader(FsFile& file, BmpHeader& header) {
  return file.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) == sizeof(header) &&
         header.fileHeader.bfType == 0x4D42 && header.infoHeader.biPlanes == 1 &&
         header.infoHeader.biCompression == 0 && header.infoHeader.biWidth > 0 && header.infoHeader.biWidth <= 8192 &&
         header.infoHeader.biHeight != 0 && header.infoHeader.biHeight >= -65535 &&
         header.infoHeader.biHeight <= 65535 &&
         (header.infoHeader.biBitCount == 1 || header.infoHeader.biBitCount == 2);
}
}  // namespace

bool Xtc::load() {
  LOG_DBG("XTC", "Loading XTC: %s", filepath.c_str());

  // Initialize parser
  loaded = false;
  parser = makeUniqueNoThrow<xtc::XtcParser>();
  if (!parser) {
    LOG_ERR("XTC", "Failed to allocate parser");
    return false;
  }

  // Open XTC file
  xtc::XtcError err = parser->open(filepath.c_str());
  if (err != xtc::XtcError::OK) {
    LOG_ERR("XTC", "Failed to load: %s", xtc::errorToString(err));
    parser.reset();
    return false;
  }

  loaded = true;
  LOG_DBG("XTC", "Loaded XTC: %s (%lu pages)", filepath.c_str(), parser->getPageCount());
  return true;
}

bool Xtc::clearCache() const {
  if (!Storage.exists(cachePath.c_str())) {
    LOG_DBG("XTC", "Cache does not exist, no action needed");
    return true;
  }

  if (!Storage.removeDir(cachePath.c_str())) {
    LOG_ERR("XTC", "Failed to clear cache");
    return false;
  }

  LOG_DBG("XTC", "Cache cleared successfully");
  return true;
}

void Xtc::setupCacheDir() const {
  if (Storage.exists(cachePath.c_str())) {
    return;
  }

  // Create directories recursively
  for (size_t i = 1; i < cachePath.length(); i++) {
    if (cachePath[i] == '/') {
      Storage.mkdir(cachePath.substr(0, i).c_str());
    }
  }
  Storage.mkdir(cachePath.c_str());
}

std::string Xtc::getTitle() const {
  if (!loaded || !parser) {
    return "";
  }

  // Try to get title from XTC metadata first
  std::string title = parser->getTitle();
  if (!title.empty()) {
    return title;
  }

  // Fallback: extract filename from path as title
  size_t lastSlash = filepath.find_last_of('/');
  size_t lastDot = filepath.find_last_of('.');

  if (lastSlash == std::string::npos) {
    lastSlash = 0;
  } else {
    lastSlash++;
  }

  if (lastDot == std::string::npos || lastDot <= lastSlash) {
    return filepath.substr(lastSlash);
  }

  return filepath.substr(lastSlash, lastDot - lastSlash);
}

std::string Xtc::getAuthor() const {
  if (!loaded || !parser) {
    return "";
  }

  // Try to get author from XTC metadata
  return parser->getAuthor();
}

bool Xtc::hasChapters() const {
  if (!loaded || !parser) {
    return false;
  }
  return parser->hasChapters();
}

const std::vector<xtc::ChapterInfo>& Xtc::getChapters() {
  static const std::vector<xtc::ChapterInfo> kEmpty;
  if (!loaded || !parser) {
    return kEmpty;
  }
  return parser->getChapters();
}

std::string Xtc::getCoverBmpPath() const { return cachePath + "/cover.bmp"; }

bool Xtc::generateCoverBmp() const {
  const std::string coverPath = getCoverBmpPath();
  if (!recoverGeneratedBmp(coverPath)) return false;
  const bool coverExists = Storage.exists(coverPath.c_str());

  if (!loaded || !parser) {
    if (coverExists) return true;
    LOG_ERR("XTC", "Cannot generate cover BMP, file not loaded");
    return false;
  }

  const uint8_t bitDepth = parser->getBitDepth();
  if (coverExists) {
    if (bitDepth != 2) return true;

    FsFile existing;
    bool alreadyTwoBit = false;
    if (Storage.openFileForRead("XTC", coverPath, existing)) {
      BmpHeader existingHeader{};
      alreadyTwoBit = readBmpHeader(existing, existingHeader) && existingHeader.infoHeader.biBitCount == 2;
      existing.close();
    }
    if (alreadyTwoBit) return true;
    // Keep a usable legacy cover until its complete replacement is ready.
  }

  if (parser->getPageCount() == 0) {
    LOG_ERR("XTC", "No pages in XTC file");
    return false;
  }

  // Setup cache directory
  setupCacheDir();

  // Get first page info for cover
  xtc::PageInfo pageInfo;
  if (!parser->getPageInfo(0, pageInfo)) {
    LOG_DBG("XTC", "Failed to get first page info");
    return false;
  }

  if (bitDepth == 2) {
    FsFile source;
    size_t sourceRowBytes = 0;
    if (!openXtchSourceCache(*this, pageInfo, source, sourceRowBytes)) return false;

    const uint32_t dstRowSize = ((static_cast<uint32_t>(pageInfo.width) * 2 + 31) / 32) * 4;
    // The output row is at most 200 bytes for the current 800px display. Heap storage avoids
    // growing this cold-path task stack and is reused for every row.
    auto sourceRow = makeUniqueNoThrow<uint8_t[]>(sourceRowBytes);
    auto outputRow = makeUniqueNoThrow<uint8_t[]>(dstRowSize);
    if (!sourceRow || !outputRow) {
      LOG_ERR("XTC", "Failed to allocate XTCH cover rows (%u + %u bytes)", static_cast<unsigned int>(sourceRowBytes),
              static_cast<unsigned int>(dstRowSize));
      source.close();
      return false;
    }

    const std::string tmpPath = coverPath + ".tmp";
    if (Storage.exists(tmpPath.c_str())) Storage.remove(tmpPath.c_str());
    FsFile coverBmp;
    if (!Storage.openFileForWrite("XTC", tmpPath, coverBmp)) {
      LOG_ERR("XTC", "Failed to create XTCH cover BMP");
      source.close();
      return false;
    }

    uint8_t bmpHeader[BMP_2BIT_HEADER_SIZE];
    create2BitBmpHeader(bmpHeader, pageInfo.width, pageInfo.height, dstRowSize);
    bool success = writeExact(coverBmp, bmpHeader, sizeof(bmpHeader));
    // XTH pixel values use 0=white, 1=dark gray, 2=light gray, 3=black.
    // The BMP palette is ordered black through white.
    for (uint16_t y = 0; success && y < pageInfo.height; ++y) {
      if (!readXtchSourceRow(source, sourceRowBytes, y, sourceRow.get())) {
        LOG_ERR("XTC", "Failed to read XTCH cover source row");
        success = false;
        break;
      }
      uint8_t* dst = outputRow.get();
      memset(dst, 0, dstRowSize);
      for (uint16_t x = 0; x < pageInfo.width; ++x) {
        const uint8_t pixel = static_cast<uint8_t>((sourceRow[x / 4] >> (6 - (x % 4) * 2)) & 0x03);
        dst[x / 4] |= static_cast<uint8_t>(xtc::xtchBmpIndex(pixel) << (6 - (x % 4) * 2));
      }
      success = writeExact(coverBmp, dst, dstRowSize);
    }
    const bool sourceClosed = source.close();
    const bool bmpClosed = coverBmp.close();
    if (!success || !sourceClosed || !bmpClosed) {
      if (success) LOG_ERR("XTC", "Failed to close XTCH cover BMP");
      Storage.remove(tmpPath.c_str());
      return false;
    }
    return replaceGeneratedBmp(tmpPath, coverPath);
  }

  const uint32_t rowBytes = (pageInfo.width + 7U) / 8U;
  const uint32_t paddedRowBytes = ((pageInfo.width + 31U) / 32U) * 4U;
  const std::string tmpPath = coverPath + ".tmp";
  FsFile output;
  if (!Storage.openFileForWrite("XTC", tmpPath, output)) return false;
  BmpHeader header{};
  createBmpHeader(&header, pageInfo.width, pageInfo.height, BmpRowOrder::TopDown);
  bool success = writeExact(output, reinterpret_cast<const uint8_t*>(&header), sizeof(header));
  struct CoverContext {
    FsFile& output;
    uint32_t rowBytes;
    uint32_t paddedRowBytes;
    bool success;
  } context{output, rowBytes, paddedRowBytes, success};
  const auto writeRows = [](void* raw, const uint8_t* data, const size_t size, const size_t offset) {
    auto& ctx = *static_cast<CoverContext*>(raw);
    constexpr uint8_t padding[3] = {};
    size_t used = 0;
    while (ctx.success && used < size) {
      const size_t inRow = (offset + used) % ctx.rowBytes;
      const size_t count = std::min<size_t>(size - used, ctx.rowBytes - inRow);
      ctx.success = writeExact(ctx.output, data + used, count);
      used += count;
      if (ctx.success && inRow + count == ctx.rowBytes) {
        ctx.success = writeExact(ctx.output, padding, ctx.paddedRowBytes - ctx.rowBytes);
      }
    }
  };
  const auto error = loadPageStreaming(0, writeRows, &context);
  success = context.success;
  const bool closed = output.close();
  if (!success || !closed || error != xtc::XtcError::OK) {
    LOG_ERR("XTC", "Failed to stream XTC cover");
    Storage.remove(tmpPath.c_str());
    return false;
  }
  return replaceGeneratedBmp(tmpPath, coverPath);
}

std::string Xtc::getThumbBmpPath() const { return cachePath + "/thumb_[HEIGHT].bmp"; }
std::string Xtc::getThumbBmpPath(int height) const { return cachePath + "/thumb_" + std::to_string(height) + ".bmp"; }
std::string Xtc::getThumbBmpPath(int width, int height) const {
  return cachePath + "/thumb_" + std::to_string(width) + "x" + std::to_string(height) + ".bmp";
}

bool Xtc::generateThumbBmp(int height) const {
  return generateThumbBmpToPath(static_cast<int>(height * 0.6f), height, getThumbBmpPath(height));
}

bool Xtc::generateThumbBmp(int width, int height) const {
  return generateThumbBmpToPath(width, height, getThumbBmpPath(width, height));
}

bool Xtc::generateThumbBmpToPath(const int width, const int height, const std::string& thumbPath) const {
  if (width <= 0 || height <= 0 || width > 8192 || height > 65535 || !recoverGeneratedBmp(thumbPath)) return false;
  if (!loaded || !parser) return Storage.exists(thumbPath.c_str());
  const bool twoBit = getBitDepth() == 2;
  if (Storage.exists(thumbPath.c_str())) {
    if (!twoBit) return true;
    FsFile existing;
    BmpHeader header{};
    const bool valid = Storage.openFileForRead("XTC", thumbPath, existing) && readBmpHeader(existing, header) &&
                       header.fileHeader.bfReserved1 == XTCH_THUMB_CACHE_VERSION;
    existing.close();
    if (valid) return true;
  }
  if (!generateCoverBmp()) return false;
  FsFile source;
  BmpHeader sourceHeader{};
  if (!Storage.openFileForRead("XTC", getCoverBmpPath(), source) || !readBmpHeader(source, sourceHeader)) {
    source.close();
    LOG_ERR("XTC", "Failed to read cover for thumbnail");
    return false;
  }
  const uint16_t sourceWidth = static_cast<uint16_t>(sourceHeader.infoHeader.biWidth);
  const uint16_t sourceHeight = static_cast<uint16_t>(
      sourceHeader.infoHeader.biHeight < 0 ? -sourceHeader.infoHeader.biHeight : sourceHeader.infoHeader.biHeight);
  const uint8_t sourceBits = sourceHeader.infoHeader.biBitCount;
  const float scale = std::max(static_cast<float>(width) / sourceWidth, static_cast<float>(height) / sourceHeight);
  // Preserve CPR's existing aspect ratio / no-upscaling behavior and filenames.
  const uint16_t thumbWidth = scale >= 1.0f ? sourceWidth : std::max<uint16_t>(1, sourceWidth * scale);
  const uint16_t thumbHeight = scale >= 1.0f ? sourceHeight : std::max<uint16_t>(1, sourceHeight * scale);
  const uint32_t sourceRowBytes = ((static_cast<uint32_t>(sourceWidth) * sourceBits + 31U) / 32U) * 4U;
  const uint32_t rowBytes = ((thumbWidth + 31U) / 32U) * 4U;
  // Reusable rows and per-output-column sums; never materialize the source image.
  auto sourceRow = makeUniqueNoThrow<uint8_t[]>(sourceRowBytes);
  auto outputRow = makeUniqueNoThrow<uint8_t[]>(rowBytes);
  auto graySums = makeUniqueNoThrow<uint64_t[]>(thumbWidth);
  if (!sourceRow || !outputRow || !graySums) {
    source.close();
    LOG_ERR("XTC", "Failed to allocate thumbnail rows");
    return false;
  }
  const std::string tmpPath = thumbPath + ".tmp";
  FsFile output;
  if (!Storage.openFileForWrite("XTC", tmpPath, output)) {
    source.close();
    return false;
  }
  BmpHeader header{};
  createBmpHeader(&header, thumbWidth, thumbHeight, BmpRowOrder::TopDown);
  if (twoBit) header.fileHeader.bfReserved1 = XTCH_THUMB_CACHE_VERSION;
  bool success = writeExact(output, reinterpret_cast<const uint8_t*>(&header), sizeof(header));
  uint8_t rowsSinceYield = 0;
  const uint32_t scaleInv = scale >= 1.0f ? 65536U : static_cast<uint32_t>(65536.0f / scale);
  for (uint16_t y = 0; success && y < thumbHeight; ++y) {
    const uint32_t startY = std::min<uint32_t>(sourceHeight - 1, (static_cast<uint64_t>(y) * scaleInv) >> 16);
    const uint32_t endY = std::min<uint32_t>(
        sourceHeight, std::max<uint32_t>(startY + 1, (static_cast<uint64_t>(y + 1U) * scaleInv) >> 16));
    std::fill_n(graySums.get(), thumbWidth, 0);
    for (uint32_t sy = startY; success && sy < endY; ++sy) {
      const uint32_t row = sourceHeader.infoHeader.biHeight < 0 ? sy : sourceHeight - 1 - sy;
      const uint64_t offset = sourceHeader.fileHeader.bfOffBits + static_cast<uint64_t>(row) * sourceRowBytes;
      success = source.seek64(offset) && readExact(source, sourceRow.get(), sourceRowBytes);
      for (uint16_t x = 0; success && x < thumbWidth; ++x) {
        const uint32_t startX = std::min<uint32_t>(sourceWidth - 1, (static_cast<uint64_t>(x) * scaleInv) >> 16);
        const uint32_t endX = std::min<uint32_t>(
            sourceWidth, std::max<uint32_t>(startX + 1, (static_cast<uint64_t>(x + 1U) * scaleInv) >> 16));
        for (uint32_t sx = startX; sx < endX; ++sx) {
          const uint8_t gray = sourceBits == 2 ? ((sourceRow[sx / 4] >> (6 - (sx % 4) * 2)) & 3) * 85
                                               : ((sourceRow[sx / 8] >> (7 - sx % 8)) & 1) * 255;
          graySums[x] += gray;
        }
      }
      yieldDuringThumbnail(rowsSinceYield);
    }
    memset(outputRow.get(), 0xFF, rowBytes);
    for (uint16_t x = 0; success && x < thumbWidth; ++x) {
      const uint32_t startX = std::min<uint32_t>(sourceWidth - 1, (static_cast<uint64_t>(x) * scaleInv) >> 16);
      const uint32_t endX = std::min<uint32_t>(
          sourceWidth, std::max<uint32_t>(startX + 1, (static_cast<uint64_t>(x + 1U) * scaleInv) >> 16));
      const uint64_t samples = static_cast<uint64_t>(endX - startX) * (endY - startY);
      const uint8_t gray = static_cast<uint8_t>(graySums[x] / samples);
      uint32_t hash = static_cast<uint32_t>(x) * 374761393U + static_cast<uint32_t>(y) * 668265263U;
      hash = (hash ^ (hash >> 13)) * 1274126177U;
      const int threshold = 128 + (static_cast<int>(hash >> 24) - 128) / 2;
      if (gray < threshold) outputRow[x / 8] &= static_cast<uint8_t>(~(1 << (7 - x % 8)));
    }
    success = success && writeExact(output, outputRow.get(), rowBytes);
  }
  const bool sourceClosed = source.close();
  const bool outputClosed = output.close();
  if (!success || !sourceClosed || !outputClosed) {
    LOG_ERR("XTC", "Failed to generate thumbnail rows");
    Storage.remove(tmpPath.c_str());
    return false;
  }
  return replaceGeneratedBmp(tmpPath, thumbPath);
}

uint32_t Xtc::getPageCount() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getPageCount();
}

uint16_t Xtc::getPageWidth() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getWidth();
}

uint16_t Xtc::getPageHeight() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getHeight();
}

uint8_t Xtc::getBitDepth() const {
  if (!loaded || !parser) {
    return 1;  // Default to 1-bit
  }
  return parser->getBitDepth();
}

size_t Xtc::loadPage(uint32_t pageIndex, uint8_t* buffer, size_t bufferSize) const {
  if (!loaded || !parser) {
    return 0;
  }
  return const_cast<xtc::XtcParser*>(parser.get())->loadPage(pageIndex, buffer, bufferSize);
}

xtc::XtcError Xtc::loadPageStreaming(
    uint32_t pageIndex, const std::function<void(const uint8_t* data, size_t size, size_t offset)>& callback,
    size_t chunkSize) const {
  if (!loaded || !parser) {
    return xtc::XtcError::FILE_NOT_FOUND;
  }
  return const_cast<xtc::XtcParser*>(parser.get())->loadPageStreaming(pageIndex, callback, chunkSize);
}

xtc::XtcError Xtc::loadPageStreaming(const uint32_t pageIndex, const xtc::XtcParser::StreamCallback callback,
                                     void* context, const size_t chunkSize) const {
  if (!loaded || !parser) return xtc::XtcError::FILE_NOT_FOUND;
  return parser->loadPageStreaming(pageIndex, callback, context, chunkSize);
}

xtc::XtcError Xtc::loadPagePlanePairs(const uint32_t pageIndex, const xtc::XtcParser::PlanePairCallback callback,
                                      void* context) const {
  if (!loaded || !parser) return xtc::XtcError::FILE_NOT_FOUND;
  return parser->loadPagePlanePairs(pageIndex, callback, context);
}

uint8_t Xtc::calculateProgress(uint32_t currentPage) const {
  if (!loaded || !parser || parser->getPageCount() == 0) {
    return 0;
  }
  return static_cast<uint8_t>((currentPage + 1) * 100 / parser->getPageCount());
}

xtc::XtcError Xtc::getLastError() const {
  if (!parser) {
    return xtc::XtcError::FILE_NOT_FOUND;
  }
  return parser->getLastError();
}
