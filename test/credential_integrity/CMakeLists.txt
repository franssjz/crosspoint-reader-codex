add_executable(credential_integrity_test
  CredentialIntegrityTest.cpp
)

target_link_libraries(credential_integrity_test PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(credential_integrity_test)
