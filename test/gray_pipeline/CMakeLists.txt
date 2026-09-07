add_executable(GrayPipelineTest
  GrayPipelineTest.cpp
)

target_include_directories(GrayPipelineTest PRIVATE
  ${REPO_ROOT}/lib
)

target_link_libraries(GrayPipelineTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GrayPipelineTest)
