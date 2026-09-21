cmake_minimum_required(VERSION 4.4)
project(mioyi-lang)

include(FetchContent)
include(cmake/antlr4.cmake)
include(cmake/llvm.cmake)

add_executable(mioyi-lang)
target_sources(mioyi-lang PRIVATE
  src/main.cpp
)

target_include_directories(mioyi-lang PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(mioyi-lang PRIVATE ${LLVM_DEFINITIONS_LIST})
target_link_libraries(mioyi-lang PRIVATE antlr4_static ${llvm_libs})

target_compile_features(mioyi-lang PRIVATE cxx_std_23)
set_target_properties(mioyi-lang PROPERTIES
  CXX_EXTENSIONS OFF
)
