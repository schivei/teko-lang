#ifndef LEXER_H
#define LEXER_H

#include <stdbool.h>

// Enumeração de todos os tipos de tokens da linguagem
typedef enum {
    // Fim do arquivo / Erro
    TOKEN_EOF = 0,
    TOKEN_UNKNOWN,

    // Palavras-chave (Keywords)
    TOKEN_USE, TOKEN_FROM, TOKEN_EXTERN, TOKEN_STRUCT, TOKEN_FN, TOKEN_AS,
    TOKEN_CONST, TOKEN_ASYNC, TOKEN_LET, TOKEN_MUT, TOKEN_DEFER, TOKEN_RETURN,
    TOKEN_WHEN, TOKEN_FOR, TOKEN_SWITCH, TOKEN_NULL, TOKEN_RAISED, TOKEN_WHERE,
    TOKEN_EXP, TOKEN_INTERFACE, TOKEN_SERVICE, TOKEN_DECORATES, TOKEN_EXTEND,
    TOKEN_COMMAND, TOKEN_HANDLER, TOKEN_NOTIFICATION, TOKEN_WITH, TOKEN_QUERY,
    TOKEN_OPERATOR, TOKEN_PUB, TOKEN_REQD,

    TOKEN_STRING_LIT,       // "texto" comum ou """multilinha""" tradicional
    TOKEN_STRING_INTERPOLATED,    // `texto {expr}` comum ou ```multilinha``` interpolada
    TOKEN_STRING_RAW_LIT,         // $"texto" ou $"""multilinha""" (Ignora escapes)
    TOKEN_STRING_RAW_INTERP,       // $`texto` ou $```multilinha``` (Ignora escapes, processa interpolação)

    // Identificadores e Literais
    TOKEN_IDENTIFIER,
    TOKEN_MACRO_IDENT,       // Ex: @marshall.to_ptr
    TOKEN_LIT_INT,           // Ex: 1, 3_000, 255
    TOKEN_LIT_FLOAT,         // Ex: 1.2
    TOKEN_LIT_STR,           // Ex: "World"
    TOKEN_LIT_CHAR,          // Ex: 'a'
    TOKEN_LIT_MULTILINE_STR, // Ex: """...\n..."""

    // Símbolos de Pontuação e Delimitadores
    TOKEN_LPAREN, TOKEN_RPAREN,     // ( )
    TOKEN_LBRACE, TOKEN_RBRACE,     // { }
    TOKEN_LBRACKET, TOKEN_RBRACKET, // [ ]
    TOKEN_SEMICOLON,                // ;
    TOKEN_COMMA,                    // ,
    TOKEN_DOT,                      // .
    TOKEN_QUESTION,                 // ?
    TOKEN_ELVIS,                    // ??
    TOKEN_SAFE_DOT,                 // ?.
    TOKEN_COLON,                    // :
    TOKEN_DBL_COLON,                // ::
    TOKEN_ARROW,                    // =>

    // Operadores Matemáticos, Lógicos e Bitwise Expandidos
    TOKEN_PLUS,          // +
    TOKEN_MINUS,         // -
    TOKEN_MUL,           // *
    TOKEN_DIV,           // /
    TOKEN_MOD,           // %
    TOKEN_POW,           // **
    TOKEN_SHL,           // <<
    TOKEN_SHR,           // >>
    TOKEN_BIT_AND,       // &
    TOKEN_BIT_OR,        // |
    TOKEN_BIT_XOR,       // ^
    TOKEN_BIT_NOT,       // ~
    TOKEN_AND,           // &&
    TOKEN_OR,            // ||
    TOKEN_NOT,           // !
    TOKEN_LT,            // <
    TOKEN_LE,            // <=
    TOKEN_GT,            // >
    TOKEN_GE,            // >=
    TOKEN_EQ,            // ==
    TOKEN_NE,            // !=

    // Operadores de Atribuição Composta e Especial
    TOKEN_ASSIGN,        // =
    TOKEN_QUICK_ASSIGN,  // :=
    TOKEN_ADD_ASSIGN,    // +=
    TOKEN_SUB_ASSIGN,    // -=
    TOKEN_MUL_ASSIGN,    // *=
    TOKEN_DIV_ASSIGN,    // /=
    TOKEN_MOD_ASSIGN,    // %=
    TOKEN_SHL_ASSIGN,    // <<=
    TOKEN_SHR_ASSIGN,    // >>=
    TOKEN_AND_ASSIGN,    // &=
    TOKEN_OR_ASSIGN,     // |=
    TOKEN_XOR_ASSIGN     // ^=
} TokenType;

// Estrutura que representa um Token
typedef struct {
    TokenType type;
    char* lexeme;
    int line;
} Token;

// Estado do Lexer
typedef struct {
    const char* source;
    int cursor;
    int line;
} Lexer;

// Assinaturas das funções públicas do Lexer
void lexer_init(Lexer* lexer, const char* source);
Token lexer_next_token(Lexer* lexer);
const char* get_token_type_name(TokenType type);

#endif // LEXER_H