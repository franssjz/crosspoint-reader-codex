#pragma once

#include <cstdint>
#include <cstring>

// Only these reader choices may be overridden by a book. Identity, credentials,
// button mappings, statistics and all other application settings stay global.
// The limits describe CPR values, never the upstream fork's enum numbers.
#define CPR_READER_PREFERENCE_FIELDS(X) \
  X(darkMode, 1)                        \
  X(fadingFix, 1)                       \
  X(refreshFrequency, 4)                \
  X(fontFamily, 1)                      \
  X(fontSize, 4)                        \
  X(lineSpacing, 3)                     \
  X(wordSpacing, 4)                     \
  X(screenMargin, 40)                   \
  X(paragraphAlignment, 4)              \
  X(embeddedStyle, 1)                   \
  X(hyphenationEnabled, 1)              \
  X(bionicReading, 2)                   \
  X(orientation, 3)                     \
  X(extraParagraphSpacing, 1)           \
  X(forceParagraphIndents, 1)           \
  X(textAntiAliasing, 1)                \
  X(textDarkness, 3)                    \
  X(readerRefreshMode, 3)               \
  X(imageRendering, 2)                  \
  X(readerAutoPageTurn, 4)

struct ReaderPreferences {
#define CPR_DECLARE_READER_FIELD(name, maximum) uint8_t name = 0;
  CPR_READER_PREFERENCE_FIELDS(CPR_DECLARE_READER_FIELD)
#undef CPR_DECLARE_READER_FIELD
  char sdFontFamilyName[32] = "";

  template <class Settings>
  static ReaderPreferences capture(const Settings& settings) {
    ReaderPreferences result;
#define CPR_CAPTURE_READER_FIELD(name, maximum) result.name = settings.name;
    CPR_READER_PREFERENCE_FIELDS(CPR_CAPTURE_READER_FIELD)
#undef CPR_CAPTURE_READER_FIELD
    std::memcpy(result.sdFontFamilyName, settings.sdFontFamilyName, sizeof(result.sdFontFamilyName));
    result.sdFontFamilyName[sizeof(result.sdFontFamilyName) - 1] = '\0';
    return result;
  }

  template <class Settings>
  void apply(Settings& settings) const {
#define CPR_APPLY_READER_FIELD(name, maximum) settings.name = name;
    CPR_READER_PREFERENCE_FIELDS(CPR_APPLY_READER_FIELD)
#undef CPR_APPLY_READER_FIELD
    std::memcpy(settings.sdFontFamilyName, sdFontFamilyName, sizeof(sdFontFamilyName));
  }
};

// Pure scope management, shared by firmware and host compatibility tests. The
// serializer asks for persisted() instead of temporarily mutating the singleton,
// so concurrent rendering never sees a transient global font or orientation.
class ReaderPreferenceScope {
  ReaderPreferences global;
  bool active = false;

 public:
  bool isActive() const { return active; }
  template <class Settings>
  void begin(Settings& settings, const ReaderPreferences& overrides) {
    if (!active) global = ReaderPreferences::capture(settings);
    active = true;
    overrides.apply(settings);
  }
  template <class Settings>
  void end(Settings& settings) {
    if (active) global.apply(settings);
    active = false;
  }
  template <class Settings>
  ReaderPreferences persisted(const Settings& settings) const {
    return active ? global : ReaderPreferences::capture(settings);
  }
};
