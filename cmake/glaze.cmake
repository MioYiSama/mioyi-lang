set(GLAZE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/.cache/glaze")
if(EXISTS "${GLAZE_SOURCE_DIR}/CMakeLists.txt")
  set(FETCHCONTENT_SOURCE_DIR_GLAZE "${GLAZE_SOURCE_DIR}")
endif()

# https://github.com/stephenberry/glaze/releases/tag/v8.4.0
FetchContent_Declare(
  glaze
  URL https://github.com/stephenberry/glaze/archive/refs/tags/v8.4.0.tar.gz
  URL_HASH SHA256=4ee6f2ec68e8c763553d6a16e0d79cd51fac7c58cbd98783760f418ce82c9a91
  SOURCE_DIR "${GLAZE_SOURCE_DIR}"
)
FetchContent_MakeAvailable(glaze)
