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
#include "CrossPointState.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderDocumentId.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "ReadingStatsStore.h"
#include "activities/apps/ReadingStatsDetailActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/icons/listIcons.h"  // download/upload icons for the compare rows
#include "fontIds.h"
#include "util/AchievementPopupUtils.h"
#include "util/CompletedBookMover.h"
#include "util/NetworkMemory.h"
#include "util/TimeUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr time_t NTP_RESYNC_MIN_INTERVAL_SEC = 15 * 60;

// One action id for both interactive states: the compare rows (SHOWING_RESULT)
// and the upload button (NO_REMOTE_PROGRESS) never coexist, so state
// disambiguates them in the handler.
constexpr fui::ActionId ACTION_ROW = 1;

std::string calculateDocumentHashForMethod(const std::string& path, const DocumentMatchMethod method) {
  return method == DocumentMatchMethod::FILENAME ? KOReaderDocumentId::calculateFromFilename(path)
                                                 : KOReaderDocumentId::calculate(path);
}

DocumentMatchMethod alternateMatchMethod(const DocumentMatchMethod method) {
  return method == DocumentMatchMethod::FILENAME ? DocumentMatchMethod::BINARY : DocumentMatchMethod::FILENAME;
}

const char* matchMethodName(const DocumentMatchMethod method) {
  return method == DocumentMatchMethod::FILENAME ? "filename" : "binary";
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

std::string appendFailureDetail(std::string message) {
  const char* detail = KOReaderSyncClient::lastFailureDetail();
  if (detail && detail[0]) {
    message += " \xe2\x80\x94 ";
    message += detail;
  }
  return message;
}
}  // namespace

KOReaderSyncActivity::KOReaderSyncActivity(
    GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& epubPath, const int currentSpineIndex,
    const int currentPage, const int totalPagesInSpine, const uint16_t paragraphIndex, const bool hasParagraphIndex,
    const uint32_t xhtmlSeekHint, const KOReaderSyncIntentState syncIntent, const bool hasPrecomputedLocalProgress,
    const SavedProgressPosition& precomputedLocalProgress, const std::string& precomputedLocalChapterLabel)
    : Activity("KOReaderSync", renderer, mappedInput),
      UiAppHost(renderer),
      epubPath(epubPath),
      currentSpineIndex(currentSpineIndex),
      currentPage(currentPage),
      totalPagesInSpine(totalPagesInSpine),
      localParagraphIndex(paragraphIndex),
      hasLocalParagraphIndex(hasParagraphIndex),
      localXhtmlSeekHint(xhtmlSeekHint),
      syncIntent(syncIntent),
      remoteProgress{},
      remotePosition{},
      hasLocalProgress(hasPrecomputedLocalProgress && !precomputedLocalProgress.xpath.empty()),
      localProgress(hasLocalProgress ? precomputedLocalProgress : SavedProgressPosition{}),
      localChapterLabel(hasLocalProgress ? precomputedLocalChapterLabel : std::string()) {}

void KOReaderSyncActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    LOG_DBG("KOSync", "WiFi connection failed, resuming reader");
    resumeReader(KOReaderSyncOutcomeState::CANCELLED);
    return;
  }

  LOG_DBG("KOSync", "WiFi connected, starting sync");

  // Keep the station fully awake for the short sync transaction (upstream):
  // ESP32 modem sleep can introduce multi-second network stalls that surface
  // as HTTP timeouts. WiFi is torn down when this activity exits.
  WiFi.setSleep(false);

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
  const bool smartSync =
      KOREADER_STORE.getSyncBehavior() == KOReaderSyncBehavior::SMART && syncIntent == KOReaderSyncIntentState::COMPARE;
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
  const std::string primaryHash = documentHash;

  LOG_DBG("KOSync", "Document hash (%s): %s", matchMethodName(primaryMethod), documentHash.c_str());

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
        statusMessage = appendFailureDetail(KOReaderSyncClient::errorString(warmupResult));
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
  LOG_DBG("KOSync", "Primary remote (%s): result=%d http=%d local=%.6f remote=%.6f", matchMethodName(primaryMethod),
          result, KOReaderSyncClient::lastHttpCode, localProgress.percentage, remoteProgress.percentage);

  if (smartSync) {
    // In smart mode also probe the alternate document-id method and use the
    // furthest remote state we can find, so a stale local upload never
    // overwrites progress another device synced under the other id.
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
      documentHash = primaryHash;
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
      statusMessage = appendFailureDetail(KOReaderSyncClient::errorString(result));
    }
    requestUpdate(true);
    return;
  }

  hasRemoteProgress = false;
  remotePositionMapped = false;
  remotePosition = CrossPointPosition{-1, -1, 0};
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

    const AppliedPosition applied = remoteAppliedPosition();
    if (syncIntent == KOReaderSyncIntentState::AUTO_PULL) {
      wifiOff();
      resumeReader(KOReaderSyncOutcomeState::APPLIED_REMOTE, &applied);
      return;
    }
    // Manual pull: record the result now so a Back press (or the auto-close)
    // reopens the reader on the remote position.
    auto& sync = APP_STATE.koReaderSyncSession;
    sync.outcome = KOReaderSyncOutcomeState::APPLIED_REMOTE;
    sync.resultSpineIndex = applied.spineIndex;
    sync.resultPage = applied.page;
    sync.resultParagraphIndex = applied.paragraphIndex;
    sync.resultHasParagraphIndex = applied.hasParagraphIndex;
    sync.resultLiIndex = applied.listItemIndex;
    sync.resultHasLiIndex = applied.hasListItemIndex;
    sync.resultHasVisibleTextOffset = applied.hasVisibleTextOffset;
    sync.resultVisibleTextOffset = applied.visibleTextOffset;
    APP_STATE.saveToFile();
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
    LOG_DBG("KOSync", "Smart decision: local=%.6f remote=%.6f delta=%.6f mapped=%d/%d", localProgress.percentage,
            remoteProgress.percentage, delta, remotePosition.spineIndex, remotePosition.pageNumber);
    if (std::fabs(delta) <= SAME_PROGRESS_EPSILON) {
      wifiOff();
      resumeReader(KOReaderSyncOutcomeState::UPLOAD_COMPLETE);
      return;
    }
    if (delta > 0.0f) {
      // Alternate hashes are only probes for newer remote state. Keep uploads
      // on the user's configured matching method so its primary record heals.
      documentHash = primaryHash;
      performUpload();
      return;
    }
    const AppliedPosition applied = remoteAppliedPosition();
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

  // Rich CrossPoint position for the default CrossPoint sync server (lossless
  // CrossPoint<->CrossPoint sync, upstream). The HTTP client also enforces this
  // boundary before serializing the extension.
  if (KOREADER_STORE.usesCrossPointSyncServer()) {
    KOReaderRichPosition pos;
    const float pct = localProgress.percentage < 0.0f   ? 0.0f
                      : localProgress.percentage > 1.0f ? 1.0f
                                                        : localProgress.percentage;
    pos.pctQ = static_cast<uint32_t>(pct * 1000000.0f + 0.5f);
    pos.spineIndex = static_cast<uint16_t>(currentSpineIndex);
    pos.pageNumber = static_cast<uint16_t>(currentPage);
    pos.totalPages = static_cast<uint16_t>(totalPagesInSpine > 0 ? totalPagesInSpine : 1);
    if (hasLocalParagraphIndex) pos.paragraphIndex = localParagraphIndex;
    pos.xpath = localProgress.xpath;
    progress.position = std::move(pos);
  }

  if (KOREADER_STORE.getSendMetadata()) {
    KOReaderMetadata metadata;
    const auto lastSlash = epubPath.rfind('/');
    metadata.filename = lastSlash == std::string::npos ? epubPath : epubPath.substr(lastSlash + 1);
    if (ensureEpubLoadedForMapping()) {
      metadata.title = epub->getTitle();
      metadata.authors = epub->getAuthor();
      releaseEpubForMapping();
    } else {
      LOG_ERR("KOSync", "Epub unavailable for metadata; sending filename only");
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
      statusMessage = appendFailureDetail(KOReaderSyncClient::errorString(result));
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

  // Match the reader's rotation so the sync screens do not flip the panel around.
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

  resetUi();
  app.on(ACTION_ROW, &KOReaderSyncActivity::onResultRow, this);
  app.setScreen(&KOReaderSyncActivity::resultScreen, this);

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
  startActivityForResult(
      std::make_unique<WifiSelectionActivity>(renderer, mappedInput, !chooseWifiManually, true, automaticSync),
      [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void KOReaderSyncActivity::onExit() {
  Activity::onExit();

  logSyncMemSnapshot("onExit_before_cleanup");
  KOReaderSyncClient::endPersistentSession();
  restoreNetworkMemory("onExit_restore");
  wifiOff();
  releaseEpubForMapping();
  // The reopened reader re-applies its own orientation; everything else is portrait.
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  logSyncMemSnapshot("onExit_after_cleanup");
}

void KOReaderSyncActivity::closeCancelled() {
  if (closeRequested) {
    return;
  }
  resumeReader(KOReaderSyncOutcomeState::CANCELLED);
}

void KOReaderSyncActivity::resumeReader(const KOReaderSyncOutcomeState outcome, const AppliedPosition* appliedResult) {
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
    sync.resultLiIndex = appliedResult->listItemIndex;
    sync.resultHasLiIndex = appliedResult->hasListItemIndex;
    sync.resultHasVisibleTextOffset = appliedResult->hasVisibleTextOffset;
    sync.resultVisibleTextOffset = appliedResult->visibleTextOffset;
  } else if (outcome != KOReaderSyncOutcomeState::APPLIED_REMOTE) {
    sync.resultSpineIndex = 0;
    sync.resultPage = 0;
    sync.resultParagraphIndex = 0;
    sync.resultHasParagraphIndex = false;
    sync.resultLiIndex = 0;
    sync.resultHasLiIndex = false;
    sync.resultHasVisibleTextOffset = false;
    sync.resultVisibleTextOffset = 0;
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
    activityManager.replaceActivity(std::make_unique<ReadingStatsDetailActivity>(renderer, mappedInput, finalBookPath,
                                                                                 ReadingStatsDetailContext{true}));
  } else {
    activityManager.goHome();
  }
}

KOReaderSyncActivity::AppliedPosition KOReaderSyncActivity::remoteAppliedPosition() const {
  AppliedPosition applied;
  applied.spineIndex = remotePosition.spineIndex;
  applied.page = remotePosition.pageNumber;
  applied.paragraphIndex = remotePosition.paragraphIndex;
  applied.hasParagraphIndex = remotePosition.hasParagraphIndex;
  applied.listItemIndex = remotePosition.liIndex;
  applied.hasListItemIndex = remotePosition.hasLiIndex;
  applied.hasVisibleTextOffset = remotePosition.hasVisibleTextOffset;
  applied.visibleTextOffset = remotePosition.visibleTextOffset;
  return applied;
}

void KOReaderSyncActivity::applyRemoteFromChooser() {
  if (!ensureRemotePositionMapped()) {
    {
      RenderLock lock(*this);
      state = SYNC_FAILED;
      statusMessage = tr(STR_SYNC_FAILED_MSG);
    }
    requestUpdate(true);
    return;
  }
  const AppliedPosition applied = remoteAppliedPosition();
  resumeReader(KOReaderSyncOutcomeState::APPLIED_REMOTE, &applied);
}

void KOReaderSyncActivity::startUpload() {
  if (documentHash.empty()) {
    documentHash = calculateDocumentHashForMethod(epubPath, KOREADER_STORE.getMatchMethod());
  }
  performUpload();
}

void KOReaderSyncActivity::onResultRow(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<KOReaderSyncActivity*>(user);
  // Activation leaves this screen (applies/uploads); drop the flash so it does
  // not ghost onto the next paint.
  self->app.clearTapFlash();
  if (self->state == SHOWING_RESULT) {
    if (event.value < 0 || event.value > 1) return;
    self->selectedOption = event.value;
    if (self->selectedOption == 0) {
      self->applyRemoteFromChooser();
    } else {
      self->performUpload();
    }
  } else if (self->state == NO_REMOTE_PROGRESS) {
    self->startUpload();
  }
}

void KOReaderSyncActivity::resultScreen(UiScreen& screen, void* user) {
  static_cast<KOReaderSyncActivity*>(user)->buildResultScreen(screen);
}

void KOReaderSyncActivity::buildResultScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Side padding is 0 here (like the other FreeInkApp screens): the action list
  // supplies its own theme side padding, and the raw comparison text is indented
  // to line up with the list rows below (see labelIndent).
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (state == SHOWING_RESULT) {
    const std::string remoteChapter =
        !remoteChapterLabel.empty()
            ? remoteChapterLabel
            : (std::string(tr(STR_SECTION_PREFIX)) + std::to_string(remotePosition.spineIndex + 1));
    const std::string localChapter =
        !localChapterLabel.empty() ? localChapterLabel
                                   : (std::string(tr(STR_SECTION_PREFIX)) + std::to_string(currentSpineIndex + 1));

    char remoteVal[64];
    snprintf(remoteVal, sizeof(remoteVal), tr(STR_PAGE_OVERALL_FORMAT), remotePosition.pageNumber + 1,
             remoteProgress.percentage * 100);
    char localVal[64];
    snprintf(localVal, sizeof(localVal), tr(STR_PAGE_TOTAL_OVERALL_FORMAT), currentPage + 1, totalPagesInSpine,
             localProgress.percentage * 100);
    char deviceStr[80];
    deviceStr[0] = '\0';
    if (!remoteProgress.device.empty()) {
      snprintf(deviceStr, sizeof(deviceStr), tr(STR_DEVICE_FROM_FORMAT), remoteProgress.device.c_str());
    }

    // Labeled, multi-line comparison flowing from the top. Indent everything to
    // the list rows' content-left (the row inset + side padding the list adds
    // below) so the "Remote"/"Local" labels sit directly above the row icons.
    auto labelStyle = screen.theme().bodyText;
    labelStyle.bold = true;
    auto detailStyle = screen.theme().smallText;
    const int16_t labelH = screen.target().lineHeight(labelStyle.font);
    const int16_t detailH = screen.target().lineHeight(detailStyle.font);
    const int16_t labelIndent = static_cast<int16_t>(screen.theme().listInset + screen.theme().listSidePadding);
    const int16_t detailIndent = static_cast<int16_t>(labelIndent + screen.theme().spaceMd);
    const auto textLine = [&](const char* text, const fui::TextStyle& style, int16_t height, int16_t indent,
                              int16_t gap) {
      fui::Rect r = screen.takeTop(height, gap);
      r.x = static_cast<int16_t>(r.x + indent);
      r.width = static_cast<int16_t>(r.width - indent);
      screen.target().text(r, text, style);
    };
    const auto labelLine = [&](const char* text) {
      textLine(text, labelStyle, labelH, labelIndent, screen.theme().spaceSm);
    };
    const auto detailLine = [&](const char* text) {
      textLine(text, detailStyle, detailH, detailIndent, screen.theme().spaceXs);
    };

    labelLine(tr(STR_REMOTE_LABEL));
    detailLine(remoteChapter.c_str());
    detailLine(remoteVal);
    if (deviceStr[0] != '\0') detailLine(deviceStr);
    screen.spacer(screen.theme().spaceLg);
    labelLine(tr(STR_LOCAL_LABEL));
    detailLine(localChapter.c_str());
    detailLine(localVal);

    // Two themed action rows flowing directly below the labels. Apply Remote
    // pulls (download), Upload Local pushes (upload); the selected row
    // highlights for physical-button users and tap works either way.
    screen.spacer(screen.theme().spaceMd);
    fui::ListItem actions[2];
    actions[0].label = tr(STR_APPLY_REMOTE);
    actions[0].icon = fui::bitmapFromIcon(icon_download_24);
    actions[0].actionValue = 0;
    actions[1].label = tr(STR_UPLOAD_LOCAL);
    actions[1].icon = fui::bitmapFromIcon(icon_upload_24);
    actions[1].actionValue = 1;
    fui::ListProps actionProps;
    actionProps.items = actions;
    actionProps.count = 2;
    actionProps.selectedIndex = static_cast<int16_t>(selectedOption);
    actionProps.action = ACTION_ROW;
    actionProps.inputMask = fui::InputTouch;  // physical buttons stay in loop()
    actionProps.scrollIndicator = false;      // never scrolls; no indicator needed
    // Non-touch hardware (X3/X4) keeps the original, denser row height instead
    // of FreeInkUI's touch-target-sized default; actionsBand must use the same
    // value or the band and the rows it contains fall out of sync.
    int16_t actionRowHeight = screen.theme().rowHeight;
    if (!mappedInput.hasTouch()) {
      actionRowHeight = static_cast<int16_t>(UITheme::getInstance().getMetrics().listRowHeight);
      actionProps.rowHeight = actionRowHeight;
    }
    const auto actionsBand =
        static_cast<int16_t>(actionRowHeight * 2 + screen.theme().listRowGap + screen.theme().spaceSm);
    screen.list(actionProps, actionsBand);
    return;
  }

  if (state == NO_REMOTE_PROGRESS) {
    auto centered = screen.theme().bodyText;
    centered.align = fui::TextAlign::Center;
    auto centeredBold = centered;
    centeredBold.bold = true;
    const int16_t lineH = screen.target().lineHeight(centered.font);
    screen.target().text(screen.takeTop(lineH, screen.theme().spaceSm), tr(STR_NO_REMOTE_MSG), centeredBold);
    screen.target().text(screen.takeTop(lineH, screen.theme().spaceMd), tr(STR_UPLOAD_PROMPT), centered);

    // Single themed action row anchored to the bottom, matching the lists used
    // everywhere else (inherits the theme's row radius + selection style).
    fui::ListItem action;
    action.label = tr(STR_UPLOAD_LOCAL);
    action.actionValue = 0;
    fui::ListProps actionProps;
    actionProps.items = &action;
    actionProps.count = 1;
    actionProps.selectedIndex = 0;
    actionProps.action = ACTION_ROW;
    actionProps.inputMask = fui::InputTouch;
    actionProps.scrollIndicator = false;
    int16_t actionRowHeight = screen.theme().rowHeight;
    if (!mappedInput.hasTouch()) {
      actionRowHeight = static_cast<int16_t>(UITheme::getInstance().getMetrics().listRowHeight);
      actionProps.rowHeight = actionRowHeight;
    }
    const auto actionsBand = static_cast<int16_t>(actionRowHeight + screen.theme().spaceMd);
    screen.list(actionProps, actionsBand, fui::LayoutAnchor::Bottom);
  }
}

void KOReaderSyncActivity::render(RenderLock&&) {
  renderer.clearScreen();

  auto metrics = UITheme::getInstance().getMetrics();
  Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 state == SHOWING_RESULT ? tr(STR_PROGRESS_FOUND) : tr(STR_KOREADER_SYNC));

  const int top = screen.y + screen.height / 2 - 40;
  if (state == NO_CREDENTIALS) {
    UITheme::drawCenteredText(renderer, screen, UI_10_FONT_ID, top, tr(STR_NO_CREDENTIALS_MSG), true,
                              EpdFontFamily::BOLD);
    UITheme::drawCenteredText(renderer, screen, UI_10_FONT_ID, top + 40, tr(STR_KOREADER_SETUP_HINT));

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
    // Comparison rows + option selection render through the FreeInkApp
    // (themed rows, tap-flash); the header above shows "Progress Found".
    renderUi();

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == NO_REMOTE_PROGRESS) {
    // Prompt text + upload button render through the FreeInkApp.
    renderUi();

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_UPLOAD), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == UPLOAD_COMPLETE || state == APPLY_COMPLETE) {
    UITheme::drawCenteredText(renderer, screen, UI_10_FONT_ID, top,
                              state == UPLOAD_COMPLETE ? tr(STR_UPLOAD_SUCCESS) : tr(STR_PULL_SUCCESS), true,
                              EpdFontFamily::BOLD);

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DONE), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == SYNC_FAILED) {
    UITheme::drawCenteredText(renderer, screen, UI_10_FONT_ID, top, tr(STR_SYNC_FAILED_MSG), true, EpdFontFamily::BOLD);

    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const auto lines = renderer.wrappedText(UI_10_FONT_ID, statusMessage.c_str(), screen.width - 40, 4);
    int y = top + 40;
    for (const auto& line : lines) {
      UITheme::drawCenteredText(renderer, screen, UI_10_FONT_ID, y, line.c_str());
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

  const SavedProgressPosition koPos = {remoteProgress.progress, remoteProgress.percentage};
  if (!ensureEpubLoadedForMapping()) {
    return false;
  }
  // The standard KOReader progress XPath is the authoritative content anchor.
  // The CrossPoint server's rich page hints remain a fallback (upstream).
  remotePosition = ProgressMapper::toCrossPoint(epub, koPos, renderer, currentSpineIndex, totalPagesInSpine);
  if (!remotePosition.hasVisibleTextOffset && remoteProgress.position.has_value()) {
    // toCrossPoint above already tried koPos.xpath; if the rich position carries the same XPath,
    // tell fromRichPosition to skip re-resolving it and use its page hints directly.
    const bool sameXPath = remoteProgress.position->xpath == remoteProgress.progress;
    if (const auto richMapped = ProgressMapper::fromRichPosition(epub, *remoteProgress.position, renderer, sameXPath)) {
      remotePosition = *richMapped;
    }
  }
  computeRemoteChapter();
  releaseEpubForMapping();
  hasRemoteProgress = true;
  remotePositionMapped = true;
  return true;
}

void KOReaderSyncActivity::releaseEpubForMapping() { epub.reset(); }

bool KOReaderSyncActivity::retryWithBinaryDocumentHash() {
  if (KOREADER_STORE.getMatchMethod() != DocumentMatchMethod::FILENAME ||
      !KOReaderSyncClient::usesKosyncSubdirectory()) {
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
    localProgress = SavedProgressPosition{};
    localChapterLabel.clear();
    return false;
  }

  CrossPointPosition localPos{currentSpineIndex, currentPage, totalPagesInSpine};
  localPos.paragraphIndex = localParagraphIndex;
  localPos.hasParagraphIndex = hasLocalParagraphIndex;
  localProgress = ProgressMapper::toSavedProgress(epub, localPos);

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
    const bool dismissed = mappedInput.wasReleased(MappedInputManager::Button::Back) ||
                           mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasBackGesture();
    int tx = 0;
    int ty = 0;
    const bool tapped = mappedInput.wasScreenTapped(tx, ty);
    if (dismissed || tapped) {
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
    // Touch goes through the FreeInkApp: render() registered the compare rows;
    // route the snapshot and let onResultRow apply/upload on tap.
    const auto route = routeTouch(mappedInput);
    if (route.routed && app.invalidated()) requestUpdate();
    if (route) return;  // dispatched to onResultRow

    if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
        mappedInput.wasReleased(MappedInputManager::Button::Left) ||
        mappedInput.wasReleased(MappedInputManager::Button::Down) ||
        mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      selectedOption = (selectedOption + 1) % 2;  // Wrap around among 2 options
      requestUpdate();
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (selectedOption == 0) {
        applyRemoteFromChooser();
      } else {
        performUpload();
      }
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasBackGesture()) {
      closeCancelled();
    }
    return;
  }

  if (state == NO_REMOTE_PROGRESS) {
    // Touch goes through the FreeInkApp: render() registered the upload button.
    const auto route = routeTouch(mappedInput);
    if (route.routed && app.invalidated()) requestUpdate();
    if (route) return;  // dispatched to onResultRow -> startUpload

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      startUpload();
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasBackGesture()) {
      closeCancelled();
    }
    return;
  }
}
