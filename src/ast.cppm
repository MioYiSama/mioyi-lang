module;

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

#include <glaze/json.hpp>
#include <glaze/yaml.hpp>

export module mioyi.ast;

import mioyi.options;
import mioyi.parser;

namespace {

using Json = glz::generic_i64;

Json object() { return Json::object_t{}; }
Json array() { return Json::array_t{}; }

Json convert(mioyi::SourceLocation value) {
  Json result = object();
  result["line"] = value.line;
  result["column"] = value.column;
  return result;
}

Json convert(const mioyi::Expression &value) {
  Json result = object();
  result["location"] = convert(value.location);
  using Kind = mioyi::Expression::Kind;
  switch (value.kind) {
  case Kind::Integer:
    result["kind"] = "integer";
    result["value"] = value.integer;
    break;
  case Kind::LValue:
    result["kind"] = "lvalue";
    result["name"] = value.text;
    break;
  case Kind::Call:
    result["kind"] = "call";
    result["name"] = value.text;
    break;
  case Kind::Unary:
    result["kind"] = "unary";
    result["operator"] = value.text;
    break;
  case Kind::Binary:
    result["kind"] = "binary";
    result["operator"] = value.text;
    break;
  }
  if (!value.operands.empty()) {
    result["operands"] = array();
    for (const auto &operand : value.operands)
      result["operands"].get_array().push_back(convert(*operand));
  }
  return result;
}

Json convert(const mioyi::Initializer &value) {
  Json result = object();
  result["location"] = convert(value.location);
  if (value.expression)
    result["expression"] = convert(*value.expression);
  if (!value.elements.empty()) {
    result["elements"] = array();
    for (const auto &element : value.elements)
      result["elements"].get_array().push_back(convert(*element));
  }
  return result;
}

Json convert(const mioyi::Declaration &value) {
  Json result = object();
  result["constant"] = value.constant;
  result["definitions"] = array();
  for (const auto &definition : value.definitions) {
    Json item = object();
    item["location"] = convert(definition.location);
    item["name"] = definition.name;
    if (!definition.dimensions.empty()) {
      item["dimensions"] = array();
      for (const auto &dimension : definition.dimensions)
        item["dimensions"].get_array().push_back(convert(*dimension));
    }
    if (definition.initializer)
      item["initializer"] = convert(*definition.initializer);
    result["definitions"].get_array().push_back(std::move(item));
  }
  return result;
}

Json convert(const mioyi::Statement &value) {
  Json result = object();
  result["location"] = convert(value.location);
  using Kind = mioyi::Statement::Kind;
  switch (value.kind) {
  case Kind::Declaration:
    result["kind"] = "declaration";
    break;
  case Kind::Assignment:
    result["kind"] = "assignment";
    break;
  case Kind::Expression:
    result["kind"] = "expression";
    break;
  case Kind::Block:
    result["kind"] = "block";
    break;
  case Kind::If:
    result["kind"] = "if";
    break;
  case Kind::While:
    result["kind"] = "while";
    break;
  case Kind::Break:
    result["kind"] = "break";
    break;
  case Kind::Continue:
    result["kind"] = "continue";
    break;
  case Kind::Return:
    result["kind"] = "return";
    break;
  }
  if (value.declaration)
    result["declaration"] = convert(*value.declaration);
  if (value.target)
    result["target"] = convert(*value.target);
  if (value.expression)
    result["expression"] = convert(*value.expression);
  if (!value.block.empty()) {
    result["statements"] = array();
    for (const auto &child : value.block)
      result["statements"].get_array().push_back(convert(*child));
  }
  if (value.thenBranch)
    result["then"] = convert(*value.thenBranch);
  if (value.elseBranch)
    result["else"] = convert(*value.elseBranch);
  return result;
}

Json convert(const mioyi::Function &value) {
  Json result = object();
  result["location"] = convert(value.location);
  result["name"] = value.name;
  result["returnsValue"] = value.returnsValue;
  result["parameters"] = array();
  for (const auto &parameter : value.parameters) {
    Json item = object();
    item["location"] = convert(parameter.location);
    item["name"] = parameter.name;
    item["array"] = parameter.array;
    if (!parameter.dimensions.empty()) {
      item["dimensions"] = array();
      for (const auto &dimension : parameter.dimensions)
        item["dimensions"].get_array().push_back(convert(*dimension));
    }
    result["parameters"].get_array().push_back(std::move(item));
  }
  result["body"] = convert(*value.body);
  return result;
}

Json convert(const mioyi::Ast &value) {
  Json result = object();
  result["items"] = array();
  for (const auto &item : value.items) {
    Json output = object();
    if (item.declaration) {
      output["kind"] = "declaration";
      output["declaration"] = convert(*item.declaration);
    } else {
      output["kind"] = "function";
      output["function"] = convert(*item.function);
    }
    result["items"].get_array().push_back(std::move(output));
  }
  return result;
}

bool serialize(const Json &ast, const std::string &format,
               std::string &output) {
  glz::error_ctx error;
  if (format == "yaml") {
    error = glz::write_yaml(ast, output);
  } else {
    error = glz::write<glz::opts{.prettify = true}>(ast, output);
  }
  if (!error)
    return true;
  std::cerr << "error: failed to serialize AST: "
            << glz::format_error(error, output) << '\n';
  return false;
}

} // namespace

export int dumpAst(const CompilerOptions &options) {
  std::ifstream inputFile;
  std::istream *source = &std::cin;
  if (!options.input.empty()) {
    inputFile.open(options.input);
    if (!inputFile) {
      std::cerr << "error: cannot open input file '" << options.input << "'\n";
      return 1;
    }
    source = &inputFile;
  }
  std::ostringstream sourceBuffer;
  sourceBuffer << source->rdbuf();
  auto ast = parseSource(sourceBuffer.str());
  if (!ast)
    return 1;

  std::string output;
  if (!serialize(convert(*ast), options.astFormat, output))
    return 1;
  output.push_back('\n');
  if (options.output == "-") {
    std::cout << output;
    return std::cout ? 0 : 1;
  }
  std::ofstream outputFile(options.output);
  if (!outputFile) {
    std::cerr << "error: cannot open output file '" << options.output << "'\n";
    return 1;
  }
  outputFile << output;
  if (!outputFile) {
    std::cerr << "error: cannot write output file '" << options.output << "'\n";
    return 1;
  }
  return 0;
}
