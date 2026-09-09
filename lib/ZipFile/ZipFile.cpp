#include "ZipFile.h"

#include <HalStorage.h>
#include <InflateReader.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

struct ZipInflateCtx {
  InflateReader reader;  // Must be first — callback casts uzlib_uncomp* to ZipInflateCtx*
  FsFile* file = nullptr;
  size_t fileRemaining = 0;
  uint8_t* readBuf = nullptr;
  size_t readBufSize = 0;
};

namespace {
constexpr uint16_t ZIP_METHOD_STORED = 0;
constexpr uint16_t ZIP_METHOD_DEFLATED = 8;

// RAII zip: opens the zip if not already open, closes on destruction only if
// it performed the open.  Removes the wasOpen/close boilerplate from every method.
class ScopedOpenClose final {
 public:
  [[nodiscard]] explicit ScopedOpenClose(ZipFile& zf) : zf(zf), needsClose(!zf.isOpen()) {
    if (needsClose) ok = zf.open();
  }
  ~ScopedOpenClose() {
    if (needsClose && ok) zf.close();
  }
  ScopedOpenClose(const ScopedOpenClose&) = delete;
  ScopedOpenClose& operator=(const ScopedOpenClose&) = delete;
  ScopedOpenClose(ScopedOpenClose&&) = delete;
  ScopedOpenClose& operator=(ScopedOpenClose&&) = delete;
  explicit operator bool() const { return ok || !needsClose; }

 private:
  ZipFile& zf;
  bool needsClose = false;
  bool ok = true;  // true when zip was already open (no open() call needed)
};

int zipReadCallback(uzlib_uncomp* uncomp) {
  auto* ctx = reinterpret_cast<ZipInflateCtx*>(uncomp);
  if (ctx->fileRemaining == 0) return -1;

  const size_t toRead = ctx->fileRemaining < ctx->readBufSize ? ctx->fileRemaining : ctx->readBufSize;
  const int readResult = ctx->file->read(ctx->readBuf, toRead);
  // HAL uses negative values for I/O errors. Check before converting to size_t
  // or changing the decoder bounds / remaining byte count.
  if (readResult <= 0) return -1;
  const size_t bytesRead = static_cast<size_t>(readResult);
  ctx->fileRemaining -= bytesRead;

  uncomp->source = ctx->readBuf + 1;
  uncomp->source_limit = ctx->readBuf + bytesRead;
  return ctx->readBuf[0];
}
}  // namespace

bool ZipFile::readExact(void* buffer, const size_t length) {
  const int result = file.read(buffer, length);
  if (result < 0 || static_cast<size_t>(result) != length) {
    LOG_ERR("ZIP", "Failed to read %zu bytes (got %d)", length, result);
    return false;
  }
  return true;
}

bool ZipFile::loadAllFileStatSlims() {
  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  if (!loadZipDetails()) return false;

  if (!file.seek(zipDetails.centralDirOffset)) return false;

  uint32_t sig;
  char itemName[256];
  fileStatSlimCache.clear();
  fileStatSlimCache.reserve(zipDetails.totalEntries);

  while (file.available()) {
    if (!readExact(&sig, 4)) {
      fileStatSlimCache.clear();
      return false;
    }
    if (sig != 0x02014b50) break;  // End of list

    FileStatSlim fileStat = {};

    uint16_t nameLen, m, k;
    if (!file.seekCur(6) || !readExact(&fileStat.method, 2) || !file.seekCur(8) ||
        !readExact(&fileStat.compressedSize, 4) || !readExact(&fileStat.uncompressedSize, 4) ||
        !readExact(&nameLen, 2) || !readExact(&m, 2) || !readExact(&k, 2) || !file.seekCur(8) ||
        !readExact(&fileStat.localHeaderOffset, 4)) {
      fileStatSlimCache.clear();
      return false;
    }

    if (nameLen < sizeof(itemName)) {
      if (!readExact(itemName, nameLen)) {
        fileStatSlimCache.clear();
        return false;
      }
      itemName[nameLen] = '\0';
      fileStatSlimCache.emplace(itemName, fileStat);
    } else {
      // Skip over oversized entry names to avoid writing past fixed buffer.
      if (!file.seekCur(nameLen)) {
        fileStatSlimCache.clear();
        return false;
      }
    }

    // Skip the rest of this entry (extra field + comment)
    if (!file.seekCur(m + k)) {
      fileStatSlimCache.clear();
      return false;
    }
  }

  // Set cursor to start of central directory for sequential access
  lastCentralDirPos = zipDetails.centralDirOffset;
  lastCentralDirPosValid = true;

  return true;
}

bool ZipFile::loadFileStatSlim(const char* filename, FileStatSlim* fileStat) {
  if (!fileStatSlimCache.empty()) {
    const auto it = fileStatSlimCache.find(filename);
    if (it != fileStatSlimCache.end()) {
      *fileStat = it->second;
      return true;
    }
    return false;
  }

  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  if (!loadZipDetails()) return false;

  // Phase 1: Try scanning from cursor position first
  uint32_t startPos = lastCentralDirPosValid ? lastCentralDirPos : zipDetails.centralDirOffset;
  bool wrapped = false;
  bool found = false;

  if (!file.seek(startPos)) return false;

  uint32_t sig;
  char itemName[256];

  while (true) {
    uint32_t entryStart = file.position();

    if (!readExact(&sig, 4)) return false;
    if (sig != 0x02014b50) {
      // End of central directory
      if (!wrapped && lastCentralDirPosValid && startPos != zipDetails.centralDirOffset) {
        // Wrap around to beginning
        if (!file.seek(zipDetails.centralDirOffset)) return false;
        wrapped = true;
        continue;
      }
      break;
    }

    // If we've wrapped and reached our start position, stop
    if (wrapped && entryStart >= startPos) {
      break;
    }

    uint16_t nameLen, m, k;
    if (!file.seekCur(6) || !readExact(&fileStat->method, 2) || !file.seekCur(8) ||
        !readExact(&fileStat->compressedSize, 4) || !readExact(&fileStat->uncompressedSize, 4) ||
        !readExact(&nameLen, 2) || !readExact(&m, 2) || !readExact(&k, 2) || !file.seekCur(8) ||
        !readExact(&fileStat->localHeaderOffset, 4))
      return false;

    if (nameLen < 256) {
      if (!readExact(itemName, nameLen)) return false;
      itemName[nameLen] = '\0';

      if (strcmp(itemName, filename) == 0) {
        // Found it! Update cursor to next entry
        if (!file.seekCur(m + k)) return false;
        lastCentralDirPos = file.position();
        lastCentralDirPosValid = true;
        found = true;
        break;
      }
    } else {
      // Name too long, skip it
      if (!file.seekCur(nameLen)) return false;
    }

    // Skip extra field + comment
    if (!file.seekCur(m + k)) return false;
  }

  return found;
}

long ZipFile::getDataOffset(const FileStatSlim& fileStat) {
  const ScopedOpenClose zip{*this};
  if (!zip) return -1;

  constexpr auto localHeaderSize = 30;

  uint8_t pLocalHeader[localHeaderSize];
  const uint64_t fileOffset = fileStat.localHeaderOffset;

  if (!file.seek(fileOffset) || !readExact(pLocalHeader, localHeaderSize)) {
    LOG_ERR("ZIP", "Something went wrong reading the local header");
    return -1;
  }

  if (pLocalHeader[0] + (pLocalHeader[1] << 8) + (pLocalHeader[2] << 16) + (pLocalHeader[3] << 24) !=
      0x04034b50 /* ZIP local file header signature */) {
    LOG_ERR("ZIP", "Not a valid zip file header");
    return -1;
  }

  const uint16_t filenameLength = pLocalHeader[26] + (pLocalHeader[27] << 8);
  const uint16_t extraOffset = pLocalHeader[28] + (pLocalHeader[29] << 8);
  return fileOffset + localHeaderSize + filenameLength + extraOffset;
}

bool ZipFile::loadZipDetails() {
  if (zipDetails.isSet) {
    return true;
  }

  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  const size_t fileSize = file.size();
  if (fileSize < 22) {
    LOG_ERR("ZIP", "File too small to be a valid zip");
    return false;  // Minimum EOCD size is 22 bytes
  }

  // We scan the last 1KB (or the whole file if smaller) for the EOCD signature
  // 0x06054b50 is stored as 0x50, 0x4b, 0x05, 0x06 in little-endian
  const int scanRange = fileSize > 1024 ? 1024 : fileSize;
  const auto buffer = static_cast<uint8_t*>(malloc(scanRange));
  if (!buffer) {
    LOG_ERR("ZIP", "Failed to allocate memory for EOCD scan buffer");
    return false;
  }

  if (!file.seek(fileSize - scanRange) || !readExact(buffer, scanRange)) {
    free(buffer);
    return false;
  }

  // Scan backwards for the signature
  int foundOffset = -1;
  for (int i = scanRange - 22; i >= 0; i--) {
    constexpr uint32_t signature = 0x06054b50;
    if (*reinterpret_cast<uint32_t*>(&buffer[i]) == signature) {
      foundOffset = i;
      break;
    }
  }

  if (foundOffset == -1) {
    LOG_ERR("ZIP", "EOCD signature not found in zip file");
    free(buffer);
    return false;
  }

  // Now extract the values we need from the EOCD record
  // Relative positions within EOCD:
  // Offset 10: Total number of entries (2 bytes)
  // Offset 16: Offset of start of central directory with respect to the starting disk number (4 bytes)
  zipDetails.totalEntries = *reinterpret_cast<uint16_t*>(&buffer[foundOffset + 10]);
  zipDetails.centralDirOffset = *reinterpret_cast<uint32_t*>(&buffer[foundOffset + 16]);
  zipDetails.isSet = true;

  free(buffer);
  return true;
}

bool ZipFile::open() {
  if (!Storage.openFileForRead("ZIP", filePath, file)) {
    return false;
  }
  return true;
}

bool ZipFile::close() {
  if (file) {
    // Explicit close() required: member variable persists beyond function scope
    file.close();
  }
  lastCentralDirPos = 0;
  lastCentralDirPosValid = false;
  return true;
}

bool ZipFile::getInflatedFileSize(const char* filename, size_t* size) {
  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) {
    return false;
  }

  *size = static_cast<size_t>(fileStat.uncompressedSize);
  return true;
}

int ZipFile::fillUncompressedSizes(std::deque<SizeTarget>& targets, std::deque<uint32_t>& sizes) {
  if (targets.empty()) {
    return 0;
  }

  const ScopedOpenClose zip{*this};
  if (!zip) return 0;

  if (!loadZipDetails()) return 0;

  if (!file.seek(zipDetails.centralDirOffset)) return 0;

  int matched = 0;
  const int targetCount = static_cast<int>(targets.size());
  uint32_t sig;
  char itemName[256];

  while (file.available()) {
    if (!readExact(&sig, 4)) return matched;
    if (sig != 0x02014b50) break;

    uint16_t method;
    uint32_t compressedSize, uncompressedSize;
    uint16_t nameLen, m, k;
    uint32_t localHeaderOffset;
    if (!file.seekCur(6) || !readExact(&method, 2) || !file.seekCur(8) || !readExact(&compressedSize, 4) ||
        !readExact(&uncompressedSize, 4) || !readExact(&nameLen, 2) || !readExact(&m, 2) || !readExact(&k, 2) ||
        !file.seekCur(8) || !readExact(&localHeaderOffset, 4))
      return matched;

    if (nameLen < 256) {
      if (!readExact(itemName, nameLen)) return matched;
      itemName[nameLen] = '\0';

      uint64_t hash = fnvHash64(itemName, nameLen);
      SizeTarget key = {hash, nameLen, 0};

      auto it = std::lower_bound(targets.begin(), targets.end(), key, [](const SizeTarget& a, const SizeTarget& b) {
        return a.hash < b.hash || (a.hash == b.hash && a.len < b.len);
      });

      while (it != targets.end() && it->hash == hash && it->len == nameLen) {
        if (it->index < sizes.size()) {
          sizes[it->index] = uncompressedSize;
          matched++;
        }
        ++it;
      }

      if (matched >= targetCount) {
        break;
      }
    } else {
      if (!file.seekCur(nameLen)) return matched;
    }

    if (!file.seekCur(m + k)) return matched;
  }

  return matched;
}

uint8_t* ZipFile::readFileToMemory(const char* filename, size_t* size, const bool trailingNullByte) {
  const ScopedOpenClose zip{*this};
  if (!zip) return nullptr;

  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) return nullptr;

  const long fileOffset = getDataOffset(fileStat);
  if (fileOffset < 0) return nullptr;

  if (!file.seek(fileOffset)) return nullptr;

  const auto deflatedDataSize = fileStat.compressedSize;
  const auto inflatedDataSize = fileStat.uncompressedSize;
  const auto dataSize = trailingNullByte ? inflatedDataSize + 1 : inflatedDataSize;
  const auto data = static_cast<uint8_t*>(malloc(dataSize));
  if (data == nullptr) {
    LOG_ERR("ZIP", "Failed to allocate memory for output buffer (%zu bytes)", dataSize);
    return nullptr;
  }

  if (fileStat.method == ZIP_METHOD_STORED) {
    // no deflation, just read content
    if (!readExact(data, inflatedDataSize)) {
      LOG_ERR("ZIP", "Failed to read data");
      free(data);
      return nullptr;
    }

    // Continue out of block with data set
  } else if (fileStat.method == ZIP_METHOD_DEFLATED) {
    // Read out deflated content from file
    const auto deflatedData = static_cast<uint8_t*>(malloc(deflatedDataSize));
    if (deflatedData == nullptr) {
      LOG_ERR("ZIP", "Failed to allocate memory for decompression buffer");
      free(data);
      return nullptr;
    }

    if (!readExact(deflatedData, deflatedDataSize)) {
      free(deflatedData);
      free(data);
      return nullptr;
    }

    bool success = false;
    {
      InflateReader r;
      r.init(false);
      r.setSource(deflatedData, deflatedDataSize);
      success = r.read(data, inflatedDataSize);
    }
    free(deflatedData);

    if (!success) {
      LOG_ERR("ZIP", "Failed to inflate file");
      free(data);
      return nullptr;
    }

    // Continue out of block with data set
  } else {
    LOG_ERR("ZIP", "Unsupported compression method");
    free(data);
    return nullptr;
  }

  if (trailingNullByte) data[inflatedDataSize] = '\0';
  if (size) *size = inflatedDataSize;
  return data;
}

bool ZipFile::readFileToStream(const char* filename, Print& out, const size_t chunkSize, const bool allowEarlyStop) {
  if (chunkSize == 0) {
    LOG_ERR("ZIP", "Stream chunk size must be positive");
    return false;
  }
  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) return false;

  const long fileOffset = getDataOffset(fileStat);
  if (fileOffset < 0) return false;

  if (!file.seek(fileOffset)) return false;
  const auto deflatedDataSize = fileStat.compressedSize;
  const auto inflatedDataSize = fileStat.uncompressedSize;

  if (fileStat.method == ZIP_METHOD_STORED) {
    // no deflation, just read content
    const auto buffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!buffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for buffer");
      return false;
    }

    size_t remaining = inflatedDataSize;
    while (remaining > 0) {
      const int readResult = file.read(buffer, remaining < chunkSize ? remaining : chunkSize);
      if (readResult <= 0) {
        LOG_ERR("ZIP", "Could not read more bytes");
        free(buffer);
        return false;
      }

      const size_t dataRead = static_cast<size_t>(readResult);

      if (out.write(buffer, dataRead) != dataRead) {
        free(buffer);
        if (allowEarlyStop) return true;
        LOG_ERR("ZIP", "Failed to write all output bytes to stream");
        return false;
      }
      remaining -= dataRead;
    }

    free(buffer);
    return true;
  }

  if (fileStat.method == ZIP_METHOD_DEFLATED) {
    auto* fileReadBuffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!fileReadBuffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for zip file read buffer");
      return false;
    }

    auto* outputBuffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!outputBuffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for output buffer");
      free(fileReadBuffer);
      return false;
    }

    ZipInflateCtx ctx;
    ctx.file = &file;
    ctx.fileRemaining = deflatedDataSize;
    ctx.readBuf = fileReadBuffer;
    ctx.readBufSize = chunkSize;

    if (!ctx.reader.init(true)) {
      LOG_ERR("ZIP", "Failed to init inflate reader");
      free(outputBuffer);
      free(fileReadBuffer);
      return false;
    }
    ctx.reader.setReadCallback(zipReadCallback);

    bool success = false;
    size_t totalProduced = 0;
    uint8_t noProgressLoops = 0;

    while (true) {
      size_t produced;
      const size_t beforeRemaining = ctx.fileRemaining;
      const uint8_t* beforeSource = ctx.reader.raw()->source;
      const uint8_t* beforeLimit = ctx.reader.raw()->source_limit;
      const InflateStatus status = ctx.reader.readAtMost(outputBuffer, chunkSize, &produced);

      totalProduced += produced;
      if (totalProduced > static_cast<size_t>(inflatedDataSize)) {
        LOG_ERR("ZIP", "Decompressed size exceeds expected (%zu > %zu)", totalProduced,
                static_cast<size_t>(inflatedDataSize));
        break;
      }

      if (produced > 0) {
        if (out.write(outputBuffer, produced) != produced) {
          if (allowEarlyStop) {
            success = true;
          } else {
            LOG_ERR("ZIP", "Failed to write all output bytes to stream");
          }
          break;
        }
      }

      if (status == InflateStatus::Done) {
        if (totalProduced != static_cast<size_t>(inflatedDataSize)) {
          LOG_ERR("ZIP", "Decompressed size mismatch (expected %zu, got %zu)", static_cast<size_t>(inflatedDataSize),
                  totalProduced);
          break;
        }
        LOG_DBG("ZIP", "Decompressed %d bytes into %d bytes", deflatedDataSize, inflatedDataSize);
        success = true;
        break;
      }

      if (status == InflateStatus::Error) {
        LOG_ERR("ZIP", "Decompression failed");
        break;
      }

      const bool madeProgress = produced > 0 || ctx.fileRemaining != beforeRemaining ||
                                ctx.reader.raw()->source != beforeSource ||
                                ctx.reader.raw()->source_limit != beforeLimit;
      if (madeProgress) {
        noProgressLoops = 0;
      } else if (++noProgressLoops >= 8) {
        LOG_ERR("ZIP", "Decompression stalled for %s after %zu/%zu bytes", filename, totalProduced,
                static_cast<size_t>(inflatedDataSize));
        break;
      }
      // InflateStatus::Ok: output buffer full, continue
    }

    free(outputBuffer);
    free(fileReadBuffer);
    return success;  // ctx.reader destructor frees the ring buffer
  }

  LOG_ERR("ZIP", "Unsupported compression method");
  return false;
}

bool ZipFile::readFilePrefixToBuffer(const char* filename, uint8_t* out, const size_t maxBytes, size_t* bytesRead,
                                     const size_t chunkSize) {
  if (bytesRead) *bytesRead = 0;
  if (!out || maxBytes == 0 || chunkSize == 0) return false;

  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) return false;

  const long fileOffset = getDataOffset(fileStat);
  if (fileOffset < 0) return false;

  if (!file.seek(fileOffset)) return false;
  const auto deflatedDataSize = fileStat.compressedSize;
  const auto inflatedDataSize = fileStat.uncompressedSize;
  size_t copied = 0;

  if (fileStat.method == ZIP_METHOD_STORED) {
    const size_t toRead = std::min<size_t>(maxBytes, inflatedDataSize);
    size_t remaining = toRead;
    while (remaining > 0) {
      const int readResult = file.read(out + copied, remaining);
      if (readResult <= 0) {
        LOG_ERR("ZIP", "Could not read stored file prefix");
        return false;
      }
      const size_t dataRead = static_cast<size_t>(readResult);
      copied += dataRead;
      remaining -= dataRead;
    }
    if (bytesRead) *bytesRead = copied;
    return true;
  }

  if (fileStat.method == ZIP_METHOD_DEFLATED) {
    auto* fileReadBuffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!fileReadBuffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for zip prefix read buffer");
      return false;
    }

    auto* outputBuffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!outputBuffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for zip prefix output buffer");
      free(fileReadBuffer);
      return false;
    }

    ZipInflateCtx ctx;
    ctx.file = &file;
    ctx.fileRemaining = deflatedDataSize;
    ctx.readBuf = fileReadBuffer;
    ctx.readBufSize = chunkSize;

    if (!ctx.reader.init(true)) {
      LOG_ERR("ZIP", "Failed to init inflate reader for prefix");
      free(outputBuffer);
      free(fileReadBuffer);
      return false;
    }
    ctx.reader.setReadCallback(zipReadCallback);

    bool success = false;
    size_t totalProduced = 0;
    uint8_t noProgressLoops = 0;

    while (copied < maxBytes) {
      size_t produced = 0;
      const size_t beforeRemaining = ctx.fileRemaining;
      const uint8_t* beforeSource = ctx.reader.raw()->source;
      const uint8_t* beforeLimit = ctx.reader.raw()->source_limit;
      const InflateStatus status = ctx.reader.readAtMost(outputBuffer, chunkSize, &produced);
      totalProduced += produced;
      if (totalProduced > static_cast<size_t>(inflatedDataSize)) {
        LOG_ERR("ZIP", "Prefix decompressed size exceeds expected (%zu > %zu)", totalProduced,
                static_cast<size_t>(inflatedDataSize));
        break;
      }

      if (produced > 0) {
        const size_t toCopy = std::min(produced, maxBytes - copied);
        memcpy(out + copied, outputBuffer, toCopy);
        copied += toCopy;
      }

      if (copied >= maxBytes) {
        success = true;
        break;
      }

      if (status == InflateStatus::Done) {
        success = true;
        break;
      }

      if (status == InflateStatus::Error) {
        LOG_ERR("ZIP", "Prefix decompression failed");
        break;
      }

      const bool madeProgress = produced > 0 || ctx.fileRemaining != beforeRemaining ||
                                ctx.reader.raw()->source != beforeSource ||
                                ctx.reader.raw()->source_limit != beforeLimit;
      if (madeProgress) {
        noProgressLoops = 0;
      } else if (++noProgressLoops >= 8) {
        LOG_ERR("ZIP", "Prefix decompression stalled for %s after %zu/%zu bytes", filename, totalProduced,
                static_cast<size_t>(inflatedDataSize));
        break;
      }
    }

    free(outputBuffer);
    free(fileReadBuffer);
    if (bytesRead) *bytesRead = copied;
    return success && copied > 0;
  }

  LOG_ERR("ZIP", "Unsupported compression method for prefix");
  return false;
}
