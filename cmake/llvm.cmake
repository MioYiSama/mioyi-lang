set(LLVM_SOURCE_DIR "${CMAKE_SOURCE_DIR}/.cache/llvm")
if(EXISTS "${LLVM_SOURCE_DIR}/llvm/CMakeLists.txt")
  set(FETCHCONTENT_SOURCE_DIR_LLVM "${LLVM_SOURCE_DIR}")
endif()

string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" MIOYI_HOST_PROCESSOR)
if(MIOYI_HOST_PROCESSOR MATCHES "^(aarch64|arm64)$")
  set(MIOYI_LLVM_TARGET AArch64)
elseif(MIOYI_HOST_PROCESSOR MATCHES "^(x86_64|amd64)$")
  set(MIOYI_LLVM_TARGET X86)
else()
  message(FATAL_ERROR
    "Unsupported host CPU architecture: ${CMAKE_HOST_SYSTEM_PROCESSOR}. "
    "Only aarch64 and x86_64 are supported."
  )
endif()

set(LLVM_TARGETS_TO_BUILD "${MIOYI_LLVM_TARGET}" CACHE STRING "" FORCE)
set(LLVM_INCLUDE_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(LLVM_INCLUDE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLVM_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
set(LLVM_BUILD_TOOLS OFF CACHE BOOL "" FORCE)

# https://github.com/llvm/llvm-project/releases/tag/llvmorg-23.1.1
FetchContent_Declare(
  llvm
  URL https://github.com/llvm/llvm-project/releases/download/llvmorg-23.1.1/llvm-project-23.1.1.src.tar.xz
  URL_HASH SHA256=ebe9be46fe8756d58c5b198ffad0fa2a766257add81a4dc52179bfacc7888ee6
  SOURCE_SUBDIR llvm
  SOURCE_DIR "${LLVM_SOURCE_DIR}"
)
FetchContent_MakeAvailable(llvm)

include("${llvm_BINARY_DIR}/lib/cmake/llvm/LLVMConfig.cmake")

# https://llvm.org/docs/CMake.html#embedding-llvm-in-your-project
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
llvm_map_components_to_libnames(llvm_libs
  support
  core
  passes
  nativecodegen
)
