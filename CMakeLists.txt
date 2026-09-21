cmake_minimum_required(VERSION 4.4)
project(mioyi-lang)

set(ANTLR4_CACHE_DIR "${CMAKE_SOURCE_DIR}/.cache/antlr4")
if(EXISTS "${ANTLR4_CACHE_DIR}/.git")
  set(FETCHCONTENT_SOURCE_DIR_ANTLR4 "${ANTLR4_CACHE_DIR}")
endif()

include(FetchContent)
# FetchContent_Declare(
#   llvm
#   GIT_REPOSITORY https://github.com/llvm/llvm-project.git
#   GIT_TAG        llvmorg-23.1.1
#   GIT_SHALLOW    TRUE
#   GIT_SUBMODULES ""
# )
FetchContent_Declare(
  antlr4
  GIT_REPOSITORY https://github.com/antlr/antlr4
  GIT_TAG        4.13.2
  GIT_SHALLOW    TRUE
  GIT_SUBMODULES ""
  SOURCE_SUBDIR  runtime/Cpp
  SOURCE_DIR     "${ANTLR4_CACHE_DIR}"
)
# FetchContent_MakeAvailable(llvm antlr4)
FetchContent_MakeAvailable(antlr4)

add_executable(mioyi-lang src/main.cpp)
target_compile_features(mioyi-lang PRIVATE cxx_std_23)
set_target_properties(mioyi-lang PROPERTIES
  CXX_EXTENSIONS OFF
)
target_compile_definitions(mioyi-lang PRIVATE ANTLR4CPP_STATIC)
target_link_libraries(mioyi-lang PRIVATE antlr4_static)
