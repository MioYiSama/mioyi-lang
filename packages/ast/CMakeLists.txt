add_library(mioyi-lang-ast)
target_sources(mioyi-lang-ast
  PUBLIC FILE_SET CXX_MODULES FILES
  src/mioyi.ast.cppm
)

target_compile_features(mioyi-lang-ast PRIVATE cxx_std_23)
set_target_properties(mioyi-lang-ast PROPERTIES
  CXX_EXTENSIONS OFF
)
