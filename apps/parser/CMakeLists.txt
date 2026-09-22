add_library(mioyi-lang-parser)
target_sources(mioyi-lang-parser
  PUBLIC FILE_SET CXX_MODULES FILES
  src/mioyi.parser.cppm
)

target_compile_features(mioyi-lang-parser PRIVATE cxx_std_23)
set_target_properties(mioyi-lang-parser PROPERTIES
  CXX_EXTENSIONS OFF
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)
