#pragma once
enum DictionaryTestTranslation {
  STR_DICTIONARY_IO_ERROR,
  STR_DICTIONARY_INVALID,
  STR_DICTIONARY_LOW_MEMORY,
  STR_DICTIONARY_NONE_SELECTED,
  STR_DICTIONARY_NOT_READY,
  STR_DEFINITION_NOT_FOUND
};
inline const char* tr(DictionaryTestTranslation key) {
  static const char* text[] = {"I/O error",     "Invalid dictionary", "Low memory",
                               "No dictionary", "Not ready",          "Not found"};
  return text[key];
}
