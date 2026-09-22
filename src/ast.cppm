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

Json convert(const mioyi::TypeRef &value) {
  Json result = object();
  result["location"] = convert(value.location);
  result["name"] = value.name;
  if (value.element)
    result["element"] = convert(*value.element);
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
    result["type"] = value.text;
    break;
  case Kind::Floating:
    result["kind"] = "floating";
    result["value"] = value.floating;
    result["type"] = value.text;
    break;
  case Kind::String:
    result["kind"] = "string";
    result["value"] = value.text;
    break;
  case Kind::Character:
    result["kind"] = "character";
    result["value"] = value.integer;
    break;
  case Kind::Boolean:
    result["kind"] = "boolean";
    result["value"] = value.boolean;
    break;
  case Kind::Void:
    result["kind"] = "void";
    break;
  case Kind::LValue:
    result["kind"] = "lvalue";
    result["name"] = value.text;
    break;
  case Kind::Array:
    result["kind"] = "array";
    break;
  case Kind::Call:
    result["kind"] = "call";
    break;
  case Kind::Index:
    result["kind"] = "index";
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

const char *bindingKind(mioyi::BindingKind kind) {
  switch (kind) {
  case mioyi::BindingKind::Variable:
    return "var";
  case mioyi::BindingKind::Value:
    return "val";
  case mioyi::BindingKind::Definition:
    return "def";
  }
  return "unknown";
}

Json convert(const mioyi::Declaration &value) {
  Json result = object();
  result["bindingKind"] = bindingKind(value.kind);
  result["exported"] = value.exported;
  result["bindings"] = array();
  for (const auto &binding : value.bindings) {
    Json item = object();
    item["location"] = convert(binding.location);
    item["name"] = binding.name;
    if (binding.type)
      item["type"] = convert(*binding.type);
    item["initializer"] = convert(*binding.initializer);
    result["bindings"].get_array().push_back(std::move(item));
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
    result["operator"] = value.text;
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
  case Kind::For:
    result["kind"] = "for";
    break;
  case Kind::ForEach:
    result["kind"] = "forEach";
    result["binding"] = value.text;
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
  result["exported"] = value.exported;
  result["genericParameters"] = array();
  for (const auto &parameter : value.genericParameters)
    result["genericParameters"].get_array().push_back(parameter);
  result["parameters"] = array();
  for (const auto &parameter : value.parameters) {
    Json item = object();
    item["location"] = convert(parameter.location);
    item["name"] = parameter.name;
    item["type"] = convert(*parameter.type);
    result["parameters"].get_array().push_back(std::move(item));
  }
  if (value.returnType)
    result["returnType"] = convert(*value.returnType);
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
  if (format == "yaml")
    error = glz::write_yaml(ast, output);
  else
    error = glz::write<glz::opts{.prettify = true}>(ast, output);
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
  return outputFile ? 0 : 1;
}
