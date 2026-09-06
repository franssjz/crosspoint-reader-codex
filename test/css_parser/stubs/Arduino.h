#pragma once

#include <cstddef>
#include <cstdint>

struct EspHostStub {
  uint32_t getMaxAllocHeap() const { return 1u << 20; }
  uint32_t getFreeHeap() const { return UINT32_MAX; }
};

inline EspHostStub ESP;
