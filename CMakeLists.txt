cmake_minimum_required(VERSION 4.4)
project(mioyi-lang LANGUAGES C CXX)

include(FetchContent)
include(cmake/mimalloc.cmake)
include(cmake/antlr4.cmake)
include(cmake/cli11.cmake)
include(cmake/glaze.cmake)
include(cmake/llvm.cmake)
include(cmake/grammar.cmake)

add_subdirectory(packages/ast)
add_subdirectory(apps/parser)
add_subdirectory(apps/transformer)
add_subdirectory(apps/optimizer)
add_subdirectory(apps/code-generator)
add_subdirectory(apps/linker)
add_subdirectory(apps/compiler)
add_subdirectory(apps/builder)
add_subdirectory(apps/formatter)
add_subdirectory(apps/language-server)
add_subdirectory(apps/linter)
add_subdirectory(apps/manager)
add_subdirectory(apps/package-manager)

add_executable(mioyi-lang src/main.cpp)
target_link_libraries(mioyi-lang PRIVATE
  CLI11::CLI11
  mimalloc-obj
  mioyi-lang-builder
  mioyi-lang-code-generator
  mioyi-lang-compiler
  mioyi-lang-formatter
  mioyi-lang-language-server
  mioyi-lang-linker
  mioyi-lang-linter
  mioyi-lang-manager
  mioyi-lang-optimizer
  mioyi-lang-parser
  mioyi-lang-pm
  mioyi-lang-transformer
)

target_compile_features(mioyi-lang PRIVATE cxx_std_23)
set_target_properties(mioyi-lang PROPERTIES
  CXX_EXTENSIONS OFF
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)
