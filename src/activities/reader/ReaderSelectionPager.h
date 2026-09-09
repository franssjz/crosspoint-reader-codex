#pragma once
#include <Epub.h>
#include <Epub/Page.h>
#include <Epub/Section.h>
#include <GfxRenderer.h>
#include <Memory.h>

#include "SelectionPageStepper.h"

class ReaderSelectionBackend {
  std::shared_ptr<Epub> epub;
  GfxRenderer& renderer;
  ReaderRenderSpec spec;
  Section* initial;
  int initialSpine;
  std::unique_ptr<Section> extra;
  int extraSpine = -1;
  Section* selected = nullptr;
  bool extraNeedsBuild = false;

 public:
  ReaderSelectionBackend(std::shared_ptr<Epub> epub, GfxRenderer& renderer, ReaderRenderSpec spec, Section* initial,
                         int initialSpine)
      : epub(std::move(epub)), renderer(renderer), spec(spec), initial(initial), initialSpine(initialSpine) {}
  int spineCount() const { return epub->getSpineItemsCount(); }
  bool selectSpine(int index) {
    if (index == initialSpine) {
      selected = initial;
      return selected != nullptr;
    }
    if (index != extraSpine) {
      if (initial && initial->isBuilding()) initial->suspendBuild();
      extra.reset();
      extraSpine = -1;
      selected = nullptr;
      extra = makeUniqueNoThrow<Section>(epub, index, renderer);
      if (!extra) return false;
      extraNeedsBuild = !extra->loadSectionFile(spec);
      extraSpine = index;
    }
    selected = extra.get();
    return selected != nullptr;
  }
  int pageCount() const { return selected->pageCount; }
  bool hasMore() const {
    return (selected == extra.get() && extraNeedsBuild) || selected->isPartial() || selected->isBuilding();
  }
  bool buildStep() {
    GfxRenderer::FrameBufferLoan loan(renderer);
    if (!selected->isBuilding() && !selected->startBuild(spec)) return false;
    if (selected == extra.get()) extraNeedsBuild = false;
    return selected->buildSomeMore(1);
  }
  std::shared_ptr<Page> readPage(int pageIndex) { return selected->loadPage(pageIndex); }
  bool hasText(const Page& page) const {
    bool found = false;
    page.forEachTextLine([&](const TextBlock& block, int, int, uint16_t) {
      for (uint16_t i = 0; i < block.wordCount(); ++i) {
        for (const char* p = block.wordText(i); *p; ++p) {
          if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
            found = true;
            return false;
          }
        }
      }
      return true;
    });
    return found;
  }
};

using ReaderSelectionPager = SelectionPageStepper<ReaderSelectionBackend>;
