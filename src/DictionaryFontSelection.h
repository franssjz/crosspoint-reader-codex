#pragma once

#include <cstdint>

#include "fontIds.h"

namespace DictionaryFontSelection {

inline int definitionFontId(const int readerFontId, const bool useReaderFont, const uint8_t definitionTextSize) {
  if (useReaderFont && readerFontId != 0) {
    return readerFontId;
  }

  return definitionTextSize == 1 ? UI_12_FONT_ID : UI_10_FONT_ID;
}

}  // namespace DictionaryFontSelection
