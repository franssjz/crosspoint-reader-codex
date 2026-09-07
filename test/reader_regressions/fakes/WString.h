#pragma once

#include <cstddef>
#include <string>

class String {
 public:
  String() = default;
  String(const char* value) : storage(value ? value : "") {}

  const char* c_str() const { return storage.c_str(); }
  size_t length() const { return storage.length(); }

 private:
  std::string storage;
};
