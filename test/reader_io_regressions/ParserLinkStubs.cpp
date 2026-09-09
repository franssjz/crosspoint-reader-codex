#include <Epub.h>
#include <Epub/Page.h>
#include <Epub/converters/ImageDecoderFactory.h>
#include <Epub/hyphenation/Hyphenator.h>
#include <uzlib.h>

#include <cstdlib>

// The fixtures exercise real parser/style/token/layout/ZIP code. Image decoding,
// physical drawing and optional hyphenation dictionaries are outside this suite.
std::vector<Hyphenator::BreakInfo> Hyphenator::breakOffsets(const std::string&, bool) { return {}; }
const std::string& Epub::getPath() const { return filepath; }
bool Epub::readItemContentsToStream(const std::string&, Print&, size_t, bool) const { return false; }
ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string&) { return nullptr; }
bool ImageDecoderFactory::isFormatSupported(const std::string&) { return false; }

ImageBlock::ImageBlock(const std::string& path, int16_t width, int16_t height)
    : imagePath(path), width(width), height(height) {}
ImageBlock::ImageBlock(const std::string& path, int16_t width, int16_t height, std::string source, std::string href)
    : imagePath(path),
      sourceEpubPath(std::move(source)),
      sourceItemHref(std::move(href)),
      width(width),
      height(height) {}
bool ImageBlock::needsDecode() const { return false; }

void PageLine::render(GfxRenderer&, int, int, int, uint8_t) {}
bool PageLine::serialize(FsFile&) { return false; }
void PageImage::render(GfxRenderer&, int, int, int, uint8_t) {}
bool PageImage::serialize(FsFile&) { return false; }
void PageHorizontalRule::render(GfxRenderer&, int, int, int, uint8_t) {}
bool PageHorizontalRule::serialize(FsFile&) { return false; }
void PageTableFragment::render(GfxRenderer&, int, int, int, uint8_t) {}
bool PageTableFragment::serialize(FsFile&) { return false; }

// Firmware's raw-deflate path never invokes the optional checksum wrapper;
// this repo does not ship its checksum implementations. Fail loudly if a
// future change unexpectedly selects that untested wrapper in this host suite.
extern "C" uint32_t uzlib_adler32(const void*, unsigned int, uint32_t) { std::abort(); }
extern "C" uint32_t uzlib_crc32(const void*, unsigned int, uint32_t) { std::abort(); }
