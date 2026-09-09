#include "BmpViewerActivity.h"

#include <Bitmap.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <PngToBmpConverter.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "Epub/converters/PngToFramebufferConverter.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/AtomicFileReplace.h"
#include "util/SleepScreenCache.h"

namespace {
// Some image converters write through Print and do not propagate short writes.
// A valid header alone must never authorize replacing a complete sleep cover.
class CheckedImageOutput final : public Print {
  FsFile& file;

 public:
  bool failed = false;
  explicit CheckedImageOutput(FsFile& file) : file(file) {}
  size_t write(const uint8_t* bytes, size_t count) override {
    const size_t written = file.write(bytes, count);
    failed = failed || written != count;
    return written;
  }
  size_t write(uint8_t byte) override { return write(&byte, 1); }
};
}  // namespace

BmpViewerActivity::BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path)
    : Activity("BmpViewer", renderer, mappedInput), filePath(std::move(path)) {}

void BmpViewerActivity::loadSiblingImages() {
  siblingCount = 0;
  currentImageIndex = -1;
  if (filePath.empty()) return;
  if (!siblingEntry) siblingEntry = makeUniqueNoThrow<FileIndex::Entry>();
  if (!siblingEntry) return;
  const std::string directory = FsHelpers::extractFolderPath(filePath);
  const auto accept = [](const char* name, bool isDir, const void*) {
    const std::string_view filename{name};
    return !isDir && name[0] != '.' && (FsHelpers::hasBmpExtension(filename) || FsHelpers::hasPngExtension(filename));
  };
  // A separate filter key avoids replacing a browser's open index for this directory.
  if (!siblingIndex.open(directory.c_str(), accept, nullptr, 5)) return;
  siblingCount = siblingIndex.totalCount();
  const size_t slash = filePath.find_last_of('/');
  const size_t row = siblingIndex.findRowByName(filePath.c_str() + (slash == std::string::npos ? 0 : slash + 1));
  if (row != SIZE_MAX) currentImageIndex = static_cast<int>(row);
}

void BmpViewerActivity::drawImageError() {
  imageLoaded = false;
  renderer.clearScreen();
  renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_IMAGE_LOAD_FAILED));
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

bool BmpViewerActivity::renderPngImage() {
  ImageDimensions dims;
  if (!PngToFramebufferConverter::getDimensionsStatic(filePath, dims)) {
    drawImageError();
    return false;
  }
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const float scale = std::min(
      1.0f, std::min(static_cast<float>(pageWidth) / dims.width, static_cast<float>(pageHeight) / dims.height));
  const int drawWidth = std::max(1, static_cast<int>(dims.width * scale));
  const int drawHeight = std::max(1, static_cast<int>(dims.height * scale));
  RenderConfig config{};
  config.x = (pageWidth - drawWidth) / 2;
  config.y = (pageHeight - drawHeight) / 2;
  config.maxWidth = drawWidth;
  config.maxHeight = drawHeight;
  config.useExactDimensions = true;
  PngToFramebufferConverter converter;
  renderer.clearScreen();
  if (!converter.decodeToFramebuffer(filePath, renderer, config)) {
    drawImageError();
    return false;
  }
  const bool hasPrevious = currentImageIndex > 0;
  const bool hasNext = currentImageIndex >= 0 && static_cast<size_t>(currentImageIndex + 1) < siblingCount;
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), tr(STR_SET_SLEEP_COVER), hasPrevious ? "<" : "", hasNext ? ">" : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  return true;
}

void BmpViewerActivity::goToSibling(size_t index) {
  if (!siblingEntry || !siblingIndex.entryAt(index, *siblingEntry)) {
    drawImageError();
    return;
  }
  std::string directory = FsHelpers::extractFolderPath(filePath);
  if (directory.back() != '/') directory += '/';
  filePath = directory + siblingEntry->name;
  currentImageIndex = static_cast<int>(index);
  onEnter();
}

void BmpViewerActivity::onEnter() {
  Activity::onEnter();
  imageLoaded = false;

  if (!siblingIndex.isOpen() && !filePath.empty()) {
    loadSiblingImages();
  }

  FsFile file;

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  Rect popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  GUI.fillPopupProgress(renderer, popupRect, 20);  // Initial 20% progress
  if (FsHelpers::hasPngExtension(filePath)) {
    imageLoaded = renderPngImage();
    return;
  }
  // 1. Open the file
  if (Storage.openFileForRead("BMP", filePath, file)) {
    Bitmap bitmap(file, true);

    // 2. Parse headers to get dimensions
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      int x, y;

      if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
        float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
        const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

        if (ratio > screenRatio) {
          // Wider than screen
          x = 0;
          y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
        } else {
          // Taller than screen
          x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
          y = 0;
        }
      } else {
        // Center small images
        x = (pageWidth - bitmap.getWidth()) / 2;
        y = (pageHeight - bitmap.getHeight()) / 2;
      }

      // 4. Prepare Rendering
      bool hasPrevious = (siblingCount > 1 && currentImageIndex > 0);
      bool hasNext =
          (siblingCount > 1 && currentImageIndex != -1 && currentImageIndex < static_cast<int>(siblingCount) - 1);

      const auto labels =
          mappedInput.mapLabels(tr(STR_BACK), tr(STR_SET_SLEEP_COVER), (hasPrevious ? "<" : ""), (hasNext ? ">" : ""));
      GUI.fillPopupProgress(renderer, popupRect, 50);

      renderer.clearScreen();
      // Assuming drawBitmap defaults to 0,0 crop if omitted, or pass explicitly: drawBitmap(bitmap, x, y, pageWidth,
      // pageHeight, 0, 0)
      renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0);

      // Draw UI hints on the base layer
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      // Single pass for non-grayscale images

      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      imageLoaded = true;

    } else {
      drawImageError();
    }

    file.close();
  } else {
    drawImageError();
  }
}

void BmpViewerActivity::onExit() {
  Activity::onExit();
  siblingIndex.close();
  siblingEntry.reset();
  siblingCount = 0;
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void BmpViewerActivity::doSetSleepCover() {
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));

  constexpr const char* target = "/sleep.bmp";
  constexpr const char* temporary = "/sleep.bmp.tmp";
  constexpr const char* backup = "/sleep.bmp.bak";
  AtomicFileReplace::recover(Storage, target, backup);
  bool success = false;
  FsFile inFile, outFile;
  if (Storage.openFileForRead("IMAGE", filePath, inFile) && Storage.openFileForWrite("IMAGE", temporary, outFile)) {
    if (FsHelpers::hasPngExtension(filePath)) {
      // Convert into the existing BMP setting so selecting a PNG does not need
      // to delete an earlier sleep.bmp just to make sleep.png take precedence.
      CheckedImageOutput output(outFile);
      success = PngToBmpConverter::pngFileToBmpStreamWithSize(inFile, output, renderer.getScreenWidth(),
                                                              renderer.getScreenHeight()) &&
                !output.failed;
    } else {
      uint8_t buffer[256];
      const uint64_t expected = inFile.fileSize64();
      uint64_t copied = 0;
      int bytesRead;
      success = true;
      while ((bytesRead = inFile.read(buffer, sizeof(buffer))) > 0) {
        if (outFile.write(buffer, bytesRead) != static_cast<size_t>(bytesRead)) {
          success = false;
          break;
        }
        copied += bytesRead;
        delay(1);
      }
      success = success && bytesRead == 0 && copied == expected;
    }
    success = outFile.close() && success;
    inFile.close();
  }
  if (outFile) outFile.close();
  if (inFile) inFile.close();
  if (success) {
    FsFile check;
    success = Storage.openFileForRead("IMAGE", temporary, check);
    if (success) {
      Bitmap bitmap(check, true);
      success = bitmap.parseHeaders() == BmpReaderError::Ok;
      check.close();
    }
  }
  success = success && AtomicFileReplace::promote(Storage, temporary, target, backup);
  if (success) {
    SleepScreenCache::invalidateAll();
    SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM;
    SETTINGS.saveToFile();
    GUI.drawPopup(renderer, tr(STR_DONE));
  } else {
    GUI.drawPopup(renderer, tr(STR_FAILED_LOWER));
  }

  delay(1000);
  onEnter();
}

void BmpViewerActivity::loop() {
  // Keep CPU awake/polling so 1st click works
  Activity::loop();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToFileBrowser(filePath);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (imageLoaded) doSetSleepCover();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (siblingCount > 1 && currentImageIndex > 0) {
      goToSibling(currentImageIndex - 1);
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (siblingCount > 1 && currentImageIndex != -1 && currentImageIndex < static_cast<int>(siblingCount) - 1) {
      goToSibling(currentImageIndex + 1);
    }
    return;
  }
}
