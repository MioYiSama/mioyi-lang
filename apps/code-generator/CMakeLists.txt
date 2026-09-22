add_library(mioyi-lang-code-generator)
target_sources(mioyi-lang-code-generator
  PUBLIC FILE_SET CXX_MODULES FILES
  src/mioyi.codegen.cppm
)

target_include_directories(mioyi-lang-code-generator PRIVATE
  ${LLVM_INCLUDE_DIRS}
)
target_link_libraries(mioyi-lang-code-generator PRIVATE
  ${llvm_libs}
)
target_compile_definitions(mioyi-lang-code-generator PRIVATE
  ${LLVM_DEFINITIONS_LIST}
)
target_compile_features(mioyi-lang-code-generator PRIVATE cxx_std_23)
set_target_properties(mioyi-lang-code-generator PROPERTIES
  CXX_EXTENSIONS OFF
)
