add_library(mioyi-lang-compiler)
target_sources(mioyi-lang-compiler
  PUBLIC FILE_SET CXX_MODULES FILES
  src/mioyi.compiler.cppm
)

target_compile_features(mioyi-lang-compiler PRIVATE cxx_std_23)
set_target_properties(mioyi-lang-compiler PROPERTIES
  CXX_EXTENSIONS OFF
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)
