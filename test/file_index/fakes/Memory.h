#pragma once
#include <algorithm>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
inline int allocationBudget = -1;
inline size_t largestAllocation = 0;
inline bool allocationAllowed(size_t bytes) {
  largestAllocation = std::max(largestAllocation, bytes);
  return allocationBudget < 0 || allocationBudget-- > 0;
}
template <typename T, typename... Args>
  requires(!std::is_array_v<T>)
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  if (!allocationAllowed(sizeof(T))) return nullptr;
  return std::unique_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...));
}
template <typename T>
  requires std::is_unbounded_array_v<T>
std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  using Element = std::remove_extent_t<T>;
  if (!allocationAllowed(count * sizeof(Element))) return nullptr;
  return std::unique_ptr<T>(new (std::nothrow) Element[count]());
}
