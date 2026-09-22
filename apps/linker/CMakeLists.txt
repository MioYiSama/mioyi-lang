add_library(mioyi-lang-linker)
target_sources(mioyi-lang-linker
  PUBLIC FILE_SET CXX_MODULES FILES
  src/mioyi.linker.cppm
)

target_compile_features(mioyi-lang-linker PRIVATE cxx_std_23)
set_target_properties(mioyi-lang-linker PROPERTIES
  CXX_EXTENSIONS OFF
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)
