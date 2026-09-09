#pragma once
#include <cstdint>
#include <cstdlib>
inline uint32_t millis() { return 0; }
inline void delay(uint32_t) {}
inline void vTaskDelay(uint32_t) {}
struct TestEsp {
  uint32_t getFreeHeap() const { return 256 * 1024; }
  uint32_t getMaxAllocHeap() const { return 128 * 1024; }
};
inline TestEsp ESP;
