add_library(mioyi-lang-compiler)
target_sources(mioyi-lang-compiler
  PUBLIC FILE_SET CXX_MODULES FILES
  src/mioyi.compiler.cppm
)

target_include_directories(mioyi-lang-compiler PRIVATE ${LLVM_INCLUDE_DIRS})
target_link_libraries(mioyi-lang-compiler PRIVATE
  mioyi-lang-code-generator
  mioyi-lang-linker
  mioyi-lang-optimizer
  mioyi-lang-parser
  mioyi-lang-transformer
  ${llvm_libs}
)
target_compile_definitions(mioyi-lang-compiler PRIVATE
  ${LLVM_DEFINITIONS_LIST}
)
target_compile_features(mioyi-lang-compiler PRIVATE cxx_std_23)
set_target_properties(mioyi-lang-compiler PROPERTIES
  CXX_EXTENSIONS OFF
)
