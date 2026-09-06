#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "activities/Activity.h"

class Epub;
class Xtc;
class Txt;

// Reader entry point. Loads the book on the way in and replaces itself with the
// format-specific reader (EPUB / XTC / TXT+Markdown). The fork keeps this as a
// concrete dispatcher (rather than upstream's abstract base) so that
// ActivityManager can both construct it directly with a bookmark launch target
// and go through the upstream-style `create()` factory.
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
  // Upstream: the first page of a book opened from a fresh boot may skip the
  // clean HALF refresh so the boot screen transitions quickly. Forwarded to the
  // format reader, which owns the refresh counter.
  bool allowFastInitialRefresh = false;

  std::unique_ptr<Epub> loadEpub(const std::string& path, bool& uncached);
  static std::unique_ptr<Xtc> loadXtc(const std::string& path);
  static std::unique_ptr<Txt> loadTxt(const std::string& path);
  static bool isXtcFile(const std::string& path);
  static bool isTxtFile(const std::string& path);
  static bool isBmpFile(const std::string& path);

  void goToLibrary(const std::string& fromBookPath = "");
  void onGoToEpubReader(std::unique_ptr<Epub> epub, bool allowFastRefresh);
  void onGoToXtcReader(std::unique_ptr<Xtc> xtc);
  void onGoToTxtReader(std::unique_ptr<Txt> txt);
  void onGoToBmpViewer(const std::string& path);

 public:
  explicit ReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialBookPath,
                          bool allowFastInitialRefresh = false)
      : ReaderActivity(renderer, mappedInput, std::move(initialBookPath), EpubBookmarkLaunch{},
                       allowFastInitialRefresh) {}

  explicit ReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialBookPath,
                          EpubBookmarkLaunch initialBookmark, bool allowFastInitialRefresh = false)
      : Activity("Reader", renderer, mappedInput),
        initialBookPath(std::move(initialBookPath)),
        initialBookmark(initialBookmark),
        allowFastInitialRefresh(allowFastInitialRefresh) {}

  // Upstream factory used by ActivityManager::goToReader(). Returns nullptr on OOM.
  static std::unique_ptr<ReaderActivity> create(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                std::string path, bool allowFastInitialRefresh);

  void onEnter() override;
  bool isReaderActivity() const override { return true; }
};
