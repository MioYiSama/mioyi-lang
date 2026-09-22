set(CLI11_SOURCE_DIR "${CMAKE_SOURCE_DIR}/.cache/CLI11")
if(EXISTS "${CLI11_SOURCE_DIR}/CMakeLists.txt")
  set(FETCHCONTENT_SOURCE_DIR_CLI11 "${CLI11_SOURCE_DIR}")
endif()

# https://github.com/CLIUtils/CLI11/releases/tag/v2.7.2
FetchContent_Declare(
  cli11
  URL https://github.com/CLIUtils/CLI11/releases/download/v2.7.2/CLI11-2.7.2-Source.tar.gz
  URL_HASH SHA256=0cb0ef44c4c7129ea972505ee81e6d09c71b2e7fe59ca3a13c47d8a994d682b2
  SOURCE_DIR "${CLI11_SOURCE_DIR}"
)
FetchContent_MakeAvailable(cli11)
