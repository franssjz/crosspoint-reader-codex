add_executable(epub_memory_test
  EpubMemoryTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/css/CssParser.cpp
)
target_include_directories(epub_memory_test BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/fakes
  ${REPO_ROOT}/lib/Epub
)
target_link_libraries(epub_memory_test PRIVATE crosspoint_test_common GTest::gtest_main)
if(MSVC)
  target_compile_options(epub_memory_test PRIVATE /utf-8)
endif()
gtest_discover_tests(epub_memory_test)
