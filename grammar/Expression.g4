grammar Expression;

program
    : expression EOF
    ;

expression
    : additive
    ;

additive
    : multiplicative ((PLUS | MINUS) multiplicative)*
    ;

multiplicative
    : unary ((STAR | SLASH) unary)*
    ;

unary
    : (PLUS | MINUS) unary
    | primary
    ;

primary
    : INTEGER
    | LPAREN expression RPAREN
    ;

PLUS    : '+';
MINUS   : '-';
STAR    : '*';
SLASH   : '/';
LPAREN  : '(';
RPAREN  : ')';
INTEGER : [0-9]+;
WS      : [ \t\r\n]+ -> skip;
