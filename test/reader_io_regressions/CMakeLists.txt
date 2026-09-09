cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(reader_io_regressions LANGUAGES C CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
  set(GTEST_SOURCE_DIR "${REPO_ROOT}/test/build/_deps/googletest-src" CACHE PATH "Existing GoogleTest checkout")
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  add_subdirectory("${GTEST_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/googletest")
  enable_testing()
  include(GoogleTest)
else()
  enable_language(C)
endif()

add_library(reader_io_expat STATIC
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)
target_include_directories(reader_io_expat PUBLIC ${REPO_ROOT}/lib/expat)
target_compile_definitions(reader_io_expat PUBLIC XML_STATIC XML_GE=0 XML_CONTEXT_BYTES=1024)
target_compile_definitions(reader_io_expat PRIVATE HAVE_EXPAT_CONFIG_H)

add_executable(reader_io_regressions_test
  ReaderIoTest.cpp
  PageTextVisitTest.cpp
  SelectionPageStepperTest.cpp
  ParserLinkStubs.cpp
  ${REPO_ROOT}/lib/ZipFile/ZipFile.cpp
  ${REPO_ROOT}/lib/InflateReader/InflateReader.cpp
  ${REPO_ROOT}/lib/uzlib/src/tinflate.c
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/css/CssParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParsedText.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/HyphenationCommon.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_include_directories(reader_io_regressions_test BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/fakes
  ${REPO_ROOT}/test/reader_regressions/fakes
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/MemoryBudget
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/lib/ZipFile
  ${REPO_ROOT}/lib/InflateReader
  ${REPO_ROOT}/lib/uzlib/src
  ${REPO_ROOT}/src
)
target_link_libraries(reader_io_regressions_test PRIVATE GTest::gtest_main reader_io_expat)
if(MSVC)
  target_compile_options(reader_io_regressions_test PRIVATE /W4 /utf-8 /EHsc)
endif()
gtest_discover_tests(reader_io_regressions_test)
