#pragma once

#include <cstddef>
#include <string_view>

// Paths received by the file APIs are untrusted. These helpers allocate nothing
// and preserve UTF-8 bytes; callers own the output buffer and its lifetime.
namespace WebPath {
bool normalize(std::string_view input, char* output, size_t capacity, size_t& length);
bool decodeUrlPath(std::string_view input, char* output, size_t capacity, size_t& length);
bool sanitizeFilename(std::string_view input, char* output, size_t capacity, size_t& length);
bool isProtected(std::string_view normalizedPath);
}  // namespace WebPath
