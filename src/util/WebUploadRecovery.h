#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string_view>

#include "WebPath.h"

// A single journal is sufficient because all web upload transports serialize
// transactions. It is committed and read back before moving the old book.
// Recovery restores the previously committed file; it never publishes a staged
// upload after an interruption. Storage is injected for host fault testing.
namespace WebUploadRecovery {
inline constexpr char JOURNAL[] = "/.crosspoint/web-upload.journal";
inline constexpr char PENDING[] = "/.crosspoint/web-upload.journal.pending";
inline constexpr size_t MAX_PATH_BYTES = 4096;
inline constexpr size_t HEADER_BYTES = 24;

struct Record {
  std::unique_ptr<char[]> data;
  const char* target = nullptr;
  const char* temporary = nullptr;
  const char* previous = nullptr;
  bool hadTarget = false;
};

inline uint32_t crc32(const void* bytes, size_t length, uint32_t value = 0xFFFFFFFFu) {
  const auto* data = static_cast<const uint8_t*>(bytes);
  while (length--) {
    value ^= *data++;
    for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ (0xEDB88320u & (0u - (value & 1u)));
  }
  return value;
}

inline uint32_t read32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

inline void write32(uint8_t* data, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) data[i] = static_cast<uint8_t>(value >> (i * 8));
}

inline bool validPaths(std::string_view target, std::string_view temporary, std::string_view previous) {
  if (target == "/" || WebPath::isProtected(target)) return false;
  const auto parent = target.substr(0, target.rfind('/') + 1);
  constexpr std::string_view prefix = ".cpr-upload-";
  constexpr std::string_view partialSuffix = ".partial";
  constexpr std::string_view previousSuffix = ".previous";
  const size_t commonLength = parent.size() + prefix.size() + 64;
  if (temporary.size() != commonLength + partialSuffix.size() ||
      previous.size() != commonLength + previousSuffix.size() || temporary.substr(0, parent.size()) != parent ||
      previous.substr(0, parent.size()) != parent || temporary.substr(parent.size(), prefix.size()) != prefix ||
      previous.substr(0, commonLength) != temporary.substr(0, commonLength) ||
      temporary.substr(commonLength) != partialSuffix || previous.substr(commonLength) != previousSuffix)
    return false;
  for (const char digit : temporary.substr(parent.size() + prefix.size(), 64)) {
    if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f'))) return false;
  }
  return true;
}

template <typename FileSystem>
bool load(FileSystem& fs, const char* path, Record& record) {
  auto file = fs.open(path);
  if (!file) return false;
  uint8_t header[HEADER_BYTES];
  if (file.isDirectory() || file.read(header, sizeof(header)) != static_cast<int>(sizeof(header)) ||
      std::memcmp(header, "CPRU", 4) != 0 || header[4] != 1 || header[5] > 1 || header[6] != 0 || header[7] != 0) {
    file.close();
    return false;
  }
  const uint32_t targetBytes = read32(header + 8);
  const uint32_t temporaryBytes = read32(header + 12);
  const uint32_t previousBytes = read32(header + 16);
  if (!targetBytes || !temporaryBytes || !previousBytes || targetBytes > MAX_PATH_BYTES + 1 ||
      temporaryBytes > MAX_PATH_BYTES + 1 || previousBytes > MAX_PATH_BYTES + 1) {
    file.close();
    return false;
  }
  const size_t payloadBytes = targetBytes + temporaryBytes + previousBytes;
  if (file.fileSize() != HEADER_BYTES + payloadBytes) {
    file.close();
    return false;
  }
  // One bounded, fallible allocation per recovery; no large stack arrays.
  record.data.reset(new (std::nothrow) char[payloadBytes]);
  if (!record.data) {
    file.close();
    return false;
  }
  const bool read = file.read(record.data.get(), payloadBytes) == static_cast<int>(payloadBytes);
  const bool closed = file.close();
  if (!read || !closed || crc32(record.data.get(), payloadBytes, crc32(header, 20)) != read32(header + 20))
    return false;
  record.target = record.data.get();
  record.temporary = record.target + targetBytes;
  record.previous = record.temporary + temporaryBytes;
  if (record.target[targetBytes - 1] || record.temporary[temporaryBytes - 1] || record.previous[previousBytes - 1] ||
      std::strlen(record.target) != targetBytes - 1 || std::strlen(record.temporary) != temporaryBytes - 1 ||
      std::strlen(record.previous) != previousBytes - 1)
    return false;
  record.hadTarget = header[5] != 0;
  return validPaths(record.target, record.temporary, record.previous);
}

template <typename FileSystem>
bool recover(FileSystem& fs) {
  if (!fs.exists(JOURNAL)) return true;
  Record record;
  if (!load(fs, JOURNAL, record)) return false;
  if (!fs.exists(record.target) && record.hadTarget) {
    if (fs.exists(record.previous)) {
      if (!fs.rename(record.previous, record.target)) return false;
    } else {
      // Do not erase the evidence or select the uncommitted new upload.
      return false;
    }
  }
  if (fs.exists(record.target)) {
    auto current = fs.open(record.target);
    if (!current) return false;
    const bool directory = current.isDirectory();
    const bool closed = current.close();
    if (directory || !closed) return false;
  }
  // Keep both the previous backup and staged file. Clearing only the journal
  // makes this operation idempotent and cannot delete the last good book.
  return fs.remove(JOURNAL);
}

template <typename FileSystem>
bool prepare(FileSystem& fs, const char* target, const char* temporary, const char* previous) {
  if (!target || !temporary || !previous || !validPaths(target, temporary, previous)) return false;
  const size_t targetBytes = std::strlen(target) + 1;
  const size_t temporaryBytes = std::strlen(temporary) + 1;
  const size_t previousBytes = std::strlen(previous) + 1;
  if (targetBytes > MAX_PATH_BYTES + 1 || temporaryBytes > MAX_PATH_BYTES + 1 || previousBytes > MAX_PATH_BYTES + 1 ||
      !recover(fs))
    return false;
  if (!fs.exists("/.crosspoint") && !fs.mkdir("/.crosspoint")) return false;
  if (fs.exists(PENDING) && !fs.remove(PENDING)) return false;
  uint8_t header[HEADER_BYTES] = {'C', 'P', 'R', 'U', 1, static_cast<uint8_t>(fs.exists(target)), 0, 0};
  write32(header + 8, static_cast<uint32_t>(targetBytes));
  write32(header + 12, static_cast<uint32_t>(temporaryBytes));
  write32(header + 16, static_cast<uint32_t>(previousBytes));
  const uint32_t checksum =
      crc32(previous, previousBytes, crc32(temporary, temporaryBytes, crc32(target, targetBytes, crc32(header, 20))));
  write32(header + 20, checksum);
  decltype(fs.open(PENDING)) file;
  if (!fs.openFileForWrite("UPLOAD", PENDING, file)) return false;
  const bool written =
      file.write(header, sizeof(header)) == sizeof(header) && file.write(target, targetBytes) == targetBytes &&
      file.write(temporary, temporaryBytes) == temporaryBytes && file.write(previous, previousBytes) == previousBytes;
  file.flush();
  const bool sizeMatches = file.fileSize() == HEADER_BYTES + targetBytes + temporaryBytes + previousBytes;
  const bool closed = file.close();
  if (!written || !sizeMatches || !closed) return false;
  Record verified;
  if (!load(fs, PENDING, verified)) return false;
  return fs.rename(PENDING, JOURNAL);
}

template <typename FileSystem>
bool complete(FileSystem& fs) {
  return !fs.exists(JOURNAL) || fs.remove(JOURNAL);
}
}  // namespace WebUploadRecovery
