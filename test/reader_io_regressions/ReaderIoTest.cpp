#include <Epub.h>
#include <Epub/Page.h>
#include <Epub/parsers/ChapterHtmlSlimParser.h>
#include <GfxRenderer.h>
#include <ZipFile.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace {
class BoundedOutput final : public Print {
 public:
  std::vector<uint8_t> bytes;
  bool invalidWrite = false;
  size_t write(const uint8_t* input, size_t count) override {
    if (count > 4096 || bytes.size() + count > 4096) {
      invalidWrite = true;
      return 0;
    }
    bytes.insert(bytes.end(), input, input + count);
    return count;
  }
};

struct ZipFixture {
  std::vector<uint8_t> bytes;
  size_t dataOffset = 0;
  size_t centralOffset = 0;
  static constexpr const char* itemName = "page.xhtml";
  explicit ZipFixture(const std::string& text, bool deflated) {
    const auto append = [&](uint32_t value, size_t length) {
      for (size_t i = 0; i < length; ++i) bytes.push_back(static_cast<uint8_t>(value >> (8 * i)));
    };
    std::vector<uint8_t> payload(text.begin(), text.end());
    if (deflated) {
      const uint16_t length = static_cast<uint16_t>(text.size());
      const uint16_t inverse = static_cast<uint16_t>(~length);
      payload = {1, static_cast<uint8_t>(length), static_cast<uint8_t>(length >> 8), static_cast<uint8_t>(inverse),
                 static_cast<uint8_t>(inverse >> 8)};
      payload.insert(payload.end(), text.begin(), text.end());
    }
    const size_t nameLength = std::strlen(itemName);
    append(0x04034b50, 4);
    append(20, 2);
    append(0, 2);
    append(deflated ? 8 : 0, 2);
    append(0, 4);
    append(0, 4);
    append(static_cast<uint32_t>(payload.size()), 4);
    append(static_cast<uint32_t>(text.size()), 4);
    append(static_cast<uint32_t>(nameLength), 2);
    append(0, 2);
    bytes.insert(bytes.end(), itemName, itemName + nameLength);
    dataOffset = bytes.size();
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    centralOffset = bytes.size();
    append(0x02014b50, 4);
    append(20, 2);
    append(20, 2);
    append(0, 2);
    append(deflated ? 8 : 0, 2);
    append(0, 4);
    append(0, 4);
    append(static_cast<uint32_t>(payload.size()), 4);
    append(static_cast<uint32_t>(text.size()), 4);
    append(static_cast<uint32_t>(nameLength), 2);
    append(0, 2);
    append(0, 2);
    append(0, 2);
    append(0, 2);
    append(0, 4);
    append(0, 4);
    bytes.insert(bytes.end(), itemName, itemName + nameLength);
    const size_t centralSize = bytes.size() - centralOffset;
    append(0x06054b50, 4);
    append(0, 2);
    append(0, 2);
    append(1, 2);
    append(1, 2);
    append(static_cast<uint32_t>(centralSize), 4);
    append(static_cast<uint32_t>(centralOffset), 4);
    append(0, 2);
  }
};

class ZipReadTest : public ::testing::TestWithParam<bool> {
 protected:
  const std::string path = "/test.epub";
  const std::string text = "Chapter text: retained without damage.";
  void SetUp() override { Storage.reset(); }
};

TEST_P(ZipReadTest, ReadsRealStoredAndDeflateDataThroughAllEntryPoints) {
  ZipFixture fixture(text, GetParam());
  Storage.put(path, fixture.bytes);
  ZipFile zip(path);
  size_t size = 0;
  std::unique_ptr<uint8_t, decltype(&std::free)> memory(zip.readFileToMemory(ZipFixture::itemName, &size, true),
                                                        &std::free);
  ASSERT_NE(memory, nullptr);
  EXPECT_EQ(size, text.size());
  EXPECT_STREQ(reinterpret_cast<const char*>(memory.get()), text.c_str());
  BoundedOutput output;
  ASSERT_TRUE(zip.readFileToStream(ZipFixture::itemName, output, 3));
  EXPECT_EQ(std::string(output.bytes.begin(), output.bytes.end()), text);
  EXPECT_FALSE(output.invalidWrite);
  std::array<uint8_t, 7> prefix{};
  ASSERT_TRUE(zip.readFilePrefixToBuffer(ZipFixture::itemName, prefix.data(), prefix.size(), &size, 3));
  EXPECT_EQ(size, prefix.size());
  EXPECT_EQ(std::string(prefix.begin(), prefix.end()), text.substr(0, prefix.size()));
}

TEST_P(ZipReadTest, SignedPayloadReadErrorsNeverBecomeOutputLengths) {
  ZipFixture fixture(text, GetParam());
  auto data = Storage.put(path, fixture.bytes);
  data->failOffset = fixture.dataOffset;
  ZipFile zip(path);
  BoundedOutput output;
  EXPECT_FALSE(zip.readFileToStream(ZipFixture::itemName, output, 3));
  EXPECT_TRUE(output.bytes.empty());
  EXPECT_FALSE(output.invalidWrite);
  size_t size = 100;
  std::array<uint8_t, 8> prefix{};
  EXPECT_FALSE(zip.readFilePrefixToBuffer(ZipFixture::itemName, prefix.data(), prefix.size(), &size, 3));
  EXPECT_EQ(size, 0u);
  EXPECT_EQ(zip.readFileToMemory(ZipFixture::itemName), nullptr);
  // Failed reads have not written to the original archive.
  EXPECT_EQ(data->bytes, fixture.bytes);
}

TEST_P(ZipReadTest, ZeroLengthReadFailsWithoutRetryLoopOrBogusSuccess) {
  ZipFixture fixture(text, GetParam());
  auto data = Storage.put(path, fixture.bytes);
  data->failOffset = fixture.dataOffset;
  data->failure = 0;
  ZipFile zip(path);
  BoundedOutput output;
  EXPECT_FALSE(zip.readFileToStream(ZipFixture::itemName, output, 3));
  EXPECT_FALSE(output.invalidWrite);
  EXPECT_LT(data->reads, 40u);
}

TEST_P(ZipReadTest, RejectsZeroStreamChunkSize) {
  Storage.put(path, ZipFixture(text, GetParam()).bytes);
  ZipFile zip(path);
  BoundedOutput output;
  EXPECT_FALSE(zip.readFileToStream(ZipFixture::itemName, output, 0));
  EXPECT_TRUE(output.bytes.empty());
}

INSTANTIATE_TEST_SUITE_P(StoredAndDeflated, ZipReadTest, ::testing::Bool());

TEST(ZipMetadataRead, RejectsSignedAndShortEocdReadsBeforeScanningBuffer) {
  for (int readResult : {-1, 1}) {
    Storage.reset();
    const std::string path = "/metadata.epub";
    auto data = Storage.put(path, ZipFixture("text", false).bytes);
    data->failOffset = 0;
    data->failure = readResult;
    ZipFile zip(path);
    size_t size = 0;
    EXPECT_FALSE(zip.getInflatedFileSize(ZipFixture::itemName, &size));
    EXPECT_EQ(size, 0u);
  }
}

TEST(ZipMetadataRead, RejectsBrokenCentralFieldsAndNamesAcrossLookupApis) {
  for (size_t relativeOffset : {size_t{10}, size_t{28}, size_t{46}}) {
    Storage.reset();
    const std::string path = "/metadata.epub";
    ZipFixture fixture("text", false);
    auto data = Storage.put(path, fixture.bytes);
    data->failOffset = fixture.centralOffset + relativeOffset;
    ZipFile zip(path);
    size_t size = 0;
    EXPECT_FALSE(zip.getInflatedFileSize(ZipFixture::itemName, &size));
    EXPECT_FALSE(zip.loadAllFileStatSlims());
    if (relativeOffset != 10) {
      size_t enumerated = 0;
      EXPECT_FALSE(zip.enumerateFilePaths([&](std::string_view) { ++enumerated; }));
      EXPECT_EQ(enumerated, 0u);
    }
    std::deque<ZipFile::SizeTarget> targets{{ZipFile::fnvHash64(ZipFixture::itemName, 10), 10, 0}};
    std::deque<uint32_t> sizes{123};
    EXPECT_EQ(zip.fillUncompressedSizes(targets, sizes), 0);
    EXPECT_EQ(sizes.front(), 123u);
    // A retry after the media recovers must not see partially cached metadata.
    data->failOffset = std::numeric_limits<size_t>::max();
    EXPECT_TRUE(zip.getInflatedFileSize(ZipFixture::itemName, &size));
    EXPECT_EQ(size, 4u);
  }
}

struct ParsedChapter {
  bool success = false;
  std::vector<std::pair<std::string, EpdFontFamily::Style>> words;
  std::vector<FootnoteEntry> notes;
};

ParsedChapter parseChapter(const std::string& markup, bool signedReadFailure = false, const std::string& css = "") {
  Storage.reset();
  const std::string filepath = "/chapter.xhtml";
  auto data = Storage.put(filepath, markup);
  if (signedReadFailure) data->failOffset = 0;
  auto epub = std::make_shared<Epub>("/book.epub", "/cache");
  GfxRenderer renderer;
  ParsedChapter result;
  CssParser cssParser("/cache");
  if (!css.empty()) {
    Storage.put("/style.css", css);
    HalFile cssFile;
    if (!Storage.openFileForRead("TEST", "/style.css", cssFile) || !cssParser.loadFromStream(cssFile)) return result;
    cssFile.close();
  }
  ChapterHtmlSlimParser parser(
      epub, filepath, renderer, 0, 1.0f, false, false, static_cast<uint8_t>(CssTextAlign::Left), 480, 800, false, false,
      0,
      [&](std::unique_ptr<Page> page, ChapterHtmlSlimParser::ParagraphLutEntry) {
        for (const auto& element : page->elements) {
          if (element->getTag() != TAG_PageLine) continue;
          const auto& block = static_cast<PageLine&>(*element).getBlock();
          for (uint16_t i = 0; i < block->wordCount(); ++i) {
            result.words.emplace_back(block->wordText(i), block->wordStyle(i));
          }
        }
        result.notes.insert(result.notes.end(), page->footnotes.begin(), page->footnotes.end());
      },
      true, "", "/cache", 1, {}, nullptr, &cssParser);
  result.success = parser.parseAndBuildPages();
  return result;
}

TEST(ChapterMarkup, PreservesContentBeforeTrailingJunkWithinAndAcrossParseChunks) {
  for (size_t padding : {size_t{0}, size_t{1100}}) {
    const std::string valid = "<html><body><p>Retained chapter text.</p></body></html>";
    const auto chapter = parseChapter(valid + std::string(padding, ' ') + "junk\x01");
    ASSERT_TRUE(chapter.success);
    ASSERT_EQ(chapter.words.size(), 3u);
    EXPECT_EQ(chapter.words[0].first, "Retained");
    EXPECT_EQ(chapter.words[2].first, "text.");
  }
}

TEST(ChapterMarkup, AcceptsOrdinaryHtmlAndLegalTrailingComments) {
  const auto chapter = parseChapter("<html><body><p>Normal text.</p></body></html><!-- legal comment -->");
  ASSERT_TRUE(chapter.success);
  ASSERT_EQ(chapter.words.size(), 2u);
  EXPECT_EQ(chapter.words[0].first, "Normal");
}

TEST(ChapterMarkup, RecognizesNamespacedRootClose) {
  const auto chapter = parseChapter(
      "<x:html xmlns:x='http://www.w3.org/1999/xhtml'><x:body><x:p>Namespaced text</x:p></x:body></x:html>junk");
  ASSERT_TRUE(chapter.success);
  ASSERT_EQ(chapter.words.size(), 2u);
  EXPECT_EQ(chapter.words[0].first, "Namespaced");
}

TEST(ChapterMarkup, DoesNotMaskMalformedOrTruncatedMarkupBeforeRootClose) {
  for (const std::string& markup : {"<html><body><p>Missing end", "<html><body><p>Wrong close</div></body></html>",
                                    "<root><html><body>Inner html</body></html><broken</root>",
                                    "<html><body><p>Bad control \x01</p></body></html>"}) {
    EXPECT_FALSE(parseChapter(markup).success) << markup;
  }
}

TEST(ChapterMarkup, SignedReadFailureIsAnErrorNotAChunkLength) {
  EXPECT_FALSE(parseChapter("<html><body><p>Valid chapter</p></body></html>", true).success);
}

TEST(ChapterMarkup, KeepsFootnoteVerticalAlignAndTarget) {
  for (const auto& [position, flag] : std::vector<std::pair<std::string, EpdFontFamily::Style>>{
           {"super", EpdFontFamily::SUP}, {"sub", EpdFontFamily::SUB}}) {
    const auto chapter = parseChapter("<html><body><p>Text <a href='#note' style='vertical-align:" + position +
                                      "'>1</a> after</p></body></html>");
    ASSERT_TRUE(chapter.success);
    const auto number =
        std::find_if(chapter.words.begin(), chapter.words.end(), [](const auto& word) { return word.first == "1"; });
    ASSERT_NE(number, chapter.words.end());
    EXPECT_NE(static_cast<uint8_t>(number->second) & static_cast<uint8_t>(flag), 0u);
    ASSERT_EQ(chapter.notes.size(), 1u);
    EXPECT_STREQ(chapter.notes.front().href, "#note");
    EXPECT_STREQ(chapter.notes.front().number, "1");
    EXPECT_EQ(chapter.words.back().second, EpdFontFamily::REGULAR);
  }
}

TEST(ChapterMarkup, PreservesNestedSupTagsAndOrdinaryLinks) {
  for (const std::string& link : {"<sup><a href='#note'>1</a></sup>", "<a href='#note'><sup>1</sup></a>"}) {
    const auto chapter = parseChapter("<html><body><p>Text " + link + " after</p></body></html>");
    ASSERT_TRUE(chapter.success);
    const auto number =
        std::find_if(chapter.words.begin(), chapter.words.end(), [](const auto& word) { return word.first == "1"; });
    ASSERT_NE(number, chapter.words.end());
    EXPECT_NE(static_cast<uint8_t>(number->second) & EpdFontFamily::SUP, 0u);
  }
  const auto plain = parseChapter("<html><body><p><a href='#note'>1</a> after</p></body></html>");
  ASSERT_TRUE(plain.success);
  EXPECT_EQ(static_cast<uint8_t>(plain.words.front().second) & (EpdFontFamily::SUP | EpdFontFamily::SUB), 0u);
}

TEST(ChapterMarkup, AppliesStylesheetSuperscriptToFootnoteLinks) {
  const auto chapter = parseChapter("<html><body><p>Text <a href='#note' class='noteref'>1</a> after</p></body></html>",
                                    false, ".noteref { vertical-align: super; }");
  ASSERT_TRUE(chapter.success);
  const auto number =
      std::find_if(chapter.words.begin(), chapter.words.end(), [](const auto& word) { return word.first == "1"; });
  ASSERT_NE(number, chapter.words.end());
  EXPECT_NE(static_cast<uint8_t>(number->second) & EpdFontFamily::SUP, 0u);
  ASSERT_EQ(chapter.notes.size(), 1u);
  EXPECT_STREQ(chapter.notes.front().href, "#note");
}

TEST(ChapterMarkup, SmallCapsInheritsAllowsNormalAndWorksInTableCells) {
  const auto chapter = parseChapter(
      "<html><body><p class='caps'>One <span style='font-variant-caps:normal'>Two</span> Three</p>"
      "<table><tr><td class='caps'>Four</td></tr></table><p>Five</p></body></html>",
      false, ".caps { font-variant: small-caps; }");
  ASSERT_TRUE(chapter.success);
  ASSERT_EQ(chapter.words.size(), 5u);
  const auto hasSmallCaps = [](const auto& word) {
    return (static_cast<uint8_t>(word.second) & EpdFontFamily::SMALL_CAPS) != 0;
  };
  EXPECT_TRUE(hasSmallCaps(chapter.words[0]));
  EXPECT_FALSE(hasSmallCaps(chapter.words[1]));
  EXPECT_TRUE(hasSmallCaps(chapter.words[2]));
  EXPECT_TRUE(hasSmallCaps(chapter.words[3]));
  EXPECT_FALSE(hasSmallCaps(chapter.words[4]));
}

TEST(ChapterMarkup, SkipsOnlyExplicitKindleFallbackAndKeepsFollowingText) {
  const auto chapter = parseChapter(
      "<html><body><p>Before</p><img src='primary.jpg' alt='PRIMARY'/>"
      "<img src='old.jpg' alt='FALLBACK' data-AmznRemoved-M8='true'/>"
      "<image data-AmznRemoved-M8='true'><span>FALLBACKCHILD</span></image>"
      "<img src='normal.jpg' alt='UNRELATED' data-AmznRemoved-M8extra='true'/>"
      "<p>After</p></body></html>");
  ASSERT_TRUE(chapter.success);
  std::string text;
  for (const auto& word : chapter.words) text += word.first + " ";
  EXPECT_NE(text.find("PRIMARY"), std::string::npos);
  EXPECT_NE(text.find("UNRELATED"), std::string::npos);
  EXPECT_EQ(text.find("FALLBACK"), std::string::npos);
  EXPECT_NE(text.find("Before"), std::string::npos);
  EXPECT_NE(text.find("After"), std::string::npos);
}
}  // namespace
