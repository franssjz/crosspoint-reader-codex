#include "ChapterHtmlSlimParser.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <MemoryBudget.h>
#include <Utf8.h>
#include <XmlParserUtils.h>
#include <expat.h>
#include <strings.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <new>

#include "../../Epub.h"
#include "../Page.h"
#include "../VisibleTextUtils.h"
#include "../converters/ImageDecoderFactory.h"
#include "../converters/ImageDimsProbe.h"
#include "../converters/ImageToFramebufferDecoder.h"
#include "../htmlEntities.h"

// Minimum file size (in bytes) to show indexing popup - smaller chapters don't benefit from it
constexpr size_t MIN_SIZE_FOR_POPUP = 10 * 1024;  // 10KB
constexpr size_t PARSE_BUFFER_SIZE = 1024;
constexpr size_t MAX_ANCHORS_PER_CHAPTER = 1024;
constexpr size_t MAX_REFERENCED_ANCHORS_PER_CHAPTER = 1024;
constexpr uint16_t MAX_REPEATED_IMAGE_RENDERS_PER_CHAPTER = 16;
constexpr uint32_t LONG_PARSE_SERVICE_INTERVAL_MS = 50;
constexpr uint32_t SOFT_MIN_FREE_HEAP_FOR_TEXT_LAYOUT = 44 * 1024;
constexpr uint32_t SOFT_MIN_MAX_ALLOC_FOR_TEXT_LAYOUT = 32 * 1024;
constexpr uint32_t HARD_MIN_FREE_HEAP_FOR_TEXT_LAYOUT = 30 * 1024;
constexpr uint32_t HARD_MIN_MAX_ALLOC_FOR_TEXT_LAYOUT = 20 * 1024;
constexpr uint32_t MIN_FREE_HEAP_FOR_TABLE_BUFFERING = 64 * 1024;
constexpr uint32_t MIN_MAX_ALLOC_FOR_TABLE_BUFFERING = 40 * 1024;
constexpr uint16_t TEXT_BLOCK_SPLIT_WORD_LIMIT = 300;
constexpr uint8_t INITIAL_PAGE_ELEMENT_RESERVE = 8;
constexpr uint8_t INITIAL_TABLE_FRAGMENT_ROW_RESERVE = 8;
constexpr uint32_t PAGE_ELEMENT_RESERVE_MIN_MAX_ALLOC = 1024;
constexpr size_t IMAGE_DIMENSION_PREFIX_CHUNK = 2048;

constexpr const char* HEADER_TAGS[] = {"h1", "h2", "h3", "h4", "h5", "h6"};
constexpr const char* BLOCK_TAGS[] = {"p", "li", "div", "br", "blockquote"};
constexpr const char* BOLD_TAGS[] = {"b", "strong"};
constexpr const char* ITALIC_TAGS[] = {"i", "em"};
constexpr const char* UNDERLINE_TAGS[] = {"u", "ins"};
constexpr const char* STRIKE_TAGS[] = {"s", "strike", "del"};
constexpr const char* IMAGE_TAGS[] = {"img", "image"};
bool isWhitespace(const char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }

std::string trimAndNormalize(const std::string& text) {
  std::string result;
  result.reserve(text.size());
  bool pendingSpace = false;
  for (const char c : text) {
    if (isWhitespace(c)) {
      pendingSpace = !result.empty();
    } else {
      if (pendingSpace) result.push_back(' ');
      result.push_back(c);
      pendingSpace = false;
    }
  }
  return result;
}

bool matches(const char* tag_name, const char* const* possible_tags, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (strcmp(tag_name, possible_tags[i]) == 0) {
      return true;
    }
  }
  return false;
}

const char* getAttribute(const XML_Char** atts, const char* attrName) {
  if (!atts) return nullptr;
  for (int i = 0; atts[i]; i += 2) {
    if (strcmp(atts[i], attrName) == 0) return atts[i + 1];
  }
  return nullptr;
}

bool hasClassToken(const std::string& classAttr, const char* token) {
  const size_t tokenLen = strlen(token);
  size_t pos = 0;
  while (pos < classAttr.size()) {
    while (pos < classAttr.size() && isWhitespace(classAttr[pos])) {
      pos++;
    }
    const size_t start = pos;
    while (pos < classAttr.size() && !isWhitespace(classAttr[pos])) {
      pos++;
    }
    if (pos - start == tokenLen && classAttr.compare(start, tokenLen, token) == 0) {
      return true;
    }
  }
  return false;
}

bool isInternalEpubLink(const char* href) {
  if (!href || href[0] == '\0') return false;
  if (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0) return false;
  if (strncmp(href, "mailto:", 7) == 0) return false;
  if (strncmp(href, "ftp://", 6) == 0) return false;
  if (strncmp(href, "tel:", 4) == 0) return false;
  if (strncmp(href, "javascript:", 11) == 0) return false;
  return true;
}

bool isHeaderOrBlock(const char* name) {
  return matches(name, HEADER_TAGS, std::size(HEADER_TAGS)) || matches(name, BLOCK_TAGS, std::size(BLOCK_TAGS));
}

bool isTableStructuralTag(const char* name) {
  return strcmp(name, "table") == 0 || strcmp(name, "tr") == 0 || strcmp(name, "td") == 0 || strcmp(name, "th") == 0;
}

bool isNonNavigableInlineElement(const char* name) { return strcmp(name, "span") == 0; }

bool ChapterHtmlSlimParser::shouldAbortForLowMemory(const char* stage) {
  if (lowMemoryAbort) {
    return true;
  }

  auto heap = MemoryBudget::snapshot();
  if (MemoryBudget::hasHeap(heap, SOFT_MIN_FREE_HEAP_FOR_TEXT_LAYOUT, SOFT_MIN_MAX_ALLOC_FOR_TEXT_LAYOUT)) {
    return false;
  }

  if (!attemptedTextLayoutFontCacheRelease) {
    attemptedTextLayoutFontCacheRelease = true;
    if (renderer.releaseSdCardFontForLowMemory(fontId)) {
      const auto afterRelease = MemoryBudget::snapshot();
      LOG_DBG("EHP", "Released SD font caches before %s: free=%u->%u maxAlloc=%u->%u", stage, heap.freeHeap,
              afterRelease.freeHeap, heap.maxAllocHeap, afterRelease.maxAllocHeap);
      heap = afterRelease;
      if (MemoryBudget::hasHeap(heap, SOFT_MIN_FREE_HEAP_FOR_TEXT_LAYOUT, SOFT_MIN_MAX_ALLOC_FOR_TEXT_LAYOUT)) {
        return false;
      }
    }
  }

  if (cssParser && !cssParser->empty()) {
    const auto beforeCssClear = heap;
    const size_t ruleCount = cssParser->ruleCount();
    cssParser->clear();
    cssParser = nullptr;
    const auto afterCssClear = MemoryBudget::snapshot();
    LOG_DBG("EHP", "Dropped %u CSS rules during %s: free=%u->%u maxAlloc=%u->%u", static_cast<unsigned>(ruleCount),
            stage, beforeCssClear.freeHeap, afterCssClear.freeHeap, beforeCssClear.maxAllocHeap,
            afterCssClear.maxAllocHeap);
    heap = afterCssClear;
    if (MemoryBudget::hasHeap(heap, SOFT_MIN_FREE_HEAP_FOR_TEXT_LAYOUT, SOFT_MIN_MAX_ALLOC_FOR_TEXT_LAYOUT)) {
      return false;
    }
  }

  if (MemoryBudget::hasHeap(heap, HARD_MIN_FREE_HEAP_FOR_TEXT_LAYOUT, HARD_MIN_MAX_ALLOC_FOR_TEXT_LAYOUT)) {
    if (!loggedSoftLowMemoryContinuation) {
      loggedSoftLowMemoryContinuation = true;
      LOG_DBG("EHP", "Continuing section build below soft heap during %s (free=%u maxAlloc=%u)", stage, heap.freeHeap,
              heap.maxAllocHeap);
    }
    return false;
  }

  LOG_ERR("EHP", "Critical low heap during %s (%u free, %u max alloc); aborting section build", stage, heap.freeHeap,
          heap.maxAllocHeap);
  lowMemoryAbort = true;
  return true;
}

bool ChapterHtmlSlimParser::startNewPage(const char* reason) {
  currentPage.reset(new (std::nothrow) Page());
  if (!currentPage) {
    const auto heap = MemoryBudget::snapshot();
    LOG_ERR("EHP", "Failed to create page during %s (%u free, %u max alloc)", reason, heap.freeHeap, heap.maxAllocHeap);
    lowMemoryAbort = true;
    return false;
  }

  const auto heap = MemoryBudget::snapshot();
  if (MemoryBudget::hasHeap(heap, SOFT_MIN_FREE_HEAP_FOR_TEXT_LAYOUT, PAGE_ELEMENT_RESERVE_MIN_MAX_ALLOC)) {
    currentPage->elements.reserve(INITIAL_PAGE_ELEMENT_RESERVE);
  }
  currentPageNextY = 0;
  currentPageVisibleOffsetSet = false;
  return true;
}

bool isAsciiNameChar(const char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ':';
}

bool isAsciiSpace(const char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t' || c == '\f'; }

char asciiLower(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool looksLikeLocalFilePath(const std::string& value) {
  size_t pos = 0;
  while (pos < value.size() && isAsciiSpace(value[pos])) {
    pos++;
  }

  const bool drivePath = pos + 2 < value.size() &&
                         ((value[pos] >= 'a' && value[pos] <= 'z') || (value[pos] >= 'A' && value[pos] <= 'Z')) &&
                         value[pos + 1] == ':' && (value[pos + 2] == '\\' || value[pos + 2] == '/');
  const bool uncPath = pos + 1 < value.size() && value[pos] == '\\' && value[pos + 1] == '\\';
  const bool fileUri = pos + 7 <= value.size() && asciiLower(value[pos]) == 'f' && asciiLower(value[pos + 1]) == 'i' &&
                       asciiLower(value[pos + 2]) == 'l' && asciiLower(value[pos + 3]) == 'e' &&
                       value[pos + 4] == ':' && value[pos + 5] == '/' && value[pos + 6] == '/';
  return drivePath || uncPath || fileUri;
}

bool startsHrefAttribute(const std::string& text, const size_t pos) {
  if (pos + 4 > text.size()) return false;
  if (pos > 0 && isAsciiNameChar(text[pos - 1])) return false;
  if (asciiLower(text[pos]) != 'h' || asciiLower(text[pos + 1]) != 'r' || asciiLower(text[pos + 2]) != 'e' ||
      asciiLower(text[pos + 3]) != 'f') {
    return false;
  }
  if (pos + 4 < text.size() && isAsciiNameChar(text[pos + 4])) return false;
  return true;
}

void ChapterHtmlSlimParser::applyDirectionToEntry(StyleStackEntry& entry, const CssStyle& css) {
  if (css.hasDirection()) {
    entry.hasDirection = true;
    entry.direction = css.direction;
  }
}

void ChapterHtmlSlimParser::applyVerticalAlignToEntry(StyleStackEntry& entry, const CssStyle& css) {
  if (!css.hasVerticalAlign()) return;
  if (css.verticalAlign == CssVerticalAlign::Super) {
    entry.hasSup = true;
    entry.sup = true;
  } else if (css.verticalAlign == CssVerticalAlign::Sub) {
    entry.hasSub = true;
    entry.sub = true;
  }
}

// Update effective bold/italic/underline/direction based on block style and inline style stack
void ChapterHtmlSlimParser::updateEffectiveInlineStyle() {
  // Start with block-level styles
  effectiveBold = currentCssStyle.hasFontWeight() && currentCssStyle.fontWeight == CssFontWeight::Bold;
  effectiveItalic = currentCssStyle.hasFontStyle() && currentCssStyle.fontStyle == CssFontStyle::Italic;
  effectiveUnderline = currentCssStyle.hasTextDecoration() &&
                       hasTextDecoration(currentCssStyle.textDecoration, CssTextDecoration::Underline);
  effectiveStrikeThrough = currentCssStyle.hasTextDecoration() &&
                           hasTextDecoration(currentCssStyle.textDecoration, CssTextDecoration::LineThrough);
  bool paragraphDirectionDefined = false;
  bool paragraphIsRtl = false;
  if (!blockStyleStack.empty()) {
    const auto& blockStyle = blockStyleStack.back();
    paragraphDirectionDefined = blockStyle.directionDefined;
    paragraphIsRtl = blockStyle.isRtl;
  }
  effectiveDirectionDefined = paragraphDirectionDefined;
  effectiveDirection = paragraphIsRtl ? CssTextDirection::Rtl : CssTextDirection::Ltr;
  effectiveSup = false;
  effectiveSub = false;

  // Apply inline style stack in order
  for (const auto& entry : inlineStyleStack) {
    if (entry.hasBold) {
      effectiveBold = entry.bold;
    }
    if (entry.hasItalic) {
      effectiveItalic = entry.italic;
    }
    if (entry.hasUnderline) {
      effectiveUnderline = entry.underline;
    }
    if (entry.hasStrikeThrough) {
      effectiveStrikeThrough = entry.strikeThrough;
    }
    if (entry.hasDirection) {
      effectiveDirectionDefined = true;
      effectiveDirection = entry.direction;
      if (entry.setsParagraphDirection) {
        paragraphDirectionDefined = true;
        paragraphIsRtl = entry.direction == CssTextDirection::Rtl;
      }
    }
    if (entry.hasSup) {
      effectiveSup = entry.sup;
      if (entry.sup) effectiveSub = false;
    }
    if (entry.hasSub) {
      effectiveSub = entry.sub;
      if (entry.sub) effectiveSup = false;
    }
  }

  // Keep flow direction in the active empty text block. Inline direction remains
  // available for CSS inheritance without replacing the paragraph's base direction.
  if (currentTextBlock && currentTextBlock->isEmpty()) {
    auto& style = currentTextBlock->getBlockStyle();
    style.directionDefined = paragraphDirectionDefined;
    style.isRtl = paragraphIsRtl;
  }
}

void ChapterHtmlSlimParser::flushPendingAnchor() {
  if (pendingAnchorId.empty()) return;

  // If the pending anchor is a TOC chapter boundary, force a page break after the previous
  // block is flushed so the chapter starts on a fresh page.
  if (std::find(tocAnchors.begin(), tocAnchors.end(), pendingAnchorId) != tocAnchors.end()) {
    if (currentPage && !currentPage->elements.empty()) {
      emitPage(lastBodyChildByteOffset);
      if (!startNewPage("TOC anchor page break")) {
        return;
      }
    }
  }

  // Record deferred anchor after previous block is flushed (and any TOC page break)
  anchorData.push_back({std::move(pendingAnchorId), static_cast<uint16_t>(completedPageCount)});
  pendingAnchorId.clear();
}

void ChapterHtmlSlimParser::collectReferencedAnchor(const char* href) {
  if (!isInternalEpubLink(href)) return;

  const char* hash = std::strchr(href, '#');
  if (!hash || hash[1] == '\0') return;

  std::string anchor(hash + 1);
  const auto queryPos = anchor.find('?');
  if (queryPos != std::string::npos) {
    anchor.resize(queryPos);
  }
  if (anchor.empty()) return;

  if (std::find(referencedAnchors.begin(), referencedAnchors.end(), anchor) != referencedAnchors.end()) return;
  if (referencedAnchors.size() >= MAX_REFERENCED_ANCHORS_PER_CHAPTER) return;

  referencedAnchors.push_back(std::move(anchor));
}

bool ChapterHtmlSlimParser::isReferencedAnchor(const std::string& anchor) const {
  return std::find(referencedAnchors.begin(), referencedAnchors.end(), anchor) != referencedAnchors.end();
}

bool ChapterHtmlSlimParser::shouldRecordAnchor(const char* elementName, const std::string& anchor) const {
  if (std::find(tocAnchors.begin(), tocAnchors.end(), anchor) != tocAnchors.end()) return true;
  if (isReferencedAnchor(anchor)) return true;
  if (isNonNavigableInlineElement(elementName)) return false;
  return anchorData.size() < MAX_ANCHORS_PER_CHAPTER;
}

bool ChapterHtmlSlimParser::readImageDimensions(const std::string& resolvedPath, const std::string& cachedImagePath,
                                                ImageDimensions& dims) {
  if (hasLastImageDimensions && lastImageDimensionsPath == resolvedPath) {
    dims = lastImageDimensions;
    return true;
  }

  ImageDimsProbe headerProbe;
  epub->readItemContentsToStream(resolvedPath, headerProbe, IMAGE_DIMENSION_PREFIX_CHUNK,
                                 /*allowEarlyStop=*/true);
  bool dimensionsRead = headerProbe.getDimensions(dims);

  // Some otherwise-decodable EPUB images have metadata/layout that the small
  // prefix probe cannot recognize. Fall back to the older streaming extraction
  // path only for those images; the extracted file also becomes the lazy cache.
  if (!dimensionsRead) {
    HalFile cachedImageFile;
    bool extracted = false;
    if (Storage.openFileForWrite("EHP", cachedImagePath, cachedImageFile)) {
      extracted = epub->readItemContentsToStream(resolvedPath, cachedImageFile, IMAGE_DIMENSION_PREFIX_CHUNK);
      cachedImageFile.flush();
      cachedImageFile.close();
    }

    ImageToFramebufferDecoder* decoder = extracted ? ImageDecoderFactory::getDecoder(cachedImagePath) : nullptr;
    dimensionsRead = decoder && decoder->getDimensions(cachedImagePath, dims);
    if (!dimensionsRead) {
      Storage.remove(cachedImagePath.c_str());
    } else {
      LOG_DBG("EHP", "Read image dimensions after streaming fallback: %s", resolvedPath.c_str());
    }
  }

  if (dimensionsRead) {
    lastImageDimensionsPath = resolvedPath;
    lastImageDimensions = dims;
    hasLastImageDimensions = true;
  }

  return dimensionsRead;
}

bool ChapterHtmlSlimParser::shouldSuppressRepeatedImage(const std::string& resolvedPath) {
  if (lastRenderedImagePath == resolvedPath) {
    if (lastRenderedImageCount < UINT16_MAX) {
      lastRenderedImageCount++;
    }
  } else {
    lastRenderedImagePath = resolvedPath;
    lastRenderedImageCount = 1;
  }

  if (lastRenderedImageCount == MAX_REPEATED_IMAGE_RENDERS_PER_CHAPTER + 1) {
    LOG_DBG("EHP", "Suppressing repeated chapter image after %u uses: %s", MAX_REPEATED_IMAGE_RENDERS_PER_CHAPTER,
            resolvedPath.c_str());
  }

  return lastRenderedImageCount > MAX_REPEATED_IMAGE_RENDERS_PER_CHAPTER;
}

void ChapterHtmlSlimParser::serviceLongParse(const char* stage) {
  (void)stage;
  const uint32_t now = millis();
  if (lastLongParseServiceMs != 0 && now - lastLongParseServiceMs < LONG_PARSE_SERVICE_INTERVAL_MS) {
    return;
  }
  lastLongParseServiceMs = now;
  delay(1);
}

void ChapterHtmlSlimParser::collectReferencedAnchors() {
  referencedAnchors.clear();

  HalFile file;
  if (!Storage.openFileForRead("EHP", filepath, file)) {
    return;
  }

  std::string carry;
  char buffer[PARSE_BUFFER_SIZE + 1] = {};

  while (file.available() > 0 && referencedAnchors.size() < MAX_REFERENCED_ANCHORS_PER_CHAPTER) {
    // This pass is only an optimization for retaining inline anchor targets.
    // Do not let a link-heavy chapter consume the heap needed by Expat and
    // page layout. Checking at vector-growth boundaries also keeps the hot
    // scan from polling the heap for every href.
    if (!referencedAnchors.empty() && (referencedAnchors.size() & 0x0F) == 0) {
      const auto heap = MemoryBudget::snapshot();
      if (!MemoryBudget::hasHeap(heap, SOFT_MIN_FREE_HEAP_FOR_TEXT_LAYOUT, SOFT_MIN_MAX_ALLOC_FOR_TEXT_LAYOUT)) {
        LOG_DBG("EHP", "Stopped anchor pre-scan to preserve layout heap (anchors=%u free=%u maxAlloc=%u)",
                static_cast<unsigned>(referencedAnchors.size()), heap.freeHeap, heap.maxAllocHeap);
        break;
      }
    }

    serviceLongParse("anchor scan");
    const size_t len = file.read(buffer, PARSE_BUFFER_SIZE);
    if (len == 0) break;

    std::string chunk = carry;
    chunk.append(buffer, len);

    size_t pos = 0;
    while (pos < chunk.size() && referencedAnchors.size() < MAX_REFERENCED_ANCHORS_PER_CHAPTER) {
      if (!startsHrefAttribute(chunk, pos)) {
        pos++;
        continue;
      }

      pos += 4;
      while (pos < chunk.size() && isAsciiSpace(chunk[pos])) pos++;
      if (pos >= chunk.size() || chunk[pos] != '=') continue;
      pos++;
      while (pos < chunk.size() && isAsciiSpace(chunk[pos])) pos++;
      if (pos >= chunk.size()) break;

      const char quote = (chunk[pos] == '"' || chunk[pos] == '\'') ? chunk[pos++] : '\0';
      const size_t valueStart = pos;
      while (pos < chunk.size() &&
             ((quote && chunk[pos] != quote) || (!quote && !isAsciiSpace(chunk[pos]) && chunk[pos] != '>'))) {
        pos++;
      }

      const bool valueComplete = quote ? (pos < chunk.size() && chunk[pos] == quote) : (pos < chunk.size());
      if (!valueComplete) break;
      {
        const std::string href = chunk.substr(valueStart, pos - valueStart);
        collectReferencedAnchor(href.c_str());
      }
    }

    if (chunk.size() > 256) {
      carry = chunk.substr(chunk.size() - 256);
    } else {
      carry = std::move(chunk);
    }
  }

  file.close();
}

// flush the contents of partWordBuffer to currentTextBlock
void ChapterHtmlSlimParser::flushPartWordBuffer() {
  if (lowMemoryAbort || !currentTextBlock) {
    partWordBufferIndex = 0;
    nextWordContinues = false;
    return;
  }

  // Flush before addWord() can force the word vector to grow from one large
  // contiguous allocation to the next. This keeps long HTML text nodes within
  // the ESP32-C3's largest allocatable heap block.
  flushLongTextBlockIfNeeded();

  // Determine font style from depth-based tracking and CSS effective style
  const bool isBold = boldUntilDepth < depth || effectiveBold;
  const bool isItalic = italicUntilDepth < depth || effectiveItalic;
  const bool isUnderline = underlineUntilDepth < depth || effectiveUnderline;
  const bool isStrikeThrough = strikeUntilDepth < depth || effectiveStrikeThrough;

  // Combine style flags using bitwise OR
  EpdFontFamily::Style fontStyle = EpdFontFamily::REGULAR;
  if (isBold) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::BOLD);
  }
  if (isItalic) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::ITALIC);
  }
  if (isUnderline) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::UNDERLINE);
  }
  if (isStrikeThrough) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::STRIKETHROUGH);
  }
  if (effectiveSup) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::SUP);
  } else if (effectiveSub) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::SUB);
  }

  // flush the buffer
  partWordBuffer[partWordBufferIndex] = '\0';
  uint8_t linkId = 0;
  if (insideFootnoteLink) {
    if (!currentTextBlock->linkTargetMatches(currentFootnoteLinkId, currentFootnote.href)) {
      currentFootnoteLinkId = currentTextBlock->addLinkTarget(currentFootnote.href);
    }
    linkId = currentFootnoteLinkId;
  }
  currentTextBlock->addWord(partWordBuffer, fontStyle, false, nextWordContinues, partWordVisibleOffset, linkId);
  partWordBufferIndex = 0;
  nextWordContinues = false;
  listItemBulletOnly = false;
}

void ChapterHtmlSlimParser::flushLongTextBlockIfNeeded() {
  // A group ruby must remain in one ParsedText until </rt> associates the
  // annotation with every base word. Splitting it here loses that mapping.
  if (inRuby || !currentTextBlock || currentTextBlock->size() <= TEXT_BLOCK_SPLIT_WORD_LIMIT) {
    return;
  }

  LOG_DBG("EHP", "Text block too long, splitting before vector growth");
  const int horizontalInset = currentTextBlock->getBlockStyle().totalHorizontalInset();
  const uint16_t effectiveWidth =
      (horizontalInset < viewportWidth) ? static_cast<uint16_t>(viewportWidth - horizontalInset) : viewportWidth;
  currentTextBlock->layoutAndExtractLines(
      renderer, fontId, effectiveWidth,
      [this](const std::shared_ptr<TextBlock>& textBlock, const uint32_t offset) {
        this->addLineToPage(textBlock, offset);
      },
      false);
}

void ChapterHtmlSlimParser::setCurrentPageVisibleOffset(const uint32_t offset) {
  if (currentPageVisibleOffsetSet) return;
  // Preserve the chapter start even when formatting whitespace precedes the first glyph.
  currentPageVisibleOffset = completedPageCount == 0 ? 0 : offset;
  currentPageVisibleOffsetSet = true;
}

// start a new text block if needed
void ChapterHtmlSlimParser::startNewTextBlock(const BlockStyle& blockStyle) {
  if (shouldAbortForLowMemory("text block start")) {
    return;
  }

  nextWordContinues = false;  // New block = new paragraph, no continuation
  if (currentTextBlock) {
    // already have a text block running and it is empty - just reuse it
    if (currentTextBlock->isEmpty()) {
      // The stack accumulates horizontal margins and text properties from ancestors.
      // Vertical margins are per-element and not inherited through the stack, but
      // container elements deposit their vertical margins on the empty block when they
      // open. Merge those into the new style so the first child in a container inherits
      // the container's vertical spacing.
      const auto style = currentTextBlock->getBlockStyle();
      BlockStyle incoming = blockStyle;
      if (style.fromBrElement) {
        // The empty block was created by a <br> section separator. Inject a full line of
        // blank space before the following paragraph so the scene/section break is visible.
        // This only fires when the <br> block stayed empty (i.e. no inline text was added).
        const int16_t lineHeight = static_cast<int16_t>(renderer.getLineHeight(fontId) * lineCompression + 0.5f);
        incoming.marginTop = static_cast<int16_t>(incoming.marginTop + lineHeight);
      }

      currentTextBlock->setBlockStyle(style.getCombinedBlockStyle(incoming, BlockStyle::CombineAxis::Vertical));

      flushPendingAnchor();
      return;
    }

    // <li> added a bullet as the first word, making the block non-empty. When a nested
    // block-level child (<p>, <div>, etc.) opens, reuse the block instead of flushing
    // the bullet to its own line. The bullet stays inline with the child's text.
    if (listItemBulletOnly) {
      const auto style = currentTextBlock->getBlockStyle();
      currentTextBlock->setBlockStyle(style.getCombinedBlockStyle(blockStyle, BlockStyle::CombineAxis::Vertical));
      listItemBulletOnly = false;
      flushPendingAnchor();
      return;
    }

    makePages();
    if (lowMemoryAbort) {
      return;
    }
  }
  // If the pending anchor is a TOC chapter boundary, force a page break after the previous
  // block is flushed so the chapter starts on a fresh page.
  flushPendingAnchor();
  if (lowMemoryAbort) {
    return;
  }
  currentTextBlock.reset(new (std::nothrow) ParsedText(extraParagraphSpacing, forceParagraphIndents, hyphenationEnabled,
                                                       focusReadingEnabled, blockStyle));
  if (!currentTextBlock) {
    const auto heap = MemoryBudget::snapshot();
    LOG_ERR("EHP", "Failed to create text block (%u free, %u max alloc)", heap.freeHeap, heap.maxAllocHeap);
    lowMemoryAbort = true;
    return;
  }
  wordsExtractedInBlock = 0;
  listItemBulletOnly = false;
}

void ChapterHtmlSlimParser::finalizeCurrentTableCell() {
  if (lowMemoryAbort) {
    return;
  }

  if (tableDepth != 1 || !currentTextBlock) {
    return;
  }

  if (!currentTableBuffer) {
    makePages();
    currentTextBlock.reset();
    pendingFootnotes.clear();
    currentTableCellIsHeader = false;
    currentTableCellColSpan = 1;
    wordsExtractedInBlock = 0;
    nextWordContinues = false;
    return;
  }

  if (currentTableBuffer->rows.empty()) {
    currentTableBuffer->rows.push_back({});
  }

  BufferedTableCell cell;
  cell.isHeader = currentTableCellIsHeader;
  cell.colSpan = currentTableCellColSpan;
  cell.text = std::move(currentTextBlock);
  cell.footnotes = std::move(pendingFootnotes);
  pendingFootnotes.clear();

  if (cell.text && cell.text->size() > MAX_SIMPLE_TABLE_CELL_WORDS) {
    currentTableBuffer->unsupported = true;
  }

  auto& row = currentTableBuffer->rows.back();
  row.hasHeaderCell = row.hasHeaderCell || cell.isHeader;
  row.hasDataCell = row.hasDataCell || !cell.isHeader;
  row.effectiveColumnCount = static_cast<uint16_t>(row.effectiveColumnCount + cell.colSpan);
  row.cells.push_back(std::move(cell));

  currentTableBuffer->totalCells++;
  currentTableBuffer->maxCols = std::max<uint16_t>(currentTableBuffer->maxCols, row.effectiveColumnCount);
  if (currentTableBuffer->totalCells > MAX_SIMPLE_TABLE_CELLS ||
      currentTableBuffer->maxCols > MAX_SIMPLE_TABLE_COLUMNS) {
    currentTableBuffer->unsupported = true;
  }

  currentTableCellIsHeader = false;
  currentTableCellColSpan = 1;
  wordsExtractedInBlock = 0;
  nextWordContinues = false;
  fallbackCurrentTableBufferIfNeeded("cell complete");
}

void ChapterHtmlSlimParser::emitPage(const uint32_t xhtmlByteOffset) {
  if (!currentPage) {
    return;
  }
  setCurrentPageVisibleOffset(visibleTextOffset);
  completePageFn(std::move(currentPage),
                 {xhtmlByteOffset, xpathParagraphIndex, xpathListItemIndex, currentPageVisibleOffset});
  completedPageCount++;
  serviceLongParse("page emit");
}

void ChapterHtmlSlimParser::emitBufferedTableAsParagraphs(BufferedTable& table) {
  if (!currentPage) {
    if (!startNewPage("table paragraph fallback")) {
      return;
    }
  }

  if (table.blockStyle.marginTop > 0) {
    currentPageNextY += table.blockStyle.marginTop;
  }
  if (table.blockStyle.paddingTop > 0) {
    currentPageNextY += table.blockStyle.paddingTop;
  }

  for (auto& row : table.rows) {
    for (auto& cell : row.cells) {
      if (!cell.text) {
        continue;
      }

      pendingFootnotes = std::move(cell.footnotes);
      currentTextBlock = std::move(cell.text);
      wordsExtractedInBlock = 0;
      makePages();
      currentTextBlock.reset();
      pendingFootnotes.clear();
      if (lowMemoryAbort) {
        break;
      }
    }
    std::vector<BufferedTableCell>().swap(row.cells);
    if (lowMemoryAbort) {
      break;
    }
  }
  std::vector<BufferedTableRow>().swap(table.rows);
  if (lowMemoryAbort) {
    return;
  }

  if (table.blockStyle.marginBottom > 0) {
    currentPageNextY += table.blockStyle.marginBottom;
  }
  if (table.blockStyle.paddingBottom > 0) {
    currentPageNextY += table.blockStyle.paddingBottom;
  }

  const int lineHeight = renderer.getLineHeight(fontId) * lineCompression;
  if (extraParagraphSpacing) {
    currentPageNextY += lineHeight / 2;
  }
}

void ChapterHtmlSlimParser::emitBufferedTableAsFragments(BufferedTable& table) {
  struct PreparedRow {
    TableFragmentRow fragmentRow;
    std::vector<FootnoteEntry> footnotes;
  };

  struct PreparedSegment {
    uint8_t columnCount = 0;
    std::vector<PreparedRow> rows;
  };

  if (!currentPage) {
    if (!startNewPage("table fragments")) {
      return;
    }
  }

  const int horizontalInset = table.blockStyle.totalHorizontalInset();
  const uint16_t tableWidth =
      (horizontalInset < viewportWidth) ? static_cast<uint16_t>(viewportWidth - horizontalInset) : viewportWidth;
  const uint16_t lineHeight = renderer.getLineHeight(fontId) * lineCompression;
  std::vector<PreparedSegment> preparedSegments;
  preparedSegments.reserve(table.rows.size());

  auto releasePreparedSegments = [&preparedSegments]() {
    for (auto& segment : preparedSegments) {
      for (auto& row : segment.rows) {
        std::vector<TableFragmentCell>().swap(row.fragmentRow.cells);
        std::vector<FootnoteEntry>().swap(row.footnotes);
      }
      std::vector<PreparedRow>().swap(segment.rows);
    }
    std::vector<PreparedSegment>().swap(preparedSegments);
  };

  auto prepareRow = [&](const BufferedTableRow& row, const uint8_t columnCount, PreparedSegment& segment) -> bool {
    const uint16_t baseColumnWidth = columnCount > 0 ? tableWidth / columnCount : 0;
    const uint16_t innerColumnWidth = (baseColumnWidth > TABLE_CELL_PADDING * 2)
                                          ? static_cast<uint16_t>(baseColumnWidth - TABLE_CELL_PADDING * 2)
                                          : 0;
    if (columnCount == 0 || innerColumnWidth < 20) {
      LOG_DBG("EHP", "Table layout fallback: width %u too small for %u columns", tableWidth, columnCount);
      return false;
    }

    PreparedRow prepared;
    prepared.fragmentRow.cells.resize(columnCount);
    prepared.fragmentRow.headerSeparator = row.hasHeaderCell && !row.hasDataCell;

    uint32_t rowHeight = static_cast<uint32_t>(lineHeight) + TABLE_CELL_PADDING * 2;
    if (rowHeight > viewportHeight) {
      LOG_DBG("EHP", "Table layout fallback: row height %lu exceeds viewport %u", static_cast<unsigned long>(rowHeight),
              viewportHeight);
      return false;
    }
    for (size_t colIndex = 0; colIndex < row.cells.size(); colIndex++) {
      const auto& sourceCell = row.cells[colIndex];
      auto& destCell = prepared.fragmentRow.cells[colIndex];
      destCell.isHeader = sourceCell.isHeader;

      if (sourceCell.text) {
        sourceCell.text->layoutAndExtractLines(renderer, fontId, innerColumnWidth,
                                               [&destCell](const std::shared_ptr<TextBlock>& textBlock,
                                                           const uint32_t) { destCell.lines.push_back(textBlock); });
      }

      for (const auto& footnotePair : sourceCell.footnotes) {
        prepared.footnotes.push_back(footnotePair.second);
      }

      if (destCell.lines.size() > TableFragmentCell::MAX_SERIALIZED_LINES) {
        LOG_DBG("EHP", "Table layout fallback: cell line count %u exceeds fragment max %u",
                static_cast<uint32_t>(destCell.lines.size()), TableFragmentCell::MAX_SERIALIZED_LINES);
        return false;
      }

      const uint32_t cellLineCount = std::max<size_t>(1, destCell.lines.size());
      const uint32_t cellHeight = cellLineCount * lineHeight + TABLE_CELL_PADDING * 2;
      if (cellHeight > viewportHeight) {
        LOG_DBG("EHP", "Table layout fallback: row height %lu exceeds viewport %u",
                static_cast<unsigned long>(cellHeight), viewportHeight);
        return false;
      }

      rowHeight = std::max<uint32_t>(rowHeight, cellHeight);
    }

    prepared.fragmentRow.height = static_cast<uint16_t>(rowHeight);
    segment.rows.push_back(std::move(prepared));
    return true;
  };

  for (const auto& row : table.rows) {
    const bool rowHasMergedCells = std::any_of(row.cells.begin(), row.cells.end(),
                                               [](const BufferedTableCell& cell) { return cell.colSpan != 1; });
    const bool isFullWidthSingleCellRow =
        row.cells.size() == 1 && table.maxCols > 0 && row.cells[0].colSpan == table.maxCols;

    if (rowHasMergedCells && !isFullWidthSingleCellRow) {
      LOG_DBG("EHP", "Table layout fallback: unsupported colspan structure");
      releasePreparedSegments();
      emitBufferedTableAsParagraphs(table);
      return;
    }

    const uint8_t segmentColumnCount = isFullWidthSingleCellRow ? 1 : static_cast<uint8_t>(table.maxCols);
    if (preparedSegments.empty() || preparedSegments.back().columnCount != segmentColumnCount) {
      preparedSegments.push_back({});
      preparedSegments.back().columnCount = segmentColumnCount;
      preparedSegments.back().rows.reserve(table.rows.size());
    }

    if (!prepareRow(row, segmentColumnCount, preparedSegments.back())) {
      releasePreparedSegments();
      emitBufferedTableAsParagraphs(table);
      return;
    }
  }

  if (table.blockStyle.marginTop > 0) {
    currentPageNextY += table.blockStyle.marginTop;
  }
  if (table.blockStyle.paddingTop > 0) {
    currentPageNextY += table.blockStyle.paddingTop;
  }

  for (auto& segment : preparedSegments) {
    size_t nextRowIndex = 0;
    while (nextRowIndex < segment.rows.size()) {
      if (!currentPage) {
        if (!startNewPage("table fragment continuation")) {
          return;
        }
      }

      std::vector<TableFragmentRow> fragmentRows;
      std::vector<FootnoteEntry> fragmentFootnotes;
      fragmentRows.reserve(std::min<size_t>(segment.rows.size() - nextRowIndex, INITIAL_TABLE_FRAGMENT_ROW_RESERVE));
      uint16_t fragmentHeight = 1;

      while (nextRowIndex < segment.rows.size()) {
        const uint16_t nextHeight =
            static_cast<uint16_t>(fragmentHeight + segment.rows[nextRowIndex].fragmentRow.height);
        if (!fragmentRows.empty() && currentPageNextY + nextHeight > viewportHeight) {
          break;
        }
        if (fragmentRows.empty() && currentPageNextY + nextHeight > viewportHeight && !currentPage->elements.empty()) {
          emitPage(lastBodyChildByteOffset);
          if (!startNewPage("table fragment page break")) {
            return;
          }
          continue;
        }

        fragmentHeight = nextHeight;
        fragmentRows.push_back(std::move(segment.rows[nextRowIndex].fragmentRow));
        fragmentFootnotes.insert(fragmentFootnotes.end(), segment.rows[nextRowIndex].footnotes.begin(),
                                 segment.rows[nextRowIndex].footnotes.end());
        nextRowIndex++;
      }

      if (fragmentRows.empty()) {
        fragmentHeight = static_cast<uint16_t>(1 + segment.rows[nextRowIndex].fragmentRow.height);
        fragmentRows.push_back(std::move(segment.rows[nextRowIndex].fragmentRow));
        fragmentFootnotes.insert(fragmentFootnotes.end(), segment.rows[nextRowIndex].footnotes.begin(),
                                 segment.rows[nextRowIndex].footnotes.end());
        nextRowIndex++;
      }

      auto tableFragment = std::shared_ptr<PageTableFragment>(new (std::nothrow) PageTableFragment(
          tableWidth, segment.columnCount, TABLE_CELL_PADDING, lineHeight, std::move(fragmentRows),
          table.blockStyle.leftInset(), currentPageNextY));
      if (!tableFragment) {
        const auto heap = MemoryBudget::snapshot();
        LOG_ERR("EHP", "Failed to create PageTableFragment (%u free, %u max alloc)", heap.freeHeap, heap.maxAllocHeap);
        lowMemoryAbort = true;
        return;
      }
      currentPage->elements.push_back(tableFragment);
      setCurrentPageVisibleOffset(visibleTextOffset);
      for (const auto& footnote : fragmentFootnotes) {
        currentPage->addFootnote(footnote.number, footnote.href);
      }
      currentPageNextY += fragmentHeight;

      if (nextRowIndex < segment.rows.size()) {
        emitPage(lastBodyChildByteOffset);
        if (!startNewPage("table fragment split")) {
          return;
        }
      }
    }
  }

  if (table.blockStyle.marginBottom > 0) {
    currentPageNextY += table.blockStyle.marginBottom;
  }
  if (table.blockStyle.paddingBottom > 0) {
    currentPageNextY += table.blockStyle.paddingBottom;
  }

  if (extraParagraphSpacing) {
    currentPageNextY += lineHeight / 2;
  }
}

void ChapterHtmlSlimParser::emitCurrentTableBuffer() {
  if (!currentTableBuffer) {
    return;
  }

  auto table = std::move(currentTableBuffer);
  currentTableCellIsHeader = false;
  currentTableCellColSpan = 1;

  if (table->rows.empty() || table->maxCols == 0) {
    return;
  }

  if (table->unsupported) {
    LOG_DBG("EHP", "Table layout fallback: unsupported structure (%u rows, %u cols, %u cells)",
            static_cast<uint32_t>(table->rows.size()), table->maxCols, table->totalCells);
    emitBufferedTableAsParagraphs(*table);
    return;
  }

  emitBufferedTableAsFragments(*table);
}

void ChapterHtmlSlimParser::fallbackCurrentTableBufferToParagraphs(const char* reason) {
  if (!currentTableBuffer) {
    return;
  }

  const auto heap = MemoryBudget::snapshot();
  LOG_DBG("EHP", "Table layout fallback: %s (%u rows, %u cols, %u cells, free=%u, maxAlloc=%u)", reason,
          static_cast<uint32_t>(currentTableBuffer->rows.size()), currentTableBuffer->maxCols,
          currentTableBuffer->totalCells, heap.freeHeap, heap.maxAllocHeap);

  auto activeTextBlock = std::move(currentTextBlock);
  auto activeFootnotes = std::move(pendingFootnotes);
  const int activeWordsExtracted = wordsExtractedInBlock;
  const bool activeNextWordContinues = nextWordContinues;
  const bool activeTableCellIsHeader = currentTableCellIsHeader;
  const uint8_t activeTableCellColSpan = currentTableCellColSpan;

  emitBufferedTableAsParagraphs(*currentTableBuffer);
  currentTableBuffer.reset();

  currentTextBlock = std::move(activeTextBlock);
  pendingFootnotes = std::move(activeFootnotes);
  wordsExtractedInBlock = activeWordsExtracted;
  nextWordContinues = activeNextWordContinues;
  currentTableCellIsHeader = activeTableCellIsHeader;
  currentTableCellColSpan = activeTableCellColSpan;
}

void ChapterHtmlSlimParser::fallbackCurrentTableBufferIfNeeded(const char* stage) {
  if (!currentTableBuffer) {
    return;
  }

  if (currentTableBuffer->unsupported) {
    fallbackCurrentTableBufferToParagraphs(stage);
    return;
  }

  const auto heap = MemoryBudget::snapshot();
  if (!MemoryBudget::hasHeap(heap, MIN_FREE_HEAP_FOR_TABLE_BUFFERING, MIN_MAX_ALLOC_FOR_TABLE_BUFFERING)) {
    fallbackCurrentTableBufferToParagraphs(stage);
  }
}

void ChapterHtmlSlimParser::emitHorizontalRule(const BlockStyle& blockStyle) {
  if (partWordBufferIndex > 0) {
    flushPartWordBuffer();
  }

  if (currentTextBlock) {
    const BlockStyle parentBlockStyle = currentTextBlock->getBlockStyle();
    startNewTextBlock(parentBlockStyle);
  }

  if (!currentPage) {
    if (!startNewPage("horizontal rule")) {
      return;
    }
  }

  const int16_t lineHeight = static_cast<int16_t>(renderer.getLineHeight(fontId) * lineCompression + 0.5f);
  const int16_t defaultVerticalSpacing = static_cast<int16_t>(lineHeight / 2);
  const int16_t topSpacing =
      static_cast<int16_t>((blockStyle.marginTop > 0 ? blockStyle.marginTop : defaultVerticalSpacing) +
                           (blockStyle.paddingTop > 0 ? blockStyle.paddingTop : 0));
  const int16_t bottomSpacing =
      static_cast<int16_t>((blockStyle.marginBottom > 0 ? blockStyle.marginBottom : defaultVerticalSpacing) +
                           (blockStyle.paddingBottom > 0 ? blockStyle.paddingBottom : 0));
  constexpr uint8_t ruleThickness = 2;
  const int16_t availableWidth =
      std::max<int16_t>(1, static_cast<int16_t>(viewportWidth - blockStyle.totalHorizontalInset()));
  const int16_t width = std::max<int16_t>(1, static_cast<int16_t>(availableWidth / 4));
  const int16_t xPos = static_cast<int16_t>(blockStyle.leftInset() + ((availableWidth - width) / 2));
  const int16_t totalHeight = static_cast<int16_t>(topSpacing + ruleThickness + bottomSpacing);

  if (!currentPage->elements.empty() && currentPageNextY + totalHeight > viewportHeight) {
    emitPage(lastBodyChildByteOffset);
    if (!startNewPage("horizontal-rule page break")) {
      return;
    }
  }

  currentPageNextY += topSpacing;

  auto pageRule = std::shared_ptr<PageHorizontalRule>(
      new (std::nothrow) PageHorizontalRule(width, ruleThickness, xPos, currentPageNextY));
  if (!pageRule) {
    LOG_ERR("EHP", "Failed to create PageHorizontalRule");
    return;
  }
  currentPage->elements.push_back(pageRule);
  setCurrentPageVisibleOffset(visibleTextOffset);
  currentPageNextY = static_cast<int16_t>(currentPageNextY + ruleThickness + bottomSpacing);

  if (!pendingAnchorId.empty()) {
    anchorData.push_back({std::move(pendingAnchorId), static_cast<uint16_t>(completedPageCount)});
    pendingAnchorId.clear();
  }
}

void XMLCALL ChapterHtmlSlimParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);
  if (const char* colon = std::strrchr(name, ':')) {
    name = colon + 1;
  }

  if (VisibleTextUtils::equalsTag(name, "body")) {
    self->insideBody = true;
  }
  if (self->insideBody && (self->nonVisibleTextDepth > 0 || VisibleTextUtils::isNonVisibleElement(name))) {
    self->nonVisibleTextDepth++;
  }

  if (self->shouldAbortForLowMemory("element start")) {
    return;
  }

  // Middle of skip
  if (self->skipUntilDepth < self->depth) {
    self->depth += 1;
    return;
  }

  if (strcmp(name, "body") == 0 && self->xpathBodyDepth < 0) {
    self->xpathBodyDepth = self->depth;
  }

  if (self->xpathBodyDepth >= 0 && self->depth == self->xpathBodyDepth + 1) {
    if (self->activeParser) {
      const XML_Index byteIndex = XML_GetCurrentByteIndex(self->activeParser);
      if (byteIndex >= 0) {
        self->lastBodyChildByteOffset = static_cast<uint32_t>(byteIndex);
      }
    }
    if (strcmp(name, "p") == 0) {
      self->xpathParagraphIndex++;
    }
  }

  if (self->xpathBodyDepth >= 0 && strcmp(name, "li") == 0) {
    self->xpathListItemIndex++;
  }

  // Extract class, style, and id attributes
  std::string classAttr;
  std::string styleAttr;
  std::string dirAttr;
  if (atts != nullptr) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "class") == 0) {
        classAttr = atts[i + 1];
      } else if (strcmp(atts[i], "style") == 0) {
        styleAttr = atts[i + 1];
      } else if (strcmp(atts[i], "dir") == 0) {
        dirAttr = atts[i + 1];
      } else if (strcmp(atts[i], "id") == 0) {
        const std::string idValue = atts[i + 1];
        if (self->shouldRecordAnchor(name, idValue)) {
          // Defer both anchor recording and TOC page breaks until startNewTextBlock,
          // after the previous block is flushed to pages via makePages().
          // Consecutive non-block elements can supply another ID before that happens;
          // retain the displaced anchor instead of silently overwriting it.
          if (!self->pendingAnchorId.empty()) {
            self->flushPendingAnchor();
          }
          self->pendingAnchorId = idValue;
        }
      }
    }
  }

  auto centeredBlockStyle = BlockStyle();
  centeredBlockStyle.textAlignDefined = true;
  centeredBlockStyle.alignment = CssTextAlign::Center;

  // Compute CSS style for this element early so display:none can short-circuit
  // before tag-specific branches emit any content or metadata.
  CssStyle cssStyle;
  if (self->cssParser) {
    cssStyle = self->cssParser->resolveStyle(name, classAttr);
    if (!styleAttr.empty()) {
      CssStyle inlineStyle = CssParser::parseInlineStyle(styleAttr);
      cssStyle.applyOver(inlineStyle);
    }
  }

  // HTML dir attribute overrides CSS direction (case-insensitive per HTML spec)
  if (!dirAttr.empty()) {
    if (strcasecmp(dirAttr.c_str(), "rtl") == 0) {
      cssStyle.direction = CssTextDirection::Rtl;
      cssStyle.defined.direction = 1;
    } else if (strcasecmp(dirAttr.c_str(), "ltr") == 0) {
      cssStyle.direction = CssTextDirection::Ltr;
      cssStyle.defined.direction = 1;
    }
  }

  // Direction is inherited in HTML/CSS. If this element does not define one, carry
  // the currently active inherited direction into its computed style.
  if (!cssStyle.hasDirection() && self->effectiveDirectionDefined) {
    cssStyle.direction = self->effectiveDirection;
    cssStyle.defined.direction = 1;
  }

  // Skip elements with display:none before all fast paths (tables, links, etc.).
  if (cssStyle.hasDisplay() && cssStyle.display == CssDisplay::None) {
    self->skipUntilDepth = self->depth;
    self->depth += 1;
    return;
  }

  if (strcmp(name, "ruby") == 0) {
    // <ruby> is an inline element: a base that follows text with no whitespace between them
    // continues the same visual word, exactly like <b>/<i> handling in endElement().
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    self->inRuby = true;
    self->rubyStartWordIndex = self->currentTextBlock ? static_cast<int>(self->currentTextBlock->size()) : 0;
    if (self->currentTextBlock) self->currentTextBlock->ensureRubyCapacity();
    self->rubyTextBuffer.clear();
    self->depth += 1;
    return;
  }
  if (strcmp(name, "rt") == 0) {
    self->flushPartWordBuffer();
    self->collectingRubyText = true;
    self->rubyTextBuffer.clear();
    self->depth += 1;
    return;
  }

  // Tables are streamed as plain content. EPUBs often use tables for dialogue/layout;
  // buffering them for grid rendering can make indexing painfully slow on-device.
  if (strcmp(name, "table") == 0) {
    // skip nested tables
    if (self->tableDepth > 0) {
      self->tableDepth += 1;
      return;
    }

    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
    }
    const bool narrativeLayoutTable = hasClassToken(classAttr, "table");
    (void)narrativeLayoutTable;
    self->currentTableBuffer.reset();
    LOG_DBG("EHP", "Table layout fallback: streaming table content (class=%s)",
            classAttr.empty() ? "-" : classAttr.c_str());
    self->tableDepth += 1;
    self->tableRowIndex = 0;
    self->tableColIndex = 0;
    self->depth += 1;
    return;
  }

  if (self->tableDepth == 1 && strcmp(name, "tr") == 0) {
    self->tableRowIndex += 1;
    self->tableColIndex = 0;
    self->depth += 1;
    return;
  }

  if (self->tableDepth == 1 && (strcmp(name, "td") == 0 || strcmp(name, "th") == 0)) {
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
    }
    self->tableColIndex += 1;

    self->currentTableCellColSpan = 1;

    auto tableCellBlockStyle = BlockStyle();
    tableCellBlockStyle.textAlignDefined = true;
    tableCellBlockStyle.alignment =
        cssStyle.hasTextAlign()
            ? cssStyle.textAlign
            : (cssStyle.hasDirection() && cssStyle.direction == CssTextDirection::Rtl ? CssTextAlign::Right
                                                                                      : CssTextAlign::Left);
    if (cssStyle.hasDirection()) {
      tableCellBlockStyle.directionDefined = true;
      tableCellBlockStyle.isRtl = cssStyle.direction == CssTextDirection::Rtl;
    }
    self->currentTableCellIsHeader = strcmp(name, "th") == 0;
    if (self->currentTableCellIsHeader) {
      StyleStackEntry headerStyle;
      headerStyle.depth = self->depth;
      headerStyle.hasBold = true;
      headerStyle.bold = true;
      self->inlineStyleStack.push_back(headerStyle);
      self->updateEffectiveInlineStyle();
    }
    self->startNewTextBlock(tableCellBlockStyle);

    self->depth += 1;
    return;
  }

  if (self->tableDepth == 1 &&
      (matches(name, HEADER_TAGS, std::size(HEADER_TAGS)) || matches(name, BLOCK_TAGS, std::size(BLOCK_TAGS)))) {
    if (strcmp(name, "br") == 0 && self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = false;
    }
    self->depth += 1;
    return;
  }

  if (self->tableDepth == 1 && matches(name, IMAGE_TAGS, std::size(IMAGE_TAGS))) {
    const char* altAttr = getAttribute(atts, "alt");
    if (altAttr && altAttr[0] != '\0') {
      self->syntheticCharacterData = true;
      self->characterData(userData, altAttr, strlen(altAttr));
      self->syntheticCharacterData = false;
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
      }
      self->nextWordContinues = false;
    }
    self->skipUntilDepth = self->depth;
    self->depth += 1;
    return;
  }

  if (self->tableDepth == 1 && strcmp(name, "hr") == 0) {
    self->depth += 1;
    return;
  }

  if (matches(name, IMAGE_TAGS, std::size(IMAGE_TAGS))) {
    std::string src;
    std::string alt;
    if (atts != nullptr) {
      for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "src") == 0) {
          src = atts[i + 1];
        } else if ((strcmp(atts[i], "href") == 0 || strcmp(atts[i], "xlink:href") == 0) && src.empty()) {
          src = atts[i + 1];
        } else if (strcmp(atts[i], "alt") == 0) {
          alt = atts[i + 1];
        }
      }

      // SVG image references may include a fragment identifier. The fragment
      // selects content inside the resource, while extraction needs its path.
      const size_t fragmentPos = src.find('#');
      if (fragmentPos != std::string::npos) src.erase(fragmentPos);

      // Accessibility-only role/aria-hidden attributes do not hide visual content.
      // CSS display:none and the reader's image setting remain the visual controls.
      // imageRendering: 0=display, 1=placeholder (alt text only), 2=suppress entirely
      if (self->imageRendering == 2) {
        self->skipUntilDepth = self->depth;
        self->depth += 1;
        return;
      }

      // Skip image if CSS display:none
      if (cssStyle.hasDisplay() && cssStyle.display == CssDisplay::None) {
        self->skipUntilDepth = self->depth;
        self->depth += 1;
        return;
      }

      if (!src.empty() && self->imageRendering != 1) {
        LOG_DBG("EHP", "Found image: src=%s", src.c_str());

        {
          const auto releaseHeapBefore = MemoryBudget::snapshot();
          if (MemoryBudget::shouldReleaseSdFontCachesForEpubInlineImage(releaseHeapBefore) &&
              self->renderer.releaseSdCardFontForLowMemory(self->fontId)) {
            const auto releaseHeapAfter = MemoryBudget::snapshot();
            LOG_DBG("EHP", "Released SD font caches before image metadata probe: free=%u->%u maxAlloc=%u->%u src=%s",
                    releaseHeapBefore.freeHeap, releaseHeapAfter.freeHeap, releaseHeapBefore.maxAllocHeap,
                    releaseHeapAfter.maxAllocHeap, src.c_str());
          }

          const auto heapBeforeImage = MemoryBudget::snapshot();
          LOG_DBG("EHP", "Heap before image metadata probe: free=%u maxAlloc=%u src=%s", heapBeforeImage.freeHeap,
                  heapBeforeImage.maxAllocHeap, src.c_str());

          // Dimension discovery now streams through a small header probe and image
          // extraction/decoding is deferred until the page is rendered. Do not apply
          // the old decoder-sized heap gate here: on a fragmented but otherwise
          // healthy heap it replaced valid images with their alt text before the
          // lightweight probe had a chance to run. The fallback decoder retains its
          // own allocation guard for formats the probe cannot identify.
          {
            // Resolve the image path relative to the HTML file
            std::string resolvedPath = FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(self->contentBase + src));

            if (ImageDecoderFactory::isFormatSupported(resolvedPath)) {
              if (self->shouldSuppressRepeatedImage(resolvedPath)) {
                self->skipUntilDepth = self->depth;
                self->depth += 1;
                return;
              }

              std::string ext;
              size_t extPos = resolvedPath.rfind('.');
              if (extPos != std::string::npos) {
                ext = resolvedPath.substr(extPos);
              }

              const std::string cachedImagePath = self->imageBasePath + std::to_string(self->imageCounter++) + ext;
              ImageDimensions dims = {0, 0};
              const bool dimensionsRead = self->readImageDimensions(resolvedPath, cachedImagePath, dims);

              if (dimensionsRead) {
                LOG_DBG("EHP", "Image dimensions: %dx%d", dims.width, dims.height);

                int displayWidth = 0;
                int displayHeight = 0;
                const float emSize = static_cast<float>(self->renderer.getFontAscenderSize(self->fontId));
                const CssStyle& imgStyle = cssStyle;
                const bool hasCssHeight = imgStyle.hasImageHeight();
                const bool hasCssWidth = imgStyle.hasImageWidth();

                // Compute effective container width for percentage-based image sizes.
                // If the image is inside a block with horizontal margins/padding (e.g.
                // <div style="margin: 1em 40%">), percentage widths like width:100%
                // should resolve against the container width, not the full viewport.
                int containerWidth = self->viewportWidth;
                if (self->currentTextBlock) {
                  const int inset = self->currentTextBlock->getBlockStyle().totalHorizontalInset();
                  if (inset > 0 && inset < self->viewportWidth) {
                    containerWidth = self->viewportWidth - inset;
                  }
                }

                if (hasCssHeight && hasCssWidth && dims.width > 0 && dims.height > 0) {
                  // Both CSS height and width set: resolve both, then clamp to viewport preserving requested ratio
                  displayHeight = static_cast<int>(
                      imgStyle.imageHeight.toPixels(emSize, static_cast<float>(self->viewportHeight)) + 0.5f);
                  displayWidth =
                      static_cast<int>(imgStyle.imageWidth.toPixels(emSize, static_cast<float>(containerWidth)) + 0.5f);
                  if (displayHeight < 1) displayHeight = 1;
                  if (displayWidth < 1) displayWidth = 1;
                  if (displayWidth > containerWidth || displayHeight > self->viewportHeight) {
                    float scaleX =
                        (displayWidth > containerWidth) ? static_cast<float>(containerWidth) / displayWidth : 1.0f;
                    float scaleY = (displayHeight > self->viewportHeight)
                                       ? static_cast<float>(self->viewportHeight) / displayHeight
                                       : 1.0f;
                    float scale = (scaleX < scaleY) ? scaleX : scaleY;
                    displayWidth = static_cast<int>(displayWidth * scale + 0.5f);
                    displayHeight = static_cast<int>(displayHeight * scale + 0.5f);
                    if (displayWidth < 1) displayWidth = 1;
                    if (displayHeight < 1) displayHeight = 1;
                  }
                  LOG_DBG("EHP", "Display size from CSS height+width: %dx%d", displayWidth, displayHeight);
                } else if (hasCssHeight && !hasCssWidth && dims.width > 0 && dims.height > 0) {
                  // Use CSS height (resolve % against viewport height) and derive width from aspect ratio
                  displayHeight = static_cast<int>(
                      imgStyle.imageHeight.toPixels(emSize, static_cast<float>(self->viewportHeight)) + 0.5f);
                  if (displayHeight < 1) displayHeight = 1;
                  displayWidth =
                      static_cast<int>(displayHeight * (static_cast<float>(dims.width) / dims.height) + 0.5f);
                  if (displayHeight > self->viewportHeight) {
                    displayHeight = self->viewportHeight;
                    // Rescale width to preserve aspect ratio when height is clamped
                    displayWidth =
                        static_cast<int>(displayHeight * (static_cast<float>(dims.width) / dims.height) + 0.5f);
                    if (displayWidth < 1) displayWidth = 1;
                  }
                  if (displayWidth > containerWidth) {
                    displayWidth = containerWidth;
                    // Rescale height to preserve aspect ratio when width is clamped
                    displayHeight =
                        static_cast<int>(displayWidth * (static_cast<float>(dims.height) / dims.width) + 0.5f);
                    if (displayHeight < 1) displayHeight = 1;
                  }
                  if (displayWidth < 1) displayWidth = 1;
                  LOG_DBG("EHP", "Display size from CSS height: %dx%d", displayWidth, displayHeight);
                } else if (hasCssWidth && !hasCssHeight && dims.width > 0 && dims.height > 0) {
                  // Use CSS width (resolve % against container width) and derive height from aspect ratio
                  displayWidth =
                      static_cast<int>(imgStyle.imageWidth.toPixels(emSize, static_cast<float>(containerWidth)) + 0.5f);
                  if (displayWidth > containerWidth) displayWidth = containerWidth;
                  if (displayWidth < 1) displayWidth = 1;
                  displayHeight =
                      static_cast<int>(displayWidth * (static_cast<float>(dims.height) / dims.width) + 0.5f);
                  if (displayHeight > self->viewportHeight) {
                    displayHeight = self->viewportHeight;
                    // Rescale width to preserve aspect ratio when height is clamped
                    displayWidth =
                        static_cast<int>(displayHeight * (static_cast<float>(dims.width) / dims.height) + 0.5f);
                    if (displayWidth < 1) displayWidth = 1;
                  }
                  if (displayHeight < 1) displayHeight = 1;
                  LOG_DBG("EHP", "Display size from CSS width: %dx%d", displayWidth, displayHeight);
                } else {
                  // Scale to fit container while maintaining aspect ratio
                  int maxWidth = containerWidth;
                  int maxHeight = self->viewportHeight;
                  float scaleX = (dims.width > maxWidth) ? (float)maxWidth / dims.width : 1.0f;
                  float scaleY = (dims.height > maxHeight) ? (float)maxHeight / dims.height : 1.0f;
                  float scale = (scaleX < scaleY) ? scaleX : scaleY;
                  if (scale > 1.0f) scale = 1.0f;

                  displayWidth = (int)(dims.width * scale);
                  displayHeight = (int)(dims.height * scale);
                  LOG_DBG("EHP", "Display size: %dx%d (scale %.2f)", displayWidth, displayHeight, scale);
                }

                // Flush any pending text block so it appears before the image
                if (self->partWordBufferIndex > 0) {
                  self->flushPartWordBuffer();
                }
                if (self->currentTextBlock && !self->currentTextBlock->isEmpty()) {
                  const BlockStyle parentBlockStyle = self->currentTextBlock->getBlockStyle();
                  self->startNewTextBlock(parentBlockStyle);
                  if (self->lowMemoryAbort) {
                    return;
                  }
                }

                // Apply vertical margins from the container to the image.
                // Top margin lives on the empty text block (deposited via vertical merge
                // in startNewTextBlock). Bottom margin was stripped by withoutBottom() for
                // deferred application at element close, so read it from the stack.
                int16_t imageMarginTop = 0;
                int16_t imageMarginBottom = 0;
                if (self->currentTextBlock && self->currentTextBlock->isEmpty()) {
                  const auto& bs = self->currentTextBlock->getBlockStyle();
                  imageMarginTop = bs.topInset();
                  if (self->blockStyleStack.size() > 1) {
                    imageMarginBottom = self->blockStyleStack.back().bottomInset();
                  }
                }

                // Create page for image - only break if image won't fit remaining space
                if (self->currentPage && !self->currentPage->elements.empty() &&
                    (self->currentPageNextY + imageMarginTop + displayHeight + imageMarginBottom >
                     self->viewportHeight)) {
                  self->emitPage(self->lastBodyChildByteOffset);
                  if (!self->startNewPage("image page break")) {
                    return;
                  }
                } else if (!self->currentPage) {
                  if (!self->startNewPage("image page")) {
                    return;
                  }
                }

                // A full-height image leaves no room for its container's top
                // margin. Clamp it so the image cannot cross the viewport and
                // be rejected by ImageBlock's bounds check.
                if (self->currentPageNextY + imageMarginTop + displayHeight > self->viewportHeight) {
                  const int room = self->viewportHeight - displayHeight - self->currentPageNextY;
                  imageMarginTop = static_cast<int16_t>(room > 0 ? room : 0);
                }
                self->currentPageNextY += imageMarginTop;

                // Create ImageBlock and add to page
                auto imageBlock = std::shared_ptr<ImageBlock>(
                    new (std::nothrow) ImageBlock(cachedImagePath, resolvedPath, displayWidth, displayHeight));
                if (!imageBlock) {
                  const auto heap = MemoryBudget::snapshot();
                  LOG_ERR("EHP", "Failed to create ImageBlock (%u free, %u max alloc)", heap.freeHeap,
                          heap.maxAllocHeap);
                  self->lowMemoryAbort = true;
                  return;
                }
                int xPos = (self->viewportWidth - displayWidth) / 2;
                auto pageImage =
                    std::shared_ptr<PageImage>(new (std::nothrow) PageImage(imageBlock, xPos, self->currentPageNextY));
                if (!pageImage) {
                  const auto heap = MemoryBudget::snapshot();
                  LOG_ERR("EHP", "Failed to create PageImage (%u free, %u max alloc)", heap.freeHeap,
                          heap.maxAllocHeap);
                  self->lowMemoryAbort = true;
                  return;
                }
                self->currentPage->elements.push_back(pageImage);
                self->setCurrentPageVisibleOffset(self->visibleTextOffset);
                self->currentPageNextY += displayHeight + imageMarginBottom;

                // The image consumed the empty block's accumulated vertical spacing.
                // Reset the block so the Vertical merge in startNewTextBlock doesn't
                // re-apply the same margins to the next text paragraph.
                if (self->currentTextBlock && self->currentTextBlock->isEmpty()) {
                  BlockStyle resetStyle;
                  resetStyle.alignment = (self->paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None))
                                             ? CssTextAlign::Justify
                                             : static_cast<CssTextAlign>(self->paragraphAlignment);
                  self->currentTextBlock->setBlockStyle(resetStyle);
                }

                self->depth += 1;
                return;
              } else {
                self->lowMemoryImageFallback = true;
                LOG_ERR("EHP", "Failed to read image dimensions: %s", resolvedPath.c_str());
              }
            }  // isFormatSupported
          }
        }
      }

      // Fallback to alt text if image processing fails
      if (!alt.empty() && !looksLikeLocalFilePath(alt)) {
        alt = "[Image: " + alt + "]";
        self->startNewTextBlock(self->blockStyleStack.back()
                                    .getCombinedBlockStyle(centeredBlockStyle, BlockStyle::CombineAxis::Horizontal)
                                    .withoutBottom());
        self->italicUntilDepth = std::min(self->italicUntilDepth, self->depth);
        self->depth += 1;
        self->syntheticCharacterData = true;
        self->characterData(userData, alt.c_str(), alt.length());
        self->syntheticCharacterData = false;
        // Skip any child content (skip until parent as we pre-advanced depth above)
        self->skipUntilDepth = self->depth - 1;
        return;
      }

      // No alt text, skip
      self->skipUntilDepth = self->depth;
      self->depth += 1;
      return;
    }
  }

  if (VisibleTextUtils::isNonVisibleElement(name)) {
    // start skip
    self->skipUntilDepth = self->depth;
    self->depth += 1;
    return;
  }

  // Skip blocks with role="doc-pagebreak" and epub:type="pagebreak"
  if (atts != nullptr) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "role") == 0 && strcmp(atts[i + 1], "doc-pagebreak") == 0 ||
          strcmp(atts[i], "epub:type") == 0 && strcmp(atts[i + 1], "pagebreak") == 0) {
        self->skipUntilDepth = self->depth;
        self->depth += 1;
        return;
      }
    }
  }

  // Detect internal <a href="..."> links (footnotes, cross-references)
  // Note: <aside epub:type="footnote"> elements are rendered as normal content
  // without special handling. Links pointing to them are collected as footnotes.
  if (strcmp(name, "a") == 0) {
    const char* href = getAttribute(atts, "href");
    self->collectReferencedAnchor(href);

    bool isInternalLink = isInternalEpubLink(href);

    // Special case: javascript:void(0) links with data attributes
    // Example: <a href="javascript:void(0)"
    // data-xyz="{&quot;name&quot;:&quot;OPS/ch2.xhtml&quot;,&quot;frag&quot;:&quot;id46&quot;}">
    if (href && strncmp(href, "javascript:", 11) == 0) {
      isInternalLink = false;
      // TODO: Parse data-* attributes to extract actual href
    }

    if (isInternalLink) {
      // Flush buffer before style change
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
        self->nextWordContinues = true;
      }
      self->insideFootnoteLink = true;
      self->footnoteLinkDepth = self->depth;
      strncpy(self->currentFootnote.href, href, sizeof(self->currentFootnote.href) - 1);
      self->currentFootnote.href[sizeof(self->currentFootnote.href) - 1] = '\0';
      self->currentFootnote.number[0] = '\0';
      self->currentFootnoteLinkTextLen = 0;
      // Register the target so laid-out words carry a link id (tap-to-follow links).
      self->currentFootnoteLinkId =
          self->currentTextBlock ? self->currentTextBlock->addLinkTarget(self->currentFootnote.href) : 0;

      // Apply underline style to visually indicate the link
      self->underlineUntilDepth = std::min(self->underlineUntilDepth, self->depth);
      StyleStackEntry entry;
      entry.depth = self->depth;
      entry.hasUnderline = true;
      entry.underline = true;
      applyDirectionToEntry(entry, cssStyle);
      applyVerticalAlignToEntry(entry, cssStyle);
      self->inlineStyleStack.push_back(entry);
      self->updateEffectiveInlineStyle();

      // Skip CSS resolution — we already handled styling for this <a> tag
      self->depth += 1;
      return;
    }
  }

  const float emSize = static_cast<float>(self->renderer.getFontAscenderSize(self->fontId));
  auto userAlignmentBlockStyle = BlockStyle::fromCssStyle(
      cssStyle, emSize, static_cast<CssTextAlign>(self->paragraphAlignment), self->viewportWidth);

  if (strcmp(name, "hr") == 0) {
    auto hrBlockStyle = BlockStyle::fromCssStyle(cssStyle, emSize, CssTextAlign::Left, self->viewportWidth);
    if (!self->embeddedStyle) {
      hrBlockStyle.marginLeft = 0;
      hrBlockStyle.marginRight = 0;
      hrBlockStyle.marginTop = 0;
      hrBlockStyle.marginBottom = 0;
      hrBlockStyle.paddingLeft = 0;
      hrBlockStyle.paddingRight = 0;
      hrBlockStyle.paddingTop = 0;
      hrBlockStyle.paddingBottom = 0;
      hrBlockStyle.textIndentDefined = false;
      hrBlockStyle.textIndent = 0;
    }
    self->emitHorizontalRule(hrBlockStyle);
    self->depth += 1;
    return;
  }

  if (self->forceParagraphIndents && strcmp(name, "p") == 0 &&
      (userAlignmentBlockStyle.alignment == CssTextAlign::Justify ||
       userAlignmentBlockStyle.alignment == CssTextAlign::Left) &&
      (!userAlignmentBlockStyle.textIndentDefined || userAlignmentBlockStyle.textIndent == 0)) {
    userAlignmentBlockStyle.textIndentDefined = true;
    userAlignmentBlockStyle.textIndent = static_cast<int16_t>(std::min(emSize, static_cast<float>(INT16_MAX)));
  }

  if (matches(name, HEADER_TAGS, std::size(HEADER_TAGS))) {
    self->currentCssStyle = cssStyle;
    auto headerBlockStyle = BlockStyle::fromCssStyle(cssStyle, emSize, CssTextAlign::Center, self->viewportWidth);
    headerBlockStyle.textAlignDefined = true;
    if (self->embeddedStyle && cssStyle.hasTextAlign()) {
      headerBlockStyle.alignment = cssStyle.textAlign;
    }
    const auto accumulated =
        self->blockStyleStack.back().getCombinedBlockStyle(headerBlockStyle, BlockStyle::CombineAxis::Horizontal);
    self->blockStyleStack.push_back(accumulated);
    self->startNewTextBlock(accumulated.withoutBottom());
    self->boldUntilDepth = std::min(self->boldUntilDepth, self->depth);
    self->updateEffectiveInlineStyle();
  } else if (matches(name, BLOCK_TAGS, std::size(BLOCK_TAGS))) {
    if (strcmp(name, "br") == 0) {
      if (self->partWordBufferIndex > 0) {
        // flush word preceding <br/> to currentTextBlock before calling startNewTextBlock
        self->flushPartWordBuffer();
      }
      // A <br> after text is a line break, not a new application of the parent
      // paragraph margins. Consecutive/standalone <br> elements retain the
      // existing empty-block scene-break behavior.
      BlockStyle brStyle = self->blockStyleStack.back();
      if (self->currentTextBlock && !self->currentTextBlock->isEmpty()) {
        brStyle = brStyle.withoutTop().withoutBottom();
      }
      brStyle.fromBrElement = true;
      self->startNewTextBlock(brStyle);
    } else {
      self->currentCssStyle = cssStyle;
      const auto accumulated = self->blockStyleStack.back().getCombinedBlockStyle(userAlignmentBlockStyle,
                                                                                  BlockStyle::CombineAxis::Horizontal);
      self->blockStyleStack.push_back(accumulated);
      self->startNewTextBlock(accumulated.withoutBottom());
      self->updateEffectiveInlineStyle();

      if (strcmp(name, "li") == 0) {
        self->currentTextBlock->addWord("\xe2\x80\xa2", EpdFontFamily::REGULAR, false, false, self->visibleTextOffset);
        self->listItemBulletOnly = true;
      }
    }
  } else if (matches(name, UNDERLINE_TAGS, std::size(UNDERLINE_TAGS))) {
    // Flush buffer before style change so preceding text gets current style
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    self->underlineUntilDepth = std::min(self->underlineUntilDepth, self->depth);
    // Push inline style entry for underline tag
    StyleStackEntry entry;
    entry.depth = self->depth;  // Track depth for matching pop
    entry.hasUnderline = true;
    entry.underline = true;
    if (cssStyle.hasFontWeight()) {
      entry.hasBold = true;
      entry.bold = cssStyle.fontWeight == CssFontWeight::Bold;
    }
    if (cssStyle.hasFontStyle()) {
      entry.hasItalic = true;
      entry.italic = cssStyle.fontStyle == CssFontStyle::Italic;
    }
    if (cssStyle.hasTextDecoration()) {
      entry.hasStrikeThrough = true;
      entry.strikeThrough = hasTextDecoration(cssStyle.textDecoration, CssTextDecoration::LineThrough);
    }
    self->inlineStyleStack.push_back(entry);
    self->updateEffectiveInlineStyle();
  } else if (matches(name, STRIKE_TAGS, std::size(STRIKE_TAGS))) {
    // Flush buffer before style change so preceding text gets current style
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    self->strikeUntilDepth = std::min(self->strikeUntilDepth, self->depth);
    // Push inline style entry for strikethrough tag
    StyleStackEntry entry;
    entry.depth = self->depth;  // Track depth for matching pop
    entry.hasStrikeThrough = true;
    entry.strikeThrough = true;
    if (cssStyle.hasFontWeight()) {
      entry.hasBold = true;
      entry.bold = cssStyle.fontWeight == CssFontWeight::Bold;
    }
    if (cssStyle.hasFontStyle()) {
      entry.hasItalic = true;
      entry.italic = cssStyle.fontStyle == CssFontStyle::Italic;
    }
    if (cssStyle.hasTextDecoration()) {
      entry.hasUnderline = true;
      entry.underline = hasTextDecoration(cssStyle.textDecoration, CssTextDecoration::Underline);
    }
    self->inlineStyleStack.push_back(entry);
    self->updateEffectiveInlineStyle();
  } else if (matches(name, BOLD_TAGS, std::size(BOLD_TAGS))) {
    // Flush buffer before style change so preceding text gets current style
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    self->boldUntilDepth = std::min(self->boldUntilDepth, self->depth);
    // Push inline style entry for bold tag
    StyleStackEntry entry;
    entry.depth = self->depth;  // Track depth for matching pop
    entry.hasBold = true;
    entry.bold = true;
    if (cssStyle.hasFontStyle()) {
      entry.hasItalic = true;
      entry.italic = cssStyle.fontStyle == CssFontStyle::Italic;
    }
    if (cssStyle.hasTextDecoration()) {
      entry.hasUnderline = true;
      entry.underline = hasTextDecoration(cssStyle.textDecoration, CssTextDecoration::Underline);
      entry.hasStrikeThrough = true;
      entry.strikeThrough = hasTextDecoration(cssStyle.textDecoration, CssTextDecoration::LineThrough);
    }
    self->inlineStyleStack.push_back(entry);
    self->updateEffectiveInlineStyle();
  } else if (matches(name, ITALIC_TAGS, std::size(ITALIC_TAGS))) {
    // Flush buffer before style change so preceding text gets current style
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    self->italicUntilDepth = std::min(self->italicUntilDepth, self->depth);
    // Push inline style entry for italic tag
    StyleStackEntry entry;
    entry.depth = self->depth;  // Track depth for matching pop
    entry.hasItalic = true;
    entry.italic = true;
    if (cssStyle.hasFontWeight()) {
      entry.hasBold = true;
      entry.bold = cssStyle.fontWeight == CssFontWeight::Bold;
    }
    if (cssStyle.hasTextDecoration()) {
      entry.hasUnderline = true;
      entry.underline = hasTextDecoration(cssStyle.textDecoration, CssTextDecoration::Underline);
      entry.hasStrikeThrough = true;
      entry.strikeThrough = hasTextDecoration(cssStyle.textDecoration, CssTextDecoration::LineThrough);
    }
    self->inlineStyleStack.push_back(entry);
    self->updateEffectiveInlineStyle();
  } else if (strcmp(name, "sup") == 0 || strcmp(name, "sub") == 0) {
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    StyleStackEntry entry;
    entry.depth = self->depth;
    if (strcmp(name, "sup") == 0) {
      entry.hasSup = true;
      entry.sup = true;
    } else {
      entry.hasSub = true;
      entry.sub = true;
    }
    self->inlineStyleStack.push_back(entry);
    self->updateEffectiveInlineStyle();
  } else if (strcmp(name, "span") == 0 || !isHeaderOrBlock(name)) {
    // Handle span and other inline elements for CSS styling
    if (cssStyle.hasFontWeight() || cssStyle.hasFontStyle() || cssStyle.hasTextDecoration() ||
        cssStyle.hasDirection() || cssStyle.hasVerticalAlign()) {
      // Flush buffer before style change so preceding text gets current style
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
        self->nextWordContinues = true;
      }
      StyleStackEntry entry;
      entry.depth = self->depth;  // Track depth for matching pop
      if (cssStyle.hasFontWeight()) {
        entry.hasBold = true;
        entry.bold = cssStyle.fontWeight == CssFontWeight::Bold;
      }
      if (cssStyle.hasFontStyle()) {
        entry.hasItalic = true;
        entry.italic = cssStyle.fontStyle == CssFontStyle::Italic;
      }
      if (cssStyle.hasTextDecoration()) {
        entry.hasUnderline = true;
        entry.underline = hasTextDecoration(cssStyle.textDecoration, CssTextDecoration::Underline);
        entry.hasStrikeThrough = true;
        entry.strikeThrough = hasTextDecoration(cssStyle.textDecoration, CssTextDecoration::LineThrough);
      }
      applyDirectionToEntry(entry, cssStyle);
      entry.setsParagraphDirection = strcmp(name, "html") == 0 || strcmp(name, "body") == 0;
      if (cssStyle.hasVerticalAlign()) {
        if (cssStyle.verticalAlign == CssVerticalAlign::Super) {
          entry.hasSup = true;
          entry.sup = true;
        } else if (cssStyle.verticalAlign == CssVerticalAlign::Sub) {
          entry.hasSub = true;
          entry.sub = true;
        }
      }
      self->inlineStyleStack.push_back(entry);
      self->updateEffectiveInlineStyle();
    }
  }

  // Unprocessed tag, just increasing depth and continue forward
  self->depth += 1;
}

void XMLCALL ChapterHtmlSlimParser::characterData(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);
  const bool countVisibleOffsets = self->insideBody && self->nonVisibleTextDepth == 0 && !self->syntheticCharacterData;
  const uint32_t callbackVisibleOffset = self->visibleTextOffset;
  if (countVisibleOffsets) {
    const auto* ptr = reinterpret_cast<const unsigned char*>(s);
    const auto* const end = ptr + len;
    while (ptr < end) {
      utf8NextCodepoint(&ptr);
      self->visibleTextOffset++;
    }
  }
  if (self->lowMemoryAbort) {
    return;
  }
  // Skip content of nested table
  if (self->tableDepth > 1) {
    return;
  }

  // Middle of skip
  if (self->skipUntilDepth < self->depth) {
    return;
  }

  if (self->collectingRubyText) {
    self->rubyTextBuffer.append(s, len);
    return;
  }

  // Collect footnote link display text (for the number label)
  // Skip whitespace and brackets to normalize noterefs like "[1]" → "1"
  if (self->insideFootnoteLink) {
    int start = 0;
    int end = len - 1;

    // Example input and output texts:
    // "     [  12  ]   " => "12"
    // "   turn to 256  " => "turn to 256"

    // Ignore leading whitespaces and left square brackets
    while (start < len && (isWhitespace(s[start]) || (s[start] == '['))) {
      ++start;
    }

    // Ignore trailing whitespaces and right square brackets
    while (end >= start && (isWhitespace(s[end]) || (s[end] == ']'))) {
      --end;
    }

    // Extract footnote link text
    for (int i = start; (self->currentFootnoteLinkTextLen < sizeof(self->currentFootnote.number) - 1) && (i <= end);
         ++i) {
      self->currentFootnote.number[self->currentFootnoteLinkTextLen++] = s[i];
    }
    self->currentFootnote.number[self->currentFootnoteLinkTextLen] = '\0';
  }

  uint32_t nextCodepointOffset = callbackVisibleOffset;
  for (int i = 0; i < len; i++) {
    const uint32_t codepointOffset = nextCodepointOffset;
    if (countVisibleOffsets && (static_cast<uint8_t>(s[i]) & 0xC0) != 0x80) {
      nextCodepointOffset++;
    }
    if ((i & 0xFF) == 0) {
      self->serviceLongParse("character data");
    }

    if (isWhitespace(s[i])) {
      // Currently looking at whitespace, if there's anything in the partWordBuffer, flush it
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
      }
      // Whitespace is a real word boundary — reset continuation state
      self->nextWordContinues = false;
      // Skip the whitespace char
      continue;
    }

    // Detect U+00A0 (non-breaking space, UTF-8: 0xC2 0xA0) or
    //        U+202F (narrow no-break space, UTF-8: 0xE2 0x80 0xAF).
    //
    // Both are rendered as a visible space but must never allow a line break around them.
    // We split the no-break space into its own word token and link the surrounding words
    // with continuation flags so the layout engine treats them as an indivisible group.
    //
    // Example: "200&#xA0;Quadratkilometer" or "200&#x202F;Quadratkilometer"
    //   Input bytes:  "200\xC2\xA0Quadratkilometer"  (or 0xE2 0x80 0xAF for U+202F)
    //   Tokens produced:
    //     [0] "200"               continues=false
    //     [1] " "                 continues=true   (attaches to "200", no gap)
    //     [2] "Quadratkilometer"  continues=true   (attaches to " ", no gap)
    //
    //   The continuation flags prevent the line-breaker from inserting a line break
    //   between "200" and "Quadratkilometer". However, "Quadratkilometer" is now a
    //   standalone word for hyphenation purposes, so Liang patterns can produce
    //   "200 Quadrat-" / "kilometer" instead of the unusable "200" / "Quadratkilometer".
    if (static_cast<uint8_t>(s[i]) == 0xC2 && i + 1 < len && static_cast<uint8_t>(s[i + 1]) == 0xA0) {
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
      }

      self->partWordBuffer[0] = ' ';
      self->partWordBuffer[1] = '\0';
      self->partWordBufferIndex = 1;
      self->partWordVisibleOffset = codepointOffset;
      self->nextWordContinues = true;  // Attach space to previous word (no break).
      self->flushPartWordBuffer();

      self->nextWordContinues = true;  // Next real word attaches to this space (no break).

      i++;  // Skip the second byte (0xA0)
      continue;
    }

    // U+202F (narrow no-break space) — identical logic to U+00A0 above.
    if (static_cast<uint8_t>(s[i]) == 0xE2 && i + 2 < len && static_cast<uint8_t>(s[i + 1]) == 0x80 &&
        static_cast<uint8_t>(s[i + 2]) == 0xAF) {
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
      }

      self->partWordBuffer[0] = ' ';
      self->partWordBuffer[1] = '\0';
      self->partWordBufferIndex = 1;
      self->partWordVisibleOffset = codepointOffset;
      self->nextWordContinues = true;
      self->flushPartWordBuffer();

      self->nextWordContinues = true;

      i += 2;  // Skip the remaining two bytes (0x80 0xAF)
      continue;
    }

    // Skip Zero Width No-Break Space / BOM (U+FEFF) = 0xEF 0xBB 0xBF
    const XML_Char FEFF_BYTE_1 = static_cast<XML_Char>(0xEF);
    const XML_Char FEFF_BYTE_2 = static_cast<XML_Char>(0xBB);
    const XML_Char FEFF_BYTE_3 = static_cast<XML_Char>(0xBF);

    if (s[i] == FEFF_BYTE_1) {
      // Check if the next two bytes complete the 3-byte sequence
      if ((i + 2 < len) && (s[i + 1] == FEFF_BYTE_2) && (s[i + 2] == FEFF_BYTE_3)) {
        // Sequence 0xEF 0xBB 0xBF found!
        i += 2;    // Skip the next two bytes
        continue;  // Move to the next iteration
      }
    }

    // If we're about to run out of space, then cut the word off and start a new one.
    // For CJK text (no spaces), this is the primary word-breaking mechanism.
    // We must avoid splitting multi-byte UTF-8 sequences across word boundaries,
    // otherwise the trailing bytes become orphaned continuation bytes that the
    // decoder can't interpret.
    if (self->partWordBufferIndex >= MAX_WORD_SIZE) {
      int safeLen = utf8SafeTruncateBuffer(self->partWordBuffer, self->partWordBufferIndex);

      if (safeLen < self->partWordBufferIndex && safeLen > 0) {
        // Incomplete UTF-8 sequence at the end — save it before flushing
        int overflow = self->partWordBufferIndex - safeLen;
        uint32_t overflowVisibleOffset = self->partWordVisibleOffset;
        const auto* offsetPtr = reinterpret_cast<const unsigned char*>(self->partWordBuffer);
        const auto* const safeEnd = offsetPtr + safeLen;
        while (offsetPtr < safeEnd) {
          utf8NextCodepoint(&offsetPtr);
          overflowVisibleOffset++;
        }
        char saved[4];
        for (int j = 0; j < overflow; j++) {
          saved[j] = self->partWordBuffer[safeLen + j];
        }
        self->partWordBufferIndex = safeLen;
        self->flushPartWordBuffer();
        self->nextWordContinues = true;
        for (int j = 0; j < overflow; j++) {
          self->partWordBuffer[j] = saved[j];
        }
        self->partWordBufferIndex = overflow;
        self->partWordVisibleOffset = overflowVisibleOffset;
      } else {
        self->flushPartWordBuffer();
        self->nextWordContinues = true;
      }
    }

    if (self->partWordBufferIndex == 0) {
      self->partWordVisibleOffset = codepointOffset;
    }
    self->partWordBuffer[self->partWordBufferIndex++] = s[i];
  }

  // If a text block grows too large, perform layout and consume all but the last line
  // There should be enough here to build out 1-2 full pages and doing this will free up a lot of
  // memory.
  // Spotted when reading Intermezzo, there are some really long text blocks in there.
  self->flushLongTextBlockIfNeeded();
}

void XMLCALL ChapterHtmlSlimParser::defaultHandlerExpand(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);
  if (self->lowMemoryAbort) {
    return;
  }

  // Check if this looks like an entity reference (&...;)
  if (len >= 3 && s[0] == '&' && s[len - 1] == ';') {
    const char* utf8Value = lookupHtmlEntity(s, static_cast<size_t>(len));
    if (utf8Value != nullptr) {
      // Known entity: expand to its UTF-8 value
      characterData(userData, utf8Value, strlen(utf8Value));
      return;
    }
    // Unknown entity: preserve original &...; sequence
    characterData(userData, s, len);
    return;
  }
  // Not an entity we recognize - skip it
}

void XMLCALL ChapterHtmlSlimParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);
  if (self->lowMemoryAbort) {
    return;
  }

  if (const char* colon = std::strrchr(name, ':')) {
    name = colon + 1;
  }

  if (self->nonVisibleTextDepth > 0) {
    self->nonVisibleTextDepth--;
  }

  if (strcmp(name, "rt") == 0) {
    self->collectingRubyText = false;
    if (self->inRuby && self->currentTextBlock) {
      const int currentWordCount = static_cast<int>(self->currentTextBlock->size());
      const int baseWordCount = currentWordCount - self->rubyStartWordIndex;
      const std::string cleanRuby = trimAndNormalize(self->rubyTextBuffer);
      if (!cleanRuby.empty() && baseWordCount > 0) {
        self->currentTextBlock->setRubyGroupAt(static_cast<size_t>(self->rubyStartWordIndex),
                                               static_cast<size_t>(baseWordCount), cleanRuby);
        self->rubyStartWordIndex = currentWordCount;
      }
    }
    self->rubyTextBuffer.clear();
    self->depth -= 1;
    return;
  }
  if (strcmp(name, "ruby") == 0 && self->inRuby) {
    self->inRuby = false;
    self->rubyStartWordIndex = -1;
    self->rubyTextBuffer.clear();
    self->depth -= 1;
    return;
  }

  // Check if any style state will change after we decrement depth
  // If so, we MUST flush the partWordBuffer with the CURRENT style first
  // Note: depth hasn't been decremented yet, so we check against (depth - 1)
  const bool willPopStyleStack =
      !self->inlineStyleStack.empty() && self->inlineStyleStack.back().depth == self->depth - 1;
  const bool willClearBold = self->boldUntilDepth == self->depth - 1;
  const bool willClearItalic = self->italicUntilDepth == self->depth - 1;
  const bool willClearUnderline = self->underlineUntilDepth == self->depth - 1;
  const bool willClearStrike = self->strikeUntilDepth == self->depth - 1;

  const bool styleWillChange =
      willPopStyleStack || willClearBold || willClearItalic || willClearUnderline || willClearStrike;
  const bool headerOrBlockTag = isHeaderOrBlock(name);
  const bool tableStructuralTag = isTableStructuralTag(name);

  if (self->tableDepth > 1 && strcmp(name, "table") == 0) {
    // get rid of all text inside the nested table
    self->partWordBufferIndex = 0;
    self->tableDepth -= 1;
    LOG_DBG("EHP", "nested table detected, get rid of its content");
    return;
  }

  // Flush buffer with current style BEFORE any style changes
  if (self->partWordBufferIndex > 0) {
    // Flush if style will change OR if we're closing a block/structural element
    const bool isInlineTag = !headerOrBlockTag && !tableStructuralTag &&
                             !matches(name, IMAGE_TAGS, std::size(IMAGE_TAGS)) && self->depth != 1;
    const bool shouldFlush = styleWillChange || headerOrBlockTag || matches(name, BOLD_TAGS, std::size(BOLD_TAGS)) ||
                             matches(name, ITALIC_TAGS, std::size(ITALIC_TAGS)) ||
                             matches(name, UNDERLINE_TAGS, std::size(UNDERLINE_TAGS)) ||
                             matches(name, STRIKE_TAGS, std::size(STRIKE_TAGS)) || tableStructuralTag ||
                             matches(name, IMAGE_TAGS, std::size(IMAGE_TAGS)) || self->depth == 1;

    if (shouldFlush) {
      self->flushPartWordBuffer();
      // If closing an inline element, the next word fragment continues the same visual word
      if (isInlineTag) {
        self->nextWordContinues = true;
      }
    }
  }

  self->depth -= 1;

  // Closing a footnote link — create entry from collected text and href
  if (self->insideFootnoteLink && self->depth == self->footnoteLinkDepth) {
    if (self->currentFootnote.number[0] != '\0' && self->currentFootnote.href[0] != '\0') {
      FootnoteEntry entry;
      strncpy(entry.number, self->currentFootnote.number, sizeof(entry.number) - 1);
      entry.number[sizeof(entry.number) - 1] = '\0';
      strncpy(entry.href, self->currentFootnote.href, sizeof(entry.href) - 1);
      entry.href[sizeof(entry.href) - 1] = '\0';
      int wordIndex =
          self->wordsExtractedInBlock + (self->currentTextBlock ? static_cast<int>(self->currentTextBlock->size()) : 0);
      self->pendingFootnotes.push_back({wordIndex, entry});
    }
    self->insideFootnoteLink = false;
    self->currentFootnoteLinkId = 0;
  }

  // Leaving skip
  if (self->skipUntilDepth == self->depth) {
    self->skipUntilDepth = INT_MAX;
  }

  if (self->tableDepth == 1 && (strcmp(name, "td") == 0 || strcmp(name, "th") == 0)) {
    self->finalizeCurrentTableCell();
    self->nextWordContinues = false;
  }

  if (self->tableDepth == 1 && (strcmp(name, "tr") == 0)) {
    self->nextWordContinues = false;
  }

  if (self->tableDepth == 1 && strcmp(name, "table") == 0) {
    self->tableDepth -= 1;
    self->tableRowIndex = 0;
    self->tableColIndex = 0;
    auto paragraphAlignmentBlockStyle = BlockStyle();
    paragraphAlignmentBlockStyle.textAlignDefined = true;
    paragraphAlignmentBlockStyle.alignment = (self->paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None))
                                                 ? CssTextAlign::Justify
                                                 : static_cast<CssTextAlign>(self->paragraphAlignment);
    self->startNewTextBlock(paragraphAlignmentBlockStyle);
    self->nextWordContinues = false;
  }

  // Leaving bold tag
  if (self->boldUntilDepth == self->depth) {
    self->boldUntilDepth = INT_MAX;
  }

  // Leaving italic tag
  if (self->italicUntilDepth == self->depth) {
    self->italicUntilDepth = INT_MAX;
  }

  // Leaving underline tag
  if (self->underlineUntilDepth == self->depth) {
    self->underlineUntilDepth = INT_MAX;
  }

  // Leaving strikethrough tag
  if (self->strikeUntilDepth == self->depth) {
    self->strikeUntilDepth = INT_MAX;
  }

  // Pop from inline style stack if we pushed an entry at this depth
  // This handles all inline elements: b, i, u, span, etc.
  if (!self->inlineStyleStack.empty() && self->inlineStyleStack.back().depth == self->depth) {
    self->inlineStyleStack.pop_back();
    self->updateEffectiveInlineStyle();
  }

  // Clear block style when leaving header or block elements
  if (headerOrBlockTag) {
    self->currentCssStyle.reset();
    self->updateEffectiveInlineStyle();

    // br is self-closing and not a container — it doesn't push/pop the stack.
    if (strcmp(name, "br") != 0 && self->blockStyleStack.size() > 1) {
      // Apply closing element's bottom margin to the current text block so
      // container spacing appears after the element's content (on the last child),
      // not on the first child via the empty-block merge in startNewTextBlock.
      if (self->currentTextBlock) {
        const auto style = self->currentTextBlock->getBlockStyle();
        self->currentTextBlock->setBlockStyle(style.addBottom(self->blockStyleStack.back()));
      }
      self->blockStyleStack.pop_back();
      // Subsequent bare text must inherit the parent, not the style of the
      // block that just closed (alignment, margins, and padding included).
      // Vertical margins and paddings are stripped.
      self->startNewTextBlock(self->blockStyleStack.back().withoutTop().withoutBottom());
      self->updateEffectiveInlineStyle();
    }

    // </li> closes: if the bullet never got inline text (empty <li> or <li> with only
    // block children that were flushed), clear the flag so the next sibling doesn't
    // merge into this block.
    if (strcmp(name, "li") == 0) {
      self->listItemBulletOnly = false;
    }
  }
  if (VisibleTextUtils::equalsTag(name, "body")) {
    self->insideBody = false;
  }
  if (VisibleTextUtils::equalsTag(name, "html")) {
    self->htmlEnded_ = true;
  }
}

ChapterHtmlSlimParser::~ChapterHtmlSlimParser() { abortParse(); }

bool ChapterHtmlSlimParser::beginParse() {
  abortParse();
  htmlEnded_ = false;
  lastLongParseServiceMs = 0;
  lowMemoryAbort = false;
  attemptedTextLayoutFontCacheRelease = false;
  loggedSoftLowMemoryContinuation = false;

  // The optional anchor pre-scan used to be the first allocating operation in
  // a section build. On a fragmented X3 heap even carry.reserve(256) could
  // therefore throw std::bad_alloc and terminate the firmware before the
  // normal low-memory recovery had a chance to release font/CSS caches.
  if (shouldAbortForLowMemory("anchor scan setup")) {
    return false;
  }

  const auto anchorScanHeap = MemoryBudget::snapshot();
  if (MemoryBudget::hasHeap(anchorScanHeap, SOFT_MIN_FREE_HEAP_FOR_TEXT_LAYOUT, SOFT_MIN_MAX_ALLOC_FOR_TEXT_LAYOUT)) {
    collectReferencedAnchors();
  } else {
    referencedAnchors.clear();
    LOG_DBG("EHP", "Skipped anchor pre-scan on constrained heap (free=%u maxAlloc=%u)", anchorScanHeap.freeHeap,
            anchorScanHeap.maxAllocHeap);
  }

  // A chapter with many distinct href targets can lower the heap while being
  // scanned. Re-run the recovery gate before vectors and Expat start allocating.
  if (shouldAbortForLowMemory("parser setup")) {
    return false;
  }

  // Initialize block style stack with a root entry representing "no ancestor block elements".
  // The user's paragraph alignment is set as the default so child elements without explicit
  // text-align inherit it correctly through getCombinedBlockStyle.
  BlockStyle rootBlockStyle;
  rootBlockStyle.alignment = (this->paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None))
                                 ? CssTextAlign::Justify
                                 : static_cast<CssTextAlign>(this->paragraphAlignment);
  blockStyleStack.clear();
  blockStyleStack.reserve(8);
  blockStyleStack.push_back(rootBlockStyle);

  auto paragraphAlignmentBlockStyle = BlockStyle();
  paragraphAlignmentBlockStyle.textAlignDefined = true;
  const auto align = rootBlockStyle.alignment;
  paragraphAlignmentBlockStyle.alignment = align;
  startNewTextBlock(paragraphAlignmentBlockStyle);

  xmlParser_ = XML_ParserCreate(nullptr);
  if (!xmlParser_) {
    LOG_ERR("EHP", "Couldn't allocate memory for parser");
    return false;
  }
  activeParser = xmlParser_;
  xpathBodyDepth = -1;
  lastBodyChildByteOffset = 0;
  xpathParagraphIndex = 0;
  xpathListItemIndex = 0;

  // Handle HTML entities (like &nbsp;) that aren't in XML spec or DTD
  // Using DefaultHandlerExpand preserves normal entity expansion from DOCTYPE
  XML_SetDefaultHandlerExpand(xmlParser_, defaultHandlerExpand);

  if (!Storage.openFileForRead("EHP", filepath, parseFile_)) {
    abortParse();
    return false;
  }

  // Get file size to decide whether to show indexing popup.
  if (popupFn && parseFile_.size() >= MIN_SIZE_FOR_POPUP) {
    popupFn();
  }

  XML_SetUserData(xmlParser_, this);
  XML_SetElementHandler(xmlParser_, startElement, endElement);
  XML_SetCharacterDataHandler(xmlParser_, characterData);

  parseStartTime_ = millis();
  return true;
}

ChapterHtmlSlimParser::ParseStatus ChapterHtmlSlimParser::parseStep() {
  if (!xmlParser_ || !parseFile_) return ParseStatus::Error;
  void* const buf = XML_GetBuffer(xmlParser_, PARSE_BUFFER_SIZE);
  if (!buf) {
    LOG_ERR("EHP", "Couldn't allocate memory for buffer");
    return ParseStatus::Error;
  }

  const size_t len = parseFile_.read(buf, PARSE_BUFFER_SIZE);

  if (len == 0 && parseFile_.available() > 0) {
    LOG_ERR("EHP", "File read error");
    return ParseStatus::Error;
  }

  const bool done = parseFile_.available() == 0;

  if (XML_ParseBuffer(xmlParser_, static_cast<int>(len), done) == XML_STATUS_ERROR) {
    if (htmlEnded_) {
      LOG_DBG("EHP", "Ignoring trailing data after </html>: %s", XML_ErrorString(XML_GetErrorCode(xmlParser_)));
      return ParseStatus::Done;
    }
    LOG_ERR("EHP", "Parse error at line %lu:\n%s", XML_GetCurrentLineNumber(xmlParser_),
            XML_ErrorString(XML_GetErrorCode(xmlParser_)));
    return ParseStatus::Error;
  }

  serviceLongParse("parse buffer");

  if (lowMemoryAbort) {
    const auto heap = MemoryBudget::snapshot();
    LOG_ERR("EHP", "Aborting parse because of low heap (free=%u, maxAlloc=%u)", heap.freeHeap, heap.maxAllocHeap);
    return ParseStatus::Error;
  }
  return done ? ParseStatus::Done : ParseStatus::More;
}

void ChapterHtmlSlimParser::abortParse() {
  activeParser = nullptr;
  if (xmlParser_) {
    destroyXmlParser(xmlParser_);
    xmlParser_ = nullptr;
  }
  if (parseFile_.isOpen()) parseFile_.close();
}

bool ChapterHtmlSlimParser::finishParse() {
  LOG_DBG("EHP", "Time to parse and build pages: %lu ms", millis() - parseStartTime_);
  activeParser = nullptr;
  if (xmlParser_) {
    destroyXmlParser(xmlParser_);
    xmlParser_ = nullptr;
  }
  if (parseFile_.isOpen()) parseFile_.close();

  // Process last page if there is still text
  if (!lowMemoryAbort && currentTextBlock) {
    makePages();
    if (lowMemoryAbort) {
      currentPage.reset();
      currentTextBlock.reset();
      return false;
    }
    if (!pendingAnchorId.empty()) {
      anchorData.push_back({std::move(pendingAnchorId), static_cast<uint16_t>(completedPageCount)});
      pendingAnchorId.clear();
    }
    emitPage(0u);
    currentPage.reset();
    currentTextBlock.reset();
  }

  return !lowMemoryAbort;
}

bool ChapterHtmlSlimParser::parseAndBuildPages() {
  if (!beginParse()) return false;
  for (;;) {
    const ParseStatus status = parseStep();
    if (status == ParseStatus::Error) {
      abortParse();
      return false;
    }
    if (status == ParseStatus::Done) break;
  }
  return finishParse();
}

void ChapterHtmlSlimParser::addLineToPage(std::shared_ptr<TextBlock> line, const uint32_t visibleOffset) {
  serviceLongParse("line layout");

  if (!line || !line->valid()) {
    lowMemoryAbort = true;
    LOG_ERR("EHP", "Aborting layout: could not allocate flattened text line");
    return;
  }

  if (shouldAbortForLowMemory("line layout")) {
    return;
  }

  const int lineHeight =
      renderer.getLineHeight(fontId) * lineCompression + line->getRubyShift(renderer.getFontAscenderSize(fontId));

  if (!currentPage) {
    if (!startNewPage("line layout")) {
      return;
    }
  }

  if (currentPageNextY + lineHeight > viewportHeight) {
    emitPage(lastBodyChildByteOffset);
    if (!startNewPage("line page break")) {
      return;
    }
  }

  setCurrentPageVisibleOffset(visibleOffset);

  // Track cumulative words to assign footnotes to the page containing their anchor
  wordsExtractedInBlock += line->wordCount();
  auto footnoteIt = pendingFootnotes.begin();
  while (footnoteIt != pendingFootnotes.end() && footnoteIt->first <= wordsExtractedInBlock) {
    currentPage->addFootnote(footnoteIt->second.number, footnoteIt->second.href);
    ++footnoteIt;
  }
  pendingFootnotes.erase(pendingFootnotes.begin(), footnoteIt);

  // Apply horizontal left inset (margin + padding) as x position offset
  const int16_t xOffset = line->getBlockStyle().leftInset();
  auto pageLine = std::shared_ptr<PageLine>(new (std::nothrow) PageLine(line, xOffset, currentPageNextY));
  if (!pageLine) {
    const auto heap = MemoryBudget::snapshot();
    LOG_ERR("EHP", "Failed to create PageLine (%u free, %u max alloc)", heap.freeHeap, heap.maxAllocHeap);
    lowMemoryAbort = true;
    return;
  }
  // Internal-link rectangles for touch navigation (layout-only metadata moved onto the page).
  const int rubyShift = line->getRubyShift(renderer.getFontAscenderSize(fontId));
  const int baseLineHeight = renderer.getLineHeight(fontId, lineCompression);
  for (const auto& link : line->takeLinkSpans()) {
    if (!currentPage->addLink(link.href, static_cast<int16_t>(xOffset + link.x),
                              static_cast<int16_t>(currentPageNextY + rubyShift - link.topLift), link.width,
                              static_cast<int16_t>(baseLineHeight + link.topLift))) {
      LOG_DBG("EHP", "Dropped page link: %.48s", link.href);
    }
  }
  currentPage->elements.push_back(pageLine);
  currentPageNextY += lineHeight;
}

void ChapterHtmlSlimParser::makePages() {
  serviceLongParse("text layout");

  if (shouldAbortForLowMemory("text layout")) {
    return;
  }

  if (!currentTextBlock) {
    LOG_ERR("EHP", "!! No text block to make pages for !!");
    return;
  }

  if (!currentPage) {
    if (!startNewPage("text layout")) {
      return;
    }
  }

  const int lineHeight = renderer.getLineHeight(fontId) * lineCompression;

  // Apply top spacing before the paragraph (stored in pixels)
  const BlockStyle& blockStyle = currentTextBlock->getBlockStyle();
  if (blockStyle.marginTop > 0) {
    currentPageNextY += blockStyle.marginTop;
  }
  if (blockStyle.paddingTop > 0) {
    currentPageNextY += blockStyle.paddingTop;
  }

  // Calculate effective width accounting for horizontal margins/padding
  const int horizontalInset = blockStyle.totalHorizontalInset();
  const uint16_t effectiveWidth =
      (horizontalInset < viewportWidth) ? static_cast<uint16_t>(viewportWidth - horizontalInset) : viewportWidth;

  currentTextBlock->layoutAndExtractLines(
      renderer, fontId, effectiveWidth,
      [this](const std::shared_ptr<TextBlock>& textBlock, const uint32_t offset) { addLineToPage(textBlock, offset); });

  // Fallback: transfer any remaining pending footnotes to current page.
  // Normally addLineToPage handles this via word-index tracking, but this catches
  // edge cases where a footnote's word index equals the exact block size.
  if (!pendingFootnotes.empty() && currentPage) {
    for (const auto& [idx, fn] : pendingFootnotes) {
      currentPage->addFootnote(fn.number, fn.href);
    }
    pendingFootnotes.clear();
  }

  // Apply bottom spacing after the paragraph (stored in pixels)
  if (blockStyle.marginBottom > 0) {
    currentPageNextY += blockStyle.marginBottom;
  }
  if (blockStyle.paddingBottom > 0) {
    currentPageNextY += blockStyle.paddingBottom;
  }

  // Extra paragraph spacing if enabled (default behavior)
  if (extraParagraphSpacing) {
    currentPageNextY += lineHeight / 2;
  }
}
