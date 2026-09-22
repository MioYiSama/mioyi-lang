add_library(mioyi-lang-formatter)
target_sources(mioyi-lang-formatter
  PUBLIC FILE_SET CXX_MODULES FILES
  src/mioyi.formatter.cppm
)

target_compile_features(mioyi-lang-formatter PRIVATE cxx_std_23)
set_target_properties(mioyi-lang-formatter PROPERTIES
  CXX_EXTENSIONS OFF
)
