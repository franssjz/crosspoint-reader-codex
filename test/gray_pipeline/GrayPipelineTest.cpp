// Regression tests for the 2-bit grayscale pipeline.
//
// Both groups exist because of review findings on the factory-LUT work:
// unbounded error growth in Floyd-Steinberg, and fill polarity that was only
// correct on the BW path.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "GfxRenderer/BitmapHelpers.h"
#include "GfxRenderer/FillPolarity.h"

namespace {

constexpr int kPanelWidth = 800;
constexpr int kPanelHeight = 480;

// Run a ditherer over a solid field and count pixels that came out at a level
// other than the expected one.
//
// A solid field is the worst case for error diffusion: every pixel leaves the
// same residual, so nothing cancels. It is also the clearest way to observe an
// overflow without reaching into the ditherer's private error rows — once an
// int16_t row wraps, the accumulator flips sign and the quantizer starts
// emitting the opposite extreme, so a solid white field visibly speckles.
template <typename Ditherer>
int countUnexpectedLevels(const uint8_t tone, const uint8_t expectedLevel, const Gray4QuantizationMode mode,
                          const int rows = kPanelHeight) {
  Ditherer ditherer(kPanelWidth, mode);
  int unexpected = 0;
  for (int y = 0; y < rows; ++y) {
    for (int x = 0; x < kPanelWidth; ++x) {
      if (ditherer.processPixel(tone, x) != expectedLevel) ++unexpected;
    }
    ditherer.nextRow();
  }
  return unexpected;
}

}  // namespace

// --- Error diffusion bounds -------------------------------------------------

// Tuned reconstructs white as 210, so a white field leaves a permanent +45
// residual that no output level can absorb. Floyd-Steinberg redistributes the
// whole error rather than a fraction of it, so before saturation this diverged
// past INT16_MAX around row 428 (peak ~36691), wrapped the error rows, and
// turned a solid white field into black speckle.
TEST(GrayPipelineErrorBounds, FloydSteinbergKeepsSolidWhiteWhite) {
  EXPECT_EQ(countUnexpectedLevels<FloydSteinbergDitherer>(255, 3, Gray4QuantizationMode::Tuned), 0);
}

TEST(GrayPipelineErrorBounds, FloydSteinbergKeepsSolidBlackBlack) {
  // Symmetric case: Tuned reconstructs black as 15, leaving a permanent -15.
  EXPECT_EQ(countUnexpectedLevels<FloydSteinbergDitherer>(0, 0, Gray4QuantizationMode::Tuned), 0);
}

TEST(GrayPipelineErrorBounds, AtkinsonKeepsSolidFieldsFlat) {
  // Atkinson spreads only 6/8 and is contractive, so it was never at risk.
  // Pinned so a change to the diffusion weights has to notice.
  EXPECT_EQ(countUnexpectedLevels<AtkinsonDitherer>(255, 3, Gray4QuantizationMode::Tuned), 0);
  EXPECT_EQ(countUnexpectedLevels<AtkinsonDitherer>(0, 0, Gray4QuantizationMode::Tuned), 0);
}

TEST(GrayPipelineErrorBounds, FullRangeReconstructionIsAlsoStable) {
  // NativePalette can represent both extremes exactly, so there is no residual
  // to accumulate in the first place.
  EXPECT_EQ(countUnexpectedLevels<FloydSteinbergDitherer>(255, 3, Gray4QuantizationMode::NativePalette), 0);
  EXPECT_EQ(countUnexpectedLevels<FloydSteinbergDitherer>(0, 0, Gray4QuantizationMode::NativePalette), 0);
}

TEST(GrayPipelineErrorBounds, SaturationLeavesRealisticValuesUntouched) {
  // The bound must not engage on values real content produces, or it would be
  // altering output rather than guarding against overflow.
  EXPECT_EQ(saturateDitherAccumulator(0), 0);
  EXPECT_EQ(saturateDitherAccumulator(255), 255);
  EXPECT_EQ(saturateDitherAccumulator(-255), -255);
  EXPECT_EQ(saturateDitherAccumulator(DITHER_ACCUMULATOR_LIMIT - 1), DITHER_ACCUMULATOR_LIMIT - 1);
  EXPECT_EQ(saturateDitherAccumulator(DITHER_ACCUMULATOR_LIMIT + 5000), DITHER_ACCUMULATOR_LIMIT);
  EXPECT_EQ(saturateDitherAccumulator(-DITHER_ACCUMULATOR_LIMIT - 5000), -DITHER_ACCUMULATOR_LIMIT);
}

// --- Quantizer modes --------------------------------------------------------

TEST(GrayPipelineQuantizer, TunedMatchesThePreviouslyLiveThresholds) {
  EXPECT_EQ(quantizeGray4(29, Gray4QuantizationMode::Tuned).index, 0);
  EXPECT_EQ(quantizeGray4(30, Gray4QuantizationMode::Tuned).index, 1);
  EXPECT_EQ(quantizeGray4(49, Gray4QuantizationMode::Tuned).index, 1);
  EXPECT_EQ(quantizeGray4(50, Gray4QuantizationMode::Tuned).index, 2);
  EXPECT_EQ(quantizeGray4(139, Gray4QuantizationMode::Tuned).index, 2);
  EXPECT_EQ(quantizeGray4(140, Gray4QuantizationMode::Tuned).index, 3);
}

TEST(GrayPipelineQuantizer, NativePaletteUsesEvenMidpointsAndFullRange) {
  EXPECT_EQ(quantizeGray4(42, Gray4QuantizationMode::NativePalette).value, 0);
  EXPECT_EQ(quantizeGray4(127, Gray4QuantizationMode::NativePalette).value, 85);
  EXPECT_EQ(quantizeGray4(212, Gray4QuantizationMode::NativePalette).value, 170);
  EXPECT_EQ(quantizeGray4(255, Gray4QuantizationMode::NativePalette).value, 255);
}

TEST(GrayPipelineQuantizer, ClampsOutOfRangeInput) {
  // Callers pass a saturated accumulator, which can sit outside 0..255.
  EXPECT_EQ(quantizeGray4(-5000, Gray4QuantizationMode::Tuned).index, 0);
  EXPECT_EQ(quantizeGray4(5000, Gray4QuantizationMode::Tuned).index, 3);
}

// --- Fill polarity (BW vs GRAY2 planes) -------------------------------------

TEST(GrayPipelineFillPolarity, SolidFillsInvertBetweenBwAndGray2) {
  // BW: the framebuffer holds 1 = white, so a black fill clears bits.
  EXPECT_FALSE(fillSetsBits(/*fillBlack=*/true, /*gray2=*/false));
  EXPECT_TRUE(fillSetsBits(/*fillBlack=*/false, /*gray2=*/false));

  // GRAY2: the plane starts cleared and a set bit marks the pixel, so a black
  // fill sets bits. Getting this backwards made fillRectDither(Black) erase
  // instead of paint on factory-LUT pages.
  EXPECT_TRUE(fillSetsBits(/*fillBlack=*/true, /*gray2=*/true));
  EXPECT_FALSE(fillSetsBits(/*fillBlack=*/false, /*gray2=*/true));
}

TEST(GrayPipelineFillPolarity, DitheredFillMaskInvertsBetweenBwAndGray2) {
  constexpr uint8_t kBlackMask = 0b10100000;  // dither wants these pixels black

  // BW stores the complement (a set bit is a white pixel).
  EXPECT_EQ(ditherPatternMask(kBlackMask, /*gray2=*/false), static_cast<uint8_t>(~kBlackMask));

  // GRAY2 marks exactly the black pixels.
  EXPECT_EQ(ditherPatternMask(kBlackMask, /*gray2=*/true), kBlackMask);
}

TEST(GrayPipelineFillPolarity, Gray2DitheredMaskIsIdenticalForBothPlanes) {
  // A dithered fill is a pure black/white pattern, so marking the same pixels
  // in the LSB and MSB planes resolves them to black and leaves the rest
  // white. If the planes disagreed, a highlight would render as a wrong level
  // instead of the intended dither — the failure the review flagged.
  for (int m = 0; m < 256; ++m) {
    const auto blackMask = static_cast<uint8_t>(m);
    const uint8_t lsbPlane = ditherPatternMask(blackMask, /*gray2=*/true);
    const uint8_t msbPlane = ditherPatternMask(blackMask, /*gray2=*/true);
    EXPECT_EQ(lsbPlane, msbPlane);
    EXPECT_EQ(lsbPlane, blackMask);
  }
}
