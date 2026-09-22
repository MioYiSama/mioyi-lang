add_library(mioyi-lang-optimizer)
target_sources(mioyi-lang-optimizer
  PUBLIC FILE_SET CXX_MODULES FILES
  src/mioyi.optimizer.cppm
)

target_compile_features(mioyi-lang-optimizer PRIVATE cxx_std_23)
set_target_properties(mioyi-lang-optimizer PROPERTIES
  CXX_EXTENSIONS OFF
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)
