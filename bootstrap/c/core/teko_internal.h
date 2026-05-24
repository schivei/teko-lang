#ifndef TEKO_INTERNAL_H
#define TEKO_INTERNAL_H

#include "teko_core.h"

#include <stdarg.h>

typedef struct TekoContext TekoContext;

typedef enum TokenKind {
    TOK_EOF,
    TOK_INVALID,
    TOK_NEWLINE,
    TOK_IDENTIFIER,
    TOK_INT_LITERAL,
    TOK_STRING_LITERAL,
    TOK_FN,
    TOK_STRUCT,
    TOK_ENUM,
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_RETURN,
    TOK_VAR,
    TOK_LET,
    TOK_TRUE,
    TOK_FALSE,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_COLON,
    TOK_COMMA,
    TOK_SEMICOLON,
    TOK_DOT,
    TOK_ARROW,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_BANG,
    TOK_EQUAL,
    TOK_EQUAL_EQUAL,
    TOK_BANG_EQUAL,
    TOK_LT,
    TOK_LTE,
    TOK_GT,
    TOK_GTE,
    TOK_AMP_AMP,
    TOK_PIPE_PIPE
} TokenKind;

typedef struct Token {
    TokenKind kind;
    TekoString text;
    size_t offset;
    unsigned line;
    unsigned column;
} Token;

typedef struct Lexer {
    const TekoSource *source;
    size_t pos;
    unsigned line;
    unsigned column;
} Lexer;

typedef struct AstType {
    TekoString text;
} AstType;

typedef struct AstParam {
    TekoString name;
    AstType type;
} AstParam;

typedef enum AstStmtKind {
    AST_STMT_RETURN,
    AST_STMT_IF,
    AST_STMT_WHILE,
    AST_STMT_LOCAL,
    AST_STMT_EXPR
} AstStmtKind;

typedef struct AstStmt AstStmt;

typedef struct AstBlock {
    AstStmt *items;
    size_t count;
    size_t capacity;
} AstBlock;

struct AstStmt {
    AstStmtKind kind;
    int is_mutable;
    TekoString name;
    AstType type;
    TekoString expr;
    TekoString condition;
    AstBlock then_block;
    AstBlock else_block;
};

typedef struct AstField {
    TekoString name;
    AstType type;
} AstField;

typedef struct AstVariant {
    TekoString name;
} AstVariant;

typedef enum AstDeclKind {
    AST_DECL_FUNCTION,
    AST_DECL_STRUCT,
    AST_DECL_ENUM
} AstDeclKind;

typedef struct AstDecl {
    AstDeclKind kind;
    TekoString name;
    AstType return_type;
    AstParam *params;
    size_t param_count;
    size_t param_capacity;
    AstField *fields;
    size_t field_count;
    size_t field_capacity;
    AstVariant *variants;
    size_t variant_count;
    size_t variant_capacity;
    AstBlock body;
} AstDecl;

typedef struct AstProgram {
    AstDecl *decls;
    size_t decl_count;
    size_t decl_capacity;
} AstProgram;

struct TekoContext {
    TekoAllocator allocator;
    TekoDiagnostic *diagnostics;
    size_t diagnostic_count;
    size_t diagnostic_capacity;
};

typedef struct StringBuilder {
    TekoContext *ctx;
    char *data;
    size_t length;
    size_t capacity;
} StringBuilder;

void *teko_alloc(TekoContext *ctx, size_t size);
void *teko_realloc(TekoContext *ctx, void *ptr, size_t size);
void teko_free(TekoContext *ctx, void *ptr);

int teko_string_equal(TekoString a, TekoString b);
int teko_string_equal_cstr(TekoString a, const char *b);
TekoString teko_string_slice(TekoString base, size_t offset, size_t length);
TekoString teko_string_trim(TekoString value);
char *teko_copy_cstr(TekoContext *ctx, TekoString value);

void teko_add_diagnostic(TekoContext *ctx,
                         TekoDiagnosticSeverity severity,
                         const TekoSource *source,
                         size_t offset,
                         unsigned line,
                         unsigned column,
                         const char *message);

void teko_lexer_init(Lexer *lexer, const TekoSource *source);
Token teko_lexer_next(Lexer *lexer);

int teko_parse_sources(TekoContext *ctx,
                       const TekoSource *sources,
                       size_t source_count,
                       AstProgram *program);
void teko_free_program(TekoContext *ctx, AstProgram *program);

int teko_backend_emit_c(TekoContext *ctx, const AstProgram *program, TekoOutput *output);

void sb_init(StringBuilder *sb, TekoContext *ctx);
void sb_append(StringBuilder *sb, const char *text);
void sb_append_n(StringBuilder *sb, const char *text, size_t length);
void sb_append_string(StringBuilder *sb, TekoString text);
void sb_append_char(StringBuilder *sb, char c);
TekoString sb_build(StringBuilder *sb);

#endif
