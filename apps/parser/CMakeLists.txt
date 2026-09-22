add_library(mioyi-lang-parser)

grammar_generate(apps/parser/grammar/MioYiLang.g4)

target_sources(mioyi-lang-parser
  PUBLIC FILE_SET CXX_MODULES FILES
  src/mioyi.parser.cppm
  PRIVATE
  src/serialization.cpp
  ${GRAMMAR_SOURCES}
)

target_include_directories(mioyi-lang-parser PRIVATE ${GRAMMAR_INCLUDE_DIR})
target_link_libraries(mioyi-lang-parser
  PUBLIC mioyi-lang-ast
  PRIVATE antlr4_static glaze::glaze
)
target_compile_features(mioyi-lang-parser PRIVATE cxx_std_23)
set_target_properties(mioyi-lang-parser PROPERTIES
  CXX_EXTENSIONS OFF
)
