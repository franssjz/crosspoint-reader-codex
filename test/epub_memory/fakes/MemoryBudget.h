#pragma once
#include <cstddef>
#include <cstdint>
namespace MemoryBudget {
struct HeapSnapshot {
  uint32_t freeHeap, maxAllocHeap;
};
inline HeapSnapshot heap{1024 * 1024, 1024 * 1024};
inline size_t successfulSnapshots = SIZE_MAX;
inline HeapSnapshot snapshot() {
  if (!successfulSnapshots) return {4096, 1024};
  if (successfulSnapshots != SIZE_MAX) --successfulSnapshots;
  return heap;
}
inline bool hasHeap(HeapSnapshot value, uint32_t free, uint32_t block) {
  return value.freeHeap >= free && value.maxAllocHeap >= block;
}
}  // namespace MemoryBudget
