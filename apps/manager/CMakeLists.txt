add_library(mioyi-lang-manager)
target_sources(mioyi-lang-manager
  PUBLIC FILE_SET CXX_MODULES FILES
  src/mioyi.manager.cppm
)

target_compile_features(mioyi-lang-manager PRIVATE cxx_std_23)
set_target_properties(mioyi-lang-manager PROPERTIES
  CXX_EXTENSIONS OFF
)
