add_executable(xtc_streaming_test
  XtcStreamingTest.cpp
  ${REPO_ROOT}/lib/Xtc/Xtc.cpp
  ${REPO_ROOT}/lib/Xtc/Xtc/XtcParser.cpp
)
target_include_directories(xtc_streaming_test BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/fakes
  ${REPO_ROOT}/lib/Xtc
  ${REPO_ROOT}/lib/Memory
)
target_link_libraries(xtc_streaming_test PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(xtc_streaming_test)
