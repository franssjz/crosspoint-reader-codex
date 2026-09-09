#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>

#include "src/network/WebUploadSize.h"
#include "src/util/WebPath.h"

namespace {
std::string normalized(std::string_view input) {
  std::string output(input.size() + 2, '\0');
  size_t length = 0;
  if (!WebPath::normalize(input, output.data(), output.size(), length)) return {};
  output.resize(length);
  return output;
}

std::string decoded(std::string_view input) {
  std::string output(input.size() + 1, '\0');
  size_t length = 0;
  if (!WebPath::decodeUrlPath(input, output.data(), output.size(), length)) return {};
  output.resize(length);
  return output;
}

std::string filename(std::string_view input) {
  std::string output(input.size() + 1, '\0');
  size_t length = 0;
  if (!WebPath::sanitizeFilename(input, output.data(), output.size(), length)) return {};
  output.resize(length);
  return output;
}
}  // namespace

TEST(WebUploadSize, RejectsOverflowBeforeStartingAnUploadAndPreservesPlusSyntax) {
  uint32_t result = 123;
  ASSERT_TRUE(WebUploadSize::parse("+4294967295", result));
  EXPECT_EQ(result, UINT32_MAX);
  for (const auto* invalid : {"4294967296", "999999999999999999999999", "", "+", "-1", " 1", "1x"}) {
    EXPECT_FALSE(WebUploadSize::parse(invalid, result));
    EXPECT_EQ(result, UINT32_MAX);
  }
  EXPECT_TRUE(WebUploadSize::parse("000", result));
  EXPECT_EQ(result, 0u);
}

TEST(WebPath, CanonicalizesSeparatorsAndDotSegments) {
  EXPECT_EQ(normalized(""), "/");
  EXPECT_EQ(normalized("//"), "/");
  EXPECT_EQ(normalized("Books//Fiction/./book.epub/"), "/Books/Fiction/book.epub");
  EXPECT_EQ(normalized("\\Books\\one\\..\\two.epub"), "/Books/two.epub");
  EXPECT_EQ(normalized("/Books/../other.epub"), "/other.epub");
}

TEST(WebPath, RejectsEscapingRootAndAmbiguousFatNames) {
  for (const auto* path : {"../book.epub", "/../../book.epub", "/Books/../../file", "/XTCache./file",
                           "/System Volume Information /file", "/Books/name?", "/Books/file:stream"}) {
    EXPECT_TRUE(normalized(path).empty()) << path;
  }
}

TEST(WebPath, ProtectsAllAncestorsCaseInsensitively) {
  for (const auto* path : {"/.crosspoint/credentials.json", "/Books/.private/book.epub", "/xtcache/a/b.epub",
                           "/system volume information/ordinary.txt", "/Books/../.crosspoint/data"}) {
    EXPECT_TRUE(WebPath::isProtected(normalized(path))) << path;
  }
  EXPECT_FALSE(WebPath::isProtected(normalized("/Books/XTCache-not-system/book.epub")));
  EXPECT_FALSE(WebPath::isProtected(normalized("/Books/My.Book.epub")));
  EXPECT_FALSE(WebPath::isProtected("/"));
  EXPECT_TRUE(WebPath::isProtected(""));
}

TEST(WebPath, Utf8AndLongNamesKeepTheirExactExtension) {
  const std::string unicode = "El corazón — 日本語 + 100%.epub";
  const std::string longName = std::string(180, 'a') + " — 日本語.epub";
  EXPECT_EQ(filename(unicode), unicode);
  EXPECT_EQ(filename(longName), longName);
  EXPECT_EQ(normalized("/Books/" + unicode), "/Books/" + unicode);
}

TEST(WebPath, SanitizesOnlyFilenameSyntaxWithoutAddingDirectories) {
  EXPECT_EQ(filename("folder/other\\book?.epub"), "folder_other_book_.epub");
  EXPECT_EQ(filename("Book: Volume 1.epub"), "Book_ Volume 1.epub");
  EXPECT_TRUE(filename(".").empty());
  EXPECT_TRUE(filename("..").empty());
  EXPECT_TRUE(filename("").empty());
  EXPECT_TRUE(WebPath::isProtected("/" + filename(".secret.epub")));
  EXPECT_EQ(filename("book.epub. "), "book.epub");
}

TEST(WebPath, RejectsNulAndControlBytesWithoutAcceptingPrefix) {
  const std::string nul("/Books/ok.epub\0/secret", 22);
  EXPECT_TRUE(normalized(nul).empty());
  EXPECT_TRUE(filename(std::string("ok\0.epub", 8)).empty());
  EXPECT_TRUE(normalized("/Books/name\r\n.epub").empty());
  EXPECT_TRUE(decoded("/Books/book%00.epub").empty());
  EXPECT_TRUE(decoded("/Books/book%0D%0A.epub").empty());
}

TEST(WebPath, DecodesWebDavUrlExactlyOnceAndKeepsPlusLiteral) {
  EXPECT_EQ(decoded("/Books/A+B%20C.epub"), "/Books/A+B C.epub");
  EXPECT_EQ(decoded("/Books/100%25.epub"), "/Books/100%.epub");
  EXPECT_EQ(decoded("/Books/%252e%252e.epub"), "/Books/%2e%2e.epub");
  EXPECT_TRUE(WebPath::isProtected(normalized(decoded("/%2ecrosspoint%2fcredentials.json"))));
  EXPECT_TRUE(WebPath::isProtected(normalized(decoded("/Books/%2e%2e/.crosspoint/data"))));
  EXPECT_TRUE(normalized(decoded("/%2e%2e/book.epub")).empty());
  EXPECT_TRUE(WebPath::isProtected(normalized(decoded("%5c.crosspoint%5cdata"))));
}

TEST(WebPath, RejectsMalformedPercentEncoding) {
  for (const auto* path : {"/%", "/%0", "/%GG", "/abc%2z"}) EXPECT_TRUE(decoded(path).empty()) << path;
}

TEST(WebPath, CapacityFailureNeverWritesBeyondDestination) {
  std::array<char, 8> output{};
  output.fill('!');
  size_t length = 0;
  EXPECT_FALSE(WebPath::normalize("/book.epub", output.data(), 7, length));
  EXPECT_EQ(output.back(), '!');
  EXPECT_FALSE(WebPath::sanitizeFilename("book.epub", output.data(), 7, length));
  EXPECT_EQ(output.back(), '!');
  EXPECT_FALSE(WebPath::decodeUrlPath("/book.epub", output.data(), 7, length));
  EXPECT_EQ(output.back(), '!');
  EXPECT_FALSE(WebPath::normalize("/", nullptr, 0, length));
}

TEST(WebPath, CanonicalizationIsIdempotentAcrossNestedPaths) {
  const char* components[] = {"Books", "日本語", ".", "..", "", "volume.epub", ".private"};
  for (const auto* first : components) {
    for (const auto* second : components) {
      for (const auto* third : components) {
        const auto result = normalized(std::string("/") + first + "/" + second + "/" + third);
        if (!result.empty()) EXPECT_EQ(normalized(result), result);
      }
    }
  }
}
