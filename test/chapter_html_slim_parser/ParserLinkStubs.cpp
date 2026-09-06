#include <Epub/Page.h>
#include <Epub/TokenBoundary.h>
#include <Epub/blocks/ImageBlock.h>
#include <Epub/blocks/TextBlock.h>
#include <Epub/converters/ImageDecoderFactory.h>
#include <Epub/converters/ImageToFramebufferDecoder.h>
#include <Epub/hyphenation/Hyphenator.h>
#include <GfxRenderer.h>

#include "../../lib/Epub/Epub.h"  // real header: matches the parser TU, which includes it relatively

const char* lookupHtmlEntity(const char*, size_t) { return nullptr; }

#include <BidiUtils.h>

bool isExplicitHyphen(uint32_t) { return false; }
bool isSoftHyphen(uint32_t) { return false; }

std::vector<Hyphenator::BreakInfo> Hyphenator::breakOffsets(const std::string&, bool) { return {}; }

namespace BidiUtils {
bool startsWithRtl(const char*, int) { return false; }
bool computeVisualWordOrder(const std::vector<std::string>& words, bool, std::vector<uint16_t>& order) {
  order.resize(words.size());
  for (size_t index = 0; index < words.size(); ++index) order[index] = static_cast<uint16_t>(index);
  return true;
}
}  // namespace BidiUtils

// Fork TextBlock: flatten-on-construct arena; the parser test only needs a
// constructible object, so leave the arena empty and keep the link spans.
TextBlock::TextBlock(const std::vector<std::string>&, const std::vector<int16_t>&,
                     const std::vector<EpdFontFamily::Style>&, const std::vector<uint8_t>&,
                     const std::vector<uint16_t>&, const std::vector<uint8_t>&, const BlockStyle& blockStyle,
                     const std::vector<std::string>&, std::vector<LinkSpan> linkSpans)
    : blockStyle(blockStyle), linkSpans(std::move(linkSpans)) {}

ImageBlock::ImageBlock(const std::string& imagePath, const std::string& srcPath, int16_t width, int16_t height)
    : imagePath(imagePath), srcPath(srcPath), width(width), height(height) {}

bool ImageDecoderFactory::isFormatSupported(const std::string&) { return false; }
ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string&) { return nullptr; }
bool ImageToFramebufferDecoder::validateAndStoreDimensions(int64_t, int64_t, ImageDimensions&, const char*) {
  return false;
}

void PageLine::render(GfxRenderer&, int, int, int, uint8_t) {}
bool PageLine::serialize(HalFile&) { return false; }

void PageImage::render(GfxRenderer&, int, int, int, uint8_t) {}
void PageImage::renderPlaceholder(GfxRenderer&, int, int) const {}
bool PageImage::serialize(HalFile&) { return false; }

void PageHorizontalRule::render(GfxRenderer&, int, int, int, uint8_t) {}
bool PageHorizontalRule::serialize(HalFile&) { return false; }

// Fork-only surfaces reached from the parser: the real Epub.h wins over the
// stub header via the parser's relative include, and PageTableFragment is a
// fork page element whose bodies live in Page.cpp (not compiled here).
bool Epub::readItemContentsToStream(const std::string&, Print&, size_t, bool) const { return false; }

void PageTableFragment::render(GfxRenderer&, int, int, int, uint8_t) {}
bool PageTableFragment::serialize(HalFile&) { return false; }
