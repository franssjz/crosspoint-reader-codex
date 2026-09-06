#pragma once
#include <Epub.h>

#include <memory>
#include <string>

#include "CrossPointState.h"
#include "KOReaderSyncClient.h"
#include "ProgressMapper.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"

/**
 * Activity for syncing reading progress with KOReader sync server.
 *
 * This activity is launched as a standalone replacement screen, not as a
 * child activity of the reader. The reader persists a compact handoff record
 * (APP_STATE.koReaderSyncSession), is destroyed to reclaim memory before
 * WiFi/TLS work begins, and a fresh reader instance is reopened after sync
 * completes or is cancelled.
 *
 * Shared pipeline:
 * 1. Connect to WiFi (if not connected)
 * 2. Optionally sync NTP (if stale)
 * 3. Calculate document hash
 *
 * Intent-specific behavior:
 * - COMPARE: fetch remote progress, show full comparison screen, let user
 *   choose Apply or Upload.
 * - PULL_REMOTE: fetch and map remote progress, show success feedback, then
 *   persist an applied result for the reopened reader.
 * - PUSH_LOCAL: compute local mapping, warm session with GET, then upload via
 *   reused connection to avoid a second full TLS handshake.
 * - AUTO_PULL/AUTO_PUSH: same data path without the manual chooser, intended
 *   for the optional advanced open/close automation.
 *
 * The interactive states (compare rows, upload prompt) render through the
 * FreeInkUI app host so they get themed rows and touch targets (upstream).
 */
class KOReaderSyncActivity final : public Activity, private UiAppHost {
 public:
  explicit KOReaderSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& epubPath,
                                int currentSpineIndex, int currentPage, int totalPagesInSpine,
                                uint16_t paragraphIndex = 0, bool hasParagraphIndex = false, uint32_t xhtmlSeekHint = 0,
                                KOReaderSyncIntentState syncIntent = KOReaderSyncIntentState::COMPARE,
                                bool hasPrecomputedLocalProgress = false,
                                const SavedProgressPosition& precomputedLocalProgress = SavedProgressPosition{},
                                const std::string& precomputedLocalChapterLabel = std::string());

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == CONNECTING || state == SYNCING || state == UPLOADING; }

 private:
  enum State {
    WIFI_SELECTION,
    CONNECTING,
    SYNCING,
    SHOWING_RESULT,
    UPLOADING,
    UPLOAD_COMPLETE,
    APPLY_COMPLETE,
    NO_REMOTE_PROGRESS,
    SYNC_FAILED,
    NO_CREDENTIALS
  };

  // Position handed back to the reopened reader through the session record.
  struct AppliedPosition {
    int spineIndex = 0;
    int page = 0;
    uint16_t paragraphIndex = 0;
    bool hasParagraphIndex = false;
    uint16_t listItemIndex = 0;
    bool hasListItemIndex = false;
    bool hasVisibleTextOffset = false;
    uint32_t visibleTextOffset = 0;
  };

  std::shared_ptr<Epub> epub;
  std::string epubPath;
  int currentSpineIndex;
  int currentPage;
  int totalPagesInSpine;
  uint16_t localParagraphIndex;
  bool hasLocalParagraphIndex;
  uint32_t localXhtmlSeekHint;  // Kept for the handoff record; the upstream mapper does not need it.
  KOReaderSyncIntentState syncIntent = KOReaderSyncIntentState::COMPARE;

  State state = WIFI_SELECTION;
  std::string statusMessage;
  std::string documentHash;

  // Remote progress data
  bool hasRemoteProgress = false;
  bool remotePositionMapped = false;
  KOReaderProgress remoteProgress;
  CrossPointPosition remotePosition;

  // Local progress as KOReader format (for display / upload)
  bool hasLocalProgress = false;
  SavedProgressPosition localProgress;
  std::string remoteChapterLabel;
  std::string localChapterLabel;

  // Selection in result screen (0=Apply, 1=Upload)
  int selectedOption = 0;

  // Timestamp when completion state was entered (for auto-close)
  unsigned long uploadCompleteTime = 0;
  bool closeRequested = false;
  bool networkMemoryReleasePending = false;

  void onWifiSelectionComplete(bool success);
  void performSync();
  void performUpload();
  void prepareNetworkMemory(const char* stage);
  void restoreNetworkMemory(const char* stage);
  void closeCancelled();
  void resumeReader(KOReaderSyncOutcomeState outcome, const AppliedPosition* appliedResult = nullptr);
  void returnAfterAutoPush();
  bool ensureEpubLoadedForMapping();
  void releaseEpubForMapping();
  bool computeLocalProgressAndChapter();
  void computeRemoteChapter();
  bool ensureRemotePositionMapped(bool closeSessionBeforeMapping = true);
  bool retryWithBinaryDocumentHash();
  AppliedPosition remoteAppliedPosition() const;
  void applyRemoteFromChooser();
  void startUpload();

  // The UiAppHost app hosts the interactive states (SHOWING_RESULT compare
  // rows and the NO_REMOTE_PROGRESS upload prompt) so they get themed
  // rows/buttons and tap-flash; the header stays on GUI.drawHeader for the
  // battery indicator and the purely-informational states keep their raw
  // centered text.
  static void resultScreen(UiScreen& screen, void* user);
  static void onResultRow(const freeink::ui::ActionEvent& event, void* user);
  void buildResultScreen(UiScreen& screen);
};
