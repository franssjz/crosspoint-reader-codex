#include "I18n.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

#include <cstddef>
#include <cstring>

#include "I18nStrings.h"

using namespace i18n_strings;

// Settings file path
static constexpr const char* SETTINGS_FILE = "/.crosspoint/language.bin";
static constexpr uint8_t SETTINGS_VERSION = 1;

I18n& I18n::getInstance() {
  static I18n instance;
  return instance;
}

const char* I18n::get(StrId id) const {
  const auto index = static_cast<size_t>(id);
  if (index >= static_cast<size_t>(StrId::_COUNT)) {
    return "???";
  }

  const LangStrings lang = getLanguageStrings(_language);
  const uint16_t off = lang.offsets[index];
  if (off & 0x8000) {
    return STRINGS_EN_DATA + (off & 0x7FFF);
  }
  return lang.data + off;
}

void I18n::setLanguage(Language lang) {
  if (lang >= Language::_COUNT) {
    return;
  }
  _language = lang;
  saveSettings();
}

const char* I18n::getLanguageName(Language lang) const {
  const auto index = static_cast<size_t>(lang);
  if (index >= static_cast<size_t>(Language::_COUNT)) {
    return "???";
  }
  return LANGUAGE_NAMES[index];
}

Language I18n::languageFromCode(const char* code) {
  for (uint8_t i = 0; i < getLanguageCount(); i++) {
    if (strcmp(code, LANGUAGE_CODES[i]) == 0) {
      return static_cast<Language>(i);
    }
  }
  return Language::EN;
}

void I18n::saveSettings() {
  Storage.mkdir("/.crosspoint");

  HalFile file;
  if (!Storage.openFileForWrite("I18N", SETTINGS_FILE, file)) {
    LOG_ERR("I18N", "Failed to save settings");
    return;
  }

  serialization::writePod(file, SETTINGS_VERSION);
  serialization::writePod(file, static_cast<uint8_t>(_language));

  file.close();
  LOG_DBG("I18N", "Settings saved: language=%d", static_cast<int>(_language));
}

void I18n::loadSettings() {
  HalFile file;
  if (!Storage.openFileForRead("I18N", SETTINGS_FILE, file)) {
    LOG_DBG("I18N", "No settings file, using default (English)");
    return;
  }

  uint8_t version;
  serialization::readPod(file, version);
  if (version != SETTINGS_VERSION) {
    LOG_ERR("I18N", "Settings version mismatch");
    return;
  }

  uint8_t lang;
  serialization::readPod(file, lang);
  if (lang < static_cast<size_t>(Language::_COUNT)) {
    _language = static_cast<Language>(lang);
    LOG_DBG("I18N", "Loaded language: %d", static_cast<int>(_language));
  }
}

// Generate character set for a specific language
const char* I18n::getCharacterSet(Language lang) {
  const auto langIndex = static_cast<size_t>(lang);
  if (langIndex >= static_cast<size_t>(Language::_COUNT)) {
    lang = Language::EN;  // Fallback to first language
  }

  return CHARACTER_SETS[static_cast<size_t>(lang)];
}
