set(ANTLR4_SOURCE_DIR "${CMAKE_SOURCE_DIR}/.cache/antlr4")
if(EXISTS "${ANTLR4_SOURCE_DIR}/README.md")
  set(FETCHCONTENT_SOURCE_DIR_ANTLR4 "${ANTLR4_SOURCE_DIR}")
endif()

set(ANTLR_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ANTLR_BUILD_CPP_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  antlr4
  URL https://github.com/antlr/antlr4/archive/refs/tags/4.13.2.tar.gz
  SOURCE_SUBDIR runtime/Cpp
  SOURCE_DIR "${ANTLR4_SOURCE_DIR}"
)
FetchContent_MakeAvailable(antlr4)
