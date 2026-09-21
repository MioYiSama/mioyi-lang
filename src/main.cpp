#include <any>
#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <antlr4-runtime.h>

#include <ExpressionBaseVisitor.h>
#include <ExpressionLexer.h>
#include <ExpressionParser.h>

#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

class SyntaxErrorListener final : public antlr4::BaseErrorListener {
public:
  void syntaxError(antlr4::Recognizer *, antlr4::Token *, std::size_t line,
                   std::size_t charPositionInLine, const std::string &message,
                   std::exception_ptr) override {
    hasErrors_ = true;
    llvm::errs() << "error: " << line << ':' << charPositionInLine + 1 << ": "
                 << message << '\n';
  }

  [[nodiscard]] bool hasErrors() const { return hasErrors_; }

private:
  bool hasErrors_ = false;
};

class LLVMIRVisitor final : public ExpressionBaseVisitor {
public:
  LLVMIRVisitor(llvm::LLVMContext &context, llvm::IRBuilder<> &builder)
      : context_(context), builder_(builder) {}

  std::any visitProgram(ExpressionParser::ProgramContext *context) override {
    return visit(context->expression());
  }

  std::any
  visitExpression(ExpressionParser::ExpressionContext *context) override {
    return visit(context->additive());
  }

  std::any visitAdditive(ExpressionParser::AdditiveContext *context) override {
    const auto operands = context->multiplicative();
    llvm::Value *value = visitValue(operands.front());

    for (std::size_t index = 1; index < operands.size(); ++index) {
      const char operation = context->children[index * 2 - 1]->getText().front();
      value = emitBinary(operation, value, visitValue(operands[index]));
    }
    return value;
  }

  std::any visitMultiplicative(
      ExpressionParser::MultiplicativeContext *context) override {
    const auto operands = context->unary();
    llvm::Value *value = visitValue(operands.front());

    for (std::size_t index = 1; index < operands.size(); ++index) {
      const char operation = context->children[index * 2 - 1]->getText().front();
      value = emitBinary(operation, value, visitValue(operands[index]));
    }
    return value;
  }

  std::any visitUnary(ExpressionParser::UnaryContext *context) override {
    if (context->primary() != nullptr) {
      return visit(context->primary());
    }

    llvm::Value *operand = visitValue(context->unary());
    if (context->MINUS() == nullptr) {
      return operand;
    }

    return static_cast<llvm::Value *>(insert(llvm::BinaryOperator::CreateSub(
        llvm::ConstantInt::get(context_, llvm::APInt(64, 0)), operand, "neg")));
  }

  std::any visitPrimary(ExpressionParser::PrimaryContext *context) override {
    if (context->expression() != nullptr) {
      return visit(context->expression());
    }

    const std::string text = context->INTEGER()->getText();
    std::int64_t number = 0;
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), number);
    if (error != std::errc{} || end != text.data() + text.size()) {
      throw std::runtime_error("integer is outside the signed 64-bit range");
    }
    return static_cast<llvm::Value *>(
        llvm::ConstantInt::get(context_, llvm::APInt(64, number, true)));
  }

private:
  llvm::Value *visitValue(antlr4::tree::ParseTree *tree) {
    return std::any_cast<llvm::Value *>(visit(tree));
  }

  llvm::Value *emitBinary(char operation, llvm::Value *left,
                          llvm::Value *right) {
    // Construct instructions directly so the emitted, unoptimized IR keeps
    // every operation from the source expression.
    switch (operation) {
    case '+':
      return insert(llvm::BinaryOperator::CreateAdd(left, right, "add"));
    case '-':
      return insert(llvm::BinaryOperator::CreateSub(left, right, "sub"));
    case '*':
      return insert(llvm::BinaryOperator::CreateMul(left, right, "mul"));
    case '/':
      return insert(llvm::BinaryOperator::CreateSDiv(left, right, "div"));
    default:
      throw std::logic_error("unknown binary operator");
    }
  }

  llvm::Instruction *insert(llvm::Instruction *instruction) {
    return builder_.Insert(instruction);
  }

  llvm::LLVMContext &context_;
  llvm::IRBuilder<> &builder_;
};

int main() {
  std::cerr << "Enter an integer expression: ";
  std::string expression;
  if (!std::getline(std::cin, expression)) {
    llvm::errs() << "error: failed to read the expression\n";
    return 1;
  }

  antlr4::ANTLRInputStream input(expression);
  ExpressionLexer lexer(&input);
  antlr4::CommonTokenStream tokens(&lexer);
  ExpressionParser parser(&tokens);

  SyntaxErrorListener errorListener;
  lexer.removeErrorListeners();
  lexer.addErrorListener(&errorListener);
  parser.removeErrorListeners();
  parser.addErrorListener(&errorListener);

  ExpressionParser::ProgramContext *syntaxTree = parser.program();
  if (errorListener.hasErrors()) {
    return 1;
  }

  llvm::LLVMContext llvmContext;
  auto module =
      std::make_unique<llvm::Module>("arithmetic-expression", llvmContext);
  llvm::IRBuilder<> builder(llvmContext);

  auto *mainType = llvm::FunctionType::get(builder.getInt32Ty(), false);
  auto *mainFunction = llvm::Function::Create(
      mainType, llvm::Function::ExternalLinkage, "main", *module);
  auto *entry = llvm::BasicBlock::Create(llvmContext, "entry", mainFunction);
  builder.SetInsertPoint(entry);

  try {
    LLVMIRVisitor visitor(llvmContext, builder);
    llvm::Value *result =
        std::any_cast<llvm::Value *>(visitor.visitProgram(syntaxTree));

    auto *printfType = llvm::FunctionType::get(
        builder.getInt32Ty(), {builder.getPtrTy()}, true);
    llvm::FunctionCallee printfFunction =
        module->getOrInsertFunction("printf", printfType);
    llvm::Value *format = builder.CreateGlobalString("%lld\n", "format");
    builder.CreateCall(printfFunction, {format, result});
    builder.CreateRet(builder.getInt32(0));
  } catch (const std::exception &error) {
    llvm::errs() << "error: " << error.what() << '\n';
    return 1;
  }

  if (llvm::verifyModule(*module, &llvm::errs())) {
    return 1;
  }

  module->print(llvm::outs(), nullptr);
  return 0;
}
