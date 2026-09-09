/**
 * XtcParser.cpp
 *
 * XTC file parsing implementation
 * XTC ebook support for CrossPoint Reader
 */

#include "XtcParser.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>

#include "XtcBitmapUtils.h"

namespace xtc {

namespace {
constexpr size_t MAX_XTC_CHAPTERS = 256;
}  // namespace

XtcParser::XtcParser()
    : m_isOpen(false),
      m_defaultWidth(DISPLAY_WIDTH),
      m_defaultHeight(DISPLAY_HEIGHT),
      m_bitDepth(1),
      m_hasChapters(false),
      m_chaptersLoaded(false),
      m_lastError(XtcError::OK) {
  memset(&m_header, 0, sizeof(m_header));
}

XtcParser::~XtcParser() { close(); }

XtcError XtcParser::open(const char* filepath) {
  // Close if already open
  if (m_isOpen) {
    close();
  }

  m_filepath = filepath;

  // Open file
  if (!Storage.openFileForRead("XTC", filepath, m_file)) {
    m_lastError = XtcError::FILE_NOT_FOUND;
    return m_lastError;
  }

  // Read header
  m_lastError = readHeader();
  if (m_lastError != XtcError::OK) {
    LOG_DBG("XTC", "Failed to read header: %s", errorToString(m_lastError));
    // Explicit close() required: member variable persists beyond function scope
    m_file.close();
    return m_lastError;
  }

  // Read title & author if available
  if (m_header.hasMetadata) {
    m_lastError = readTitle();
    if (m_lastError != XtcError::OK) {
      LOG_DBG("XTC", "Failed to read title: %s", errorToString(m_lastError));
      // Explicit close() required: member variable persists beyond function scope
      m_file.close();
      return m_lastError;
    }
    m_lastError = readAuthor();
    if (m_lastError != XtcError::OK) {
      LOG_DBG("XTC", "Failed to read author: %s", errorToString(m_lastError));
      // Explicit close() required: member variable persists beyond function scope
      m_file.close();
      return m_lastError;
    }
    // Trim excess capacity from metadata strings
    m_title.shrink_to_fit();
    m_author.shrink_to_fit();
  }

  // Read first page info for default dimensions. Other page-table entries are
  // read on demand, avoiding pageCount * sizeof(PageTableEntry) heap use.
  m_lastError = readFirstPageInfo();
  if (m_lastError != XtcError::OK) {
    LOG_DBG("XTC", "Failed to read first page info: %s", errorToString(m_lastError));
    // Explicit close() required: member variable persists beyond function scope
    m_file.close();
    return m_lastError;
  }

  // Defer chapter parsing until actually needed. Chapter strings can use
  // significant heap, and most page renders do not need them.
  // Older XTC files start the page table at 0x30, so they do not have the later
  // chapterOffset field even if the bytes read into that slot are non-zero.
  m_hasChapters = (m_header.hasChapters == 1 && m_header.pageTableOffset >= sizeof(XtcHeader));
  m_chaptersLoaded = false;

  // Close the source file to free SdFat buffers before the reader allocates
  // the page bitmap buffer.
  m_file.close();

  m_isOpen = true;
  LOG_DBG("XTC", "Opened file: %s (%u pages, %dx%d)", filepath, m_header.pageCount, m_defaultWidth, m_defaultHeight);
  return XtcError::OK;
}

void XtcParser::close() {
  closeFile();
  m_isOpen = false;
  m_streamBuffer.reset();
  m_chaptersLoaded = false;
  m_chapters.clear();
  m_title.clear();
  m_author.clear();
  m_hasChapters = false;
  memset(&m_header, 0, sizeof(m_header));
}

bool XtcParser::ensureFileOpen() {
  if (m_file.isOpen()) {
    return true;
  }
  return !m_filepath.empty() && Storage.openFileForRead("XTC", m_filepath.c_str(), m_file);
}

void XtcParser::closeFile() {
  if (m_file.isOpen()) {
    m_file.close();
  }
}

XtcError XtcParser::readHeader() {
  // Read first 56 bytes of header
  size_t bytesRead = m_file.read(reinterpret_cast<uint8_t*>(&m_header), sizeof(XtcHeader));
  if (bytesRead != sizeof(XtcHeader)) {
    return XtcError::READ_ERROR;
  }

  // Verify magic number (accept both XTC and XTCH)
  if (m_header.magic != XTC_MAGIC && m_header.magic != XTCH_MAGIC) {
    LOG_DBG("XTC", "Invalid magic: 0x%08X (expected 0x%08X or 0x%08X)", m_header.magic, XTC_MAGIC, XTCH_MAGIC);
    return XtcError::INVALID_MAGIC;
  }

  // Determine bit depth from file magic
  m_bitDepth = (m_header.magic == XTCH_MAGIC) ? 2 : 1;

  // Check version
  // Currently, version 1.0 is the only valid version, however some generators are swapping the bytes around, so we
  // accept both 1.0 and 0.1 for compatibility
  const bool validVersion = m_header.versionMajor == 1 && m_header.versionMinor == 0 ||
                            m_header.versionMajor == 0 && m_header.versionMinor == 1;
  if (!validVersion) {
    LOG_DBG("XTC", "Unsupported version: %u.%u", m_header.versionMajor, m_header.versionMinor);
    return XtcError::INVALID_VERSION;
  }

  // Basic validation
  if (m_header.pageCount == 0) {
    return XtcError::CORRUPTED_HEADER;
  }

  LOG_DBG("XTC", "Header: magic=0x%08X (%s), ver=%u.%u, pages=%u, bitDepth=%u", m_header.magic,
          (m_header.magic == XTCH_MAGIC) ? "XTCH" : "XTC", m_header.versionMajor, m_header.versionMinor,
          m_header.pageCount, m_bitDepth);

  return XtcError::OK;
}

XtcError XtcParser::readTitle() {
  constexpr auto titleOffset = 0x38;
  if (!m_file.seek(titleOffset)) {
    return XtcError::READ_ERROR;
  }

  char titleBuf[128] = {0};
  m_file.read(titleBuf, sizeof(titleBuf) - 1);
  m_title = titleBuf;

  LOG_DBG("XTC", "Title: %s", m_title.c_str());
  return XtcError::OK;
}

XtcError XtcParser::readAuthor() {
  // Read author as null-terminated UTF-8 string with max length 64, directly following title
  constexpr auto authorOffset = 0xB8;
  if (!m_file.seek(authorOffset)) {
    return XtcError::READ_ERROR;
  }

  char authorBuf[64] = {0};
  m_file.read(authorBuf, sizeof(authorBuf) - 1);
  m_author = authorBuf;

  LOG_DBG("XTC", "Author: %s", m_author.c_str());
  return XtcError::OK;
}

XtcError XtcParser::readFirstPageInfo() {
  if (m_header.pageTableOffset == 0) {
    LOG_DBG("XTC", "Page table offset is 0, cannot read");
    return XtcError::CORRUPTED_HEADER;
  }

  // Verify the file is large enough to contain the full page table
  const uint64_t fileSize = m_file.fileSize64();
  const uint64_t pageTableSize = static_cast<uint64_t>(m_header.pageCount) * sizeof(PageTableEntry);
  if (m_header.pageTableOffset < XTC_LEGACY_HEADER_SIZE || m_header.pageTableOffset > fileSize ||
      pageTableSize > fileSize - m_header.pageTableOffset) {
    LOG_DBG("XTC",
            "Page table exceeds file bounds: file=%llu tableOffset=%llu tableSize=%llu pages=%u entrySize=%u "
            "dataOffset=%llu minTableOffset=%llu",
            static_cast<unsigned long long>(fileSize), static_cast<unsigned long long>(m_header.pageTableOffset),
            static_cast<unsigned long long>(pageTableSize), m_header.pageCount,
            static_cast<unsigned int>(sizeof(PageTableEntry)), static_cast<unsigned long long>(m_header.dataOffset),
            static_cast<unsigned long long>(XTC_LEGACY_HEADER_SIZE));
    return XtcError::CORRUPTED_HEADER;
  }

  if (!m_file.seek64(m_header.pageTableOffset)) {
    LOG_DBG("XTC", "Failed to seek to page table at %llu", m_header.pageTableOffset);
    return XtcError::READ_ERROR;
  }

  PageTableEntry entry;
  const size_t bytesRead = m_file.read(reinterpret_cast<uint8_t*>(&entry), sizeof(PageTableEntry));
  if (bytesRead != sizeof(PageTableEntry)) {
    LOG_DBG("XTC", "Failed to read first page table entry");
    return XtcError::READ_ERROR;
  }

  m_defaultWidth = entry.width;
  m_defaultHeight = entry.height;

  LOG_DBG("XTC", "Page table validated: %u pages, default %dx%d", m_header.pageCount, m_defaultWidth, m_defaultHeight);
  return XtcError::OK;
}

bool XtcParser::readPageTableEntry(uint32_t pageIndex, PageInfo& info) {
  if (pageIndex >= m_header.pageCount) {
    return false;
  }

  if (!ensureFileOpen()) {
    LOG_DBG("XTC", "Failed to reopen file for page table read");
    return false;
  }

  const uint64_t entryOffset = m_header.pageTableOffset + static_cast<uint64_t>(pageIndex) * sizeof(PageTableEntry);
  if (!m_file.seek64(entryOffset)) {
    LOG_DBG("XTC", "Failed to seek to page table entry %lu at %llu", pageIndex,
            static_cast<unsigned long long>(entryOffset));
    return false;
  }

  PageTableEntry entry;
  const size_t bytesRead = m_file.read(reinterpret_cast<uint8_t*>(&entry), sizeof(PageTableEntry));
  if (bytesRead != sizeof(PageTableEntry)) {
    LOG_DBG("XTC", "Failed to read page table entry %lu", pageIndex);
    return false;
  }

  info.offset = entry.dataOffset;
  info.size = entry.dataSize;
  info.width = entry.width;
  info.height = entry.height;
  info.bitDepth = m_bitDepth;
  return true;
}

XtcError XtcParser::readChapters() {
  m_chapters.clear();

  if (!ensureFileOpen()) {
    return XtcError::READ_ERROR;
  }

  uint8_t hasChaptersFlag = 0;
  if (!m_file.seek(0x0B)) {
    return XtcError::READ_ERROR;
  }
  if (m_file.read(&hasChaptersFlag, sizeof(hasChaptersFlag)) != sizeof(hasChaptersFlag)) {
    return XtcError::READ_ERROR;
  }

  if (hasChaptersFlag != 1) {
    m_hasChapters = false;
    return XtcError::OK;
  }

  uint64_t chapterOffset = 0;
  if (!m_file.seek(0x30)) {
    return XtcError::READ_ERROR;
  }
  if (m_file.read(reinterpret_cast<uint8_t*>(&chapterOffset), sizeof(chapterOffset)) != sizeof(chapterOffset)) {
    return XtcError::READ_ERROR;
  }

  if (chapterOffset == 0) {
    m_hasChapters = false;
    return XtcError::OK;
  }

  const uint64_t fileSize = m_file.fileSize64();
  if (chapterOffset < sizeof(XtcHeader) || chapterOffset >= fileSize || chapterOffset + 96 > fileSize) {
    m_hasChapters = false;
    return XtcError::OK;
  }

  // Clamp maxOffset to fileSize so bogus header values can't inflate chapterCount
  uint64_t maxOffset = fileSize;
  if (m_header.pageTableOffset > chapterOffset && m_header.pageTableOffset <= fileSize) {
    maxOffset = m_header.pageTableOffset;
  } else if (m_header.dataOffset > chapterOffset && m_header.dataOffset <= fileSize) {
    maxOffset = m_header.dataOffset;
  }

  if (maxOffset <= chapterOffset) {
    m_hasChapters = false;
    return XtcError::OK;
  }

  constexpr size_t chapterSize = 96;
  const uint64_t available = maxOffset - chapterOffset;
  const size_t chapterCount = static_cast<size_t>(available / chapterSize);
  if (chapterCount == 0) {
    m_hasChapters = false;
    return XtcError::OK;
  }

  if (!m_file.seek64(chapterOffset)) {
    return XtcError::READ_ERROR;
  }

  const size_t chaptersToRead = chapterCount > MAX_XTC_CHAPTERS ? MAX_XTC_CHAPTERS : chapterCount;
  if (chapterCount > MAX_XTC_CHAPTERS) {
    LOG_DBG("XTC", "Chapter table has %u entries, limiting to %u", static_cast<unsigned int>(chapterCount),
            static_cast<unsigned int>(MAX_XTC_CHAPTERS));
  }

  // Bounded reserve: chapterCount is derived from file offsets, so do not reserve
  // an untrusted value on ESP32-C3's constrained heap.
  m_chapters.reserve(chaptersToRead);
  std::vector<uint8_t> chapterBuf(chapterSize);
  for (size_t i = 0; i < chaptersToRead; i++) {
    if (m_file.read(chapterBuf.data(), chapterSize) != chapterSize) {
      return XtcError::READ_ERROR;
    }

    char nameBuf[81];
    memcpy(nameBuf, chapterBuf.data(), 80);
    nameBuf[80] = '\0';
    const size_t nameLen = strnlen(nameBuf, 80);
    std::string name(nameBuf, nameLen);

    uint16_t startPage = 0;
    uint16_t endPage = 0;
    memcpy(&startPage, chapterBuf.data() + 0x50, sizeof(startPage));
    memcpy(&endPage, chapterBuf.data() + 0x52, sizeof(endPage));

    if (name.empty() && startPage == 0 && endPage == 0) {
      break;
    }

    if (startPage > 0) {
      startPage--;
    }
    if (endPage > 0) {
      endPage--;
    }

    if (startPage >= m_header.pageCount) {
      continue;
    }

    if (endPage >= m_header.pageCount) {
      endPage = m_header.pageCount - 1;
    }

    if (startPage > endPage) {
      continue;
    }

    ChapterInfo chapter{std::move(name), startPage, endPage};
    m_chapters.push_back(std::move(chapter));
  }

  m_hasChapters = !m_chapters.empty();
  LOG_DBG("XTC", "Chapters: %u", static_cast<unsigned int>(m_chapters.size()));
  return XtcError::OK;
}

bool XtcParser::getPageInfo(uint32_t pageIndex, PageInfo& info) { return readPageTableEntry(pageIndex, info); }

const std::vector<ChapterInfo>& XtcParser::getChapters() {
  if (!m_chaptersLoaded && m_hasChapters) {
    const XtcError err = readChapters();
    if (err != XtcError::OK) {
      LOG_ERR("XTC", "Failed to lazy-load chapters: %s", errorToString(err));
      m_hasChapters = false;
      m_chapters.clear();
    }
    m_chaptersLoaded = true;
    closeFile();
  }
  return m_chapters;
}

XtcError XtcParser::preparePageRead(const uint32_t pageIndex, PageInfo& page, size_t& bitmapSize) {
  if (!m_isOpen) return m_lastError = XtcError::FILE_NOT_FOUND;
  if (pageIndex >= m_header.pageCount) return m_lastError = XtcError::PAGE_OUT_OF_RANGE;
  if (!readPageTableEntry(pageIndex, page) || !ensureFileOpen()) {
    closeFile();
    return m_lastError = XtcError::READ_ERROR;
  }
  const uint64_t fileSize = m_file.fileSize64();
  if (page.offset > fileSize || sizeof(XtgPageHeader) > fileSize - page.offset || !m_file.seek64(page.offset)) {
    closeFile();
    return m_lastError = XtcError::READ_ERROR;
  }
  XtgPageHeader header{};
  if (m_file.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header)) {
    closeFile();
    return m_lastError = XtcError::READ_ERROR;
  }
  if (header.magic != (m_bitDepth == 2 ? XTH_MAGIC : XTG_MAGIC)) {
    closeFile();
    return m_lastError = XtcError::INVALID_MAGIC;
  }
  if (header.compression != 0) {
    closeFile();
    return m_lastError = XtcError::DECOMPRESSION_ERROR;
  }
  if (header.width != page.width || header.height != page.height ||
      !bitmapPayloadSize(header.width, header.height, m_bitDepth, bitmapSize) ||
      bitmapSize > fileSize - page.offset - sizeof(header) ||
      // Some legacy generators leave dataSize unset. Keep that compatibility,
      // but never read past a nonzero declared payload or the source file.
      (header.dataSize != 0 && header.dataSize < bitmapSize)) {
    closeFile();
    return m_lastError = XtcError::CORRUPTED_HEADER;
  }
  return m_lastError = XtcError::OK;
}

bool XtcParser::ensureStreamBuffer() {
  if (!m_streamBuffer) m_streamBuffer = makeUniqueNoThrow<uint8_t[]>(STREAM_BUFFER_SIZE);
  if (m_streamBuffer) return true;
  LOG_ERR("XTC", "Could not allocate %u-byte streaming buffer", static_cast<unsigned>(STREAM_BUFFER_SIZE));
  m_lastError = XtcError::MEMORY_ERROR;
  return false;
}

size_t XtcParser::loadPage(const uint32_t pageIndex, uint8_t* buffer, const size_t bufferSize) {
  PageInfo page{};
  size_t bitmapSize = 0;
  if (preparePageRead(pageIndex, page, bitmapSize) != XtcError::OK) return 0;
  if (!buffer || bufferSize < bitmapSize) {
    closeFile();
    m_lastError = XtcError::MEMORY_ERROR;
    return 0;
  }
  const int bytesRead = m_file.read(buffer, bitmapSize);
  closeFile();
  if (bytesRead < 0 || static_cast<size_t>(bytesRead) != bitmapSize) {
    m_lastError = XtcError::READ_ERROR;
    return 0;
  }
  m_lastError = XtcError::OK;
  return bitmapSize;
}

XtcError XtcParser::loadPageStreaming(
    const uint32_t pageIndex, const std::function<void(const uint8_t* data, size_t size, size_t offset)>& callback,
    const size_t chunkSize) {
  if (!callback) return m_lastError = XtcError::READ_ERROR;
  const auto forward = [](void* raw, const uint8_t* data, const size_t size, const size_t offset) {
    (*static_cast<const std::function<void(const uint8_t*, size_t, size_t)>*>(raw))(data, size, offset);
  };
  return loadPageStreaming(pageIndex, forward, const_cast<void*>(static_cast<const void*>(&callback)), chunkSize);
}

XtcError XtcParser::loadPageStreaming(const uint32_t pageIndex, const StreamCallback callback, void* context,
                                      const size_t chunkSize) {
  if (!callback || chunkSize == 0) return m_lastError = XtcError::READ_ERROR;
  PageInfo page{};
  size_t bitmapSize = 0;
  if (preparePageRead(pageIndex, page, bitmapSize) != XtcError::OK) return m_lastError;
  if (!ensureStreamBuffer()) {
    closeFile();
    return m_lastError;
  }
  const size_t boundedChunkSize = std::min(chunkSize, STREAM_BUFFER_SIZE);
  for (size_t offset = 0; offset < bitmapSize;) {
    const size_t toRead = std::min(boundedChunkSize, bitmapSize - offset);
    const int bytesRead = m_file.read(m_streamBuffer.get(), toRead);
    if (bytesRead <= 0 || static_cast<size_t>(bytesRead) != toRead) {
      closeFile();
      return m_lastError = XtcError::READ_ERROR;
    }
    callback(context, m_streamBuffer.get(), toRead, offset);
    offset += toRead;
  }
  closeFile();
  return m_lastError = XtcError::OK;
}

XtcError XtcParser::loadPagePlanePairs(const uint32_t pageIndex, const PlanePairCallback callback, void* context) {
  if (!callback || m_bitDepth != 2) return m_lastError = XtcError::READ_ERROR;
  PageInfo page{};
  size_t bitmapSize = 0;
  if (preparePageRead(pageIndex, page, bitmapSize) != XtcError::OK) return m_lastError;
  if (!ensureStreamBuffer()) {
    closeFile();
    return m_lastError;
  }
  constexpr size_t planeChunkSize = STREAM_BUFFER_SIZE / 2;
  const size_t planeSize = bitmapSize / 2;
  const uint64_t payloadOffset = page.offset + sizeof(XtgPageHeader);
  uint8_t* first = m_streamBuffer.get();
  uint8_t* second = first + planeChunkSize;
  for (size_t offset = 0; offset < planeSize;) {
    const size_t toRead = std::min(planeChunkSize, planeSize - offset);
    if (!m_file.seek64(payloadOffset + offset) || m_file.read(first, toRead) != static_cast<int>(toRead) ||
        !m_file.seek64(payloadOffset + planeSize + offset) || m_file.read(second, toRead) != static_cast<int>(toRead)) {
      closeFile();
      return m_lastError = XtcError::READ_ERROR;
    }
    callback(context, first, second, toRead, offset);
    offset += toRead;
  }
  closeFile();
  return m_lastError = XtcError::OK;
}

bool XtcParser::isValidXtcFile(const char* filepath) {
  FsFile file;
  if (!Storage.openFileForRead("XTC", filepath, file)) {
    return false;
  }

  uint32_t magic = 0;
  size_t bytesRead = file.read(reinterpret_cast<uint8_t*>(&magic), sizeof(magic));
  file.close();

  if (bytesRead != sizeof(magic)) {
    return false;
  }

  return (magic == XTC_MAGIC || magic == XTCH_MAGIC);
}

}  // namespace xtc
