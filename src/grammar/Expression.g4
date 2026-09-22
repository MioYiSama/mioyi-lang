grammar Expression;

program         : compUnit EOF;
compUnit        : (decl | funcDef)*;

decl            : constDecl | varDecl;
constDecl       : CONST INT constDef (COMMA constDef)* SEMI;
constDef        : IDENT (LBRACK constExp RBRACK)* ASSIGN constInitVal;
constInitVal    : constExp | LBRACE (constInitVal (COMMA constInitVal)*)? RBRACE;
varDecl         : INT varDef (COMMA varDef)* SEMI;
varDef          : IDENT (LBRACK constExp RBRACK)* (ASSIGN initVal)?;
initVal         : exp | LBRACE (initVal (COMMA initVal)*)? RBRACE;

funcDef         : funcType IDENT LPAREN funcFParams? RPAREN block;
funcType        : VOID | INT;
funcFParams     : funcFParam (COMMA funcFParam)*;
funcFParam      : INT IDENT (LBRACK RBRACK (LBRACK constExp RBRACK)*)?;

block           : LBRACE blockItem* RBRACE;
blockItem       : decl | stmt;
stmt
    : lVal ASSIGN exp SEMI                         # assignStmt
    | exp? SEMI                                    # expressionStmt
    | block                                        # blockStmt
    | IF LPAREN cond RPAREN stmt (ELSE stmt)?      # ifStmt
    | WHILE LPAREN cond RPAREN stmt                # whileStmt
    | BREAK SEMI                                   # breakStmt
    | CONTINUE SEMI                                # continueStmt
    | RETURN exp? SEMI                             # returnStmt
    ;

exp             : addExp;
cond            : lOrExp;
lVal            : IDENT (LBRACK exp RBRACK)*;
primaryExp      : LPAREN exp RPAREN | lVal | number;
number          : INTEGER;
unaryExp
    : primaryExp
    | IDENT LPAREN funcRParams? RPAREN
    | unaryOp unaryExp
    ;
unaryOp         : PLUS | MINUS | NOT;
funcRParams     : exp (COMMA exp)*;
mulExp          : unaryExp ((STAR | SLASH | PERCENT) unaryExp)*;
addExp          : mulExp ((PLUS | MINUS) mulExp)*;
relExp          : addExp ((LT | GT | LE | GE) addExp)*;
eqExp           : relExp ((EQ | NE) relExp)*;
lAndExp         : eqExp (AND eqExp)*;
lOrExp          : lAndExp (OR lAndExp)*;
constExp        : addExp;

CONST           : 'const';
INT             : 'int';
VOID            : 'void';
IF              : 'if';
ELSE            : 'else';
WHILE           : 'while';
BREAK           : 'break';
CONTINUE        : 'continue';
RETURN          : 'return';
PLUS            : '+';
MINUS           : '-';
STAR            : '*';
SLASH           : '/';
PERCENT         : '%';
NOT             : '!';
ASSIGN          : '=';
LT              : '<';
GT              : '>';
LE              : '<=';
GE              : '>=';
EQ              : '==';
NE              : '!=';
AND             : '&&';
OR              : '||';
LPAREN          : '(';
RPAREN          : ')';
LBRACE          : '{';
RBRACE          : '}';
LBRACK          : '[';
RBRACK          : ']';
COMMA           : ',';
SEMI            : ';';
IDENT           : [a-zA-Z_] [a-zA-Z_0-9]*;
INTEGER         : '0' [xX] [0-9a-fA-F]+ | '0' [0-7]* | [1-9] [0-9]*;
LINE_COMMENT    : '//' ~[\r\n]* -> skip;
BLOCK_COMMENT   : '/*' .*? '*/' -> skip;
WS              : [ \t\r\n]+ -> skip;
