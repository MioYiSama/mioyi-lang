cmake_minimum_required(VERSION 4.4)
project(mioyi-lang LANGUAGES C CXX)

include(FetchContent)
include(cmake/mimalloc.cmake)
include(cmake/antlr4.cmake)
include(cmake/cli11.cmake)
include(cmake/glaze.cmake)
include(cmake/llvm.cmake)
include(cmake/grammar.cmake)

add_executable(mioyi-lang)

# 源码
# grammar_generate(src/grammar/Expression.g4)
target_sources(mioyi-lang PRIVATE
  src/main.cpp
  # ${GRAMMAR_SOURCES}
)
# target_sources(mioyi-lang PRIVATE
#   FILE_SET CXX_MODULES FILES
#   src/backend.cppm
#   src/ast.cppm
#   src/compiler.cppm
#   src/frontend.cppm
#   src/linker.cppm
#   src/options.cppm
#   src/parser.cppm
# )

# 依赖
# target_include_directories(mioyi-lang PRIVATE
#   ${GRAMMAR_INCLUDE_DIR}
#   ${LLVM_INCLUDE_DIRS}
# )
# target_link_libraries(mioyi-lang PRIVATE
#   mimalloc-obj
#   antlr4_static
#   CLI11::CLI11
#   glaze::glaze
#   ${llvm_libs}
# )
# target_compile_definitions(mioyi-lang PRIVATE
#   ${LLVM_DEFINITIONS_LIST}
#   MIOYI_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
# )


add_subdirectory(apps/builder)
add_subdirectory(apps/code-generator)
add_subdirectory(apps/compiler)
add_subdirectory(apps/formatter)
add_subdirectory(apps/language-server)
add_subdirectory(apps/linker)
add_subdirectory(apps/linter)
add_subdirectory(apps/manager)
add_subdirectory(apps/optimizer)
add_subdirectory(apps/package-manager)
add_subdirectory(apps/parser)
add_subdirectory(apps/transformer)
target_link_libraries(mioyi-lang PRIVATE
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
  mioyi-lang-pm
  mioyi-lang-parser
  mioyi-lang-transformer
)

target_compile_features(mioyi-lang PRIVATE cxx_std_23)
set_target_properties(mioyi-lang PROPERTIES
  CXX_EXTENSIONS OFF
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)
