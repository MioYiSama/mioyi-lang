cmake_minimum_required(VERSION 4.4)
project(mioyi-lang)

include(FetchContent)
include(cmake/antlr4.cmake)
include(cmake/cli11.cmake)
include(cmake/llvm.cmake)

find_program(ANTLR4_EXECUTABLE antlr4 REQUIRED)

set(ANTLR_GRAMMAR "${CMAKE_CURRENT_SOURCE_DIR}/grammar/Expression.g4")
set(ANTLR_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(ANTLR_GENERATED_SOURCES
  "${ANTLR_GENERATED_DIR}/ExpressionLexer.cpp"
  "${ANTLR_GENERATED_DIR}/ExpressionParser.cpp"
  "${ANTLR_GENERATED_DIR}/ExpressionBaseVisitor.cpp"
  "${ANTLR_GENERATED_DIR}/ExpressionVisitor.cpp"
)
set(ANTLR_GENERATED_HEADERS
  "${ANTLR_GENERATED_DIR}/ExpressionLexer.h"
  "${ANTLR_GENERATED_DIR}/ExpressionParser.h"
  "${ANTLR_GENERATED_DIR}/ExpressionBaseVisitor.h"
  "${ANTLR_GENERATED_DIR}/ExpressionVisitor.h"
)

add_custom_command(
  OUTPUT ${ANTLR_GENERATED_SOURCES} ${ANTLR_GENERATED_HEADERS}
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${ANTLR_GENERATED_DIR}"
  COMMAND "${ANTLR4_EXECUTABLE}"
          -Dlanguage=Cpp
          -visitor
          -no-listener
          -Xexact-output-dir
          -o "${ANTLR_GENERATED_DIR}"
          "${ANTLR_GRAMMAR}"
  DEPENDS "${ANTLR_GRAMMAR}"
  COMMENT "Generating the expression parser with ANTLR4"
  VERBATIM
)

add_executable(mioyi-lang)
target_compile_features(mioyi-lang PRIVATE cxx_std_23)
set_target_properties(mioyi-lang PROPERTIES
  CXX_EXTENSIONS OFF
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)

target_sources(mioyi-lang PRIVATE
  src/main.cpp
  ${ANTLR_GENERATED_SOURCES}
  ${ANTLR_GENERATED_HEADERS}
)
target_sources(mioyi-lang PRIVATE
  FILE_SET CXX_MODULES FILES
  src/compiler.cppm
  src/options.cppm
)

target_include_directories(mioyi-lang PRIVATE
  "${ANTLR_GENERATED_DIR}"
  ${LLVM_INCLUDE_DIRS}
)
target_compile_definitions(mioyi-lang PRIVATE ${LLVM_DEFINITIONS_LIST})
target_link_libraries(mioyi-lang PRIVATE antlr4_static CLI11::CLI11 ${llvm_libs})
