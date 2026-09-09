cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(font_cache_regressions LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
  set(GTEST_SOURCE_DIR "${REPO_ROOT}/test/build/_deps/googletest-src" CACHE PATH "Existing GoogleTest checkout")
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  add_subdirectory("${GTEST_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/googletest")
  enable_testing()
  include(GoogleTest)
endif()

add_executable(font_cache_regressions_test
  FontCacheTest.cpp
  ${REPO_ROOT}/lib/GfxRenderer/FontCacheManager.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_include_directories(font_cache_regressions_test BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/fakes
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/test/reader_regressions/fakes
)
target_link_libraries(font_cache_regressions_test PRIVATE GTest::gtest_main)
if(MSVC)
  target_compile_options(font_cache_regressions_test PRIVATE /W4 /utf-8 /EHsc
    "/FI${CMAKE_CURRENT_SOURCE_DIR}/fakes/CompilerCompat.h")
endif()
gtest_discover_tests(font_cache_regressions_test)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME builtin_font_encoding_regressions
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/FontEncodingTest.py)
endif()
