add_executable(reader_regressions_test
  BookmarkStoreTest.cpp
  DictionaryFontSelectionTest.cpp
  FsHelpersCaseTest.cpp
  HighlightTextMatcherTest.cpp
  ImageSafetyTest.cpp
  ReadingSessionLogTest.cpp
  RubyCjkLayoutTest.cpp
  TextBlockSerializationTest.cpp
  TokenBoundaryTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/HyphenationCommon.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  fakes/BookIdentityFake.cpp
)

target_include_directories(reader_regressions_test BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/fakes
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/MiniBidi
)

target_link_libraries(reader_regressions_test PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(reader_regressions_test)
