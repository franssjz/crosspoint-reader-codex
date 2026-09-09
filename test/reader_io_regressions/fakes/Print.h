#pragma once
#include <cstddef>
#include <cstdint>

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t byte) { return write(&byte, 1); }
  virtual size_t write(const uint8_t* data, size_t length) = 0;
};
