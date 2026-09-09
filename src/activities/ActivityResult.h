#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

struct WifiResult {
  bool connected = false;
  std::string ssid;
  std::string ip;
};

struct KeyboardResult {
  std::string text;
};

struct MenuResult {
  int action = -1;
  uint8_t orientation = 0;
  uint8_t pageTurnOption = 0;
};

struct ChapterResult {
  int spineIndex = 0;
  std::string anchor;
};

struct PercentResult {
  int percent = 0;
};

struct PageResult {
  uint32_t page = 0;
};

struct BookmarkResult {
  int spineIndex = 0;
  uint32_t page = 0;
  bool hasVisibleTextOffset = false;
  uint32_t visibleTextOffset = 0;
};

struct HighlightFragment {
  std::string text;
  uint16_t spineIndex = 0;
  uint16_t pageNumber = 0;
  uint16_t startWordIndex = 0;
  uint16_t endWordIndex = 0;
  uint32_t visibleTextOffset = 0;
};

struct HighlightResult {
  std::string text;
  uint16_t startWordIndex = 0;
  uint16_t endWordIndex = 0;
  // Multi-page selections persist as independently anchored v5 fragments.
  std::vector<HighlightFragment> fragments;
};

struct SyncResult {
  int spineIndex = 0;
  int page = 0;
  uint16_t paragraphIndex = 0;
  bool hasParagraphIndex = false;
  uint16_t listItemIndex = 0;
  bool hasListItemIndex = false;
};

enum class NetworkMode;

struct NetworkModeResult {
  NetworkMode mode;
};

struct FootnoteResult {
  std::string href;
};

struct FilePathResult {
  std::string path;
};

struct FlashcardSessionResult {
  std::string deckId;
  std::string deckPath;
  std::string deckTitle;
  int reviewed = 0;
  int correct = 0;
  int failed = 0;
  int skipped = 0;
  int newSeen = 0;
  int dueRemaining = 0;
  int totalCards = 0;
  int seenCards = 0;
  int unseenCards = 0;
  int dueCards = 0;
  int masteredCards = 0;
  int successRatePercent = 0;
  int sessionCount = 0;
};

using ResultVariant = std::variant<std::monostate, WifiResult, KeyboardResult, MenuResult, ChapterResult, PercentResult,
                                   PageResult, BookmarkResult, HighlightResult, SyncResult, NetworkModeResult,
                                   FootnoteResult, FilePathResult, FlashcardSessionResult>;

struct ActivityResult {
  bool isCancelled = false;
  ResultVariant data;

  explicit ActivityResult() = default;

  template <typename ResultType>
    requires std::is_constructible_v<ResultVariant, ResultType&&>
  // cppcheck-suppress noExplicitConstructor
  ActivityResult(ResultType&& result) : data{std::forward<ResultType>(result)} {}
};

using ActivityResultHandler = std::function<void(const ActivityResult&)>;
