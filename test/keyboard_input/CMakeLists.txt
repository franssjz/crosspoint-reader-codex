add_executable(keyboard_input_test KeyboardInputTest.cpp
  ${REPO_ROOT}/src/activities/util/KeyboardLayoutSet.cpp)
target_include_directories(keyboard_input_test PRIVATE ${REPO_ROOT}/src/activities/util ${REPO_ROOT}/src)
target_link_libraries(keyboard_input_test PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(keyboard_input_test)
if(MSVC)
  target_compile_options(keyboard_input_test PRIVATE /utf-8)
endif()
