cmake_minimum_required(VERSION 3.16)
project(crosspoint_reader_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_library(crosspoint_test_common INTERFACE)
target_include_directories(crosspoint_test_common INTERFACE
  ${REPO_ROOT}
  ${REPO_ROOT}/lib
)
if(MSVC)
  target_compile_options(crosspoint_test_common INTERFACE /W4)
else()
  target_compile_options(crosspoint_test_common INTERFACE
    -Wall
    -Wextra
    -pedantic
  )
endif()

enable_testing()
include(GoogleTest)

add_subdirectory(utf8_compose)
add_subdirectory(reader_position)
add_subdirectory(gray_pipeline)
add_subdirectory(credential_integrity)
add_subdirectory(reader_regressions)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(
    NAME web_optimizer_regressions
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/web_optimizer_regressions/EpubOptimizerFallbackTest.py
  )
endif()
