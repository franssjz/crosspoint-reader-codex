#include "GfxRenderer.h"

#include <BuildScratch.h>
#include <FontDecompressor.h>
#include <HalGPIO.h>
#include <Logging.h>
#include <SdCardFont.h>
#include <Utf8.h>

#include <algorithm>
#include <cassert>
#include <climits>

#include "FontCacheManager.h"
#include "SmallCaps.h"

namespace {

std::vector<uint8_t> invertMonochromeBitmap(const uint8_t* bitmap, size_t size) {
  std::vector<uint8_t> inverted(size);
  for (size_t i = 0; i < size; ++i) {
    inverted[i] = static_cast<uint8_t>(~bitmap[i]);
  }
  return inverted;
}

/**
 * Resolves the requested style to the best available style in the given SD card font.
 * Falls back gracefully when the font lacks the requested variant.
 */
uint8_t resolveSdCardStyle(const SdCardFont& font, const EpdFontFamily::Style style) {
  return font.resolveStyle(static_cast<uint8_t>(style));
}

int scaleSmallCapsMetric(const int value) { return value * 3 / 4; }

int smallCapsScaledExtent(const int value) { return (value * 3 + 3) / 4; }

int scaled75SourceEnd(const int dst, const int srcLimit) {
  const int srcStart = dst * 4 / 3;
  return std::min(srcLimit, std::max(srcStart + 1, ((dst + 1) * 4 + 2) / 3));
}

}  // namespace

const uint8_t* GfxRenderer::getGlyphBitmap(const EpdFontData* fontData, const EpdGlyph* glyph) const {
  if (fontData->groups != nullptr) {
    auto* fd = fontCacheManager_ ? fontCacheManager_->getDecompressor() : nullptr;
    if (!fd) {
      LOG_ERR("GFX", "Compressed font but no FontDecompressor set");
      return nullptr;
    }
    uint32_t glyphIndex = static_cast<uint32_t>(glyph - fontData->glyph);
    // For page-buffer hits the pointer is stable for the page lifetime.
    // For hot-group hits it is valid only until the next getBitmap() call — callers
    // must consume it (draw the glyph) before requesting another bitmap.
    return fd->getBitmap(fontData, glyph, glyphIndex);
  }
  if (fontData->glyphMissCtx) {
    auto* sdFont = SdCardFont::fromMissCtx(fontData->glyphMissCtx);
    if (sdFont->isOverflowGlyph(glyph)) {
      return sdFont->getOverflowBitmap(glyph);
    }
    return sdFont->miniGlyphBitmap(fontData->glyphMissCtx, glyph->dataOffset);
  }
  return &fontData->bitmap[glyph->dataOffset];
}

void GfxRenderer::ensureSdCardFontReady(int fontId, const char* utf8Text, uint8_t styleMask) const {
  auto it = sdCardFonts_.find(fontId);
  if (it == sdCardFonts_.end()) {
    return;
  }

  const int missed = it->second->buildAdvanceTable(utf8Text, styleMask);
  if (missed > 0) {
    LOG_DBG("GFX", "ensureSdCardFontReady: %d glyph(s) not found", missed);
  }
}

void GfxRenderer::ensureSdCardFontReady(int fontId, const std::deque<std::string>& words, bool includeHyphen,
                                        uint8_t styleMask) const {
  auto it = sdCardFonts_.find(fontId);
  if (it == sdCardFonts_.end()) {
    return;
  }

  const int missed = it->second->buildAdvanceTable(words, includeHyphen, styleMask);
  if (missed > 0) {
    LOG_DBG("GFX", "ensureSdCardFontReady: %d glyph(s) not found", missed);
  }
}

bool GfxRenderer::releaseSdCardFontForLowMemory(int fontId) const {
  auto it = sdCardFonts_.find(fontId);
  if (it == sdCardFonts_.end()) {
    return false;
  }

  it->second->releaseForLowMemory();
  return true;
}

void GfxRenderer::begin() {
  frameBuffer = display.getFrameBuffer();
  if (!frameBuffer) {
    LOG_ERR("GFX", "!! No framebuffer");
    assert(false);
  }
  panelWidth = display.getDisplayWidth();
  panelHeight = display.getDisplayHeight();
  panelWidthBytes = display.getDisplayWidthBytes();
  frameBufferSize = display.getBufferSize();
  bwBufferChunks.assign((frameBufferSize + BW_BUFFER_CHUNK_SIZE - 1) / BW_BUFFER_CHUNK_SIZE, nullptr);
}

void GfxRenderer::releaseFrameBufferForBuild() {
  if (!frameBuffer) return;
  buildscratch::lend(frameBuffer, frameBufferSize);
  frameBuffer = nullptr;
}

bool GfxRenderer::restoreFrameBufferAfterBuild() {
  buildscratch::reclaim();
  frameBuffer = display.getFrameBuffer();
  if (frameBuffer) memset(frameBuffer, 0xFF, frameBufferSize);
  return frameBuffer != nullptr;
}

GfxRenderer::FrameBufferLoan::FrameBufferLoan(GfxRenderer& renderer) : renderer_(renderer) {
  if (!renderer_.hasFrameBuffer()) return;
  renderer_.releaseFrameBufferForBuild();
  active_ = true;
}

void GfxRenderer::FrameBufferLoan::end() {
  if (!active_) return;
  active_ = false;
  if (!renderer_.restoreFrameBufferAfterBuild()) {
    LOG_ERR("GFX", "Framebuffer restore failed - restarting");
    ESP.restart();
  }
}

bool GfxRenderer::isFontCacheScanning() const { return fontCacheManager_ && fontCacheManager_->isScanning(); }

void GfxRenderer::insertFont(const int fontId, EpdFontFamily font) {
  const auto it = fontMap.find(fontId);
  if (it != fontMap.end()) {
    it->second = font;
  } else {
    fontMap.emplace(fontId, font);
  }

  if (fontCacheManager_) {
    fontCacheManager_->clearCache();
  }
}

int GfxRenderer::resolveTextFontId(const int fontId, const char* text, const EpdFontFamily::Style style) const {
  if (!text || !*text) return fontId;
  const auto fallbackId = fallbackFontMap_.find(fontId);
  if (fallbackId == fallbackFontMap_.end()) return fontId;
  const auto primary = fontMap.find(fontId);
  const auto fallback = fontMap.find(fallbackId->second);
  if (primary == fontMap.end() || fallback == fontMap.end()) return fontId;

  const char* cursor = text;
  while (const uint32_t cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&cursor))) {
    if (utf8IsCjkCodepoint(cp) && !primary->second.hasCodepoint(cp, style) &&
        fallback->second.hasCodepoint(cp, style)) {
      return fallbackId->second;
    }
  }
  return fontId;
}

void GfxRenderer::ensureSdGlyphsResident(const int fontId, const char* text, const EpdFontFamily::Style style,
                                         const bool metadataOnly) const {
  const auto it = sdCardFonts_.find(fontId);
  if (it == sdCardFonts_.end()) return;
  const uint8_t styleMask = static_cast<uint8_t>(1u << (static_cast<uint8_t>(style) & 0x03));
  it->second->prewarm(text, styleMask, metadataOnly, /*loadKernLig=*/false);
}

void GfxRenderer::prewarmFallbackText(const int fontId, const TextGetter getter, const void* ctx,
                                      const uint32_t textCount, const EpdFontFamily::Style style) const {
  if (!getter || textCount == 0) return;
  int fallbackFontId = fontId;
  for (uint32_t i = 0; i < textCount && fallbackFontId == fontId; i++) {
    const char* text = getter(ctx, i);
    if (text && *text) fallbackFontId = resolveTextFontId(fontId, text, style);
  }
  const auto it = sdCardFonts_.find(fallbackFontId);
  if (fallbackFontId == fontId || it == sdCardFonts_.end()) return;
  const uint8_t styleMask = static_cast<uint8_t>(1u << (static_cast<uint8_t>(style) & 0x03));
  it->second->prewarm(getter, ctx, textCount, styleMask, /*metadataOnly=*/false, /*loadKernLig=*/false);
}

// Translate logical (x,y) coordinates to physical panel coordinates based on current orientation
// This should always be inlined for better performance
static inline void rotateCoordinates(const GfxRenderer::Orientation orientation, const int x, const int y, int* phyX,
                                     int* phyY, const uint16_t panelWidth, const uint16_t panelHeight) {
  switch (orientation) {
    case GfxRenderer::Portrait: {
      // Logical portrait (480x800) → panel (800x480)
      // Rotation: 90 degrees clockwise
      *phyX = y;
      *phyY = panelHeight - 1 - x;
      break;
    }
    case GfxRenderer::LandscapeClockwise: {
      // Logical landscape (800x480) rotated 180 degrees (swap top/bottom and left/right)
      *phyX = panelWidth - 1 - x;
      *phyY = panelHeight - 1 - y;
      break;
    }
    case GfxRenderer::PortraitInverted: {
      // Logical portrait (480x800) → panel (800x480)
      // Rotation: 90 degrees counter-clockwise
      *phyX = panelWidth - 1 - y;
      *phyY = x;
      break;
    }
    case GfxRenderer::LandscapeCounterClockwise: {
      // Logical landscape (800x480) aligned with panel orientation
      *phyX = x;
      *phyY = y;
      break;
    }
  }
}

enum class TextRotation { None, Rotated90CW };

// Shared glyph rendering logic for normal and rotated text.
// Coordinate mapping and cursor advance direction are selected at compile time via the template parameter.
// Render a glyph at 50% scale. Used for SUP/SUB style bits.
//
// Each destination pixel represents a 2x2 source block. Drawing when that block
// contains ink preserves thin strokes that nearest-neighbor sampling can skip.
//
// The advance width is also halved in drawText() so layout reserves exactly the right
// horizontal space for the scaled glyph.
static void renderCharScaled(const GfxRenderer& renderer, GfxRenderer::RenderMode renderMode,
                             const EpdFontFamily& fontFamily, const uint32_t cp, int cursorX, int cursorY,
                             const bool pixelState, const EpdFontFamily::Style style) {
  const EpdGlyph* glyph = fontFamily.getGlyph(cp, style);
  if (!glyph) return;

  const EpdFontData* fontData = fontFamily.getData(style);
  const uint8_t* bitmap = renderer.getGlyphBitmap(fontData, glyph);
  if (!bitmap) return;

  const int srcW = glyph->width;
  const int srcH = glyph->height;
  const int dstW = (srcW + 1) / 2;  // ceil so odd-width glyphs aren't clipped
  const int dstH = (srcH + 1) / 2;
  // Scale the glyph bearing by the same factor so the scaled glyph sits at the correct
  // pixel offset from the (already-shifted) cursor position.
  const int baseX = cursorX + glyph->left / 2;
  const int baseY = cursorY - glyph->top / 2;

  if (fontData->is2Bit) {
    // 2-bit packed format: 4 pixels per byte, MSB first, 2 bits per pixel.
    // raw value: 0=white, 1=light-gray, 2=dark-gray, 3=black.
    for (int dstY = 0; dstY < dstH; dstY++) {
      const int srcY = dstY * 2;
      for (int dstX = 0; dstX < dstW; dstX++) {
        const int srcX = dstX * 2;
        uint8_t coverage = 0;
        uint8_t maxRaw = 0;
        for (int sampleY = 0; sampleY < 2 && srcY + sampleY < srcH; sampleY++) {
          for (int sampleX = 0; sampleX < 2 && srcX + sampleX < srcW; sampleX++) {
            const int pos = (srcY + sampleY) * srcW + srcX + sampleX;
            const uint8_t byte = bitmap[pos >> 2];
            const uint8_t raw = (byte >> ((3 - (pos & 3)) * 2)) & 0x3;
            coverage += raw;
            if (raw > maxRaw) maxRaw = raw;
          }
        }
        if (maxRaw >= 2 || coverage >= 2) {
          renderer.drawPixel(baseX + dstX, baseY + dstY, pixelState);
        }
      }
    }
  } else {
    // 1-bit packed format: 8 pixels per byte, MSB first.
    for (int dstY = 0; dstY < dstH; dstY++) {
      const int srcY = dstY * 2;
      for (int dstX = 0; dstX < dstW; dstX++) {
        const int srcX = dstX * 2;
        bool hasInk = false;
        for (int sampleY = 0; sampleY < 2 && srcY + sampleY < srcH; sampleY++) {
          for (int sampleX = 0; sampleX < 2 && srcX + sampleX < srcW; sampleX++) {
            const int pos = (srcY + sampleY) * srcW + srcX + sampleX;
            const uint8_t byte = bitmap[pos >> 3];
            const uint8_t bit = 7 - (pos & 7);
            if ((byte >> bit) & 1) {
              hasInk = true;
            }
          }
        }
        if (hasInk) {
          renderer.drawPixel(baseX + dstX, baseY + dstY, pixelState);
        }
      }
    }
  }
}

// Render a lowercase codepoint as its uppercase glyph at 75%. This is large
// enough to retain serif detail while remaining visibly distinct from capitals.
static void renderCharSmallCaps(const GfxRenderer& renderer, const GfxRenderer::RenderMode renderMode,
                                const EpdFontFamily& fontFamily, const uint32_t cp, const int cursorX,
                                const int cursorY, const bool pixelState, const EpdFontFamily::Style style) {
  const EpdGlyph* glyph = fontFamily.getGlyph(cp, style);
  if (!glyph) return;
  const EpdFontData* fontData = fontFamily.getData(style);
  const uint8_t* bitmap = renderer.getGlyphBitmap(fontData, glyph);
  if (!bitmap) return;

  const int srcW = glyph->width;
  const int srcH = glyph->height;
  const int dstW = smallCapsScaledExtent(srcW);
  const int dstH = smallCapsScaledExtent(srcH);
  const int baseX = cursorX + scaleSmallCapsMetric(glyph->left);
  const int baseY = cursorY - scaleSmallCapsMetric(glyph->top);

  for (int dstY = 0; dstY < dstH; ++dstY) {
    const int srcY = dstY * 4 / 3;
    const int srcYEnd = scaled75SourceEnd(dstY, srcH);
    for (int dstX = 0; dstX < dstW; ++dstX) {
      const int srcX = dstX * 4 / 3;
      const int srcXEnd = scaled75SourceEnd(dstX, srcW);
      uint8_t maxRaw = 0;
      for (int sampleY = srcY; sampleY < srcYEnd; ++sampleY) {
        for (int sampleX = srcX; sampleX < srcXEnd; ++sampleX) {
          const int pos = sampleY * srcW + sampleX;
          const uint8_t raw = fontData->is2Bit
                                  ? static_cast<uint8_t>((bitmap[pos >> 2] >> ((3 - (pos & 3)) * 2)) & 0x3)
                                  : static_cast<uint8_t>((bitmap[pos >> 3] >> (7 - (pos & 7))) & 1 ? 3 : 0);
          maxRaw = std::max(maxRaw, raw);
        }
      }

      bool draw = false;
      if (renderMode == GfxRenderer::BW) {
        draw = maxRaw != 0;
      } else {
        const uint8_t bmpVal = 3 - maxRaw;
        const uint8_t darkness = renderer.getTextDarkness();
        if (renderMode == GfxRenderer::GRAYSCALE_MSB) {
          draw = darkness == 1 ? bmpVal == 2 : (bmpVal == 1 || bmpVal == 2);
        } else if (renderMode == GfxRenderer::GRAYSCALE_LSB) {
          draw = darkness <= 1 ? bmpVal == 1 : (bmpVal == 1 || bmpVal == 2);
        }
      }
      if (draw) renderer.drawPixel(baseX + dstX, baseY + dstY, pixelState);
    }
  }
}

template <TextRotation rotation = TextRotation::None>
static void renderCharImpl(const GfxRenderer& renderer, GfxRenderer::RenderMode renderMode,
                           const EpdFontFamily& fontFamily, const uint32_t cp, int cursorX, int cursorY,
                           const bool pixelState, const EpdFontFamily::Style style) {
  const EpdGlyph* glyph = fontFamily.getGlyph(cp, style);
  if (!glyph) {
    LOG_ERR("GFX", "No glyph for codepoint %d", cp);
    return;
  }

  const EpdFontData* fontData = fontFamily.getData(style);
  const bool is2Bit = fontData->is2Bit;
  const uint8_t width = glyph->width;
  const uint8_t height = glyph->height;
  const int left = glyph->left;
  const int top = glyph->top;

  const uint8_t* bitmap = renderer.getGlyphBitmap(fontData, glyph);

  if (bitmap != nullptr) {
    // For Normal:  outer loop advances screenY, inner loop advances screenX
    // For Rotated: outer loop advances screenX, inner loop advances screenY (in reverse)
    int outerBase, innerBase;
    if constexpr (rotation == TextRotation::Rotated90CW) {
      outerBase = cursorX + fontData->ascender - top;  // screenX = outerBase + glyphY
      innerBase = cursorY - left;                      // screenY = innerBase - glyphX
    } else {
      outerBase = cursorY - top;   // screenY = outerBase + glyphY
      innerBase = cursorX + left;  // screenX = innerBase + glyphX
    }

    if (is2Bit) {
      int pixelPosition = 0;
      for (int glyphY = 0; glyphY < height; glyphY++) {
        const int outerCoord = outerBase + glyphY;
        for (int glyphX = 0; glyphX < width; glyphX++, pixelPosition++) {
          int screenX, screenY;
          if constexpr (rotation == TextRotation::Rotated90CW) {
            screenX = outerCoord;
            screenY = innerBase - glyphX;
          } else {
            screenX = innerBase + glyphX;
            screenY = outerCoord;
          }

          const uint8_t byte = bitmap[pixelPosition >> 2];
          const uint8_t bit_index = (3 - (pixelPosition & 3)) * 2;
          // the direct bit from the font is 0 -> white, 1 -> light gray, 2 -> dark gray, 3 -> black
          // we swap this to better match the way images and screen think about colors:
          // 0 -> black, 1 -> dark grey, 2 -> light grey, 3 -> white
          uint8_t bmpVal = 3 - ((byte >> bit_index) & 0x3);

          const uint8_t darkness = renderer.getTextDarkness();

          if (renderMode == GfxRenderer::BW) {
            if (bmpVal < 3) {
              renderer.drawPixel(screenX, screenY, pixelState);
            }
          } else {
            bool hitMsb = false;
            bool hitLsb = false;

            switch (darkness) {
              case 1:  // Crisp: keep the old lighter grayscale overlay.
                hitMsb = (bmpVal == 2);
                hitLsb = (bmpVal == 1);
                break;
              case 0:  // Normal: CrossInk-style solid text with smooth AA.
                hitMsb = (bmpVal == 1 || bmpVal == 2);
                hitLsb = (bmpVal == 1);
                break;
              case 2:  // Dark: promote both AA buckets to darker ink.
                hitMsb = (bmpVal == 1 || bmpVal == 2);
                hitLsb = (bmpVal == 1 || bmpVal == 2);
                break;
              default:  // Extra Dark: keep maximum AA darkening without touching white.
                hitMsb = (bmpVal == 1 || bmpVal == 2);
                hitLsb = (bmpVal == 1 || bmpVal == 2);
                break;
            }

            if (renderMode == GfxRenderer::GRAYSCALE_MSB && hitMsb) {
              renderer.drawPixel(screenX, screenY, false);
            } else if (renderMode == GfxRenderer::GRAYSCALE_LSB && hitLsb) {
              renderer.drawPixel(screenX, screenY, false);
            }
          }
        }
      }
    } else {
      int pixelPosition = 0;
      for (int glyphY = 0; glyphY < height; glyphY++) {
        const int outerCoord = outerBase + glyphY;
        for (int glyphX = 0; glyphX < width; glyphX++, pixelPosition++) {
          int screenX, screenY;
          if constexpr (rotation == TextRotation::Rotated90CW) {
            screenX = outerCoord;
            screenY = innerBase - glyphX;
          } else {
            screenX = innerBase + glyphX;
            screenY = outerCoord;
          }

          const uint8_t byte = bitmap[pixelPosition >> 3];
          const uint8_t bit_index = 7 - (pixelPosition & 7);

          if ((byte >> bit_index) & 1) {
            renderer.drawPixel(screenX, screenY, pixelState);
          }
        }
      }
    }
  }
}

// IMPORTANT: This function is in critical rendering path and is called for every pixel. Please keep it as simple and
// efficient as possible.
void GfxRenderer::drawPixelRaw(const int x, const int y, const bool state) const {
  int phyX = 0;
  int phyY = 0;

  // Note: this call should be inlined for better performance
  rotateCoordinates(orientation, x, y, &phyX, &phyY, panelWidth, panelHeight);

  // Bounds checking against runtime panel dimensions
  if (phyX < 0 || phyX >= panelWidth || phyY < 0 || phyY >= panelHeight) {
    LOG_ERR("GFX", "!! Outside range (%d, %d) -> (%d, %d)", x, y, phyX, phyY);
    return;
  }

  uint8_t* target = frameBuffer;
  uint32_t rowY = static_cast<uint32_t>(phyY);
  if (_stripActive) {
    if (phyY < _stripY0 || phyY >= _stripY0 + _stripRows) {
      return;
    }
    target = _stripBuf;
    rowY = static_cast<uint32_t>(phyY - _stripY0);
  }

  // Calculate byte position and bit position
  const uint32_t byteIndex = rowY * panelWidthBytes + (phyX / 8);
  const uint8_t bitPosition = 7 - (phyX % 8);  // MSB first

  if (state) {
    target[byteIndex] &= ~(1 << bitPosition);  // Clear bit
  } else {
    target[byteIndex] |= 1 << bitPosition;  // Set bit
  }
}

void GfxRenderer::drawPixel(const int x, const int y, const bool state) const {
  const bool effectiveState = (darkMode && renderMode == BW) ? !state : state;
  drawPixelRaw(x, y, effectiveState);
}

int GfxRenderer::getTextWidth(const int fontId, const char* text, const EpdFontFamily::Style style) const {
  if ((style & EpdFontFamily::SMALL_CAPS) != 0) return getTextAdvanceX(fontId, text, style);
  const int resolvedFontId = resolveTextFontId(fontId, text, style);
  if (resolvedFontId != fontId) ensureSdGlyphsResident(resolvedFontId, text, style, true);
  const auto fontIt = fontMap.find(resolvedFontId);
  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "Font %d not found", resolvedFontId);
    return 0;
  }

  int w = 0, h = 0;
  fontIt->second.getTextDimensions(text, &w, &h, style);
  return w;
}

void GfxRenderer::drawCenteredText(const int fontId, const int y, const char* text, const bool black,
                                   const EpdFontFamily::Style style) const {
  const int x = (getScreenWidth() - getTextWidth(fontId, text, style)) / 2;
  drawText(fontId, x, y, text, black, style);
}

void GfxRenderer::drawText(const int fontId, const int x, const int y, const char* text, const bool black,
                           const EpdFontFamily::Style style) const {
  // cannot draw a NULL / empty string
  if (text == nullptr || *text == '\0') {
    return;
  }

  const int resolvedFontId = resolveTextFontId(fontId, text, style);
  if (resolvedFontId != fontId) ensureSdGlyphsResident(resolvedFontId, text, style, false);
  int yPos = y + getFontAscenderSize(resolvedFontId);
  if (resolvedFontId != fontId) yPos += (getLineHeight(fontId) - getLineHeight(resolvedFontId)) / 2;
  int lastBaseX = x;
  int lastBaseLeft = 0;
  int lastBaseWidth = 0;
  int lastBaseTop = 0;
  int32_t prevAdvanceFP = 0;  // 12.4 fixed-point: prev glyph's advance + next kern for snap

  if (fontCacheManager_ && fontCacheManager_->isScanning()) {
    fontCacheManager_->recordText(text, resolvedFontId, style);
    return;
  }

  const auto fontIt = fontMap.find(resolvedFontId);
  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "Font %d not found", resolvedFontId);
    return;
  }
  const auto& font = fontIt->second;

  uint32_t cp;
  uint32_t prevCp = 0;
  bool prevScaledSmallCap = false;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    if (utf8IsCombiningMark(cp)) {
      const EpdGlyph* combiningGlyph = font.getGlyph(cp, style);
      if (!combiningGlyph) continue;
      const int raiseBy = combiningMark::raiseAboveBase(combiningGlyph->top, combiningGlyph->height, lastBaseTop);
      const int combiningX = combiningMark::centerOver(lastBaseX, lastBaseLeft, lastBaseWidth, combiningGlyph->left,
                                                       combiningGlyph->width);
      renderCharImpl<TextRotation::None>(*this, renderMode, font, cp, combiningX, yPos - raiseBy, black, style);
      continue;
    }

    const bool scaledSmallCap = smallcaps::isLowercase(style, cp);
    if (scaledSmallCap) {
      cp = smallcaps::toUppercase(cp);
    } else {
      cp = font.applyLigatures(cp, text, style);
    }

    // Differential rounding: snap (previous advance + current kern) as one unit so
    // identical character pairs always produce the same pixel step regardless of
    // where they fall on the line.
    if (prevCp != 0) {
      int32_t kernFP = font.getKerning(prevCp, cp, style);  // 4.4 fixed-point kern
      if (prevScaledSmallCap || scaledSmallCap) kernFP = smallcaps::scaleAdvance(kernFP);
      lastBaseX += fp4::toPixel(prevAdvanceFP + kernFP);  // snap 12.4 fixed-point to nearest pixel
    }

    const EpdGlyph* glyph = font.getGlyph(cp, style);

    lastBaseLeft = glyph ? glyph->left : 0;
    lastBaseWidth = glyph ? glyph->width : 0;
    lastBaseTop = glyph ? glyph->top : 0;
    prevAdvanceFP = glyph ? glyph->advanceX : 0;  // 12.4 fixed-point

    const bool isSupSub = (style & (EpdFontFamily::SUP | EpdFontFamily::SUB)) != 0;
    if (isSupSub) {
      // Halve the advance so the cursor advances by the same amount the scaled glyph
      // actually occupies, keeping spacing correct without needing a separate smaller font.
      prevAdvanceFP = (prevAdvanceFP + 1) / 2;
    } else if (scaledSmallCap) {
      prevAdvanceFP = smallcaps::scaleAdvance(prevAdvanceFP);
    }

    if (isSupSub) {
      // yPos already carries the vertical offset applied by TextBlock::render().
      renderCharScaled(*this, renderMode, font, cp, lastBaseX, yPos, black, style);
    } else if (scaledSmallCap) {
      renderCharSmallCaps(*this, renderMode, font, cp, lastBaseX, yPos, black, style);
    } else {
      renderCharImpl<TextRotation::None>(*this, renderMode, font, cp, lastBaseX, yPos, black, style);
    }
    prevCp = cp;
    prevScaledSmallCap = scaledSmallCap;
  }
}

void GfxRenderer::drawLine(int x1, int y1, int x2, int y2, const bool state) const {
  if (fontCacheManager_ && fontCacheManager_->isScanning()) return;
  if (x1 == x2) {
    if (y2 < y1) {
      std::swap(y1, y2);
    }
    for (int y = y1; y <= y2; y++) {
      drawPixel(x1, y, state);
    }
  } else if (y1 == y2) {
    if (x2 < x1) {
      std::swap(x1, x2);
    }
    for (int x = x1; x <= x2; x++) {
      drawPixel(x, y1, state);
    }
  } else {
    // Bresenham's line algorithm — integer arithmetic only
    int dx = x2 - x1;
    int dy = y2 - y1;
    int sx = (dx > 0) ? 1 : -1;
    int sy = (dy > 0) ? 1 : -1;
    dx = sx * dx;  // abs
    dy = sy * dy;  // abs

    int err = dx - dy;
    while (true) {
      drawPixel(x1, y1, state);
      if (x1 == x2 && y1 == y2) break;
      int e2 = 2 * err;
      if (e2 > -dy) {
        err -= dy;
        x1 += sx;
      }
      if (e2 < dx) {
        err += dx;
        y1 += sy;
      }
    }
  }
}

void GfxRenderer::drawLine(int x1, int y1, int x2, int y2, const int lineWidth, const bool state) const {
  for (int i = 0; i < lineWidth; i++) {
    drawLine(x1, y1 + i, x2, y2 + i, state);
  }
}

void GfxRenderer::drawRect(const int x, const int y, const int width, const int height, const bool state) const {
  drawLine(x, y, x + width - 1, y, state);
  drawLine(x + width - 1, y, x + width - 1, y + height - 1, state);
  drawLine(x + width - 1, y + height - 1, x, y + height - 1, state);
  drawLine(x, y, x, y + height - 1, state);
}

// Border is inside the rectangle
void GfxRenderer::drawRect(const int x, const int y, const int width, const int height, const int lineWidth,
                           const bool state) const {
  for (int i = 0; i < lineWidth; i++) {
    drawLine(x + i, y + i, x + width - i, y + i, state);
    drawLine(x + width - i, y + i, x + width - i, y + height - i, state);
    drawLine(x + width - i, y + height - i, x + i, y + height - i, state);
    drawLine(x + i, y + height - i, x + i, y + i, state);
  }
}

void GfxRenderer::drawArc(const int maxRadius, const int cx, const int cy, const int xDir, const int yDir,
                          const int lineWidth, const bool state) const {
  const int stroke = std::min(lineWidth, maxRadius);
  const int innerRadius = std::max(maxRadius - stroke, 0);
  const int outerRadiusSq = maxRadius * maxRadius;
  const int innerRadiusSq = innerRadius * innerRadius;
  for (int dy = 0; dy <= maxRadius; ++dy) {
    for (int dx = 0; dx <= maxRadius; ++dx) {
      const int distSq = dx * dx + dy * dy;
      if (distSq > outerRadiusSq || distSq < innerRadiusSq) {
        continue;
      }
      const int px = cx + xDir * dx;
      const int py = cy + yDir * dy;
      drawPixel(px, py, state);
    }
  }
};

// Border is inside the rectangle, rounded corners
void GfxRenderer::drawRoundedRect(const int x, const int y, const int width, const int height, const int lineWidth,
                                  const int cornerRadius, bool state) const {
  drawRoundedRect(x, y, width, height, lineWidth, cornerRadius, true, true, true, true, state);
}

// Border is inside the rectangle, rounded corners
void GfxRenderer::drawRoundedRect(const int x, const int y, const int width, const int height, const int lineWidth,
                                  const int cornerRadius, bool roundTopLeft, bool roundTopRight, bool roundBottomLeft,
                                  bool roundBottomRight, bool state) const {
  if (lineWidth <= 0 || width <= 0 || height <= 0) {
    return;
  }

  const int maxRadius = std::min({cornerRadius, width / 2, height / 2});
  if (maxRadius <= 0) {
    drawRect(x, y, width, height, lineWidth, state);
    return;
  }

  const int stroke = std::min(lineWidth, maxRadius);
  const int right = x + width - 1;
  const int bottom = y + height - 1;

  const int horizontalWidth = width - 2 * maxRadius;
  if (horizontalWidth > 0) {
    if (roundTopLeft || roundTopRight) {
      fillRect(x + maxRadius, y, horizontalWidth, stroke, state);
    }
    if (roundBottomLeft || roundBottomRight) {
      fillRect(x + maxRadius, bottom - stroke + 1, horizontalWidth, stroke, state);
    }
  }

  const int verticalHeight = height - 2 * maxRadius;
  if (verticalHeight > 0) {
    if (roundTopLeft || roundBottomLeft) {
      fillRect(x, y + maxRadius, stroke, verticalHeight, state);
    }
    if (roundTopRight || roundBottomRight) {
      fillRect(right - stroke + 1, y + maxRadius, stroke, verticalHeight, state);
    }
  }

  if (roundTopLeft) {
    drawArc(maxRadius, x + maxRadius, y + maxRadius, -1, -1, lineWidth, state);
  }
  if (roundTopRight) {
    drawArc(maxRadius, right - maxRadius, y + maxRadius, 1, -1, lineWidth, state);
  }
  if (roundBottomRight) {
    drawArc(maxRadius, right - maxRadius, bottom - maxRadius, 1, 1, lineWidth, state);
  }
  if (roundBottomLeft) {
    drawArc(maxRadius, x + maxRadius, bottom - maxRadius, -1, 1, lineWidth, state);
  }
}

void GfxRenderer::fillRect(const int x, const int y, const int width, const int height, const bool state) const {
  if (state) {
    fillRectImpl<Color::Black>(x, y, width, height);
  } else {
    fillRectImpl<Color::White>(x, y, width, height);
  }
}

// NOTE: Those are in critical path, and need to be templated to avoid runtime checks for every pixel.
// Any branching must be done outside the loops to avoid performance degradation.
template <>
void GfxRenderer::drawPixelDither<Color::Clear>(const int x, const int y) const {
  // Do nothing
}

template <>
void GfxRenderer::drawPixelDither<Color::Black>(const int x, const int y) const {
  drawPixel(x, y, true);
}

template <>
void GfxRenderer::drawPixelDither<Color::White>(const int x, const int y) const {
  drawPixel(x, y, false);
}

template <>
void GfxRenderer::drawPixelDither<Color::LightGray>(const int x, const int y) const {
  drawPixel(x, y, x % 2 == 0 && y % 2 == 0);
}

template <>
void GfxRenderer::drawPixelDither<Color::MediumGray>(const int x, const int y) const {
  static constexpr uint8_t BAYER_4X4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  drawPixel(x, y, BAYER_4X4[y & 3][x & 3] < static_cast<uint8_t>(Color::MediumGray));
}

template <>
void GfxRenderer::drawPixelDither<Color::DarkGray>(const int x, const int y) const {
  drawPixel(x, y, (x + y) % 2 == 0);  // TODO: maybe find a better pattern?
}

template <>
void GfxRenderer::drawPixelDither<Color::ExtraDarkGray>(const int x, const int y) const {
  static constexpr uint8_t BAYER_4X4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  drawPixel(x, y, BAYER_4X4[y & 3][x & 3] < static_cast<uint8_t>(Color::ExtraDarkGray));
}

void GfxRenderer::fillRectDither(const int x, const int y, const int width, const int height, Color color) const {
  switch (color) {
    case Color::Clear:
      break;
    case Color::Black:
      fillRectImpl<Color::Black>(x, y, width, height);
      break;
    case Color::White:
      fillRectImpl<Color::White>(x, y, width, height);
      break;
    case Color::LightGray:
      fillRectImpl<Color::LightGray>(x, y, width, height);
      break;
    case Color::MediumGray:
      fillRectImpl<Color::MediumGray>(x, y, width, height);
      break;
    case Color::DarkGray:
      fillRectImpl<Color::DarkGray>(x, y, width, height);
      break;
    case Color::ExtraDarkGray:
      fillRectImpl<Color::ExtraDarkGray>(x, y, width, height);
      break;
  }
}

template <Color color>
void GfxRenderer::fillRectImpl(const int x, const int y, const int width, const int height) const {
  if constexpr (color == Color::Clear) return;
  if (width <= 0 || height <= 0) return;
  if (fontCacheManager_ && fontCacheManager_->isScanning()) return;

  const int screenWidth = getScreenWidth();
  const int screenHeight = getScreenHeight();
  const int logicalX0 = std::max(0, x);
  const int logicalY0 = std::max(0, y);
  const int logicalX1 = std::min(screenWidth, x + width);
  const int logicalY1 = std::min(screenHeight, y + height);
  if (logicalX0 >= logicalX1 || logicalY0 >= logicalY1) return;

  int cornerAX = 0;
  int cornerAY = 0;
  int cornerBX = 0;
  int cornerBY = 0;
  rotateCoordinates(orientation, logicalX0, logicalY0, &cornerAX, &cornerAY, panelWidth, panelHeight);
  rotateCoordinates(orientation, logicalX1 - 1, logicalY1 - 1, &cornerBX, &cornerBY, panelWidth, panelHeight);

  const int physicalX0 = std::min(cornerAX, cornerBX);
  const int physicalX1 = std::max(cornerAX, cornerBX);
  int physicalY0 = std::min(cornerAY, cornerBY);
  int physicalY1 = std::max(cornerAY, cornerBY);
  if (physicalX0 < 0 || physicalX1 >= panelWidth || physicalY0 < 0 || physicalY1 >= panelHeight) return;

  uint8_t* target = getWriteTarget();
  const int originY = getWriteOriginY();
  const int writeRows = getWriteRows();
  physicalY0 = std::max(physicalY0, originY);
  physicalY1 = std::min(physicalY1, originY + writeRows - 1);
  if (physicalY0 > physicalY1) return;

  const int byteStart = physicalX0 >> 3;
  const int byteEnd = physicalX1 >> 3;
  const uint8_t headMask = static_cast<uint8_t>(0xFFu >> (physicalX0 & 7));
  const uint8_t tailMask = static_cast<uint8_t>(0xFFu << (7 - (physicalX1 & 7)));
  const uint32_t panelStride = panelWidthBytes;
  const bool invertForDarkMode = darkMode && renderMode == BW;

  if constexpr (color == Color::Black || color == Color::White) {
    const bool fillBlack = invertForDarkMode ? color == Color::White : color == Color::Black;
    const uint8_t fillByte = fillBlack ? 0x00u : 0xFFu;
    for (int physicalY = physicalY0; physicalY <= physicalY1; ++physicalY) {
      uint8_t* row = target + static_cast<uint32_t>(physicalY - originY) * panelStride;
      if (byteStart == byteEnd) {
        const uint8_t mask = headMask & tailMask;
        if (fillBlack) {
          row[byteStart] &= static_cast<uint8_t>(~mask);
        } else {
          row[byteStart] |= mask;
        }
      } else if (fillBlack) {
        row[byteStart] &= static_cast<uint8_t>(~headMask);
        if (byteEnd > byteStart + 1) {
          memset(row + byteStart + 1, fillByte, byteEnd - byteStart - 1);
        }
        row[byteEnd] &= static_cast<uint8_t>(~tailMask);
      } else {
        row[byteStart] |= headMask;
        if (byteEnd > byteStart + 1) {
          memset(row + byteStart + 1, fillByte, byteEnd - byteStart - 1);
        }
        row[byteEnd] |= tailMask;
      }
    }
  } else {
    static constexpr uint8_t BAYER_4X4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

    int logicalDeltaXPerPhysicalX = 0;
    int logicalDeltaYPerPhysicalX = 0;
    switch (orientation) {
      case Portrait:
        logicalDeltaXPerPhysicalX = 0;
        logicalDeltaYPerPhysicalX = 1;
        break;
      case PortraitInverted:
        logicalDeltaXPerPhysicalX = 0;
        logicalDeltaYPerPhysicalX = -1;
        break;
      case LandscapeClockwise:
        logicalDeltaXPerPhysicalX = -1;
        logicalDeltaYPerPhysicalX = 0;
        break;
      case LandscapeCounterClockwise:
        logicalDeltaXPerPhysicalX = 1;
        logicalDeltaYPerPhysicalX = 0;
        break;
    }

    uint8_t blackMasks[4] = {};
    for (int parityIndex = 0; parityIndex < 4; ++parityIndex) {
      const int samplePhysicalY = physicalY0 + parityIndex;
      int logicalXBase = 0;
      int logicalYBase = 0;
      switch (orientation) {
        case Portrait:
          logicalXBase = panelHeight - 1 - samplePhysicalY;
          logicalYBase = byteStart * 8;
          break;
        case PortraitInverted:
          logicalXBase = samplePhysicalY;
          logicalYBase = panelWidth - 1 - byteStart * 8;
          break;
        case LandscapeClockwise:
          logicalXBase = panelWidth - 1 - byteStart * 8;
          logicalYBase = panelHeight - 1 - samplePhysicalY;
          break;
        case LandscapeCounterClockwise:
          logicalXBase = byteStart * 8;
          logicalYBase = samplePhysicalY;
          break;
      }

      uint8_t mask = 0;
      for (int bit = 0; bit < 8; ++bit) {
        const int logicalX = logicalXBase + bit * logicalDeltaXPerPhysicalX;
        const int logicalY = logicalYBase + bit * logicalDeltaYPerPhysicalX;
        bool isBlack = false;
        if constexpr (color == Color::LightGray) {
          isBlack = ((logicalX & 1) == 0) && ((logicalY & 1) == 0);
        } else if constexpr (color == Color::DarkGray) {
          isBlack = (((logicalX + logicalY) & 1) == 0);
        } else {
          isBlack = BAYER_4X4[logicalY & 3][logicalX & 3] < static_cast<uint8_t>(color);
        }
        if (invertForDarkMode) {
          isBlack = !isBlack;
        }
        if (isBlack) {
          mask |= static_cast<uint8_t>(1u << (7 - bit));
        }
      }
      blackMasks[samplePhysicalY & 3] = mask;
    }

    for (int physicalY = physicalY0; physicalY <= physicalY1; ++physicalY) {
      const uint8_t whiteMask = static_cast<uint8_t>(~blackMasks[physicalY & 3]);
      uint8_t* row = target + static_cast<uint32_t>(physicalY - originY) * panelStride;
      if (byteStart == byteEnd) {
        const uint8_t rectMask = headMask & tailMask;
        row[byteStart] = static_cast<uint8_t>((row[byteStart] & ~rectMask) | (rectMask & whiteMask));
      } else {
        row[byteStart] = static_cast<uint8_t>((row[byteStart] & ~headMask) | (headMask & whiteMask));
        if (byteEnd > byteStart + 1) {
          memset(row + byteStart + 1, whiteMask, byteEnd - byteStart - 1);
        }
        row[byteEnd] = static_cast<uint8_t>((row[byteEnd] & ~tailMask) | (tailMask & whiteMask));
      }
    }
  }
}

template void GfxRenderer::fillRectImpl<Color::Black>(int, int, int, int) const;
template void GfxRenderer::fillRectImpl<Color::White>(int, int, int, int) const;
template void GfxRenderer::fillRectImpl<Color::LightGray>(int, int, int, int) const;
template void GfxRenderer::fillRectImpl<Color::MediumGray>(int, int, int, int) const;
template void GfxRenderer::fillRectImpl<Color::DarkGray>(int, int, int, int) const;
template void GfxRenderer::fillRectImpl<Color::ExtraDarkGray>(int, int, int, int) const;

template <Color color>
void GfxRenderer::fillArc(const int maxRadius, const int cx, const int cy, const int xDir, const int yDir) const {
  const int radiusSq = maxRadius * maxRadius;
  for (int dy = 0; dy <= maxRadius; ++dy) {
    for (int dx = 0; dx <= maxRadius; ++dx) {
      const int distSq = dx * dx + dy * dy;
      const int px = cx + xDir * dx;
      const int py = cy + yDir * dy;
      if (distSq <= radiusSq) {
        drawPixelDither<color>(px, py);
      }
    }
  }
}

void GfxRenderer::fillRoundedRect(const int x, const int y, const int width, const int height, const int cornerRadius,
                                  const Color color) const {
  fillRoundedRect(x, y, width, height, cornerRadius, true, true, true, true, color);
}

void GfxRenderer::maskRoundedRectOutsideCorners(const int x, const int y, const int width, const int height,
                                                const int radius, const Color color) const {
  if (radius <= 0 || color == Color::Clear) {
    return;
  }

  const int rr = radius - 1;
  const int rr2 = rr * rr;
  for (int dy = 0; dy < radius; dy++) {
    for (int dx = 0; dx < radius; dx++) {
      const int tx = rr - dx;
      const int ty = rr - dy;
      if (tx * tx + ty <= rr2) {
        continue;
      }

      if (color == Color::White || color == Color::Black) {
        const bool state = color == Color::Black;
        drawPixel(x + dx, y + dy, state);
        drawPixel(x + width - 1 - dx, y + dy, state);
        drawPixel(x + dx, y + height - 1 - dy, state);
        drawPixel(x + width - 1 - dx, y + height - 1 - dy, state);
      } else if (color == Color::LightGray) {
        drawPixelDither<Color::LightGray>(x + dx, y + dy);
        drawPixelDither<Color::LightGray>(x + width - 1 - dx, y + dy);
        drawPixelDither<Color::LightGray>(x + dx, y + height - 1 - dy);
        drawPixelDither<Color::LightGray>(x + width - 1 - dx, y + height - 1 - dy);
      } else if (color == Color::DarkGray) {
        drawPixelDither<Color::DarkGray>(x + dx, y + dy);
        drawPixelDither<Color::DarkGray>(x + width - 1 - dx, y + dy);
        drawPixelDither<Color::DarkGray>(x + dx, y + height - 1 - dy);
        drawPixelDither<Color::DarkGray>(x + width - 1 - dx, y + height - 1 - dy);
      }
    }
  }
}

void GfxRenderer::fillRoundedRect(const int x, const int y, const int width, const int height, const int cornerRadius,
                                  bool roundTopLeft, bool roundTopRight, bool roundBottomLeft, bool roundBottomRight,
                                  const Color color) const {
  if (width <= 0 || height <= 0) {
    return;
  }

  // Assume if we're not rounding all corners then we are only rounding one side
  const int roundedSides = (!roundTopLeft || !roundTopRight || !roundBottomLeft || !roundBottomRight) ? 1 : 2;
  const int maxRadius = std::min({cornerRadius, width / roundedSides, height / roundedSides});
  if (maxRadius <= 0) {
    fillRectDither(x, y, width, height, color);
    return;
  }

  const int horizontalWidth = width - 2 * maxRadius;
  if (horizontalWidth > 0) {
    fillRectDither(x + maxRadius + 1, y, horizontalWidth - 2, height, color);
  }

  const int leftFillTop = y + (roundTopLeft ? (maxRadius + 1) : 0);
  const int leftFillBottom = y + height - 1 - (roundBottomLeft ? (maxRadius + 1) : 0);
  if (leftFillBottom >= leftFillTop) {
    fillRectDither(x, leftFillTop, maxRadius + 1, leftFillBottom - leftFillTop + 1, color);
  }

  const int rightFillTop = y + (roundTopRight ? (maxRadius + 1) : 0);
  const int rightFillBottom = y + height - 1 - (roundBottomRight ? (maxRadius + 1) : 0);
  if (rightFillBottom >= rightFillTop) {
    fillRectDither(x + width - maxRadius - 1, rightFillTop, maxRadius + 1, rightFillBottom - rightFillTop + 1, color);
  }

  auto fillArcTemplated = [this](int maxRadius, int cx, int cy, int xDir, int yDir, Color color) {
    switch (color) {
      case Color::Clear:
        break;
      case Color::Black:
        fillArc<Color::Black>(maxRadius, cx, cy, xDir, yDir);
        break;
      case Color::White:
        fillArc<Color::White>(maxRadius, cx, cy, xDir, yDir);
        break;
      case Color::LightGray:
        fillArc<Color::LightGray>(maxRadius, cx, cy, xDir, yDir);
        break;
      case Color::DarkGray:
        fillArc<Color::DarkGray>(maxRadius, cx, cy, xDir, yDir);
        break;
    }
  };

  if (roundTopLeft) {
    fillArcTemplated(maxRadius, x + maxRadius, y + maxRadius, -1, -1, color);
  }

  if (roundTopRight) {
    fillArcTemplated(maxRadius, x + width - maxRadius - 1, y + maxRadius, 1, -1, color);
  }

  if (roundBottomRight) {
    fillArcTemplated(maxRadius, x + width - maxRadius - 1, y + height - maxRadius - 1, 1, 1, color);
  }

  if (roundBottomLeft) {
    fillArcTemplated(maxRadius, x + maxRadius, y + height - maxRadius - 1, -1, 1, color);
  }
}

void GfxRenderer::drawImage(const uint8_t bitmap[], const int x, const int y, const int width, const int height) const {
  int rotatedX = 0;
  int rotatedY = 0;
  rotateCoordinates(orientation, x, y, &rotatedX, &rotatedY, panelWidth, panelHeight);
  // Rotate origin corner
  switch (orientation) {
    case Portrait:
      rotatedY = rotatedY - height;
      break;
    case PortraitInverted:
      rotatedX = rotatedX - width;
      break;
    case LandscapeClockwise:
      rotatedY = rotatedY - height;
      rotatedX = rotatedX - width;
      break;
    case LandscapeCounterClockwise:
      break;
  }
  // TODO: Rotate bits
  display.drawImage(bitmap, rotatedX, rotatedY, width, height);
}

void GfxRenderer::drawIcon(const uint8_t bitmap[], const int x, const int y, const int width, const int height) const {
  const int destX = y;
  const int destY = getScreenWidth() - width - x;
  if (!(darkMode && renderMode == BW)) {
    display.drawImageTransparent(bitmap, destX, destY, height, width);
    return;
  }

  const size_t imageWidthBytes = (static_cast<size_t>(height) + 7U) / 8U;
  auto invertedBitmap = invertMonochromeBitmap(bitmap, imageWidthBytes * width);
  display.drawImage(invertedBitmap.data(), destX, destY, height, width);
}

void GfxRenderer::drawIconBlack(const uint8_t bitmap[], const int x, const int y, const int width,
                                const int height) const {
  const int destX = y;
  const int destY = getScreenWidth() - width - x;
  display.drawImageTransparent(bitmap, destX, destY, height, width);
}

void GfxRenderer::drawIconInverted(const uint8_t bitmap[], const int x, const int y, const int width,
                                   const int height) const {
  const int physX = y;
  const int physY = getScreenWidth() - width - x;
  const int imgW = height;
  const int imgH = width;
  const int srcStride = (imgW + 7) / 8;

  if (physX + imgW <= 0 || physX >= panelWidth) return;
  if (physY + imgH <= 0 || physY >= panelHeight) return;

  const int baseByte = (physX >= 0) ? (physX >> 3) : -(((-physX) + 7) >> 3);
  const int bitShift = ((physX % 8) + 8) % 8;
  const int trail = srcStride * 8 - imgW;
  const uint8_t trailMask = static_cast<uint8_t>(0xFF << trail);
  const int lastCol = srcStride - 1;

  for (int row = 0; row < imgH; ++row) {
    const int destY = physY + row;
    if (destY < 0 || destY >= panelHeight) continue;
    const int rowBase = destY * panelWidthBytes;
    const int srcOffset = row * srcStride;

    if (bitShift == 0) {
      for (int col = 0; col < srcStride; ++col) {
        const int dst = baseByte + col;
        if (dst < 0) continue;
        if (dst >= panelWidthBytes) break;
        uint8_t inv = ~bitmap[srcOffset + col];
        if (col == lastCol && trail > 0) inv &= trailMask;
        frameBuffer[rowBase + dst] |= inv;
      }
    } else {
      const int rsh = bitShift;
      const int lsh = 8 - bitShift;
      for (int col = 0; col < srcStride; ++col) {
        uint8_t inv = ~bitmap[srcOffset + col];
        if (col == lastCol && trail > 0) inv &= trailMask;
        const int dstHi = baseByte + col;
        const int dstLo = dstHi + 1;
        if (dstHi >= 0 && dstHi < panelWidthBytes) {
          frameBuffer[rowBase + dstHi] |= static_cast<uint8_t>(inv >> rsh);
        }
        if (dstLo >= 0 && dstLo < panelWidthBytes) {
          frameBuffer[rowBase + dstLo] |= static_cast<uint8_t>(inv << lsh);
        }
      }
    }
  }
}

void GfxRenderer::drawBitmap(const Bitmap& bitmap, const int x, const int y, const int maxWidth, const int maxHeight,
                             const float cropX, const float cropY) const {
  if (fontCacheManager_ && fontCacheManager_->isScanning()) return;
  // For 1-bit bitmaps, use optimized 1-bit rendering path (no crop support for 1-bit)
  if (bitmap.is1Bit() && cropX == 0.0f && cropY == 0.0f) {
    drawBitmap1Bit(bitmap, x, y, maxWidth, maxHeight);
    return;
  }

  float scale = 1.0f;
  bool isScaled = false;
  int cropPixX = std::floor(bitmap.getWidth() * cropX / 2.0f);
  int cropPixY = std::floor(bitmap.getHeight() * cropY / 2.0f);
  LOG_DBG("GFX", "Cropping %dx%d by %dx%d pix, is %s", bitmap.getWidth(), bitmap.getHeight(), cropPixX, cropPixY,
          bitmap.isTopDown() ? "top-down" : "bottom-up");

  const float croppedWidth = (1.0f - cropX) * static_cast<float>(bitmap.getWidth());
  const float croppedHeight = (1.0f - cropY) * static_cast<float>(bitmap.getHeight());
  bool hasTargetBounds = false;
  float fitScale = 1.0f;

  if (maxWidth > 0 && croppedWidth > 0.0f) {
    fitScale = static_cast<float>(maxWidth) / croppedWidth;
    hasTargetBounds = true;
  }

  if (maxHeight > 0 && croppedHeight > 0.0f) {
    const float heightScale = static_cast<float>(maxHeight) / croppedHeight;
    fitScale = hasTargetBounds ? std::min(fitScale, heightScale) : heightScale;
    hasTargetBounds = true;
  }

  if (hasTargetBounds && fitScale < 1.0f) {
    scale = fitScale;
    isScaled = true;
  }
  LOG_DBG("GFX", "Scaling by %f - %s", scale, isScaled ? "scaled" : "not scaled");

  // Calculate output row size (2 bits per pixel, packed into bytes)
  // IMPORTANT: Use int, not uint8_t, to avoid overflow for images > 1020 pixels wide
  const int outputRowSize = (bitmap.getWidth() + 3) / 4;
  auto* outputRow = static_cast<uint8_t*>(malloc(outputRowSize));
  auto* rowBytes = static_cast<uint8_t*>(malloc(bitmap.getRowBytes()));

  if (!outputRow || !rowBytes) {
    LOG_ERR("GFX", "!! Failed to allocate BMP row buffers");
    free(outputRow);
    free(rowBytes);
    return;
  }

  for (int bmpY = 0; bmpY < (bitmap.getHeight() - cropPixY); bmpY++) {
    // The BMP's (0, 0) is the bottom-left corner (if the height is positive, top-left if negative).
    // Screen's (0, 0) is the top-left corner.
    int screenY = -cropPixY + (bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY);
    if (isScaled) {
      screenY = std::floor(screenY * scale);
    }
    screenY += y;  // the offset should not be scaled
    if (screenY >= getScreenHeight()) {
      break;
    }

    if (bitmap.readNextRow(outputRow, rowBytes) != BmpReaderError::Ok) {
      LOG_ERR("GFX", "Failed to read row %d from bitmap", bmpY);
      free(outputRow);
      free(rowBytes);
      return;
    }

    if (screenY < 0) {
      continue;
    }

    if (bmpY < cropPixY) {
      // Skip the row if it's outside the crop area
      continue;
    }

    for (int bmpX = cropPixX; bmpX < bitmap.getWidth() - cropPixX; bmpX++) {
      int screenX = bmpX - cropPixX;
      if (isScaled) {
        screenX = std::floor(screenX * scale);
      }
      screenX += x;  // the offset should not be scaled
      if (screenX >= getScreenWidth()) {
        break;
      }
      if (screenX < 0) {
        continue;
      }

      const uint8_t val = outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8)) & 0x3;

      if (renderMode == BW) {
        if (darkMode) {
          drawPixelRaw(screenX, screenY, val < 3);
        } else if (val < 3) {
          drawPixelRaw(screenX, screenY, true);
        }
      } else if (renderMode == GRAYSCALE_MSB && (val == 1 || val == 2)) {
        drawPixel(screenX, screenY, false);
      } else if (renderMode == GRAYSCALE_LSB && val == 1) {
        drawPixel(screenX, screenY, false);
      }
    }
  }

  free(outputRow);
  free(rowBytes);
}

void GfxRenderer::drawBitmap1Bit(const Bitmap& bitmap, const int x, const int y, const int maxWidth,
                                 const int maxHeight) const {
  float scale = 1.0f;
  bool isScaled = false;
  if (maxWidth > 0 && bitmap.getWidth() > maxWidth) {
    scale = static_cast<float>(maxWidth) / static_cast<float>(bitmap.getWidth());
    isScaled = true;
  }
  if (maxHeight > 0 && bitmap.getHeight() > maxHeight) {
    scale = std::min(scale, static_cast<float>(maxHeight) / static_cast<float>(bitmap.getHeight()));
    isScaled = true;
  }

  // For 1-bit BMP, output is still 2-bit packed (for consistency with readNextRow)
  const int outputRowSize = (bitmap.getWidth() + 3) / 4;
  auto* outputRow = static_cast<uint8_t*>(malloc(outputRowSize));
  auto* rowBytes = static_cast<uint8_t*>(malloc(bitmap.getRowBytes()));

  if (!outputRow || !rowBytes) {
    LOG_ERR("GFX", "!! Failed to allocate 1-bit BMP row buffers");
    free(outputRow);
    free(rowBytes);
    return;
  }

  for (int bmpY = 0; bmpY < bitmap.getHeight(); bmpY++) {
    // Read rows sequentially using readNextRow
    if (bitmap.readNextRow(outputRow, rowBytes) != BmpReaderError::Ok) {
      LOG_ERR("GFX", "Failed to read row %d from 1-bit bitmap", bmpY);
      free(outputRow);
      free(rowBytes);
      return;
    }

    // Calculate screen Y based on whether BMP is top-down or bottom-up
    const int bmpYOffset = bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY;
    int screenY = y + (isScaled ? static_cast<int>(std::floor(bmpYOffset * scale)) : bmpYOffset);
    if (screenY >= getScreenHeight()) {
      continue;  // Continue reading to keep row counter in sync
    }
    if (screenY < 0) {
      continue;
    }

    for (int bmpX = 0; bmpX < bitmap.getWidth(); bmpX++) {
      int screenX = x + (isScaled ? static_cast<int>(std::floor(bmpX * scale)) : bmpX);
      if (screenX >= getScreenWidth()) {
        break;
      }
      if (screenX < 0) {
        continue;
      }

      // Get 2-bit value (result of readNextRow quantization)
      const uint8_t val = outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8)) & 0x3;

      // For 1-bit source: 0 or 1 -> map to black (0,1,2) or white (3)
      if (darkMode) {
        drawPixelRaw(screenX, screenY, val < 3);
      } else if (val < 3) {
        drawPixelRaw(screenX, screenY, true);
      }
    }
  }

  free(outputRow);
  free(rowBytes);
}

void GfxRenderer::fillPolygon(const int* xPoints, const int* yPoints, int numPoints, bool state) const {
  if (numPoints < 3) return;

  // Find bounding box
  int minY = yPoints[0], maxY = yPoints[0];
  for (int i = 1; i < numPoints; i++) {
    if (yPoints[i] < minY) minY = yPoints[i];
    if (yPoints[i] > maxY) maxY = yPoints[i];
  }

  // Clip to screen
  if (minY < 0) minY = 0;
  if (maxY >= getScreenHeight()) maxY = getScreenHeight() - 1;

  // Allocate node buffer for scanline algorithm
  auto* nodeX = static_cast<int*>(malloc(numPoints * sizeof(int)));
  if (!nodeX) {
    LOG_ERR("GFX", "!! Failed to allocate polygon node buffer");
    return;
  }

  // Scanline fill algorithm
  for (int scanY = minY; scanY <= maxY; scanY++) {
    int nodes = 0;

    // Find all intersection points with edges
    int j = numPoints - 1;
    for (int i = 0; i < numPoints; i++) {
      if ((yPoints[i] < scanY && yPoints[j] >= scanY) || (yPoints[j] < scanY && yPoints[i] >= scanY)) {
        // Calculate X intersection using fixed-point to avoid float
        int dy = yPoints[j] - yPoints[i];
        if (dy != 0) {
          nodeX[nodes++] = xPoints[i] + (scanY - yPoints[i]) * (xPoints[j] - xPoints[i]) / dy;
        }
      }
      j = i;
    }

    // Sort nodes by X
    std::sort(nodeX, nodeX + nodes);

    // Fill between pairs of nodes
    for (int i = 0; i < nodes - 1; i += 2) {
      int startX = nodeX[i];
      int endX = nodeX[i + 1];

      // Clip to screen
      if (startX < 0) startX = 0;
      if (endX >= getScreenWidth()) endX = getScreenWidth() - 1;

      // Draw horizontal line
      for (int x = startX; x <= endX; x++) {
        drawPixel(x, scanY, state);
      }
    }
  }

  free(nodeX);
}

// For performance measurement (using static to allow "const" methods)
static unsigned long start_ms = 0;

void GfxRenderer::clearScreen(const uint8_t color) const {
  start_ms = millis();
  const uint8_t effectiveColor = (darkMode && renderMode == BW && color == 0xFF) ? 0x00 : color;
  if (_stripActive) {
    memset(_stripBuf, effectiveColor, static_cast<size_t>(panelWidthBytes) * static_cast<size_t>(_stripRows));
    return;
  }
  display.clearScreen(effectiveColor);
}

void GfxRenderer::beginStripTarget(uint8_t* scratch, int stripY0, int stripRows) const {
  assert(scratch != nullptr && stripRows > 0 && stripY0 >= 0 && stripY0 <= static_cast<int>(panelHeight) - stripRows);
  _stripBuf = scratch;
  _stripY0 = stripY0;
  _stripRows = stripRows;
  _stripActive = true;
}

void GfxRenderer::endStripTarget() const {
  _stripActive = false;
  _stripBuf = nullptr;
  _stripY0 = 0;
  _stripRows = 0;
}

bool GfxRenderer::glyphIntersectsStrip(int x0, int y0, int x1, int y1) const {
  if (!_stripActive) {
    return true;
  }

  int ax = 0;
  int ay = 0;
  int bx = 0;
  int by = 0;
  rotateCoordinates(orientation, x0, y0, &ax, &ay, panelWidth, panelHeight);
  rotateCoordinates(orientation, x1, y1, &bx, &by, panelWidth, panelHeight);
  const int minY = std::min(ay, by);
  const int maxY = std::max(ay, by);
  return !(maxY < _stripY0 || minY >= _stripY0 + _stripRows);
}

void GfxRenderer::invertScreen() const {
  for (uint32_t i = 0; i < frameBufferSize; i++) {
    frameBuffer[i] = ~frameBuffer[i];
  }
}

void GfxRenderer::displayBuffer(const HalDisplay::RefreshMode refreshMode) const {
  auto elapsed = millis() - start_ms;
  LOG_DBG("GFX", "Time = %lu ms from clearScreen to displayBuffer", elapsed);
  HalDisplay::RefreshMode effectiveRefreshMode = refreshMode;
  if (nextRefreshOverridePending) {
    effectiveRefreshMode = nextRefreshOverride;
    nextRefreshOverridePending = false;
  }
  display.displayBuffer(effectiveRefreshMode, fadingFix);
}

std::string GfxRenderer::truncatedText(const int fontId, const char* text, const int maxWidth,
                                       const EpdFontFamily::Style style) const {
  if (!text || maxWidth <= 0) return "";

  // U+2026 HORIZONTAL ELLIPSIS (UTF-8: 0xE2 0x80 0xA6)
  const char* ellipsis = "\xe2\x80\xa6";
  const int textWidth = getTextWidth(fontId, text, style);
  if (textWidth <= maxWidth) {
    return text;
  }

  if (getTextWidth(fontId, ellipsis, style) > maxWidth) {
    return ellipsis;
  }

  std::vector<size_t> charEnds;
  const auto* start = reinterpret_cast<const unsigned char*>(text);
  const auto* cursor = start;
  while (*cursor != '\0') {
    utf8NextCodepoint(&cursor);
    charEnds.push_back(static_cast<size_t>(cursor - start));
  }

  size_t low = 0;
  size_t high = charEnds.size();
  while (low < high) {
    const size_t mid = low + (high - low + 1) / 2;
    std::string candidate(text, charEnds[mid - 1]);
    candidate += ellipsis;
    if (getTextWidth(fontId, candidate.c_str(), style) <= maxWidth) {
      low = mid;
    } else {
      high = mid - 1;
    }
  }

  if (low == 0) {
    return ellipsis;
  }
  std::string result(text, charEnds[low - 1]);
  result += ellipsis;
  return result;
}

std::vector<std::string> GfxRenderer::wrappedText(const int fontId, const char* text, const int maxWidth,
                                                  const int maxLines, const EpdFontFamily::Style style) const {
  std::vector<std::string> lines;

  if (!text || maxWidth <= 0 || maxLines <= 0) return lines;

  std::string remaining = text;
  std::string currentLine;

  while (!remaining.empty()) {
    if (static_cast<int>(lines.size()) == maxLines - 1) {
      // Last available line: combine any word already started on this line with
      // the rest of the text, then let truncatedText fit it with an ellipsis.
      std::string lastContent = currentLine.empty() ? remaining : currentLine + " " + remaining;
      lines.push_back(truncatedText(fontId, lastContent.c_str(), maxWidth, style));
      return lines;
    }

    // Find next word
    size_t spacePos = remaining.find(' ');
    std::string word;

    if (spacePos == std::string::npos) {
      word = remaining;
      remaining.clear();
    } else {
      word = remaining.substr(0, spacePos);
      remaining.erase(0, spacePos + 1);
    }

    std::string testLine = currentLine.empty() ? word : currentLine + " " + word;

    if (getTextWidth(fontId, testLine.c_str(), style) <= maxWidth) {
      currentLine = testLine;
    } else {
      if (!currentLine.empty()) {
        lines.push_back(currentLine);
        // If the carried-over word itself exceeds maxWidth, truncate it and
        // push it as a complete line immediately — storing it in currentLine
        // would allow a subsequent short word to be appended after the ellipsis.
        if (getTextWidth(fontId, word.c_str(), style) > maxWidth) {
          lines.push_back(truncatedText(fontId, word.c_str(), maxWidth, style));
          currentLine.clear();
          if (static_cast<int>(lines.size()) >= maxLines) return lines;
        } else {
          currentLine = word;
        }
      } else {
        // Single word wider than maxWidth: truncate and stop to avoid complicated
        // splitting rules (different between languages). Results in an aesthetically
        // pleasing end.
        lines.push_back(truncatedText(fontId, word.c_str(), maxWidth, style));
        return lines;
      }
    }
  }

  if (!currentLine.empty() && static_cast<int>(lines.size()) < maxLines) {
    lines.push_back(currentLine);
  }

  return lines;
}

// Note: Internal driver treats screen in command orientation; this library exposes a logical orientation
int GfxRenderer::getScreenWidth() const {
  switch (orientation) {
    case Portrait:
    case PortraitInverted:
      // 480px wide in portrait logical coordinates
      return panelHeight;
    case LandscapeClockwise:
    case LandscapeCounterClockwise:
      // 800px wide in landscape logical coordinates
      return panelWidth;
  }
  return panelHeight;
}

int GfxRenderer::getScreenHeight() const {
  switch (orientation) {
    case Portrait:
    case PortraitInverted:
      // 800px tall in portrait logical coordinates
      return panelWidth;
    case LandscapeClockwise:
    case LandscapeCounterClockwise:
      // 480px tall in landscape logical coordinates
      return panelHeight;
  }
  return panelWidth;
}

static bool logicalRectToPhysicalBounds(GfxRenderer::Orientation orientation, int lx, int ly, int lw, int lh,
                                        uint16_t panelWidth, uint16_t panelHeight, int* outX0, int* outY0, int* outX1,
                                        int* outY1) {
  if (lw <= 0 || lh <= 0) return false;

  int minX = INT_MAX;
  int minY = INT_MAX;
  int maxX = INT_MIN;
  int maxY = INT_MIN;
  const int corners[4][2] = {{lx, ly}, {lx + lw - 1, ly}, {lx, ly + lh - 1}, {lx + lw - 1, ly + lh - 1}};
  for (const auto& corner : corners) {
    int phyX;
    int phyY;
    rotateCoordinates(orientation, corner[0], corner[1], &phyX, &phyY, panelWidth, panelHeight);
    if (phyX < minX) minX = phyX;
    if (phyY < minY) minY = phyY;
    if (phyX > maxX) maxX = phyX;
    if (phyY > maxY) maxY = phyY;
  }

  if (minX < 0) minX = 0;
  if (minY < 0) minY = 0;
  if (maxX >= panelWidth) maxX = panelWidth - 1;
  if (maxY >= panelHeight) maxY = panelHeight - 1;
  if (minX > maxX || minY > maxY) return false;

  *outX0 = minX;
  *outY0 = minY;
  *outX1 = maxX;
  *outY1 = maxY;
  return true;
}

size_t GfxRenderer::getRegionByteSize(int lx, int ly, int lw, int lh) const {
  int x0, y0, x1, y1;
  if (!logicalRectToPhysicalBounds(orientation, lx, ly, lw, lh, panelWidth, panelHeight, &x0, &y0, &x1, &y1)) {
    return 0;
  }

  const int byteX0 = x0 / 8;
  const int byteX1 = x1 / 8;
  const int bytesPerRow = byteX1 - byteX0 + 1;
  const int rowCount = y1 - y0 + 1;
  return static_cast<size_t>(bytesPerRow) * static_cast<size_t>(rowCount);
}

bool GfxRenderer::copyRegionToBuffer(int lx, int ly, int lw, int lh, uint8_t* buf, size_t bufSize) const {
  int x0, y0, x1, y1;
  if (!logicalRectToPhysicalBounds(orientation, lx, ly, lw, lh, panelWidth, panelHeight, &x0, &y0, &x1, &y1)) {
    return false;
  }

  const int byteX0 = x0 / 8;
  const int byteX1 = x1 / 8;
  const int bytesPerRow = byteX1 - byteX0 + 1;
  const int rowCount = y1 - y0 + 1;
  const size_t needed = static_cast<size_t>(bytesPerRow) * static_cast<size_t>(rowCount);
  if (bufSize < needed || !frameBuffer || !buf) return false;

  for (int row = 0; row < rowCount; row++) {
    const uint8_t* src = frameBuffer + (y0 + row) * panelWidthBytes + byteX0;
    memcpy(buf + row * bytesPerRow, src, bytesPerRow);
  }
  return true;
}

bool GfxRenderer::copyBufferToRegion(int lx, int ly, int lw, int lh, const uint8_t* buf, size_t bufSize) const {
  int x0, y0, x1, y1;
  if (!logicalRectToPhysicalBounds(orientation, lx, ly, lw, lh, panelWidth, panelHeight, &x0, &y0, &x1, &y1)) {
    return false;
  }

  const int byteX0 = x0 / 8;
  const int byteX1 = x1 / 8;
  const int bytesPerRow = byteX1 - byteX0 + 1;
  const int rowCount = y1 - y0 + 1;
  const size_t needed = static_cast<size_t>(bytesPerRow) * static_cast<size_t>(rowCount);
  if (bufSize < needed || !frameBuffer || !buf) return false;

  for (int row = 0; row < rowCount; row++) {
    uint8_t* dst = frameBuffer + (y0 + row) * panelWidthBytes + byteX0;
    memcpy(dst, buf + row * bytesPerRow, bytesPerRow);
  }
  return true;
}

int GfxRenderer::getSpaceWidth(const int fontId, const EpdFontFamily::Style style) const {
  auto sdIt = sdCardFonts_.find(fontId);
  if (sdIt != sdCardFonts_.end() && sdIt->second->hasAdvanceTable()) {
    const uint8_t resolvedStyle = resolveSdCardStyle(*sdIt->second, style);
    uint16_t advance = 0;
    if (sdIt->second->getAdvance(' ', resolvedStyle, &advance)) {
      return fp4::toPixel(advance);
    }
  }

  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return 0;
  }

  const EpdGlyph* spaceGlyph = fontIt->second.getGlyph(' ', style);
  return spaceGlyph ? fp4::toPixel(spaceGlyph->advanceX) : 0;  // snap 12.4 fixed-point to nearest pixel
}

int GfxRenderer::getSpaceAdvance(const int fontId, const uint32_t leftCp, const uint32_t rightCp,
                                 const EpdFontFamily::Style style) const {
  auto sdIt = sdCardFonts_.find(fontId);
  if (sdIt != sdCardFonts_.end() && sdIt->second->hasAdvanceTable()) {
    const uint8_t resolvedStyle = resolveSdCardStyle(*sdIt->second, style);
    uint16_t advance = 0;
    if (sdIt->second->getAdvance(' ', resolvedStyle, &advance)) {
      return fp4::toPixel(advance);
    }
  }

  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) return 0;
  const auto& font = fontIt->second;
  const EpdGlyph* spaceGlyph = font.getGlyph(' ', style);
  const int32_t spaceAdvanceFP = spaceGlyph ? static_cast<int32_t>(spaceGlyph->advanceX) : 0;
  // Combine space advance + flanking kern into one fixed-point sum before snapping.
  // Snapping the combined value avoids the +/-1 px error from snapping each component separately.
  const int32_t kernFP = static_cast<int32_t>(font.getKerning(leftCp, ' ', style)) +
                         static_cast<int32_t>(font.getKerning(' ', rightCp, style));
  return fp4::toPixel(spaceAdvanceFP + kernFP);
}

int GfxRenderer::getKerning(const int fontId, const uint32_t leftCp, const uint32_t rightCp,
                            const EpdFontFamily::Style style) const {
  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) return 0;
  const int kernFP = fontIt->second.getKerning(leftCp, rightCp, style);  // 4.4 fixed-point
  return fp4::toPixel(kernFP);                                           // snap 4.4 fixed-point to nearest pixel
}

int GfxRenderer::getTextAdvanceX(const int fontId, const char* text, EpdFontFamily::Style style) const {
  const int resolvedFontId = resolveTextFontId(fontId, text, style);
  if (resolvedFontId != fontId) ensureSdGlyphsResident(resolvedFontId, text, style, true);
  auto sdIt = sdCardFonts_.find(resolvedFontId);
  if (sdIt != sdCardFonts_.end() && sdIt->second->hasAdvanceTable()) {
    int32_t widthFP = 0;
    const bool isSupSub = (style & (EpdFontFamily::SUP | EpdFontFamily::SUB)) != 0;
    const uint8_t styleIdx = resolveSdCardStyle(*sdIt->second, style);
    const char* scan = text;
    bool complete = true;
    while (uint32_t cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&scan))) {
      const bool scaledSmallCap = smallcaps::isLowercase(style, cp);
      if (scaledSmallCap) cp = smallcaps::toUppercase(cp);
      uint16_t advance = 0;
      if (!sdIt->second->getAdvance(cp, styleIdx, &advance)) {
        complete = false;
        break;
      }
      int32_t advFP = advance;
      widthFP += isSupSub ? (advFP + 1) / 2 : (scaledSmallCap ? smallcaps::scaleAdvance(advFP) : advFP);
    }
    if (complete) {
      return fp4::toPixel(widthFP);
    }
  }

  const auto fontIt = fontMap.find(resolvedFontId);
  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "Font %d not found", resolvedFontId);
    return 0;
  }

  uint32_t cp;
  uint32_t prevCp = 0;
  int widthPx = 0;
  int32_t prevAdvanceFP = 0;  // 12.4 fixed-point: prev glyph's advance + next kern for snap
  bool prevScaledSmallCap = false;
  const auto& font = fontIt->second;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    if (utf8IsCombiningMark(cp)) {
      continue;
    }
    const bool scaledSmallCap = smallcaps::isLowercase(style, cp);
    if (scaledSmallCap) {
      cp = smallcaps::toUppercase(cp);
    } else {
      cp = font.applyLigatures(cp, text, style);
    }

    // Differential rounding: snap (previous advance + current kern) together,
    // matching drawText so measurement and rendering agree exactly.
    if (prevCp != 0) {
      int32_t kernFP = font.getKerning(prevCp, cp, style);  // 4.4 fixed-point kern
      if (prevScaledSmallCap || scaledSmallCap) kernFP = smallcaps::scaleAdvance(kernFP);
      widthPx += fp4::toPixel(prevAdvanceFP + kernFP);  // snap 12.4 fixed-point to nearest pixel
    }

    const EpdGlyph* glyph = font.getGlyph(cp, style);
    prevAdvanceFP = glyph ? glyph->advanceX : 0;
    if ((style & (EpdFontFamily::SUP | EpdFontFamily::SUB)) != 0) {
      prevAdvanceFP = (prevAdvanceFP + 1) / 2;
    } else if (scaledSmallCap) {
      prevAdvanceFP = smallcaps::scaleAdvance(prevAdvanceFP);
    }
    prevCp = cp;
    prevScaledSmallCap = scaledSmallCap;
  }
  widthPx += fp4::toPixel(prevAdvanceFP);  // final glyph's advance
  return widthPx;
}

int GfxRenderer::getFontAscenderSize(const int fontId) const {
  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return 0;
  }

  return fontIt->second.getData(EpdFontFamily::REGULAR)->ascender;
}

int GfxRenderer::getLineHeight(const int fontId) const {
  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return 0;
  }

  return fontIt->second.getData(EpdFontFamily::REGULAR)->advanceY;
}

int GfxRenderer::getTextHeight(const int fontId) const {
  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return 0;
  }
  return fontIt->second.getData(EpdFontFamily::REGULAR)->ascender;
}

void GfxRenderer::drawTextRotated90CW(const int fontId, const int x, const int y, const char* text, const bool black,
                                      const EpdFontFamily::Style style) const {
  // Cannot draw a NULL / empty string
  if (text == nullptr || *text == '\0') {
    return;
  }

  const int resolvedFontId = resolveTextFontId(fontId, text, style);
  if (resolvedFontId != fontId) ensureSdGlyphsResident(resolvedFontId, text, style, false);
  const auto fontIt = fontMap.find(resolvedFontId);
  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "Font %d not found", resolvedFontId);
    return;
  }

  const auto& font = fontIt->second;

  int lastBaseY = y;
  int lastBaseLeft = 0;
  int lastBaseWidth = 0;
  int lastBaseTop = 0;
  int32_t prevAdvanceFP = 0;  // 12.4 fixed-point: prev glyph's advance + next kern for snap

  uint32_t cp;
  uint32_t prevCp = 0;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    if (utf8IsCombiningMark(cp)) {
      const EpdGlyph* combiningGlyph = font.getGlyph(cp, style);
      if (!combiningGlyph) continue;
      const int raiseBy = combiningMark::raiseAboveBase(combiningGlyph->top, combiningGlyph->height, lastBaseTop);
      const int combiningX = x - raiseBy;
      const int combiningY = combiningMark::centerOverRotated90CW(lastBaseY, lastBaseLeft, lastBaseWidth,
                                                                  combiningGlyph->left, combiningGlyph->width);
      renderCharImpl<TextRotation::Rotated90CW>(*this, renderMode, font, cp, combiningX, combiningY, black, style);
      continue;
    }

    cp = font.applyLigatures(cp, text, style);

    // Differential rounding: snap (previous advance + current kern) as one unit,
    // subtracting for the rotated coordinate direction.
    if (prevCp != 0) {
      const auto kernFP = font.getKerning(prevCp, cp, style);  // 4.4 fixed-point kern
      lastBaseY -= fp4::toPixel(prevAdvanceFP + kernFP);       // snap 12.4 fixed-point to nearest pixel
    }

    const EpdGlyph* glyph = font.getGlyph(cp, style);

    lastBaseLeft = glyph ? glyph->left : 0;
    lastBaseWidth = glyph ? glyph->width : 0;
    lastBaseTop = glyph ? glyph->top : 0;
    prevAdvanceFP = glyph ? glyph->advanceX : 0;  // 12.4 fixed-point

    renderCharImpl<TextRotation::Rotated90CW>(*this, renderMode, font, cp, x, lastBaseY, black, style);
    prevCp = cp;
  }
}

uint8_t* GfxRenderer::getFrameBuffer() const { return frameBuffer; }

size_t GfxRenderer::getBufferSize() const { return frameBufferSize; }

// unused
// void GfxRenderer::grayscaleRevert() const { display.grayscaleRevert(); }

void GfxRenderer::displayGrayscaleBase(HalDisplay::RefreshMode fallback) const {
  display.displayGrayscaleBase(fallback, fadingFix);
}

void GfxRenderer::preconditionGrayscale() const { display.preconditionGrayscale(); }

void GfxRenderer::preconditionGrayscale(int x, int y, int w, int h) const {
  if (w <= 0 || h <= 0) return;

  int ax = 0;
  int ay = 0;
  int bx = 0;
  int by = 0;
  rotateCoordinates(orientation, x, y, &ax, &ay, panelWidth, panelHeight);
  rotateCoordinates(orientation, x + w - 1, y + h - 1, &bx, &by, panelWidth, panelHeight);

  int x0 = std::min(ax, bx);
  int x1 = std::max(ax, bx);
  int y0 = std::min(ay, by);
  int y1 = std::max(ay, by);
  x0 = std::max(x0, 0);
  y0 = std::max(y0, 0);
  x1 = std::min(x1, static_cast<int>(panelWidth) - 1);
  y1 = std::min(y1, static_cast<int>(panelHeight) - 1);
  if (x1 < x0 || y1 < y0) return;

  display.preconditionGrayscale(static_cast<uint16_t>(x0), static_cast<uint16_t>(y0),
                                static_cast<uint16_t>(x1 - x0 + 1), static_cast<uint16_t>(y1 - y0 + 1));
}

void GfxRenderer::copyGrayscaleLsbBuffers() const { display.copyGrayscaleLsbBuffers(frameBuffer); }

void GfxRenderer::copyGrayscaleMsbBuffers() const { display.copyGrayscaleMsbBuffers(frameBuffer); }

void GfxRenderer::displayGrayBuffer() const { display.displayGrayBuffer(fadingFix); }

void GfxRenderer::writeGrayscalePlaneStrip(bool lsbPlane, const uint8_t* scratch, int yStart, int numRows) const {
  if (scratch == nullptr) {
    return;
  }
  assert(yStart >= 0 && numRows > 0 && yStart <= static_cast<int>(panelHeight) - numRows);
  display.writeGrayscalePlaneStrip(lsbPlane, scratch, static_cast<uint16_t>(yStart), static_cast<uint16_t>(numRows));
}

bool GfxRenderer::supportsStripGrayscale() const { return display.supportsStripGrayscale(); }

void GfxRenderer::freeBwBufferChunks() {
  for (auto& bwBufferChunk : bwBufferChunks) {
    if (bwBufferChunk) {
      free(bwBufferChunk);
      bwBufferChunk = nullptr;
    }
  }
}

/**
 * This should be called before grayscale buffers are populated.
 * A `restoreBwBuffer` call should always follow the grayscale render if this method was called.
 * Uses chunked allocation to avoid needing 48KB of contiguous memory.
 * Returns true if buffer was stored successfully, false if allocation failed.
 */
bool GfxRenderer::storeBwBuffer() {
  // Allocate and copy each chunk
  for (size_t i = 0; i < bwBufferChunks.size(); i++) {
    // Check if any chunks are already allocated
    if (bwBufferChunks[i]) {
      LOG_ERR("GFX", "!! BW buffer chunk %zu already stored - this is likely a bug, freeing chunk", i);
      free(bwBufferChunks[i]);
      bwBufferChunks[i] = nullptr;
    }

    const size_t offset = i * BW_BUFFER_CHUNK_SIZE;
    const size_t chunkSize = std::min(BW_BUFFER_CHUNK_SIZE, static_cast<size_t>(frameBufferSize - offset));
    bwBufferChunks[i] = static_cast<uint8_t*>(malloc(chunkSize));

    if (!bwBufferChunks[i]) {
      LOG_ERR("GFX", "!! Failed to allocate BW buffer chunk %zu (%zu bytes)", i, chunkSize);
      // Free previously allocated chunks
      freeBwBufferChunks();
      return false;
    }

    memcpy(bwBufferChunks[i], frameBuffer + offset, chunkSize);
  }

  LOG_DBG("GFX", "Stored BW buffer in %zu chunks (%zu bytes each)", bwBufferChunks.size(), BW_BUFFER_CHUNK_SIZE);
  return true;
}

/**
 * This can only be called if `storeBwBuffer` was called prior to the grayscale render.
 * It should be called to restore the BW buffer state after grayscale rendering is complete.
 * Uses chunked restoration to match chunked storage.
 */
void GfxRenderer::restoreBwBuffer() {
  // Check if all chunks are allocated
  bool missingChunks = false;
  for (const auto& bwBufferChunk : bwBufferChunks) {
    if (!bwBufferChunk) {
      missingChunks = true;
      break;
    }
  }

  if (missingChunks) {
    freeBwBufferChunks();
    return;
  }

  for (size_t i = 0; i < bwBufferChunks.size(); i++) {
    const size_t offset = i * BW_BUFFER_CHUNK_SIZE;
    const size_t chunkSize = std::min(BW_BUFFER_CHUNK_SIZE, static_cast<size_t>(frameBufferSize - offset));
    memcpy(frameBuffer + offset, bwBufferChunks[i], chunkSize);
  }

  display.cleanupGrayscaleBuffers(frameBuffer);

  freeBwBufferChunks();
  LOG_DBG("GFX", "Restored and freed BW buffer chunks");
}

/**
 * Cleanup grayscale buffers using the current frame buffer.
 * Use this when BW buffer was re-rendered instead of stored/restored.
 */
void GfxRenderer::cleanupGrayscaleWithFrameBuffer() const {
  if (frameBuffer) {
    display.cleanupGrayscaleBuffers(frameBuffer);
  }
}

void GfxRenderer::getOrientedViewableTRBL(int* outTop, int* outRight, int* outBottom, int* outLeft) const {
  switch (orientation) {
    case Portrait:
      *outTop = VIEWABLE_MARGIN_TOP;
      *outRight = VIEWABLE_MARGIN_RIGHT;
      *outBottom = VIEWABLE_MARGIN_BOTTOM;
      *outLeft = VIEWABLE_MARGIN_LEFT;
      break;
    case LandscapeClockwise:
      *outTop = VIEWABLE_MARGIN_LEFT;
      *outRight = VIEWABLE_MARGIN_TOP;
      *outBottom = VIEWABLE_MARGIN_RIGHT;
      *outLeft = VIEWABLE_MARGIN_BOTTOM;
      break;
    case PortraitInverted:
      *outTop = VIEWABLE_MARGIN_BOTTOM;
      *outRight = VIEWABLE_MARGIN_LEFT;
      *outBottom = VIEWABLE_MARGIN_TOP;
      *outLeft = VIEWABLE_MARGIN_RIGHT;
      break;
    case LandscapeCounterClockwise:
      *outTop = VIEWABLE_MARGIN_RIGHT;
      *outRight = VIEWABLE_MARGIN_BOTTOM;
      *outBottom = VIEWABLE_MARGIN_LEFT;
      *outLeft = VIEWABLE_MARGIN_TOP;
      break;
  }
}
