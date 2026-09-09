#include <Bitmap.h>
#include <Memory.h>
#include <Xtc.h>
#include <Xtc/XtcBitmapUtils.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace {
class XtcStreamingTest : public testing::Test {
 protected:
  std::filesystem::path root;
  void SetUp() override {
    root = std::filesystem::temp_directory_path() /
           ("cpr_xtc_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    Storage.setRoot(root);
    FsFile::writeBudget = SIZE_MAX;
    FsFile::maxReadSize = 0;
    failXtcAllocation = false;
  }
  void TearDown() override {
    EXPECT_TRUE(FsFile::openReaders.empty());
    failXtcAllocation = false;
    FsFile::writeBudget = SIZE_MAX;
    if (root.parent_path() == std::filesystem::temp_directory_path() &&
        root.filename().string().find("cpr_xtc_test_") == 0) {
      std::filesystem::remove_all(root);
    }
  }
  void write(const std::string& path, const std::vector<uint8_t>& bytes) {
    const auto target = root / (path[0] == '/' ? path.substr(1) : path);
    std::filesystem::create_directories(target.parent_path());
    std::ofstream file(target, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  }
  std::vector<uint8_t> read(const std::string& path) {
    std::ifstream file(root / path.substr(1), std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  }
  std::vector<uint8_t> fixture(const uint16_t width, const uint16_t height, const uint8_t bits,
                               const bool legacy = false) {
    size_t payloadSize = 0;
    EXPECT_TRUE(xtc::bitmapPayloadSize(width, height, bits, payloadSize));
    xtc::XtcHeader header{};
    header.magic = bits == 2 ? xtc::XTCH_MAGIC : xtc::XTC_MAGIC;
    header.versionMajor = 1;
    header.pageCount = 1;
    header.pageTableOffset = legacy ? xtc::XTC_LEGACY_HEADER_SIZE : sizeof(header);
    header.dataOffset = header.pageTableOffset + sizeof(xtc::PageTableEntry);
    xtc::PageTableEntry entry{header.dataOffset, static_cast<uint32_t>(payloadSize + sizeof(xtc::XtgPageHeader)), width,
                              height};
    xtc::XtgPageHeader page{};
    page.magic = bits == 2 ? xtc::XTH_MAGIC : xtc::XTG_MAGIC;
    page.width = width;
    page.height = height;
    page.dataSize = static_cast<uint32_t>(payloadSize);
    std::vector<uint8_t> bytes(header.dataOffset + sizeof(page) + payloadSize);
    memcpy(bytes.data(), &header, header.pageTableOffset);
    memcpy(bytes.data() + header.pageTableOffset, &entry, sizeof(entry));
    memcpy(bytes.data() + header.dataOffset, &page, sizeof(page));
    uint8_t* payload = bytes.data() + header.dataOffset + sizeof(page);
    for (uint16_t y = 0; y < height; ++y) {
      for (uint16_t x = 0; x < width; ++x) {
        if (bits == 1) {
          if ((x + y) % 2) payload[static_cast<size_t>(y) * ((width + 7U) / 8U) + x / 8] |= 1 << (7 - x % 8);
        } else {
          const uint8_t pixel = x % 4;
          const size_t offset = static_cast<size_t>(width - 1 - x) * ((height + 7U) / 8U) + y / 8;
          if (pixel & 2) payload[offset] |= 1 << (7 - y % 8);
          if (pixel & 1) payload[payloadSize / 2 + offset] |= 1 << (7 - y % 8);
        }
      }
    }
    write("/book.xtc", bytes);
    return bytes;
  }
};

TEST_F(XtcStreamingTest, RawStreamingMatchesPayloadAndReusesBoundedBuffer) {
  const auto bytes = fixture(480, 800, 2);
  xtc::XtcParser parser;
  ASSERT_EQ(parser.open("/book.xtc"), xtc::XtcError::OK);
  std::vector<uint8_t> streamed;
  const uint8_t* buffer = nullptr;
  size_t callbacks = 0;
  ASSERT_EQ(parser.loadPageStreaming(
                0,
                [&](const uint8_t* data, const size_t size, const size_t offset) {
                  EXPECT_EQ(offset, streamed.size());
                  EXPECT_LE(size, 1024U);
                  if (buffer) EXPECT_EQ(data, buffer);
                  buffer = data;
                  streamed.insert(streamed.end(), data, data + size);
                  ++callbacks;
                },
                65536),
            xtc::XtcError::OK);
  const size_t start = sizeof(xtc::XtcHeader) + sizeof(xtc::PageTableEntry) + sizeof(xtc::XtgPageHeader);
  EXPECT_EQ(streamed, std::vector<uint8_t>(bytes.begin() + start, bytes.end()));
  EXPECT_GT(callbacks, 1U);
  failXtcAllocation = true;
  EXPECT_EQ(parser.loadPageStreaming(0, [](const uint8_t*, size_t, size_t) {}, 17), xtc::XtcError::OK);
  EXPECT_LE(FsFile::maxReadSize, 1024U);
}

TEST_F(XtcStreamingTest, PlanePairsPreserveGrayOrderAndPaddedBottomRows) {
  fixture(19, 9, 2);
  xtc::XtcParser parser;
  ASSERT_EQ(parser.open("/book.xtc"), xtc::XtcError::OK);
  struct State {
    size_t count = 0;
  } state;
  const auto callback = [](void* raw, const uint8_t* first, const uint8_t* second, size_t size, size_t offset) {
    auto& state = *static_cast<State*>(raw);
    EXPECT_LE(size, 512U);
    for (size_t i = 0; i < size; ++i) {
      const size_t x = 18 - (offset + i) / 2;
      const size_t yBase = ((offset + i) % 2) * 8;
      for (size_t bit = 0; bit < 8 && yBase + bit < 9; ++bit) {
        const uint8_t pixel = (((first[i] >> (7 - bit)) & 1) << 1) | ((second[i] >> (7 - bit)) & 1);
        EXPECT_EQ(pixel, x % 4);
        const uint8_t masks[] = {xtc::xtchPassMask(first[i], second[i], xtc::XtchRenderPass::Base),
                                 xtc::xtchPassMask(first[i], second[i], xtc::XtchRenderPass::Lsb),
                                 xtc::xtchPassMask(first[i], second[i], xtc::XtchRenderPass::Msb)};
        EXPECT_EQ((masks[0] >> (7 - bit)) & 1, pixel != 0);
        EXPECT_EQ((masks[1] >> (7 - bit)) & 1, pixel == 1);
        EXPECT_EQ((masks[2] >> (7 - bit)) & 1, pixel == 1 || pixel == 2);
        ++state.count;
      }
    }
  };
  EXPECT_EQ(parser.loadPagePlanePairs(0, callback, &state), xtc::XtcError::OK);
  EXPECT_EQ(state.count, 19U * 9U);
}

TEST_F(XtcStreamingTest, LegacyHeaderAndOneBitStreamingStayCompatible) {
  fixture(13, 17, 1, true);
  Xtc book("/book.xtc", "/.crosspoint");
  ASSERT_TRUE(book.load());
  ASSERT_TRUE(book.generateCoverBmp());
  const auto bytes = read(book.getCoverBmpPath());
  ASSERT_EQ(bytes.size(), sizeof(BmpHeader) + 4U * 17U);
  EXPECT_EQ(bytes[sizeof(BmpHeader)], 0x55);
  EXPECT_EQ(bytes[sizeof(BmpHeader) + 1], 0x50);
  EXPECT_EQ(bytes[sizeof(BmpHeader) + 4], 0xAA);
  EXPECT_LE(FsFile::maxReadSize, 1024U);
}

TEST_F(XtcStreamingTest, InvalidChunkTruncatedPayloadAndAllocationFailureAreRecoverable) {
  auto bytes = fixture(16, 16, 2);
  xtc::XtcParser parser;
  ASSERT_EQ(parser.open("/book.xtc"), xtc::XtcError::OK);
  EXPECT_EQ(parser.loadPageStreaming(0, [](const uint8_t*, size_t, size_t) {}, 0), xtc::XtcError::READ_ERROR);
  failXtcAllocation = true;
  EXPECT_EQ(parser.loadPageStreaming(0, [](const uint8_t*, size_t, size_t) {}), xtc::XtcError::MEMORY_ERROR);
  EXPECT_EQ(parser.getLastError(), xtc::XtcError::MEMORY_ERROR);
  EXPECT_TRUE(FsFile::openReaders.empty());
  failXtcAllocation = false;
  bytes.pop_back();
  write("/book.xtc", bytes);
  size_t callbacks = 0;
  EXPECT_NE(parser.loadPageStreaming(0, [&](const uint8_t*, size_t, size_t) { ++callbacks; }), xtc::XtcError::OK);
  EXPECT_EQ(callbacks, 0U);
  EXPECT_TRUE(FsFile::openReaders.empty());
}

TEST_F(XtcStreamingTest, TwoBitCoversAndThumbnailsPreserveNonLinearGrays) {
  fixture(800, 16, 2);
  Xtc book("/book.xtc", "/.crosspoint");
  ASSERT_TRUE(book.load());
  const std::string progress = book.getCachePath() + "/progress.bin";
  write(progress, {3, 0, 0, 0});
  write(book.getCachePath() + "/reader_settings.bin", {11, 22, 33});
  ASSERT_TRUE(book.generateCoverBmp());
  const auto cover = read(book.getCoverBmpPath());
  ASSERT_EQ(cover.size(), 70U + 200U * 16U);
  EXPECT_EQ(cover[28], 2);
  EXPECT_EQ(cover[70], 0xD8);  // white, dark gray, light gray, black => indices 3,1,2,0
  for (int y = 0; y < 16; ++y) EXPECT_EQ(cover[70 + 200 * y], 0xD8);
  ASSERT_TRUE(book.generateThumbBmp(800, 16));
  const auto thumb = read(book.getThumbBmpPath(800, 16));
  ASSERT_GT(thumb.size(), sizeof(BmpHeader));
  // Dithered dark-gray samples must be darker, on average, than light-gray samples.
  int darkWhite = 0, lightWhite = 0;
  for (int y = 0; y < 16; ++y) {
    for (int x = 0; x < 800; ++x) {
      const bool white = (thumb[sizeof(BmpHeader) + y * 100 + x / 8] >> (7 - x % 8)) & 1;
      if (x % 4 == 1) darkWhite += white;
      if (x % 4 == 2) lightWhite += white;
    }
  }
  EXPECT_LT(darkWhite, lightWhite);
  EXPECT_EQ(read(progress), (std::vector<uint8_t>{3, 0, 0, 0}));
  EXPECT_EQ(read(book.getCachePath() + "/reader_settings.bin"), (std::vector<uint8_t>{11, 22, 33}));
  EXPECT_LE(FsFile::maxReadSize, 1024U);
}

TEST_F(XtcStreamingTest, FailedCoverMigrationKeepsPreviousFileAndRecoversBackup) {
  fixture(16, 16, 2);
  Xtc book("/book.xtc", "/.crosspoint");
  ASSERT_TRUE(book.load());
  const std::vector<uint8_t> legacyCover{1, 2, 3, 4};
  write(book.getCoverBmpPath(), legacyCover);
  FsFile::writeBudget = 4;
  EXPECT_FALSE(book.generateCoverBmp());
  EXPECT_EQ(read(book.getCoverBmpPath()), legacyCover);
  FsFile::writeBudget = SIZE_MAX;
  ASSERT_TRUE(book.generateCoverBmp());
  const auto newCover = read(book.getCoverBmpPath());
  ASSERT_TRUE(Storage.rename(book.getCoverBmpPath().c_str(), (book.getCoverBmpPath() + ".bak").c_str()));
  EXPECT_TRUE(book.generateCoverBmp());
  EXPECT_EQ(read(book.getCoverBmpPath()), newCover);
}
}  // namespace
