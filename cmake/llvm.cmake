set(LLVM_SOURCE_DIR "${CMAKE_SOURCE_DIR}/.cache/llvm")
if(EXISTS "${LLVM_SOURCE_DIR}/README.md")
  set(FETCHCONTENT_SOURCE_DIR_LLVM "${LLVM_SOURCE_DIR}")
endif()

set(LLVM_TARGETS_TO_BUILD "AArch64" CACHE STRING "" FORCE)

FetchContent_Declare(
  llvm
  URL https://github.com/llvm/llvm-project/archive/refs/tags/llvmorg-23.1.1.tar.gz
  SOURCE_SUBDIR llvm
  SOURCE_DIR "${LLVM_SOURCE_DIR}"
)
FetchContent_MakeAvailable(llvm)

include("${llvm_BINARY_DIR}/lib/cmake/llvm/LLVMConfig.cmake")

# https://llvm.org/docs/CMake.html#embedding-llvm-in-your-project
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
llvm_map_components_to_libnames(llvm_libs support core irreader)
