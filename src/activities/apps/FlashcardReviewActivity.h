#pragma once

#include <FreeInkUICore.h>

#include <string>
#include <vector>

#include "../Activity.h"
#include "FlashcardsStore.h"
#include "components/UiAppHost.h"

// Landscape flashcard session. The card stays hand-drawn; the FreeInkApp adds
// touch: tap the card = flip, a Fail / Flip / Success button row along the
// card's bottom edge (touch boards only), swipe up/down = scroll a long card,
// tap anywhere on the error / no-cards pages = leave. Physical buttons keep
// their legacy roles (Confirm flip, Left success, Right fail, Up/Down scroll).
class FlashcardReviewActivity final : public Activity, private UiAppHost {
  std::string deckPath;
  FlashcardDeck deck;
  std::vector<FlashcardCardProgress> progress;
  std::vector<int> queue;
  FlashcardCard activeCard;
  size_t queueIndex = 0;
  int activeCardIndex = -1;
  int initialSessionSize = 0;
  bool showBack = false;
  bool loaded = false;
  bool activeCardLoaded = false;
  bool cardLayoutValid = false;
  std::vector<std::string> wrappedLines;
  int wrappedFontId = 0;
  int wrappedLineHeight = 0;
  int visibleLineCount = 1;
  int scrollLineOffset = 0;
  int maxScrollLineOffset = 0;
  std::string errorMessage;
  GfxRenderer::Orientation originalOrientation = GfxRenderer::Orientation::Portrait;
  bool orientationApplied = false;

  int sessionReviewed = 0;
  int sessionCorrect = 0;
  int sessionFailed = 0;
  int sessionSkipped = 0;
  int sessionNewSeen = 0;
  std::vector<std::string> newlySeenKeys;

  // Touch targets measured by render() and registered by buildReviewScreen()
  // on the same pass. dismissOnTap marks the error / no-cards pages, where a
  // tap anywhere leaves. actionButton_ is a member: fui::ButtonProps embeds a
  // StyleSet, past the stack budget for a render-path local.
  freeink::ui::Rect hitCardRect{};
  freeink::ui::Rect hitFailRect{};
  freeink::ui::Rect hitFlipRect{};
  freeink::ui::Rect hitSuccessRect{};
  bool touchActionsVisible = false;
  bool dismissOnTap = false;
  freeink::ui::ButtonProps actionButton_;

  static void reviewScreen(UiScreen& screen, void* user);
  static void onDismissEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onFlipEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onFailEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onSuccessEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildReviewScreen(UiScreen& screen);
  bool hasActiveCard() const { return loaded && queueIndex < queue.size(); }
  void flipCard();
  void leave();
  // Error / no-cards page: message + the shared dismiss target.
  void renderDismissPage(const char* title, const char* subtitle, const char* message);

  void loadDeckData();
  void finishWithSummary();
  bool isCurrentCardUnseen() const;
  bool ensureCurrentCardLoaded();
  FlashcardCardProgress& currentProgress();
  const FlashcardCard& currentCard() const;
  void invalidateCardLayout();
  void scrollCard(int direction);
  void goToNextCard();
  void markCurrentSuccess();
  void markCurrentFailure();

 public:
  FlashcardReviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string deckPath)
      : Activity("FlashcardReview", renderer, mappedInput), UiAppHost(renderer), deckPath(std::move(deckPath)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
