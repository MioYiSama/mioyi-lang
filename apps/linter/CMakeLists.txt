add_library(mioyi-lang-linter)
target_sources(mioyi-lang-linter
  PUBLIC FILE_SET CXX_MODULES FILES
  src/mioyi.linter.cppm
)

target_compile_features(mioyi-lang-linter PRIVATE cxx_std_23)
set_target_properties(mioyi-lang-linter PROPERTIES
  CXX_EXTENSIONS OFF
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)
