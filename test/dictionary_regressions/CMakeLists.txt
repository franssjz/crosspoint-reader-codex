cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(dictionary_regressions LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
  set(GTEST_SOURCE_DIR "${REPO_ROOT}/test/build/_deps/googletest-src" CACHE PATH "Existing GoogleTest checkout")
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  add_subdirectory("${GTEST_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/googletest")
  enable_testing()
  include(GoogleTest)
endif()

add_executable(dictionary_regressions_test
  DictionaryStoreTest.cpp
  ${REPO_ROOT}/src/DictionaryStore.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_include_directories(dictionary_regressions_test BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/fakes
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/.pio/libdeps/default/ArduinoJson/src
  ${REPO_ROOT}/test/reader_regressions/fakes
)
target_link_libraries(dictionary_regressions_test PRIVATE GTest::gtest_main)
if(MSVC)
  target_compile_options(dictionary_regressions_test PRIVATE /W4 /utf-8 /EHsc)
endif()
gtest_discover_tests(dictionary_regressions_test)
