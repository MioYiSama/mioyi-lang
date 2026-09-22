module;

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <antlr4-runtime.h>
#include <ExpressionLexer.h>
#include <ExpressionParser.h>

export module mioyi.parser;

export namespace mioyi {

struct SourceLocation {
  std::size_t line = 0;
  std::size_t column = 0;
};

struct Expression {
  enum class Kind { Integer, LValue, Call, Unary, Binary };

  Kind kind = Kind::Integer;
  SourceLocation location;
  std::int32_t integer = 0;
  std::string text;
  std::vector<std::unique_ptr<Expression>> operands;
};

struct Initializer {
  SourceLocation location;
  std::unique_ptr<Expression> expression;
  std::vector<std::unique_ptr<Initializer>> elements;
};

struct VariableDefinition {
  SourceLocation location;
  std::string name;
  std::vector<std::unique_ptr<Expression>> dimensions;
  std::unique_ptr<Initializer> initializer;
};

struct Declaration {
  bool constant = false;
  std::vector<VariableDefinition> definitions;
};

struct Statement {
  enum class Kind {
    Declaration,
    Assignment,
    Expression,
    Block,
    If,
    While,
    Break,
    Continue,
    Return
  };

  Kind kind = Kind::Expression;
  SourceLocation location;
  std::unique_ptr<Declaration> declaration;
  std::unique_ptr<Expression> target;
  std::unique_ptr<Expression> expression;
  std::vector<std::unique_ptr<Statement>> block;
  std::unique_ptr<Statement> thenBranch;
  std::unique_ptr<Statement> elseBranch;
};

struct Parameter {
  SourceLocation location;
  std::string name;
  bool array = false;
  std::vector<std::unique_ptr<Expression>> dimensions;
};

struct Function {
  SourceLocation location;
  std::string name;
  bool returnsValue = false;
  std::vector<Parameter> parameters;
  std::unique_ptr<Statement> body;
};

struct TopLevel {
  std::unique_ptr<Declaration> declaration;
  std::unique_ptr<Function> function;
};

struct Ast {
  std::vector<TopLevel> items;
};

} // namespace mioyi

namespace {

class SyntaxErrorListener final : public antlr4::BaseErrorListener {
public:
  void syntaxError(antlr4::Recognizer *, antlr4::Token *, std::size_t line,
                   std::size_t column, const std::string &message,
                   std::exception_ptr) override {
    failed = true;
    std::cerr << "error: " << line << ':' << column + 1 << ": " << message
              << '\n';
  }

  bool failed = false;
};

class AstError final : public std::runtime_error {
public:
  AstError(mioyi::SourceLocation location, const std::string &message)
      : std::runtime_error(std::to_string(location.line) + ":" +
                           std::to_string(location.column) + ": " + message) {}
};

class AstBuilder {
public:
  std::unique_ptr<mioyi::Ast> build(ExpressionParser::ProgramContext *program) {
    auto ast = std::make_unique<mioyi::Ast>();
    for (auto *child : program->compUnit()->children) {
      mioyi::TopLevel item;
      if (auto *declaration =
              dynamic_cast<ExpressionParser::DeclContext *>(child))
        item.declaration = buildDeclaration(declaration);
      else if (auto *function =
                   dynamic_cast<ExpressionParser::FuncDefContext *>(child))
        item.function = buildFunction(function);
      else
        continue;
      ast->items.push_back(std::move(item));
    }
    return ast;
  }

private:
  static mioyi::SourceLocation location(antlr4::ParserRuleContext *context) {
    return {context->getStart()->getLine(),
            context->getStart()->getCharPositionInLine() + 1};
  }

  static std::int32_t integer(ExpressionParser::NumberContext *context) {
    const std::string text = context->getText();
    int base = 10;
    std::string_view digits = text;
    if (text.size() > 2 && text[0] == '0' &&
        (text[1] == 'x' || text[1] == 'X')) {
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
      throw AstError(location(context),
                     "integer literal is outside the 32-bit range");
    return static_cast<std::int32_t>(value);
  }

  static std::unique_ptr<mioyi::Expression>
  makeExpression(mioyi::Expression::Kind kind,
                 antlr4::ParserRuleContext *context) {
    auto result = std::make_unique<mioyi::Expression>();
    result->kind = kind;
    result->location = location(context);
    return result;
  }

  std::unique_ptr<mioyi::Expression>
  buildLValue(ExpressionParser::LValContext *context) {
    auto result = makeExpression(mioyi::Expression::Kind::LValue, context);
    result->text = context->IDENT()->getText();
    for (auto *index : context->exp())
      result->operands.push_back(buildExpression(index));
    return result;
  }

  std::unique_ptr<mioyi::Expression>
  buildExpression(ExpressionParser::ExpContext *context) {
    return buildAdd(context->addExp());
  }

  std::unique_ptr<mioyi::Expression>
  buildPrimary(ExpressionParser::PrimaryExpContext *context) {
    if (context->number()) {
      auto result = makeExpression(mioyi::Expression::Kind::Integer, context);
      result->integer = integer(context->number());
      return result;
    }
    if (context->exp()) return buildExpression(context->exp());
    return buildLValue(context->lVal());
  }

  std::unique_ptr<mioyi::Expression>
  buildUnary(ExpressionParser::UnaryExpContext *context) {
    if (context->primaryExp()) return buildPrimary(context->primaryExp());
    if (context->unaryOp()) {
      auto result = makeExpression(mioyi::Expression::Kind::Unary, context);
      result->text = context->unaryOp()->getText();
      result->operands.push_back(buildUnary(context->unaryExp()));
      return result;
    }
    auto result = makeExpression(mioyi::Expression::Kind::Call, context);
    result->text = context->IDENT()->getText();
    if (context->funcRParams())
      for (auto *argument : context->funcRParams()->exp())
        result->operands.push_back(buildExpression(argument));
    return result;
  }

  template <class Context, class Child, class Build>
  std::unique_ptr<mioyi::Expression>
  buildBinary(Context *context, const std::vector<Child *> &children,
              Build buildChild) {
    auto value = buildChild(children.front());
    for (std::size_t index = 1; index < children.size(); ++index) {
      auto result = makeExpression(mioyi::Expression::Kind::Binary, context);
      result->text = context->children[index * 2 - 1]->getText();
      result->operands.push_back(std::move(value));
      result->operands.push_back(buildChild(children[index]));
      value = std::move(result);
    }
    return value;
  }

  std::unique_ptr<mioyi::Expression>
  buildMul(ExpressionParser::MulExpContext *context) {
    return buildBinary(context, context->unaryExp(),
                       [&](auto *child) { return buildUnary(child); });
  }

  std::unique_ptr<mioyi::Expression>
  buildAdd(ExpressionParser::AddExpContext *context) {
    return buildBinary(context, context->mulExp(),
                       [&](auto *child) { return buildMul(child); });
  }

  std::unique_ptr<mioyi::Expression>
  buildRel(ExpressionParser::RelExpContext *context) {
    return buildBinary(context, context->addExp(),
                       [&](auto *child) { return buildAdd(child); });
  }

  std::unique_ptr<mioyi::Expression>
  buildEq(ExpressionParser::EqExpContext *context) {
    return buildBinary(context, context->relExp(),
                       [&](auto *child) { return buildRel(child); });
  }

  std::unique_ptr<mioyi::Expression>
  buildAnd(ExpressionParser::LAndExpContext *context) {
    return buildBinary(context, context->eqExp(),
                       [&](auto *child) { return buildEq(child); });
  }

  std::unique_ptr<mioyi::Expression>
  buildOr(ExpressionParser::LOrExpContext *context) {
    return buildBinary(context, context->lAndExp(),
                       [&](auto *child) { return buildAnd(child); });
  }

  std::unique_ptr<mioyi::Initializer>
  buildInitializer(ExpressionParser::InitValContext *context) {
    auto result = std::make_unique<mioyi::Initializer>();
    result->location = location(context);
    if (context->exp()) result->expression = buildExpression(context->exp());
    for (auto *element : context->initVal())
      result->elements.push_back(buildInitializer(element));
    return result;
  }

  std::unique_ptr<mioyi::Initializer>
  buildInitializer(ExpressionParser::ConstInitValContext *context) {
    auto result = std::make_unique<mioyi::Initializer>();
    result->location = location(context);
    if (context->constExp())
      result->expression = buildAdd(context->constExp()->addExp());
    for (auto *element : context->constInitVal())
      result->elements.push_back(buildInitializer(element));
    return result;
  }

  std::unique_ptr<mioyi::Declaration>
  buildDeclaration(ExpressionParser::DeclContext *context) {
    auto result = std::make_unique<mioyi::Declaration>();
    result->constant = context->constDecl() != nullptr;
    if (auto *declaration = context->constDecl()) {
      for (auto *definition : declaration->constDef()) {
        mioyi::VariableDefinition item;
        item.location = location(definition);
        item.name = definition->IDENT()->getText();
        for (auto *dimension : definition->constExp())
          item.dimensions.push_back(buildAdd(dimension->addExp()));
        item.initializer = buildInitializer(definition->constInitVal());
        result->definitions.push_back(std::move(item));
      }
    } else {
      for (auto *definition : context->varDecl()->varDef()) {
        mioyi::VariableDefinition item;
        item.location = location(definition);
        item.name = definition->IDENT()->getText();
        for (auto *dimension : definition->constExp())
          item.dimensions.push_back(buildAdd(dimension->addExp()));
        if (definition->initVal())
          item.initializer = buildInitializer(definition->initVal());
        result->definitions.push_back(std::move(item));
      }
    }
    return result;
  }

  std::unique_ptr<mioyi::Statement>
  buildBlock(ExpressionParser::BlockContext *context) {
    auto result = std::make_unique<mioyi::Statement>();
    result->kind = mioyi::Statement::Kind::Block;
    result->location = location(context);
    for (auto *item : context->blockItem()) {
      if (item->decl()) {
        auto child = std::make_unique<mioyi::Statement>();
        child->kind = mioyi::Statement::Kind::Declaration;
        child->location = location(item->decl());
        child->declaration = buildDeclaration(item->decl());
        result->block.push_back(std::move(child));
      } else {
        result->block.push_back(buildStatement(item->stmt()));
      }
    }
    return result;
  }

  std::unique_ptr<mioyi::Statement>
  buildStatement(ExpressionParser::StmtContext *context) {
    auto result = std::make_unique<mioyi::Statement>();
    result->location = location(context);
    if (auto *statement =
            dynamic_cast<ExpressionParser::AssignStmtContext *>(context)) {
      result->kind = mioyi::Statement::Kind::Assignment;
      result->target = buildLValue(statement->lVal());
      result->expression = buildExpression(statement->exp());
    } else if (auto *statement =
                   dynamic_cast<ExpressionParser::ExpressionStmtContext *>(
                       context)) {
      result->kind = mioyi::Statement::Kind::Expression;
      if (statement->exp()) result->expression = buildExpression(statement->exp());
    } else if (auto *statement =
                   dynamic_cast<ExpressionParser::BlockStmtContext *>(context)) {
      return buildBlock(statement->block());
    } else if (auto *statement =
                   dynamic_cast<ExpressionParser::IfStmtContext *>(context)) {
      result->kind = mioyi::Statement::Kind::If;
      result->expression = buildOr(statement->cond()->lOrExp());
      result->thenBranch = buildStatement(statement->stmt(0));
      if (statement->ELSE())
        result->elseBranch = buildStatement(statement->stmt(1));
    } else if (auto *statement =
                   dynamic_cast<ExpressionParser::WhileStmtContext *>(context)) {
      result->kind = mioyi::Statement::Kind::While;
      result->expression = buildOr(statement->cond()->lOrExp());
      result->thenBranch = buildStatement(statement->stmt());
    } else if (dynamic_cast<ExpressionParser::BreakStmtContext *>(context)) {
      result->kind = mioyi::Statement::Kind::Break;
    } else if (dynamic_cast<ExpressionParser::ContinueStmtContext *>(context)) {
      result->kind = mioyi::Statement::Kind::Continue;
    } else if (auto *statement =
                   dynamic_cast<ExpressionParser::ReturnStmtContext *>(context)) {
      result->kind = mioyi::Statement::Kind::Return;
      if (statement->exp()) result->expression = buildExpression(statement->exp());
    }
    return result;
  }

  std::unique_ptr<mioyi::Function>
  buildFunction(ExpressionParser::FuncDefContext *context) {
    auto result = std::make_unique<mioyi::Function>();
    result->location = location(context);
    result->name = context->IDENT()->getText();
    result->returnsValue = context->funcType()->INT() != nullptr;
    if (context->funcFParams()) {
      for (auto *parameter : context->funcFParams()->funcFParam()) {
        mioyi::Parameter item;
        item.location = location(parameter);
        item.name = parameter->IDENT()->getText();
        item.array = !parameter->LBRACK().empty();
        for (auto *dimension : parameter->constExp())
          item.dimensions.push_back(buildAdd(dimension->addExp()));
        result->parameters.push_back(std::move(item));
      }
    }
    result->body = buildBlock(context->block());
    return result;
  }
};

} // namespace

export std::unique_ptr<mioyi::Ast> parseSource(std::string_view source) {
  antlr4::ANTLRInputStream input{std::string(source)};
  ExpressionLexer lexer(&input);
  antlr4::CommonTokenStream tokens(&lexer);
  ExpressionParser parser(&tokens);
  SyntaxErrorListener errors;
  lexer.removeErrorListeners();
  lexer.addErrorListener(&errors);
  parser.removeErrorListeners();
  parser.addErrorListener(&errors);
  auto *tree = parser.program();
  if (errors.failed) return nullptr;
  try {
    return AstBuilder{}.build(tree);
  } catch (const std::exception &error) {
    std::cerr << "error: " << error.what() << '\n';
    return nullptr;
  }
}
