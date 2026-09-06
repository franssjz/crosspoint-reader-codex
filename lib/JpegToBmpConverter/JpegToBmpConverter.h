#pragma once

#include <HalStorage.h>

#include <string>

class Print;
class ZipFile;

class JpegToBmpConverter {
  static bool jpegFileToBmpStreamInternal(HalFile& jpegFile, Print& bmpOut, int targetWidth, int targetHeight,
                                          bool oneBit, bool crop = true, bool* permanentFailure = nullptr);

 public:
  static bool jpegFileToBmpStream(HalFile& jpegFile, Print& bmpOut, bool crop = true);
  // Convert with custom target size (for thumbnails)
  static bool jpegFileToBmpStreamWithSize(HalFile& jpegFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);
  // Convert to 1-bit BMP (black and white only, no grays) for fast home screen rendering.
  // permanentFailure (optional): set to true when the failure is due to the JPEG data itself
  // (progressive encoding, image too large, corrupt) rather than a transient resource issue (OOM).
  // Callers can use this to write a sentinel file and skip future retry attempts.
  static bool jpegFileTo1BitBmpStreamWithSize(HalFile& jpegFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight,
                                              bool* permanentFailure = nullptr);

  // Fallback for progressive or otherwise undecodable JPEGs: locate the small baseline JPEG
  // thumbnail embedded in the Exif APP1 block, extract it to tempThumbPath, decode it, and
  // write the result as a 1-bit BMP to bmpOut.  tempThumbPath is always removed on return.
  // permanentFailure follows the same convention as jpegFileTo1BitBmpStreamWithSize.
  static bool jpegExifThumbnailTo1BitBmpStreamWithSize(HalFile& jpegFile, const std::string& tempThumbPath,
                                                       Print& bmpOut, int targetMaxWidth, int targetMaxHeight,
                                                       bool* permanentFailure = nullptr);
};
