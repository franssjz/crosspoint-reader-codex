#pragma once
#include <cstdint>
#include <memory>

#include "../Activity.h"
#include "activities/home/FileBrowserActivity.h"

class Epub;
class Xtc;
class Txt;

class ReaderActivity final : public Activity {
 public:
  struct EpubBookmarkLaunch {
    bool enabled = false;
    int spineIndex = 0;
    uint32_t page = 0;
    bool hasVisibleTextOffset = false;
    uint32_t visibleTextOffset = 0;
  };

 private:
  std::string initialBookPath;
  std::string currentBookPath;  // Track current book path for navigation
  EpubBookmarkLaunch initialBookmark;
  std::unique_ptr<Epub> loadEpub(const std::string& path);
  static std::unique_ptr<Xtc> loadXtc(const std::string& path);
  static std::unique_ptr<Txt> loadTxt(const std::string& path);
  static bool isXtcFile(const std::string& path);
  static bool isTxtFile(const std::string& path);
  static bool isBmpFile(const std::string& path);

  void goToLibrary(const std::string& fromBookPath = "");
  void onGoToEpubReader(std::unique_ptr<Epub> epub);
  void onGoToXtcReader(std::unique_ptr<Xtc> xtc);
  void onGoToTxtReader(std::unique_ptr<Txt> txt);
  void onGoToBmpViewer(const std::string& path);

  void onGoBack();

 public:
  explicit ReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialBookPath)
      : ReaderActivity(renderer, mappedInput, std::move(initialBookPath), EpubBookmarkLaunch{}) {}

  explicit ReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialBookPath,
                          EpubBookmarkLaunch initialBookmark)
      : Activity("Reader", renderer, mappedInput),
        initialBookPath(std::move(initialBookPath)),
        initialBookmark(initialBookmark) {}
  void onEnter() override;
  bool isReaderActivity() const override { return true; }
};
