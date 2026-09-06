#include "Page.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Serialization.h>

#include <new>

namespace {
constexpr uint8_t MAX_TABLE_ROWS_PER_FRAGMENT = 64;
constexpr uint8_t MAX_TABLE_CELLS_PER_ROW = 8;
constexpr uint8_t MAX_TABLE_LINES_PER_CELL = 64;
constexpr uint16_t MAX_PAGE_ELEMENTS_PER_PAGE = 128;
static_assert(TableFragmentCell::MAX_SERIALIZED_LINES == MAX_TABLE_LINES_PER_CELL);
static_assert(TableFragmentRow::MAX_SERIALIZED_CELLS == MAX_TABLE_CELLS_PER_ROW);
static_assert(PageTableFragment::MAX_SERIALIZED_ROWS == MAX_TABLE_ROWS_PER_FRAGMENT);
}  // namespace

void PageLine::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset,
                      const uint8_t bionicReadingMode) {
  if (!block) return;
  block->render(renderer, fontId, xPos + xOffset, yPos + yOffset, bionicReadingMode);
}

bool PageLine::serialize(HalFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);

  // serialize TextBlock pointed to by PageLine
  if (!block) return false;
  return block->serialize(file);
}

std::unique_ptr<PageLine> PageLine::deserialize(HalFile& file) {
  int16_t xPos;
  int16_t yPos;
  serialization::readPod(file, xPos);
  serialization::readPod(file, yPos);

  auto tb = TextBlock::deserialize(file);
  if (!tb) {
    LOG_ERR("PGE", "Deserialization failed: invalid text block");
    return nullptr;
  }
  std::shared_ptr<TextBlock> sharedBlock(std::move(tb));
  auto* line = new (std::nothrow) PageLine(std::move(sharedBlock), xPos, yPos);
  if (!line) {
    LOG_ERR("PGE", "Deserialization failed: could not allocate PageLine");
    return nullptr;
  }
  return std::unique_ptr<PageLine>(line);
}

void PageImage::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset,
                       const uint8_t /*bionicReadingMode*/) {
  // Images don't use fontId or text rendering
  if (!imageBlock) return;
  imageBlock->render(renderer, xPos + xOffset, yPos + yOffset);
}

void PageImage::renderPlaceholder(GfxRenderer& renderer, const int xOffset, const int yOffset) const {
  if (!imageBlock) return;
  imageBlock->renderPlaceholder(renderer, xPos + xOffset, yPos + yOffset);
}

bool PageImage::serialize(HalFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);

  // serialize ImageBlock
  if (!imageBlock) return false;
  return imageBlock->serialize(file);
}

std::unique_ptr<PageImage> PageImage::deserialize(HalFile& file) {
  int16_t xPos;
  int16_t yPos;
  serialization::readPod(file, xPos);
  serialization::readPod(file, yPos);

  auto ib = ImageBlock::deserialize(file);
  if (!ib) {
    LOG_ERR("PGE", "Deserialization failed: invalid image block");
    return nullptr;
  }
  std::shared_ptr<ImageBlock> sharedBlock(std::move(ib));
  auto* image = new (std::nothrow) PageImage(std::move(sharedBlock), xPos, yPos);
  if (!image) {
    LOG_ERR("PGE", "Deserialization failed: could not allocate PageImage");
    return nullptr;
  }
  return std::unique_ptr<PageImage>(image);
}

void PageHorizontalRule::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset,
                                const uint8_t /*bionicReadingMode*/) {
  (void)fontId;
  if (width == 0 || thickness == 0) {
    return;
  }

  renderer.drawLine(xPos + xOffset, yPos + yOffset, xPos + xOffset + width - 1, yPos + yOffset, thickness, true);
}

bool PageHorizontalRule::serialize(HalFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);
  serialization::writePod(file, width);
  serialization::writePod(file, thickness);
  return true;
}

std::unique_ptr<PageHorizontalRule> PageHorizontalRule::deserialize(HalFile& file) {
  int16_t xPos = 0;
  int16_t yPos = 0;
  uint16_t width = 0;
  uint8_t thickness = 0;
  serialization::readPod(file, xPos);
  serialization::readPod(file, yPos);
  serialization::readPod(file, width);
  serialization::readPod(file, thickness);

  if (width == 0 || thickness == 0) {
    LOG_ERR("PGE", "Deserialization failed: invalid horizontal rule metadata (width=%u thickness=%u)", width,
            thickness);
    return nullptr;
  }

  auto* rule = new (std::nothrow) PageHorizontalRule(width, thickness, xPos, yPos);
  if (!rule) {
    LOG_ERR("PGE", "Deserialization failed: could not allocate PageHorizontalRule");
    return nullptr;
  }
  return std::unique_ptr<PageHorizontalRule>(rule);
}

bool TableFragmentCell::serialize(HalFile& file) const {
  if (lines.size() > MAX_TABLE_LINES_PER_CELL) {
    LOG_ERR("PTB", "Serialization failed: cell line count %u exceeds maximum", static_cast<uint32_t>(lines.size()));
    return false;
  }

  serialization::writePod(file, isHeader);
  serialization::writePod(file, static_cast<uint8_t>(lines.size()));
  for (const auto& line : lines) {
    if (!line || !line->serialize(file)) {
      LOG_ERR("PTB", "Serialization failed: invalid table cell line");
      return false;
    }
  }
  return true;
}

bool TableFragmentCell::deserialize(HalFile& file, TableFragmentCell& outCell) {
  uint8_t lineCount = 0;
  serialization::readPod(file, outCell.isHeader);
  serialization::readPod(file, lineCount);
  if (lineCount > MAX_TABLE_LINES_PER_CELL) {
    LOG_ERR("PTB", "Deserialization failed: cell line count %u exceeds maximum", lineCount);
    return false;
  }

  outCell.lines.clear();
  outCell.lines.reserve(lineCount);
  for (uint8_t i = 0; i < lineCount; i++) {
    auto line = TextBlock::deserialize(file);
    if (!line) {
      LOG_ERR("PTB", "Deserialization failed: invalid table cell line");
      return false;
    }
    outCell.lines.push_back(std::move(line));
  }
  return true;
}

bool TableFragmentRow::serialize(HalFile& file) const {
  if (cells.size() > MAX_TABLE_CELLS_PER_ROW) {
    LOG_ERR("PTB", "Serialization failed: row cell count %u exceeds maximum", static_cast<uint32_t>(cells.size()));
    return false;
  }

  serialization::writePod(file, height);
  serialization::writePod(file, headerSeparator);
  serialization::writePod(file, static_cast<uint8_t>(cells.size()));
  for (const auto& cell : cells) {
    if (!cell.serialize(file)) {
      return false;
    }
  }
  return true;
}

bool TableFragmentRow::deserialize(HalFile& file, TableFragmentRow& outRow) {
  uint8_t cellCount = 0;
  serialization::readPod(file, outRow.height);
  serialization::readPod(file, outRow.headerSeparator);
  serialization::readPod(file, cellCount);
  if (cellCount > MAX_TABLE_CELLS_PER_ROW) {
    LOG_ERR("PTB", "Deserialization failed: row cell count %u exceeds maximum", cellCount);
    return false;
  }

  outRow.cells.clear();
  outRow.cells.reserve(cellCount);
  for (uint8_t i = 0; i < cellCount; i++) {
    TableFragmentCell cell;
    if (!TableFragmentCell::deserialize(file, cell)) {
      return false;
    }
    outRow.cells.push_back(std::move(cell));
  }
  return true;
}

uint16_t PageTableFragment::getHeight() const {
  uint16_t total = 1;
  for (const auto& row : rows) {
    total = static_cast<uint16_t>(total + row.height);
  }
  return total;
}

void PageTableFragment::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset,
                               const uint8_t bionicReadingMode) {
  if (columnCount == 0 || columnCount > MAX_TABLE_CELLS_PER_ROW || rows.empty() || width < 2) {
    return;
  }

  const int drawX = xPos + xOffset;
  const int drawY = yPos + yOffset;
  const uint16_t totalHeight = getHeight();

  int16_t columnStarts[MAX_TABLE_CELLS_PER_ROW + 1] = {};
  for (uint8_t i = 0; i < columnCount; i++) {
    columnStarts[i] = static_cast<int16_t>((static_cast<uint32_t>(width) * i) / columnCount);
  }
  columnStarts[columnCount] = static_cast<int16_t>(width - 1);

  renderer.drawRect(drawX, drawY, width, totalHeight, true);
  for (uint8_t i = 1; i < columnCount; i++) {
    const int x = drawX + columnStarts[i];
    renderer.drawLine(x, drawY, x, drawY + totalHeight - 1, true);
  }

  int currentY = 0;
  for (size_t rowIndex = 0; rowIndex < rows.size(); rowIndex++) {
    const auto& row = rows[rowIndex];

    for (size_t colIndex = 0; colIndex < row.cells.size() && colIndex < columnCount; colIndex++) {
      const auto& cell = row.cells[colIndex];
      const int cellTextX = drawX + columnStarts[colIndex] + cellPadding;
      const int cellTextY = drawY + currentY + cellPadding;

      for (size_t lineIndex = 0; lineIndex < cell.lines.size(); lineIndex++) {
        cell.lines[lineIndex]->render(renderer, fontId, cellTextX, cellTextY + static_cast<int>(lineIndex) * lineHeight,
                                      bionicReadingMode);
      }
    }

    currentY += row.height;
    if (rowIndex + 1 < rows.size()) {
      const int lineWidth = row.headerSeparator ? 2 : 1;
      renderer.drawLine(drawX, drawY + currentY, drawX + width - 1, drawY + currentY, lineWidth, true);
    }
  }
}

bool PageTableFragment::serialize(HalFile& file) {
  if (rows.size() > MAX_TABLE_ROWS_PER_FRAGMENT) {
    LOG_ERR("PTB", "Serialization failed: fragment row count %u exceeds maximum", static_cast<uint32_t>(rows.size()));
    return false;
  }

  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);
  serialization::writePod(file, width);
  serialization::writePod(file, columnCount);
  serialization::writePod(file, cellPadding);
  serialization::writePod(file, lineHeight);
  serialization::writePod(file, static_cast<uint8_t>(rows.size()));
  for (const auto& row : rows) {
    if (!row.serialize(file)) {
      return false;
    }
  }
  return true;
}

std::unique_ptr<PageTableFragment> PageTableFragment::deserialize(HalFile& file) {
  int16_t xPos = 0;
  int16_t yPos = 0;
  uint16_t width = 0;
  uint8_t columnCount = 0;
  uint8_t cellPadding = 0;
  uint16_t lineHeight = 0;
  uint8_t rowCount = 0;
  serialization::readPod(file, xPos);
  serialization::readPod(file, yPos);
  serialization::readPod(file, width);
  serialization::readPod(file, columnCount);
  serialization::readPod(file, cellPadding);
  serialization::readPod(file, lineHeight);
  serialization::readPod(file, rowCount);

  if (rowCount == 0 || rowCount > MAX_TABLE_ROWS_PER_FRAGMENT || columnCount == 0 ||
      columnCount > MAX_TABLE_CELLS_PER_ROW || width < 2 || lineHeight == 0) {
    LOG_ERR("PTB", "Deserialization failed: invalid fragment metadata (rows=%u cols=%u width=%u lineHeight=%u)",
            rowCount, columnCount, width, lineHeight);
    return nullptr;
  }

  std::vector<TableFragmentRow> rows;
  rows.reserve(rowCount);
  for (uint8_t i = 0; i < rowCount; i++) {
    TableFragmentRow row;
    if (!TableFragmentRow::deserialize(file, row)) {
      return nullptr;
    }
    rows.push_back(std::move(row));
  }

  auto* fragment =
      new (std::nothrow) PageTableFragment(width, columnCount, cellPadding, lineHeight, std::move(rows), xPos, yPos);
  if (!fragment) {
    LOG_ERR("PTB", "Deserialization failed: could not allocate PageTableFragment");
    return nullptr;
  }
  return std::unique_ptr<PageTableFragment>(fragment);
}

void PageTableFragment::recordFontUsage(FontCacheManager& fontCacheManager, const int fontId,
                                        const uint8_t bionicReadingMode) const {
  for (const auto& row : rows) {
    for (const auto& cell : row.cells) {
      for (const auto& line : cell.lines) {
        if (line) {
          line->recordFontUsage(fontCacheManager, fontId, bionicReadingMode);
        }
      }
    }
  }
}

void Page::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset,
                  const uint8_t bionicReadingMode) const {
  for (auto& element : elements) {
    if (!element) continue;
    element->render(renderer, fontId, xOffset, yOffset, bionicReadingMode);
  }
}

void Page::recordFontUsage(FontCacheManager& fontCacheManager, const int fontId,
                           const uint8_t bionicReadingMode) const {
  for (const auto& element : elements) {
    if (!element) continue;
    if (element->getTag() == TAG_PageLine) {
      const auto& line = static_cast<const PageLine&>(*element);
      if (!line.getBlock()) continue;
      line.getBlock()->recordFontUsage(fontCacheManager, fontId, bionicReadingMode);
    } else if (element->getTag() == TAG_PageTableFragment) {
      const auto& table = static_cast<const PageTableFragment&>(*element);
      table.recordFontUsage(fontCacheManager, fontId, bionicReadingMode);
    }
  }
}

void Page::renderImages(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) const {
  for (const auto& element : elements) {
    if (!element) continue;
    if (element->getTag() == TAG_PageImage) {
      element->render(renderer, fontId, xOffset, yOffset);
    }
  }
}

void Page::renderWithImagePlaceholders(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset,
                                       const uint8_t bionicReadingMode) const {
  for (const auto& element : elements) {
    if (!element) continue;
    if (element->getTag() == TAG_PageImage) {
      static_cast<const PageImage&>(*element).renderPlaceholder(renderer, xOffset, yOffset);
    } else {
      element->render(renderer, fontId, xOffset, yOffset, bionicReadingMode);
    }
  }
}

bool Page::serialize(HalFile& file) const {
  const uint16_t count = elements.size();
  serialization::writePod(file, count);

  for (const auto& el : elements) {
    if (!el) {
      LOG_ERR("PGE", "Serialization failed: null page element");
      return false;
    }
    // Use getTag() method to determine type
    serialization::writePod(file, static_cast<uint8_t>(el->getTag()));

    if (!el->serialize(file)) {
      return false;
    }
  }

  // Serialize footnotes (clamp to MAX_FOOTNOTES_PER_PAGE to match addFootnote/deserialize limits)
  const uint16_t fnCount = std::min<uint16_t>(footnotes.size(), MAX_FOOTNOTES_PER_PAGE);
  serialization::writePod(file, fnCount);
  for (uint16_t i = 0; i < fnCount; i++) {
    const auto& fn = footnotes[i];
    if (file.write(fn.number, sizeof(fn.number)) != sizeof(fn.number) ||
        file.write(fn.href, sizeof(fn.href)) != sizeof(fn.href)) {
      LOG_ERR("PGE", "Failed to write footnote");
      return false;
    }
  }

  const uint16_t linkCount = std::min<uint16_t>(links.size(), MAX_LINKS_PER_PAGE);
  serialization::writePod(file, linkCount);
  for (uint16_t i = 0; i < linkCount; i++) {
    const auto& link = links[i];
    if (file.write(link.href, sizeof(link.href)) != sizeof(link.href)) {
      LOG_ERR("PGE", "Failed to write link %u", i);
      return false;
    }
    serialization::writePod(file, link.x);
    serialization::writePod(file, link.y);
    serialization::writePod(file, link.width);
    serialization::writePod(file, link.height);
  }

  return true;
}

std::unique_ptr<Page> Page::deserialize(HalFile& file) {
  auto page = std::unique_ptr<Page>(new (std::nothrow) Page());
  if (!page) {
    LOG_ERR("PGE", "Deserialization failed: could not allocate page");
    return nullptr;
  }

  uint16_t count;
  serialization::readPod(file, count);
  if (count > MAX_PAGE_ELEMENTS_PER_PAGE) {
    LOG_ERR("PGE", "Deserialization failed: element count %u exceeds maximum", count);
    return nullptr;
  }
  // count has already been validated against a small hard limit, so reserve
  // once instead of repeatedly growing the shared_ptr vector while loading
  // (adapted from upstream fe37bf80).
  page->elements.reserve(count);

  // Reserve up front so a page load costs one allocation for the element vector
  // instead of a grow-copy-free cycle every doubling. `count` is untrusted (it
  // comes straight off the SD cache), so clamp it: a real page holds a few dozen
  // elements, while a corrupt header could ask for 65535 * sizeof(shared_ptr) and
  // abort() on the failed allocation (vector's operator new is throwing, and this
  // firmware builds with -fno-exceptions). Under-reserving is harmless -- the
  // push_back path below still grows normally.
  static constexpr uint16_t RESERVE_CAP = 256;
  page->elements.reserve(std::min(count, RESERVE_CAP));

  for (uint16_t i = 0; i < count; i++) {
    uint8_t tag;
    serialization::readPod(file, tag);

    if (tag == TAG_PageLine) {
      auto pl = PageLine::deserialize(file);
      if (!pl) {
        return nullptr;
      }
      page->elements.push_back(std::move(pl));
    } else if (tag == TAG_PageImage) {
      auto pi = PageImage::deserialize(file);
      if (!pi) {
        return nullptr;
      }
      page->elements.push_back(std::move(pi));
    } else if (tag == TAG_PageHorizontalRule) {
      auto rule = PageHorizontalRule::deserialize(file);
      if (!rule) {
        return nullptr;
      }
      page->elements.push_back(std::move(rule));
    } else if (tag == TAG_PageTableFragment) {
      auto fragment = PageTableFragment::deserialize(file);
      if (!fragment) {
        return nullptr;
      }
      page->elements.push_back(std::move(fragment));
    } else {
      LOG_ERR("PGE", "Deserialization failed: Unknown tag %u", tag);
      return nullptr;
    }
  }

  // Deserialize footnotes
  uint16_t fnCount;
  serialization::readPod(file, fnCount);
  if (fnCount > MAX_FOOTNOTES_PER_PAGE) {
    LOG_ERR("PGE", "Invalid footnote count %u", fnCount);
    return nullptr;
  }
  page->footnotes.resize(fnCount);
  for (uint16_t i = 0; i < fnCount; i++) {
    auto& entry = page->footnotes[i];
    if (file.read(entry.number, sizeof(entry.number)) != sizeof(entry.number) ||
        file.read(entry.href, sizeof(entry.href)) != sizeof(entry.href)) {
      LOG_ERR("PGE", "Failed to read footnote %u", i);
      return nullptr;
    }
    entry.number[sizeof(entry.number) - 1] = '\0';
    entry.href[sizeof(entry.href) - 1] = '\0';
  }

  uint16_t linkCount;
  serialization::readPod(file, linkCount);
  if (linkCount > MAX_LINKS_PER_PAGE) {
    LOG_ERR("PGE", "Invalid link count %u", linkCount);
    return nullptr;
  }
  page->links.resize(linkCount);
  for (uint16_t i = 0; i < linkCount; i++) {
    auto& link = page->links[i];
    if (file.read(link.href, sizeof(link.href)) != sizeof(link.href)) {
      LOG_ERR("PGE", "Failed to read link %u", i);
      return nullptr;
    }
    link.href[sizeof(link.href) - 1] = '\0';
    serialization::readPod(file, link.x);
    serialization::readPod(file, link.y);
    serialization::readPod(file, link.width);
    serialization::readPod(file, link.height);
    if (link.href[0] == '\0' || link.width <= 0 || link.height <= 0) {
      LOG_ERR("PGE", "Invalid link geometry %u", i);
      return nullptr;
    }
  }

  return page;
}
