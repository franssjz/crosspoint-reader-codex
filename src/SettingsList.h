#pragma once

#include <I18n.h>
#include <SdCardFontRegistry.h>

#include <vector>

#include "activities/settings/SettingsActivity.h"

// Shared settings list used by the web settings API and settings persistence.
// Each entry has a key (for JSON API) and category (for grouping).
// ACTION-type entries and entries without a key are device-only.
//
// The static base list is constructed exactly once; every call then copies it.
// When an SdCardFontRegistry is supplied AND has SD card fonts installed, the
// font-family entry is replaced in that copy with a registry-aware version. The
// font-size entry is always rebuilt, since its options are the point sizes of
// the active family rather than a fixed enum. Board-capability filters
// (touch / home key) are applied to the copy as well, matching upstream.
std::vector<SettingInfo> getSettingsList(const SdCardFontRegistry* registry = nullptr);

// Build the font family setting dynamically. When registry is non-null, SD card fonts
// are appended after the built-in fonts. Otherwise only built-in fonts are listed.
SettingInfo buildFontFamilySetting(const SdCardFontRegistry* registry);

// Build the font size setting dynamically from the point sizes the active family
// ships. The selected point size persists in SETTINGS.fontPointSize while the
// ENUM contract shared with the web UI stays index-based.
SettingInfo buildFontSizeSetting(const SdCardFontRegistry* registry);

// Labels for CrossPointSettings::LONG_PRESS_MENU_FUNCTION, in enum order. The
// Reader Menu option is only offered on boards with a Home key.
std::vector<StrId> buildLongPressMenuValues();
