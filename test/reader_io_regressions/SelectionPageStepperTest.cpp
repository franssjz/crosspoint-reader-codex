#include <gtest/gtest.h>

#include <memory>
#include <vector>

class Page {
 public:
  explicit Page(bool hasText = true) : hasText(hasText) {}
  bool hasText;
};

#include "activities/reader/SelectionPageStepper.h"

namespace {
struct FakeState {
  std::vector<std::vector<bool>> pages;
  std::vector<std::vector<bool>> planned;
  int selected = -1;
  int buildCalls = 0;
  bool failBuild = false;
  bool failRead = false;
};

class FakeBackend {
  std::shared_ptr<FakeState> state;

 public:
  explicit FakeBackend(std::shared_ptr<FakeState> state) : state(std::move(state)) {}
  int spineCount() const { return static_cast<int>(state->pages.size()); }
  bool selectSpine(int spine) {
    state->selected = spine;
    return spine >= 0 && spine < spineCount();
  }
  int pageCount() const { return static_cast<int>(state->pages[state->selected].size()); }
  bool hasMore() const { return state->pages[state->selected].size() < state->planned[state->selected].size(); }
  bool buildStep() {
    ++state->buildCalls;
    if (state->failBuild) return false;
    auto& pages = state->pages[state->selected];
    const auto& planned = state->planned[state->selected];
    if (pages.size() < planned.size()) pages.push_back(planned[pages.size()]);
    return true;
  }
  std::shared_ptr<Page> readPage(int page) {
    if (state->failRead) return {};
    return std::make_shared<Page>(state->pages[state->selected][page]);
  }
  bool hasText(const Page& page) const { return page.hasText; }
};

using Stepper = SelectionPageStepper<FakeBackend>;

TEST(SelectionPageStepper, MovesWithinChapterWithoutChangingExternalReaderPosition) {
  auto state = std::make_shared<FakeState>();
  state->pages = {{true, true}};
  state->planned = state->pages;
  const int readerSpine = 0;
  const int readerPage = 0;
  Stepper stepper(FakeBackend(state), readerSpine, readerPage);
  const auto result = stepper.step(SelectionPageRequest::Next);
  ASSERT_EQ(result.status, SelectionPageResult::Status::Ready);
  EXPECT_EQ(result.spine, 0);
  EXPECT_EQ(result.pageIndex, 1);
  EXPECT_EQ(readerSpine, 0);
  EXPECT_EQ(readerPage, 0);
}

TEST(SelectionPageStepper, TraversesChaptersAndSkipsImageOnlyPages) {
  auto state = std::make_shared<FakeState>();
  state->pages = {{true}, {false, true}};
  state->planned = state->pages;
  Stepper stepper(FakeBackend(state), 0, 0);
  EXPECT_EQ(stepper.step(SelectionPageRequest::Next).status, SelectionPageResult::Status::Pending);
  EXPECT_EQ(stepper.step(SelectionPageRequest::Poll).status, SelectionPageResult::Status::Pending);
  const auto result = stepper.step(SelectionPageRequest::Poll);
  ASSERT_EQ(result.status, SelectionPageResult::Status::Ready);
  EXPECT_EQ(result.spine, 1);
  EXPECT_EQ(result.pageIndex, 1);
}

TEST(SelectionPageStepper, BuildsAtMostOncePerPollAndCanCancel) {
  auto state = std::make_shared<FakeState>();
  state->pages = {{true}, {}};
  state->planned = {{true}, {true, true, true}};
  Stepper stepper(FakeBackend(state), 1, 2);
  EXPECT_EQ(stepper.step(SelectionPageRequest::Previous).status, SelectionPageResult::Status::Pending);
  EXPECT_EQ(state->buildCalls, 1);
  EXPECT_EQ(stepper.step(SelectionPageRequest::Cancel).status, SelectionPageResult::Status::Boundary);
  EXPECT_EQ(stepper.step(SelectionPageRequest::Poll).status, SelectionPageResult::Status::Boundary);
  EXPECT_EQ(state->buildCalls, 1);
}

TEST(SelectionPageStepper, PreviousChapterWaitsForItsLastPage) {
  auto state = std::make_shared<FakeState>();
  state->pages = {{true}, {true}};
  state->planned = {{true, false, true}, {true}};
  Stepper stepper(FakeBackend(state), 1, 0);
  EXPECT_EQ(stepper.step(SelectionPageRequest::Previous).status, SelectionPageResult::Status::Pending);
  EXPECT_EQ(stepper.step(SelectionPageRequest::Poll).status, SelectionPageResult::Status::Pending);
  const auto result = stepper.step(SelectionPageRequest::Poll);
  ASSERT_EQ(result.status, SelectionPageResult::Status::Ready);
  EXPECT_EQ(result.spine, 0);
  EXPECT_EQ(result.pageIndex, 2);
  EXPECT_EQ(state->buildCalls, 2);
}

TEST(SelectionPageStepper, ReportsBoundariesAndIoOrAllocationFailures) {
  auto state = std::make_shared<FakeState>();
  state->pages = {{true}};
  state->planned = state->pages;
  Stepper first(FakeBackend(state), 0, 0);
  EXPECT_EQ(first.step(SelectionPageRequest::Previous).status, SelectionPageResult::Status::Boundary);
  Stepper last(FakeBackend(state), 0, 0);
  EXPECT_EQ(last.step(SelectionPageRequest::Next).status, SelectionPageResult::Status::Pending);
  EXPECT_EQ(last.step(SelectionPageRequest::Poll).status, SelectionPageResult::Status::Boundary);
  state->failRead = true;
  state->pages = {{true, true}};
  state->planned = state->pages;
  Stepper readFailure(FakeBackend(state), 0, 0);
  EXPECT_EQ(readFailure.step(SelectionPageRequest::Next).status, SelectionPageResult::Status::Error);
  state->failRead = false;
  state->pages = {{true}};
  state->planned = {{true, true}};
  state->failBuild = true;
  Stepper buildFailure(FakeBackend(state), 0, 0);
  EXPECT_EQ(buildFailure.step(SelectionPageRequest::Next).status, SelectionPageResult::Status::Error);
}
}  // namespace
