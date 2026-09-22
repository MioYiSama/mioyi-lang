add_library(mioyi-lang-pm)
target_sources(mioyi-lang-pm
  PUBLIC FILE_SET CXX_MODULES FILES
  src/mioyi.pm.cppm
)

target_compile_features(mioyi-lang-pm PRIVATE cxx_std_23)
set_target_properties(mioyi-lang-pm PROPERTIES
  CXX_EXTENSIONS OFF
)
