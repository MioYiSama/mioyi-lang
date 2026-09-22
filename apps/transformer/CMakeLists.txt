add_library(mioyi-lang-transformer)
target_sources(mioyi-lang-transformer
  PUBLIC FILE_SET CXX_MODULES FILES
  src/mioyi.transformer.cppm
)

target_compile_features(mioyi-lang-transformer PRIVATE cxx_std_23)
set_target_properties(mioyi-lang-transformer PROPERTIES
  CXX_EXTENSIONS OFF
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)
