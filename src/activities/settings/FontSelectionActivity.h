#pragma once

#include <SdCardFontRegistry.h>

#include <string>
#include <vector>

#include "activities/UiTabListActivity.h"

// Reader font picker: a Family tab (built-in + SD card families) and a Size
// tab (the point sizes the active family ships). Activating a row applies it
// to SETTINGS and finishes; callers reload the reader font on return.
class FontSelectionActivity final : public UiTabListActivity {
 public:
  explicit FontSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 const SdCardFontRegistry* registry);

  void onEnter() override;

 private:
  enum Tab : int { TAB_FAMILY = 0, TAB_SIZE = 1, TAB_COUNT = 2 };

  struct FontEntry {
    std::string name;
    bool isBuiltin;
    uint8_t settingIndex;  // index used by valueSetter
  };

  // --- UiTabListActivity contract ---
  int listCount() const override;
  int tabCount() const override { return TAB_COUNT; }
  int activeTab() const override { return tab_; }
  const char* tabLabel(int index) const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onTabAction(int index) override;
  void stepTab(int direction) override;
  bool handleButtons() override;
  const char* headerTitle() const override;

  void applyFamily(int index);
  void applySize(int index);
  int currentFamilyIndex() const;
  int currentSizeIndex() const;

  const SdCardFontRegistry* registry_;
  int tab_ = TAB_FAMILY;
  std::vector<FontEntry> fonts_;
  std::vector<uint8_t> sizes_;
  // Row caches, built once in onEnter() (both tabs finish on activation, so
  // the "Selected" markers cannot go stale within one visit).
  std::vector<std::string> sizeLabels_;
  std::vector<freeink::ui::ListItem> familyRows_;
  std::vector<freeink::ui::ListItem> sizeRows_;
};
