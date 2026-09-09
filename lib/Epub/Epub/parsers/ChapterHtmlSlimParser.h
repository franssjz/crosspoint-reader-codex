#pragma once

#include <HalStorage.h>
#include <expat.h>

#include <climits>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../FootnoteEntry.h"
#include "../ParsedText.h"
#include "../blocks/ImageBlock.h"
#include "../blocks/TextBlock.h"
#include "../converters/ImageToFramebufferDecoder.h"
#include "../css/CssParser.h"
#include "../css/CssStyle.h"

class Page;
class GfxRenderer;
class Epub;

#define MAX_WORD_SIZE 200

class ChapterHtmlSlimParser {
 public:
  struct ParagraphLutEntry {
    uint32_t xhtmlByteOffset = 0;
    uint16_t paragraphIndex = 0;
    uint16_t listItemIndex = 0;
    uint32_t visibleTextOffset = 0;
  };

 private:
  static constexpr uint8_t MAX_SIMPLE_TABLE_COLUMNS = 8;
  static constexpr uint16_t MAX_SIMPLE_TABLE_ROWS = 64;
  static constexpr uint16_t MAX_SIMPLE_TABLE_CELLS = 64;
  static constexpr uint16_t MAX_SIMPLE_TABLE_CELL_WORDS = 160;
  static constexpr uint8_t MAX_GRID_TABLES_PER_CHAPTER = 4;
  static constexpr uint8_t TABLE_CELL_PADDING = 6;

  std::shared_ptr<Epub> epub;
  const std::string& filepath;
  GfxRenderer& renderer;
  std::function<void(std::unique_ptr<Page>, ParagraphLutEntry)> completePageFn;
  std::function<void()> popupFn;      // Popup callback
  XML_Parser activeParser = nullptr;  // Expat parser used to capture byte offsets for sync LUT hints
  XML_Parser xmlParser_ = nullptr;
  HalFile parseFile_;
  uint32_t parseStartTime_ = 0;
  int depth = 0;
  int skipUntilDepth = INT_MAX;
  int boldUntilDepth = INT_MAX;
  int italicUntilDepth = INT_MAX;
  int underlineUntilDepth = INT_MAX;
  int strikeUntilDepth = INT_MAX;
  // buffer for building up words from characters, will auto break if longer than this
  // leave one char at end for null pointer
  char partWordBuffer[MAX_WORD_SIZE + 1] = {};
  int partWordBufferIndex = 0;
  bool nextWordContinues = false;  // true when next flushed word attaches to previous (inline element boundary)
  std::unique_ptr<ParsedText> currentTextBlock = nullptr;
  bool inRuby = false;
  int rubyStartWordIndex = -1;
  bool collectingRubyText = false;
  std::string rubyTextBuffer;
  std::unique_ptr<Page> currentPage = nullptr;
  int16_t currentPageNextY = 0;
  int fontId;
  float lineCompression;
  bool extraParagraphSpacing;
  bool forceParagraphIndents;
  uint8_t paragraphAlignment;
  uint16_t viewportWidth;
  uint16_t viewportHeight;
  bool hyphenationEnabled;
  bool focusReadingEnabled;
  uint8_t wordSpacing;
  CssParser* cssParser;
  bool embeddedStyle;
  uint8_t imageRendering;
  std::string contentBase;
  std::string imageBasePath;
  int imageCounter = 0;
  bool lowMemoryImageFallback = false;
  bool lowMemoryAbort = false;
  bool htmlEnded_ = false;
  bool attemptedTextLayoutFontCacheRelease = false;
  bool loggedSoftLowMemoryContinuation = false;

  std::string lastImageDimensionsPath;
  ImageDimensions lastImageDimensions = {0, 0};
  bool hasLastImageDimensions = false;
  std::string lastRenderedImagePath;
  uint16_t lastRenderedImageCount = 0;
  uint32_t lastLongParseServiceMs = 0;

  // Style tracking (replaces depth-based approach)
  struct StyleStackEntry {
    int depth = 0;
    bool hasBold = false, bold = false;
    bool hasItalic = false, italic = false;
    bool hasUnderline = false, underline = false;
    bool hasStrikeThrough = false, strikeThrough = false;
    bool hasSup = false, sup = false;
    bool hasSub = false, sub = false;
    bool hasSmallCaps = false, smallCaps = false;
  };
  std::vector<StyleStackEntry> inlineStyleStack;
  std::vector<BlockStyle> blockStyleStack;  // accumulated block styles from open ancestor elements
  CssStyle currentCssStyle;
  bool effectiveBold = false;
  bool effectiveItalic = false;
  bool effectiveUnderline = false;
  bool effectiveStrikeThrough = false;
  bool effectiveSup = false;
  bool effectiveSub = false;
  bool effectiveSmallCaps = false;

  struct BufferedTableCell {
    std::unique_ptr<ParsedText> text;
    std::vector<std::pair<int, FootnoteEntry>> footnotes;
    bool isHeader = false;
    uint8_t colSpan = 1;
  };

  struct BufferedTableRow {
    std::vector<BufferedTableCell> cells;
    bool hasHeaderCell = false;
    bool hasDataCell = false;
    uint16_t effectiveColumnCount = 0;
  };

  struct BufferedTable {
    BlockStyle blockStyle;
    std::vector<BufferedTableRow> rows;
    uint16_t maxCols = 0;
    uint16_t totalCells = 0;
    bool unsupported = false;
  };

  int tableDepth = 0;
  int tableRowIndex = 0;
  int tableColIndex = 0;
  uint8_t tableGridCandidatesInChapter = 0;
  bool currentTableCellIsHeader = false;
  uint8_t currentTableCellColSpan = 1;
  std::unique_ptr<BufferedTable> currentTableBuffer = nullptr;
  bool listItemBulletOnly = false;  // true when currentTextBlock has only the <li> bullet

  // Anchor-to-page mapping: tracks which page each HTML id attribute lands on
  int completedPageCount = 0;
  std::deque<std::pair<std::string, uint16_t>> anchorData;
  std::string pendingAnchorId;          // deferred until after previous text block is flushed
  std::vector<std::string> tocAnchors;  // the list of anchors that are TOC chapter boundaries
  std::vector<std::string> referencedAnchors;
  int xpathBodyDepth = -1;
  uint32_t lastBodyChildByteOffset = 0;
  uint16_t xpathParagraphIndex = 0;
  uint16_t xpathListItemIndex = 0;
  // Stable source position: zero-based Unicode codepoints in visible body text.
  uint32_t visibleTextOffset = 0;
  uint32_t partWordVisibleOffset = 0;
  uint32_t currentPageVisibleOffset = 0;
  bool currentPageVisibleOffsetSet = false;
  bool insideBody = false;
  bool syntheticCharacterData = false;
  uint16_t nonVisibleTextDepth = 0;

  // Footnote link tracking
  bool insideFootnoteLink = false;
  int footnoteLinkDepth = -1;
  FootnoteEntry currentFootnote = {};
  int currentFootnoteLinkTextLen = 0;
  std::vector<std::pair<int, FootnoteEntry>> pendingFootnotes;  // <wordIndex, entry>
  int wordsExtractedInBlock = 0;

  void updateEffectiveInlineStyle();
  static void applyVerticalAlignToEntry(StyleStackEntry& entry, const CssStyle& style);
  static void applySmallCapsToEntry(StyleStackEntry& entry, const CssStyle& style);
  bool shouldAbortForLowMemory(const char* stage);
  bool startNewPage(const char* reason);
  void collectReferencedAnchors();
  void collectReferencedAnchor(const char* href);
  bool isReferencedAnchor(const std::string& anchor) const;
  bool shouldRecordAnchor(const char* elementName, const std::string& anchor) const;
  bool readImageDimensions(const std::string& resolvedPath, const std::string& cachedImagePath, ImageDimensions& dims);
  bool shouldSuppressRepeatedImage(const std::string& resolvedPath);
  void serviceLongParse(const char* stage);
  void startNewTextBlock(const BlockStyle& blockStyle);
  void flushPendingAnchor();
  void flushPartWordBuffer();
  void flushLongTextBlockIfNeeded();
  void setCurrentPageVisibleOffset(uint32_t offset);
  void makePages();
  void emitPage(uint32_t xhtmlByteOffset);
  void emitHorizontalRule(const BlockStyle& blockStyle);
  void finalizeCurrentTableCell();
  void emitBufferedTableAsParagraphs(BufferedTable& table);
  void emitBufferedTableAsFragments(BufferedTable& table);
  void emitCurrentTableBuffer();
  void fallbackCurrentTableBufferToParagraphs(const char* reason);
  void fallbackCurrentTableBufferIfNeeded(const char* stage);
  // XML callbacks
  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void XMLCALL characterData(void* userData, const XML_Char* s, int len);
  static void XMLCALL defaultHandlerExpand(void* userData, const XML_Char* s, int len);
  static void XMLCALL endElement(void* userData, const XML_Char* name);

 public:
  explicit ChapterHtmlSlimParser(std::shared_ptr<Epub> epub, const std::string& filepath, GfxRenderer& renderer,
                                 const int fontId, const float lineCompression, const bool extraParagraphSpacing,
                                 const bool forceParagraphIndents, const uint8_t paragraphAlignment,
                                 const uint16_t viewportWidth, const uint16_t viewportHeight,
                                 const bool hyphenationEnabled, const bool focusReadingEnabled,
                                 const uint8_t wordSpacing,
                                 const std::function<void(std::unique_ptr<Page>, ParagraphLutEntry)>& completePageFn,
                                 const bool embeddedStyle, const std::string& contentBase,
                                 const std::string& imageBasePath, const uint8_t imageRendering = 0,
                                 std::vector<std::string> tocAnchors = {},
                                 const std::function<void()>& popupFn = nullptr, CssParser* cssParser = nullptr)

      : epub(epub),
        filepath(filepath),
        renderer(renderer),
        fontId(fontId),
        lineCompression(lineCompression),
        extraParagraphSpacing(extraParagraphSpacing),
        forceParagraphIndents(forceParagraphIndents),
        paragraphAlignment(paragraphAlignment),
        viewportWidth(viewportWidth),
        viewportHeight(viewportHeight),
        hyphenationEnabled(hyphenationEnabled),
        focusReadingEnabled(focusReadingEnabled),
        wordSpacing(std::min<uint8_t>(wordSpacing, 4)),
        completePageFn(completePageFn),
        popupFn(popupFn),
        cssParser(cssParser),
        embeddedStyle(embeddedStyle),
        imageRendering(imageRendering),
        contentBase(contentBase),
        imageBasePath(imageBasePath),
        tocAnchors(std::move(tocAnchors)) {}

  ~ChapterHtmlSlimParser();
  bool parseAndBuildPages();
  enum class ParseStatus { More, Done, Error };
  bool beginParse();
  ParseStatus parseStep();
  bool finishParse();
  void abortParse();
  size_t parseBytesConsumed() { return parseFile_ ? parseFile_.position() : 0; }
  size_t parseTotalBytes() { return parseFile_ ? parseFile_.size() : 0; }
  void addLineToPage(std::shared_ptr<TextBlock> line, uint32_t visibleOffset);
  const std::deque<std::pair<std::string, uint16_t>>& getAnchors() const { return anchorData; }
  bool wasLowMemoryFallbackTriggered() const { return lowMemoryImageFallback; }
  bool wasLowMemoryAbortTriggered() const { return lowMemoryAbort; }
};
