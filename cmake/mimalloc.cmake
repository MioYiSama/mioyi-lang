set(MIMALLOC_SOURCE_DIR "${CMAKE_SOURCE_DIR}/.cache/mimalloc")
if(EXISTS "${MIMALLOC_SOURCE_DIR}/CMakeLists.txt")
  set(FETCHCONTENT_SOURCE_DIR_MIMALLOC "${MIMALLOC_SOURCE_DIR}")
endif()

# Build only the single object file recommended by mimalloc for reliable
# process-wide replacement of malloc/free and C++ new/delete.
set(MI_OVERRIDE ON CACHE BOOL "" FORCE)
set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MI_BUILD_STATIC OFF CACHE BOOL "" FORCE)
set(MI_BUILD_OBJECT ON CACHE BOOL "" FORCE)
set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# https://github.com/microsoft/mimalloc/releases/tag/v3.5.3
FetchContent_Declare(
  mimalloc
  URL https://github.com/microsoft/mimalloc/releases/download/v3.5.3/mimalloc-v3.5.3-source.tar.gz
  URL_HASH SHA256=43857a9e4f26412e970cbe49635d0465c2de6e77a697ce3d4b926a473d1045ca
  SOURCE_DIR "${MIMALLOC_SOURCE_DIR}"
  EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(mimalloc)
