#pragma once
#include <HalStorage.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "FootnoteEntry.h"
#include "blocks/ImageBlock.h"
#include "blocks/TextBlock.h"

class FontCacheManager;

enum PageElementTag : uint8_t {
  TAG_PageLine = 1,
  TAG_PageImage = 2,
  TAG_PageHorizontalRule = 3,
  TAG_PageTableFragment = 4,
};

// represents something that has been added to a page
class PageElement {
 public:
  int16_t xPos;
  int16_t yPos;
  explicit PageElement(const int16_t xPos, const int16_t yPos) : xPos(xPos), yPos(yPos) {}
  virtual ~PageElement() = default;
  virtual void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset, uint8_t bionicReadingMode = 0) = 0;
  virtual bool serialize(FsFile& file) = 0;
  virtual PageElementTag getTag() const = 0;  // Add type identification
};

// a line from a block element
class PageLine final : public PageElement {
  std::shared_ptr<TextBlock> block;

 public:
  PageLine(std::shared_ptr<TextBlock> block, const int16_t xPos, const int16_t yPos)
      : PageElement(xPos, yPos), block(std::move(block)) {}
  const std::shared_ptr<TextBlock>& getBlock() const { return block; }
  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset, uint8_t bionicReadingMode = 0) override;
  bool serialize(FsFile& file) override;
  PageElementTag getTag() const override { return TAG_PageLine; }
  static std::unique_ptr<PageLine> deserialize(FsFile& file);
};

// New PageImage class
class PageImage final : public PageElement {
  std::shared_ptr<ImageBlock> imageBlock;

 public:
  PageImage(std::shared_ptr<ImageBlock> block, const int16_t xPos, const int16_t yPos)
      : PageElement(xPos, yPos), imageBlock(std::move(block)) {}
  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset, uint8_t bionicReadingMode = 0) override;
  void renderPlaceholder(GfxRenderer& renderer, int xOffset, int yOffset) const;
  bool serialize(FsFile& file) override;
  PageElementTag getTag() const override { return TAG_PageImage; }
  static std::unique_ptr<PageImage> deserialize(FsFile& file);
  const ImageBlock& getImageBlock() const { return *imageBlock; }
};

class PageHorizontalRule final : public PageElement {
  uint16_t width;
  uint8_t thickness;

 public:
  PageHorizontalRule(uint16_t width, uint8_t thickness, const int16_t xPos, const int16_t yPos)
      : PageElement(xPos, yPos), width(width), thickness(thickness) {}

  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset, uint8_t bionicReadingMode = 0) override;
  bool serialize(FsFile& file) override;
  PageElementTag getTag() const override { return TAG_PageHorizontalRule; }
  static std::unique_ptr<PageHorizontalRule> deserialize(FsFile& file);
};

struct TableFragmentCell {
  static constexpr uint8_t MAX_SERIALIZED_LINES = 64;
  bool isHeader = false;
  std::vector<std::shared_ptr<TextBlock>> lines;

  bool serialize(FsFile& file) const;
  static bool deserialize(FsFile& file, TableFragmentCell& outCell);
};

struct TableFragmentRow {
  static constexpr uint8_t MAX_SERIALIZED_CELLS = 8;
  uint16_t height = 0;
  bool headerSeparator = false;
  std::vector<TableFragmentCell> cells;

  bool serialize(FsFile& file) const;
  static bool deserialize(FsFile& file, TableFragmentRow& outRow);
};

class PageTableFragment final : public PageElement {
 public:
  static constexpr uint8_t MAX_SERIALIZED_ROWS = 64;

 private:
  uint16_t width;
  uint8_t columnCount;
  uint8_t cellPadding;
  uint16_t lineHeight;
  std::vector<TableFragmentRow> rows;

 public:
  PageTableFragment(uint16_t width, uint8_t columnCount, uint8_t cellPadding, uint16_t lineHeight,
                    std::vector<TableFragmentRow> rows, const int16_t xPos, const int16_t yPos)
      : PageElement(xPos, yPos),
        width(width),
        columnCount(columnCount),
        cellPadding(cellPadding),
        lineHeight(lineHeight),
        rows(std::move(rows)) {}

  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset, uint8_t bionicReadingMode = 0) override;
  bool serialize(FsFile& file) override;
  PageElementTag getTag() const override { return TAG_PageTableFragment; }
  static std::unique_ptr<PageTableFragment> deserialize(FsFile& file);
  uint16_t getHeight() const;
  void recordFontUsage(FontCacheManager& fontCacheManager, int fontId, uint8_t bionicReadingMode = 0) const;
  template <class Visitor>
  bool forEachTextLine(Visitor& visitor, uint16_t& flow) const {
    if (columnCount == 0 || columnCount > TableFragmentRow::MAX_SERIALIZED_CELLS || width < 2) return true;
    int rowY = yPos;
    for (const auto& row : rows) {
      for (size_t col = 0; col < row.cells.size() && col < columnCount; ++col) {
        ++flow;
        const int cellX = xPos + static_cast<int>((static_cast<uint32_t>(width) * col) / columnCount) + cellPadding;
        int lineY = rowY + cellPadding;
        for (const auto& line : row.cells[col].lines) {
          if (line && !visitor(*line, cellX, lineY, flow)) return false;
          lineY += lineHeight;
        }
      }
      rowY += row.height;
    }
    ++flow;  // Do not join a cell's last hyphen to the paragraph after the table.
    return true;
  }
};

class Page {
 public:
  // Reader text in semantic order, with precisely the positions used by render.
  // Different table cells have separate flow IDs even when they share a screen Y.
  // No temporary PageLines or allocations; false stops a bounded caller's scan.
  template <class Visitor>
  void forEachTextLine(Visitor visitor) const {
    uint16_t flow = 0;
    for (const auto& element : elements) {
      if (!element) continue;
      if (element->getTag() == TAG_PageLine) {
        const auto& line = static_cast<const PageLine&>(*element);
        if (line.getBlock() && !visitor(*line.getBlock(), line.xPos, line.yPos, flow)) return;
      } else if (element->getTag() == TAG_PageTableFragment) {
        if (!static_cast<const PageTableFragment&>(*element).forEachTextLine(visitor, flow)) return;
      }
    }
  }
  // Source position is stored in the section LUT, not in the serialized page body.
  uint32_t visibleTextOffset = 0;
  // the list of block index and line numbers on this page
  std::vector<std::shared_ptr<PageElement>> elements;
  std::vector<FootnoteEntry> footnotes;
  static constexpr uint16_t MAX_FOOTNOTES_PER_PAGE = 16;

  void addFootnote(const char* number, const char* href) {
    if (footnotes.size() >= MAX_FOOTNOTES_PER_PAGE) return;  // Cap per-page footnotes
    FootnoteEntry entry;
    strncpy(entry.number, number, sizeof(entry.number) - 1);
    entry.number[sizeof(entry.number) - 1] = '\0';
    strncpy(entry.href, href, sizeof(entry.href) - 1);
    entry.href[sizeof(entry.href) - 1] = '\0';
    footnotes.push_back(entry);
  }

  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset, uint8_t bionicReadingMode = 0) const;
  void recordFontUsage(FontCacheManager& fontCacheManager, int fontId, uint8_t bionicReadingMode = 0) const;
  void renderImages(GfxRenderer& renderer, int xOffset, int yOffset) const;
  void renderWithImagePlaceholders(GfxRenderer& renderer, int fontId, int xOffset, int yOffset,
                                   uint8_t bionicReadingMode = 0) const;
  bool serialize(FsFile& file) const;
  static std::unique_ptr<Page> deserialize(FsFile& file);

  // Check if page contains any images (used to force full refresh)
  bool hasImages() const {
    return std::any_of(elements.begin(), elements.end(),
                       [](const std::shared_ptr<PageElement>& el) { return el && el->getTag() == TAG_PageImage; });
  }

  bool hasImagesNeedingDecode() const {
    return std::any_of(elements.begin(), elements.end(), [](const std::shared_ptr<PageElement>& element) {
      return element && element->getTag() == TAG_PageImage &&
             static_cast<const PageImage&>(*element).getImageBlock().needsDecode();
    });
  }

  // Get bounding box of all images on the page (union of image rects)
  // Returns false if no images. Coordinates are relative to page origin.
  bool getImageBoundingBox(int16_t& outX, int16_t& outY, int16_t& outW, int16_t& outH) const {
    bool found = false;
    int16_t minX = INT16_MAX, minY = INT16_MAX, maxX = INT16_MIN, maxY = INT16_MIN;
    for (const auto& el : elements) {
      if (!el) continue;
      if (el->getTag() == TAG_PageImage) {
        const auto& img = static_cast<const PageImage&>(*el);
        int16_t x = img.xPos;
        int16_t y = img.yPos;
        int16_t right = x + img.getImageBlock().getWidth();
        int16_t bottom = y + img.getImageBlock().getHeight();
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, right);
        maxY = std::max(maxY, bottom);
        found = true;
      }
    }
    if (found) {
      outX = minX;
      outY = minY;
      outW = maxX - minX;
      outH = maxY - minY;
    }
    return found;
  }
};
