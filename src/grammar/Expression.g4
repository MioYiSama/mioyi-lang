grammar Expression;

program         : topLevel* EOF;
topLevel
    : EXPORT? DEF IDENT genericParams? ASSIGN functionExpression SEMI? # functionTopLevel
    | EXPORT? declaration                                      # declarationTopLevel
    ;

declaration     : bindingKind binding (COMMA binding)* SEMI?;
bindingKind     : VAR | VAL | DEF;
binding         : IDENT (COLON typeRef)? ASSIGN expression;
genericParams   : LT IDENT (COMMA IDENT)* GT;

functionExpression
    : LBRACE LPAREN parameters? RPAREN ARROW typeRef? functionItem* RBRACE
    ;
parameters      : parameter (COMMA parameter)*;
parameter       : IDENT COLON typeRef;
functionItem    : declaration | statement;

statement
    : block                                                    # blockStatement
    | ifStatement                                              # conditionalStatement
    | forStatement                                             # loopStatement
    | RETURN expression? SEMI?                                 # returnStatement
    | BREAK SEMI?                                              # breakStatement
    | CONTINUE SEMI?                                           # continueStatement
    | lvalue assignmentOperator expression SEMI?               # assignmentStatement
    | expression SEMI?                                         # expressionStatement
    ;
block           : LBRACE (declaration | statement)* RBRACE;
ifStatement     : IF expression block (ELSE (ifStatement | block))?;
forStatement    : FOR (IDENT IN expression | expression)? block;
assignmentOperator : ASSIGN | PLUS_ASSIGN | MINUS_ASSIGN | STAR_ASSIGN
                   | SLASH_ASSIGN | PERCENT_ASSIGN;

expression      : logicalOr;
logicalOr       : logicalAnd (OR logicalAnd)*;
logicalAnd      : equality (AND equality)*;
equality        : comparison ((EQ | NE) comparison)*;
comparison      : additive ((LT | GT | LE | GE) additive)*;
additive        : multiplicative ((PLUS | MINUS) multiplicative)*;
multiplicative  : unary ((STAR | SLASH | PERCENT) unary)*;
unary           : (PLUS | MINUS | NOT) unary | postfix;
postfix         : primary (LPAREN arguments? RPAREN | LBRACK expression RBRACK)*;
arguments       : expression (COMMA expression)*;
primary
    : integerLiteral
    | FLOAT_LITERAL
    | STRING_LITERAL
    | CHAR_LITERAL
    | TRUE
    | FALSE
    | VOID
    | arrayLiteral
    | LPAREN expression RPAREN
    | IDENT
    ;
arrayLiteral    : LBRACK (expression (COMMA expression)*)? RBRACK;
lvalue          : IDENT (LBRACK expression RBRACK)*;
integerLiteral  : INTEGER (BYTE_SUFFIX | LONG_SUFFIX | UINT_SUFFIX | ULONG_SUFFIX)?;

typeRef         : BUILTIN_TYPE | LBRACK typeRef RBRACK;

EXPORT          : 'export';
VAR             : 'var';
VAL             : 'val';
DEF             : 'def';
IF              : 'if';
ELSE            : 'else';
FOR             : 'for';
IN              : 'in';
RETURN          : 'return';
BREAK           : 'break';
CONTINUE        : 'continue';
TRUE            : 'true';
FALSE           : 'false';
VOID            : 'void';
ARROW           : '=>';
PLUS_ASSIGN     : '+=';
MINUS_ASSIGN    : '-=';
STAR_ASSIGN     : '*=';
SLASH_ASSIGN    : '/=';
PERCENT_ASSIGN  : '%=';
LE              : '<=';
GE              : '>=';
EQ              : '==';
NE              : '!=';
AND             : '&&';
OR              : '||';
ASSIGN          : '=';
PLUS            : '+';
MINUS           : '-';
STAR            : '*';
SLASH           : '/';
PERCENT         : '%';
NOT             : '!';
LT              : '<';
GT              : '>';
LPAREN          : '(';
RPAREN          : ')';
LBRACE          : '{';
RBRACE          : '}';
LBRACK          : '[';
RBRACK          : ']';
COLON           : ':';
COMMA           : ',';
SEMI            : ';';
ULONG_SUFFIX    : 'UL';
BYTE_SUFFIX     : 'B';
LONG_SUFFIX     : 'L';
UINT_SUFFIX     : 'U';
BUILTIN_TYPE    : 'Byte' | 'Int' | 'Int32' | 'Int64' | 'UInt' | 'UInt32'
                | 'UInt64' | 'Float' | 'Float32' | 'Float64' | 'Char'
                | 'String' | 'Bool' | 'Void' | 'Never';
FLOAT_LITERAL   : [0-9]+ '.' [0-9]+ ([eE] [+-]? [0-9]+)? 'L'?;
INTEGER         : '0' [xX] [0-9a-fA-F]+ | '0' [bB] [01]+ | '0' [oO] [0-7]+
                | '0' | [1-9] [0-9]*;
STRING_LITERAL  : '"' (ESCAPE | ~["\\\r\n])* '"';
CHAR_LITERAL    : '\'' (ESCAPE | ~['\\\r\n]) '\'';
fragment ESCAPE : '\\' (["'\\nrt0] | 'u' HEX HEX HEX HEX);
fragment HEX    : [0-9a-fA-F];
IDENT           : [a-zA-Z_] [a-zA-Z_0-9]*;
LINE_COMMENT    : '//' ~[\r\n]* -> skip;
BLOCK_COMMENT   : '/*' .*? '*/' -> skip;
WS              : [ \t\r\n]+ -> skip;
