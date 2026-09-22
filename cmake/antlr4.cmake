set(ANTLR4_SOURCE_DIR "${CMAKE_SOURCE_DIR}/.cache/antlr4")
if(EXISTS "${ANTLR4_SOURCE_DIR}/runtime/Cpp/CMakeLists.txt")
  set(FETCHCONTENT_SOURCE_DIR_ANTLR4 "${ANTLR4_SOURCE_DIR}")
endif()

set(ANTLR_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ANTLR_BUILD_CPP_TESTS OFF CACHE BOOL "" FORCE)

# https://github.com/antlr/antlr4/releases/tag/4.13.2
FetchContent_Declare(
  antlr4
  URL https://github.com/antlr/antlr4/archive/refs/tags/4.13.2.tar.gz
  # shasum -a 256
  URL_HASH SHA256=9f18272a9b32b622835a3365f850dd1063d60f5045fb1e12ce475ae6e18a35bb
  SOURCE_SUBDIR runtime/Cpp
  SOURCE_DIR "${ANTLR4_SOURCE_DIR}"
)
FetchContent_MakeAvailable(antlr4)
