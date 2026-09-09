#pragma once
#include <cstdint>
struct KeyDef {
  uint32_t primary;
  uint32_t secondary;
};
namespace KeyboardLayoutSet {
enum Id : uint8_t { Qwerty = 0, Azerty = 1, Qwertz = 2, Spanish = 3, Cyrillic = 4, HebrewReserved = 5 };
constexpr uint8_t COUNT = 5;
constexpr uint8_t normalizeMask(uint8_t mask) {
  mask &= 0x1F;  // Hebrew is reserved until CPR has a complete bidi renderer.
  return (mask & 0x0F) ? mask : static_cast<uint8_t>(mask | 1);
}
constexpr bool multiple(uint8_t mask) {
  mask = normalizeMask(mask);
  return (mask & (mask - 1)) != 0;
}
uint8_t first(uint8_t mask);
uint8_t next(uint8_t mask, uint8_t current);
int columns(uint8_t id);
const char* name(uint8_t id);
const char* rowText(uint8_t id, int row, bool shifted);
KeyDef key(uint8_t id, int row, int column);
}  // namespace KeyboardLayoutSet
