#include "SdCardFontSystem.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "fontIds.h"

namespace {
struct UiFontSize {
  int fontId;
  uint8_t pointSize;
};
constexpr UiFontSize UI_FONT_SIZES[] = {{SMALL_FONT_ID, 8}, {UI_10_FONT_ID, 10}, {UI_12_FONT_ID, 12}};
}  // namespace

static uint8_t fontSizeEnumFromSettings() {
  uint8_t e = SETTINGS.fontSize;
  if (e >= CrossPointSettings::FONT_SIZE_COUNT) e = 1;  // default to MEDIUM
  return e;
}

void SdCardFontSystem::begin(GfxRenderer& renderer) {
  // Register this system as the SD font ID resolver in settings.
  // Uses a static trampoline since CrossPointSettings stores a plain function pointer.
  SETTINGS.sdFontIdResolver = [](void* ctx, const char* familyName, uint8_t fontSizeEnum) -> int {
    return static_cast<SdCardFontSystem*>(ctx)->resolveFontId(familyName, fontSizeEnum);
  };
  SETTINGS.sdFontResolverCtx = this;

  if (SETTINGS.sdFontFamilyName[0] == '\0') {
    LOG_DBG("SDFS", "SD font resolver ready; discovery deferred until requested");
    return;
  }

  ensureLoaded(renderer);
}

void SdCardFontSystem::ensureLoaded(GfxRenderer& renderer) {
  const char* wantedFamily = SETTINGS.sdFontFamilyName;
  const std::string& currentFamily = manager_.currentFamilyName();
  if (wantedFamily[0] == '\0') {
    if (!currentFamily.empty()) {
      manager_.unloadAll(renderer);
    }
    return;
  }

  // If the web server (or another task) installed/deleted fonts, re-discover.
  // Track whether we just re-discovered so we can force a reload below even
  // when the wanted family/size still maps to the same point size — the file
  // contents on disk may have changed (e.g. user re-uploaded a new build).
  bool registryWasDirty = false;
  bool registryWasReleased = false;
  const bool registryWasRefreshed = refreshRegistryIfNeeded(&registryWasDirty, &registryWasReleased);
  const uint8_t sizeEnum = fontSizeEnumFromSettings();

  // Reload if family changed OR if the user-selected size maps to a
  // different file than what's currently loaded OR if the registry was
  // just rediscovered (file may have been replaced on disk).
  bool familyMatches = (currentFamily == wantedFamily);
  if (familyMatches) {
    const auto* family = registry_.findFamily(wantedFamily);
    if (!family) {
      LOG_DBG("SDFS", "SD font family disappeared: %s (clearing)", wantedFamily);
      manager_.unloadAll(renderer);
      SETTINGS.sdFontFamilyName[0] = '\0';
      return;
    }
    auto sizes = family->availableSizes();
    const uint8_t idx = std::min<uint8_t>(sizeEnum, 3);
    const bool standardSizes = family->hasSize(12) && family->hasSize(14) && family->hasSize(16) && family->hasSize(18);
    const uint8_t readerTargets[] = {12, 14, 16, 18};
    uint8_t wantedPt = sizes.empty() ? 0 : (standardSizes ? readerTargets[idx] : sizes[std::min<size_t>(idx, sizes.size() - 1)]);
    if (!registryWasRefreshed && wantedPt == manager_.currentPointSize()) return;
    const char* reason = registryWasDirty ? " [registry dirty]" : (registryWasReleased ? " [network restore]" : "");
    LOG_DBG("SDFS", "Reloading %s: size %u -> %u (enum %u)%s", wantedFamily, manager_.currentPointSize(), wantedPt,
            sizeEnum, reason);
  }

  if (!currentFamily.empty()) {
    manager_.unloadAll(renderer);
  }

  const auto* family = registry_.findFamily(wantedFamily);
  if (family) {
    if (manager_.loadFamily(*family, renderer, sizeEnum)) {
      setupUiFallbacks(renderer);
      LOG_DBG("SDFS", "Loaded SD font family: %s", wantedFamily);
    } else {
      LOG_ERR("SDFS", "Failed to load SD font family: %s (clearing)", wantedFamily);
      SETTINGS.sdFontFamilyName[0] = '\0';
    }
  } else {
    LOG_DBG("SDFS", "SD font family not found: %s (clearing)", wantedFamily);
    SETTINGS.sdFontFamilyName[0] = '\0';
  }
}

bool SdCardFontSystem::refreshRegistryIfNeeded(bool* wasDirty, bool* wasReleased) {
  const bool dirty = registryDirty_.exchange(false, std::memory_order_acquire);
  const bool released = registryReleasedForNetwork_.exchange(false, std::memory_order_acquire);
  const bool loaded = registryLoaded_.load(std::memory_order_acquire);
  if (wasDirty) *wasDirty = dirty;
  if (wasReleased) *wasReleased = released;
  if (loaded && !dirty && !released) return false;

  LOG_DBG("SDFS", "Discovering SD fonts%s", released ? " after network release" : "");
  registry_.discover();
  registryLoaded_.store(true, std::memory_order_release);
  return true;
}

void SdCardFontSystem::refreshIfDirty() { refreshRegistryIfNeeded(); }

void SdCardFontSystem::setupUiFallbacks(GfxRenderer& renderer) {
  const std::string& familyName = manager_.currentFamilyName();
  const auto* family = registry_.findFamily(familyName);
  const auto reader = renderer.getFontMap().find(manager_.getFontId(familyName));
  if (!family || reader == renderer.getFontMap().end()) return;

  static constexpr uint32_t CJK_PROBES[] = {0x4E00, 0x3042, 0x30A2, 0xAC00};
  bool hasCjk = false;
  for (const uint32_t cp : CJK_PROBES) hasCjk = hasCjk || reader->second.hasCodepoint(cp);
  if (!hasCjk) return;

  for (const auto& ui : UI_FONT_SIZES) {
    const int fallbackId = manager_.loadFamilyExtraSize(*family, renderer, ui.pointSize);
    if (fallbackId) renderer.setFallbackFont(ui.fontId, fallbackId);
  }
}

bool SdCardFontSystem::releaseForNetwork(GfxRenderer& renderer) {
  const bool hadLoadedFont = manager_.hasLoadedFont();
  if (hadLoadedFont) {
    LOG_DBG("SDFS", "Unloading SD font family before network: %s", manager_.currentFamilyName().c_str());
    manager_.unloadAll(renderer);
  }

  registry_.releaseMemory();
  registryLoaded_.store(false, std::memory_order_release);
  registryReleasedForNetwork_.store(true, std::memory_order_release);
  return hadLoadedFont;
}

int SdCardFontSystem::resolveFontId(const char* familyName, uint8_t /*fontSizeEnum*/) const {
  // The manager loads exactly one size (closest to SETTINGS.fontSize), so the
  // enum is implicit — always return the single loaded font ID for this family.
  // ensureLoaded() must have been called with the current settings before this.
  return manager_.getFontId(familyName);
}
