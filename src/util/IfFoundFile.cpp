#include "IfFoundFile.h"

#include <HalStorage.h>

#include <algorithm>
#include <cstdint>

namespace {
std::string toLowerAscii(std::string value) {
  for (char& c : value) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return value;
}

std::string basenameFromRootEntry(std::string value) {
  std::replace(value.begin(), value.end(), '\\', '/');
  const auto slash = value.find_last_of('/');
  if (slash != std::string::npos) {
    value = value.substr(slash + 1);
  }
  return value;
}

bool isIfFoundCandidate(const std::string& filename) {
  const std::string lower = toLowerAscii(basenameFromRootEntry(filename));
  return lower == "if_found" || lower == "if_found.txt" || lower == "if_found.txt.txt";
}

std::string readSmallTextFile(const std::string& path) {
  if (path.empty()) {
    return "";
  }

  HalFile file;
  if (!Storage.openFileForRead("IFF", path, file)) {
    return "";
  }

  std::string content;
  content.reserve(std::min(IfFoundFile::MAX_BYTES, static_cast<size_t>(file.fileSize())));
  uint8_t buffer[128];
  while (file.available() && content.size() < IfFoundFile::MAX_BYTES) {
    const size_t remaining = IfFoundFile::MAX_BYTES - content.size();
    const size_t toRead = std::min(remaining, sizeof(buffer));
    const int read = file.read(buffer, toRead);
    if (read <= 0) {
      break;
    }
    content.append(reinterpret_cast<const char*>(buffer), static_cast<size_t>(read));
  }

  file.close();
  return content;
}

void appendUtf8(std::string& out, const uint32_t codepoint) {
  if (codepoint == 0) {
    return;
  }
  if (codepoint <= 0x7F) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0x10FFFF) {
    out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

uint16_t readUtf16Unit(const std::string& value, const size_t offset, const bool littleEndian) {
  const uint16_t first = static_cast<uint8_t>(value[offset]);
  const uint16_t second = static_cast<uint8_t>(value[offset + 1]);
  return littleEndian ? static_cast<uint16_t>(first | (second << 8)) : static_cast<uint16_t>((first << 8) | second);
}

std::string decodeUtf16(const std::string& value, const bool littleEndian, size_t offset) {
  std::string out;
  out.reserve(value.size() / 2);
  while (offset + 1 < value.size()) {
    uint32_t codepoint = readUtf16Unit(value, offset, littleEndian);
    offset += 2;

    if (codepoint >= 0xD800 && codepoint <= 0xDBFF && offset + 1 < value.size()) {
      const uint16_t low = readUtf16Unit(value, offset, littleEndian);
      if (low >= 0xDC00 && low <= 0xDFFF) {
        codepoint = 0x10000 + (((codepoint - 0xD800) << 10) | (low - 0xDC00));
        offset += 2;
      }
    }

    appendUtf8(out, codepoint);
  }
  return out;
}

bool looksLikeUtf16Le(const std::string& value) { return value.size() >= 4 && value[1] == '\0' && value[3] == '\0'; }

bool looksLikeUtf16Be(const std::string& value) { return value.size() >= 4 && value[0] == '\0' && value[2] == '\0'; }

std::string normalizeTextEncoding(std::string value) {
  if (value.size() >= 3 && static_cast<uint8_t>(value[0]) == 0xEF && static_cast<uint8_t>(value[1]) == 0xBB &&
      static_cast<uint8_t>(value[2]) == 0xBF) {
    value.erase(0, 3);
  } else if (value.size() >= 2 && static_cast<uint8_t>(value[0]) == 0xFF && static_cast<uint8_t>(value[1]) == 0xFE) {
    value = decodeUtf16(value, true, 2);
  } else if (value.size() >= 2 && static_cast<uint8_t>(value[0]) == 0xFE && static_cast<uint8_t>(value[1]) == 0xFF) {
    value = decodeUtf16(value, false, 2);
  } else if (looksLikeUtf16Le(value)) {
    value = decodeUtf16(value, true, 0);
  } else if (looksLikeUtf16Be(value)) {
    value = decodeUtf16(value, false, 0);
  }

  return value;
}

std::string normalizeNewlines(std::string value) {
  size_t pos = 0;
  while ((pos = value.find("\r\n", pos)) != std::string::npos) {
    value.replace(pos, 2, "\n");
  }
  value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
  return value;
}
}  // namespace

std::string IfFoundFile::findPath() {
  if (Storage.exists(DEFAULT_PATH)) {
    return DEFAULT_PATH;
  }

  const auto rootFiles = Storage.listFiles("/", 128);
  for (const auto& file : rootFiles) {
    std::string filename = file.c_str();
    if (!isIfFoundCandidate(filename)) {
      continue;
    }
    if (filename.empty() || filename.front() != '/') {
      filename = "/" + filename;
    }
    return filename;
  }

  return "";
}

std::string IfFoundFile::readNormalized(const std::string& path) {
  return normalizeNewlines(normalizeTextEncoding(readSmallTextFile(path)));
}
