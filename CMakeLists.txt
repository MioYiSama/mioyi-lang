cmake_minimum_required(VERSION 4.4)
project(mioyi-lang LANGUAGES CXX)

include(FetchContent)
include(cmake/antlr4.cmake)
include(cmake/cli11.cmake)
include(cmake/glaze.cmake)
include(cmake/llvm.cmake)
include(cmake/grammar.cmake)
grammar_generate(src/grammar/Expression.g4)

add_executable(mioyi-lang)
# 配置
target_compile_features(mioyi-lang PRIVATE cxx_std_23)
set_target_properties(mioyi-lang PROPERTIES
  CXX_EXTENSIONS OFF
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)

# 源码
target_sources(mioyi-lang PRIVATE
  src/main.cpp
  ${GRAMMAR_SOURCES}
)
target_sources(mioyi-lang PRIVATE
  FILE_SET CXX_MODULES FILES
  src/backend.cppm
  src/ast.cppm
  src/compiler.cppm
  src/frontend.cppm
  src/linker.cppm
  src/options.cppm
  src/parser.cppm
)

# 依赖
target_include_directories(mioyi-lang PRIVATE "${GRAMMAR_INCLUDE_DIR}" ${LLVM_INCLUDE_DIRS})
target_compile_definitions(mioyi-lang PRIVATE ${LLVM_DEFINITIONS_LIST})
target_link_libraries(mioyi-lang PRIVATE antlr4_static CLI11::CLI11 glaze::glaze ${llvm_libs})
