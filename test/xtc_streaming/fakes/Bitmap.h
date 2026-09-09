#pragma once

#include <HalStorage.h>

#include <cstring>

#pragma pack(push, 1)
struct BmpHeader {
  struct {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
  } fileHeader;
  struct {
    uint32_t biSize;
    int32_t biWidth;
    int32_t biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t biXPelsPerMeter;
    int32_t biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
  } infoHeader;
  uint8_t colors[8];
};
#pragma pack(pop)
static_assert(sizeof(BmpHeader) == 62);
enum class BmpRowOrder { TopDown };
enum class BmpReaderError { Ok, Invalid };
inline void createBmpHeader(BmpHeader* h, const int width, const int height, BmpRowOrder) {
  *h = {};
  h->fileHeader.bfType = 0x4d42;
  h->fileHeader.bfSize = sizeof(*h) + ((width + 31) / 32) * 4 * height;
  h->fileHeader.bfOffBits = sizeof(*h);
  h->infoHeader.biSize = 40;
  h->infoHeader.biWidth = width;
  h->infoHeader.biHeight = -height;
  h->infoHeader.biPlanes = 1;
  h->infoHeader.biBitCount = 1;
  h->infoHeader.biSizeImage = ((width + 31) / 32) * 4 * height;
  h->infoHeader.biClrUsed = 2;
  h->colors[4] = h->colors[5] = h->colors[6] = 255;
}
class Bitmap {
  FsFile& file;
  BmpHeader header{};

 public:
  explicit Bitmap(FsFile& source) : file(source) {}
  BmpReaderError parseHeaders() {
    return file.read(&header, sizeof(header)) == sizeof(header) && header.fileHeader.bfType == 0x4d42
               ? BmpReaderError::Ok
               : BmpReaderError::Invalid;
  }
  uint16_t getBpp() const { return header.infoHeader.biBitCount; }
};
