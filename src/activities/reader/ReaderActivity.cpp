#include "ReaderActivity.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <I18n.h>

#include <optional>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "Epub.h"
#include "EpubReaderActivity.h"
#include "KOReaderCredentialStore.h"
#include "Txt.h"
#include "TxtReaderActivity.h"
#include "Xtc.h"
#include "XtcReaderActivity.h"
#include "activities/util/BmpViewerActivity.h"
#include "activities/util/FullScreenMessageActivity.h"
#include "components/UITheme.h"

bool ReaderActivity::isXtcFile(const std::string& path) { return FsHelpers::hasXtcExtension(path); }

bool ReaderActivity::isTxtFile(const std::string& path) {
  return FsHelpers::hasTxtExtension(path) ||
         FsHelpers::hasMarkdownExtension(path);  // Treat .md as txt files (until we have a markdown reader)
}

bool ReaderActivity::isBmpFile(const std::string& path) {
  return FsHelpers::hasBmpExtension(path) || FsHelpers::hasPngExtension(path);
}

std::unique_ptr<Epub> ReaderActivity::loadEpub(const std::string& path) {
  auto epub = std::unique_ptr<Epub>(new Epub(path, "/.crosspoint"));
  const bool uncached = !Storage.exists((epub->getCachePath() + "/book.bin").c_str());
  if (uncached) GUI.drawPopup(renderer, tr(STR_INDEXING));
  bool loaded = false;
  {
    std::optional<GfxRenderer::FrameBufferLoan> loan;
    if (uncached) loan.emplace(renderer);
    loaded = epub->load(true, SETTINGS.embeddedStyle == 0);
  }
  if (loaded) {
    return epub;
  }

  LOG_ERR("READER", "Failed to load epub");
  return nullptr;
}

std::unique_ptr<Xtc> ReaderActivity::loadXtc(const std::string& path) {
  auto xtc = std::unique_ptr<Xtc>(new Xtc(path, "/.crosspoint"));
  if (xtc->load()) {
    return xtc;
  }

  LOG_ERR("READER", "Failed to load XTC");
  return nullptr;
}

std::unique_ptr<Txt> ReaderActivity::loadTxt(const std::string& path) {
  auto txt = std::unique_ptr<Txt>(new Txt(path, "/.crosspoint"));
  if (txt->load()) {
    return txt;
  }

  LOG_ERR("READER", "Failed to load TXT");
  return nullptr;
}

void ReaderActivity::goToLibrary(const std::string& fromBookPath) {
  // If coming from a book, start in that book's folder; otherwise start from root
  auto initialPath = fromBookPath.empty() ? "/" : FsHelpers::extractFolderPath(fromBookPath);
  activityManager.goToFileBrowser(std::move(initialPath));
}

void ReaderActivity::onGoToEpubReader(std::unique_ptr<Epub> epub) {
  const auto epubPath = epub->getPath();
  currentBookPath = epubPath;

  auto& sync = APP_STATE.koReaderSyncSession;
  const bool canAutoPull =
      SETTINGS.koSyncAutoPullOnOpen && KOREADER_STORE.hasCredentials() && !initialBookmark.enabled && !sync.active;
  if (canAutoPull) {
    sync.clear();
    sync.active = true;
    sync.epubPath = epubPath;
    sync.spineIndex = 0;
    sync.page = 0;
    sync.totalPagesInSpine = 0;
    sync.intent = KOReaderSyncIntentState::AUTO_PULL;
    sync.outcome = KOReaderSyncOutcomeState::PENDING;
    sync.autoPullEpubPath = epubPath;
    APP_STATE.saveToFile();

    LOG_DBG("READER", "Auto-pull KOReader sync before opening EPUB: %s", epubPath.c_str());
    activityManager.goToKOReaderSync();
    return;
  }

  activityManager.replaceActivity(std::make_unique<EpubReaderActivity>(
      renderer, mappedInput, std::move(epub), initialBookmark.enabled ? initialBookmark.spineIndex : -1,
      initialBookmark.enabled ? static_cast<int>(initialBookmark.page) : -1,
      initialBookmark.enabled && initialBookmark.hasVisibleTextOffset
          ? std::optional<uint32_t>(initialBookmark.visibleTextOffset)
          : std::nullopt));
}

void ReaderActivity::onGoToBmpViewer(const std::string& path) {
  activityManager.replaceActivity(std::make_unique<BmpViewerActivity>(renderer, mappedInput, path));
}

void ReaderActivity::onGoToXtcReader(std::unique_ptr<Xtc> xtc) {
  const auto xtcPath = xtc->getPath();
  currentBookPath = xtcPath;
  activityManager.replaceActivity(std::make_unique<XtcReaderActivity>(renderer, mappedInput, std::move(xtc)));
}

void ReaderActivity::onGoToTxtReader(std::unique_ptr<Txt> txt) {
  const auto txtPath = txt->getPath();
  currentBookPath = txtPath;
  activityManager.replaceActivity(std::make_unique<TxtReaderActivity>(renderer, mappedInput, std::move(txt)));
}

void ReaderActivity::onEnter() {
  Activity::onEnter();

  if (initialBookPath.empty()) {
    goToLibrary();  // Start from root when entering via Browse
    return;
  }

  currentBookPath = initialBookPath;
  if (APP_STATE.koReaderSyncSession.active && APP_STATE.koReaderSyncSession.epubPath == initialBookPath) {
    LOG_DBG("READER", "Opening EPUB with pending KOReader sync outcome=%d",
            static_cast<int>(APP_STATE.koReaderSyncSession.outcome));
  }
  if (isBmpFile(initialBookPath)) {
    onGoToBmpViewer(initialBookPath);
  } else if (isXtcFile(initialBookPath)) {
    auto xtc = loadXtc(initialBookPath);
    if (!xtc) {
      goToLibrary(initialBookPath);
      return;
    }
    onGoToXtcReader(std::move(xtc));
  } else if (isTxtFile(initialBookPath)) {
    auto txt = loadTxt(initialBookPath);
    if (!txt) {
      goToLibrary(initialBookPath);
      return;
    }
    onGoToTxtReader(std::move(txt));
  } else {
    auto epub = loadEpub(initialBookPath);
    if (!epub) {
      goToLibrary(initialBookPath);
      return;
    }
    onGoToEpubReader(std::move(epub));
  }
}

void ReaderActivity::onGoBack() { finish(); }
