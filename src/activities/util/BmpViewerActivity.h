#pragma once

#include <FileIndex.h>

#include <functional>
#include <string>

#include "../Activity.h"
#include "MappedInputManager.h"

class BmpViewerActivity final : public Activity {
 public:
  BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  uint8_t getUiTransitionRefreshWeight() const override { return UI_TRANSITION_REFRESH_WEIGHT_DENSE; }

 private:
  void loadSiblingImages();
  void doSetSleepCover();
  bool renderPngImage();
  void drawImageError();
  void goToSibling(size_t index);

  std::string filePath;
  FileIndex siblingIndex;
  std::unique_ptr<FileIndex::Entry> siblingEntry;
  size_t siblingCount = 0;
  bool imageLoaded = false;
  int currentImageIndex = -1;
};
