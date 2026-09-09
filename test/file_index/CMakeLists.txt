add_executable(file_index_test
  FileIndexTest.cpp
  ${REPO_ROOT}/lib/FileIndex/FileIndex.cpp
  ${REPO_ROOT}/lib/FileIndex/FileIndexNaturalSort.cpp
)
target_include_directories(file_index_test BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/fakes
  ${REPO_ROOT}/lib/FileIndex
)
target_link_libraries(file_index_test PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(file_index_test)
