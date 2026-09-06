#pragma once

#include <string>

#include "activities/Activity.h"

class Bitmap;
class HalFile;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromTimeout = false)
      : Activity("Sleep", renderer, mappedInput), fromTimeout(fromTimeout) {}
  void onEnter() override;

 private:
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  void renderCoverSleepScreen() const;
  void renderReadingDashboardSleepScreen() const;
  void renderCoverStatsSleepScreen(bool footerOnly = false) const;
  void renderCustomStatsSleepScreen(bool footerOnly = false) const;
  // preserveBackground: draw over the retained frame (transparent overlays) instead of clearing first.
  void renderBitmapSleepScreen(const Bitmap& bitmap, const std::string& sourcePath = "",
                               bool preserveBackground = false) const;
  bool renderPngSleepScreen(const std::string& sourcePath) const;
  // Transparent overlay sleep (upstream): alpha BMP/PNG art composited over the last screen.
  bool renderSleepOverlayFile(HalFile& file, const char* pathForLog) const;
  bool renderTransparentOverlayPng(const std::string& path) const;
  bool renderSleepOverlayPath(const std::string& path) const;
  void renderTransparentCustomSleepScreen() const;
  // Quick Resume: keep the last screen and add a small moon marker.
  void renderLastScreenSleepScreen() const;
  void renderBlankSleepScreen() const;
  bool resolveLastBookCoverPath(std::string& coverBmpPath) const;

  // True when sleep was triggered by the inactivity timeout (Quick Resume after timeout).
  bool fromTimeout = false;
};
