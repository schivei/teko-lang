#include "teko_internal.h"

static int is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int is_alnum(char c) {
    return is_alpha(c) || is_digit(c);
}

static char peek(Lexer *lexer) {
    if (lexer->pos >= lexer->source->text.length) return 0;
    return lexer->source->text.data[lexer->pos];
}

static char peek_next(Lexer *lexer) {
    if (lexer->pos + 1 >= lexer->source->text.length) return 0;
    return lexer->source->text.data[lexer->pos + 1];
}

static char advance(Lexer *lexer) {
    char c = peek(lexer);
    if (!c) return 0;
    lexer->pos++;
    if (c == '\n') {
        lexer->line++;
        lexer->column = 1;
    } else {
        lexer->column++;
    }
    return c;
}

static Token make_token(Lexer *lexer, TokenKind kind, size_t offset, unsigned line, unsigned column) {
    Token token;
    token.kind = kind;
    token.offset = offset;
    token.line = line;
    token.column = column;
    token.text = teko_string_slice(lexer->source->text, offset, lexer->pos - offset);
    return token;
}

static TokenKind keyword_kind(TekoString text) {
    if (teko_string_equal_cstr(text, "fn")) return TOK_FN;
    if (teko_string_equal_cstr(text, "struct")) return TOK_STRUCT;
    if (teko_string_equal_cstr(text, "enum")) return TOK_ENUM;
    if (teko_string_equal_cstr(text, "if")) return TOK_IF;
    if (teko_string_equal_cstr(text, "else")) return TOK_ELSE;
    if (teko_string_equal_cstr(text, "while")) return TOK_WHILE;
    if (teko_string_equal_cstr(text, "return")) return TOK_RETURN;
    if (teko_string_equal_cstr(text, "var")) return TOK_VAR;
    if (teko_string_equal_cstr(text, "let")) return TOK_LET;
    if (teko_string_equal_cstr(text, "true")) return TOK_TRUE;
    if (teko_string_equal_cstr(text, "false")) return TOK_FALSE;
    return TOK_IDENTIFIER;
}

void teko_lexer_init(Lexer *lexer, const TekoSource *source) {
    lexer->source = source;
    lexer->pos = 0;
    lexer->line = 1;
    lexer->column = 1;
}

Token teko_lexer_next(Lexer *lexer) {
    size_t offset;
    unsigned line;
    unsigned column;
    char c;

    for (;;) {
        c = peek(lexer);
        if (c == ' ' || c == '\t' || c == '\r') {
            advance(lexer);
            continue;
        }
        if (c == '/' && peek_next(lexer) == '/') {
            while (peek(lexer) && peek(lexer) != '\n') advance(lexer);
            continue;
        }
        if (c == '/' && peek_next(lexer) == '*') {
            advance(lexer);
            advance(lexer);
            while (peek(lexer) && !(peek(lexer) == '*' && peek_next(lexer) == '/')) advance(lexer);
            if (peek(lexer)) {
                advance(lexer);
                advance(lexer);
            }
            continue;
        }
        break;
    }

    offset = lexer->pos;
    line = lexer->line;
    column = lexer->column;
    c = advance(lexer);

    if (!c) return make_token(lexer, TOK_EOF, offset, line, column);
    if (c == '\n') return make_token(lexer, TOK_NEWLINE, offset, line, column);

    if (is_alpha(c)) {
        Token token;
        while (is_alnum(peek(lexer))) advance(lexer);
        token = make_token(lexer, TOK_IDENTIFIER, offset, line, column);
        token.kind = keyword_kind(token.text);
        return token;
    }

    if (is_digit(c)) {
        while (is_digit(peek(lexer))) advance(lexer);
        return make_token(lexer, TOK_INT_LITERAL, offset, line, column);
    }

    if (c == '"') {
        while (peek(lexer) && peek(lexer) != '"' && peek(lexer) != '\n') {
            if (peek(lexer) == '\\') advance(lexer);
            advance(lexer);
        }
        if (peek(lexer) == '"') advance(lexer);
        return make_token(lexer, TOK_STRING_LITERAL, offset, line, column);
    }

    if (c == '(') return make_token(lexer, TOK_LPAREN, offset, line, column);
    if (c == ')') return make_token(lexer, TOK_RPAREN, offset, line, column);
    if (c == '{') return make_token(lexer, TOK_LBRACE, offset, line, column);
    if (c == '}') return make_token(lexer, TOK_RBRACE, offset, line, column);
    if (c == '[') return make_token(lexer, TOK_LBRACKET, offset, line, column);
    if (c == ']') return make_token(lexer, TOK_RBRACKET, offset, line, column);
    if (c == ':') return make_token(lexer, TOK_COLON, offset, line, column);
    if (c == ',') return make_token(lexer, TOK_COMMA, offset, line, column);
    if (c == ';') return make_token(lexer, TOK_SEMICOLON, offset, line, column);
    if (c == '.') return make_token(lexer, TOK_DOT, offset, line, column);
    if (c == '+') return make_token(lexer, TOK_PLUS, offset, line, column);
    if (c == '*') return make_token(lexer, TOK_STAR, offset, line, column);
    if (c == '/') return make_token(lexer, TOK_SLASH, offset, line, column);
    if (c == '%') return make_token(lexer, TOK_PERCENT, offset, line, column);

    if (c == '-' && peek(lexer) == '>') {
        advance(lexer);
        return make_token(lexer, TOK_ARROW, offset, line, column);
    }
    if (c == '-') return make_token(lexer, TOK_MINUS, offset, line, column);
    if (c == '!' && peek(lexer) == '=') {
        advance(lexer);
        return make_token(lexer, TOK_BANG_EQUAL, offset, line, column);
    }
    if (c == '!') return make_token(lexer, TOK_BANG, offset, line, column);
    if (c == '=' && peek(lexer) == '=') {
        advance(lexer);
        return make_token(lexer, TOK_EQUAL_EQUAL, offset, line, column);
    }
    if (c == '=') return make_token(lexer, TOK_EQUAL, offset, line, column);
    if (c == '<' && peek(lexer) == '=') {
        advance(lexer);
        return make_token(lexer, TOK_LTE, offset, line, column);
    }
    if (c == '<') return make_token(lexer, TOK_LT, offset, line, column);
    if (c == '>' && peek(lexer) == '=') {
        advance(lexer);
        return make_token(lexer, TOK_GTE, offset, line, column);
    }
    if (c == '>') return make_token(lexer, TOK_GT, offset, line, column);
    if (c == '&' && peek(lexer) == '&') {
        advance(lexer);
        return make_token(lexer, TOK_AMP_AMP, offset, line, column);
    }
    if (c == '|' && peek(lexer) == '|') {
        advance(lexer);
        return make_token(lexer, TOK_PIPE_PIPE, offset, line, column);
    }

    return make_token(lexer, TOK_INVALID, offset, line, column);
}
