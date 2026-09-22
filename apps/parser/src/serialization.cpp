module;

#include <expected>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include <glaze/json.hpp>
#include <glaze/yaml.hpp>

module mioyi.parser;

import mioyi.ast;

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

[[noreturn]] void invalidAst(const std::string &message) {
  throw std::runtime_error("invalid serialized AST: " + message);
}

const Json &required(const Json &value, std::string_view key) {
  if (!value.is_object() || !value.contains(key))
    invalidAst("missing '" + std::string(key) + "'");
  return value[key];
}

std::string stringValue(const Json &value, std::string_view key) {
  const auto &member = required(value, key);
  if (!member.is_string())
    invalidAst("'" + std::string(key) + "' must be a string");
  return member.get_string();
}

bool boolValue(const Json &value, std::string_view key) {
  const auto &member = required(value, key);
  if (!member.is_boolean())
    invalidAst("'" + std::string(key) + "' must be a boolean");
  return member.get_boolean();
}

std::uint64_t unsignedValue(const Json &value, std::string_view key) {
  const auto &member = required(value, key);
  if (!member.is_number())
    invalidAst("'" + std::string(key) + "' must be a number");
  return member.as<std::uint64_t>();
}

double numberValue(const Json &value, std::string_view key) {
  const auto &member = required(value, key);
  if (!member.is_number())
    invalidAst("'" + std::string(key) + "' must be a number");
  return member.as<double>();
}

mioyi::SourceLocation decodeLocation(const Json &value) {
  return {static_cast<std::size_t>(unsignedValue(value, "line")),
          static_cast<std::size_t>(unsignedValue(value, "column"))};
}

std::unique_ptr<mioyi::TypeRef> decodeType(const Json &value) {
  auto result = std::make_unique<mioyi::TypeRef>();
  result->location = decodeLocation(required(value, "location"));
  result->name = stringValue(value, "name");
  if (value.contains("element"))
    result->element = decodeType(value["element"]);
  return result;
}

std::unique_ptr<mioyi::Expression> decodeExpression(const Json &value) {
  auto result = std::make_unique<mioyi::Expression>();
  result->location = decodeLocation(required(value, "location"));
  const std::string kind = stringValue(value, "kind");
  using Kind = mioyi::Expression::Kind;
  if (kind == "integer") {
    result->kind = Kind::Integer;
    result->integer = unsignedValue(value, "value");
    result->text = stringValue(value, "type");
  } else if (kind == "floating") {
    result->kind = Kind::Floating;
    result->floating = numberValue(value, "value");
    result->text = stringValue(value, "type");
  } else if (kind == "string") {
    result->kind = Kind::String;
    result->text = stringValue(value, "value");
  } else if (kind == "character") {
    result->kind = Kind::Character;
    result->integer = unsignedValue(value, "value");
  } else if (kind == "boolean") {
    result->kind = Kind::Boolean;
    result->boolean = boolValue(value, "value");
  } else if (kind == "void") {
    result->kind = Kind::Void;
  } else if (kind == "lvalue") {
    result->kind = Kind::LValue;
    result->text = stringValue(value, "name");
  } else if (kind == "array") {
    result->kind = Kind::Array;
  } else if (kind == "call") {
    result->kind = Kind::Call;
  } else if (kind == "index") {
    result->kind = Kind::Index;
  } else if (kind == "unary") {
    result->kind = Kind::Unary;
    result->text = stringValue(value, "operator");
  } else if (kind == "binary") {
    result->kind = Kind::Binary;
    result->text = stringValue(value, "operator");
  } else {
    invalidAst("unknown expression kind '" + kind + "'");
  }
  if (value.contains("operands")) {
    const auto &operands = value["operands"];
    if (!operands.is_array())
      invalidAst("'operands' must be an array");
    for (const auto &operand : operands.get_array())
      result->operands.push_back(decodeExpression(operand));
  }
  return result;
}

std::unique_ptr<mioyi::Declaration> decodeDeclaration(const Json &value) {
  auto result = std::make_unique<mioyi::Declaration>();
  const std::string kind = stringValue(value, "bindingKind");
  if (kind == "var")
    result->kind = mioyi::BindingKind::Variable;
  else if (kind == "val")
    result->kind = mioyi::BindingKind::Value;
  else if (kind == "def")
    result->kind = mioyi::BindingKind::Definition;
  else
    invalidAst("unknown binding kind '" + kind + "'");
  result->exported = boolValue(value, "exported");
  const auto &bindings = required(value, "bindings");
  if (!bindings.is_array())
    invalidAst("'bindings' must be an array");
  for (const auto &valueBinding : bindings.get_array()) {
    mioyi::Binding binding;
    binding.location = decodeLocation(required(valueBinding, "location"));
    binding.name = stringValue(valueBinding, "name");
    if (valueBinding.contains("type"))
      binding.type = decodeType(valueBinding["type"]);
    binding.initializer =
        decodeExpression(required(valueBinding, "initializer"));
    result->bindings.push_back(std::move(binding));
  }
  return result;
}

std::unique_ptr<mioyi::Statement> decodeStatement(const Json &value) {
  auto result = std::make_unique<mioyi::Statement>();
  result->location = decodeLocation(required(value, "location"));
  const std::string kind = stringValue(value, "kind");
  using Kind = mioyi::Statement::Kind;
  if (kind == "declaration")
    result->kind = Kind::Declaration;
  else if (kind == "assignment") {
    result->kind = Kind::Assignment;
    result->text = stringValue(value, "operator");
  } else if (kind == "expression")
    result->kind = Kind::Expression;
  else if (kind == "block")
    result->kind = Kind::Block;
  else if (kind == "if")
    result->kind = Kind::If;
  else if (kind == "for")
    result->kind = Kind::For;
  else if (kind == "forEach") {
    result->kind = Kind::ForEach;
    result->text = stringValue(value, "binding");
  } else if (kind == "break")
    result->kind = Kind::Break;
  else if (kind == "continue")
    result->kind = Kind::Continue;
  else if (kind == "return")
    result->kind = Kind::Return;
  else
    invalidAst("unknown statement kind '" + kind + "'");

  if (value.contains("declaration"))
    result->declaration = decodeDeclaration(value["declaration"]);
  if (value.contains("target"))
    result->target = decodeExpression(value["target"]);
  if (value.contains("expression"))
    result->expression = decodeExpression(value["expression"]);
  if (value.contains("statements")) {
    const auto &statements = value["statements"];
    if (!statements.is_array())
      invalidAst("'statements' must be an array");
    for (const auto &statement : statements.get_array())
      result->block.push_back(decodeStatement(statement));
  }
  if (value.contains("then"))
    result->thenBranch = decodeStatement(value["then"]);
  if (value.contains("else"))
    result->elseBranch = decodeStatement(value["else"]);
  return result;
}

std::unique_ptr<mioyi::Function> decodeFunction(const Json &value) {
  auto result = std::make_unique<mioyi::Function>();
  result->location = decodeLocation(required(value, "location"));
  result->name = stringValue(value, "name");
  result->exported = boolValue(value, "exported");
  const auto &genericParameters = required(value, "genericParameters");
  if (!genericParameters.is_array())
    invalidAst("'genericParameters' must be an array");
  for (const auto &parameter : genericParameters.get_array()) {
    if (!parameter.is_string())
      invalidAst("generic parameters must be strings");
    result->genericParameters.push_back(parameter.get_string());
  }
  const auto &parameters = required(value, "parameters");
  if (!parameters.is_array())
    invalidAst("'parameters' must be an array");
  for (const auto &valueParameter : parameters.get_array()) {
    mioyi::Parameter parameter;
    parameter.location = decodeLocation(required(valueParameter, "location"));
    parameter.name = stringValue(valueParameter, "name");
    parameter.type = decodeType(required(valueParameter, "type"));
    result->parameters.push_back(std::move(parameter));
  }
  if (value.contains("returnType"))
    result->returnType = decodeType(value["returnType"]);
  result->body = decodeStatement(required(value, "body"));
  return result;
}

std::unique_ptr<mioyi::Ast> decodeAst(const Json &value) {
  auto result = std::make_unique<mioyi::Ast>();
  const auto &items = required(value, "items");
  if (!items.is_array())
    invalidAst("'items' must be an array");
  for (const auto &valueItem : items.get_array()) {
    mioyi::TopLevel item;
    const std::string kind = stringValue(valueItem, "kind");
    if (kind == "declaration")
      item.declaration = decodeDeclaration(required(valueItem, "declaration"));
    else if (kind == "function")
      item.function = decodeFunction(required(valueItem, "function"));
    else
      invalidAst("unknown top-level kind '" + kind + "'");
    result->items.push_back(std::move(item));
  }
  return result;
}

} // namespace

std::unique_ptr<mioyi::Ast>
mioyi::parser::deserializeAst(std::string_view source,
                              std::string_view format) {
  Json value;
  glz::error_ctx error;
  if (format == "yaml")
    error = glz::read_yaml(value, source);
  else
    error = glz::read_json(value, source);
  if (error) {
    std::cerr << "error: failed to deserialize AST: "
              << glz::format_error(error, source) << '\n';
    return nullptr;
  }
  try {
    return decodeAst(value);
  } catch (const std::exception &error) {
    std::cerr << "error: " << error.what() << '\n';
    return nullptr;
  }
}

mioyi::parser::Result mioyi::parser::parse(Options options) {
  std::ifstream inputFile;
  std::istream *source = &std::cin;
  if (!options.input.empty()) {
    inputFile.open(options.input);
    if (!inputFile)
      return std::unexpected("cannot open input file '" + options.input + "'");
    source = &inputFile;
  }
  std::ostringstream sourceBuffer;
  sourceBuffer << source->rdbuf();
  auto ast = mioyi::parser::parseSource(sourceBuffer.str());
  if (!ast)
    return std::unexpected("failed to parse Mioyi source");

  std::string output;
  if (!serialize(convert(*ast), options.format, output))
    return std::unexpected("failed to serialize AST");
  output.push_back('\n');
  if (options.output == "-") {
    std::cout << output;
    if (!std::cout)
      return std::unexpected("failed to write AST to stdout");
    return {};
  }
  std::ofstream outputFile(options.output);
  if (!outputFile)
    return std::unexpected("cannot open output file '" + options.output + "'");
  outputFile << output;
  if (!outputFile)
    return std::unexpected("failed to write output file '" + options.output +
                           "'");
  return {};
}
