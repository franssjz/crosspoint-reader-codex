#pragma once
#include <cstddef>
#include <cstdint>
namespace buildscratch {
inline uint8_t* claim(size_t, size_t* = nullptr) { return nullptr; }
inline void release(const uint8_t*) {}
}  // namespace buildscratch
