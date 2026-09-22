find_program(UVX_EXECUTABLE uvx REQUIRED)

function(grammar_generate grammar)
  # 转换为绝对路径
  cmake_path(
    ABSOLUTE_PATH grammar
    BASE_DIRECTORY "${CMAKE_SOURCE_DIR}"
    NORMALIZE
  )
  # 添加配置期的依赖
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${grammar}")

  # 获取文件名
  get_filename_component(name "${grammar}" NAME_WE)

  set(output "${CMAKE_BINARY_DIR}/grammar")
  set(sources
    "${output}/${name}Lexer.cpp"
    "${output}/${name}Parser.cpp"
    "${output}/${name}BaseVisitor.cpp"
    "${output}/${name}Visitor.cpp"
    "${output}/${name}BaseListener.cpp"
    "${output}/${name}Listener.cpp"
  )
  set(headers
    "${output}/${name}Lexer.h"
    "${output}/${name}Parser.h"
    "${output}/${name}BaseVisitor.h"
    "${output}/${name}Visitor.h"
    "${output}/${name}BaseListener.h"
    "${output}/${name}Listener.h"
  )

  file(MAKE_DIRECTORY "${output}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "ANTLR4_TOOLS_ANTLR_VERSION=4.13.2"
            "${UVX_EXECUTABLE}" --from=antlr4-tools antlr4
            -Dlanguage=Cpp
            -visitor
            -listener
            -Xexact-output-dir
            -o "${output}"
            "${grammar}"
    COMMAND_ERROR_IS_FATAL ANY
  )

  # 输出
  set(GRAMMAR_SOURCES ${sources} PARENT_SCOPE)
  set(GRAMMAR_INCLUDE_DIR ${output} PARENT_SCOPE)
endfunction()
