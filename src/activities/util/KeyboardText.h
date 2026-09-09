#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
namespace KeyboardText {
inline bool continuation(unsigned char c) { return (c & 0xC0) == 0x80; }
inline size_t next(const std::string& text, size_t pos) {
  if (pos >= text.size()) return text.size();
  ++pos;
  while (pos < text.size() && continuation(static_cast<unsigned char>(text[pos]))) ++pos;
  return pos;
}
inline size_t previous(const std::string& text, size_t pos) {
  pos = std::min(pos, text.size());
  if (!pos) return 0;
  --pos;
  while (pos && continuation(static_cast<unsigned char>(text[pos]))) --pos;
  return pos;
}
inline size_t decode(const char* text, uint32_t& cp) {
  const auto first = static_cast<unsigned char>(*text);
  if (!first) {
    cp = 0;
    return 0;
  }
  size_t size = first < 0x80             ? 1
                : (first & 0xE0) == 0xC0 ? 2
                : (first & 0xF0) == 0xE0 ? 3
                : (first & 0xF8) == 0xF0 ? 4
                                         : 0;
  if (!size) {
    cp = 0xFFFD;
    return 1;
  }
  cp = first & (size == 1 ? 0x7F : size == 2 ? 0x1F : size == 3 ? 0x0F : 0x07);
  for (size_t i = 1; i < size; ++i) {
    const auto byte = static_cast<unsigned char>(text[i]);
    if (!byte || !continuation(byte)) {
      cp = 0xFFFD;
      return 1;
    }
    cp = (cp << 6) | (byte & 0x3F);
  }
  if ((size == 2 && cp < 0x80) || (size == 3 && cp < 0x800) || (size == 4 && cp < 0x10000) || cp > 0x10FFFF ||
      (cp >= 0xD800 && cp <= 0xDFFF)) {
    cp = 0xFFFD;
    return 1;
  }
  return size;
}
inline size_t encode(uint32_t cp, char (&out)[5]) {
  if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
  size_t size = 0;
  if (cp < 0x80)
    out[size++] = static_cast<char>(cp);
  else if (cp < 0x800) {
    out[size++] = static_cast<char>(0xC0 | (cp >> 6));
    out[size++] = static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out[size++] = static_cast<char>(0xE0 | (cp >> 12));
    out[size++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[size++] = static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out[size++] = static_cast<char>(0xF0 | (cp >> 18));
    out[size++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out[size++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[size++] = static_cast<char>(0x80 | (cp & 0x3F));
  }
  out[size] = 0;
  return size;
}
inline bool insert(std::string& text, size_t& cursor, uint32_t cp, size_t maxBytes) {
  char bytes[5];
  const size_t size = encode(cp, bytes);
  if (!cp || !size || (maxBytes && size > maxBytes - std::min(maxBytes, text.size()))) return false;
  cursor = std::min(cursor, text.size());
  text.insert(cursor, bytes, size);
  cursor += size;
  return true;
}
inline bool erasePrevious(std::string& text, size_t& cursor) {
  cursor = std::min(cursor, text.size());
  if (!cursor) return false;
  const size_t start = previous(text, cursor);
  text.erase(start, cursor - start);
  cursor = start;
  return true;
}
struct Display {
  std::string text;
  size_t cursor = 0;
};
inline Display display(const std::string& text, size_t cursor, bool masked, bool revealPrevious) {
  if (!masked) return {text, std::min(cursor, text.size())};
  Display out;
  out.text.reserve(text.size());
  const size_t reveal = cursor && revealPrevious ? previous(text, cursor) : text.size();
  for (size_t pos = 0; pos < text.size();) {
    if (pos == cursor) out.cursor = out.text.size();
    const size_t end = next(text, pos);
    if (pos == reveal)
      out.text.append(text, pos, end - pos);
    else
      out.text += '*';
    pos = end;
  }
  if (cursor >= text.size()) out.cursor = out.text.size();
  return out;
}
}  // namespace KeyboardText
