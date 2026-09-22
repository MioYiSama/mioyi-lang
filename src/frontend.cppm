module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
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
#include <llvm/Support/raw_ostream.h>

export module mioyi.compiler.frontend;

import mioyi.parser;

namespace {

class CodegenError final : public std::runtime_error {
public:
  CodegenError(mioyi::SourceLocation location, const std::string &message)
      : std::runtime_error(std::to_string(location.line) + ":" +
                           std::to_string(location.column) + ": " + message) {}
};

class SysYCodegen {
  struct Symbol {
    llvm::Value *address = nullptr;
    llvm::Type *pointee = nullptr;
    bool constant = false;
    bool arrayParameter = false;
    llvm::Constant *constantValue = nullptr;
  };

  struct LoopTargets {
    llvm::BasicBlock *condition;
    llvm::BasicBlock *exit;
  };

public:
  SysYCodegen(llvm::LLVMContext &context, llvm::Module &module)
      : context_(context), module_(module), builder_(context) {
    scopes_.emplace_back();
    declareRuntime();
  }

  void generate(const mioyi::Ast &ast) {
    // Globals are emitted and every function signature is registered before
    // any body, so calls to functions defined later in the file are valid.
    for (const auto &item : ast.items) {
      if (item.declaration) emitDeclaration(*item.declaration, true);
      else declareFunction(*item.function);
    }
    for (const auto &item : ast.items)
      if (item.function) emitFunction(*item.function);
  }

private:
  llvm::Type *i32() { return builder_.getInt32Ty(); }
  llvm::ConstantInt *zero() { return builder_.getInt32(0); }

  bool currentBlockTerminated() const {
    auto *block = builder_.GetInsertBlock();
    return block && !block->empty() && block->back().isTerminator();
  }

  [[noreturn]] static void fail(mioyi::SourceLocation location,
                                const std::string &message) {
    throw CodegenError(location, message);
  }

  void declareRuntime() {
    auto declare = [&](const char *name, llvm::Type *result,
                       std::vector<llvm::Type *> parameters) {
      module_.getOrInsertFunction(
          name, llvm::FunctionType::get(result, parameters, false));
    };
    auto *ptr = builder_.getPtrTy();
    declare("getint", i32(), {});
    declare("getch", i32(), {});
    declare("getarray", i32(), {ptr});
    declare("putint", builder_.getVoidTy(), {i32()});
    declare("putch", builder_.getVoidTy(), {i32()});
    declare("putarray", builder_.getVoidTy(), {i32(), ptr});
    declare("starttime", builder_.getVoidTy(), {});
    declare("stoptime", builder_.getVoidTy(), {});
    declare("_sysy_starttime", builder_.getVoidTy(), {i32()});
    declare("_sysy_stoptime", builder_.getVoidTy(), {i32()});
  }

  Symbol &lookup(const mioyi::Expression &expression) {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
      if (auto found = scope->find(expression.text); found != scope->end())
        return found->second;
    }
    fail(expression.location, "unknown identifier '" + expression.text + "'");
  }

  void bind(mioyi::SourceLocation location, const std::string &name,
            Symbol value) {
    if (!scopes_.back().emplace(name, std::move(value)).second)
      fail(location, "redefinition of '" + name + "'");
  }

  std::int32_t evalConst(const mioyi::Expression &expression) {
    using Kind = mioyi::Expression::Kind;
    if (expression.kind == Kind::Integer) return expression.integer;
    if (expression.kind == Kind::LValue) {
      Symbol &symbol = lookup(expression);
      if (!symbol.constant || !symbol.constantValue)
        fail(expression.location, "expression is not constant");
      llvm::Constant *value = symbol.constantValue;
      for (const auto &index : expression.operands) {
        const auto position = evalConst(*index);
        if (position < 0 || !value->getType()->isArrayTy() ||
            static_cast<std::uint64_t>(position) >=
                llvm::cast<llvm::ArrayType>(value->getType())->getNumElements())
          fail(expression.location, "invalid constant array subscript");
        value = value->getAggregateElement(static_cast<unsigned>(position));
        if (!value)
          fail(expression.location, "invalid constant array subscript");
      }
      auto *integer = llvm::dyn_cast<llvm::ConstantInt>(value);
      if (!integer)
        fail(expression.location, "array value is not a scalar constant");
      return static_cast<std::int32_t>(integer->getSExtValue());
    }
    if (expression.kind == Kind::Unary) {
      const auto value = evalConst(*expression.operands.front());
      if (expression.text == "-") return -value;
      if (expression.text == "!") return value == 0;
      return value;
    }
    if (expression.kind == Kind::Binary) {
      const auto left = evalConst(*expression.operands[0]);
      const auto right = evalConst(*expression.operands[1]);
      if ((expression.text == "/" || expression.text == "%") && right == 0)
        fail(expression.location, "division by zero in constant expression");
      if (expression.text == "+") return left + right;
      if (expression.text == "-") return left - right;
      if (expression.text == "*") return left * right;
      if (expression.text == "/") return left / right;
      if (expression.text == "%") return left % right;
    }
    fail(expression.location, "expression is not constant");
  }

  std::vector<std::int64_t> dimensions(
      const std::vector<std::unique_ptr<mioyi::Expression>> &expressions,
      mioyi::SourceLocation owner) {
    std::vector<std::int64_t> result;
    for (const auto &expression : expressions) {
      const auto size = evalConst(*expression);
      if (size <= 0) fail(owner, "array dimension must be positive");
      result.push_back(size);
    }
    return result;
  }

  llvm::Type *arrayType(const std::vector<std::int64_t> &dimensions) {
    llvm::Type *type = i32();
    for (auto it = dimensions.rbegin(); it != dimensions.rend(); ++it)
      type = llvm::ArrayType::get(type, *it);
    return type;
  }

  std::size_t scalarCount(llvm::Type *type) {
    if (type->isIntegerTy()) return 1;
    auto *array = llvm::cast<llvm::ArrayType>(type);
    return array->getNumElements() * scalarCount(array->getElementType());
  }

  // Translate C/SysY brace elision into scalar slots. A nested brace starts at
  // the next subaggregate boundary; unbraced values continue linearly.
  void fillInitializerSlots(
      llvm::Type *type, const mioyi::Initializer *initializer,
      std::vector<const mioyi::Initializer *> &slots, std::size_t base) {
    if (!initializer || base >= slots.size()) return;
    if (type->isIntegerTy()) {
      if (initializer->expression) slots[base] = initializer;
      else if (!initializer->elements.empty())
        fillInitializerSlots(type, initializer->elements.front().get(), slots,
                             base);
      return;
    }
    if (initializer->expression) {
      slots[base] = initializer;
      return;
    }
    auto *array = llvm::cast<llvm::ArrayType>(type);
    llvm::Type *elementType = array->getElementType();
    const std::size_t elementWidth = scalarCount(elementType);
    const std::size_t capacity = scalarCount(type);
    std::size_t cursor = 0;
    for (const auto &element : initializer->elements) {
      if (cursor >= capacity) break;
      if (element->expression) {
        slots[base + cursor++] = element.get();
      } else {
        cursor = ((cursor + elementWidth - 1) / elementWidth) * elementWidth;
        if (cursor >= capacity) break;
        fillInitializerSlots(elementType, element.get(), slots, base + cursor);
        cursor += elementWidth;
      }
    }
  }

  llvm::Constant *buildConstant(
      llvm::Type *type,
      const std::vector<const mioyi::Initializer *> &slots,
      std::size_t &cursor) {
    if (type->isIntegerTy()) {
      const auto *initializer = slots[cursor++];
      const auto value = initializer ? evalConst(*initializer->expression) : 0;
      return llvm::ConstantInt::get(type, value, true);
    }
    auto *array = llvm::cast<llvm::ArrayType>(type);
    std::vector<llvm::Constant *> values;
    values.reserve(array->getNumElements());
    for (std::uint64_t index = 0; index < array->getNumElements(); ++index)
      values.push_back(buildConstant(array->getElementType(), slots, cursor));
    return llvm::ConstantArray::get(array, values);
  }

  llvm::Constant *constantInitializer(llvm::Type *type,
                                      const mioyi::Initializer *initializer) {
    std::vector<const mioyi::Initializer *> slots(scalarCount(type), nullptr);
    fillInitializerSlots(type, initializer, slots, 0);
    std::size_t cursor = 0;
    return buildConstant(type, slots, cursor);
  }

  llvm::AllocaInst *createAlloca(llvm::Type *type, const std::string &name) {
    auto &entry = currentFunction_->getEntryBlock();
    llvm::IRBuilder<> allocations(&entry, entry.begin());
    return allocations.CreateAlloca(type, nullptr, name);
  }

  void emitInitializerSlots(
      llvm::Type *type, llvm::Value *address,
      const std::vector<const mioyi::Initializer *> &slots,
      std::size_t &cursor) {
    if (type->isIntegerTy()) {
      llvm::Value *value = zero();
      if (const auto *initializer = slots[cursor])
        value = emitExpression(*initializer->expression);
      ++cursor;
      builder_.CreateStore(value, address);
      return;
    }
    auto *array = llvm::cast<llvm::ArrayType>(type);
    for (std::uint64_t index = 0; index < array->getNumElements(); ++index) {
      auto *elementAddress = builder_.CreateInBoundsGEP(
          type, address, {zero(), builder_.getInt32(index)}, "init.elem");
      emitInitializerSlots(array->getElementType(), elementAddress, slots,
                           cursor);
    }
  }

  void emitLocalInitializer(llvm::Type *type, llvm::Value *address,
                            const mioyi::Initializer *initializer) {
    std::vector<const mioyi::Initializer *> slots(scalarCount(type), nullptr);
    fillInitializerSlots(type, initializer, slots, 0);
    std::size_t cursor = 0;
    emitInitializerSlots(type, address, slots, cursor);
  }

  void emitDeclaration(const mioyi::Declaration &declaration, bool global) {
    for (const auto &definition : declaration.definitions) {
      auto dims = dimensions(definition.dimensions, definition.location);
      llvm::Type *type = arrayType(dims);
      if (declaration.constant) {
        llvm::Constant *initial =
            constantInitializer(type, definition.initializer.get());
        if (global) {
          auto *variable = new llvm::GlobalVariable(
              module_, type, true, llvm::GlobalValue::InternalLinkage, initial,
              definition.name);
          bind(definition.location, definition.name,
               {variable, type, true, false, initial});
        } else {
          auto *slot = createAlloca(type, definition.name);
          emitLocalInitializer(type, slot, definition.initializer.get());
          bind(definition.location, definition.name,
               {slot, type, true, false, initial});
        }
      } else if (global) {
        llvm::Constant *initial = definition.initializer
            ? constantInitializer(type, definition.initializer.get())
            : llvm::Constant::getNullValue(type);
        auto *variable = new llvm::GlobalVariable(
            module_, type, false, llvm::GlobalValue::ExternalLinkage, initial,
            definition.name);
        bind(definition.location, definition.name,
             {variable, type, false, false, nullptr});
      } else {
        auto *slot = createAlloca(type, definition.name);
        if (definition.initializer)
          emitLocalInitializer(type, slot, definition.initializer.get());
        bind(definition.location, definition.name,
             {slot, type, false, false, nullptr});
      }
    }
  }

  void declareFunction(const mioyi::Function &function) {
    if (module_.getFunction(function.name))
      fail(function.location,
           "redefinition of function '" + function.name + "'");
    std::vector<llvm::Type *> parameters;
    for (const auto &parameter : function.parameters)
      parameters.push_back(parameter.array ? builder_.getPtrTy() : i32());
    llvm::Type *result =
        function.returnsValue ? i32() : builder_.getVoidTy();
    llvm::Function::Create(llvm::FunctionType::get(result, parameters, false),
                           llvm::Function::ExternalLinkage, function.name,
                           module_);
  }

  void emitFunction(const mioyi::Function &function) {
    currentFunction_ = module_.getFunction(function.name);
    auto *entry = llvm::BasicBlock::Create(context_, "entry", currentFunction_);
    builder_.SetInsertPoint(entry);
    scopes_.emplace_back();

    std::size_t index = 0;
    for (auto &argument : currentFunction_->args()) {
      const auto &parameter = function.parameters[index++];
      argument.setName(parameter.name);
      if (!parameter.array) {
        auto *slot = createAlloca(i32(), parameter.name + ".addr");
        builder_.CreateStore(&argument, slot);
        bind(parameter.location, parameter.name,
             {slot, i32(), false, false, nullptr});
      } else {
        auto dims = dimensions(parameter.dimensions, parameter.location);
        bind(parameter.location, parameter.name,
             {&argument, arrayType(dims), false, true, nullptr});
      }
    }
    emitBlock(*function.body, false);
    if (!currentBlockTerminated()) {
      if (currentFunction_->getReturnType()->isVoidTy())
        builder_.CreateRetVoid();
      else
        builder_.CreateRet(zero());
    }
    scopes_.pop_back();
    currentFunction_ = nullptr;
  }

  void emitBlock(const mioyi::Statement &statement, bool createScope = true) {
    if (createScope) scopes_.emplace_back();
    for (const auto &item : statement.block) {
      if (currentBlockTerminated()) break;
      emitStatement(*item);
    }
    if (createScope) scopes_.pop_back();
  }

  llvm::Value *asCondition(llvm::Value *value) {
    if (value->getType()->isIntegerTy(1)) return value;
    return builder_.CreateICmpNE(value, zero(), "tobool");
  }

  void emitStatement(const mioyi::Statement &statement) {
    using Kind = mioyi::Statement::Kind;
    switch (statement.kind) {
    case Kind::Declaration:
      emitDeclaration(*statement.declaration, false);
      return;
    case Kind::Assignment: {
      Symbol &symbol = lookup(*statement.target);
      if (symbol.constant)
        fail(statement.location, "cannot assign to a constant");
      auto [address, type] = emitAddress(*statement.target);
      if (!type->isIntegerTy())
        fail(statement.location, "cannot assign to an array");
      builder_.CreateStore(emitExpression(*statement.expression), address);
      return;
    }
    case Kind::Expression:
      if (statement.expression) (void)emitExpression(*statement.expression);
      return;
    case Kind::Block:
      emitBlock(statement);
      return;
    case Kind::If: {
      auto *thenBlock =
          llvm::BasicBlock::Create(context_, "if.then", currentFunction_);
      auto *elseBlock = statement.elseBranch
          ? llvm::BasicBlock::Create(context_, "if.else", currentFunction_)
          : nullptr;
      auto *endBlock =
          llvm::BasicBlock::Create(context_, "if.end", currentFunction_);
      builder_.CreateCondBr(asCondition(emitExpression(*statement.expression)),
                            thenBlock, elseBlock ? elseBlock : endBlock);
      builder_.SetInsertPoint(thenBlock);
      emitStatement(*statement.thenBranch);
      if (!currentBlockTerminated()) builder_.CreateBr(endBlock);
      if (elseBlock) {
        builder_.SetInsertPoint(elseBlock);
        emitStatement(*statement.elseBranch);
        if (!currentBlockTerminated()) builder_.CreateBr(endBlock);
      }
      builder_.SetInsertPoint(endBlock);
      return;
    }
    case Kind::While: {
      auto *condition =
          llvm::BasicBlock::Create(context_, "while.cond", currentFunction_);
      auto *body =
          llvm::BasicBlock::Create(context_, "while.body", currentFunction_);
      auto *exit =
          llvm::BasicBlock::Create(context_, "while.end", currentFunction_);
      builder_.CreateBr(condition);
      builder_.SetInsertPoint(condition);
      builder_.CreateCondBr(asCondition(emitExpression(*statement.expression)),
                            body, exit);
      loops_.push_back({condition, exit});
      builder_.SetInsertPoint(body);
      emitStatement(*statement.thenBranch);
      if (!currentBlockTerminated()) builder_.CreateBr(condition);
      loops_.pop_back();
      builder_.SetInsertPoint(exit);
      return;
    }
    case Kind::Break:
      if (loops_.empty()) fail(statement.location, "'break' is not inside a loop");
      builder_.CreateBr(loops_.back().exit);
      return;
    case Kind::Continue:
      if (loops_.empty())
        fail(statement.location, "'continue' is not inside a loop");
      builder_.CreateBr(loops_.back().condition);
      return;
    case Kind::Return:
      if (currentFunction_->getReturnType()->isVoidTy()) {
        if (statement.expression)
          fail(statement.location, "void function cannot return a value");
        builder_.CreateRetVoid();
      } else {
        if (!statement.expression)
          fail(statement.location, "non-void function must return a value");
        builder_.CreateRet(emitExpression(*statement.expression));
      }
      return;
    }
  }

  std::pair<llvm::Value *, llvm::Type *>
  emitAddress(const mioyi::Expression &expression) {
    Symbol &symbol = lookup(expression);
    llvm::Value *address = symbol.address;
    llvm::Type *type = symbol.pointee;
    std::size_t begin = 0;
    if (symbol.arrayParameter && !expression.operands.empty()) {
      address = builder_.CreateInBoundsGEP(
          type, address, emitExpression(*expression.operands.front()),
          "param.idx");
      begin = 1;
    }
    for (std::size_t index = begin; index < expression.operands.size(); ++index) {
      auto *array = llvm::dyn_cast<llvm::ArrayType>(type);
      if (!array)
        fail(expression.location, "too many array subscripts");
      address = builder_.CreateInBoundsGEP(
          type, address,
          {zero(), emitExpression(*expression.operands[index])}, "array.idx");
      type = array->getElementType();
    }
    return {address, type};
  }

  llvm::Value *emitLValue(const mioyi::Expression &expression) {
    auto [address, type] = emitAddress(expression);
    if (llvm::isa<llvm::ArrayType>(type))
      return builder_.CreateInBoundsGEP(type, address, {zero(), zero()},
                                        "array.decay");
    if (lookup(expression).arrayParameter && expression.operands.empty())
      return address;
    return builder_.CreateLoad(type, address, expression.text + ".value");
  }

  llvm::Value *emitCall(const mioyi::Expression &expression) {
    const bool timed = expression.text == "starttime" ||
                       expression.text == "stoptime";
    const std::string calleeName =
        timed ? "_sysy_" + expression.text : expression.text;
    llvm::Function *function = module_.getFunction(calleeName);
    if (!function)
      fail(expression.location, "unknown function '" + expression.text + "'");
    std::vector<llvm::Value *> arguments;
    for (const auto &argument : expression.operands)
      arguments.push_back(emitExpression(*argument));
    if (timed && arguments.empty())
      arguments.push_back(builder_.getInt32(expression.location.line));
    if (arguments.size() != function->arg_size())
      fail(expression.location,
           "wrong number of arguments in call to '" + expression.text + "'");
    std::size_t index = 0;
    for (auto *argument : arguments) {
      if (argument->getType() !=
          function->getFunctionType()->getParamType(index++))
        fail(expression.location,
             "argument type mismatch in call to '" + expression.text + "'");
    }
    return builder_.CreateCall(
        function, arguments,
        function->getReturnType()->isVoidTy() ? "" : expression.text + ".call");
  }

  llvm::Value *emitLogical(const mioyi::Expression &expression, bool isAnd) {
    llvm::Value *left = emitExpression(*expression.operands[0]);
    auto *origin = builder_.GetInsertBlock();
    auto *rightBlock = llvm::BasicBlock::Create(
        context_, isAnd ? "and.rhs" : "or.rhs", currentFunction_);
    auto *merge = llvm::BasicBlock::Create(
        context_, isAnd ? "and.end" : "or.end", currentFunction_);
    builder_.CreateCondBr(asCondition(left), isAnd ? rightBlock : merge,
                          isAnd ? merge : rightBlock);
    builder_.SetInsertPoint(rightBlock);
    llvm::Value *right = asCondition(emitExpression(*expression.operands[1]));
    auto *rightEnd = builder_.GetInsertBlock();
    builder_.CreateBr(merge);
    builder_.SetInsertPoint(merge);
    auto *phi = builder_.CreatePHI(builder_.getInt1Ty(), 2,
                                   isAnd ? "and" : "or");
    phi->addIncoming(builder_.getInt1(!isAnd), origin);
    phi->addIncoming(right, rightEnd);
    return builder_.CreateZExt(phi, i32(), "logic.i32");
  }

  llvm::Value *emitBinary(const mioyi::Expression &expression) {
    if (expression.text == "&&") return emitLogical(expression, true);
    if (expression.text == "||") return emitLogical(expression, false);
    llvm::Value *left = emitExpression(*expression.operands[0]);
    llvm::Value *right = emitExpression(*expression.operands[1]);
    if (expression.text == "+") return builder_.CreateAdd(left, right, "add");
    if (expression.text == "-") return builder_.CreateSub(left, right, "sub");
    if (expression.text == "*") return builder_.CreateMul(left, right, "mul");
    if (expression.text == "/") return builder_.CreateSDiv(left, right, "div");
    if (expression.text == "%") return builder_.CreateSRem(left, right, "rem");

    llvm::CmpInst::Predicate predicate;
    if (expression.text == "<") predicate = llvm::CmpInst::ICMP_SLT;
    else if (expression.text == ">") predicate = llvm::CmpInst::ICMP_SGT;
    else if (expression.text == "<=") predicate = llvm::CmpInst::ICMP_SLE;
    else if (expression.text == ">=") predicate = llvm::CmpInst::ICMP_SGE;
    else if (expression.text == "==") predicate = llvm::CmpInst::ICMP_EQ;
    else predicate = llvm::CmpInst::ICMP_NE;
    return builder_.CreateZExt(
        builder_.CreateICmp(predicate, left, right, "cmp"), i32(), "cmp.i32");
  }

  llvm::Value *emitExpression(const mioyi::Expression &expression) {
    using Kind = mioyi::Expression::Kind;
    switch (expression.kind) {
    case Kind::Integer:
      return builder_.getInt32(expression.integer);
    case Kind::LValue:
      return emitLValue(expression);
    case Kind::Call:
      return emitCall(expression);
    case Kind::Unary: {
      llvm::Value *value = emitExpression(*expression.operands.front());
      if (expression.text == "-") return builder_.CreateNeg(value, "neg");
      if (expression.text == "!")
        return builder_.CreateZExt(builder_.CreateICmpEQ(value, zero(), "not"),
                                   i32(), "not.i32");
      return value;
    }
    case Kind::Binary:
      return emitBinary(expression);
    }
    fail(expression.location, "unsupported expression");
  }

  llvm::LLVMContext &context_;
  llvm::Module &module_;
  llvm::IRBuilder<> builder_;
  llvm::Function *currentFunction_ = nullptr;
  std::vector<std::unordered_map<std::string, Symbol>> scopes_;
  std::vector<LoopTargets> loops_;
};

} // namespace

export std::unique_ptr<llvm::Module>
generateIR(const mioyi::Ast &ast, llvm::LLVMContext &context,
           std::string_view moduleName) {
  auto module = std::make_unique<llvm::Module>(std::string(moduleName), context);
  try {
    SysYCodegen codegen(context, *module);
    codegen.generate(ast);
  } catch (const std::exception &error) {
    llvm::errs() << "error: " << error.what() << '\n';
    return nullptr;
  }
  if (llvm::verifyModule(*module, &llvm::errs())) return nullptr;
  return module;
}
