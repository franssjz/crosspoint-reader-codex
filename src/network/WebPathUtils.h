#pragma once

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>

#include <limits>

#include "util/WebPath.h"

namespace WebPathUtils {
inline String normalize(const String& input, const bool decodeUrl = false) {
  const size_t size = input.length();
  if (size > (std::numeric_limits<size_t>::max() - 4) / 2) return {};
  // Request-sized scratch is owned only during validation, never on the small
  // network task stack. URL decoding and normalization have separate regions.
  auto scratch = makeUniqueNoThrow<char[]>(decodeUrl ? size * 2 + 4 : size + 2);
  if (!scratch) {
    LOG_ERR("WEB", "Out of memory validating file path");
    return {};
  }
  std::string_view source(input.c_str(), size);
  char* normalized = scratch.get();
  size_t length = 0;
  if (decodeUrl) {
    if (!WebPath::decodeUrlPath(source, scratch.get(), size + 1, length)) return {};
    source = std::string_view(scratch.get(), length);
    normalized += size + 1;
  }
  if (!WebPath::normalize(source, normalized, size + 2, length)) return {};
  String result;
  if (!result.reserve(length)) {
    LOG_ERR("WEB", "Out of memory storing normalized file path");
    return {};
  }
  result = normalized;
  return result;
}

inline String sanitizeFilename(const String& input) {
  auto scratch = makeUniqueNoThrow<char[]>(input.length() + 1);
  if (!scratch) {
    LOG_ERR("WEB", "Out of memory validating upload filename");
    return {};
  }
  size_t length = 0;
  if (!WebPath::sanitizeFilename({input.c_str(), input.length()}, scratch.get(), input.length() + 1, length)) return {};
  String result;
  if (!result.reserve(length)) return {};
  result = scratch.get();
  return result;
}

inline bool isProtected(const String& path) { return WebPath::isProtected({path.c_str(), path.length()}); }
}  // namespace WebPathUtils
