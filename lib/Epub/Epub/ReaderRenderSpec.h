#pragma once

#include <cstdint>

// Complete set of values that influence EPUB pagination. Keeping these values
// together prevents incremental and foreground builds from drifting apart.
struct ReaderRenderSpec {
  int fontId = 0;
  float lineCompression = 1.0f;
  uint8_t wordSpacing = 0;
  bool extraParagraphSpacing = false;
  bool forceParagraphIndents = false;
  uint8_t paragraphAlignment = 0;
  uint16_t viewportWidth = 0;
  uint16_t viewportHeight = 0;
  bool hyphenationEnabled = false;
  bool focusReadingEnabled = false;
  bool embeddedStyle = true;
  uint8_t imageRendering = 0;
};
