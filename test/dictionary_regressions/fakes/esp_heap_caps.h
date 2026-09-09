#pragma once
#include "Esp.h"
constexpr int MALLOC_CAP_8BIT = 1;
constexpr int MALLOC_CAP_DEFAULT = 2;
inline uint32_t heap_caps_get_largest_free_block(int) { return dictionaryTestLargestBlock; }
