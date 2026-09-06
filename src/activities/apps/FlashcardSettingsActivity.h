#pragma once

#include <string>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// Flashcard study settings: two value rows (study mode, session size), each
// activated through an OptionPopup picker.
class FlashcardSettingsActivity final : public UiListActivity {
  static constexpr int SETTING_COUNT = 2;

  OptionPopup optionPopup;
  std::string rowValues[SETTING_COUNT];
  freeink::ui::ListItem rowItems[SETTING_COUNT]{};

  void refreshRowValues();

  int listCount() const override { return SETTING_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  void drawChrome() override;
  void drawFooter() override;

 public:
  explicit FlashcardSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("FlashcardSettings", renderer, mappedInput) {}

  void onEnter() override;
  void render(RenderLock&&) override;
};
