#include "KOReaderSyncActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <SdCardFont.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include <cmath>

#include "AchievementsStore.h"
#include "CrossPointSettings.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderDocumentId.h"
#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "CrossPointState.h"
#include "activities/apps/ReadingStatsDetailActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/AchievementPopupUtils.h"
#include "util/CompletedBookMover.h"
#include "util/NetworkMemory.h"
#include "util/TimeUtils.h"

namespace {
constexpr time_t NTP_RESYNC_MIN_INTERVAL_SEC = 15 * 60;

std::string calculateDocumentHashForMethod(const std::string& path, const DocumentMatchMethod method) {
  return method == DocumentMatchMethod::FILENAME ? KOReaderDocumentId::calculateFromFilename(path)
                                                 : KOReaderDocumentId::calculate(path);
}

DocumentMatchMethod alternateMatchMethod(const DocumentMatchMethod method) {
  return method == DocumentMatchMethod::FILENAME ? DocumentMatchMethod::BINARY : DocumentMatchMethod::FILENAME;
}

void logSyncMemSnapshot(const char* stage) { NetworkMemory::logSnapshot("KOSync", stage); }

void prepareMemoryBeforeNetwork(GfxRenderer& renderer, const char* stage) {
  NetworkMemory::prepareBeforeNetwork(renderer, "KOSync", stage);
}

void restoreMemoryAfterNetwork(GfxRenderer& renderer, const char* stage) {
  NetworkMemory::restoreAfterNetwork(renderer, "KOSync", stage);
}

void syncTimeWithNTP() {
  const bool ntpSuccess = TimeUtils::syncTimeWithNtp(5000);
  if (ntpSuccess) {
    LOG_DBG("KOSync", "NTP time synced");
  } else {
    LOG_DBG("KOSync", "NTP sync timeout, using fallback");
  }

  const uint32_t currentValidTimestamp = TimeUtils::getCurrentValidTimestamp();
  if (ntpSuccess && currentValidTimestamp > 0) {
    APP_STATE.registerValidTimeSync(currentValidTimestamp);
    APP_STATE.saveToFile();
  }

  TimeUtils::stopNtp();
}

// Simple debounce: skip NTP if we already synced less than 15 min ago this session.
static unsigned long s_lastNtpSyncMs = 0;

bool shouldSyncNtpNow() {
  if (s_lastNtpSyncMs == 0) {
    return true;
  }
  const unsigned long ageSec = (millis() - s_lastNtpSyncMs) / 1000UL;
  return ageSec >= static_cast<unsigned long>(NTP_RESYNC_MIN_INTERVAL_SEC);
}

bool isAutomaticSyncIntent(const KOReaderSyncIntentState intent) {
  return intent == KOReaderSyncIntentState::AUTO_PULL || intent == KOReaderSyncIntentState::AUTO_PUSH;
}

void wifiOff() {
  TimeUtils::stopNtp();
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);
}
}  // namespace

void KOReaderSyncActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    LOG_DBG("KOSync", "WiFi connection failed, resuming reader");
    resumeReader(KOReaderSyncOutcomeState::CANCELLED);
    return;
  }

  LOG_DBG("KOSync", "WiFi connected, starting sync");

  {
    RenderLock lock(*this);
    state = SYNCING;
    statusMessage = tr(STR_SYNCING_TIME);
  }
  requestUpdate();

  if (shouldSyncNtpNow()) {
    syncTimeWithNTP();
    s_lastNtpSyncMs = millis();
  } else {
    LOG_DBG("KOSync", "Skipping NTP sync (recently synced)");
  }

  {
    RenderLock lock(*this);
    statusMessage = tr(STR_CALC_HASH);
  }
  requestUpdateAndWait();

  logSyncMemSnapshot("before_performSync");
  prepareNetworkMemory("after_trim_before_performSync");

  performSync();

  restoreNetworkMemory("after_performSync_restore");
  logSyncMemSnapshot("after_performSync");
}

void KOReaderSyncActivity::performSync() {
  const DocumentMatchMethod primaryMethod = KOREADER_STORE.getMatchMethod();
  const bool smartSync = KOREADER_STORE.getSyncBehavior() == KOReaderSyncBehavior::SMART &&
                         syncIntent == KOReaderSyncIntentState::COMPARE;
  documentHash = calculateDocumentHashForMethod(epubPath, primaryMethod);
  if (documentHash.empty()) {
    {
      RenderLock lock(*this);
      state = SYNC_FAILED;
      statusMessage = tr(STR_HASH_FAILED);
    }
    requestUpdate(true);
    return;
  }

  LOG_DBG("KOSync", "Document hash: %s", documentHash.c_str());

  // Local mapping is only needed for compare/upload paths.
  if (syncIntent != KOReaderSyncIntentState::PULL_REMOTE && syncIntent != KOReaderSyncIntentState::AUTO_PULL) {
    if (!hasLocalProgress) {
      {
        RenderLock lock(*this);
        statusMessage = tr(STR_MAPPING_LOCAL);
      }
      requestUpdateAndWait();
      if (!computeLocalProgressAndChapter()) {
        {
          RenderLock lock(*this);
          state = SYNC_FAILED;
          statusMessage = tr(STR_SYNC_FAILED_MSG);
        }
        requestUpdate(true);
        return;
      }
    }
  }

  releaseEpubForMapping();

  // Push intent warms the session first so PUT can reuse the connection.
  if (syncIntent == KOReaderSyncIntentState::PUSH_LOCAL || syncIntent == KOReaderSyncIntentState::AUTO_PUSH) {
    prepareNetworkMemory("before_push_warmup_get");
    KOReaderSyncClient::beginPersistentSession();
    KOReaderProgress warmupProgress;
    auto warmupResult = KOReaderSyncClient::getProgress(documentHash, warmupProgress);
    if (warmupResult == KOReaderSyncClient::NOT_FOUND && retryWithBinaryDocumentHash()) {
      warmupResult = KOReaderSyncClient::getProgress(documentHash, warmupProgress);
    }
    if (warmupResult != KOReaderSyncClient::OK && warmupResult != KOReaderSyncClient::NOT_FOUND) {
      KOReaderSyncClient::endPersistentSession();
      {
        RenderLock lock(*this);
        state = SYNC_FAILED;
        statusMessage = KOReaderSyncClient::errorString(warmupResult);
        const char* detail = KOReaderSyncClient::lastFailureDetail();
        if (detail && detail[0]) {
          statusMessage += " \xe2\x80\x94 ";
          statusMessage += detail;
        }
      }
      requestUpdate(true);
      return;
    }
    if (syncIntent == KOReaderSyncIntentState::AUTO_PUSH && warmupResult == KOReaderSyncClient::OK &&
        warmupProgress.percentage > localProgress.percentage + 0.0005f) {
      LOG_INF("KOSync", "Auto-push skipped because remote progress is ahead: remote=%.4f local=%.4f",
              warmupProgress.percentage, localProgress.percentage);
      KOReaderSyncClient::endPersistentSession();
      wifiOff();
      resumeReader(KOReaderSyncOutcomeState::UPLOAD_COMPLETE);
      return;
    }
    performUpload();
    return;
  }

  {
    RenderLock lock(*this);
    statusMessage = tr(STR_FETCH_PROGRESS);
  }
  requestUpdateAndWait();
  prepareNetworkMemory("before_getProgress");

  KOReaderSyncClient::beginPersistentSession();

  auto result = KOReaderSyncClient::getProgress(documentHash, remoteProgress);
  if (result == KOReaderSyncClient::NOT_FOUND && retryWithBinaryDocumentHash()) {
    result = KOReaderSyncClient::getProgress(documentHash, remoteProgress);
  }

  if (smartSync) {
    const std::string alternateHash = calculateDocumentHashForMethod(epubPath, alternateMatchMethod(primaryMethod));
    if (!alternateHash.empty() && alternateHash != documentHash) {
      KOReaderProgress alternateProgress{};
      const auto alternateResult = KOReaderSyncClient::getProgress(alternateHash, alternateProgress);
      if (alternateResult == KOReaderSyncClient::OK &&
          (result == KOReaderSyncClient::NOT_FOUND || alternateProgress.percentage > remoteProgress.percentage)) {
        documentHash = alternateHash;
        remoteProgress = std::move(alternateProgress);
        result = KOReaderSyncClient::OK;
      }
    }
  }

  if (result == KOReaderSyncClient::NOT_FOUND) {
    if (smartSync) {
      performUpload();
      return;
    }
    if (syncIntent == KOReaderSyncIntentState::AUTO_PULL) {
      KOReaderSyncClient::endPersistentSession();
      wifiOff();
      LOG_DBG("KOSync", "Auto-pull found no remote progress; opening local progress");
      resumeReader(KOReaderSyncOutcomeState::CANCELLED);
      return;
    }

    if (syncIntent == KOReaderSyncIntentState::PULL_REMOTE) {
      KOReaderSyncClient::endPersistentSession();
      {
        RenderLock lock(*this);
        state = SYNC_FAILED;
        statusMessage = tr(STR_NO_REMOTE_MSG);
      }
      requestUpdate(true);
      return;
    }

    KOReaderSyncClient::endPersistentSession();
    {
      RenderLock lock(*this);
      state = NO_REMOTE_PROGRESS;
      hasRemoteProgress = false;
    }
    requestUpdate(true);
    return;
  }

  if (result != KOReaderSyncClient::OK) {
    KOReaderSyncClient::endPersistentSession();
    {
      RenderLock lock(*this);
      state = SYNC_FAILED;
      statusMessage = KOReaderSyncClient::errorString(result);
      const char* detail = KOReaderSyncClient::lastFailureDetail();
      if (detail && detail[0]) {
        statusMessage += " \xe2\x80\x94 ";
        statusMessage += detail;
      }
    }
    requestUpdate(true);
    return;
  }

  hasRemoteProgress = false;
  remotePositionMapped = false;
  remotePosition.spineIndex = -1;
  remotePosition.pageNumber = -1;
  remotePosition.totalPages = 0;
  remotePosition.paragraphIndex = 0;
  remotePosition.hasParagraphIndex = false;
  remotePosition.listItemIndex = 0;
  remotePosition.hasListItemIndex = false;
  remoteChapterLabel.clear();

  if (syncIntent == KOReaderSyncIntentState::PULL_REMOTE || syncIntent == KOReaderSyncIntentState::AUTO_PULL) {
    if (!ensureRemotePositionMapped()) {
      if (syncIntent == KOReaderSyncIntentState::AUTO_PULL) {
        wifiOff();
        resumeReader(KOReaderSyncOutcomeState::CANCELLED);
        return;
      }
      {
        RenderLock lock(*this);
        state = SYNC_FAILED;
        statusMessage = tr(STR_SYNC_FAILED_MSG);
      }
      requestUpdate(true);
      return;
    }

    auto& sync = APP_STATE.koReaderSyncSession;
    sync.outcome = KOReaderSyncOutcomeState::APPLIED_REMOTE;
    sync.resultSpineIndex = remotePosition.spineIndex;
    sync.resultPage = remotePosition.pageNumber;
    sync.resultParagraphIndex = remotePosition.paragraphIndex;
    sync.resultHasParagraphIndex = remotePosition.hasParagraphIndex;
    sync.resultListItemIndex = remotePosition.listItemIndex;
    sync.resultHasListItemIndex = remotePosition.hasListItemIndex;
    APP_STATE.saveToFile();
    if (syncIntent == KOReaderSyncIntentState::AUTO_PULL) {
      wifiOff();
      resumeReader(KOReaderSyncOutcomeState::APPLIED_REMOTE);
      return;
    }
    {
      RenderLock lock(*this);
      state = APPLY_COMPLETE;
      uploadCompleteTime = millis();
    }
    requestUpdate(true);
    return;
  }

  // Compare intent: pre-map remote so chooser always shows concrete data.
  if (!ensureRemotePositionMapped()) {
    {
      RenderLock lock(*this);
      state = SYNC_FAILED;
      statusMessage = tr(STR_SYNC_FAILED_MSG);
    }
    requestUpdate(true);
    return;
  }

  if (smartSync) {
    static constexpr float SAME_PROGRESS_EPSILON = 0.001f;
    const float delta = localProgress.percentage - remoteProgress.percentage;
    if (std::fabs(delta) <= SAME_PROGRESS_EPSILON) {
      wifiOff();
      resumeReader(KOReaderSyncOutcomeState::UPLOAD_COMPLETE);
      return;
    }
    if (delta > 0.0f) {
      performUpload();
      return;
    }
    const SyncResult applied = {remotePosition.spineIndex,
                                remotePosition.pageNumber,
                                remotePosition.paragraphIndex,
                                remotePosition.hasParagraphIndex,
                                remotePosition.listItemIndex,
                                remotePosition.hasListItemIndex};
    wifiOff();
    resumeReader(KOReaderSyncOutcomeState::APPLIED_REMOTE, &applied);
    return;
  }

  releaseEpubForMapping();

  {
    RenderLock lock(*this);
    state = SHOWING_RESULT;

    auto isLocalAhead = [&]() {
      if (remotePosition.spineIndex < 0) {
        return localProgress.percentage > remoteProgress.percentage;
      }
      if (currentSpineIndex != remotePosition.spineIndex) {
        return currentSpineIndex > remotePosition.spineIndex;
      }
      if (currentPage != remotePosition.pageNumber) {
        return currentPage > remotePosition.pageNumber;
      }
      if (hasLocalParagraphIndex && remotePosition.hasParagraphIndex) {
        return localParagraphIndex > remotePosition.paragraphIndex;
      }
      return false;
    };
    selectedOption = isLocalAhead() ? 1 : 0;
  }
  requestUpdate(true);
}

void KOReaderSyncActivity::performUpload() {
  {
    RenderLock lock(*this);
    state = UPLOADING;
    statusMessage = tr(STR_UPLOAD_PROGRESS);
  }
  requestUpdateAndWait();

  if (!hasLocalProgress || localProgress.xpath.empty()) {
    if (!computeLocalProgressAndChapter()) {
      {
        RenderLock lock(*this);
        state = SYNC_FAILED;
        statusMessage = tr(STR_SYNC_FAILED_MSG);
      }
      requestUpdate(true);
      return;
    }
    releaseEpubForMapping();
  }

  if (localProgress.xpath.empty()) {
    {
      RenderLock lock(*this);
      state = SYNC_FAILED;
      statusMessage = tr(STR_SYNC_FAILED_MSG);
    }
    requestUpdate(true);
    return;
  }

  prepareNetworkMemory("after_trim_before_updateProgress");
  logSyncMemSnapshot("before_updateProgress");

  KOReaderSyncClient::beginPersistentSession();

  KOReaderProgress progress;
  progress.document = documentHash;
  progress.progress = localProgress.xpath;
  progress.percentage = localProgress.percentage;

  if (KOREADER_STORE.getSendMetadata()) {
    KOReaderMetadata metadata;
    const auto lastSlash = epubPath.rfind('/');
    metadata.filename = lastSlash == std::string::npos ? epubPath : epubPath.substr(lastSlash + 1);
    if (ensureEpubLoadedForMapping()) {
      metadata.title = epub->getTitle();
      metadata.authors = epub->getAuthor();
      releaseEpubForMapping();
    }
    progress.metadata = std::move(metadata);
  }

  const auto result = KOReaderSyncClient::updateProgress(progress);
  KOReaderSyncClient::endPersistentSession();
  logSyncMemSnapshot("after_updateProgress");
  restoreNetworkMemory("after_updateProgress_restore");

  if (result != KOReaderSyncClient::OK) {
    wifiOff();
    {
      RenderLock lock(*this);
      state = SYNC_FAILED;
      statusMessage = KOReaderSyncClient::errorString(result);
      const char* detail = KOReaderSyncClient::lastFailureDetail();
      if (detail && detail[0]) {
        statusMessage += " \xe2\x80\x94 ";
        statusMessage += detail;
      }
    }
    requestUpdate();
    return;
  }

  wifiOff();
  APP_STATE.koReaderSyncSession.outcome = KOReaderSyncOutcomeState::UPLOAD_COMPLETE;
  APP_STATE.saveToFile();
  if (syncIntent == KOReaderSyncIntentState::AUTO_PUSH) {
    resumeReader(KOReaderSyncOutcomeState::UPLOAD_COMPLETE);
    return;
  }
  {
    RenderLock lock(*this);
    state = UPLOAD_COMPLETE;
    uploadCompleteTime = millis();
  }
  requestUpdate(true);
}

void KOReaderSyncActivity::prepareNetworkMemory(const char* stage) {
  prepareMemoryBeforeNetwork(renderer, stage);
  networkMemoryReleasePending = true;
}

void KOReaderSyncActivity::restoreNetworkMemory(const char* stage) {
  if (!networkMemoryReleasePending) {
    return;
  }
  restoreMemoryAfterNetwork(renderer, stage);
  networkMemoryReleasePending = false;
}

void KOReaderSyncActivity::onEnter() {
  Activity::onEnter();
  logSyncMemSnapshot("onEnter_begin");
  LOG_DBG("KOSync", "Standalone sync: path=%s spine=%d page=%d/%d intent=%d", epubPath.c_str(), currentSpineIndex,
          currentPage, totalPagesInSpine, static_cast<int>(syncIntent));

  if (!KOREADER_STORE.hasCredentials()) {
    state = NO_CREDENTIALS;
    requestUpdate();
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    LOG_DBG("KOSync", "Already connected to WiFi");
    onWifiSelectionComplete(true);
    return;
  }

  const bool automaticSync = isAutomaticSyncIntent(syncIntent);
  const bool chooseWifiManually =
      !automaticSync && SETTINGS.syncDayWifiChoice == CrossPointSettings::SYNC_DAY_WIFI_MANUAL;
  LOG_DBG("KOSync", "Launching WifiSelectionActivity...");
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, !chooseWifiManually, true,
                                                                 automaticSync),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void KOReaderSyncActivity::onExit() {
  Activity::onExit();

  logSyncMemSnapshot("onExit_before_cleanup");
  KOReaderSyncClient::endPersistentSession();
  restoreNetworkMemory("onExit_restore");
  wifiOff();
  releaseEpubForMapping();
  logSyncMemSnapshot("onExit_after_cleanup");
}

void KOReaderSyncActivity::closeCancelled() {
  if (closeRequested) {
    return;
  }
  resumeReader(KOReaderSyncOutcomeState::CANCELLED);
}

void KOReaderSyncActivity::resumeReader(const KOReaderSyncOutcomeState outcome, const SyncResult* appliedResult) {
  if (closeRequested) {
    return;
  }

  closeRequested = true;
  restoreNetworkMemory("before_resume_reader_restore");
  auto& sync = APP_STATE.koReaderSyncSession;
  sync.outcome = outcome;
  if (appliedResult) {
    sync.resultSpineIndex = appliedResult->spineIndex;
    sync.resultPage = appliedResult->page;
    sync.resultParagraphIndex = appliedResult->paragraphIndex;
    sync.resultHasParagraphIndex = appliedResult->hasParagraphIndex;
    sync.resultListItemIndex = appliedResult->listItemIndex;
    sync.resultHasListItemIndex = appliedResult->hasListItemIndex;
  } else if (outcome != KOReaderSyncOutcomeState::APPLIED_REMOTE) {
    sync.resultSpineIndex = 0;
    sync.resultPage = 0;
    sync.resultParagraphIndex = 0;
    sync.resultHasParagraphIndex = false;
    sync.resultListItemIndex = 0;
    sync.resultHasListItemIndex = false;
  }
  APP_STATE.saveToFile();
  logSyncMemSnapshot("before_resume_reader");
  if (sync.exitToHomeAfterSync || syncIntent == KOReaderSyncIntentState::AUTO_PUSH) {
    returnAfterAutoPush();
    return;
  }
  activityManager.goToReader(epubPath);
}

void KOReaderSyncActivity::returnAfterAutoPush() {
  APP_STATE.koReaderSyncSession.clear();
  APP_STATE.saveToFile();

  std::string finalBookPath = epubPath;
  const auto moveResult = CompletedBookMover::moveCompletedBookIfEnabled(epubPath);
  if (moveResult.moved) {
    finalBookPath = moveResult.destinationPath;
  }

  showPendingAchievementPopups(renderer);

  const auto snapshot = READING_STATS.getLastSessionSnapshot();
  const bool countedSession =
      snapshot.valid && snapshot.counted && (snapshot.path == epubPath || snapshot.path == finalBookPath);
  if (SETTINGS.showStatsAfterReading && countedSession && !finalBookPath.empty()) {
    activityManager.replaceActivity(
        std::make_unique<ReadingStatsDetailActivity>(renderer, mappedInput, finalBookPath, ReadingStatsDetailContext{true}));
  } else {
    activityManager.goHome();
  }
}

void KOReaderSyncActivity::render(RenderLock&&) {
  const auto pageWidth = renderer.getScreenWidth();

  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, 15, tr(STR_KOREADER_SYNC), true, EpdFontFamily::BOLD);

  if (state == NO_CREDENTIALS) {
    renderer.drawCenteredText(UI_10_FONT_ID, 280, tr(STR_NO_CREDENTIALS_MSG), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, 320, tr(STR_KOREADER_SETUP_HINT));

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == SYNCING || state == UPLOADING) {
    GUI.drawPopup(renderer, statusMessage.c_str());
    renderer.displayBuffer();
    return;
  }

  if (state == SHOWING_RESULT) {
    renderer.drawCenteredText(UI_10_FONT_ID, 120, tr(STR_PROGRESS_FOUND), true, EpdFontFamily::BOLD);

    renderer.drawText(UI_10_FONT_ID, 20, 160, tr(STR_REMOTE_LABEL), true);
    char remoteChapterStr[128];
    snprintf(remoteChapterStr, sizeof(remoteChapterStr), "  %s", remoteChapterLabel.c_str());
    renderer.drawText(UI_10_FONT_ID, 20, 185, remoteChapterStr);
    char remotePageStr[64];
    snprintf(remotePageStr, sizeof(remotePageStr), tr(STR_PAGE_OVERALL_FORMAT), remotePosition.pageNumber + 1,
             remoteProgress.percentage * 100);
    renderer.drawText(UI_10_FONT_ID, 20, 210, remotePageStr);

    if (!remoteProgress.device.empty()) {
      char deviceStr[64];
      snprintf(deviceStr, sizeof(deviceStr), tr(STR_DEVICE_FROM_FORMAT), remoteProgress.device.c_str());
      renderer.drawText(UI_10_FONT_ID, 20, 235, deviceStr);
    }

    renderer.drawText(UI_10_FONT_ID, 20, 270, tr(STR_LOCAL_LABEL), true);
    char localChapterStr[128];
    snprintf(localChapterStr, sizeof(localChapterStr), "  %s", localChapterLabel.c_str());
    renderer.drawText(UI_10_FONT_ID, 20, 295, localChapterStr);
    char localPageStr[64];
    snprintf(localPageStr, sizeof(localPageStr), tr(STR_PAGE_TOTAL_OVERALL_FORMAT), currentPage + 1, totalPagesInSpine,
             localProgress.percentage * 100);
    renderer.drawText(UI_10_FONT_ID, 20, 320, localPageStr);

    const int optionY = 350;
    const int optionHeight = 30;

    if (selectedOption == 0) {
      renderer.fillRect(0, optionY - 2, pageWidth - 1, optionHeight);
    }
    renderer.drawText(UI_10_FONT_ID, 20, optionY, tr(STR_APPLY_REMOTE), selectedOption != 0);

    if (selectedOption == 1) {
      renderer.fillRect(0, optionY + optionHeight - 2, pageWidth - 1, optionHeight);
    }
    renderer.drawText(UI_10_FONT_ID, 20, optionY + optionHeight, tr(STR_UPLOAD_LOCAL), selectedOption != 1);

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == NO_REMOTE_PROGRESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, 280, tr(STR_NO_REMOTE_MSG), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, 320, tr(STR_UPLOAD_PROMPT));

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_UPLOAD), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == UPLOAD_COMPLETE) {
    renderer.drawCenteredText(UI_10_FONT_ID, 300, tr(STR_UPLOAD_SUCCESS), true, EpdFontFamily::BOLD);

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == APPLY_COMPLETE) {
    renderer.drawCenteredText(UI_10_FONT_ID, 300, tr(STR_PULL_SUCCESS), true, EpdFontFamily::BOLD);

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == SYNC_FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, 280, tr(STR_SYNC_FAILED_MSG), true, EpdFontFamily::BOLD);

    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const auto lines = renderer.wrappedText(UI_10_FONT_ID, statusMessage.c_str(), pageWidth - 40, 4);
    int y = 320;
    for (const auto& line : lines) {
      renderer.drawCenteredText(UI_10_FONT_ID, y, line.c_str());
      y += lineHeight;
    }

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
}

bool KOReaderSyncActivity::ensureEpubLoadedForMapping() {
  if (epub) {
    return true;
  }

  epub = std::make_shared<Epub>(epubPath, "/.crosspoint");
  if (!epub->load(true, true)) {
    LOG_ERR("KOSync", "Failed to reload EPUB for mapping: %s", epubPath.c_str());
    epub.reset();
    return false;
  }
  epub->setupCacheDir();
  return true;
}

bool KOReaderSyncActivity::ensureRemotePositionMapped(const bool closeSessionBeforeMapping) {
  if (remotePositionMapped) {
    return true;
  }

  if (closeSessionBeforeMapping) {
    KOReaderSyncClient::endPersistentSession();
  }

  {
    RenderLock lock(*this);
    statusMessage = tr(STR_MAPPING_REMOTE);
  }
  requestUpdateAndWait();

  KOReaderPosition koPos = {remoteProgress.progress, remoteProgress.percentage};
  if (!ensureEpubLoadedForMapping()) {
    return false;
  }
  remotePosition = ProgressMapper::toCrossPoint(epub, koPos, currentSpineIndex, totalPagesInSpine);
  computeRemoteChapter();
  releaseEpubForMapping();
  hasRemoteProgress = true;
  remotePositionMapped = true;
  return true;
}

void KOReaderSyncActivity::releaseEpubForMapping() { epub.reset(); }

bool KOReaderSyncActivity::retryWithBinaryDocumentHash() {
  if (KOREADER_STORE.getMatchMethod() != DocumentMatchMethod::FILENAME || !KOReaderSyncClient::usesKosyncSubdirectory()) {
    return false;
  }

  const std::string binaryHash = KOReaderDocumentId::calculate(epubPath);
  if (binaryHash.empty() || binaryHash == documentHash) {
    return false;
  }

  documentHash = binaryHash;
  LOG_INF("KOSync", "Retrying sync with binary document hash for detected CWA server");
  return true;
}

bool KOReaderSyncActivity::computeLocalProgressAndChapter() {
  if (!ensureEpubLoadedForMapping()) {
    hasLocalProgress = false;
    localProgress = KOReaderPosition{};
    localChapterLabel.clear();
    return false;
  }

  CrossPointPosition localPos = {currentSpineIndex, currentPage, totalPagesInSpine, localParagraphIndex,
                                 hasLocalParagraphIndex};
  localProgress = ProgressMapper::toKOReader(epub, localPos);

  const int localTocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  localChapterLabel = (localTocIndex >= 0)
                          ? epub->getTocItem(localTocIndex).title
                          : (std::string(tr(STR_SECTION_PREFIX)) + std::to_string(currentSpineIndex + 1));
  hasLocalProgress = !localProgress.xpath.empty();
  return true;
}

void KOReaderSyncActivity::computeRemoteChapter() {
  if (!epub) {
    return;
  }
  const int remoteTocIndex = epub->getTocIndexForSpineIndex(remotePosition.spineIndex);
  remoteChapterLabel = (remoteTocIndex >= 0)
                           ? epub->getTocItem(remoteTocIndex).title
                           : (std::string(tr(STR_SECTION_PREFIX)) + std::to_string(remotePosition.spineIndex + 1));
}

void KOReaderSyncActivity::loop() {
  if (state == NO_CREDENTIALS || state == SYNC_FAILED || state == UPLOAD_COMPLETE || state == APPLY_COMPLETE) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      if (state == APPLY_COMPLETE) {
        resumeReader(KOReaderSyncOutcomeState::APPLIED_REMOTE);
      } else if (state == UPLOAD_COMPLETE) {
        resumeReader(KOReaderSyncOutcomeState::UPLOAD_COMPLETE);
      } else if (state == SYNC_FAILED || state == NO_CREDENTIALS) {
        resumeReader(KOReaderSyncOutcomeState::FAILED);
      } else {
        resumeReader(KOReaderSyncOutcomeState::CANCELLED);
      }
      return;
    }

    if ((state == UPLOAD_COMPLETE || state == APPLY_COMPLETE) && millis() - uploadCompleteTime >= 3000) {
      if (state == APPLY_COMPLETE) {
        resumeReader(KOReaderSyncOutcomeState::APPLIED_REMOTE);
      } else {
        resumeReader(KOReaderSyncOutcomeState::UPLOAD_COMPLETE);
      }
    }
    return;
  }

  if (state == SHOWING_RESULT) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
        mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      selectedOption = (selectedOption + 1) % 2;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
               mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      selectedOption = (selectedOption + 1) % 2;
      requestUpdate();
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (selectedOption == 0) {
        if (!ensureRemotePositionMapped()) {
          {
            RenderLock lock(*this);
            state = SYNC_FAILED;
            statusMessage = tr(STR_SYNC_FAILED_MSG);
          }
          requestUpdate(true);
          return;
        }
        const SyncResult result = {remotePosition.spineIndex,
                                   remotePosition.pageNumber,
                                   remotePosition.paragraphIndex,
                                   remotePosition.hasParagraphIndex,
                                   remotePosition.listItemIndex,
                                   remotePosition.hasListItemIndex};
        resumeReader(KOReaderSyncOutcomeState::APPLIED_REMOTE, &result);
      } else if (selectedOption == 1) {
        performUpload();
      }
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      closeCancelled();
    }
    return;
  }

  if (state == NO_REMOTE_PROGRESS) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (documentHash.empty()) {
        if (KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME) {
          documentHash = KOReaderDocumentId::calculateFromFilename(epubPath);
        } else {
          documentHash = KOReaderDocumentId::calculate(epubPath);
        }
      }
      performUpload();
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      closeCancelled();
    }
    return;
  }
}
