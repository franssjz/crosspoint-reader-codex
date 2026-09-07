#pragma once

#include <cstdint>
#include <cstring>

struct BmpHeader;

// Helper functions
uint8_t quantize(int gray, int x, int y);
uint8_t quantizeSimple(int gray);
uint8_t quantize1bit(int gray, int x, int y);
int adjustPixel(int gray);

// Result of 4-level quantization: the palette index written to the output,
// and the reflectance the panel actually produces for it. Error diffusion
// needs the second value — feeding back the nominal palette level instead of
// the measured one biases the whole image.
struct QuantizedGray4 {
  uint8_t index;
  int value;
};

// Threshold set used to map 8-bit gray onto the panel's four levels.
//
// Tuned: measured on the X4. Levels do not sit at even 85-step intervals
//   because the panel's mid grays are lighter than nominal, and the
//   thresholds are shifted down to match. This is what the reader has always
//   used; it is the default so behaviour is unchanged.
// NativePalette: even midpoints of the nominal 0/85/170/255 palette. Kept
//   because it is the correct choice for content already authored against
//   those exact levels, and to make the alternative explicit rather than
//   commented out.
//
// Cross-reference for anyone comparing with the YACP fork: their
// Gray4QuantizationMode::Legacy is this Tuned set, and their ::Native is
// NativePalette. They default to Native; we default to Tuned. Switching our
// default is a hardware-visible change, not a cleanup — it would need a
// side-by-side look on the panel first.
enum class Gray4QuantizationMode : uint8_t { Tuned, NativePalette };

// Bound for the error-diffusion accumulator; see the note below quantizeGray4.
// Guards the int16_t error rows against unbounded growth when the palette
// cannot represent an input extreme.
constexpr int DITHER_ACCUMULATOR_LIMIT = 2048;

inline int saturateDitherAccumulator(const int value) {
  if (value > DITHER_ACCUMULATOR_LIMIT) return DITHER_ACCUMULATOR_LIMIT;
  if (value < -DITHER_ACCUMULATOR_LIMIT) return -DITHER_ACCUMULATOR_LIMIT;
  return value;
}

// Note on error diffusion, since it interacts with the values above.
//
// Callers compute the diffused error against the *unclamped* accumulated value
// (source + inherited error), clamping only the copy passed in here for
// threshold selection. It matters most under Tuned, whose reconstruction tops
// out at 210 rather than 255: a white region then carries a permanent +45
// deficit, and clamping first discards exactly that surplus instead of letting
// neighbouring pixels trade it back. Measured on synthetic text-like content
// (82% white), propagating it cuts local tone error 33.5 -> 25.3.
//
// That surplus is unreachable, though — no output level can satisfy it — so a
// diffuser that redistributes 100% of the error never sheds it. Atkinson
// spreads only 6/8 and is therefore contractive (peak accumulator 369, error
// rows 114 on a solid 800x480 field). Floyd-Steinberg spreads 16/16 and is
// not: solid white diverges to ~36691 by row 428, wrapping the int16_t rows.
// saturateDitherAccumulator() bounds the accumulator so that cannot happen.
// The limit sits ~5x above Atkinson's observed worst case, so it never
// engages on the default path, and ~16x below INT16_MAX with the widest
// single-neighbour share ((v*7)>>4) still fitting comfortably.
inline QuantizedGray4 quantizeGray4(int gray, const Gray4QuantizationMode mode) {
  if (gray < 0) gray = 0;
  if (gray > 255) gray = 255;

  if (mode == Gray4QuantizationMode::NativePalette) {
    if (gray < 43) return {0, 0};
    if (gray < 128) return {1, 85};
    if (gray < 213) return {2, 170};
    return {3, 255};
  }

  if (gray < 30) return {0, 15};
  if (gray < 50) return {1, 30};
  if (gray < 140) return {2, 80};
  return {3, 210};
}

enum class BmpRowOrder { BottomUp, TopDown };

// Populates a 1-bit BMP header in the provided memory.
void createBmpHeader(BmpHeader* bmpHeader, int width, int height, BmpRowOrder rowOrder);

// 1-bit Atkinson dithering - better quality than noise dithering for thumbnails
// Error distribution pattern (same as 2-bit but quantizes to 2 levels):
//     X  1/8 1/8
// 1/8 1/8 1/8
//     1/8
class Atkinson1BitDitherer {
 public:
  explicit Atkinson1BitDitherer(int width) : width(width) {
    errorRow0 = new int16_t[width + 4]();  // Current row
    errorRow1 = new int16_t[width + 4]();  // Next row
    errorRow2 = new int16_t[width + 4]();  // Row after next
  }

  ~Atkinson1BitDitherer() {
    delete[] errorRow0;
    delete[] errorRow1;
    delete[] errorRow2;
  }

  // EXPLICITLY DELETE THE COPY CONSTRUCTOR
  Atkinson1BitDitherer(const Atkinson1BitDitherer& other) = delete;

  // EXPLICITLY DELETE THE COPY ASSIGNMENT OPERATOR
  Atkinson1BitDitherer& operator=(const Atkinson1BitDitherer& other) = delete;

  uint8_t processPixel(int gray, int x) {
    // Apply brightness/contrast/gamma adjustments
    gray = adjustPixel(gray);

    // Add accumulated error. Keep the unclamped sum: the diffused error must
    // be measured against it (see the note on quantizeGray4), otherwise
    // overshoot beyond [0,255] is discarded instead of carried to neighbours.
    const int accumulated = saturateDitherAccumulator(gray + errorRow0[x + 2]);
    int adjusted = accumulated;
    if (adjusted < 0) adjusted = 0;
    if (adjusted > 255) adjusted = 255;

    // Quantize to 2 levels (1-bit): 0 = black, 1 = white
    uint8_t quantized;
    int quantizedValue;
    if (adjusted < 128) {
      quantized = 0;
      quantizedValue = 0;
    } else {
      quantized = 1;
      quantizedValue = 255;
    }

    // Calculate error (only distribute 6/8 = 75%)
    int error = (accumulated - quantizedValue) >> 3;  // error/8

    // Distribute 1/8 to each of 6 neighbors
    errorRow0[x + 3] += error;  // Right
    errorRow0[x + 4] += error;  // Right+1
    errorRow1[x + 1] += error;  // Bottom-left
    errorRow1[x + 2] += error;  // Bottom
    errorRow1[x + 3] += error;  // Bottom-right
    errorRow2[x + 2] += error;  // Two rows down

    return quantized;
  }

  void nextRow() {
    int16_t* temp = errorRow0;
    errorRow0 = errorRow1;
    errorRow1 = errorRow2;
    errorRow2 = temp;
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

  void reset() {
    memset(errorRow0, 0, (width + 4) * sizeof(int16_t));
    memset(errorRow1, 0, (width + 4) * sizeof(int16_t));
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

 private:
  int width;
  int16_t* errorRow0;
  int16_t* errorRow1;
  int16_t* errorRow2;
};

// Atkinson dithering - distributes only 6/8 (75%) of error for cleaner results
// Error distribution pattern:
//     X  1/8 1/8
// 1/8 1/8 1/8
//     1/8
// Less error buildup = fewer artifacts than Floyd-Steinberg
class AtkinsonDitherer {
 public:
  explicit AtkinsonDitherer(int width, Gray4QuantizationMode quantizationMode = Gray4QuantizationMode::Tuned)
      : width(width), quantizationMode(quantizationMode) {
    errorRow0 = new int16_t[width + 4]();  // Current row
    errorRow1 = new int16_t[width + 4]();  // Next row
    errorRow2 = new int16_t[width + 4]();  // Row after next
  }

  ~AtkinsonDitherer() {
    delete[] errorRow0;
    delete[] errorRow1;
    delete[] errorRow2;
  }
  // **1. EXPLICITLY DELETE THE COPY CONSTRUCTOR**
  AtkinsonDitherer(const AtkinsonDitherer& other) = delete;

  // **2. EXPLICITLY DELETE THE COPY ASSIGNMENT OPERATOR**
  AtkinsonDitherer& operator=(const AtkinsonDitherer& other) = delete;

  uint8_t processPixel(int gray, int x) {
    // Add accumulated error
    // Keep the unclamped sum for the error term (see quantizeGray4's note),
    // saturated so it cannot grow without bound.
    const int accumulated = saturateDitherAccumulator(gray + errorRow0[x + 2]);
    int adjusted = accumulated;
    if (adjusted < 0) adjusted = 0;
    if (adjusted > 255) adjusted = 255;

    // Quantize to 4 levels
    const QuantizedGray4 q = quantizeGray4(adjusted, quantizationMode);
    const uint8_t quantized = q.index;
    const int quantizedValue = q.value;

    // Calculate error (only distribute 6/8 = 75%)
    int error = (accumulated - quantizedValue) >> 3;  // error/8

    // Distribute 1/8 to each of 6 neighbors
    errorRow0[x + 3] += error;  // Right
    errorRow0[x + 4] += error;  // Right+1
    errorRow1[x + 1] += error;  // Bottom-left
    errorRow1[x + 2] += error;  // Bottom
    errorRow1[x + 3] += error;  // Bottom-right
    errorRow2[x + 2] += error;  // Two rows down

    return quantized;
  }

  void nextRow() {
    int16_t* temp = errorRow0;
    errorRow0 = errorRow1;
    errorRow1 = errorRow2;
    errorRow2 = temp;
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

  void reset() {
    memset(errorRow0, 0, (width + 4) * sizeof(int16_t));
    memset(errorRow1, 0, (width + 4) * sizeof(int16_t));
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

 private:
  int width;
  Gray4QuantizationMode quantizationMode;
  int16_t* errorRow0;
  int16_t* errorRow1;
  int16_t* errorRow2;
};

// Floyd-Steinberg error diffusion dithering with serpentine scanning
// Serpentine scanning alternates direction each row to reduce "worm" artifacts
// Error distribution pattern (left-to-right):
//       X   7/16
// 3/16 5/16 1/16
// Error distribution pattern (right-to-left, mirrored):
// 1/16 5/16 3/16
//      7/16  X
class FloydSteinbergDitherer {
 public:
  explicit FloydSteinbergDitherer(int width, Gray4QuantizationMode quantizationMode = Gray4QuantizationMode::Tuned)
      : width(width), quantizationMode(quantizationMode), rowCount(0) {
    errorCurRow = new int16_t[width + 2]();  // +2 for boundary handling
    errorNextRow = new int16_t[width + 2]();
  }

  ~FloydSteinbergDitherer() {
    delete[] errorCurRow;
    delete[] errorNextRow;
  }

  // **1. EXPLICITLY DELETE THE COPY CONSTRUCTOR**
  FloydSteinbergDitherer(const FloydSteinbergDitherer& other) = delete;

  // **2. EXPLICITLY DELETE THE COPY ASSIGNMENT OPERATOR**
  FloydSteinbergDitherer& operator=(const FloydSteinbergDitherer& other) = delete;

  // Process a single pixel and return quantized 2-bit value
  // x is the logical x position (0 to width-1), direction handled internally
  uint8_t processPixel(int gray, int x) {
    // Add accumulated error to this pixel. The unclamped sum is what the error
    // term is measured against (see quantizeGray4's note). Saturating it is
    // load-bearing here: Floyd-Steinberg redistributes the whole error, so an
    // input extreme the palette cannot represent would otherwise diverge.
    const int accumulated = saturateDitherAccumulator(gray + errorCurRow[x + 1]);

    // Clamp to valid range, for quantization only
    int adjusted = accumulated;
    if (adjusted < 0) adjusted = 0;
    if (adjusted > 255) adjusted = 255;

    // Quantize to 4 levels (0, 85, 170, 255)
    const QuantizedGray4 q = quantizeGray4(adjusted, quantizationMode);
    const uint8_t quantized = q.index;
    const int quantizedValue = q.value;

    // Calculate error
    int error = accumulated - quantizedValue;

    // Distribute error to neighbors (serpentine: direction-aware)
    if (!isReverseRow()) {
      // Left to right: standard distribution
      // Right: 7/16
      errorCurRow[x + 2] += (error * 7) >> 4;
      // Bottom-left: 3/16
      errorNextRow[x] += (error * 3) >> 4;
      // Bottom: 5/16
      errorNextRow[x + 1] += (error * 5) >> 4;
      // Bottom-right: 1/16
      errorNextRow[x + 2] += (error) >> 4;
    } else {
      // Right to left: mirrored distribution
      // Left: 7/16
      errorCurRow[x] += (error * 7) >> 4;
      // Bottom-right: 3/16
      errorNextRow[x + 2] += (error * 3) >> 4;
      // Bottom: 5/16
      errorNextRow[x + 1] += (error * 5) >> 4;
      // Bottom-left: 1/16
      errorNextRow[x] += (error) >> 4;
    }

    return quantized;
  }

  // Call at the end of each row to swap buffers
  void nextRow() {
    // Swap buffers
    int16_t* temp = errorCurRow;
    errorCurRow = errorNextRow;
    errorNextRow = temp;
    // Clear the next row buffer
    memset(errorNextRow, 0, (width + 2) * sizeof(int16_t));
    rowCount++;
  }

  // Check if current row should be processed in reverse
  bool isReverseRow() const { return (rowCount & 1) != 0; }

  // Reset for a new image or MCU block
  void reset() {
    memset(errorCurRow, 0, (width + 2) * sizeof(int16_t));
    memset(errorNextRow, 0, (width + 2) * sizeof(int16_t));
    rowCount = 0;
  }

 private:
  int width;
  Gray4QuantizationMode quantizationMode;
  int rowCount;
  int16_t* errorCurRow;
  int16_t* errorNextRow;
};
