add_library(mioyi-lang-language-server)
target_sources(mioyi-lang-language-server
  PUBLIC FILE_SET CXX_MODULES FILES
  src/mioyi.ls.cppm
)

target_compile_features(mioyi-lang-language-server PRIVATE cxx_std_23)
set_target_properties(mioyi-lang-language-server PROPERTIES
  CXX_EXTENSIONS OFF
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)
