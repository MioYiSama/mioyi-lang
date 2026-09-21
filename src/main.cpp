#include <any>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <antlr4-runtime.h>
#include <ExpressionLexer.h>
#include <ExpressionParser.h>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

class SyntaxErrorListener final : public antlr4::BaseErrorListener {
public:
  void syntaxError(antlr4::Recognizer *, antlr4::Token *, std::size_t line,
                   std::size_t column, const std::string &message,
                   std::exception_ptr) override {
    failed = true;
    llvm::errs() << "error: " << line << ':' << column + 1 << ": " << message
                 << '\n';
  }
  bool failed = false;
};

class CodegenError final : public std::runtime_error {
public:
  CodegenError(antlr4::ParserRuleContext *ctx, const std::string &message)
      : std::runtime_error(std::to_string(ctx->getStart()->getLine()) + ":" +
                           std::to_string(ctx->getStart()->getCharPositionInLine() + 1) +
                           ": " + message) {}
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

  void generate(ExpressionParser::ProgramContext *program) {
    auto *unit = program->compUnit();
    // Globals are emitted and every function signature is registered before
    // any body, so calls to functions defined later in the file are valid.
    for (auto *child : unit->children) {
      if (auto *decl = dynamic_cast<ExpressionParser::DeclContext *>(child))
        emitDecl(decl, true);
      else if (auto *func = dynamic_cast<ExpressionParser::FuncDefContext *>(child))
        declareFunction(func);
    }
    for (auto *func : unit->funcDef())
      emitFunction(func);
  }

private:
  llvm::Type *i32() { return builder_.getInt32Ty(); }
  llvm::ConstantInt *zero() { return builder_.getInt32(0); }
  bool currentBlockTerminated() const {
    auto *block = builder_.GetInsertBlock();
    return block && !block->empty() && block->back().isTerminator();
  }

  [[noreturn]] void fail(antlr4::ParserRuleContext *ctx,
                         const std::string &message) {
    throw CodegenError(ctx, message);
  }

  static std::int32_t parseInteger(ExpressionParser::NumberContext *ctx) {
    const std::string text = ctx->getText();
    int base = 10;
    std::string_view digits = text;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
      base = 16;
      digits.remove_prefix(2);
    } else if (text.size() > 1 && text[0] == '0') {
      base = 8;
      digits.remove_prefix(1);
    }
    std::uint32_t value = 0;
    auto [end, error] =
        std::from_chars(digits.data(), digits.data() + digits.size(), value, base);
    if (error != std::errc{} || end != digits.data() + digits.size())
      throw CodegenError(ctx, "integer literal is outside the 32-bit range");
    return static_cast<std::int32_t>(value);
  }

  void declareRuntime() {
    auto declare = [&](const char *name, llvm::Type *result,
                       std::vector<llvm::Type *> parameters) {
      module_.getOrInsertFunction(name,
          llvm::FunctionType::get(result, parameters, false));
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

  Symbol &lookup(ExpressionParser::LValContext *ctx) {
    const std::string name = ctx->IDENT()->getText();
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
      if (auto found = scope->find(name); found != scope->end())
        return found->second;
    }
    fail(ctx, "unknown identifier '" + name + "'");
  }

  void bind(antlr4::ParserRuleContext *ctx, const std::string &name, Symbol value) {
    if (!scopes_.back().emplace(name, std::move(value)).second)
      fail(ctx, "redefinition of '" + name + "'");
  }

  std::int32_t evalConst(ExpressionParser::ConstExpContext *ctx) {
    return evalConst(ctx->addExp());
  }

  std::int32_t evalConst(ExpressionParser::ExpContext *ctx) {
    return evalConst(ctx->addExp());
  }

  std::int32_t evalConst(ExpressionParser::AddExpContext *ctx) {
    auto terms = ctx->mulExp();
    std::int32_t value = evalConst(terms[0]);
    for (std::size_t i = 1; i < terms.size(); ++i) {
      const std::int32_t rhs = evalConst(terms[i]);
      const std::string op = ctx->children[i * 2 - 1]->getText();
      value = op == "+" ? value + rhs : value - rhs;
    }
    return value;
  }

  std::int32_t evalConst(ExpressionParser::MulExpContext *ctx) {
    auto terms = ctx->unaryExp();
    std::int32_t value = evalConst(terms[0]);
    for (std::size_t i = 1; i < terms.size(); ++i) {
      const std::int32_t rhs = evalConst(terms[i]);
      const std::string op = ctx->children[i * 2 - 1]->getText();
      if ((op == "/" || op == "%") && rhs == 0)
        fail(ctx, "division by zero in constant expression");
      if (op == "*") value *= rhs;
      else if (op == "/") value /= rhs;
      else value %= rhs;
    }
    return value;
  }

  std::int32_t evalConst(ExpressionParser::UnaryExpContext *ctx) {
    if (ctx->primaryExp()) return evalConst(ctx->primaryExp());
    if (ctx->unaryOp()) {
      const auto value = evalConst(ctx->unaryExp());
      const std::string op = ctx->unaryOp()->getText();
      if (op == "-") return -value;
      if (op == "!") return value == 0;
      return value;
    }
    fail(ctx, "function call is not a constant expression");
  }

  std::int32_t evalConst(ExpressionParser::PrimaryExpContext *ctx) {
    if (ctx->number()) return parseInteger(ctx->number());
    if (ctx->exp()) return evalConst(ctx->exp());
    auto *lv = ctx->lVal();
    Symbol &symbol = lookup(lv);
    if (!symbol.constant || !symbol.constantValue)
      fail(lv, "expression is not constant");
    llvm::Constant *value = symbol.constantValue;
    for (auto *index : lv->exp()) {
      const auto i = evalConst(index);
      if (i < 0 || !value->getType()->isArrayTy() ||
          static_cast<std::uint64_t>(i) >=
              llvm::cast<llvm::ArrayType>(value->getType())->getNumElements())
        fail(lv, "invalid constant array subscript");
      value = value->getAggregateElement(static_cast<unsigned>(i));
      if (!value) fail(lv, "invalid constant array subscript");
    }
    auto *integer = llvm::dyn_cast<llvm::ConstantInt>(value);
    if (!integer) fail(lv, "array value is not a scalar constant");
    return static_cast<std::int32_t>(integer->getSExtValue());
  }

  std::vector<std::int64_t>
  dimensions(const std::vector<ExpressionParser::ConstExpContext *> &expressions,
             antlr4::ParserRuleContext *owner) {
    std::vector<std::int64_t> result;
    for (auto *expression : expressions) {
      const auto size = evalConst(expression);
      if (size <= 0) fail(owner, "array dimension must be positive");
      result.push_back(size);
    }
    return result;
  }

  llvm::Type *arrayType(const std::vector<std::int64_t> &dims) {
    llvm::Type *type = i32();
    for (auto it = dims.rbegin(); it != dims.rend(); ++it)
      type = llvm::ArrayType::get(type, *it);
    return type;
  }

  std::size_t scalarCount(llvm::Type *type) {
    if (type->isIntegerTy()) return 1;
    auto *array = llvm::cast<llvm::ArrayType>(type);
    return array->getNumElements() * scalarCount(array->getElementType());
  }

  template <class InitContext>
  bool isScalarInitializer(InitContext *init) {
    if constexpr (std::is_same_v<InitContext, ExpressionParser::InitValContext>)
      return init->exp() != nullptr;
    else
      return init->constExp() != nullptr;
  }

  template <class InitContext>
  std::vector<InitContext *> initializerChildren(InitContext *init) {
    if constexpr (std::is_same_v<InitContext, ExpressionParser::InitValContext>)
      return init->initVal();
    else
      return init->constInitVal();
  }

  // Translate C/SysY brace elision into scalar slots. A nested brace starts at
  // the next subaggregate boundary; unbraced values continue linearly.
  template <class InitContext>
  void fillInitializerSlots(llvm::Type *type, InitContext *init,
                            std::vector<InitContext *> &slots, std::size_t base) {
    if (!init || base >= slots.size()) return;
    if (type->isIntegerTy()) {
      if (isScalarInitializer(init)) slots[base] = init;
      else {
        auto children = initializerChildren(init);
        if (!children.empty()) fillInitializerSlots(type, children.front(), slots, base);
      }
      return;
    }
    if (isScalarInitializer(init)) {
      slots[base] = init;
      return;
    }
    auto *array = llvm::cast<llvm::ArrayType>(type);
    llvm::Type *elementType = array->getElementType();
    const std::size_t elementWidth = scalarCount(elementType);
    const std::size_t capacity = scalarCount(type);
    std::size_t cursor = 0;
    for (auto *child : initializerChildren(init)) {
      if (cursor >= capacity) break;
      if (isScalarInitializer(child)) {
        slots[base + cursor++] = child;
      } else {
        cursor = ((cursor + elementWidth - 1) / elementWidth) * elementWidth;
        if (cursor >= capacity) break;
        fillInitializerSlots(elementType, child, slots, base + cursor);
        cursor += elementWidth;
      }
    }
  }

  template <class InitContext>
  std::int32_t initializerValue(InitContext *init) {
    if constexpr (std::is_same_v<InitContext, ExpressionParser::InitValContext>)
      return evalConst(init->exp());
    else
      return evalConst(init->constExp());
  }

  template <class InitContext>
  llvm::Constant *buildConstant(llvm::Type *type,
                                const std::vector<InitContext *> &slots,
                                std::size_t &cursor) {
    if (type->isIntegerTy()) {
      auto *init = slots[cursor++];
      return llvm::ConstantInt::get(type, init ? initializerValue(init) : 0, true);
    }
    auto *array = llvm::cast<llvm::ArrayType>(type);
    std::vector<llvm::Constant *> values;
    values.reserve(array->getNumElements());
    for (std::uint64_t i = 0; i < array->getNumElements(); ++i)
      values.push_back(buildConstant(array->getElementType(), slots, cursor));
    return llvm::ConstantArray::get(array, values);
  }

  template <class InitContext>
  llvm::Constant *constantInitializer(llvm::Type *type, InitContext *init) {
    std::vector<InitContext *> slots(scalarCount(type), nullptr);
    fillInitializerSlots(type, init, slots, 0);
    std::size_t cursor = 0;
    return buildConstant(type, slots, cursor);
  }

  llvm::AllocaInst *createAlloca(llvm::Type *type, const std::string &name) {
    auto &entry = currentFunction_->getEntryBlock();
    llvm::IRBuilder<> allocations(&entry, entry.begin());
    return allocations.CreateAlloca(type, nullptr, name);
  }

  template <class InitContext>
  void emitInitializerSlots(llvm::Type *type, llvm::Value *address,
                            const std::vector<InitContext *> &slots,
                            std::size_t &cursor) {
    if (type->isIntegerTy()) {
      llvm::Value *value = zero();
      if (auto *init = slots[cursor]) {
        if constexpr (std::is_same_v<InitContext, ExpressionParser::InitValContext>) {
          value = emitExp(init->exp());
        } else {
          value = builder_.getInt32(evalConst(init->constExp()));
        }
      }
      ++cursor;
      builder_.CreateStore(value, address);
      return;
    }
    auto *array = llvm::cast<llvm::ArrayType>(type);
    for (std::uint64_t i = 0; i < array->getNumElements(); ++i) {
      auto *elementAddress = builder_.CreateInBoundsGEP(
          type, address, {zero(), builder_.getInt32(i)}, "init.elem");
      emitInitializerSlots(array->getElementType(), elementAddress, slots, cursor);
    }
  }

  template <class InitContext>
  void emitLocalInitializer(llvm::Type *type, llvm::Value *address,
                            InitContext *init) {
    std::vector<InitContext *> slots(scalarCount(type), nullptr);
    fillInitializerSlots(type, init, slots, 0);
    std::size_t cursor = 0;
    emitInitializerSlots(type, address, slots, cursor);
  }

  void emitDecl(ExpressionParser::DeclContext *ctx, bool global) {
    if (ctx->constDecl()) {
      for (auto *definition : ctx->constDecl()->constDef()) {
        const std::string name = definition->IDENT()->getText();
        auto dims = dimensions(definition->constExp(), definition);
        llvm::Type *type = arrayType(dims);
        llvm::Constant *initial = constantInitializer(type, definition->constInitVal());
        if (global) {
          auto *variable = new llvm::GlobalVariable(
              module_, type, true, llvm::GlobalValue::InternalLinkage, initial, name);
          bind(definition, name, {variable, type, true, false, initial});
        } else {
          auto *slot = createAlloca(type, name);
          emitLocalInitializer(type, slot, definition->constInitVal());
          bind(definition, name, {slot, type, true, false, initial});
        }
      }
      return;
    }
    for (auto *definition : ctx->varDecl()->varDef()) {
      const std::string name = definition->IDENT()->getText();
      auto dims = dimensions(definition->constExp(), definition);
      llvm::Type *type = arrayType(dims);
      if (global) {
        llvm::Constant *initial = definition->initVal()
            ? constantInitializer(type, definition->initVal())
            : llvm::Constant::getNullValue(type);
        auto *variable = new llvm::GlobalVariable(
            module_, type, false, llvm::GlobalValue::ExternalLinkage, initial, name);
        bind(definition, name, {variable, type, false, false, nullptr});
      } else {
        auto *slot = createAlloca(type, name);
        if (definition->initVal())
          emitLocalInitializer(type, slot, definition->initVal());
        bind(definition, name, {slot, type, false, false, nullptr});
      }
    }
  }

  void declareFunction(ExpressionParser::FuncDefContext *ctx) {
    const std::string name = ctx->IDENT()->getText();
    if (module_.getFunction(name)) fail(ctx, "redefinition of function '" + name + "'");
    std::vector<llvm::Type *> parameters;
    if (ctx->funcFParams()) {
      for (auto *parameter : ctx->funcFParams()->funcFParam())
        parameters.push_back(parameter->LBRACK().empty() ? i32() : builder_.getPtrTy());
    }
    llvm::Type *result = ctx->funcType()->VOID() ? builder_.getVoidTy() : i32();
    llvm::Function::Create(llvm::FunctionType::get(result, parameters, false),
                           llvm::Function::ExternalLinkage, name, module_);
  }

  void emitFunction(ExpressionParser::FuncDefContext *ctx) {
    currentFunction_ = module_.getFunction(ctx->IDENT()->getText());
    auto *entry = llvm::BasicBlock::Create(context_, "entry", currentFunction_);
    builder_.SetInsertPoint(entry);
    scopes_.emplace_back();

    std::size_t index = 0;
    if (ctx->funcFParams()) {
      auto parameters = ctx->funcFParams()->funcFParam();
      for (auto &argument : currentFunction_->args()) {
        auto *parameter = parameters[index++];
        const std::string name = parameter->IDENT()->getText();
        argument.setName(name);
        if (parameter->LBRACK().empty()) {
          auto *slot = createAlloca(i32(), name + ".addr");
          builder_.CreateStore(&argument, slot);
          bind(parameter, name, {slot, i32(), false, false, nullptr});
        } else {
          auto dims = dimensions(parameter->constExp(), parameter);
          bind(parameter, name,
               {&argument, arrayType(dims), false, true, nullptr});
        }
      }
    }
    emitBlock(ctx->block(), false);
    if (!currentBlockTerminated()) {
      if (currentFunction_->getReturnType()->isVoidTy())
        builder_.CreateRetVoid();
      else
        builder_.CreateRet(zero());
    }
    scopes_.pop_back();
    currentFunction_ = nullptr;
  }

  void emitBlock(ExpressionParser::BlockContext *ctx, bool createScope = true) {
    if (createScope) scopes_.emplace_back();
    for (auto *item : ctx->blockItem()) {
      if (currentBlockTerminated()) break;
      if (item->decl()) emitDecl(item->decl(), false);
      else emitStmt(item->stmt());
    }
    if (createScope) scopes_.pop_back();
  }

  llvm::Value *asCondition(llvm::Value *value) {
    if (value->getType()->isIntegerTy(1)) return value;
    return builder_.CreateICmpNE(value, zero(), "tobool");
  }

  void emitStmt(ExpressionParser::StmtContext *ctx) {
    if (auto *statement = dynamic_cast<ExpressionParser::AssignStmtContext *>(ctx)) {
      Symbol &symbol = lookup(statement->lVal());
      if (symbol.constant) fail(statement, "cannot assign to a constant");
      auto [address, type] = emitAddress(statement->lVal());
      if (!type->isIntegerTy()) fail(statement, "cannot assign to an array");
      builder_.CreateStore(emitExp(statement->exp()), address);
    } else if (auto *statement = dynamic_cast<ExpressionParser::ExpressionStmtContext *>(ctx)) {
      if (statement->exp()) (void)emitExp(statement->exp());
    } else if (auto *statement = dynamic_cast<ExpressionParser::BlockStmtContext *>(ctx)) {
      emitBlock(statement->block());
    } else if (auto *statement = dynamic_cast<ExpressionParser::IfStmtContext *>(ctx)) {
      auto *thenBlock = llvm::BasicBlock::Create(context_, "if.then", currentFunction_);
      auto *elseBlock = statement->ELSE()
          ? llvm::BasicBlock::Create(context_, "if.else", currentFunction_) : nullptr;
      auto *endBlock = llvm::BasicBlock::Create(context_, "if.end", currentFunction_);
      builder_.CreateCondBr(asCondition(emitLOr(statement->cond()->lOrExp())),
                            thenBlock, elseBlock ? elseBlock : endBlock);
      builder_.SetInsertPoint(thenBlock);
      emitStmt(statement->stmt(0));
      if (!currentBlockTerminated()) builder_.CreateBr(endBlock);
      if (elseBlock) {
        builder_.SetInsertPoint(elseBlock);
        emitStmt(statement->stmt(1));
        if (!currentBlockTerminated()) builder_.CreateBr(endBlock);
      }
      builder_.SetInsertPoint(endBlock);
    } else if (auto *statement = dynamic_cast<ExpressionParser::WhileStmtContext *>(ctx)) {
      auto *condition = llvm::BasicBlock::Create(context_, "while.cond", currentFunction_);
      auto *body = llvm::BasicBlock::Create(context_, "while.body", currentFunction_);
      auto *exit = llvm::BasicBlock::Create(context_, "while.end", currentFunction_);
      builder_.CreateBr(condition);
      builder_.SetInsertPoint(condition);
      builder_.CreateCondBr(asCondition(emitLOr(statement->cond()->lOrExp())), body, exit);
      loops_.push_back({condition, exit});
      builder_.SetInsertPoint(body);
      emitStmt(statement->stmt());
      if (!currentBlockTerminated()) builder_.CreateBr(condition);
      loops_.pop_back();
      builder_.SetInsertPoint(exit);
    } else if (dynamic_cast<ExpressionParser::BreakStmtContext *>(ctx)) {
      if (loops_.empty()) fail(ctx, "'break' is not inside a loop");
      builder_.CreateBr(loops_.back().exit);
    } else if (dynamic_cast<ExpressionParser::ContinueStmtContext *>(ctx)) {
      if (loops_.empty()) fail(ctx, "'continue' is not inside a loop");
      builder_.CreateBr(loops_.back().condition);
    } else if (auto *statement = dynamic_cast<ExpressionParser::ReturnStmtContext *>(ctx)) {
      if (currentFunction_->getReturnType()->isVoidTy()) {
        if (statement->exp()) fail(statement, "void function cannot return a value");
        builder_.CreateRetVoid();
      } else {
        if (!statement->exp()) fail(statement, "non-void function must return a value");
        builder_.CreateRet(emitExp(statement->exp()));
      }
    }
  }

  std::pair<llvm::Value *, llvm::Type *>
  emitAddress(ExpressionParser::LValContext *ctx) {
    Symbol &symbol = lookup(ctx);
    llvm::Value *address = symbol.address;
    llvm::Type *type = symbol.pointee;
    auto indices = ctx->exp();
    std::size_t begin = 0;
    if (symbol.arrayParameter && !indices.empty()) {
      address = builder_.CreateInBoundsGEP(type, address, emitExp(indices[0]), "param.idx");
      begin = 1;
    }
    for (std::size_t i = begin; i < indices.size(); ++i) {
      auto *array = llvm::dyn_cast<llvm::ArrayType>(type);
      if (!array) fail(ctx, "too many array subscripts");
      address = builder_.CreateInBoundsGEP(
          type, address, {zero(), emitExp(indices[i])}, "array.idx");
      type = array->getElementType();
    }
    return {address, type};
  }

  llvm::Value *emitLVal(ExpressionParser::LValContext *ctx) {
    auto [address, type] = emitAddress(ctx);
    if (auto *array = llvm::dyn_cast<llvm::ArrayType>(type))
      return builder_.CreateInBoundsGEP(type, address, {zero(), zero()}, "array.decay");
    if (lookup(ctx).arrayParameter && ctx->exp().empty()) return address;
    return builder_.CreateLoad(type, address, ctx->IDENT()->getText() + ".value");
  }

  llvm::Value *emitExp(ExpressionParser::ExpContext *ctx) {
    return emitAdd(ctx->addExp());
  }

  llvm::Value *emitPrimary(ExpressionParser::PrimaryExpContext *ctx) {
    if (ctx->number()) return builder_.getInt32(parseInteger(ctx->number()));
    if (ctx->exp()) return emitExp(ctx->exp());
    return emitLVal(ctx->lVal());
  }

  llvm::Value *emitUnary(ExpressionParser::UnaryExpContext *ctx) {
    if (ctx->primaryExp()) return emitPrimary(ctx->primaryExp());
    if (ctx->unaryOp()) {
      llvm::Value *value = emitUnary(ctx->unaryExp());
      const std::string op = ctx->unaryOp()->getText();
      if (op == "-") return builder_.CreateNeg(value, "neg");
      if (op == "!") return builder_.CreateZExt(
          builder_.CreateICmpEQ(value, zero(), "not"), i32(), "not.i32");
      return value;
    }
    const std::string name = ctx->IDENT()->getText();
    const bool timed = name == "starttime" || name == "stoptime";
    const std::string calleeName = timed ? "_sysy_" + name : name;
    llvm::Function *function = module_.getFunction(calleeName);
    if (!function) fail(ctx, "unknown function '" + name + "'");
    std::vector<llvm::Value *> arguments;
    if (ctx->funcRParams())
      for (auto *argument : ctx->funcRParams()->exp()) arguments.push_back(emitExp(argument));
    if (timed && arguments.empty())
      arguments.push_back(builder_.getInt32(ctx->getStart()->getLine()));
    if (arguments.size() != function->arg_size())
      fail(ctx, "wrong number of arguments in call to '" + name + "'");
    std::size_t i = 0;
    for (auto *argument : arguments) {
      if (argument->getType() != function->getFunctionType()->getParamType(i++))
        fail(ctx, "argument type mismatch in call to '" + name + "'");
    }
    auto *call = builder_.CreateCall(function, arguments,
        function->getReturnType()->isVoidTy() ? "" : name + ".call");
    return call;
  }

  llvm::Value *emitMul(ExpressionParser::MulExpContext *ctx) {
    auto operands = ctx->unaryExp();
    llvm::Value *value = emitUnary(operands[0]);
    for (std::size_t i = 1; i < operands.size(); ++i) {
      llvm::Value *rhs = emitUnary(operands[i]);
      const std::string op = ctx->children[i * 2 - 1]->getText();
      if (op == "*") value = builder_.CreateMul(value, rhs, "mul");
      else if (op == "/") value = builder_.CreateSDiv(value, rhs, "div");
      else value = builder_.CreateSRem(value, rhs, "rem");
    }
    return value;
  }

  llvm::Value *emitAdd(ExpressionParser::AddExpContext *ctx) {
    auto operands = ctx->mulExp();
    llvm::Value *value = emitMul(operands[0]);
    for (std::size_t i = 1; i < operands.size(); ++i) {
      llvm::Value *rhs = emitMul(operands[i]);
      const std::string op = ctx->children[i * 2 - 1]->getText();
      value = op == "+" ? builder_.CreateAdd(value, rhs, "add")
                         : builder_.CreateSub(value, rhs, "sub");
    }
    return value;
  }

  llvm::Value *emitRel(ExpressionParser::RelExpContext *ctx) {
    auto operands = ctx->addExp();
    llvm::Value *value = emitAdd(operands[0]);
    for (std::size_t i = 1; i < operands.size(); ++i) {
      llvm::Value *rhs = emitAdd(operands[i]);
      const std::string op = ctx->children[i * 2 - 1]->getText();
      llvm::CmpInst::Predicate predicate = llvm::CmpInst::ICMP_SLT;
      if (op == ">") predicate = llvm::CmpInst::ICMP_SGT;
      else if (op == "<=") predicate = llvm::CmpInst::ICMP_SLE;
      else if (op == ">=") predicate = llvm::CmpInst::ICMP_SGE;
      value = builder_.CreateZExt(builder_.CreateICmp(predicate, value, rhs, "cmp"),
                                  i32(), "cmp.i32");
    }
    return value;
  }

  llvm::Value *emitEq(ExpressionParser::EqExpContext *ctx) {
    auto operands = ctx->relExp();
    llvm::Value *value = emitRel(operands[0]);
    for (std::size_t i = 1; i < operands.size(); ++i) {
      llvm::Value *rhs = emitRel(operands[i]);
      const bool equal = ctx->children[i * 2 - 1]->getText() == "==";
      value = builder_.CreateZExt(builder_.CreateICmp(
          equal ? llvm::CmpInst::ICMP_EQ : llvm::CmpInst::ICMP_NE,
          value, rhs, "eq"), i32(), "eq.i32");
    }
    return value;
  }

  template <class Context, class Child, class Emit>
  llvm::Value *emitShortCircuit(Context *ctx, const std::vector<Child *> &children,
                                bool isAnd, Emit emitChild) {
    llvm::Value *value = emitChild(children[0]);
    for (std::size_t i = 1; i < children.size(); ++i) {
      auto *origin = builder_.GetInsertBlock();
      auto *rhsBlock = llvm::BasicBlock::Create(context_, isAnd ? "and.rhs" : "or.rhs",
                                                currentFunction_);
      auto *merge = llvm::BasicBlock::Create(context_, isAnd ? "and.end" : "or.end",
                                             currentFunction_);
      llvm::Value *condition = asCondition(value);
      builder_.CreateCondBr(condition, isAnd ? rhsBlock : merge,
                            isAnd ? merge : rhsBlock);
      builder_.SetInsertPoint(rhsBlock);
      llvm::Value *rhs = asCondition(emitChild(children[i]));
      auto *rhsEnd = builder_.GetInsertBlock();
      builder_.CreateBr(merge);
      builder_.SetInsertPoint(merge);
      auto *phi = builder_.CreatePHI(builder_.getInt1Ty(), 2, isAnd ? "and" : "or");
      phi->addIncoming(builder_.getInt1(!isAnd), origin);
      phi->addIncoming(rhs, rhsEnd);
      value = builder_.CreateZExt(phi, i32(), "logic.i32");
    }
    return value;
  }

  llvm::Value *emitLAnd(ExpressionParser::LAndExpContext *ctx) {
    auto children = ctx->eqExp();
    return emitShortCircuit(ctx, children, true,
                            [&](auto *child) { return emitEq(child); });
  }

  llvm::Value *emitLOr(ExpressionParser::LOrExpContext *ctx) {
    auto children = ctx->lAndExp();
    return emitShortCircuit(ctx, children, false,
                            [&](auto *child) { return emitLAnd(child); });
  }

  llvm::LLVMContext &context_;
  llvm::Module &module_;
  llvm::IRBuilder<> builder_;
  llvm::Function *currentFunction_ = nullptr;
  std::vector<std::unordered_map<std::string, Symbol>> scopes_;
  std::vector<LoopTargets> loops_;
};

int main(int argc, char **argv) {
  if (argc > 3) {
    llvm::errs() << "usage: " << argv[0] << " [input.sy] [output.ll]\n";
    return 1;
  }

  std::ifstream inputFile;
  std::istream *source = &std::cin;
  if (argc >= 2) {
    inputFile.open(argv[1]);
    if (!inputFile) {
      llvm::errs() << "error: cannot open input file '" << argv[1] << "'\n";
      return 1;
    }
    source = &inputFile;
  }
  std::ostringstream buffer;
  buffer << source->rdbuf();

  antlr4::ANTLRInputStream input(buffer.str());
  ExpressionLexer lexer(&input);
  antlr4::CommonTokenStream tokens(&lexer);
  ExpressionParser parser(&tokens);
  SyntaxErrorListener errors;
  lexer.removeErrorListeners();
  lexer.addErrorListener(&errors);
  parser.removeErrorListeners();
  parser.addErrorListener(&errors);
  auto *tree = parser.program();
  if (errors.failed) return 1;

  llvm::LLVMContext context;
  llvm::Module module(argc >= 2 ? argv[1] : "stdin", context);
  try {
    SysYCodegen codegen(context, module);
    codegen.generate(tree);
  } catch (const std::exception &error) {
    llvm::errs() << "error: " << error.what() << '\n';
    return 1;
  }
  if (llvm::verifyModule(module, &llvm::errs())) return 1;

  if (argc == 3) {
    std::error_code error;
    llvm::raw_fd_ostream output(argv[2], error);
    if (error) {
      llvm::errs() << "error: cannot open output file '" << argv[2]
                   << "': " << error.message() << '\n';
      return 1;
    }
    module.print(output, nullptr);
  } else {
    module.print(llvm::outs(), nullptr);
  }
  return 0;
}
