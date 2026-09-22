add_library(mioyi-lang-code-generator)
target_sources(mioyi-lang-code-generator
  PUBLIC FILE_SET CXX_MODULES FILES
  src/mioyi.codegen.cppm
)

target_compile_features(mioyi-lang-code-generator PRIVATE cxx_std_23)
set_target_properties(mioyi-lang-code-generator PROPERTIES
  CXX_EXTENSIONS OFF
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)
