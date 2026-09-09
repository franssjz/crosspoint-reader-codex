#pragma once
#include <cstdint>
inline uint32_t dictionaryTestFreeHeap = 250000;
inline uint32_t dictionaryTestLargestBlock = 220000;
struct DictionaryTestEsp {
  uint32_t getFreeHeap() const { return dictionaryTestFreeHeap; }
};
inline DictionaryTestEsp ESP;
