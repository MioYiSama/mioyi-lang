module;

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <MioYiLangLexer.h>
#include <MioYiLangParser.h>
#include <antlr4-runtime.h>

export module mioyi.parser;

import mioyi.ast;

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
  std::unique_ptr<mioyi::Ast> build(MioYiLangParser::ProgramContext *program) {
    auto ast = std::make_unique<mioyi::Ast>();
    for (auto *top : program->topLevel()) {
      mioyi::TopLevel item;
      if (auto *function =
              dynamic_cast<MioYiLangParser::FunctionTopLevelContext *>(top))
        item.function = buildFunction(function);
      else if (auto *declaration =
                   dynamic_cast<MioYiLangParser::DeclarationTopLevelContext *>(
                       top))
        item.declaration = buildDeclaration(declaration->declaration(),
                                            declaration->EXPORT() != nullptr);
      ast->items.push_back(std::move(item));
    }
    return ast;
  }

private:
  static mioyi::SourceLocation location(antlr4::ParserRuleContext *context) {
    return {context->getStart()->getLine(),
            context->getStart()->getCharPositionInLine() + 1};
  }

  [[noreturn]] static void fail(antlr4::ParserRuleContext *context,
                                const std::string &message) {
    throw AstError(location(context), message);
  }

  static std::string decodeQuoted(std::string_view text) {
    std::string result;
    if (text.size() < 2)
      return result;
    for (std::size_t index = 1; index + 1 < text.size(); ++index) {
      char value = text[index];
      if (value != '\\') {
        result.push_back(value);
        continue;
      }
      if (++index + 1 >= text.size())
        break;
      switch (text[index]) {
      case 'n':
        result.push_back('\n');
        break;
      case 'r':
        result.push_back('\r');
        break;
      case 't':
        result.push_back('\t');
        break;
      case '0':
        result.push_back('\0');
        break;
      case 'u': {
        if (index + 4 >= text.size())
          break;
        unsigned codepoint = 0;
        for (int digit = 0; digit < 4; ++digit) {
          const char hex = text[++index];
          codepoint =
              codepoint * 16 + (hex >= '0' && hex <= '9'   ? hex - '0'
                                : hex >= 'a' && hex <= 'f' ? hex - 'a' + 10
                                                           : hex - 'A' + 10);
        }
        appendUtf8(result, codepoint);
        break;
      }
      default:
        result.push_back(text[index]);
        break;
      }
    }
    return result;
  }

  static void appendUtf8(std::string &output, std::uint32_t value) {
    if (value <= 0x7f)
      output.push_back(static_cast<char>(value));
    else if (value <= 0x7ff) {
      output.push_back(static_cast<char>(0xc0 | (value >> 6)));
      output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else if (value <= 0xffff) {
      output.push_back(static_cast<char>(0xe0 | (value >> 12)));
      output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else {
      output.push_back(static_cast<char>(0xf0 | (value >> 18)));
      output.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    }
  }

  static std::uint32_t firstCodepoint(std::string_view text) {
    if (text.empty())
      return 0;
    const auto first = static_cast<unsigned char>(text.front());
    if (first < 0x80)
      return first;
    std::uint32_t result = first & (first < 0xe0   ? 0x1f
                                    : first < 0xf0 ? 0x0f
                                                   : 0x07);
    const int length = first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
    for (int index = 1; index < length && index < static_cast<int>(text.size());
         ++index)
      result = (result << 6) | (static_cast<unsigned char>(text[index]) & 0x3f);
    return result;
  }

  std::unique_ptr<mioyi::TypeRef>
  buildType(MioYiLangParser::TypeRefContext *context) {
    auto result = std::make_unique<mioyi::TypeRef>();
    result->location = location(context);
    if (context->BUILTIN_TYPE())
      result->name = context->BUILTIN_TYPE()->getText();
    else {
      result->name = "Array";
      result->element = buildType(context->typeRef());
    }
    return result;
  }

  std::unique_ptr<mioyi::Expression>
  makeExpression(mioyi::Expression::Kind kind,
                 antlr4::ParserRuleContext *context) {
    auto result = std::make_unique<mioyi::Expression>();
    result->kind = kind;
    result->location = location(context);
    return result;
  }

  std::unique_ptr<mioyi::Expression>
  buildInteger(MioYiLangParser::IntegerLiteralContext *context) {
    auto result = makeExpression(mioyi::Expression::Kind::Integer, context);
    std::string text = context->INTEGER()->getText();
    int base = 10;
    std::string_view digits = text;
    if (text.size() > 2 && text[0] == '0') {
      if (text[1] == 'x' || text[1] == 'X')
        base = 16;
      else if (text[1] == 'b' || text[1] == 'B')
        base = 2;
      else if (text[1] == 'o' || text[1] == 'O')
        base = 8;
      if (base != 10)
        digits.remove_prefix(2);
    }
    auto [end, error] = std::from_chars(
        digits.data(), digits.data() + digits.size(), result->integer, base);
    if (error != std::errc{} || end != digits.data() + digits.size())
      fail(context, "integer literal is outside the 64-bit range");
    if (context->BYTE_SUFFIX())
      result->text = "Byte";
    else if (context->LONG_SUFFIX())
      result->text = "Int64";
    else if (context->UINT_SUFFIX())
      result->text = "UInt";
    else if (context->ULONG_SUFFIX())
      result->text = "UInt64";
    else
      result->text = "Int";
    return result;
  }

  std::unique_ptr<mioyi::Expression>
  buildPrimary(MioYiLangParser::PrimaryContext *context) {
    if (context->integerLiteral())
      return buildInteger(context->integerLiteral());
    if (context->FLOAT_LITERAL()) {
      auto result = makeExpression(mioyi::Expression::Kind::Floating, context);
      std::string text = context->FLOAT_LITERAL()->getText();
      if (!text.empty() && text.back() == 'L') {
        result->text = "Float64";
        text.pop_back();
      } else
        result->text = "Float";
      result->floating = std::strtod(text.c_str(), nullptr);
      return result;
    }
    if (context->STRING_LITERAL()) {
      auto result = makeExpression(mioyi::Expression::Kind::String, context);
      result->text = decodeQuoted(context->STRING_LITERAL()->getText());
      return result;
    }
    if (context->CHAR_LITERAL()) {
      auto result = makeExpression(mioyi::Expression::Kind::Character, context);
      const std::string decoded =
          decodeQuoted(context->CHAR_LITERAL()->getText());
      result->integer = firstCodepoint(decoded);
      return result;
    }
    if (context->TRUE() || context->FALSE()) {
      auto result = makeExpression(mioyi::Expression::Kind::Boolean, context);
      result->boolean = context->TRUE() != nullptr;
      return result;
    }
    if (context->VOID())
      return makeExpression(mioyi::Expression::Kind::Void, context);
    if (context->arrayLiteral()) {
      auto result = makeExpression(mioyi::Expression::Kind::Array, context);
      for (auto *element : context->arrayLiteral()->expression())
        result->operands.push_back(buildExpression(element));
      return result;
    }
    if (context->expression())
      return buildExpression(context->expression());
    auto result = makeExpression(mioyi::Expression::Kind::LValue, context);
    result->text = context->IDENT()->getText();
    return result;
  }

  std::unique_ptr<mioyi::Expression>
  buildPostfix(MioYiLangParser::PostfixContext *context) {
    auto value = buildPrimary(context->primary());
    std::size_t argumentIndex = 0;
    std::size_t expressionIndex = 0;
    for (std::size_t index = 1; index < context->children.size();) {
      const std::string token = context->children[index]->getText();
      if (token == "(") {
        auto result = makeExpression(mioyi::Expression::Kind::Call, context);
        result->operands.push_back(std::move(value));
        if (argumentIndex < context->arguments().size()) {
          auto *arguments = context->arguments(argumentIndex++);
          for (auto *argument : arguments->expression())
            result->operands.push_back(buildExpression(argument));
          index += 3;
        } else
          index += 2;
        value = std::move(result);
      } else if (token == "[") {
        auto result = makeExpression(mioyi::Expression::Kind::Index, context);
        result->operands.push_back(std::move(value));
        result->operands.push_back(
            buildExpression(context->expression(expressionIndex++)));
        value = std::move(result);
        index += 3;
      } else
        ++index;
    }
    return value;
  }

  std::unique_ptr<mioyi::Expression>
  buildUnary(MioYiLangParser::UnaryContext *context) {
    if (context->postfix())
      return buildPostfix(context->postfix());
    auto result = makeExpression(mioyi::Expression::Kind::Unary, context);
    result->text = context->children.front()->getText();
    result->operands.push_back(buildUnary(context->unary()));
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
  buildMultiplicative(MioYiLangParser::MultiplicativeContext *context) {
    return buildBinary(context, context->unary(),
                       [&](auto *child) { return buildUnary(child); });
  }
  std::unique_ptr<mioyi::Expression>
  buildAdditive(MioYiLangParser::AdditiveContext *context) {
    return buildBinary(context, context->multiplicative(),
                       [&](auto *child) { return buildMultiplicative(child); });
  }
  std::unique_ptr<mioyi::Expression>
  buildComparison(MioYiLangParser::ComparisonContext *context) {
    return buildBinary(context, context->additive(),
                       [&](auto *child) { return buildAdditive(child); });
  }
  std::unique_ptr<mioyi::Expression>
  buildEquality(MioYiLangParser::EqualityContext *context) {
    return buildBinary(context, context->comparison(),
                       [&](auto *child) { return buildComparison(child); });
  }
  std::unique_ptr<mioyi::Expression>
  buildLogicalAnd(MioYiLangParser::LogicalAndContext *context) {
    return buildBinary(context, context->equality(),
                       [&](auto *child) { return buildEquality(child); });
  }
  std::unique_ptr<mioyi::Expression>
  buildLogicalOr(MioYiLangParser::LogicalOrContext *context) {
    return buildBinary(context, context->logicalAnd(),
                       [&](auto *child) { return buildLogicalAnd(child); });
  }
  std::unique_ptr<mioyi::Expression>
  buildExpression(MioYiLangParser::ExpressionContext *context) {
    return buildLogicalOr(context->logicalOr());
  }

  std::unique_ptr<mioyi::Expression>
  buildLValue(MioYiLangParser::LvalueContext *context) {
    auto value = makeExpression(mioyi::Expression::Kind::LValue, context);
    value->text = context->IDENT()->getText();
    for (auto *index : context->expression()) {
      auto result = makeExpression(mioyi::Expression::Kind::Index, context);
      result->operands.push_back(std::move(value));
      result->operands.push_back(buildExpression(index));
      value = std::move(result);
    }
    return value;
  }

  mioyi::BindingKind
  buildBindingKind(MioYiLangParser::BindingKindContext *context) {
    if (context->VAR())
      return mioyi::BindingKind::Variable;
    if (context->VAL())
      return mioyi::BindingKind::Value;
    return mioyi::BindingKind::Definition;
  }

  std::unique_ptr<mioyi::Declaration>
  buildDeclaration(MioYiLangParser::DeclarationContext *context,
                   bool exported = false) {
    auto result = std::make_unique<mioyi::Declaration>();
    result->kind = buildBindingKind(context->bindingKind());
    result->exported = exported;
    for (auto *binding : context->binding()) {
      mioyi::Binding item;
      item.location = location(binding);
      item.name = binding->IDENT()->getText();
      if (binding->typeRef())
        item.type = buildType(binding->typeRef());
      item.initializer = buildExpression(binding->expression());
      result->bindings.push_back(std::move(item));
    }
    return result;
  }

  std::unique_ptr<mioyi::Statement>
  declarationStatement(MioYiLangParser::DeclarationContext *context) {
    auto result = std::make_unique<mioyi::Statement>();
    result->kind = mioyi::Statement::Kind::Declaration;
    result->location = location(context);
    result->declaration = buildDeclaration(context);
    return result;
  }

  std::unique_ptr<mioyi::Statement>
  buildBlock(MioYiLangParser::BlockContext *context) {
    auto result = std::make_unique<mioyi::Statement>();
    result->kind = mioyi::Statement::Kind::Block;
    result->location = location(context);
    for (auto *child : context->children) {
      if (auto *declaration =
              dynamic_cast<MioYiLangParser::DeclarationContext *>(child))
        result->block.push_back(declarationStatement(declaration));
      else if (auto *statement =
                   dynamic_cast<MioYiLangParser::StatementContext *>(child))
        result->block.push_back(buildStatement(statement));
    }
    return result;
  }

  std::unique_ptr<mioyi::Statement>
  buildIf(MioYiLangParser::IfStatementContext *context) {
    auto result = std::make_unique<mioyi::Statement>();
    result->kind = mioyi::Statement::Kind::If;
    result->location = location(context);
    result->expression = buildExpression(context->expression());
    result->thenBranch = buildBlock(context->block(0));
    if (context->ifStatement())
      result->elseBranch = buildIf(context->ifStatement());
    else if (context->block().size() > 1)
      result->elseBranch = buildBlock(context->block(1));
    return result;
  }

  std::unique_ptr<mioyi::Statement>
  buildFor(MioYiLangParser::ForStatementContext *context) {
    auto result = std::make_unique<mioyi::Statement>();
    result->location = location(context);
    result->kind = context->IN() ? mioyi::Statement::Kind::ForEach
                                 : mioyi::Statement::Kind::For;
    if (context->IDENT())
      result->text = context->IDENT()->getText();
    if (context->expression())
      result->expression = buildExpression(context->expression());
    result->thenBranch = buildBlock(context->block());
    return result;
  }

  std::unique_ptr<mioyi::Statement>
  buildStatement(MioYiLangParser::StatementContext *context) {
    auto result = std::make_unique<mioyi::Statement>();
    result->location = location(context);
    if (auto *statement =
            dynamic_cast<MioYiLangParser::BlockStatementContext *>(context))
      return buildBlock(statement->block());
    if (auto *statement =
            dynamic_cast<MioYiLangParser::ConditionalStatementContext *>(
                context))
      return buildIf(statement->ifStatement());
    if (auto *statement =
            dynamic_cast<MioYiLangParser::LoopStatementContext *>(context))
      return buildFor(statement->forStatement());
    if (auto *statement =
            dynamic_cast<MioYiLangParser::ReturnStatementContext *>(context)) {
      result->kind = mioyi::Statement::Kind::Return;
      if (statement->expression())
        result->expression = buildExpression(statement->expression());
    } else if (dynamic_cast<MioYiLangParser::BreakStatementContext *>(context))
      result->kind = mioyi::Statement::Kind::Break;
    else if (dynamic_cast<MioYiLangParser::ContinueStatementContext *>(context))
      result->kind = mioyi::Statement::Kind::Continue;
    else if (auto *statement =
                 dynamic_cast<MioYiLangParser::AssignmentStatementContext *>(
                     context)) {
      result->kind = mioyi::Statement::Kind::Assignment;
      result->text = statement->assignmentOperator()->getText();
      result->target = buildLValue(statement->lvalue());
      result->expression = buildExpression(statement->expression());
    } else if (auto *statement =
                   dynamic_cast<MioYiLangParser::ExpressionStatementContext *>(
                       context)) {
      result->kind = mioyi::Statement::Kind::Expression;
      result->expression = buildExpression(statement->expression());
    }
    return result;
  }

  std::unique_ptr<mioyi::Function>
  buildFunction(MioYiLangParser::FunctionTopLevelContext *context) {
    auto result = std::make_unique<mioyi::Function>();
    result->location = location(context);
    result->name = context->IDENT()->getText();
    result->exported = context->EXPORT() != nullptr;
    if (context->genericParams())
      for (auto *parameter : context->genericParams()->IDENT())
        result->genericParameters.push_back(parameter->getText());
    auto *expression = context->functionExpression();
    if (expression->parameters()) {
      for (auto *parameter : expression->parameters()->parameter()) {
        mioyi::Parameter item;
        item.location = location(parameter);
        item.name = parameter->IDENT()->getText();
        item.type = buildType(parameter->typeRef());
        result->parameters.push_back(std::move(item));
      }
    }
    if (expression->typeRef())
      result->returnType = buildType(expression->typeRef());
    result->body = std::make_unique<mioyi::Statement>();
    result->body->kind = mioyi::Statement::Kind::Block;
    result->body->location = location(expression);
    for (auto *item : expression->functionItem()) {
      if (item->declaration())
        result->body->block.push_back(
            declarationStatement(item->declaration()));
      else
        result->body->block.push_back(buildStatement(item->statement()));
    }
    return result;
  }
};

} // namespace

export namespace mioyi::parser {

class Options {
public:
  std::string input;
  std::string output;
  std::string format;
};

using Result = std::expected<void, std::string>;

std::unique_ptr<mioyi::Ast> parseSource(std::string_view source) {
  antlr4::ANTLRInputStream input{std::string(source)};
  MioYiLangLexer lexer(&input);
  antlr4::CommonTokenStream tokens(&lexer);
  MioYiLangParser parser(&tokens);
  SyntaxErrorListener errors;
  lexer.removeErrorListeners();
  lexer.addErrorListener(&errors);
  parser.removeErrorListeners();
  parser.addErrorListener(&errors);
  auto *tree = parser.program();
  if (errors.failed)
    return nullptr;
  try {
    return AstBuilder{}.build(tree);
  } catch (const std::exception &error) {
    std::cerr << "error: " << error.what() << '\n';
    return nullptr;
  }
}

std::unique_ptr<mioyi::Ast> deserializeAst(std::string_view source,
                                           std::string_view format);

Result parse(Options options);

} // namespace mioyi::parser
