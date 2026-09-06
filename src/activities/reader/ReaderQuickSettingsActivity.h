#pragma once

#include <I18n.h>

#include <cstddef>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/UiListActivity.h"

// Fork: the reader's quick-settings list. Every row shows its live value;
// tapping a row (or Confirm on the selection) cycles it, exactly as before.
class ReaderQuickSettingsActivity final : public UiListActivity {
  enum class QuickSettingType { Toggle, Enum, Value, FontFamily, FontSize };

  struct QuickSetting {
    StrId nameId;
    QuickSettingType type;
    uint8_t CrossPointSettings::* valuePtr = nullptr;
    std::vector<StrId> enumValues;

    struct ValueRange {
      uint8_t min;
      uint8_t max;
      uint8_t step;
    };
    ValueRange valueRange = {};
  };

  // Row storage: settings() is a fixed table well under MAX_QUICK_SETTINGS,
  // so a fixed-capacity array avoids any heap allocation for the row list.
  // Labels are set once in onEnter() (buildRowItems()); buildScreen() only
  // refreshes the value pointers, which alias I18N strings, SETTINGS' font
  // family name, or the small per-row scratch buffers below.
  static constexpr size_t MAX_QUICK_SETTINGS = 24;
  static constexpr size_t VALUE_SCRATCH_LEN = 12;  // "255 pt" and friends
  freeink::ui::ListItem rowItems[MAX_QUICK_SETTINGS]{};
  char valueScratch[MAX_QUICK_SETTINGS][VALUE_SCRATCH_LEN]{};
  void buildRowItems();
  void refreshRowValues();

  static const std::vector<QuickSetting>& settings();
  static size_t settingCount();
  // Live value text for row `index`, written into scratch when it is not a
  // stable string (font size, numeric value).
  const char* settingValueText(size_t index, char* scratch, size_t scratchLen) const;
  static bool isImmediateRendererSetting(const QuickSetting& setting);
  static bool needsImmediateRendererFullRefresh(const QuickSetting& setting);

  void toggleSetting(int index);
  void applyImmediateRendererSetting(const QuickSetting& setting);

  int listCount() const override { return static_cast<int>(settingCount()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // Header via HeaderDateUtils (title + date line), footer says "Toggle".
  void drawChrome() override;
  void drawFooter() override;

 public:
  explicit ReaderQuickSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("ReaderQuickSettings", renderer, mappedInput) {}

  void onEnter() override;
  bool isReaderActivity() const override { return true; }
};
