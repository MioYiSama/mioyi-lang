module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

export module mioyi.ast;

export namespace mioyi {

struct SourceLocation {
  std::size_t line = 0;
  std::size_t column = 0;
};

struct TypeRef {
  SourceLocation location;
  std::string name;
  std::unique_ptr<TypeRef> element;
};

struct Expression {
  enum class Kind {
    Integer,
    Floating,
    String,
    Character,
    Boolean,
    Void,
    LValue,
    Array,
    Call,
    Index,
    Unary,
    Binary
  };

  Kind kind = Kind::Void;
  SourceLocation location;
  std::uint64_t integer = 0;
  double floating = 0;
  bool boolean = false;
  std::string text;
  std::vector<std::unique_ptr<Expression>> operands;
};

enum class BindingKind { Variable, Value, Definition };

struct Binding {
  SourceLocation location;
  std::string name;
  std::unique_ptr<TypeRef> type;
  std::unique_ptr<Expression> initializer;
};

struct Declaration {
  BindingKind kind = BindingKind::Variable;
  bool exported = false;
  std::vector<Binding> bindings;
};

struct Statement {
  enum class Kind {
    Declaration,
    Assignment,
    Expression,
    Block,
    If,
    For,
    ForEach,
    Break,
    Continue,
    Return
  };

  Kind kind = Kind::Expression;
  SourceLocation location;
  std::string text;
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
  std::unique_ptr<TypeRef> type;
};

struct Function {
  SourceLocation location;
  std::string name;
  bool exported = false;
  std::vector<std::string> genericParameters;
  std::vector<Parameter> parameters;
  std::unique_ptr<TypeRef> returnType;
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
