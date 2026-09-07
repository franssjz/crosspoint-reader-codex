#include "ImageDecoderFactory.h"

#include <Logging.h>

#include <cctype>
#include <memory>
#include <new>
#include <string>

#include "ImageFormatSignature.h"
#include "JpegToFramebufferConverter.h"
#include "PngToFramebufferConverter.h"

std::unique_ptr<JpegToFramebufferConverter> ImageDecoderFactory::jpegDecoder = nullptr;
std::unique_ptr<PngToFramebufferConverter> ImageDecoderFactory::pngDecoder = nullptr;

namespace {

ImageFileFormat formatFromExtension(const std::string& imagePath) {
  std::string ext = imagePath;
  size_t dotPos = ext.rfind('.');
  if (dotPos != std::string::npos) {
    ext = ext.substr(dotPos);
    for (auto& c : ext) {
      c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
  } else {
    ext = "";
  }

  if (JpegToFramebufferConverter::supportsFormat(ext)) return ImageFileFormat::Jpeg;
  if (PngToFramebufferConverter::supportsFormat(ext)) return ImageFileFormat::Png;
  return ImageFileFormat::Unknown;
}

ImageFileFormat formatFromStoredSignature(const std::string& imagePath) {
  if (!Storage.exists(imagePath.c_str())) return ImageFileFormat::Unknown;

  FsFile file;
  if (!Storage.openFileForRead("DEC", imagePath, file)) return ImageFileFormat::Unknown;
  uint8_t signature[8] = {};
  const int bytesRead = file.read(signature, sizeof(signature));
  return detectImageFormatSignature(signature, bytesRead > 0 ? static_cast<size_t>(bytesRead) : 0);
}

}  // namespace

ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string& imagePath) {
  const ImageFileFormat extensionFormat = formatFromExtension(imagePath);
  const ImageFileFormat signatureFormat = formatFromStoredSignature(imagePath);
  const ImageFileFormat format = signatureFormat != ImageFileFormat::Unknown ? signatureFormat : extensionFormat;

  if (signatureFormat != ImageFileFormat::Unknown && extensionFormat != ImageFileFormat::Unknown &&
      signatureFormat != extensionFormat) {
    LOG_ERR("DEC", "Image signature overrides mismatched extension: %s", imagePath.c_str());
  }

  if (format == ImageFileFormat::Jpeg) {
    if (!jpegDecoder) {
      jpegDecoder.reset(new (std::nothrow) JpegToFramebufferConverter());
    }
    if (!jpegDecoder) LOG_ERR("DEC", "Could not allocate JPEG decoder");
    return jpegDecoder.get();
  }
  if (format == ImageFileFormat::Png) {
    if (!pngDecoder) {
      pngDecoder.reset(new (std::nothrow) PngToFramebufferConverter());
    }
    if (!pngDecoder) LOG_ERR("DEC", "Could not allocate PNG decoder");
    return pngDecoder.get();
  }

  LOG_ERR("DEC", "No decoder found for image: %s", imagePath.c_str());
  return nullptr;
}

bool ImageDecoderFactory::isFormatSupported(const std::string& imagePath) { return getDecoder(imagePath) != nullptr; }
