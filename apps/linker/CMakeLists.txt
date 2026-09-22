add_library(mioyi-lang-linker)
target_sources(mioyi-lang-linker
  PUBLIC FILE_SET CXX_MODULES FILES
  src/mioyi.linker.cppm
)

target_include_directories(mioyi-lang-linker PRIVATE ${LLVM_INCLUDE_DIRS})
target_link_libraries(mioyi-lang-linker PRIVATE ${llvm_libs})
target_compile_definitions(mioyi-lang-linker PRIVATE
  ${LLVM_DEFINITIONS_LIST}
  MIOYI_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
)
target_compile_features(mioyi-lang-linker PRIVATE cxx_std_23)
set_target_properties(mioyi-lang-linker PROPERTIES
  CXX_EXTENSIONS OFF
)
