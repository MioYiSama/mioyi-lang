add_library(mioyi-lang-transformer)
target_sources(mioyi-lang-transformer
  PUBLIC FILE_SET CXX_MODULES FILES
  src/mioyi.transformer.cppm
)

target_include_directories(mioyi-lang-transformer PRIVATE ${LLVM_INCLUDE_DIRS})
target_link_libraries(mioyi-lang-transformer
  PUBLIC mioyi-lang-ast
  PRIVATE mioyi-lang-parser ${llvm_libs}
)
target_compile_definitions(mioyi-lang-transformer PRIVATE
  ${LLVM_DEFINITIONS_LIST}
)
target_compile_features(mioyi-lang-transformer PRIVATE cxx_std_23)
set_target_properties(mioyi-lang-transformer PROPERTIES
  CXX_EXTENSIONS OFF
)
