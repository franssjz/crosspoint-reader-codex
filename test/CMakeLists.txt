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
add_subdirectory(credential_integrity)
add_subdirectory(reader_regressions)
add_subdirectory(persistence)
add_subdirectory(web_paths)
add_subdirectory(xtc_streaming)
add_subdirectory(reader_io_regressions)
add_subdirectory(font_cache_regressions)
add_subdirectory(file_index)
add_subdirectory(nearby_transfer)
add_subdirectory(dictionary_regressions)
add_subdirectory(keyboard_input)
add_subdirectory(epub_memory)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(
    NAME web_optimizer_regressions
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/web_optimizer_regressions/EpubOptimizerFallbackTest.py
  )
endif()

find_program(NODE_EXECUTABLE NAMES node node.exe)
if(NODE_EXECUTABLE)
  add_test(
    NAME web_optimizer_node_regressions
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/web_optimizer_regressions/EpubOptimizerTransformTest.js
  )
endif()
