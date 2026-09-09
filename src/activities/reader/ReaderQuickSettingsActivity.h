#pragma once

#include <I18n.h>

#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class ReaderQuickSettingsActivity final : public Activity {
  enum class QuickSettingType { Toggle, Enum, Value, FontFamily };

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

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  bool saveFailed = false;

  static const std::vector<QuickSetting>& settings();
  static std::string getSettingName(int index);
  static std::string getSettingValue(int index);
  static bool isImmediateRendererSetting(const QuickSetting& setting);
  static bool needsImmediateRendererFullRefresh(const QuickSetting& setting);
  static int scopeRows() { return SETTINGS.hasBookReaderSettingsContext() ? 1 : 0; }

  void toggleSelectedSetting();
  void applyImmediateRendererSetting(const QuickSetting& setting);

 public:
  explicit ReaderQuickSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReaderQuickSettings", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
};
