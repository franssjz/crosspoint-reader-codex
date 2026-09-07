#include "DictionaryStore.h"

#include <ArduinoJson.h>
#include <Epub/htmlEntities.h>
#include <Esp.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "CrossPointSettings.h"
#include "DictionaryFontSelection.h"

namespace {
constexpr uint32_t CACHE_MAGIC = 0x44435458;  // DCTX
constexpr uint8_t NULL_TERMINATED_TYPES[] = {'m', 'l', 'g', 't', 'x', 'y', 'k', 'w', 'h', 'n', 'r'};
constexpr uint8_t DEFINITION_TEXT_SIZE_CONFIG_VERSION = 4;
constexpr size_t MAX_METADATA_TEXT_BYTES = 96;
constexpr size_t SAFE_MAX_DEFINITION_BYTES = 8192;
constexpr size_t SAFE_MIN_DEFINITION_BYTES = 1024;
constexpr size_t MAX_SUGGESTION_CANDIDATES = 24;
constexpr size_t MAX_EDIT_DISTANCE_BYTES = 64;
constexpr uint32_t HEAP_SCAN_GUARD_BYTES = 32 * 1024;
constexpr uint32_t HEAP_SMALL_GUARD_BYTES = 16 * 1024;

uint32_t largestFreeBlock() { return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_DEFAULT); }

bool hasHeapHeadroom(const size_t bytes, const uint32_t guardBytes = HEAP_SMALL_GUARD_BYTES) {
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t largest = largestFreeBlock();
  return freeHeap > bytes + guardBytes && largest > bytes + 1024;
}

size_t safeDefinitionReadBytes(const size_t requestedBytes) {
  size_t bytes = std::min<size_t>(requestedBytes, SAFE_MAX_DEFINITION_BYTES);
  while (bytes > SAFE_MIN_DEFINITION_BYTES && !hasHeapHeadroom(bytes * 4 + 8192, HEAP_SMALL_GUARD_BYTES)) {
    bytes /= 2;
  }
  if (!hasHeapHeadroom(bytes * 2 + 4096, HEAP_SMALL_GUARD_BYTES)) return 0;
  return bytes;
}

bool hasExtension(const std::string& name, const char* ext) {
  if (name.size() < strlen(ext)) return false;
  std::string suffix = name.substr(name.size() - strlen(ext));
  std::transform(suffix.begin(), suffix.end(), suffix.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return suffix == ext;
}

std::string joinPath(const std::string& dir, const std::string& name) {
  if (dir.empty() || dir == "/") return "/" + name;
  return dir + "/" + name;
}

std::string fileStem(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  const size_t start = slash == std::string::npos ? 0 : slash + 1;
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos || dot < start) return path.substr(start);
  return path.substr(start, dot - start);
}

std::string parentDirectory(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos || slash == 0) return "/";
  return path.substr(0, slash);
}

std::string pathBaseName(const std::string& path) {
  if (path.empty() || path == "/") return "";
  size_t end = path.size();
  while (end > 1 && path[end - 1] == '/') --end;
  const size_t slash = path.find_last_of('/', end - 1);
  const size_t start = slash == std::string::npos ? 0 : slash + 1;
  return path.substr(start, end - start);
}

std::string limitMetadataText(std::string value, const size_t maxBytes = MAX_METADATA_TEXT_BYTES) {
  if (value.size() <= maxBytes) return value;
  value.resize(maxBytes);
  while (!value.empty() && (static_cast<unsigned char>(value.back()) & 0xC0) == 0x80) {
    value.pop_back();
  }
  return value;
}

uint32_t readBE32(const uint8_t* buf) {
  return (static_cast<uint32_t>(buf[0]) << 24) | (static_cast<uint32_t>(buf[1]) << 16) |
         (static_cast<uint32_t>(buf[2]) << 8) | static_cast<uint32_t>(buf[3]);
}

void writeU32(uint8_t* buf, uint32_t value) {
  buf[0] = static_cast<uint8_t>(value >> 24);
  buf[1] = static_cast<uint8_t>(value >> 16);
  buf[2] = static_cast<uint8_t>(value >> 8);
  buf[3] = static_cast<uint8_t>(value);
}

uint32_t readU32(const uint8_t* buf) { return readBE32(buf); }

bool readLine(HalFile& file, char* buf, size_t len) {
  if (len == 0) return false;
  size_t index = 0;
  while (index + 1 < len) {
    const int c = file.read();
    if (c < 0) break;
    buf[index++] = static_cast<char>(c);
    if (c == '\n') break;
  }
  buf[index] = '\0';
  return index > 0;
}

void stripLineEnd(std::string& value) {
  while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ')) {
    value.pop_back();
  }
}

bool parseIfoField(const char* line, const char* key, std::string& out) {
  const size_t keyLen = strlen(key);
  if (strncmp(line, key, keyLen) != 0 || line[keyLen] != '=') return false;
  out = line + keyLen + 1;
  stripLineEnd(out);
  return true;
}

std::string readIndexWord(HalFile& file) {
  std::string word;
  word.reserve(32);
  while (true) {
    const int c = file.read();
    if (c <= 0) break;
    word.push_back(static_cast<char>(c));
    if (word.size() > 255) break;
  }
  return word;
}

int compareWords(const std::string& lhs, const std::string& rhs) {
  const unsigned char* a = reinterpret_cast<const unsigned char*>(lhs.c_str());
  const unsigned char* b = reinterpret_cast<const unsigned char*>(rhs.c_str());
  while (*a && *b) {
    const int ca = (*a < 0x80) ? std::tolower(*a) : *a;
    const int cb = (*b < 0x80) ? std::tolower(*b) : *b;
    if (ca != cb) return ca - cb;
    ++a;
    ++b;
  }
  const int ca = (*a < 0x80) ? std::tolower(*a) : *a;
  const int cb = (*b < 0x80) ? std::tolower(*b) : *b;
  return ca - cb;
}

std::string lowercaseLatinUtf8(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  for (size_t i = 0; i < input.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(input[i]);
    if (c < 0x80) {
      out.push_back(static_cast<char>(std::tolower(c)));
      continue;
    }
    if (c == 0xC3 && i + 1 < input.size()) {
      const unsigned char n = static_cast<unsigned char>(input[i + 1]);
      if (n >= 0x80 && n <= 0x96 && n != 0x97) {
        out.push_back(static_cast<char>(0xC3));
        out.push_back(static_cast<char>(n + 0x20));
        ++i;
        continue;
      }
      if (n == 0x9C) {
        out.push_back(static_cast<char>(0xC3));
        out.push_back(static_cast<char>(0xBC));
        ++i;
        continue;
      }
    }
    out.push_back(input[i]);
  }
  return out;
}

bool isAsciiTrimChar(unsigned char c) { return c < 0x80 && !std::isalnum(c); }

bool matchTokenAt(const std::string& text, const size_t pos, const char* token) {
  const size_t len = strlen(token);
  return pos + len <= text.size() && text.compare(pos, len, token) == 0;
}

bool isUtf8TrimPrefix(const std::string& text, const size_t pos, size_t& tokenLen) {
  static constexpr const char* TOKENS[] = {"\xC2\xA1",     "\xC2\xAB",     "\xC2\xBB",     "\xC2\xBF",
                                           "\xE2\x80\x93", "\xE2\x80\x94", "\xE2\x80\x98", "\xE2\x80\x99",
                                           "\xE2\x80\x9C", "\xE2\x80\x9D", "\xE2\x80\xA6"};
  for (const char* token : TOKENS) {
    if (matchTokenAt(text, pos, token)) {
      tokenLen = strlen(token);
      return true;
    }
  }
  return false;
}

bool isUtf8TrimSuffix(const std::string& text, const size_t end, size_t& tokenLen) {
  static constexpr const char* TOKENS[] = {"\xC2\xA1",     "\xC2\xAB",     "\xC2\xBB",     "\xC2\xBF",
                                           "\xE2\x80\x93", "\xE2\x80\x94", "\xE2\x80\x98", "\xE2\x80\x99",
                                           "\xE2\x80\x9C", "\xE2\x80\x9D", "\xE2\x80\xA6"};
  for (const char* token : TOKENS) {
    const size_t len = strlen(token);
    if (end >= len && text.compare(end - len, len, token) == 0) {
      tokenLen = len;
      return true;
    }
  }
  return false;
}

void appendWithSeparator(std::string& out, const char* data, size_t len) {
  while (len > 0 && (*data == '\0' || *data == '\r' || *data == '\n' || *data == ' ' || *data == '\t')) {
    ++data;
    --len;
  }
  while (len > 0 && (data[len - 1] == '\0' || data[len - 1] == '\r' || data[len - 1] == '\n' || data[len - 1] == ' ' ||
                     data[len - 1] == '\t')) {
    --len;
  }
  if (len == 0) return;
  if (!out.empty() && out.back() != '\n') out.push_back('\n');
  out.append(data, len);
}

bool isValidUtf8(const char* data, const size_t len) {
  for (size_t i = 0; i < len;) {
    const unsigned char c = static_cast<unsigned char>(data[i]);
    if (c < 0x80) {
      ++i;
      continue;
    }

    uint32_t cp = 0;
    size_t needed = 0;
    if ((c & 0xE0) == 0xC0) {
      cp = c & 0x1F;
      needed = 2;
      if (cp == 0) return false;
    } else if ((c & 0xF0) == 0xE0) {
      cp = c & 0x0F;
      needed = 3;
    } else if ((c & 0xF8) == 0xF0) {
      cp = c & 0x07;
      needed = 4;
    } else {
      return false;
    }

    if (i + needed > len) return false;
    for (size_t j = 1; j < needed; ++j) {
      const unsigned char cc = static_cast<unsigned char>(data[i + j]);
      if ((cc & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (cc & 0x3F);
    }

    if ((needed == 2 && cp < 0x80) || (needed == 3 && cp < 0x800) || (needed == 4 && cp < 0x10000) ||
        (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
      return false;
    }
    i += needed;
  }
  return true;
}

void appendUtf8(std::string& out, const uint32_t cp) {
  if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return;
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

uint32_t windows1252Codepoint(const unsigned char c) {
  static constexpr uint16_t C1_MAP[32] = {
      0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
      0x2039, 0x0152, 0x0000, 0x017D, 0x0000, 0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
      0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178,
  };
  if (c >= 0x80 && c <= 0x9F) return C1_MAP[c - 0x80];
  return c;
}

bool appendReplacementForUtf8Token(const std::string& input, const size_t pos, std::string& out, size_t& consumed) {
  struct Replacement {
    const char* token;
    const char* value;
  };

  static constexpr Replacement REPLACEMENTS[] = {
      {"\xEF\xBF\xBD", ""},    // Unicode replacement char.
      {"\xE2\x80\xA3", "- "},  // Triangular bullet.
      {"\xE2\x81\x83", "- "},  // Hyphen bullet.
      {"\xE2\x96\xA0", "- "},  // Black square.
      {"\xE2\x96\xBA", "- "},  // Black right pointer.
      {"\xE2\x96\xB8", "- "},  // Small right triangle.
      {"\xE2\x97\x86", "- "},  // Black diamond.
      {"\xE2\x97\x87", "- "},  // White diamond.
      {"\xE2\x97\x8B", "- "},  // White circle.
      {"\xE2\x97\x8F", "- "},  // Black circle.
      {"\xE2\x99\xA6", "- "},  // Diamond suit.
      {"\xCB\x88", ""},        // IPA primary stress.
      {"\xCB\x8C", ""},        // IPA secondary stress.
      {"\xCB\x90", ":"},       // IPA length mark.
      {"\xCB\x91", ":"},       // IPA half-length mark.
      {"\xC9\xA3", "g"},      {"\xC9\xBE", "r"}, {"\xCA\x9D", "y"},  {"\xCA\x8E", "ll"}, {"\xC9\xB2", "n"},
      {"\xCE\xB2", "b"},      {"\xC3\xB0", "d"}, {"\xCE\xB8", "z"},  {"\xCA\x83", "sh"}, {"\xCA\x92", "zh"},
      {"\xC9\x9F", "y"},      {"\xC9\xAB", "l"}, {"\xC9\xB1", "m"},  {"\xC9\xB0", "g"},  {"\xC9\xB8", "f"},
      {"\xC9\x9B", "e"},      {"\xC9\x99", "e"}, {"\xC3\xA6", "ae"}, {"\xC5\x8B", "ng"}, {"\xC9\xAA", "i"},
      {"\xCA\x8A", "u"},      {"\xC9\x94", "o"}, {"\xC9\x91", "a"},  {"\xCA\x8C", "a"},
  };

  for (const auto& replacement : REPLACEMENTS) {
    const size_t len = strlen(replacement.token);
    if (pos + len <= input.size() && input.compare(pos, len, replacement.token) == 0) {
      out += replacement.value;
      consumed = len;
      return true;
    }
  }
  return false;
}

std::string sanitizeDefinitionGlyphs(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  bool inEarlyBracket = false;
  bool earlyPrefix = true;
  int nonSpaceChars = 0;

  for (size_t i = 0; i < input.size();) {
    size_t consumed = 0;
    if (appendReplacementForUtf8Token(input, i, out, consumed)) {
      i += consumed;
      continue;
    }

    const char c = input[i];
    if (earlyPrefix) {
      if (c == '[') {
        inEarlyBracket = true;
      } else if (c == ']') {
        inEarlyBracket = false;
      }

      if (c == '?' && inEarlyBracket) {
        ++i;
        continue;
      }

      if (c == '\n') {
        earlyPrefix = nonSpaceChars < 160;
      } else if (c != ' ' && c != '\t' && c != '\r') {
        ++nonSpaceChars;
        if (nonSpaceChars > 220) earlyPrefix = false;
      }
    }

    out.push_back(c);
    ++i;
  }
  return out;
}

std::string ensureUtf8Text(const char* data, const size_t len) {
  if (len == 0) return "";
  if (isValidUtf8(data, len)) return std::string(data, len);

  std::string out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    const unsigned char c = static_cast<unsigned char>(data[i]);
    const uint32_t cp = windows1252Codepoint(c);
    if (cp < 0x20 && c != '\n' && c != '\r' && c != '\t') continue;
    appendUtf8(out, cp);
  }
  return out;
}

void appendNormalizedField(std::string& out, const char* data, const size_t len) {
  const std::string normalized = ensureUtf8Text(data, len);
  appendWithSeparator(out, normalized.data(), normalized.size());
}

void appendLogicalNewline(std::string& out) {
  while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) out.pop_back();
  if (out.empty() || out.back() == '\n') return;
  out.push_back('\n');
}

void appendLogicalSpace(std::string& out) {
  if (out.empty() || out.back() == '\n' || out.back() == ' ' || out.back() == '\t') return;
  out.push_back(' ');
}

bool appendDecodedEntity(const std::string& input, size_t& i, std::string& out) {
  if (input[i] != '&') return false;
  const size_t semi = input.find(';', i + 1);
  if (semi == std::string::npos || semi - i > 24) return false;

  const size_t entityLen = semi - i + 1;
  if (entityLen >= 4 && input[i + 1] == '#') {
    const bool hex = input[i + 2] == 'x' || input[i + 2] == 'X';
    const size_t firstDigit = i + (hex ? 3 : 2);
    if (firstDigit >= semi) return false;

    uint32_t value = 0;
    for (size_t pos = firstDigit; pos < semi; ++pos) {
      const unsigned char c = static_cast<unsigned char>(input[pos]);
      uint8_t digit = 0;
      if (c >= '0' && c <= '9') {
        digit = c - '0';
      } else if (hex && c >= 'a' && c <= 'f') {
        digit = 10 + c - 'a';
      } else if (hex && c >= 'A' && c <= 'F') {
        digit = 10 + c - 'A';
      } else {
        return false;
      }
      value = value * (hex ? 16 : 10) + digit;
      if (value > 0x10FFFF) return false;
    }

    if (value == 0x00A0) {
      appendLogicalSpace(out);
    } else if (value == 0x00AD) {
      // Soft hyphen is a layout hint, not visible text.
    } else if ((value >= 0x2010 && value <= 0x2015) || value == 0x2212) {
      out.push_back('-');
    } else if (value == 0x2022 || value == 0x25E6 || value == 0x2043) {
      out += "- ";
    } else {
      appendUtf8(out, value);
    }
    i = semi + 1;
    return true;
  }

  if (input.compare(i, entityLen, "&nbsp;") == 0 || input.compare(i, entityLen, "&emsp;") == 0 ||
      input.compare(i, entityLen, "&ensp;") == 0 || input.compare(i, entityLen, "&thinsp;") == 0) {
    appendLogicalSpace(out);
    i = semi + 1;
    return true;
  }
  if (input.compare(i, entityLen, "&bull;") == 0 || input.compare(i, entityLen, "&middot;") == 0) {
    out += "- ";
    i = semi + 1;
    return true;
  }
  if (input.compare(i, entityLen, "&ndash;") == 0 || input.compare(i, entityLen, "&mdash;") == 0 ||
      input.compare(i, entityLen, "&minus;") == 0) {
    out.push_back('-');
    i = semi + 1;
    return true;
  }
  if (input.compare(i, entityLen, "&shy;") == 0) {
    i = semi + 1;
    return true;
  }

  if (const char* resolved = lookupHtmlEntity(input.data() + i, entityLen)) {
    out += resolved;
    i = semi + 1;
    return true;
  }
  return false;
}

std::string trimAsciiCopy(const std::string& value) {
  size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) ++start;
  size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
  return value.substr(start, end - start);
}

std::string foldLatinUtf8ToAscii(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  bool lastSpace = true;
  for (size_t i = 0; i < input.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(input[i]);
    char folded = '\0';
    if (c < 0x80) {
      if (std::isalnum(c)) {
        folded = static_cast<char>(std::tolower(c));
      } else if (std::isspace(c) || c == '-' || c == '/' || c == '.') {
        if (!lastSpace) {
          out.push_back(' ');
          lastSpace = true;
        }
        continue;
      } else {
        continue;
      }
    } else if (c == 0xC3 && i + 1 < input.size()) {
      const unsigned char n = static_cast<unsigned char>(input[i + 1]);
      switch (n) {
        case 0x81:
        case 0xA1:
        case 0x80:
        case 0xA0:
          folded = 'a';
          break;
        case 0x89:
        case 0xA9:
        case 0x88:
        case 0xA8:
          folded = 'e';
          break;
        case 0x8D:
        case 0xAD:
        case 0x8C:
        case 0xAC:
          folded = 'i';
          break;
        case 0x93:
        case 0xB3:
        case 0x92:
        case 0xB2:
          folded = 'o';
          break;
        case 0x9A:
        case 0xBA:
        case 0x9C:
        case 0xBC:
          folded = 'u';
          break;
        case 0x91:
        case 0xB1:
          folded = 'n';
          break;
        case 0x87:
        case 0xA7:
          folded = 'c';
          break;
        default:
          break;
      }
      ++i;
    }

    if (folded != '\0') {
      out.push_back(folded);
      lastSpace = false;
    }
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

bool startsWithFoldedWord(const std::string& text, const char* word) {
  const size_t len = strlen(word);
  return text.size() >= len && text.compare(0, len, word) == 0 && (text.size() == len || text[len] == ' ');
}

bool isGrammarHeadingLine(const std::string& line) {
  const std::string folded = foldLatinUtf8ToAscii(trimAsciiCopy(line));
  if (folded.empty() || folded.size() > 48) return false;

  static constexpr const char* HEADINGS[] = {
      "adjetivo",    "adverbio",    "pronombre",    "conjuncion",  "sustantivo",   "nombre",  "verbo",
      "articulo",    "preposicion", "interjeccion", "locucion",    "prefijo",      "sufijo",  "determinante",
      "contraccion", "abreviatura", "participio",   "gerundio",    "noun",         "verb",    "adjective",
      "adverb",      "pronoun",     "conjunction",  "preposition", "interjection", "article",
  };

  for (const char* heading : HEADINGS) {
    if (startsWithFoldedWord(folded, heading)) return true;
  }
  return false;
}

void appendFormattedLine(std::string& out, const std::string& line) {
  if (!out.empty()) out.push_back('\n');
  out += line;
}

int parenthesisDelta(const std::string& line) {
  int delta = 0;
  for (const char c : line) {
    if (c == '(' || c == '[') {
      ++delta;
    } else if (c == ')' || c == ']') {
      --delta;
    }
  }
  return delta;
}

bool startsWithNoSpacePunctuation(const std::string& line) {
  if (line.empty()) return false;
  const char c = line.front();
  return c == ',' || c == '.' || c == ';' || c == ':' || c == ')' || c == ']';
}

bool endsWithoutJoinSpace(const std::string& line) {
  if (line.empty()) return false;
  const char c = line.back();
  return c == '(' || c == '[' || c == '/' || c == '*' || c == '<';
}

void appendSoftJoinedLine(std::string& current, const std::string& next) {
  if (!current.empty() && !startsWithNoSpacePunctuation(next) && !endsWithoutJoinSpace(current)) {
    current.push_back(' ');
  }
  current += next;
}

std::string stripHeadingMarkers(const std::string& line) {
  const std::string trimmed = trimAsciiCopy(line);
  size_t pos = 0;
  while (pos < trimmed.size() && (trimmed[pos] == '#' || trimmed[pos] == '*')) ++pos;
  while (pos < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[pos]))) ++pos;
  if (pos > 0 && pos < trimmed.size()) {
    const std::string candidate = trimmed.substr(pos);
    if (isGrammarHeadingLine(candidate)) return candidate;
  }
  return line;
}

std::vector<std::string> coalesceDefinitionLines(const std::string& input) {
  std::vector<std::string> lines;
  std::string current;
  int openParentheses = 0;

  auto flushCurrent = [&]() {
    if (current.empty()) return;
    lines.push_back(current);
    current.clear();
    openParentheses = 0;
  };

  size_t lineStart = 0;
  for (size_t i = 0; i <= input.size(); ++i) {
    if (i != input.size() && input[i] != '\n') continue;

    std::string line = trimAsciiCopy(input.substr(lineStart, i - lineStart));
    lineStart = i + 1;

    if (line.empty()) {
      flushCurrent();
      if (!lines.empty() && !lines.back().empty()) lines.emplace_back();
      continue;
    }

    const bool heading = isGrammarHeadingLine(line);
    if (!current.empty() && openParentheses > 0 && !heading) {
      appendSoftJoinedLine(current, line);
      openParentheses = std::max(0, openParentheses + parenthesisDelta(line));
      continue;
    }

    flushCurrent();
    current = std::move(line);
    openParentheses = heading ? 0 : std::max(0, parenthesisDelta(current));
  }

  flushCurrent();
  while (!lines.empty() && lines.back().empty()) lines.pop_back();
  return lines;
}

std::string formatDefinitionBlocks(const std::string& input) {
  std::string out;
  out.reserve(input.size() + 32);
  bool hasOutputLine = false;
  bool pendingBlankAfterHeading = false;

  const std::vector<std::string> lines = coalesceDefinitionLines(input);
  for (const std::string& line : lines) {
    const bool empty = trimAsciiCopy(line).empty();
    const bool heading = !empty && isGrammarHeadingLine(line);

    if (empty) {
      if (!out.empty() && out.back() != '\n') out.push_back('\n');
      pendingBlankAfterHeading = false;
      continue;
    }

    if (heading && hasOutputLine && !out.empty() && out.back() != '\n') {
      out.push_back('\n');
    }
    if (pendingBlankAfterHeading && !heading && !out.empty() && out.back() != '\n') {
      out.push_back('\n');
    }

    appendFormattedLine(out, heading ? stripHeadingMarkers(line) : line);
    hasOutputLine = true;
    pendingBlankAfterHeading = heading;
  }

  return out;
}

std::string normalizedTagName(const std::string& input, size_t pos, const size_t end) {
  while (pos < end && std::isspace(static_cast<unsigned char>(input[pos]))) ++pos;
  if (pos < end && (input[pos] == '/' || input[pos] == '!' || input[pos] == '?')) ++pos;
  while (pos < end && std::isspace(static_cast<unsigned char>(input[pos]))) ++pos;

  std::string name;
  name.reserve(8);
  for (; pos < end; ++pos) {
    const unsigned char c = static_cast<unsigned char>(input[pos]);
    if (!(std::isalnum(c) || c == ':' || c == '-' || c == '_')) break;
    if (c == ':') {
      name.clear();
      continue;
    }
    name.push_back(static_cast<char>(std::tolower(c)));
    if (name.size() >= 12) break;
  }
  return name;
}

bool isClosingTag(const std::string& input, size_t pos, const size_t end) {
  while (pos < end && std::isspace(static_cast<unsigned char>(input[pos]))) ++pos;
  return pos < end && input[pos] == '/';
}

bool isBlockTag(const std::string& name) {
  return name == "p" || name == "div" || name == "section" || name == "article" || name == "blockquote" ||
         name == "pre" || name == "def" || name == "ex" || name == "dl" || name == "dt" || name == "dd" ||
         name == "ul" || name == "ol" || name == "table" || name == "tr" || name == "hr" || name == "h1" ||
         name == "h2" || name == "h3" || name == "h4" || name == "h5" || name == "h6";
}

void appendTagSpacing(std::string& out, const std::string& name, const bool closing) {
  if (name.empty()) return;
  if (name == "br" || name == "hr") {
    appendLogicalNewline(out);
    return;
  }
  if (name == "li" && !closing) {
    appendLogicalNewline(out);
    out += "- ";
    return;
  }
  if (name == "dd" && !closing) {
    appendLogicalNewline(out);
    out += "  ";
    return;
  }
  if ((name == "td" || name == "th") && !closing) {
    if (!out.empty() && out.back() != '\n') out.push_back('\t');
    return;
  }
  if (isBlockTag(name) || (closing && name == "li")) {
    appendLogicalNewline(out);
  }
}

bool isNullTerminatedType(uint8_t type) {
  for (const uint8_t t : NULL_TERMINATED_TYPES) {
    if (t == type) return true;
  }
  return false;
}

bool isDisplayDefinitionType(uint8_t type) {
  // Only render textual definition fields. Pronunciation, audio and resource fields often contain unsupported glyphs
  // or binary-ish payloads that look like replacement characters on the e-ink font.
  switch (std::tolower(static_cast<unsigned char>(type))) {
    case 'm':  // plain meaning
    case 'l':  // synonym / gloss
    case 'g':  // Pango markup
    case 'x':  // XDXF markup
    case 'k':  // KingSoft PowerWord markup
    case 'h':  // HTML
    case 'n':  // WordNet data
      return true;
    default:
      return false;
  }
}

void appendTypedField(std::string& out, uint8_t type, const char* data, size_t len) {
  if (!isDisplayDefinitionType(type)) return;
  appendNormalizedField(out, data, len);
}

std::string decodeStarDictData(const std::string& raw, const std::string& sameTypeSequence) {
  if (raw.empty()) return "";
  if (!sameTypeSequence.empty()) {
    std::string out;
    size_t pos = 0;
    for (const char typeChar : sameTypeSequence) {
      if (pos >= raw.size()) break;
      const uint8_t type = static_cast<uint8_t>(typeChar);
      if (isNullTerminatedType(type)) {
        const size_t start = pos;
        while (pos < raw.size() && raw[pos] != '\0') ++pos;
        appendTypedField(out, type, raw.data() + start, pos - start);
        if (pos < raw.size() && raw[pos] == '\0') ++pos;
        continue;
      }
      if (std::isupper(type) && pos + 4 <= raw.size()) {
        const uint32_t fieldSize = readBE32(reinterpret_cast<const uint8_t*>(raw.data() + pos));
        pos += 4;
        if (pos + fieldSize > raw.size()) break;
        appendTypedField(out, type, raw.data() + pos, fieldSize);
        pos += fieldSize;
        continue;
      }
      appendTypedField(out, type, raw.data() + pos, raw.size() - pos);
      pos = raw.size();
    }
    return out;
  }

  std::string out;
  size_t pos = 0;
  while (pos < raw.size()) {
    const uint8_t type = static_cast<uint8_t>(raw[pos++]);
    if (isNullTerminatedType(type)) {
      const size_t start = pos;
      while (pos < raw.size() && raw[pos] != '\0') ++pos;
      appendTypedField(out, type, raw.data() + start, pos - start);
      if (pos < raw.size() && raw[pos] == '\0') ++pos;
      continue;
    }
    if (std::isupper(type) && pos + 4 <= raw.size()) {
      const uint32_t fieldSize = readBE32(reinterpret_cast<const uint8_t*>(raw.data() + pos));
      pos += 4;
      if (pos + fieldSize > raw.size()) break;
      appendTypedField(out, type, raw.data() + pos, fieldSize);
      pos += fieldSize;
      continue;
    }

    if (isDisplayDefinitionType(type)) {
      out.push_back(static_cast<char>(type));
      out.append(raw.data() + pos, raw.size() - pos);
    }
    break;
  }
  return out;
}

std::string stripHtmlAndEntities(const std::string& input) {
  std::string out;
  out.reserve(input.size());

  for (size_t i = 0; i < input.size();) {
    const char c = input[i];
    if (c == '<') {
      const size_t tagEnd = input.find('>', i + 1);
      if (tagEnd == std::string::npos) {
        ++i;
        continue;
      }
      const bool closing = isClosingTag(input, i + 1, tagEnd);
      appendTagSpacing(out, normalizedTagName(input, i + 1, tagEnd), closing);
      i = tagEnd + 1;
      continue;
    }
    if (c == '&') {
      if (appendDecodedEntity(input, i, out)) continue;
      out.push_back(c);
      ++i;
      continue;
    }
    out.push_back(c == '\0' ? '\n' : c);
    ++i;
  }

  std::string compact;
  compact.reserve(out.size());
  bool lastSpace = false;
  bool atLineStart = true;
  int consecutiveNewlines = 0;
  int lineStartSpaces = 0;
  for (char c : out) {
    if (c == '\r') continue;
    if (c == '\0') c = '\n';
    if (c == '\n') {
      while (!compact.empty() && compact.back() == ' ') compact.pop_back();
      if (!compact.empty() && consecutiveNewlines < 2) {
        compact.push_back('\n');
        ++consecutiveNewlines;
      }
      lastSpace = false;
      atLineStart = true;
      lineStartSpaces = 0;
      continue;
    }
    if (c == '\t' || c == ' ') {
      if (atLineStart) {
        const int spaces = c == '\t' ? 2 : 1;
        for (int s = 0; s < spaces && lineStartSpaces < 4; ++s) {
          compact.push_back(' ');
          ++lineStartSpaces;
        }
      } else if (c == '\t') {
        if (!compact.empty() && compact.back() != ' ') compact.push_back(' ');
        compact.push_back(' ');
        lastSpace = true;
      } else if (!lastSpace) {
        compact.push_back(' ');
        lastSpace = true;
      }
      continue;
    }
    compact.push_back(c);
    lastSpace = false;
    atLineStart = false;
    consecutiveNewlines = 0;
  }
  while (!compact.empty() && (compact.back() == ' ' || compact.back() == '\n')) compact.pop_back();
  return formatDefinitionBlocks(sanitizeDefinitionGlyphs(compact));
}

bool startsWithAsciiPrefix(const std::string& text, const std::string& prefix) {
  return text.size() >= prefix.size() && compareWords(text.substr(0, prefix.size()), prefix) == 0;
}

bool isSpanishLanguage(const DictionaryEntry& entry) {
  const std::string id = foldLatinUtf8ToAscii(entry.languageId);
  const std::string lang = foldLatinUtf8ToAscii(entry.lang);
  return id == "spanish" || id == "es" || id == "espanol" || startsWithAsciiPrefix(lang, "es");
}

int commonPrefixLength(const std::string& a, const std::string& b) {
  const size_t maxLen = std::min(a.size(), b.size());
  size_t len = 0;
  while (len < maxLen && a[len] == b[len]) ++len;
  return static_cast<int>(len);
}

std::string suggestionScoreKey(const std::string& word) {
  std::string folded = foldLatinUtf8ToAscii(word);
  return folded.empty() ? lowercaseLatinUtf8(word) : folded;
}

int editDistanceLimited(const std::string& a, const std::string& b, int maxDist) {
  const int m = static_cast<int>(a.size());
  const int n = static_cast<int>(b.size());
  if (std::abs(m - n) > maxDist) return maxDist + 1;
  if (n > static_cast<int>(MAX_EDIT_DISTANCE_BYTES) || m > static_cast<int>(MAX_EDIT_DISTANCE_BYTES)) {
    return maxDist + 1;
  }

  int dp[MAX_EDIT_DISTANCE_BYTES + 1];
  for (int j = 0; j <= n; ++j) dp[j] = j;
  for (int i = 1; i <= m; ++i) {
    int prev = dp[0];
    dp[0] = i;
    int rowMin = dp[0];
    for (int j = 1; j <= n; ++j) {
      const int old = dp[j];
      if (a[i - 1] == b[j - 1]) {
        dp[j] = prev;
      } else {
        dp[j] = 1 + std::min({prev, dp[j], dp[j - 1]});
      }
      prev = old;
      rowMin = std::min(rowMin, dp[j]);
    }
    if (rowMin > maxDist) return maxDist + 1;
  }
  return dp[n];
}

bool isEnglishLanguage(const DictionaryEntry& entry) {
  const std::string id = lowercaseLatinUtf8(entry.languageId);
  const std::string lang = lowercaseLatinUtf8(entry.lang);
  return id == "english" || id == "en" || startsWithAsciiPrefix(lang, "en");
}

}  // namespace

DictionaryStore& DictionaryStore::getInstance() {
  static DictionaryStore instance;
  return instance;
}

void DictionaryStore::loadConfig() {
  configLoaded = true;
  activeIfoPath.clear();
  definitionTextSize = DEF_TEXT_SMALL;
  if (!Storage.exists(CONFIG_PATH)) return;

  const String json = Storage.readFile(CONFIG_PATH);
  if (json.isEmpty()) return;

  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return;
  activeIfoPath = doc["activeIfoPath"] | std::string("");
  const int storedSize = doc["definitionTextSize"] | static_cast<int>(definitionTextSize);
  const uint8_t sizeVersion = doc["definitionTextSizeVersion"] | static_cast<uint8_t>(0);
  if (sizeVersion == 0) {
    definitionTextSize = storedSize == 2 ? static_cast<uint8_t>(DEF_TEXT_LARGE) : static_cast<uint8_t>(DEF_TEXT_SMALL);
  } else if (sizeVersion == 1) {
    definitionTextSize = storedSize == 2 ? static_cast<uint8_t>(DEF_TEXT_LARGE) : static_cast<uint8_t>(DEF_TEXT_SMALL);
  } else if (sizeVersion < DEFINITION_TEXT_SIZE_CONFIG_VERSION) {
    definitionTextSize = storedSize >= 3 ? static_cast<uint8_t>(DEF_TEXT_LARGE) : static_cast<uint8_t>(DEF_TEXT_SMALL);
  } else if (storedSize >= 0 && storedSize < DEF_TEXT_SIZE_COUNT) {
    definitionTextSize = static_cast<uint8_t>(storedSize);
  }
}

bool DictionaryStore::saveConfig() const {
  Storage.mkdir("/.crosspoint");
  JsonDocument doc;
  doc["activeIfoPath"] = activeIfoPath;
  doc["definitionTextSize"] = definitionTextSize;
  doc["definitionTextSizeVersion"] = DEFINITION_TEXT_SIZE_CONFIG_VERSION;
  const std::string tempPath = std::string(CONFIG_PATH) + ".tmp";
  Storage.remove(tempPath.c_str());

  HalFile file;
  if (!Storage.openFileForWrite("DICT", tempPath, file)) return false;
  serializeJson(doc, file);
  file.flush();
  file.close();

  Storage.remove(CONFIG_PATH);
  if (!Storage.rename(tempPath.c_str(), CONFIG_PATH)) {
    Storage.remove(tempPath.c_str());
    return false;
  }
  return true;
}

void DictionaryStore::clearActiveOnlyEntry() {
  activeOnlyEntry = DictionaryEntry{};
  activeOnlyLoaded = false;
}

bool DictionaryStore::loadEntryFromIfoPath(const std::string& ifoPath, DictionaryEntry& entry) const {
  if (!hasExtension(ifoPath, ".ifo")) return false;

  entry = DictionaryEntry{};
  entry.ifoPath = ifoPath;
  entry.directoryPath = parentDirectory(ifoPath);
  entry.languageId = limitMetadataText(pathBaseName(entry.directoryPath), 48);
  entry.name = limitMetadataText(fileStem(entry.ifoPath));

  const std::string base = entry.ifoPath.substr(0, entry.ifoPath.size() - 4);
  entry.idxPath = base + ".idx";
  entry.dictPath = base + ".dict";
  entry.synPath = base + ".syn";
  entry.cachePath = base + ".cpridx";

  HalFile ifo;
  if (Storage.openFileForRead("DICT", entry.ifoPath, ifo)) {
    char line[256];
    while (readLine(ifo, line, sizeof(line))) {
      std::string value;
      if (parseIfoField(line, "bookname", value)) {
        entry.name = limitMetadataText(std::move(value));
      } else if (parseIfoField(line, "lang", value)) {
        entry.lang = limitMetadataText(std::move(value), 32);
      } else if (parseIfoField(line, "wordcount", value)) {
        entry.wordCount = static_cast<uint32_t>(strtoul(value.c_str(), nullptr, 10));
      } else if (parseIfoField(line, "idxfilesize", value)) {
        entry.idxFileSize = static_cast<uint32_t>(strtoul(value.c_str(), nullptr, 10));
      } else if (parseIfoField(line, "sametypesequence", value)) {
        entry.sameTypeSequence = limitMetadataText(std::move(value), 16);
      }
    }
    ifo.close();
  }

  entry.missingFiles = !Storage.exists(entry.idxPath.c_str()) || !Storage.exists(entry.dictPath.c_str());
  if (!Storage.exists(entry.dictPath.c_str()) && Storage.exists((entry.dictPath + ".dz").c_str())) {
    entry.compressed = true;
  }
  if (!Storage.exists(entry.synPath.c_str())) {
    entry.synPath.clear();
  }

  if (entry.idxFileSize == 0 && Storage.exists(entry.idxPath.c_str())) {
    HalFile idx;
    if (Storage.openFileForRead("DICT", entry.idxPath, idx)) {
      entry.idxFileSize = static_cast<uint32_t>(idx.fileSize());
      idx.close();
    }
  }

  return !entry.ifoPath.empty();
}

void DictionaryStore::scan() {
  if (!configLoaded) loadConfig();
  clearActiveOnlyEntry();
  std::vector<DictionaryEntry>().swap(entries);
  activeIndex = -1;
  scanned = true;

  bool scanLimitReached = false;
  auto containsIfo = [this](const std::string& ifoPath) {
    return std::find_if(entries.begin(), entries.end(),
                        [&ifoPath](const DictionaryEntry& entry) { return entry.ifoPath == ifoPath; }) != entries.end();
  };

  auto ensureEntryCapacity = [this](const size_t desiredSize) {
    if (desiredSize <= entries.capacity()) return true;
    size_t nextCapacity = entries.capacity() == 0 ? 8 : entries.capacity() * 2;
    nextCapacity = std::min<size_t>(nextCapacity, MAX_SCAN_ENTRIES);
    nextCapacity = std::max(nextCapacity, desiredSize);
    const size_t bytes = nextCapacity * sizeof(DictionaryEntry);
    if (!hasHeapHeadroom(bytes, HEAP_SCAN_GUARD_BYTES)) {
      LOG_INF("DICT", "Skipping remaining dictionaries: heap too low for list growth free=%u largest=%u need=%u",
              ESP.getFreeHeap(), largestFreeBlock(), static_cast<unsigned>(bytes));
      return false;
    }
    entries.reserve(nextCapacity);
    return true;
  };

  auto appendEntry = [&](DictionaryEntry&& entry) {
    if (entries.size() >= MAX_SCAN_ENTRIES) {
      scanLimitReached = true;
      return false;
    }
    if (!ensureEntryCapacity(entries.size() + 1)) {
      scanLimitReached = true;
      return false;
    }
    entries.push_back(std::move(entry));
    return true;
  };

  if (!activeIfoPath.empty()) {
    DictionaryEntry activeEntry;
    if (loadEntryFromIfoPath(activeIfoPath, activeEntry)) {
      appendEntry(std::move(activeEntry));
    }
  }

  char resolvedRoot[32];
  const char* dictionaryRoot =
      FsHelpers::resolveRootDirectoryIgnoreCase(DICTIONARY_ROOT, resolvedRoot, sizeof(resolvedRoot)) ? resolvedRoot
                                                                                                     : DICTIONARY_ROOT;
  auto root = Storage.open(dictionaryRoot);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    if (!entries.empty() && entries[0].ifoPath == activeIfoPath) activeIndex = 0;
    return;
  }

  root.rewindDirectory();
  char name[128];
  for (auto child = root.openNextFile(); child; child = root.openNextFile()) {
    child.getName(name, sizeof(name));
    if (!child.isDirectory() || name[0] == '.') {
      child.close();
      continue;
    }

    const std::string languageId = name;
    const std::string dirPath = joinPath(dictionaryRoot, languageId);
    child.close();

    auto dir = Storage.open(dirPath.c_str());
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      continue;
    }

    dir.rewindDirectory();
    char fileName[192];
    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
      file.getName(fileName, sizeof(fileName));
      file.close();
      const std::string ifoName = fileName;
      if (!hasExtension(ifoName, ".ifo")) continue;

      DictionaryEntry entry;
      const std::string ifoPath = joinPath(dirPath, ifoName);
      if (containsIfo(ifoPath) || !loadEntryFromIfoPath(ifoPath, entry)) continue;
      entry.languageId = limitMetadataText(languageId, 48);
      if (!appendEntry(std::move(entry))) break;
    }
    dir.close();
    if (scanLimitReached) break;
  }
  root.close();

  std::sort(entries.begin(), entries.end(), [](const DictionaryEntry& a, const DictionaryEntry& b) {
    if (a.languageId != b.languageId) return a.languageId < b.languageId;
    return a.name < b.name;
  });

  for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
    if (entries[i].ifoPath == activeIfoPath) {
      activeIndex = i;
      break;
    }
  }

  if (activeIndex < 0 && entries.size() == 1 && !entries[0].compressed && !entries[0].missingFiles) {
    activeIndex = 0;
    activeIfoPath = entries[0].ifoPath;
    saveConfig();
  }
}

bool DictionaryStore::setActiveIndex(const int index) {
  if (!scanned) scan();
  if (index < 0 || index >= static_cast<int>(entries.size())) return false;
  if (entries[index].compressed || entries[index].missingFiles) return false;
  activeIndex = index;
  activeIfoPath = entries[index].ifoPath;
  clearActiveOnlyEntry();
  return saveConfig();
}

std::string DictionaryStore::getActiveLabel() const {
  const DictionaryEntry* entry = activeEntry();
  if (!entry) return "";
  return entry->languageId + " - " + entry->name;
}

bool DictionaryStore::setDefinitionTextSize(const uint8_t size) {
  if (size >= DEF_TEXT_SIZE_COUNT) return false;
  definitionTextSize = size;
  return saveConfig();
}

int DictionaryStore::getDefinitionFontId(const int readerFontId) const {
  if (!configLoaded) {
    const_cast<DictionaryStore*>(this)->loadConfig();
  }

  const bool useReaderFont = SETTINGS.sdFontFamilyName[0] != '\0';
  return DictionaryFontSelection::definitionFontId(readerFontId, useReaderFont, definitionTextSize);
}

bool DictionaryStore::hasActiveDictionary() const {
  const_cast<DictionaryStore*>(this)->ensureActiveEntryLoaded();
  return activeEntry() != nullptr;
}

void DictionaryStore::ensureScanned() {
  if (!scanned) scan();
}

bool DictionaryStore::ensureActiveEntryLoaded() {
  if (!configLoaded) loadConfig();
  if (activeEntry()) return true;

  if (activeIfoPath.empty()) {
    if (!scanned) scan();
    return activeEntry() != nullptr;
  }

  if (!activeOnlyLoaded || activeOnlyEntry.ifoPath != activeIfoPath) {
    clearActiveOnlyEntry();
    if (!loadEntryFromIfoPath(activeIfoPath, activeOnlyEntry)) return false;
    activeOnlyLoaded = true;
  }
  return activeEntry() != nullptr;
}

DictionaryEntry* DictionaryStore::activeEntry() {
  if (activeIndex >= 0 && activeIndex < static_cast<int>(entries.size())) return &entries[activeIndex];
  if (activeOnlyLoaded) return &activeOnlyEntry;
  return nullptr;
}

const DictionaryEntry* DictionaryStore::activeEntry() const {
  if (activeIndex >= 0 && activeIndex < static_cast<int>(entries.size())) return &entries[activeIndex];
  if (activeOnlyLoaded) return &activeOnlyEntry;
  return nullptr;
}

bool DictionaryStore::prepareActive(const std::function<void(int percent)>& onProgress) {
  ensureActiveEntryLoaded();
  DictionaryEntry* entry = activeEntry();
  if (!entry) return false;
  return ensurePrepared(*entry, onProgress);
}

bool DictionaryStore::loadCheckpointCache(DictionaryEntry& entry) {
  entry.checkpoints.clear();
  entry.ordinals.clear();
  entry.totalWords = 0;

  HalFile file;
  if (!Storage.openFileForRead("DICT", entry.cachePath, file)) return false;
  uint8_t header[16];
  if (file.read(header, sizeof(header)) != static_cast<int>(sizeof(header))) {
    file.close();
    return false;
  }

  if (readU32(header) != CACHE_MAGIC || readU32(header + 4) != entry.idxFileSize) {
    file.close();
    return false;
  }

  const uint32_t count = readU32(header + 8);
  entry.totalWords = readU32(header + 12);
  if (count == 0 || count > MAX_CHECKPOINT_COUNT ||
      !hasHeapHeadroom(static_cast<size_t>(count) * sizeof(uint32_t) * 2, HEAP_SMALL_GUARD_BYTES)) {
    file.close();
    return false;
  }

  entry.checkpoints.reserve(count);
  entry.ordinals.reserve(count);
  uint8_t buf[4];
  for (uint32_t i = 0; i < count; ++i) {
    if (file.read(buf, 4) != 4) {
      file.close();
      entry.checkpoints.clear();
      entry.ordinals.clear();
      return false;
    }
    entry.checkpoints.push_back(readU32(buf));
  }
  for (uint32_t i = 0; i < count; ++i) {
    if (file.read(buf, 4) != 4) {
      file.close();
      entry.checkpoints.clear();
      entry.ordinals.clear();
      return false;
    }
    entry.ordinals.push_back(readU32(buf));
  }
  file.close();
  return entry.checkpoints.size() == entry.ordinals.size();
}

bool DictionaryStore::saveCheckpointCache(const DictionaryEntry& entry) const {
  if (entry.checkpoints.empty() || entry.checkpoints.size() != entry.ordinals.size()) return false;
  const std::string tempPath = entry.cachePath + ".tmp";
  Storage.remove(tempPath.c_str());

  HalFile file;
  if (!Storage.openFileForWrite("DICT", tempPath, file)) return false;

  uint8_t header[16];
  writeU32(header, CACHE_MAGIC);
  writeU32(header + 4, entry.idxFileSize);
  writeU32(header + 8, static_cast<uint32_t>(entry.checkpoints.size()));
  writeU32(header + 12, entry.totalWords);
  if (file.write(header, sizeof(header)) != sizeof(header)) {
    file.close();
    Storage.remove(tempPath.c_str());
    return false;
  }

  uint8_t buf[4];
  for (uint32_t value : entry.checkpoints) {
    writeU32(buf, value);
    if (file.write(buf, 4) != 4) {
      file.close();
      Storage.remove(tempPath.c_str());
      return false;
    }
  }
  for (uint32_t value : entry.ordinals) {
    writeU32(buf, value);
    if (file.write(buf, 4) != 4) {
      file.close();
      Storage.remove(tempPath.c_str());
      return false;
    }
  }

  file.flush();
  file.close();
  Storage.remove(entry.cachePath.c_str());
  if (!Storage.rename(tempPath.c_str(), entry.cachePath.c_str())) {
    Storage.remove(tempPath.c_str());
    return false;
  }
  return true;
}

bool DictionaryStore::ensurePrepared(DictionaryEntry& entry, const std::function<void(int percent)>& onProgress) {
  if (entry.compressed || entry.missingFiles) return false;
  if (!entry.checkpoints.empty() && !entry.ordinals.empty()) return true;
  if (loadCheckpointCache(entry)) return true;

  HalFile idx;
  if (!Storage.openFileForRead("DICT", entry.idxPath, idx)) return false;
  entry.idxFileSize = static_cast<uint32_t>(idx.fileSize());
  entry.checkpoints.clear();
  entry.ordinals.clear();
  entry.totalWords = 0;

  uint32_t pos = 0;
  int lastProgress = -1;
  bool checkpointLimitReached = false;
  auto addCheckpoint = [&](const uint32_t checkpointPos, const uint32_t ordinal) {
    if (checkpointLimitReached) return true;
    if (entry.checkpoints.size() >= MAX_CHECKPOINT_COUNT) {
      checkpointLimitReached = true;
      return true;
    }
    if (entry.checkpoints.size() == entry.checkpoints.capacity()) {
      const size_t nextCapacity =
          std::min<size_t>(MAX_CHECKPOINT_COUNT, entry.checkpoints.empty() ? 128 : entry.checkpoints.capacity() * 2);
      const size_t bytes = nextCapacity * sizeof(uint32_t) * 2;
      if (!hasHeapHeadroom(bytes, HEAP_SMALL_GUARD_BYTES)) {
        if (entry.checkpoints.empty()) return false;
        checkpointLimitReached = true;
        return true;
      }
      entry.checkpoints.reserve(nextCapacity);
      entry.ordinals.reserve(nextCapacity);
    }
    entry.checkpoints.push_back(checkpointPos);
    entry.ordinals.push_back(ordinal);
    return true;
  };

  while (pos < entry.idxFileSize) {
    if (entry.totalWords % CHECKPOINT_INTERVAL == 0) {
      if (!addCheckpoint(pos, entry.totalWords)) {
        idx.close();
        entry.checkpoints.clear();
        entry.ordinals.clear();
        entry.totalWords = 0;
        return false;
      }
    }

    int c = 0;
    do {
      c = idx.read();
      if (c < 0) {
        pos = entry.idxFileSize;
        break;
      }
      ++pos;
    } while (c != 0);
    if (pos >= entry.idxFileSize) break;

    uint8_t skip[8];
    if (idx.read(skip, sizeof(skip)) != static_cast<int>(sizeof(skip))) break;
    pos += sizeof(skip);
    ++entry.totalWords;

    if (onProgress && entry.idxFileSize > 0) {
      const int progress = static_cast<int>((static_cast<uint64_t>(pos) * 100ULL) / entry.idxFileSize);
      if (progress >= lastProgress + 5) {
        lastProgress = progress;
        onProgress(progress);
      }
    }
  }
  idx.close();

  if (entry.checkpoints.empty()) return false;
  saveCheckpointCache(entry);
  return true;
}

bool DictionaryStore::findIndexHit(const DictionaryEntry& entry, const std::string& word, IndexHit& hit) const {
  if (entry.checkpoints.empty()) return false;

  HalFile idx;
  if (!Storage.openFileForRead("DICT", entry.idxPath, idx)) return false;

  int lo = 0;
  int hi = static_cast<int>(entry.checkpoints.size()) - 1;
  while (lo < hi) {
    const int mid = lo + (hi - lo + 1) / 2;
    if (!idx.seekSet(entry.checkpoints[mid])) break;
    const std::string key = readIndexWord(idx);
    if (compareWords(key, word) <= 0) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }

  if (!idx.seekSet(entry.checkpoints[lo])) {
    idx.close();
    return false;
  }

  uint32_t maxEntries = CHECKPOINT_INTERVAL;
  if (lo + 1 < static_cast<int>(entry.ordinals.size())) {
    maxEntries = entry.ordinals[lo + 1] - entry.ordinals[lo];
  } else if (entry.totalWords > entry.ordinals[lo]) {
    maxEntries = entry.totalWords - entry.ordinals[lo];
  }

  for (uint32_t i = 0; i < maxEntries; ++i) {
    const std::string key = readIndexWord(idx);
    if (key.empty()) break;
    uint8_t buf[8];
    if (idx.read(buf, sizeof(buf)) != static_cast<int>(sizeof(buf))) break;
    const int cmp = compareWords(key, word);
    if (cmp == 0) {
      hit.headword = key;
      hit.dictOffset = readBE32(buf);
      hit.dictSize = readBE32(buf + 4);
      idx.close();
      return true;
    }
    if (cmp > 0) break;
  }

  idx.close();
  return false;
}

std::string DictionaryStore::headwordAtOrdinal(const DictionaryEntry& entry, uint32_t ordinal) const {
  if (entry.checkpoints.empty() || entry.checkpoints.size() != entry.ordinals.size()) return "";
  size_t lo = 0;
  size_t hi = entry.ordinals.size();
  while (lo + 1 < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    if (entry.ordinals[mid] <= ordinal) {
      lo = mid;
    } else {
      hi = mid;
    }
  }

  HalFile idx;
  if (!Storage.openFileForRead("DICT", entry.idxPath, idx)) return "";
  if (!idx.seekSet(entry.checkpoints[lo])) {
    idx.close();
    return "";
  }

  const uint32_t skipCount = ordinal - entry.ordinals[lo];
  uint8_t skip[8];
  for (uint32_t i = 0; i < skipCount; ++i) {
    readIndexWord(idx);
    if (idx.read(skip, sizeof(skip)) != static_cast<int>(sizeof(skip))) {
      idx.close();
      return "";
    }
  }

  const std::string headword = readIndexWord(idx);
  idx.close();
  return headword;
}

bool DictionaryStore::lookupSynonym(const DictionaryEntry& entry, const std::string& word,
                                    std::string& canonical) const {
  if (entry.synPath.empty()) return false;
  HalFile syn;
  if (!Storage.openFileForRead("DICT", entry.synPath, syn)) return false;

  const size_t fileSize = syn.fileSize();
  size_t lo = 0;
  size_t hi = fileSize;
  bool found = false;
  uint32_t ordinal = 0;

  while (hi > lo && hi - lo > 1024) {
    const size_t mid = lo + (hi - lo) / 2;
    const size_t scanFrom = mid > 260 ? mid - 260 : 0;
    if (!syn.seekSet(scanFrom)) break;

    int guard = 260;
    while (guard-- > 0) {
      const int c = syn.read();
      if (c < 0) goto linearSyn;
      if (c == 0) break;
    }
    if (!syn.seekCur(4)) break;

    const size_t entryStart = syn.position();
    if (entryStart >= hi) {
      hi = mid;
      continue;
    }

    const std::string key = readIndexWord(syn);
    if (key.empty()) goto linearSyn;
    uint8_t raw[4];
    if (syn.read(raw, 4) != 4) goto linearSyn;
    const int cmp = compareWords(key, word);
    if (cmp == 0) {
      ordinal = readBE32(raw);
      found = true;
      break;
    }
    if (cmp < 0) {
      lo = syn.position();
    } else {
      hi = entryStart;
    }
  }

linearSyn:
  if (!found) {
    if (!syn.seekSet(lo)) {
      syn.close();
      return false;
    }
    while (syn.position() < hi) {
      const std::string key = readIndexWord(syn);
      if (key.empty() && syn.position() >= hi) break;
      uint8_t raw[4];
      if (syn.read(raw, 4) != 4) break;
      const int cmp = compareWords(key, word);
      if (cmp == 0) {
        ordinal = readBE32(raw);
        found = true;
        break;
      }
    }
  }

  syn.close();
  if (!found) return false;
  canonical = headwordAtOrdinal(entry, ordinal);
  return !canonical.empty();
}

std::string DictionaryStore::readDefinition(const DictionaryEntry& entry, const IndexHit& hit, bool& truncated) const {
  const size_t readBytes = safeDefinitionReadBytes(hit.dictSize);
  truncated = readBytes == 0 || hit.dictSize > readBytes;
  if (readBytes == 0) return "";

  HalFile dict;
  if (!Storage.openFileForRead("DICT", entry.dictPath, dict)) return "";
  if (!dict.seekSet(hit.dictOffset)) {
    dict.close();
    return "";
  }

  std::string raw(readBytes, '\0');
  const int bytesRead = dict.read(raw.data(), readBytes);
  dict.close();
  if (bytesRead <= 0) return "";
  raw.resize(static_cast<size_t>(bytesRead));

  std::string decoded = decodeStarDictData(raw, entry.sameTypeSequence);
  raw.clear();
  std::string().swap(raw);
  if (decoded.size() > MAX_DEFINITION_BYTES) {
    decoded.resize(MAX_DEFINITION_BYTES);
    while (!decoded.empty() && (static_cast<unsigned char>(decoded.back()) & 0xC0) == 0x80) decoded.pop_back();
    truncated = true;
  }
  return stripHtmlAndEntities(decoded);
}

std::vector<std::string> DictionaryStore::getFallbackForms(const DictionaryEntry& entry,
                                                           const std::string& word) const {
  std::vector<std::string> forms;
  const std::string lower = lowercaseLatinUtf8(word);
  if (lower != word) forms.push_back(lower);

  auto add = [&forms](std::string value) {
    if (value.size() < 3) return;
    if (std::find(forms.begin(), forms.end(), value) == forms.end()) forms.push_back(std::move(value));
  };

  auto endsWith = [&lower](const char* suffix) {
    const size_t len = strlen(suffix);
    return lower.size() >= len && lower.compare(lower.size() - len, len, suffix) == 0;
  };

  if (lower.size() > 4 && endsWith("es")) add(lower.substr(0, lower.size() - 2));
  if (lower.size() > 3 && endsWith("s")) add(lower.substr(0, lower.size() - 1));

  if (isSpanishLanguage(entry)) {
    struct SpanishInfinitivePronoun {
      const char* suffix;
      size_t pronounLen;
    };

    static constexpr SpanishInfinitivePronoun PRONOUNS[] = {
        {"arme", 2},  {"arte", 2}, {"arse", 2}, {"arnos", 3}, {"aros", 2}, {"erme", 2},  {"erte", 2}, {"erse", 2},
        {"ernos", 3}, {"eros", 2}, {"irme", 2}, {"irte", 2},  {"irse", 2}, {"irnos", 3}, {"iros", 2},
    };

    for (const auto& form : PRONOUNS) {
      if (endsWith(form.suffix) && lower.size() > strlen(form.suffix) + 2) {
        add(lower.substr(0, lower.size() - form.pronounLen));
      }
    }

    auto addGerundFallback = [&](const char* suffix, const char* infinitiveEnding) {
      const size_t suffixLen = strlen(suffix);
      if (lower.size() > suffixLen + 2 && lower.compare(lower.size() - suffixLen, suffixLen, suffix) == 0) {
        add(lower.substr(0, lower.size() - suffixLen) + infinitiveEnding);
      }
    };

    addGerundFallback("andose", "ar");
    addGerundFallback("\xC3\xA1ndose", "ar");
    addGerundFallback("iendose", "er");
    addGerundFallback("iendose", "ir");
    addGerundFallback("i\xC3\xA9ndose", "er");
    addGerundFallback("i\xC3\xA9ndose", "ir");
  }

  if (isEnglishLanguage(entry)) {
    if (lower.size() > 5 && endsWith("ing")) {
      add(lower.substr(0, lower.size() - 3));
      add(lower.substr(0, lower.size() - 3) + "e");
    }
    if (lower.size() > 4 && endsWith("ed")) {
      add(lower.substr(0, lower.size() - 2));
      add(lower.substr(0, lower.size() - 1));
    }
    if (lower.size() > 5 && endsWith("ies")) add(lower.substr(0, lower.size() - 3) + "y");
  }

  return forms;
}

DictionaryLookupResult DictionaryStore::lookup(const std::string& rawWord, const bool includeSuggestions) {
  DictionaryLookupResult result;
  ensureActiveEntryLoaded();
  result.query = cleanWord(rawWord);
  if (result.query.empty()) {
    result.status = DictionaryLookupResult::Status::NotFound;
    return result;
  }

  DictionaryEntry* entry = activeEntry();
  if (!entry) {
    result.status = DictionaryLookupResult::Status::NoDictionary;
    return result;
  }
  result.dictionaryName = entry->name;

  if (!ensurePrepared(*entry)) {
    result.status = DictionaryLookupResult::Status::NotReady;
    return result;
  }

  auto finishFound = [&](const IndexHit& hit) {
    result.status = DictionaryLookupResult::Status::Found;
    result.headword = hit.headword;
    result.definition = readDefinition(*entry, hit, result.truncated);
    addHistory(result.headword.empty() ? result.query : result.headword);
  };

  IndexHit hit;
  if (findIndexHit(*entry, result.query, hit)) {
    finishFound(hit);
    return result;
  }

  std::string canonical;
  if (lookupSynonym(*entry, result.query, canonical) && findIndexHit(*entry, canonical, hit)) {
    finishFound(hit);
    return result;
  }

  for (const std::string& fallback : getFallbackForms(*entry, result.query)) {
    if (findIndexHit(*entry, fallback, hit)) {
      finishFound(hit);
      return result;
    }
    if (lookupSynonym(*entry, fallback, canonical) && findIndexHit(*entry, canonical, hit)) {
      finishFound(hit);
      return result;
    }
  }

  result.status = DictionaryLookupResult::Status::NotFound;
  if (includeSuggestions) {
    result.suggestions = findSuggestions(*entry, result.query, 8);
  }
  return result;
}

std::vector<std::string> DictionaryStore::findSuggestions(const DictionaryEntry& entry, const std::string& word,
                                                          const int maxResults) const {
  std::vector<std::string> results;
  if (entry.checkpoints.empty()) return results;

  HalFile idx;
  if (!Storage.openFileForRead("DICT", entry.idxPath, idx)) return results;

  int lo = 0;
  int hi = static_cast<int>(entry.checkpoints.size()) - 1;
  while (lo < hi) {
    const int mid = lo + (hi - lo + 1) / 2;
    idx.seekSet(entry.checkpoints[mid]);
    const std::string key = readIndexWord(idx);
    if (compareWords(key, word) <= 0) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }

  const int startSegment = std::max(0, lo - 2);
  const int endSegment = std::min(static_cast<int>(entry.checkpoints.size()) - 1, lo + 2);
  idx.seekSet(entry.checkpoints[startSegment]);

  uint32_t totalToScan = (endSegment - startSegment + 1) * CHECKPOINT_INTERVAL;
  if (entry.totalWords > entry.ordinals[startSegment]) {
    totalToScan = std::min<uint32_t>(totalToScan, entry.totalWords - entry.ordinals[startSegment]);
  }
  const std::string scoreWord = suggestionScoreKey(word);
  const int maxDistance = std::max(2, static_cast<int>(scoreWord.size()) / 3 + 1);

  struct Candidate {
    std::string word;
    int score;
    int commonPrefix;
    int lengthDelta;
  };
  std::vector<Candidate> candidates;
  if (!hasHeapHeadroom(MAX_SUGGESTION_CANDIDATES * sizeof(Candidate), HEAP_SMALL_GUARD_BYTES)) return results;
  candidates.reserve(MAX_SUGGESTION_CANDIDATES);
  auto candidateBetter = [](const Candidate& a, const Candidate& b) {
    if (a.score != b.score) return a.score < b.score;
    if (a.commonPrefix != b.commonPrefix) return a.commonPrefix > b.commonPrefix;
    if (a.lengthDelta != b.lengthDelta) return a.lengthDelta < b.lengthDelta;
    return a.word.size() < b.word.size();
  };
  auto addCandidate = [&](const std::string& key, const int score, const int commonPrefix, const int lengthDelta) {
    if (key.empty() || compareWords(key, word) == 0) return;
    Candidate candidate{key, score, commonPrefix, lengthDelta};
    if (candidates.size() < MAX_SUGGESTION_CANDIDATES) {
      candidates.push_back(std::move(candidate));
      return;
    }
    auto worst = std::max_element(candidates.begin(), candidates.end(), candidateBetter);
    if (worst != candidates.end() && candidateBetter(candidate, *worst)) {
      *worst = std::move(candidate);
    }
  };

  for (const std::string& fallback : getFallbackForms(entry, word)) {
    IndexHit hit;
    std::string canonical;
    if (findIndexHit(entry, fallback, hit)) {
      const std::string scoreKey = suggestionScoreKey(hit.headword);
      addCandidate(hit.headword, 0, commonPrefixLength(scoreKey, scoreWord),
                   std::abs(static_cast<int>(scoreKey.size()) - static_cast<int>(scoreWord.size())));
    } else if (lookupSynonym(entry, fallback, canonical) && findIndexHit(entry, canonical, hit)) {
      const std::string scoreKey = suggestionScoreKey(hit.headword);
      addCandidate(hit.headword, 0, commonPrefixLength(scoreKey, scoreWord),
                   std::abs(static_cast<int>(scoreKey.size()) - static_cast<int>(scoreWord.size())));
    }
  }

  for (uint32_t i = 0; i < totalToScan; ++i) {
    const std::string key = readIndexWord(idx);
    if (key.empty()) break;
    uint8_t skip[8];
    if (idx.read(skip, sizeof(skip)) != static_cast<int>(sizeof(skip))) break;
    if (compareWords(key, word) == 0) continue;

    const std::string scoreKey = suggestionScoreKey(key);
    const int score = editDistanceLimited(scoreKey, scoreWord, maxDistance);
    if (score <= maxDistance) {
      addCandidate(key, score, commonPrefixLength(scoreKey, scoreWord),
                   std::abs(static_cast<int>(scoreKey.size()) - static_cast<int>(scoreWord.size())));
    }
  }
  idx.close();

  std::sort(candidates.begin(), candidates.end(), candidateBetter);

  for (const Candidate& candidate : candidates) {
    if (std::find(results.begin(), results.end(), candidate.word) == results.end()) {
      results.push_back(candidate.word);
      if (static_cast<int>(results.size()) >= maxResults) break;
    }
  }
  return results;
}

std::string DictionaryStore::cleanWord(const std::string& word) {
  if (word.empty()) return "";

  size_t start = 0;
  while (start < word.size()) {
    const unsigned char c = static_cast<unsigned char>(word[start]);
    if (c < 0x80 && isAsciiTrimChar(c)) {
      ++start;
      continue;
    }
    size_t tokenLen = 0;
    if (isUtf8TrimPrefix(word, start, tokenLen)) {
      start += tokenLen;
      continue;
    }
    break;
  }

  size_t end = word.size();
  while (end > start) {
    const unsigned char c = static_cast<unsigned char>(word[end - 1]);
    if (c < 0x80 && isAsciiTrimChar(c)) {
      --end;
      continue;
    }
    size_t tokenLen = 0;
    if (isUtf8TrimSuffix(word, end, tokenLen)) {
      end -= tokenLen;
      continue;
    }
    break;
  }
  if (start >= end) return "";

  std::string out = word.substr(start, end - start);
  for (size_t i = 0; i + 1 < out.size();) {
    if (static_cast<unsigned char>(out[i]) == 0xC2 && static_cast<unsigned char>(out[i + 1]) == 0xAD) {
      out.erase(i, 2);
    } else {
      ++i;
    }
  }
  return lowercaseLatinUtf8(out);
}

std::vector<std::string> DictionaryStore::getHistory() {
  std::vector<std::string> history;
  HalFile file;
  if (!Storage.openFileForRead("DICT", HISTORY_PATH, file)) return history;

  std::string line;
  while (true) {
    const int c = file.read();
    if (c < 0) break;
    if (c == '\r') continue;
    if (c == '\n') {
      if (!line.empty()) {
        history.push_back(line);
        if (history.size() >= MAX_HISTORY_ITEMS) break;
        line.clear();
      }
      continue;
    }
    if (line.size() < 128) line.push_back(static_cast<char>(c));
  }
  if (!line.empty() && history.size() < MAX_HISTORY_ITEMS) history.push_back(line);
  file.close();
  return history;
}

void DictionaryStore::addHistory(const std::string& word) {
  const std::string cleaned = cleanWord(word);
  if (cleaned.empty()) return;

  auto history = getHistory();
  history.erase(std::remove(history.begin(), history.end(), cleaned), history.end());
  history.insert(history.begin(), cleaned);
  if (history.size() > MAX_HISTORY_ITEMS) history.resize(MAX_HISTORY_ITEMS);

  Storage.mkdir("/.crosspoint");
  const std::string tempPath = std::string(HISTORY_PATH) + ".tmp";
  Storage.remove(tempPath.c_str());
  HalFile file;
  if (!Storage.openFileForWrite("DICT", tempPath, file)) return;
  for (const std::string& item : history) {
    file.write(item.c_str(), item.size());
    file.write(static_cast<uint8_t>('\n'));
  }
  file.flush();
  file.close();
  Storage.remove(HISTORY_PATH);
  if (!Storage.rename(tempPath.c_str(), HISTORY_PATH)) {
    Storage.remove(tempPath.c_str());
  }
}

void DictionaryStore::clearHistory() { Storage.remove(HISTORY_PATH); }
