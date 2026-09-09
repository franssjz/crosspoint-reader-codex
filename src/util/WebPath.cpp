#include "WebPath.h"

#include <cstring>

namespace {
bool separator(char value) { return value == '/' || value == '\\'; }

bool invalidByte(unsigned char value) { return value < 32 || value == 127; }

bool invalidFilenameByte(char value) {
  return separator(value) || value == ':' || value == '*' || value == '?' || value == '"' || value == '<' ||
         value == '>' || value == '|';
}

bool equalsIgnoreAsciiCase(std::string_view lhs, std::string_view rhs) {
  if (lhs.size() != rhs.size()) return false;
  for (size_t i = 0; i < lhs.size(); ++i) {
    const char a = lhs[i] >= 'A' && lhs[i] <= 'Z' ? lhs[i] + ('a' - 'A') : lhs[i];
    const char b = rhs[i] >= 'A' && rhs[i] <= 'Z' ? rhs[i] + ('a' - 'A') : rhs[i];
    if (a != b) return false;
  }
  return true;
}

int hexValue(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}
}  // namespace

bool WebPath::normalize(std::string_view input, char* output, const size_t capacity, size_t& length) {
  length = 0;
  if (!output || capacity < 2) return false;
  output[0] = '\0';
  size_t written = 1;
  output[0] = '/';
  for (size_t start = 0; start < input.size();) {
    if (separator(input[start])) {
      ++start;
      continue;
    }
    size_t end = start;
    while (end < input.size() && !separator(input[end])) ++end;
    const auto segment = input.substr(start, end - start);
    start = end;
    if (segment == ".") continue;
    if (segment == "..") {
      // Reject attempts to walk above the SD root instead of silently clamping.
      if (written == 1) return false;
      while (written > 1 && output[written - 1] != '/') --written;
      if (written > 1) --written;
      continue;
    }
    for (const unsigned char value : segment) {
      if (invalidByte(value) || invalidFilenameByte(static_cast<char>(value))) return false;
    }
    // FAT strips trailing dots/spaces in some lookups. Reject ambiguous paths
    // so the name checked for protection is the name the storage layer opens.
    if (segment.back() == '.' || segment.back() == ' ') return false;
    const size_t slash = written > 1 ? 1 : 0;
    if (written >= capacity || slash + segment.size() >= capacity - written) return false;
    if (slash) output[written++] = '/';
    std::memcpy(output + written, segment.data(), segment.size());
    written += segment.size();
  }
  output[written] = '\0';
  length = written;
  return true;
}

bool WebPath::decodeUrlPath(std::string_view input, char* output, const size_t capacity, size_t& length) {
  length = 0;
  if (!output || capacity == 0) return false;
  output[0] = '\0';
  for (size_t i = 0; i < input.size(); ++i) {
    unsigned char value = input[i];
    if (value == '%') {
      if (i + 2 >= input.size()) return false;
      const int high = hexValue(input[i + 1]);
      const int low = hexValue(input[i + 2]);
      if (high < 0 || low < 0) return false;
      value = static_cast<unsigned char>(high * 16 + low);
      i += 2;
    }
    if (invalidByte(value) || length + 1 >= capacity) return false;
    // '+' is a literal path character, unlike application/x-www-form-urlencoded.
    output[length++] = static_cast<char>(value);
  }
  output[length] = '\0';
  return true;
}

bool WebPath::sanitizeFilename(std::string_view input, char* output, const size_t capacity, size_t& length) {
  length = 0;
  if (!output || input.empty() || capacity <= input.size() || input == "." || input == "..") return false;
  for (const unsigned char value : input) {
    // Reject embedded NUL/control bytes instead of accidentally accepting a
    // C-string prefix. Replace path syntax without truncating the extension.
    if (invalidByte(value)) return false;
    output[length++] = invalidFilenameByte(static_cast<char>(value)) ? '_' : static_cast<char>(value);
  }
  while (length > 0 && (output[length - 1] == '.' || output[length - 1] == ' ')) --length;
  output[length] = '\0';
  return length > 0;
}

bool WebPath::isProtected(std::string_view path) {
  if (path.empty() || path.front() != '/') return true;
  for (size_t start = 1; start < path.size();) {
    const size_t found = path.find('/', start);
    const size_t end = found == std::string_view::npos ? path.size() : found;
    const auto segment = path.substr(start, end - start);
    if (segment.empty() || segment.front() == '.' || equalsIgnoreAsciiCase(segment, "System Volume Information") ||
        equalsIgnoreAsciiCase(segment, "XTCache")) {
      return true;
    }
    if (segment.back() == '.' || segment.back() == ' ') return true;
    for (const unsigned char value : segment) {
      if (invalidByte(value) || invalidFilenameByte(static_cast<char>(value))) return true;
    }
    start = end + 1;
  }
  return false;
}
