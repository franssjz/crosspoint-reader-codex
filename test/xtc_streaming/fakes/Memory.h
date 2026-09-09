#pragma once
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
inline bool failXtcAllocation = false;
template <typename T, typename... Args>
  requires(!std::is_array_v<T>)
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  if (failXtcAllocation) return nullptr;
  return std::unique_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...));
}
template <typename T>
  requires std::is_unbounded_array_v<T>
std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  if (failXtcAllocation) return nullptr;
  using Element = std::remove_extent_t<T>;
  return std::unique_ptr<T>(new (std::nothrow) Element[count]());
}
