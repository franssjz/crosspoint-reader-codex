add_executable(web_paths_test
  WebPathTest.cpp
  WebUploadRecoveryTest.cpp
  ${REPO_ROOT}/src/util/WebPath.cpp
)
target_link_libraries(web_paths_test PRIVATE crosspoint_test_common GTest::gtest_main)
if(MSVC)
  target_compile_options(web_paths_test PRIVATE /utf-8)
endif()
gtest_discover_tests(web_paths_test)
