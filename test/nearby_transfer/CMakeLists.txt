add_executable(nearby_transfer_test
  NearbyTransferTest.cpp
  ${REPO_ROOT}/src/network/NearbyTransferProtocol.cpp
  ${REPO_ROOT}/src/util/WebPath.cpp
  ${REPO_ROOT}/freeink-sdk/libs/network/NearbyTransfer/src/NearbyTransfer.cpp
)
target_include_directories(nearby_transfer_test PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/freeink-sdk/libs/network/NearbyTransfer/include
)
target_link_libraries(nearby_transfer_test PRIVATE crosspoint_test_common GTest::gtest_main)
if(MSVC)
  target_compile_options(nearby_transfer_test PRIVATE /utf-8)
endif()
gtest_discover_tests(nearby_transfer_test)
