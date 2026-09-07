#pragma once

#include <EpdFontFamily.h>
#include <HalDisplay.h>

class FontCacheManager;
class SdCardFont;

#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "Bitmap.h"

// Color representation: uint8_t mapped to 4x4 Bayer matrix dithering levels
// 0 = transparent, 1-16 = gray levels (white to black)
enum Color : uint8_t {
  Clear = 0x00,
  White = 0x01,
  LightGray = 0x05,
  MediumGray = 0x07,
  DarkGray = 0x0A,
  ExtraDarkGray = 0x0D,
  Black = 0x10
};

class GfxRenderer {
 public:
  // GRAYSCALE_* = differential 2-bit overlay passes (mark pixels to nudge).
  // GRAY2_* = factory absolute 2-bit passes: clearScreen(0x00) base,
  // drawPixel(false) sets the plane bit. Encoding (per plane pass, ported from
  // the zgredex fork): LSB (BW RAM) marks Black(0) and LightGray(2);
  // MSB (RED RAM) marks Black(0) and DarkGray(1). Values are the 0-3
  // quantizer output where 0=black, 3=white.
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB, GRAY2_LSB, GRAY2_MSB };

  // Waveform selection for a full grayscale render.
  // FactoryFast/FactoryQuality use the OEM absolute 2-bit LUTs (V3.1.9
  // firmware extraction) with a self-contained power cycle; Differential is
  // the existing overlay path on top of a BW base frame.
  enum class GrayscaleMode { Differential, FactoryFast, FactoryQuality };

  // Tracks whether the panel's last update came from a factory LUT render.
  // After FactoryLut the controller already powered down (0xC7 sequence), so
  // the next displayBuffer() must not request another turn-off.
  enum class DisplayState { BW, FactoryLut };

  // Logical screen orientation from the perspective of callers
  enum Orientation {
    Portrait,                  // 480x800 logical coordinates (current default)
    LandscapeClockwise,        // 800x480 logical coordinates, rotated 180° (swap top/bottom)
    PortraitInverted,          // 480x800 logical coordinates, inverted
    LandscapeCounterClockwise  // 800x480 logical coordinates, native panel orientation
  };

 private:
  static constexpr size_t BW_BUFFER_CHUNK_SIZE = 8000;  // 8KB chunks to allow for non-contiguous memory

  HalDisplay& display;
  RenderMode renderMode;
  Orientation orientation;
  bool fadingFix;
  bool darkMode;
  uint8_t textDarkness = 0;  // 0=normal, 1=crisp, 2=dark, 3=extra dark
  uint8_t* frameBuffer = nullptr;
  uint16_t panelWidth = HalDisplay::DISPLAY_WIDTH;
  uint16_t panelHeight = HalDisplay::DISPLAY_HEIGHT;
  uint16_t panelWidthBytes = HalDisplay::DISPLAY_WIDTH_BYTES;
  uint32_t frameBufferSize = HalDisplay::BUFFER_SIZE;
  std::vector<uint8_t*> bwBufferChunks;
  std::map<int, EpdFontFamily> fontMap;
  mutable std::map<int, SdCardFont*> sdCardFonts_;
  std::map<int, int> fallbackFontMap_;
  mutable bool nextRefreshOverridePending = false;
  mutable HalDisplay::RefreshMode nextRefreshOverride = HalDisplay::FAST_REFRESH;
  mutable DisplayState displayState = DisplayState::BW;
  // Which waveform's tone calibration image decoding should target. Set by the
  // reader before rendering a page; consumed by the image cache so pixels
  // quantized for one waveform are not reused under the other.
  mutable GrayscaleMode imageToneMode = GrayscaleMode::Differential;

  // Tiled grayscale strip target. When active, drawPixel()/clearScreen()
  // operate on a caller-owned scratch holding physical rows
  // [_stripY0, _stripY0 + _stripRows) instead of the shared framebuffer.
  // _stripBufMsb, when non-null in GRAY2_LSB mode, enables dual-plane
  // rendering: one renderFn pass writes the LSB plane to _stripBuf and the
  // MSB plane to _stripBufMsb (1-bit draws are mirrored; 2-bit draws write
  // per-plane bits explicitly via drawPixelGray2 / DirectPixelWriter).
  mutable uint8_t* _stripBuf = nullptr;
  mutable uint8_t* _stripBufMsb = nullptr;
  mutable int _stripY0 = 0;
  mutable int _stripRows = 0;
  mutable bool _stripActive = false;

  // Mutable because drawText() is const but needs to delegate scan-mode
  // recording to the (non-const) FontCacheManager. Same pragmatic compromise
  // as before, concentrated in a single pointer instead of four fields.
  mutable FontCacheManager* fontCacheManager_ = nullptr;

  int resolveTextFontId(int fontId, const char* text, EpdFontFamily::Style style) const;
  void ensureSdGlyphsResident(int fontId, const char* text, EpdFontFamily::Style style, bool metadataOnly) const;

  void renderChar(const EpdFontFamily& fontFamily, uint32_t cp, int* x, int* y, bool pixelState,
                  EpdFontFamily::Style style) const;
  void drawPixelRaw(int x, int y, bool state) const;
  void freeBwBufferChunks();
  template <Color color>
  void drawPixelDither(int x, int y) const;
  template <Color color>
  void fillRectImpl(int x, int y, int width, int height) const;
  template <Color color>
  void fillArc(int maxRadius, int cx, int cy, int xDir, int yDir) const;

 public:
  explicit GfxRenderer(HalDisplay& halDisplay)
      : display(halDisplay), renderMode(BW), orientation(Portrait), fadingFix(false), darkMode(false) {}
  ~GfxRenderer() { freeBwBufferChunks(); }

  static constexpr int VIEWABLE_MARGIN_TOP = 9;
  static constexpr int VIEWABLE_MARGIN_RIGHT = 3;
  static constexpr int VIEWABLE_MARGIN_BOTTOM = 3;
  static constexpr int VIEWABLE_MARGIN_LEFT = 3;

  // Setup
  void begin();  // must be called right after display.begin()
  void insertFont(int fontId, EpdFontFamily font);
  void removeFont(int fontId) {
    fontMap.erase(fontId);
    sdCardFonts_.erase(fontId);
  }
  void setFontCacheManager(FontCacheManager* m) { fontCacheManager_ = m; }
  FontCacheManager* getFontCacheManager() const { return fontCacheManager_; }
  using TextGetter = const char* (*)(const void* ctx, uint32_t index);
  void prewarmFallbackText(int fontId, TextGetter getter, const void* ctx, uint32_t textCount,
                           EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  bool isFontCacheScanning() const;
  const std::map<int, EpdFontFamily>& getFontMap() const { return fontMap; }
  void registerSdCardFont(int fontId, SdCardFont* font) { sdCardFonts_[fontId] = font; }
  void unregisterSdCardFont(int fontId) { removeFont(fontId); }
  void clearSdCardFonts() { sdCardFonts_.clear(); }
  const std::map<int, SdCardFont*>& getSdCardFonts() const { return sdCardFonts_; }
  bool isSdCardFont(int fontId) const { return sdCardFonts_.count(fontId) > 0; }
  void setFallbackFont(int primaryFontId, int fallbackFontId) { fallbackFontMap_[primaryFontId] = fallbackFontId; }
  void clearFallbackFonts() { fallbackFontMap_.clear(); }
  void ensureSdCardFontReady(int fontId, const char* utf8Text, uint8_t styleMask = 0x0F) const;
  void ensureSdCardFontReady(int fontId, const std::deque<std::string>& words, bool includeHyphen,
                             uint8_t styleMask = 0x0F) const;
  bool releaseSdCardFontForLowMemory(int fontId) const;

  // Orientation control (affects logical width/height and coordinate transforms)
  void setOrientation(const Orientation o) { orientation = o; }
  Orientation getOrientation() const { return orientation; }

  // Fading fix control
  void setFadingFix(const bool enabled) { fadingFix = enabled; }
  void setDarkMode(const bool enabled) { darkMode = enabled; }
  bool isDarkMode() const { return darkMode; }
  void requestNextRefresh(const HalDisplay::RefreshMode mode) const {
    nextRefreshOverride = mode;
    nextRefreshOverridePending = true;
  }
  void clearNextRefreshOverride() const { nextRefreshOverridePending = false; }
  void requestNextFullRefresh() const { requestNextRefresh(HalDisplay::FULL_REFRESH); }
  void setTextDarkness(const uint8_t d) { textDarkness = d; }
  uint8_t getTextDarkness() const { return textDarkness; }

  // Screen ops
  int getScreenWidth() const;
  int getScreenHeight() const;
  void displayBuffer(HalDisplay::RefreshMode refreshMode = HalDisplay::FAST_REFRESH) const;
  // EXPERIMENTAL: Windowed update - display only a rectangular region
  // void displayWindow(int x, int y, int width, int height) const;
  void invertScreen() const;
  void clearScreen(uint8_t color = 0xFF) const;
  void getOrientedViewableTRBL(int* outTop, int* outRight, int* outBottom, int* outLeft) const;

  void beginStripTarget(uint8_t* scratch, int stripY0, int stripRows, uint8_t* scratchMsb = nullptr) const;
  void endStripTarget() const;
  bool glyphIntersectsStrip(int x0, int y0, int x1, int y1) const;
  uint8_t* getWriteTarget() const { return _stripActive ? _stripBuf : frameBuffer; }
  int getWriteOriginY() const { return _stripActive ? _stripY0 : 0; }
  int getWriteRows() const { return _stripActive ? _stripRows : panelHeight; }
  // Dual-plane GRAY2 strip rendering (LSB pass also produces the MSB plane).
  bool isDualGray2Active() const { return _stripActive && _stripBufMsb != nullptr && renderMode == GRAY2_LSB; }
  uint8_t* getWriteTargetMsb() const { return _stripBufMsb; }
  // Write one quantized 2-bit value (0=black..3=white) to both GRAY2 plane
  // scratches with the correct per-plane bits. Only marks bits (background
  // stays as cleared by clearScreen(0x00)), matching two-pass semantics.
  void drawPixelGray2(int x, int y, uint8_t val) const;

  // Drawing
  void drawPixel(int x, int y, bool state = true) const;
  void drawPixelDirect(int x, int y, bool state = true) const { drawPixelRaw(x, y, state); }
  void drawLine(int x1, int y1, int x2, int y2, bool state = true) const;
  void drawLine(int x1, int y1, int x2, int y2, int lineWidth, bool state) const;
  void drawArc(int maxRadius, int cx, int cy, int xDir, int yDir, int lineWidth, bool state) const;
  void drawRect(int x, int y, int width, int height, bool state = true) const;
  void drawRect(int x, int y, int width, int height, int lineWidth, bool state) const;
  void drawRoundedRect(int x, int y, int width, int height, int lineWidth, int cornerRadius, bool state) const;
  void drawRoundedRect(int x, int y, int width, int height, int lineWidth, int cornerRadius, bool roundTopLeft,
                       bool roundTopRight, bool roundBottomLeft, bool roundBottomRight, bool state) const;
  void maskRoundedRectOutsideCorners(int x, int y, int width, int height, int radius, Color color = Color::White) const;
  void fillRect(int x, int y, int width, int height, bool state = true) const;
  void fillRectDither(int x, int y, int width, int height, Color color) const;
  void fillRoundedRect(int x, int y, int width, int height, int cornerRadius, Color color) const;
  void fillRoundedRect(int x, int y, int width, int height, int cornerRadius, bool roundTopLeft, bool roundTopRight,
                       bool roundBottomLeft, bool roundBottomRight, Color color) const;
  void drawImage(const uint8_t bitmap[], int x, int y, int width, int height) const;
  void drawIcon(const uint8_t bitmap[], int x, int y, int width, int height) const;
  void drawIconBlack(const uint8_t bitmap[], int x, int y, int width, int height) const;
  void drawIconInverted(const uint8_t bitmap[], int x, int y, int width, int height) const;
  void drawBitmap(const Bitmap& bitmap, int x, int y, int maxWidth, int maxHeight, float cropX = 0,
                  float cropY = 0) const;
  void drawBitmap1Bit(const Bitmap& bitmap, int x, int y, int maxWidth, int maxHeight) const;
  void fillPolygon(const int* xPoints, const int* yPoints, int numPoints, bool state = true) const;

  // Text
  int getTextWidth(int fontId, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  void drawCenteredText(int fontId, int y, const char* text, bool black = true,
                        EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  void drawText(int fontId, int x, int y, const char* text, bool black = true,
                EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  int getSpaceWidth(int fontId, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  /// Returns the total inter-word advance: fp4::toPixel(spaceAdvance + kern(leftCp,' ') + kern(' ',rightCp)).
  /// Using a single snap avoids the +/-1 px rounding error that arises when space advance and kern are
  /// snapped separately and then added as integers.
  int getSpaceAdvance(int fontId, uint32_t leftCp, uint32_t rightCp, EpdFontFamily::Style style) const;
  /// Returns the kerning adjustment between two adjacent codepoints.
  int getKerning(int fontId, uint32_t leftCp, uint32_t rightCp, EpdFontFamily::Style style) const;
  int getTextAdvanceX(int fontId, const char* text, EpdFontFamily::Style style) const;
  int getFontAscenderSize(int fontId) const;
  int getLineHeight(int fontId) const;
  std::string truncatedText(int fontId, const char* text, int maxWidth,
                            EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  /// Word-wrap \p text into at most \p maxLines lines, each no wider than
  /// \p maxWidth pixels. Overflowing words and excess lines are UTF-8-safely
  /// truncated with an ellipsis (U+2026).
  std::vector<std::string> wrappedText(int fontId, const char* text, int maxWidth, int maxLines,
                                       EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;

  // Helper for drawing rotated text (90 degrees clockwise, for side buttons)
  void drawTextRotated90CW(int fontId, int x, int y, const char* text, bool black = true,
                           EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  int getTextHeight(int fontId) const;

  // Grayscale functions
  void setRenderMode(const RenderMode mode) { this->renderMode = mode; }
  RenderMode getRenderMode() const { return renderMode; }
  void preconditionGrayscale() const;
  void preconditionGrayscale(int x, int y, int w, int h) const;
  void displayGrayscaleBase(HalDisplay::RefreshMode fallback = HalDisplay::HALF_REFRESH) const;
  void copyGrayscaleLsbBuffers() const;
  void copyGrayscaleMsbBuffers() const;
  void displayGrayBuffer(const unsigned char* lut = nullptr, bool factoryMode = false) const;
  // LUT for a GrayscaleMode (nullptr = driver default differential LUT).
  static const unsigned char* grayscaleLutFor(GrayscaleMode mode);
  // Tone calibration that image decoding should target for this page.
  void setImageToneMode(const GrayscaleMode mode) const { imageToneMode = mode; }
  GrayscaleMode getImageToneMode() const { return imageToneMode; }
  void writeGrayscalePlaneStrip(bool lsbPlane, const uint8_t* scratch, int yStart, int numRows) const;
  bool supportsStripGrayscale() const;
  bool storeBwBuffer();    // Returns true if buffer was stored successfully
  void restoreBwBuffer();  // Restore and free the stored buffer
  void cleanupGrayscaleWithFrameBuffer() const;

  // Temporarily expose the framebuffer as build scratch. No drawing is valid
  // while it is lent; restore always returns a cleared, ready-to-redraw buffer.
  void releaseFrameBufferForBuild();
  bool restoreFrameBufferAfterBuild();
  bool hasFrameBuffer() const { return frameBuffer != nullptr; }

  class FrameBufferLoan {
   public:
    explicit FrameBufferLoan(GfxRenderer& renderer);
    ~FrameBufferLoan() { end(); }
    FrameBufferLoan(const FrameBufferLoan&) = delete;
    FrameBufferLoan& operator=(const FrameBufferLoan&) = delete;
    void end();

   private:
    GfxRenderer& renderer_;
    bool active_ = false;
  };

  // Font helpers
  const uint8_t* getGlyphBitmap(const EpdFontData* fontData, const EpdGlyph* glyph) const;

  // Low level functions
  uint8_t* getFrameBuffer() const;
  size_t getBufferSize() const;
  uint16_t getDisplayWidth() const { return panelWidth; }
  uint16_t getDisplayHeight() const { return panelHeight; }
  uint16_t getDisplayWidthBytes() const { return panelWidthBytes; }
  size_t getRegionByteSize(int logicalX, int logicalY, int logicalW, int logicalH) const;
  bool copyRegionToBuffer(int logicalX, int logicalY, int logicalW, int logicalH, uint8_t* buf, size_t bufSize) const;
  bool copyBufferToRegion(int logicalX, int logicalY, int logicalW, int logicalH, const uint8_t* buf,
                          size_t bufSize) const;
};

class GfxStripTargetScope {
 public:
  GfxStripTargetScope(const GfxRenderer& renderer, uint8_t* scratch, const int stripY0, const int stripRows,
                      uint8_t* scratchMsb = nullptr)
      : renderer_(renderer), active_(true) {
    renderer_.beginStripTarget(scratch, stripY0, stripRows, scratchMsb);
  }
  GfxStripTargetScope(const GfxStripTargetScope&) = delete;
  GfxStripTargetScope& operator=(const GfxStripTargetScope&) = delete;
  ~GfxStripTargetScope() {
    if (active_) {
      renderer_.endStripTarget();
    }
  }

  void end() {
    if (!active_) return;
    renderer_.endStripTarget();
    active_ = false;
  }

 private:
  const GfxRenderer& renderer_;
  bool active_;
};
