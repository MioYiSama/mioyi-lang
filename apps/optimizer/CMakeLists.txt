add_library(mioyi-lang-optimizer)
target_sources(mioyi-lang-optimizer
  PUBLIC FILE_SET CXX_MODULES FILES
  src/mioyi.optimizer.cppm
)

target_include_directories(mioyi-lang-optimizer PRIVATE ${LLVM_INCLUDE_DIRS})
target_link_libraries(mioyi-lang-optimizer PRIVATE ${llvm_libs})
target_compile_definitions(mioyi-lang-optimizer PRIVATE
  ${LLVM_DEFINITIONS_LIST}
)
target_compile_features(mioyi-lang-optimizer PRIVATE cxx_std_23)
set_target_properties(mioyi-lang-optimizer PROPERTIES
  CXX_EXTENSIONS OFF
)
