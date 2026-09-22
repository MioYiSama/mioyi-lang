set(LLVM_SOURCE_DIR "${CMAKE_SOURCE_DIR}/.cache/llvm")
if(EXISTS "${LLVM_SOURCE_DIR}/llvm/CMakeLists.txt")
  set(FETCHCONTENT_SOURCE_DIR_LLVM "${LLVM_SOURCE_DIR}")
endif()

# mioyi-lang embeds LLVM and one LLD driver.  Keep LLVM's tools directory in
# the CMake graph because that is where LLVM adds external projects such as
# LLD, but exclude all tool executables and other development-only targets
# from the default build.
set(LLVM_ENABLE_PROJECTS "lld" CACHE STRING "" FORCE)
set(LLVM_ENABLE_RUNTIMES "" CACHE STRING "" FORCE)

set(LLVM_INCLUDE_TOOLS ON CACHE BOOL "" FORCE)
set(LLVM_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(LLD_BUILD_TOOLS OFF CACHE BOOL "" FORCE)

set(LLVM_INCLUDE_UTILS OFF CACHE BOOL "" FORCE)
set(LLVM_BUILD_UTILS OFF CACHE BOOL "" FORCE)
set(LLVM_INCLUDE_RUNTIMES OFF CACHE BOOL "" FORCE)
set(LLVM_BUILD_RUNTIMES OFF CACHE BOOL "" FORCE)
set(LLVM_BUILD_RUNTIME OFF CACHE BOOL "" FORCE)
set(LLVM_INCLUDE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLVM_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLVM_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
set(LLVM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLVM_INCLUDE_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(LLVM_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(LLVM_INCLUDE_DOCS OFF CACHE BOOL "" FORCE)
set(LLVM_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(LLVM_ENABLE_BINDINGS OFF CACHE BOOL "" FORCE)

# These optional facilities are not used by IR generation, optimization,
# native object emission, or the embedded platform linker.
set(LLVM_ENABLE_LIBEDIT OFF CACHE BOOL "" FORCE)
set(LLVM_ENABLE_LIBPFM OFF CACHE BOOL "" FORCE)
set(LLVM_ENABLE_LIBXML2 OFF CACHE STRING "" FORCE)
set(LLVM_ENABLE_ZLIB OFF CACHE STRING "" FORCE)
set(LLVM_ENABLE_ZSTD OFF CACHE STRING "" FORCE)
set(LLVM_ENABLE_ONDISK_CAS OFF CACHE BOOL "" FORCE)
set(LLVM_ENABLE_PLUGINS OFF CACHE BOOL "" FORCE)
set(LLVM_ENABLE_TELEMETRY OFF CACHE BOOL "" FORCE)

if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
  set(LLVM_TARGETS_TO_BUILD "AArch64" CACHE STRING "" FORCE)
elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64)$")
  set(LLVM_TARGETS_TO_BUILD "X86" CACHE STRING "" FORCE)
else()
  message(FATAL_ERROR "Unsupported CPU Arch: ${CMAKE_HOST_SYSTEM_PROCESSOR}. ")
endif()

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
  irreader
  passes
  nativecodegen
)

if(WIN32)
  set(MIOYI_LLD_LIBRARY lldCOFF)
elseif(APPLE)
  set(MIOYI_LLD_LIBRARY lldMachO)
elseif(LINUX)
  set(MIOYI_LLD_LIBRARY lldELF)
else()
  message(FATAL_ERROR "Unsupported OS")
endif()

target_include_directories(${MIOYI_LLD_LIBRARY} INTERFACE
  "$<BUILD_INTERFACE:${LLVM_SOURCE_DIR}/lld/include>"
  "$<BUILD_INTERFACE:${llvm_BINARY_DIR}/tools/lld/include>"
)
list(APPEND llvm_libs "${MIOYI_LLD_LIBRARY}")
