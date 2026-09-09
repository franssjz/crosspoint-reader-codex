#pragma once
#include <cstdint>
#include <memory>
#include <utility>

class Page;
enum class SelectionPageRequest { Previous, Next, Poll, Cancel };
struct SelectionPageResult {
  enum class Status { Pending, Ready, Boundary, Error };
  Status status = Status::Pending;
  std::shared_ptr<Page> page;
  uint16_t spine = 0;
  uint16_t pageIndex = 0;
};

// One bounded build step per loop: even an uncached previous chapter can be
// paginated with responsive cancellation. This owns a selection cursor only;
// it never changes the reader's cursor, progress or statistics.
template <class Backend>
class SelectionPageStepper {
  Backend backend;
  int spine;
  int pageIndex;
  int targetSpine = 0;
  int targetPage = 0;
  bool active = false;
  bool seekingLast = false;
  bool forward = true;

  SelectionPageResult finish(SelectionPageResult::Status status) {
    active = false;
    return {status, {}, static_cast<uint16_t>(spine), static_cast<uint16_t>(pageIndex)};
  }

 public:
  SelectionPageStepper(Backend&& backend, int spine, int pageIndex)
      : backend(std::move(backend)), spine(spine), pageIndex(pageIndex) {}

  SelectionPageResult step(SelectionPageRequest request) {
    using Status = SelectionPageResult::Status;
    if (request == SelectionPageRequest::Cancel) return finish(Status::Boundary);
    if (request != SelectionPageRequest::Poll) {
      forward = request == SelectionPageRequest::Next;
      targetSpine = spine;
      targetPage = pageIndex + (request == SelectionPageRequest::Next ? 1 : -1);
      seekingLast = false;
      active = true;
    }
    if (!active) return finish(Status::Boundary);
    if (targetPage < 0) {
      --targetSpine;
      seekingLast = true;
      targetPage = 0;
    }
    if (targetSpine < 0 || targetSpine >= backend.spineCount() || targetSpine > UINT16_MAX) {
      return finish(Status::Boundary);
    }
    if (!backend.selectSpine(targetSpine)) return finish(Status::Error);
    const int count = backend.pageCount();
    if (backend.hasMore() && (seekingLast || targetPage >= count)) {
      if (!backend.buildStep()) return finish(Status::Error);
      return {Status::Pending};
    }
    if (seekingLast) {
      if (count == 0) {
        --targetSpine;
        return {Status::Pending};
      }
      targetPage = count - 1;
      seekingLast = false;
    } else if (targetPage >= count) {
      ++targetSpine;
      targetPage = 0;
      return {Status::Pending};
    }
    if (targetPage > UINT16_MAX) return finish(Status::Error);
    auto page = backend.readPage(targetPage);
    if (!page) return finish(Status::Error);
    if (!backend.hasText(*page)) {
      targetPage += forward ? 1 : -1;
      return {Status::Pending};
    }
    spine = targetSpine;
    pageIndex = targetPage;
    active = false;
    return {Status::Ready, std::move(page), static_cast<uint16_t>(spine), static_cast<uint16_t>(pageIndex)};
  }
};
