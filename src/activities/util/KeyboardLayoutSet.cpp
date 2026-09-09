#include "KeyboardLayoutSet.h"

#include "KeyboardText.h"
namespace KeyboardLayoutSet {
namespace {
struct Layout {
  const char* name;
  int columns;
  const char* normal[4];
  const char* shifted[4];
};
constexpr Layout layouts[] = {
    {"QWERTY",
     10,
     {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm=.,"},
     {"!@#$%^&*()", "QWERTYUIOP", "ASDFGHJKL_", "ZXCVBNM+><"}},
    {"AZERTY",
     10,
     {"1234567890", "azertyuiop", "qsdfghjklm", "wxcvbnéèàç"},
     {"!@#$%^&*()", "AZERTYUIOP", "QSDFGHJKLM", "WXCVBNÉÈÀÇ"}},
    {"QWERTZ",
     11,
     {"1234567890ß", "qwertzuiopü", "asdfghjklöä", "yxcvbnm=.,-"},
     {"!@#$%^&*()ß", "QWERTZUIOPÜ", "ASDFGHJKLÖÄ", "YXCVBNM+><_"}},
    {"Español",
     11,
     {"1234567890'", "qwertyuiopá", "asdfghjklñé", "zxcvbnmíóúü"},
     {"!@#$%^&*()?", "QWERTYUIOPÁ", "ASDFGHJKLÑÉ", "ZXCVBNMÍÓÚÜ"}},
    {"Кириллица",
     11,
     {"1234567890ё", "йцукенгшщзх", "фывапролджэ", "ячсмитьбюъ."},
     {"!@#$%^&*()Ё", "ЙЦУКЕНГШЩЗХ", "ФЫВАПРОЛДЖЭ", "ЯЧСМИТЬБЮЪ,"}},
};
uint32_t at(const char* text, int column) {
  for (int i = 0; text[0]; ++i) {
    uint32_t cp = 0;
    const size_t bytes = KeyboardText::decode(text, cp);
    if (i == column) return cp;
    text += bytes;
  }
  return 0;
}
}  // namespace
uint8_t first(uint8_t mask) {
  mask = normalizeMask(mask);
  for (uint8_t id = 0; id < COUNT; ++id)
    if (mask & (1u << id)) return id;
  return Qwerty;
}
uint8_t next(uint8_t mask, uint8_t current) {
  mask = normalizeMask(mask);
  for (uint8_t step = 1; step <= COUNT; ++step) {
    const uint8_t id = (current + step) % COUNT;
    if (mask & (1u << id)) return id;
  }
  return Qwerty;
}
int columns(uint8_t id) { return layouts[id < COUNT ? id : Qwerty].columns; }
const char* name(uint8_t id) { return layouts[id < COUNT ? id : Qwerty].name; }
const char* rowText(uint8_t id, int row, bool shifted) {
  if (id >= COUNT || row < 0 || row >= 4) return "";
  return shifted ? layouts[id].shifted[row] : layouts[id].normal[row];
}
KeyDef key(uint8_t id, int row, int column) {
  if (id >= COUNT || row < 0 || row >= 4 || column < 0 || column >= columns(id)) return {};
  return {at(layouts[id].normal[row], column), at(layouts[id].shifted[row], column)};
}
}  // namespace KeyboardLayoutSet
