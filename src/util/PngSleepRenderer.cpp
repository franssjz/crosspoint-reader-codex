#include "PngSleepRenderer.h"

#include <Arduino.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <MemoryBudget.h>
#include <PNGdec.h>
#include <SdCardFont.h>

#include <cstdio>
#include <new>
#include <string>

#include "util/CprVcodexLogs.h"

namespace {

// PNG_MAX_BUFFERED_PIXELS is raised by platformio.ini, so PNG is substantially
// larger than the library's default configuration. Use the compiled size: an
// approximation can pass the heap check even though new PNG() cannot fit in
// the largest free block.
constexpr uint32_t PNG_DECODER_SIZE = static_cast<uint32_t>(sizeof(PNG));
constexpr uint32_t PNG_DECODER_MIN_FREE_SIZE = PNG_DECODER_SIZE + MemoryBudget::IMAGE_DECODER_HEADROOM;

struct PngOverlayCtx {
  const GfxRenderer* renderer;
  int screenW;
  int screenH;
  int srcWidth;
  int dstWidth;
  int dstX;
  int dstY;
  float yScale;
  int lastDstY;
  int32_t transparentColor;
  PNG* pngObj;
};

void persistPngFailure(const char* stage, const std::string& path, const int code) {
  const auto heap = MemoryBudget::snapshot();
  char message[224];
  snprintf(message, sizeof(message), "PNG %s failed: code=%d free=%u maxAlloc=%u path=%.112s", stage, code,
           heap.freeHeap, heap.maxAllocHeap, path.c_str());
  CPR_VCODEX_LOG_EVENT("SLP", message);
}

void* pngSleepOpen(const char* filename, int32_t* size) {
  // PNGdec retains the handle between callbacks, so it cannot live on this
  // callback's stack. pngSleepClose() owns the matching delete.
  HalFile* f = new (std::nothrow) HalFile();
  if (!f) {
    LOG_ERR("SLP", "Failed to allocate PNG sleep file handle");
    return nullptr;
  }
  if (!Storage.openFileForRead("SLP", std::string(filename), *f)) {
    delete f;
    return nullptr;
  }
  *size = f->size();
  return f;
}

void pngSleepClose(void* handle) {
  auto* f = reinterpret_cast<HalFile*>(handle);
  if (f) {
    f->close();
    delete f;
  }
}

int32_t pngSleepRead(PNGFILE* pFile, uint8_t* pBuf, int32_t len) {
  auto* f = reinterpret_cast<HalFile*>(pFile->fHandle);
  return f ? f->read(pBuf, len) : 0;
}

int32_t pngSleepSeek(PNGFILE* pFile, int32_t pos) {
  auto* f = reinterpret_cast<HalFile*>(pFile->fHandle);
  if (!f) return -1;
  return f->seek(pos);
}

int pngOverlayDraw(PNGDRAW* pDraw) {
  auto* ctx = reinterpret_cast<PngOverlayCtx*>(pDraw->pUser);
  if (!ctx) return 0;

  if (ctx->transparentColor == -2) {
    const int pixelType = pDraw->iPixelType;
    ctx->transparentColor = (pDraw->iHasAlpha && (pixelType == PNG_PIXEL_TRUECOLOR || pixelType == PNG_PIXEL_GRAYSCALE))
                                ? static_cast<int32_t>(ctx->pngObj->getTransparentColor())
                                : -1;
  }

  const int destY = ctx->dstY + static_cast<int>(pDraw->y * ctx->yScale);
  if (destY == ctx->lastDstY) return 1;
  ctx->lastDstY = destY;
  if (destY < 0 || destY >= ctx->screenH) return 1;

  const uint8_t* pixels = pDraw->pPixels;
  const int pixelType = pDraw->iPixelType;
  const int hasAlpha = pDraw->iHasAlpha;

  int srcX = 0;
  int error = 0;
  for (int dstX = 0; dstX < ctx->dstWidth; dstX++) {
    const int outX = ctx->dstX + dstX;
    if (outX >= 0 && outX < ctx->screenW) {
      uint8_t alpha = 255;
      uint8_t gray = 0;

      switch (pixelType) {
        case PNG_PIXEL_TRUECOLOR_ALPHA: {
          const uint8_t* p = &pixels[srcX * 4];
          alpha = p[3];
          gray = static_cast<uint8_t>((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
          break;
        }
        case PNG_PIXEL_GRAY_ALPHA:
          gray = pixels[srcX * 2];
          alpha = pixels[srcX * 2 + 1];
          break;
        case PNG_PIXEL_TRUECOLOR: {
          const uint8_t* p = &pixels[srcX * 3];
          gray = static_cast<uint8_t>((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
          if (ctx->transparentColor >= 0 && p[0] == static_cast<uint8_t>((ctx->transparentColor >> 16) & 0xFF) &&
              p[1] == static_cast<uint8_t>((ctx->transparentColor >> 8) & 0xFF) &&
              p[2] == static_cast<uint8_t>(ctx->transparentColor & 0xFF)) {
            alpha = 0;
          }
          break;
        }
        case PNG_PIXEL_GRAYSCALE:
          gray = pixels[srcX];
          if (ctx->transparentColor >= 0 && gray == static_cast<uint8_t>(ctx->transparentColor & 0xFF)) {
            alpha = 0;
          }
          break;
        case PNG_PIXEL_INDEXED:
          if (pDraw->pPalette) {
            const uint8_t idx = pixels[srcX];
            const uint8_t* p = &pDraw->pPalette[idx * 3];
            gray = static_cast<uint8_t>((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
            if (hasAlpha) alpha = pDraw->pPalette[768 + idx];
          }
          break;
        default:
          gray = pixels[srcX];
          break;
      }

      if (alpha >= 128) {
        ctx->renderer->drawPixel(outX, destY, gray < 128);
      }
    }

    error += ctx->srcWidth;
    while (error >= ctx->dstWidth) {
      error -= ctx->dstWidth;
      srcX++;
    }
  }

  return 1;
}

bool ensurePngDecoderHeap(const GfxRenderer& renderer) {
  const auto before = MemoryBudget::snapshot();
  if (MemoryBudget::hasHeap(before, PNG_DECODER_MIN_FREE_SIZE, PNG_DECODER_SIZE)) {
    return true;
  }

  // Reader activities are already destroyed before the sleep screen enters,
  // but their global font caches survive. Release only rebuildable cache data
  // before asking PNGdec for its contiguous decoder block.
  if (auto* fontCache = renderer.getFontCacheManager()) {
    fontCache->clearCache();
    fontCache->resetStats();
  }

  unsigned releasedSdFonts = 0;
  for (const auto& entry : renderer.getSdCardFonts()) {
    if (!entry.second) {
      continue;
    }
    entry.second->releaseForLowMemory();
    releasedSdFonts++;
  }

  const auto after = MemoryBudget::snapshot();
  LOG_DBG("SLP", "Trimmed sleep PNG caches: free=%u->%u maxAlloc=%u->%u sdFonts=%u", before.freeHeap, after.freeHeap,
          before.maxAllocHeap, after.maxAllocHeap, releasedSdFonts);
  return MemoryBudget::hasHeapForImageDecoder("SLP", "PNG sleep image", PNG_DECODER_SIZE);
}

}  // namespace

bool PngSleepRenderer::drawTransparentPng(const std::string& path, const GfxRenderer& renderer, const int targetX,
                                          const int targetY, const int targetWidth, const int targetHeight) {
  if (targetWidth <= 0 || targetHeight <= 0) {
    return false;
  }

  if (!ensurePngDecoderHeap(renderer)) {
    persistPngFailure("heap", path, -1);
    return false;
  }

  const auto heapBeforeDecoder = MemoryBudget::snapshot();
  LOG_DBG("SLP", "PNG sleep start: free=%u maxAlloc=%u decoder=%u path=%s", heapBeforeDecoder.freeHeap,
          heapBeforeDecoder.maxAllocHeap, PNG_DECODER_SIZE, path.c_str());

  PNG* png = new (std::nothrow) PNG();
  if (!png) {
    LOG_ERR("SLP", "Failed to allocate PNG sleep decoder for %s", path.c_str());
    persistPngFailure("allocation", path, -1);
    return false;
  }

  int rc = png->open(path.c_str(), pngSleepOpen, pngSleepClose, pngSleepRead, pngSleepSeek, pngOverlayDraw);
  if (rc != PNG_SUCCESS) {
    LOG_ERR("SLP", "PNG open failed: %s (rc=%d error=%d)", path.c_str(), rc, png->getLastError());
    // PNGdec leaves an opened callback handle alive if PNGInit fails.
    // Closing here avoids leaking both the HalFile and its SD handle.
    png->close();
    delete png;
    persistPngFailure("open", path, rc);
    return false;
  }

  const int srcW = png->getWidth();
  const int srcH = png->getHeight();
  int dstW = srcW;
  int dstH = srcH;
  float yScale = 1.0f;

  if (srcW > targetWidth || srcH > targetHeight) {
    const float scaleX = static_cast<float>(targetWidth) / static_cast<float>(srcW);
    const float scaleY = static_cast<float>(targetHeight) / static_cast<float>(srcH);
    const float scale = (scaleX < scaleY) ? scaleX : scaleY;
    dstW = static_cast<int>(srcW * scale);
    dstH = static_cast<int>(srcH * scale);
    yScale = static_cast<float>(dstH) / static_cast<float>(srcH);
  }

  PngOverlayCtx ctx{&renderer,
                    renderer.getScreenWidth(),
                    renderer.getScreenHeight(),
                    srcW,
                    dstW,
                    targetX + (targetWidth - dstW) / 2,
                    targetY + (targetHeight - dstH) / 2,
                    yScale,
                    -1,
                    -2,
                    png};

  rc = png->decode(&ctx, 0);
  png->close();
  delete png;
  if (rc != PNG_SUCCESS) {
    LOG_ERR("SLP", "PNG decode failed: %s (%d)", path.c_str(), rc);
    persistPngFailure("decode", path, rc);
  }
  return rc == PNG_SUCCESS;
}
