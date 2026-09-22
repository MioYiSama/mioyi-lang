module;

#include <cstddef>
#include <cstdint>
#include <expected>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>

export module mioyi.transformer;

import mioyi.ast;
import mioyi.parser;

namespace {

class CodegenError final : public std::runtime_error {
public:
  CodegenError(mioyi::SourceLocation location, const std::string &message)
      : std::runtime_error(std::to_string(location.line) + ":" +
                           std::to_string(location.column) + ": " + message) {}
};

struct Type {
  enum class Kind {
    Byte,
    Int32,
    Int64,
    UInt32,
    UInt64,
    Float32,
    Float64,
    Character,
    String,
    Boolean,
    Void,
    Array
  } kind = Kind::Void;
  std::shared_ptr<Type> element;
  std::size_t length = 0;
};

bool operator==(const Type &left, const Type &right) {
  if (left.kind != right.kind)
    return false;
  if (left.kind != Type::Kind::Array)
    return true;
  return left.length == right.length && left.element && right.element &&
         *left.element == *right.element;
}

bool isInteger(const Type &type) {
  using Kind = Type::Kind;
  return type.kind == Kind::Byte || type.kind == Kind::Int32 ||
         type.kind == Kind::Int64 || type.kind == Kind::UInt32 ||
         type.kind == Kind::UInt64 || type.kind == Kind::Character;
}

bool isSigned(const Type &type) {
  return type.kind == Type::Kind::Int32 || type.kind == Type::Kind::Int64;
}

bool isFloating(const Type &type) {
  return type.kind == Type::Kind::Float32 || type.kind == Type::Kind::Float64;
}

std::string typeName(const Type &type) {
  using Kind = Type::Kind;
  switch (type.kind) {
  case Kind::Byte:
    return "Byte";
  case Kind::Int32:
    return "Int";
  case Kind::Int64:
    return "Int64";
  case Kind::UInt32:
    return "UInt";
  case Kind::UInt64:
    return "UInt64";
  case Kind::Float32:
    return "Float";
  case Kind::Float64:
    return "Float64";
  case Kind::Character:
    return "Char";
  case Kind::String:
    return "String";
  case Kind::Boolean:
    return "Bool";
  case Kind::Void:
    return "Void";
  case Kind::Array:
    return "[" + typeName(*type.element) + "]";
  }
  return "<?>";
}

struct Value {
  llvm::Value *value = nullptr;
  Type type;
};

class MioyiCodegen {
  struct Symbol {
    llvm::Value *address = nullptr;
    Type type;
    bool mutableValue = false;
  };

  struct FunctionInfo {
    const mioyi::Function *ast = nullptr;
    std::vector<Type> parameters;
    Type result;
    bool resultKnown = false;
    bool inferring = false;
    llvm::Function *llvmFunction = nullptr;
  };

  struct LoopTargets {
    llvm::BasicBlock *condition = nullptr;
    llvm::BasicBlock *exit = nullptr;
  };

public:
  MioyiCodegen(llvm::LLVMContext &context, llvm::Module &module)
      : context_(context), module_(module), builder_(context) {
    scopes_.emplace_back();
    declareRuntime();
  }

  void generate(const mioyi::Ast &ast) {
    for (const auto &item : ast.items)
      if (item.declaration)
        emitDeclaration(*item.declaration, true);

    for (const auto &item : ast.items) {
      if (!item.function)
        continue;
      const auto &function = *item.function;
      if (!function.genericParameters.empty())
        fail(function.location,
             "generic functions are reserved for the next compiler stage");
      if (functions_.contains(function.name) ||
          module_.getFunction(function.name))
        fail(function.location,
             "redefinition of function '" + function.name + "'");
      FunctionInfo info;
      info.ast = &function;
      for (const auto &parameter : function.parameters)
        info.parameters.push_back(resolveType(*parameter.type));
      functions_.emplace(function.name, std::move(info));
    }

    for (auto &[name, info] : functions_)
      (void)inferFunctionResult(name, info);
    for (auto &[name, info] : functions_)
      declareFunction(name, info);

    if (!functions_.contains("main"))
      fail({1, 1}, "program must define 'def main = { () => Int ... }'");
    auto &main = functions_.at("main");
    if (!main.parameters.empty() || main.result.kind != Type::Kind::Int32)
      fail(main.ast->location, "main must have type '() => Int'");

    for (const auto &item : ast.items)
      if (item.function)
        emitFunction(*item.function);
  }

private:
  [[noreturn]] static void fail(mioyi::SourceLocation location,
                                const std::string &message) {
    throw CodegenError(location, message);
  }

  llvm::Type *llvmType(const Type &type) {
    using Kind = Type::Kind;
    switch (type.kind) {
    case Kind::Byte:
      return builder_.getInt8Ty();
    case Kind::Int32:
    case Kind::UInt32:
    case Kind::Character:
      return builder_.getInt32Ty();
    case Kind::Int64:
    case Kind::UInt64:
      return builder_.getInt64Ty();
    case Kind::Float32:
      return builder_.getFloatTy();
    case Kind::Float64:
      return builder_.getDoubleTy();
    case Kind::String:
      return builder_.getPtrTy();
    case Kind::Boolean:
      return builder_.getInt1Ty();
    case Kind::Void:
      return builder_.getVoidTy();
    case Kind::Array:
      return llvm::ArrayType::get(llvmType(*type.element), type.length);
    }
    return builder_.getVoidTy();
  }

  Type resolveName(const std::string &name, mioyi::SourceLocation location) {
    using Kind = Type::Kind;
    if (name == "Byte")
      return {Kind::Byte};
    if (name == "Int" || name == "Int32")
      return {Kind::Int32};
    if (name == "Int64")
      return {Kind::Int64};
    if (name == "UInt" || name == "UInt32")
      return {Kind::UInt32};
    if (name == "UInt64")
      return {Kind::UInt64};
    if (name == "Float" || name == "Float32")
      return {Kind::Float32};
    if (name == "Float64")
      return {Kind::Float64};
    if (name == "Char")
      return {Kind::Character};
    if (name == "String")
      return {Kind::String};
    if (name == "Bool")
      return {Kind::Boolean};
    if (name == "Void")
      return {Kind::Void};
    if (name == "Never")
      fail(location, "Never values are not constructible in the demo compiler");
    fail(location, "unknown type '" + name + "'");
  }

  Type resolveType(const mioyi::TypeRef &type) {
    if (type.name != "Array")
      return resolveName(type.name, type.location);
    Type result{Type::Kind::Array};
    result.element = std::make_shared<Type>(resolveType(*type.element));
    return result;
  }

  Type literalIntegerType(const mioyi::Expression &expression) {
    return resolveName(expression.text, expression.location);
  }

  Symbol &lookup(const std::string &name, mioyi::SourceLocation location) {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope)
      if (auto found = scope->find(name); found != scope->end())
        return found->second;
    fail(location, "unknown identifier '" + name + "'");
  }

  void bind(mioyi::SourceLocation location, const std::string &name,
            Symbol symbol) {
    if (!scopes_.back().emplace(name, std::move(symbol)).second)
      fail(location, "redefinition of '" + name + "'");
  }

  Type inferExpression(const mioyi::Expression &expression) {
    using Kind = mioyi::Expression::Kind;
    switch (expression.kind) {
    case Kind::Integer:
      return literalIntegerType(expression);
    case Kind::Floating:
      return resolveName(expression.text, expression.location);
    case Kind::String:
      return {Type::Kind::String};
    case Kind::Character:
      return {Type::Kind::Character};
    case Kind::Boolean:
      return {Type::Kind::Boolean};
    case Kind::Void:
      return {Type::Kind::Void};
    case Kind::LValue:
      return lookup(expression.text, expression.location).type;
    case Kind::Array: {
      if (expression.operands.empty())
        fail(expression.location,
             "cannot infer the element type of an empty array");
      Type element = inferExpression(*expression.operands.front());
      for (const auto &item : expression.operands)
        if (!(inferExpression(*item) == element))
          fail(item->location, "array elements must have one type");
      return {Type::Kind::Array, std::make_shared<Type>(element),
              expression.operands.size()};
    }
    case Kind::Index: {
      Type aggregate = inferExpression(*expression.operands[0]);
      if (aggregate.kind != Type::Kind::Array)
        fail(expression.location, "only arrays can be indexed");
      return *aggregate.element;
    }
    case Kind::Call: {
      const auto &callee = *expression.operands.front();
      if (callee.kind != Kind::LValue)
        fail(expression.location,
             "the demo compiler only calls named functions");
      if (callee.text == "print")
        return {Type::Kind::Void};
      auto found = functions_.find(callee.text);
      if (found == functions_.end())
        fail(callee.location, "unknown function '" + callee.text + "'");
      return inferFunctionResult(found->first, found->second);
    }
    case Kind::Unary: {
      Type operand = inferExpression(*expression.operands.front());
      if (expression.text == "!")
        return {Type::Kind::Boolean};
      return operand;
    }
    case Kind::Binary: {
      Type left = inferExpression(*expression.operands[0]);
      Type right = inferExpression(*expression.operands[1]);
      if (!(left == right))
        fail(expression.location, "operator operands have types '" +
                                      typeName(left) + "' and '" +
                                      typeName(right) + "'");
      if (expression.text == "==" || expression.text == "!=" ||
          expression.text == "<" || expression.text == ">" ||
          expression.text == "<=" || expression.text == ">=" ||
          expression.text == "&&" || expression.text == "||")
        return {Type::Kind::Boolean};
      return left;
    }
    }
    fail(expression.location, "cannot infer expression type");
  }

  Type inferFunctionResult(const std::string &name, FunctionInfo &info) {
    if (info.resultKnown)
      return info.result;
    if (info.inferring)
      fail(info.ast->location,
           "recursive function '" + name + "' needs an explicit return type");
    info.inferring = true;
    if (info.ast->returnType) {
      info.result = resolveType(*info.ast->returnType);
    } else {
      scopes_.emplace_back();
      for (std::size_t index = 0; index < info.ast->parameters.size(); ++index)
        bind(info.ast->parameters[index].location,
             info.ast->parameters[index].name,
             {nullptr, info.parameters[index], false});
      const auto &items = info.ast->body->block;
      for (std::size_t index = 0; index + 1 < items.size(); ++index) {
        if (items[index]->kind != mioyi::Statement::Kind::Declaration)
          continue;
        const auto &declaration = *items[index]->declaration;
        for (const auto &binding : declaration.bindings) {
          Type type = inferExpression(*binding.initializer);
          checkAnnotation(binding, type);
          bind(binding.location, binding.name,
               {nullptr, type,
                declaration.kind == mioyi::BindingKind::Variable});
        }
      }
      if (items.empty() ||
          items.back()->kind != mioyi::Statement::Kind::Expression)
        info.result = {Type::Kind::Void};
      else
        info.result = inferExpression(*items.back()->expression);
      scopes_.pop_back();
    }
    info.inferring = false;
    info.resultKnown = true;
    return info.result;
  }

  void declareRuntime() {
    printf_ = llvm::Function::Create(
        llvm::FunctionType::get(builder_.getInt32Ty(), {builder_.getPtrTy()},
                                true),
        llvm::Function::ExternalLinkage, "printf", module_);
    putchar_ = llvm::Function::Create(
        llvm::FunctionType::get(builder_.getInt32Ty(), {builder_.getInt32Ty()},
                                false),
        llvm::Function::ExternalLinkage, "putchar", module_);
  }

  void declareFunction(const std::string &name, FunctionInfo &info) {
    std::vector<llvm::Type *> parameters;
    for (const Type &type : info.parameters)
      parameters.push_back(llvmType(type));
    info.llvmFunction = llvm::Function::Create(
        llvm::FunctionType::get(llvmType(info.result), parameters, false),
        llvm::Function::ExternalLinkage, name, module_);
  }

  bool currentBlockTerminated() const {
    auto *block = builder_.GetInsertBlock();
    return block && !block->empty() && block->back().isTerminator();
  }

  llvm::AllocaInst *createAlloca(llvm::Type *type, const std::string &name) {
    auto &entry = currentFunction_->getEntryBlock();
    llvm::IRBuilder<> allocations(&entry, entry.begin());
    return allocations.CreateAlloca(type, nullptr, name);
  }

  llvm::Constant *constantExpression(const mioyi::Expression &expression,
                                     Type &type) {
    using Kind = mioyi::Expression::Kind;
    switch (expression.kind) {
    case Kind::Integer:
      type = literalIntegerType(expression);
      return llvm::ConstantInt::get(llvmType(type), expression.integer,
                                    isSigned(type));
    case Kind::Floating:
      type = resolveName(expression.text, expression.location);
      return llvm::ConstantFP::get(llvmType(type), expression.floating);
    case Kind::Boolean:
      type = {Type::Kind::Boolean};
      return llvm::ConstantInt::get(llvmType(type), expression.boolean);
    case Kind::Character:
      type = {Type::Kind::Character};
      return llvm::ConstantInt::get(llvmType(type), expression.integer);
    case Kind::String:
      type = {Type::Kind::String};
      return llvm::cast<llvm::Constant>(
          builder_.CreateGlobalString(expression.text));
    case Kind::Array: {
      if (expression.operands.empty())
        fail(expression.location,
             "cannot infer the element type of an empty array");
      std::vector<llvm::Constant *> elements;
      Type elementType;
      for (const auto &element : expression.operands) {
        Type current;
        elements.push_back(constantExpression(*element, current));
        if (!elements.empty() && elements.size() > 1 &&
            !(current == elementType))
          fail(element->location, "array elements must have one type");
        elementType = current;
      }
      type = {Type::Kind::Array, std::make_shared<Type>(elementType),
              elements.size()};
      return llvm::ConstantArray::get(
          llvm::cast<llvm::ArrayType>(llvmType(type)), elements);
    }
    default:
      fail(expression.location, "global and def initializers must be literal "
                                "constants in the demo compiler");
    }
  }

  void checkAnnotation(const mioyi::Binding &binding, Type &type) {
    if (!binding.type)
      return;
    Type annotation = resolveType(*binding.type);
    if (annotation.kind == Type::Kind::Array && annotation.length == 0 &&
        type.kind == Type::Kind::Array) {
      if (!(*annotation.element == *type.element))
        fail(binding.location, "initializer has type '" + typeName(type) +
                                   "', expected '" + typeName(annotation) +
                                   "'");
      return;
    }
    if (!(annotation == type))
      fail(binding.location, "initializer has type '" + typeName(type) +
                                 "', expected '" + typeName(annotation) + "'");
  }

  void emitDeclaration(const mioyi::Declaration &declaration, bool global) {
    for (const auto &binding : declaration.bindings) {
      Type type;
      if (global) {
        llvm::Constant *initial =
            constantExpression(*binding.initializer, type);
        checkAnnotation(binding, type);
        auto *variable = new llvm::GlobalVariable(
            module_, llvmType(type),
            declaration.kind != mioyi::BindingKind::Variable,
            binding.name == "main" ? llvm::GlobalValue::ExternalLinkage
                                   : llvm::GlobalValue::InternalLinkage,
            initial, binding.name);
        bind(
            binding.location, binding.name,
            {variable, type, declaration.kind == mioyi::BindingKind::Variable});
      } else {
        Value initial = emitExpression(*binding.initializer);
        type = initial.type;
        checkAnnotation(binding, type);
        if (type.kind == Type::Kind::Void)
          fail(binding.location, "a binding cannot have type Void");
        auto *slot = createAlloca(llvmType(type), binding.name);
        builder_.CreateStore(initial.value, slot);
        bind(binding.location, binding.name,
             {slot, type, declaration.kind == mioyi::BindingKind::Variable});
      }
    }
  }

  void emitFunction(const mioyi::Function &function) {
    FunctionInfo &info = functions_.at(function.name);
    currentFunction_ = info.llvmFunction;
    currentResult_ = info.result;
    auto *entry = llvm::BasicBlock::Create(context_, "entry", currentFunction_);
    builder_.SetInsertPoint(entry);
    scopes_.emplace_back();
    std::size_t index = 0;
    for (auto &argument : currentFunction_->args()) {
      const auto &parameter = function.parameters[index];
      const Type &type = info.parameters[index++];
      argument.setName(parameter.name);
      auto *slot = createAlloca(llvmType(type), parameter.name + ".addr");
      builder_.CreateStore(&argument, slot);
      bind(parameter.location, parameter.name, {slot, type, false});
    }

    const auto &items = function.body->block;
    for (std::size_t item = 0; item < items.size(); ++item) {
      if (currentBlockTerminated())
        break;
      const bool implicitReturn =
          item + 1 == items.size() &&
          items[item]->kind == mioyi::Statement::Kind::Expression &&
          currentResult_.kind != Type::Kind::Void;
      if (implicitReturn) {
        Value result = emitExpression(*items[item]->expression);
        requireType(items[item]->location, result.type, currentResult_,
                    "implicit return");
        builder_.CreateRet(result.value);
      } else
        emitStatement(*items[item]);
    }
    if (!currentBlockTerminated()) {
      if (currentResult_.kind == Type::Kind::Void)
        builder_.CreateRetVoid();
      else
        fail(function.location,
             "function '" + function.name + "' may not return a value");
    }
    scopes_.pop_back();
    currentFunction_ = nullptr;
  }

  void requireType(mioyi::SourceLocation location, const Type &actual,
                   const Type &expected, std::string_view operation) {
    if (!(actual == expected))
      fail(location, std::string(operation) + " has type '" + typeName(actual) +
                         "', expected '" + typeName(expected) + "'");
  }

  Value emitExpression(const mioyi::Expression &expression) {
    using Kind = mioyi::Expression::Kind;
    switch (expression.kind) {
    case Kind::Integer: {
      Type type = literalIntegerType(expression);
      return {llvm::ConstantInt::get(llvmType(type), expression.integer,
                                     isSigned(type)),
              type};
    }
    case Kind::Floating: {
      Type type = resolveName(expression.text, expression.location);
      return {llvm::ConstantFP::get(llvmType(type), expression.floating), type};
    }
    case Kind::String:
      return {builder_.CreateGlobalString(expression.text),
              {Type::Kind::String}};
    case Kind::Character:
      return {builder_.getInt32(expression.integer), {Type::Kind::Character}};
    case Kind::Boolean:
      return {builder_.getInt1(expression.boolean), {Type::Kind::Boolean}};
    case Kind::Void:
      return {nullptr, {Type::Kind::Void}};
    case Kind::LValue: {
      Symbol &symbol = lookup(expression.text, expression.location);
      return {builder_.CreateLoad(llvmType(symbol.type), symbol.address,
                                  expression.text + ".value"),
              symbol.type};
    }
    case Kind::Array: {
      Type type = inferExpression(expression);
      llvm::Value *aggregate = llvm::UndefValue::get(llvmType(type));
      for (std::size_t index = 0; index < expression.operands.size(); ++index) {
        Value element = emitExpression(*expression.operands[index]);
        aggregate = builder_.CreateInsertValue(aggregate, element.value,
                                               {static_cast<unsigned>(index)},
                                               "array.element");
      }
      return {aggregate, type};
    }
    case Kind::Index: {
      auto [address, type] = emitAddress(expression);
      return {builder_.CreateLoad(llvmType(type), address, "index.value"),
              type};
    }
    case Kind::Call:
      return emitCall(expression);
    case Kind::Unary:
      return emitUnary(expression);
    case Kind::Binary:
      return emitBinary(expression);
    }
    fail(expression.location, "unsupported expression");
  }

  std::pair<llvm::Value *, Type>
  emitAddress(const mioyi::Expression &expression) {
    if (expression.kind == mioyi::Expression::Kind::LValue) {
      Symbol &symbol = lookup(expression.text, expression.location);
      return {symbol.address, symbol.type};
    }
    if (expression.kind != mioyi::Expression::Kind::Index)
      fail(expression.location, "expression is not assignable");
    auto [base, aggregate] = emitAddress(*expression.operands[0]);
    if (aggregate.kind != Type::Kind::Array)
      fail(expression.location, "only arrays can be indexed");
    Value index = emitExpression(*expression.operands[1]);
    if (!isInteger(index.type))
      fail(expression.operands[1]->location, "array index must be an integer");
    if (!index.value->getType()->isIntegerTy(64))
      index.value =
          builder_.CreateZExtOrTrunc(index.value, builder_.getInt64Ty());
    auto *address = builder_.CreateInBoundsGEP(
        llvmType(aggregate), base, {builder_.getInt64(0), index.value},
        "array.index");
    return {address, *aggregate.element};
  }

  std::string rootName(const mioyi::Expression &expression) {
    if (expression.kind == mioyi::Expression::Kind::LValue)
      return expression.text;
    return rootName(*expression.operands.front());
  }

  Value emitCall(const mioyi::Expression &expression) {
    const auto &callee = *expression.operands.front();
    if (callee.kind != mioyi::Expression::Kind::LValue)
      fail(expression.location, "the demo compiler only calls named functions");
    if (callee.text == "print") {
      if (expression.operands.size() != 2)
        fail(expression.location, "print expects exactly one value");
      Value argument = emitExpression(*expression.operands[1]);
      printValue(argument);
      builder_.CreateCall(putchar_, {builder_.getInt32('\n')});
      return {nullptr, {Type::Kind::Void}};
    }
    auto found = functions_.find(callee.text);
    if (found == functions_.end())
      fail(callee.location, "unknown function '" + callee.text + "'");
    FunctionInfo &function = found->second;
    if (expression.operands.size() - 1 != function.parameters.size())
      fail(expression.location,
           "wrong number of arguments in call to '" + callee.text + "'");
    std::vector<llvm::Value *> arguments;
    for (std::size_t index = 1; index < expression.operands.size(); ++index) {
      Value argument = emitExpression(*expression.operands[index]);
      requireType(expression.operands[index]->location, argument.type,
                  function.parameters[index - 1], "argument");
      arguments.push_back(argument.value);
    }
    auto *call = builder_.CreateCall(
        function.llvmFunction, arguments,
        function.result.kind == Type::Kind::Void ? "" : callee.text + ".call");
    return {function.result.kind == Type::Kind::Void ? nullptr : call,
            function.result};
  }

  Value emitUnary(const mioyi::Expression &expression) {
    Value operand = emitExpression(*expression.operands.front());
    if (expression.text == "!")
      return {builder_.CreateNot(asCondition(operand), "not"),
              {Type::Kind::Boolean}};
    if (!isInteger(operand.type) && !isFloating(operand.type))
      fail(expression.location,
           "unary '" + expression.text + "' requires a numeric value");
    if (expression.text == "+")
      return operand;
    if (isFloating(operand.type))
      return {builder_.CreateFNeg(operand.value, "neg"), operand.type};
    return {builder_.CreateNeg(operand.value, "neg"), operand.type};
  }

  llvm::Value *asCondition(Value value) {
    if (value.type.kind == Type::Kind::Boolean)
      return value.value;
    if (isInteger(value.type))
      return builder_.CreateICmpNE(
          value.value, llvm::ConstantInt::get(llvmType(value.type), 0),
          "tobool");
    if (isFloating(value.type))
      return builder_.CreateFCmpONE(
          value.value, llvm::ConstantFP::get(llvmType(value.type), 0.0),
          "tobool");
    fail({0, 0}, "condition must be Bool or numeric");
  }

  Value emitLogical(const mioyi::Expression &expression, bool andOperator) {
    llvm::Value *left = asCondition(emitExpression(*expression.operands[0]));
    auto *origin = builder_.GetInsertBlock();
    auto *rightBlock = llvm::BasicBlock::Create(
        context_, andOperator ? "and.rhs" : "or.rhs", currentFunction_);
    auto *merge = llvm::BasicBlock::Create(
        context_, andOperator ? "and.end" : "or.end", currentFunction_);
    builder_.CreateCondBr(left, andOperator ? rightBlock : merge,
                          andOperator ? merge : rightBlock);
    builder_.SetInsertPoint(rightBlock);
    llvm::Value *right = asCondition(emitExpression(*expression.operands[1]));
    auto *rightEnd = builder_.GetInsertBlock();
    builder_.CreateBr(merge);
    builder_.SetInsertPoint(merge);
    auto *phi =
        builder_.CreatePHI(builder_.getInt1Ty(), 2, andOperator ? "and" : "or");
    phi->addIncoming(builder_.getInt1(!andOperator), origin);
    phi->addIncoming(right, rightEnd);
    return {phi, {Type::Kind::Boolean}};
  }

  Value emitBinary(const mioyi::Expression &expression) {
    if (expression.text == "&&")
      return emitLogical(expression, true);
    if (expression.text == "||")
      return emitLogical(expression, false);
    Value left = emitExpression(*expression.operands[0]);
    Value right = emitExpression(*expression.operands[1]);
    requireType(expression.location, right.type, left.type, "right operand");
    const bool comparison = expression.text == "==" ||
                            expression.text == "!=" || expression.text == "<" ||
                            expression.text == ">" || expression.text == "<=" ||
                            expression.text == ">=";
    if (isFloating(left.type)) {
      if (!comparison) {
        if (expression.text == "+")
          return {builder_.CreateFAdd(left.value, right.value), left.type};
        if (expression.text == "-")
          return {builder_.CreateFSub(left.value, right.value), left.type};
        if (expression.text == "*")
          return {builder_.CreateFMul(left.value, right.value), left.type};
        if (expression.text == "/")
          return {builder_.CreateFDiv(left.value, right.value), left.type};
        if (expression.text == "%")
          return {builder_.CreateFRem(left.value, right.value), left.type};
      }
      llvm::CmpInst::Predicate predicate = llvm::CmpInst::FCMP_OEQ;
      if (expression.text == "!=")
        predicate = llvm::CmpInst::FCMP_ONE;
      else if (expression.text == "<")
        predicate = llvm::CmpInst::FCMP_OLT;
      else if (expression.text == ">")
        predicate = llvm::CmpInst::FCMP_OGT;
      else if (expression.text == "<=")
        predicate = llvm::CmpInst::FCMP_OLE;
      else if (expression.text == ">=")
        predicate = llvm::CmpInst::FCMP_OGE;
      return {builder_.CreateFCmp(predicate, left.value, right.value),
              {Type::Kind::Boolean}};
    }
    if (!isInteger(left.type) && left.type.kind != Type::Kind::Boolean)
      fail(expression.location, "operator '" + expression.text +
                                    "' does not support " +
                                    typeName(left.type));
    if (!comparison) {
      if (!isInteger(left.type))
        fail(expression.location, "arithmetic requires numeric values");
      if (expression.text == "+")
        return {builder_.CreateAdd(left.value, right.value), left.type};
      if (expression.text == "-")
        return {builder_.CreateSub(left.value, right.value), left.type};
      if (expression.text == "*")
        return {builder_.CreateMul(left.value, right.value), left.type};
      if (expression.text == "/")
        return {isSigned(left.type)
                    ? builder_.CreateSDiv(left.value, right.value)
                    : builder_.CreateUDiv(left.value, right.value),
                left.type};
      if (expression.text == "%")
        return {isSigned(left.type)
                    ? builder_.CreateSRem(left.value, right.value)
                    : builder_.CreateURem(left.value, right.value),
                left.type};
    }
    llvm::CmpInst::Predicate predicate = llvm::CmpInst::ICMP_EQ;
    if (expression.text == "!=")
      predicate = llvm::CmpInst::ICMP_NE;
    else if (expression.text == "<")
      predicate = isSigned(left.type) ? llvm::CmpInst::ICMP_SLT
                                      : llvm::CmpInst::ICMP_ULT;
    else if (expression.text == ">")
      predicate = isSigned(left.type) ? llvm::CmpInst::ICMP_SGT
                                      : llvm::CmpInst::ICMP_UGT;
    else if (expression.text == "<=")
      predicate = isSigned(left.type) ? llvm::CmpInst::ICMP_SLE
                                      : llvm::CmpInst::ICMP_ULE;
    else if (expression.text == ">=")
      predicate = isSigned(left.type) ? llvm::CmpInst::ICMP_SGE
                                      : llvm::CmpInst::ICMP_UGE;
    return {builder_.CreateICmp(predicate, left.value, right.value),
            {Type::Kind::Boolean}};
  }

  void printText(std::string_view text) {
    builder_.CreateCall(printf_, {builder_.CreateGlobalString(text)});
  }

  void printValue(Value value) {
    using Kind = Type::Kind;
    switch (value.type.kind) {
    case Kind::Byte:
      builder_.CreateCall(
          printf_, {builder_.CreateGlobalString("%u"),
                    builder_.CreateZExt(value.value, builder_.getInt32Ty())});
      return;
    case Kind::Int32:
      builder_.CreateCall(printf_,
                          {builder_.CreateGlobalString("%d"), value.value});
      return;
    case Kind::UInt32:
    case Kind::Character:
      if (value.type.kind == Kind::Character) {
        emitUtf8Character(value.value);
        return;
      }
      builder_.CreateCall(printf_,
                          {builder_.CreateGlobalString("%u"), value.value});
      return;
    case Kind::Int64:
      builder_.CreateCall(printf_,
                          {builder_.CreateGlobalString("%lld"), value.value});
      return;
    case Kind::UInt64:
      builder_.CreateCall(printf_,
                          {builder_.CreateGlobalString("%llu"), value.value});
      return;
    case Kind::Float32:
      value.value = builder_.CreateFPExt(value.value, builder_.getDoubleTy());
      [[fallthrough]];
    case Kind::Float64:
      builder_.CreateCall(printf_,
                          {builder_.CreateGlobalString("%g"), value.value});
      return;
    case Kind::String:
      builder_.CreateCall(printf_,
                          {builder_.CreateGlobalString("%s"), value.value});
      return;
    case Kind::Boolean: {
      auto *text = builder_.CreateSelect(value.value,
                                         builder_.CreateGlobalString("true"),
                                         builder_.CreateGlobalString("false"));
      builder_.CreateCall(printf_, {builder_.CreateGlobalString("%s"), text});
      return;
    }
    case Kind::Void:
      printText("void");
      return;
    case Kind::Array:
      printText("[");
      for (std::size_t index = 0; index < value.type.length; ++index) {
        if (index)
          printText(", ");
        printValue({builder_.CreateExtractValue(value.value,
                                                {static_cast<unsigned>(index)}),
                    *value.type.element});
      }
      printText("]");
      return;
    }
  }

  void emitUtf8Character(llvm::Value *codepoint) {
    // UTF-8 encoding is branchless: emit each required leading byte only when
    // its range applies, then emit the final continuation/ASCII byte.
    auto emitConditionalByte = [&](llvm::Value *condition, llvm::Value *byte,
                                   const char *name) {
      auto *emit = llvm::BasicBlock::Create(context_, name, currentFunction_);
      auto *next =
          llvm::BasicBlock::Create(context_, "char.next", currentFunction_);
      builder_.CreateCondBr(condition, emit, next);
      builder_.SetInsertPoint(emit);
      builder_.CreateCall(putchar_, {byte});
      builder_.CreateBr(next);
      builder_.SetInsertPoint(next);
    };
    auto *aboveFFFF =
        builder_.CreateICmpUGT(codepoint, builder_.getInt32(0xffff));
    emitConditionalByte(
        aboveFFFF,
        builder_.CreateOr(
            builder_.getInt32(0xf0),
            builder_.CreateLShr(codepoint, builder_.getInt32(18))),
        "char.byte1");
    auto *above7FF =
        builder_.CreateICmpUGT(codepoint, builder_.getInt32(0x7ff));
    auto *thirdShift = builder_.CreateAnd(builder_.CreateLShr(codepoint, 12),
                                          builder_.getInt32(0x0f));
    auto *thirdPrefix = builder_.CreateSelect(
        aboveFFFF, builder_.getInt32(0x80), builder_.getInt32(0xe0));
    emitConditionalByte(above7FF, builder_.CreateOr(thirdPrefix, thirdShift),
                        "char.byte2");
    auto *above7F = builder_.CreateICmpUGT(codepoint, builder_.getInt32(0x7f));
    auto *secondShift = builder_.CreateAnd(builder_.CreateLShr(codepoint, 6),
                                           builder_.getInt32(0x3f));
    auto *secondPrefix = builder_.CreateSelect(
        above7FF, builder_.getInt32(0x80), builder_.getInt32(0xc0));
    emitConditionalByte(above7F, builder_.CreateOr(secondPrefix, secondShift),
                        "char.byte3");
    auto *last = builder_.CreateSelect(
        above7F,
        builder_.CreateOr(
            builder_.getInt32(0x80),
            builder_.CreateAnd(codepoint, builder_.getInt32(0x3f))),
        codepoint);
    builder_.CreateCall(putchar_, {last});
  }

  void emitStatement(const mioyi::Statement &statement) {
    using Kind = mioyi::Statement::Kind;
    switch (statement.kind) {
    case Kind::Declaration:
      emitDeclaration(*statement.declaration, false);
      return;
    case Kind::Assignment: {
      const std::string name = rootName(*statement.target);
      Symbol &symbol = lookup(name, statement.location);
      if (!symbol.mutableValue)
        fail(statement.location,
             "cannot assign to immutable binding '" + name + "'");
      auto [address, targetType] = emitAddress(*statement.target);
      Value value = emitExpression(*statement.expression);
      if (statement.text != "=") {
        Value current{builder_.CreateLoad(llvmType(targetType), address),
                      targetType};
        mioyi::Expression operation;
        operation.location = statement.location;
        operation.kind = mioyi::Expression::Kind::Binary;
        operation.text = statement.text.substr(0, 1);
        // Compound assignment is emitted directly to avoid manufacturing AST
        // ownership.
        requireType(statement.location, value.type, current.type, "assignment");
        if (isFloating(current.type)) {
          if (operation.text == "+")
            value.value = builder_.CreateFAdd(current.value, value.value);
          else if (operation.text == "-")
            value.value = builder_.CreateFSub(current.value, value.value);
          else if (operation.text == "*")
            value.value = builder_.CreateFMul(current.value, value.value);
          else if (operation.text == "/")
            value.value = builder_.CreateFDiv(current.value, value.value);
          else
            value.value = builder_.CreateFRem(current.value, value.value);
        } else {
          if (!isInteger(current.type))
            fail(statement.location,
                 "compound assignment requires numeric values");
          if (operation.text == "+")
            value.value = builder_.CreateAdd(current.value, value.value);
          else if (operation.text == "-")
            value.value = builder_.CreateSub(current.value, value.value);
          else if (operation.text == "*")
            value.value = builder_.CreateMul(current.value, value.value);
          else if (operation.text == "/")
            value.value = isSigned(current.type)
                              ? builder_.CreateSDiv(current.value, value.value)
                              : builder_.CreateUDiv(current.value, value.value);
          else
            value.value = isSigned(current.type)
                              ? builder_.CreateSRem(current.value, value.value)
                              : builder_.CreateURem(current.value, value.value);
        }
      }
      requireType(statement.location, value.type, targetType, "assignment");
      builder_.CreateStore(value.value, address);
      return;
    }
    case Kind::Expression:
      (void)emitExpression(*statement.expression);
      return;
    case Kind::Block:
      emitBlock(statement);
      return;
    case Kind::If:
      emitIf(statement);
      return;
    case Kind::For:
      emitFor(statement);
      return;
    case Kind::ForEach:
      emitForEach(statement);
      return;
    case Kind::Break:
      if (loops_.empty())
        fail(statement.location, "'break' is not inside a loop");
      builder_.CreateBr(loops_.back().exit);
      return;
    case Kind::Continue:
      if (loops_.empty())
        fail(statement.location, "'continue' is not inside a loop");
      builder_.CreateBr(loops_.back().condition);
      return;
    case Kind::Return:
      if (!statement.expression) {
        if (currentResult_.kind != Type::Kind::Void)
          fail(statement.location, "non-Void function must return a value");
        builder_.CreateRetVoid();
      } else {
        Value value = emitExpression(*statement.expression);
        requireType(statement.location, value.type, currentResult_, "return");
        builder_.CreateRet(value.value);
      }
      return;
    }
  }

  void emitBlock(const mioyi::Statement &block) {
    scopes_.emplace_back();
    for (const auto &item : block.block) {
      if (currentBlockTerminated())
        break;
      emitStatement(*item);
    }
    scopes_.pop_back();
  }

  void emitIf(const mioyi::Statement &statement) {
    auto *thenBlock =
        llvm::BasicBlock::Create(context_, "if.then", currentFunction_);
    auto *elseBlock =
        statement.elseBranch
            ? llvm::BasicBlock::Create(context_, "if.else", currentFunction_)
            : nullptr;
    auto *endBlock =
        llvm::BasicBlock::Create(context_, "if.end", currentFunction_);
    builder_.CreateCondBr(asCondition(emitExpression(*statement.expression)),
                          thenBlock, elseBlock ? elseBlock : endBlock);
    builder_.SetInsertPoint(thenBlock);
    emitStatement(*statement.thenBranch);
    if (!currentBlockTerminated())
      builder_.CreateBr(endBlock);
    if (elseBlock) {
      builder_.SetInsertPoint(elseBlock);
      emitStatement(*statement.elseBranch);
      if (!currentBlockTerminated())
        builder_.CreateBr(endBlock);
    }
    builder_.SetInsertPoint(endBlock);
  }

  void emitFor(const mioyi::Statement &statement) {
    auto *condition =
        llvm::BasicBlock::Create(context_, "for.cond", currentFunction_);
    auto *body =
        llvm::BasicBlock::Create(context_, "for.body", currentFunction_);
    auto *exit =
        llvm::BasicBlock::Create(context_, "for.end", currentFunction_);
    builder_.CreateBr(condition);
    builder_.SetInsertPoint(condition);
    if (statement.expression)
      builder_.CreateCondBr(asCondition(emitExpression(*statement.expression)),
                            body, exit);
    else
      builder_.CreateBr(body);
    loops_.push_back({condition, exit});
    builder_.SetInsertPoint(body);
    emitStatement(*statement.thenBranch);
    if (!currentBlockTerminated())
      builder_.CreateBr(condition);
    loops_.pop_back();
    builder_.SetInsertPoint(exit);
  }

  void emitForEach(const mioyi::Statement &statement) {
    Value aggregate = emitExpression(*statement.expression);
    if (aggregate.type.kind != Type::Kind::Array)
      fail(statement.expression->location,
           "for-in currently requires an array");
    auto *indexSlot = createAlloca(builder_.getInt64Ty(), "for.index");
    builder_.CreateStore(builder_.getInt64(0), indexSlot);
    auto *condition =
        llvm::BasicBlock::Create(context_, "for.cond", currentFunction_);
    auto *body =
        llvm::BasicBlock::Create(context_, "for.body", currentFunction_);
    auto *step =
        llvm::BasicBlock::Create(context_, "for.step", currentFunction_);
    auto *exit =
        llvm::BasicBlock::Create(context_, "for.end", currentFunction_);
    builder_.CreateBr(condition);
    builder_.SetInsertPoint(condition);
    auto *index = builder_.CreateLoad(builder_.getInt64Ty(), indexSlot);
    builder_.CreateCondBr(
        builder_.CreateICmpULT(index, builder_.getInt64(aggregate.type.length)),
        body, exit);
    loops_.push_back({step, exit});
    builder_.SetInsertPoint(body);
    scopes_.emplace_back();
    // ExtractValue needs a constant index. Store the aggregate and use a GEP.
    auto *aggregateSlot = createAlloca(llvmType(aggregate.type), "for.array");
    builder_.CreateStore(aggregate.value, aggregateSlot);
    auto *elementAddress = builder_.CreateInBoundsGEP(
        llvmType(aggregate.type), aggregateSlot, {builder_.getInt64(0), index},
        "for.element");
    bind(statement.location, statement.text,
         {elementAddress, *aggregate.type.element, false});
    emitStatement(*statement.thenBranch);
    scopes_.pop_back();
    if (!currentBlockTerminated())
      builder_.CreateBr(step);
    builder_.SetInsertPoint(step);
    auto *oldIndex = builder_.CreateLoad(builder_.getInt64Ty(), indexSlot);
    builder_.CreateStore(builder_.CreateAdd(oldIndex, builder_.getInt64(1)),
                         indexSlot);
    builder_.CreateBr(condition);
    loops_.pop_back();
    builder_.SetInsertPoint(exit);
  }

  llvm::LLVMContext &context_;
  llvm::Module &module_;
  llvm::IRBuilder<> builder_;
  llvm::Function *printf_ = nullptr;
  llvm::Function *putchar_ = nullptr;
  llvm::Function *currentFunction_ = nullptr;
  Type currentResult_;
  std::vector<std::unordered_map<std::string, Symbol>> scopes_;
  std::unordered_map<std::string, FunctionInfo> functions_;
  std::vector<LoopTargets> loops_;
};

} // namespace

export namespace mioyi::transformer {

class Options {
public:
  std::string input;
  std::string output;
  std::string format;
};

using Result = std::expected<void, std::string>;

std::unique_ptr<llvm::Module> generateIR(const mioyi::Ast &ast,
                                         llvm::LLVMContext &context,
                                         std::string_view moduleName) {
  auto module =
      std::make_unique<llvm::Module>(std::string(moduleName), context);
  try {
    MioyiCodegen codegen(context, *module);
    codegen.generate(ast);
  } catch (const std::exception &error) {
    llvm::errs() << "error: " << error.what() << '\n';
    return nullptr;
  }
  if (llvm::verifyModule(*module, &llvm::errs()))
    return nullptr;
  return module;
}

Result transform(Options options) {
  std::ifstream inputFile;
  std::istream *input = &std::cin;
  if (!options.input.empty()) {
    inputFile.open(options.input);
    if (!inputFile)
      return std::unexpected("cannot open input file '" + options.input + "'");
    input = &inputFile;
  }
  std::ostringstream buffer;
  buffer << input->rdbuf();
  auto ast = mioyi::parser::deserializeAst(buffer.str(), options.format);
  if (!ast)
    return std::unexpected("failed to deserialize AST");

  llvm::LLVMContext context;
  auto module = generateIR(*ast, context,
                           options.input.empty() ? "stdin" : options.input);
  if (!module)
    return std::unexpected("failed to transform AST into LLVM IR");

  if (options.output == "-") {
    module->print(llvm::outs(), nullptr);
    return {};
  }
  std::error_code error;
  llvm::raw_fd_ostream output(options.output, error, llvm::sys::fs::OF_None);
  if (error)
    return std::unexpected("cannot open output file '" + options.output +
                           "': " + error.message());
  module->print(output, nullptr);
  return {};
}

} // namespace mioyi::transformer
