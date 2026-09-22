add_library(mioyi-lang-builder)
target_sources(mioyi-lang-builder
  PUBLIC FILE_SET CXX_MODULES FILES
  src/mioyi.builder.cppm
)

target_compile_features(mioyi-lang-builder PRIVATE cxx_std_23)
set_target_properties(mioyi-lang-builder PROPERTIES
  CXX_EXTENSIONS OFF
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)
