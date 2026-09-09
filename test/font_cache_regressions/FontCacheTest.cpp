#include <EpdFont.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <SdCardFont.h>
#include <Utf8.h>
#include <builtinFonts/bookerly_12_regular.h>
#include <builtinFonts/opendyslexic_10_italic.h>
#include <builtinFonts/ubuntu_10_regular.h>
#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <new>
#include <set>

namespace {
std::atomic<size_t> allocationCount{0};
}
void* operator new(size_t size) {
  ++allocationCount;
  if (void* result = std::malloc(size ? size : 1)) return result;
  throw std::bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete[](void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, size_t) noexcept { std::free(ptr); }
void operator delete[](void* ptr, size_t) noexcept { std::free(ptr); }

namespace {
std::set<uint32_t> codepoints(const std::string& text) {
  std::set<uint32_t> result;
  const auto* p = reinterpret_cast<const unsigned char*>(text.c_str());
  while (*p) result.insert(utf8NextCodepoint(&p));
  return result;
}

TEST(FontPrewarm, MixedFontsKeepNegativeIdsAndActualStyles) {
  const std::map<int, EpdFontFamily> fonts;
  SdCardFont reader, status;
  const std::map<int, SdCardFont*> sd = {{-123456, &reader}, {7, &status}};
  FontCacheManager manager(fonts, sd);
  auto scope = manager.createPrewarmScope();
  manager.recordText("AAB", -123456, EpdFontFamily::BOLD);
  manager.recordText("b", -123456, EpdFontFamily::ITALIC);
  manager.recordText("title", 7, EpdFontFamily::REGULAR);
  ASSERT_TRUE(scope.endScanAndPrewarm());
  ASSERT_EQ(reader.calls.size(), 2);
  EXPECT_EQ(reader.calls[0].styles, 1 << EpdFontFamily::ITALIC);
  EXPECT_EQ(reader.calls[0].text, "b");
  EXPECT_EQ(reader.calls[1].styles, 1 << EpdFontFamily::BOLD);
  EXPECT_EQ(reader.calls[1].text, "AB");
  ASSERT_EQ(status.calls.size(), 1);
  EXPECT_EQ(codepoints(status.calls[0].text), codepoints("title"));
  EXPECT_FALSE(manager.isScanning());
}

TEST(FontPrewarm, FallbackStylesCoalesceWithoutLosingDecorationText) {
  const std::map<int, EpdFontFamily> fonts;
  SdCardFont font;
  font.resolved = {0, 0, 0, 0};
  const std::map<int, SdCardFont*> sd = {{-1, &font}};
  FontCacheManager manager(fonts, sd);
  auto scope = manager.createPrewarmScope();
  manager.recordText("A", -1, EpdFontFamily::BOLD);
  manager.recordText("B", -1, static_cast<EpdFontFamily::Style>(EpdFontFamily::ITALIC | EpdFontFamily::SUP));
  ASSERT_TRUE(scope.endScanAndPrewarm());
  ASSERT_EQ(font.calls.size(), 1);
  EXPECT_EQ(font.calls[0].styles, 1);
  EXPECT_EQ(font.calls[0].text, "AB");
}

TEST(FontPrewarm, SmallCapsPrewarmsRenderedUppercaseGlyphsAcrossScripts) {
  const std::map<int, EpdFontFamily> fonts;
  SdCardFont font;
  const std::map<int, SdCardFont*> sd = {{1, &font}};
  FontCacheManager manager(fonts, sd);
  auto scope = manager.createPrewarmScope();
  const auto style = static_cast<EpdFontFamily::Style>(EpdFontFamily::ITALIC | EpdFontFamily::SMALL_CAPS);
  manager.recordText("a\xC3\xA9\xC5\xBE\xCF\x82\xD1\x8F\xD2\x91", 1, style);
  ASSERT_TRUE(scope.endScanAndPrewarm());
  ASSERT_EQ(font.calls.size(), 1);
  EXPECT_EQ(codepoints(font.calls[0].text), codepoints("A\xC3\x89\xC5\xBD\xCE\xA3\xD0\xAF\xD2\x90"));
}

TEST(FontPrewarm, BuiltinFallbackUsesOneResolvedFont) {
  const EpdFontGroup group = {};
  EpdFontData data = {};
  data.groups = &group;
  const EpdFont font(&data);
  const std::map<int, EpdFontFamily> fonts = {{1, EpdFontFamily(&font)}};
  const std::map<int, SdCardFont*> sd;
  FontCacheManager manager(fonts, sd);
  FontDecompressor decompressor;
  manager.setFontDecompressor(&decompressor);
  auto scope = manager.createPrewarmScope();
  manager.recordText("A", 1, EpdFontFamily::BOLD);
  manager.recordText("B", 1, EpdFontFamily::BOLD_ITALIC);
  ASSERT_TRUE(scope.endScanAndPrewarm());
  ASSERT_EQ(decompressor.calls.size(), 1);
  EXPECT_EQ(decompressor.calls[0].font, &data);
  EXPECT_EQ(decompressor.calls[0].text, "AB");
}

TEST(FontPrewarm, ScanIsAllocationFreeAndBoundedAt512FullUnicodeEntries) {
  const std::map<int, EpdFontFamily> fonts;
  SdCardFont font;
  const std::map<int, SdCardFont*> sd = {{1, &font}};
  auto manager = std::make_unique<FontCacheManager>(fonts, sd);
  std::string text;
  for (uint32_t cp = 0x10000; cp < 0x10000 + 600; ++cp) utf8AppendCodepoint(cp, text);
  auto scope = manager->createPrewarmScope();
  const auto before = allocationCount.load();
  for (int i = 0; i < 4; ++i) manager->recordText(text.c_str(), 1, EpdFontFamily::REGULAR);
  const auto after = allocationCount.load();
  EXPECT_EQ(after, before);
  ASSERT_TRUE(scope.endScanAndPrewarm());
  ASSERT_EQ(font.calls.size(), 1);
  EXPECT_EQ(font.calls[0].text.size(), 512 * 4);
  const auto seen = codepoints(font.calls[0].text);
  EXPECT_EQ(seen.size(), 512);
  EXPECT_EQ(*seen.begin(), 0x10000);
  EXPECT_EQ(*seen.rbegin(), 0x101FF);
}

TEST(FontPrewarm, GroupTerminatorsDoNotCorruptUnreadFourByteCodepoints) {
  const std::map<int, EpdFontFamily> fonts;
  SdCardFont font;
  const std::map<int, SdCardFont*> sd = {{1, &font}};
  FontCacheManager manager(fonts, sd);
  auto scope = manager.createPrewarmScope();
  for (uint8_t style = 0; style < 4; ++style) {
    std::string text;
    for (uint32_t cp = 0x10000 + style * 128; cp < 0x10000 + (style + 1) * 128; ++cp) utf8AppendCodepoint(cp, text);
    manager.recordText(text.c_str(), 1, static_cast<EpdFontFamily::Style>(style));
  }
  ASSERT_TRUE(scope.endScanAndPrewarm());
  ASSERT_EQ(font.calls.size(), 4);
  for (size_t i = 0; i < 4; ++i) {
    const auto seen = codepoints(font.calls[i].text);
    ASSERT_EQ(seen.size(), 128);
    EXPECT_EQ(*seen.begin(), 0x10000 + (3 - i) * 128);
    EXPECT_EQ(*seen.rbegin(), 0x10000 + (4 - i) * 128 - 1);
  }
}

TEST(FontPrewarm, FontLimitAndRepeatedScopesResetCorrectly) {
  const std::map<int, EpdFontFamily> fonts;
  SdCardFont font;
  const std::map<int, SdCardFont*> sd = {{1, &font}, {2, &font}, {3, &font}, {4, &font}, {5, &font}};
  FontCacheManager manager(fonts, sd);
  {
    auto scope = manager.createPrewarmScope();
    for (int id = 1; id <= 5; ++id) manager.recordText("A", id, EpdFontFamily::REGULAR);
    ASSERT_TRUE(scope.endScanAndPrewarm());
    ASSERT_TRUE(scope.endScanAndPrewarm());
    EXPECT_EQ(font.calls.size(), 4);
  }
  auto scope = manager.createPrewarmScope();
  manager.recordText("B", 5, EpdFontFamily::REGULAR);
  ASSERT_TRUE(scope.endScanAndPrewarm());
  EXPECT_EQ(font.calls.size(), 5);
  EXPECT_EQ(font.calls.back().text, "B");
}

TEST(FontPrewarm, AllocationOrIoFailureIsReportedAndNextScopeCanRetry) {
  const std::map<int, EpdFontFamily> fonts;
  SdCardFont font;
  const std::map<int, SdCardFont*> sd = {{1, &font}};
  FontCacheManager manager(fonts, sd);
  font.result = -1;
  {
    auto scope = manager.createPrewarmScope();
    manager.recordText("A", 1, EpdFontFamily::REGULAR);
    EXPECT_FALSE(scope.endScanAndPrewarm());
  }
  font.result = 0;
  auto scope = manager.createPrewarmScope();
  manager.recordText("A", 1, EpdFontFamily::REGULAR);
  EXPECT_TRUE(scope.endScanAndPrewarm());
  EXPECT_EQ(font.calls.size(), 2);
}

TEST(FontKerning, DensePackedAndSplitSparseAgreeOnEveryPair) {
  const EpdKernClassEntry left[] = {{'A', 1}, {'T', 2}, {'Z', 3}};
  const EpdKernClassEntry right[] = {{'a', 1}, {'o', 2}, {'z', 3}};
  const int8_t matrix[] = {-128, 0, 127, 0, -7, 0, 0, 0, 0};
  const uint16_t leftCp[] = {'A', 'T', 'Z'}, rightCp[] = {'a', 'o', 'z'};
  const uint8_t classes[] = {1, 2, 3};
  const uint16_t rows[] = {0, 2, 3, 3};
  const uint8_t cols[] = {0, 2, 1};
  const int8_t values[] = {-128, 127, -7};
  EpdFontData dense = {};
  dense.kernLeftClasses = left;
  dense.kernRightClasses = right;
  dense.kernMatrix = matrix;
  dense.kernLeftEntryCount = dense.kernRightEntryCount = 3;
  dense.kernLeftClassCount = dense.kernRightClassCount = 3;
  EpdFontData sparse = dense;
  sparse.kernMatrix = nullptr;
  sparse.kernLeftClasses = sparse.kernRightClasses = nullptr;
  sparse.kernLeftCodepoints = leftCp;
  sparse.kernRightCodepoints = rightCp;
  sparse.kernLeftClassIds = sparse.kernRightClassIds = classes;
  sparse.kernRowOffsets = rows;
  sparse.kernSparseCols = cols;
  sparse.kernSparseValues = values;
  EpdFont a(&dense), b(&sparse);
  for (uint32_t l = 0; l <= 127; ++l)
    for (uint32_t r = 0; r <= 127; ++r) ASSERT_EQ(a.getKerning(l, r), b.getKerning(l, r));
  EXPECT_EQ(b.getKerning(0x10041, 'a'), 0);
  EXPECT_EQ(b.getKerning('A', 0x10061), 0);
  sparse.kernSparseCols = nullptr;
  EXPECT_EQ(b.getKerning('A', 'a'), 0);
  sparse = dense;
  sparse.kernLeftClassCount = 0;
  EXPECT_EQ(b.getKerning('A', 'a'), 0);
}

TEST(FontKerning, GeneratedCprHeadersKeepTheirOriginalSpacing) {
  // Expected 4.4 values captured from CPR's original dense headers.
  const EpdFont bookerly(&bookerly_12_regular);
  EXPECT_EQ(bookerly.getKerning('A', 'V'), -44);
  EXPECT_EQ(bookerly.getKerning('T', 'o'), -24);
  EXPECT_EQ(bookerly.getKerning('V', 'A'), -46);
  const EpdFont dyslexic(&opendyslexic_10_italic);
  EXPECT_EQ(dyslexic.getKerning('A', 'V'), -128);
  EXPECT_EQ(dyslexic.getKerning('T', 'o'), -123);
  EXPECT_EQ(dyslexic.getKerning('V', 'A'), -128);
  const EpdFont ui(&ubuntu_10_regular);
  EXPECT_EQ(ui.getKerning('A', 'V'), -21);
  EXPECT_EQ(ui.getKerning('T', 'o'), -18);
  EXPECT_EQ(ui.getKerning('V', 'A'), -21);
}
}  // namespace
