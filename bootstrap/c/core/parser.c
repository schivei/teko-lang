#include "teko_internal.h"

#include <string.h>

typedef struct Parser {
    TekoContext *ctx;
    const TekoSource *source;
    Lexer lexer;
    Token current;
    Token previous;
} Parser;

static void *grow_array(TekoContext *ctx, void *items, size_t *capacity, const size_t item_size) {
    const size_t next = *capacity ? *capacity * 2 : 8;
    void *result = teko_realloc(ctx, items, next * item_size);
    if (result) *capacity = next;
    return result;
}

static void ast_program_push(TekoContext *ctx, AstProgram *program, const AstDecl decl) {
    if (program->decl_count == program->decl_capacity) {
        program->decls = (AstDecl *)grow_array(ctx, program->decls, &program->decl_capacity, sizeof(AstDecl));
    }
    program->decls[program->decl_count++] = decl;
}

static void ast_block_push(TekoContext *ctx, AstBlock *block, const AstStmt stmt) {
    if (block->count == block->capacity) {
        block->items = (AstStmt *)grow_array(ctx, block->items, &block->capacity, sizeof(AstStmt));
    }
    block->items[block->count++] = stmt;
}

static void ast_param_push(TekoContext *ctx, AstDecl *decl, const AstParam param) {
    if (decl->param_count == decl->param_capacity) {
        decl->params = (AstParam *)grow_array(ctx, decl->params, &decl->param_capacity, sizeof(AstParam));
    }
    decl->params[decl->param_count++] = param;
}

static void ast_field_push(TekoContext *ctx, AstDecl *decl, const AstField field) {
    if (decl->field_count == decl->field_capacity) {
        decl->fields = (AstField *)grow_array(ctx, decl->fields, &decl->field_capacity, sizeof(AstField));
    }
    decl->fields[decl->field_count++] = field;
}

static void ast_variant_push(TekoContext *ctx, AstDecl *decl, const AstVariant variant) {
    if (decl->variant_count == decl->variant_capacity) {
        decl->variants = (AstVariant *)grow_array(ctx, decl->variants, &decl->variant_capacity, sizeof(AstVariant));
    }
    decl->variants[decl->variant_count++] = variant;
}

static void parser_init(Parser *parser, TekoContext *ctx, const TekoSource *source) {
    parser->ctx = ctx;
    parser->source = source;
    teko_lexer_init(&parser->lexer, source);
    parser->previous.kind = TOK_INVALID;
    parser->current = teko_lexer_next(&parser->lexer);
}

static void advance(Parser *parser) {
    parser->previous = parser->current;
    parser->current = teko_lexer_next(&parser->lexer);
}

static int check(const Parser *parser, const TokenKind kind) {
    return parser->current.kind == kind;
}

static int match(Parser *parser, const TokenKind kind) {
    if (!check(parser, kind)) return 0;
    advance(parser);
    return 1;
}

static void skip_newlines(Parser *parser) {
    while (match(parser, TOK_NEWLINE) || match(parser, TOK_SEMICOLON)) {
    }
}

static int expect(Parser *parser, const TokenKind kind, const char *message) {
    if (match(parser, kind)) return 1;
    teko_add_diagnostic(parser->ctx, TEKO_DIAG_ERROR, parser->source,
                        parser->current.offset, parser->current.line, parser->current.column, message);
    return 0;
}

static TekoString source_between(const Parser *parser, const size_t start, size_t end) {
    if (end < start) end = start;
    return teko_string_trim(teko_string_slice(parser->source->text, start, end - start));
}

static AstLocation token_location(const Parser *parser, const Token token) {
    AstLocation location;
    location.source = parser->source;
    location.offset = token.offset;
    location.line = token.line;
    location.column = token.column;
    return location;
}

static AstType parse_type_until(Parser *parser, const TokenKind a, const TokenKind b, const TokenKind c) {
    const size_t start = parser->current.offset;
    size_t end = start;
    int depth_angle = 0;
    int depth_bracket = 0;
    while (parser->current.kind != TOK_EOF) {
        const TokenKind k = parser->current.kind;
        if (depth_angle == 0 && depth_bracket == 0 && (k == a || k == b || k == c)) break;
        if (k == TOK_LT) depth_angle++;
        else if (k == TOK_GT && depth_angle > 0) depth_angle--;
        else if (k == TOK_LBRACKET) depth_bracket++;
        else if (k == TOK_RBRACKET && depth_bracket > 0) depth_bracket--;
        end = parser->current.offset + parser->current.text.length;
        advance(parser);
    }
    {
        AstType type;
        type.text = source_between(parser, start, end);
        return type;
    }
}

static TekoString consume_expr_until(Parser *parser, const TokenKind a, const TokenKind b, const TokenKind c) {
    const size_t start = parser->current.offset;
    size_t end = start;
    int paren = 0;
    int bracket = 0;
    while (parser->current.kind != TOK_EOF) {
        const TokenKind k = parser->current.kind;
        if (paren == 0 && bracket == 0 && (k == a || k == b || k == c)) break;
        if (k == TOK_LPAREN) paren++;
        else if (k == TOK_RPAREN && paren > 0) paren--;
        else if (k == TOK_LBRACKET) bracket++;
        else if (k == TOK_RBRACKET && bracket > 0) bracket--;
        end = parser->current.offset + parser->current.text.length;
        advance(parser);
    }
    return source_between(parser, start, end);
}

static AstBlock parse_block(Parser *parser);

static AstStmt parse_statement(Parser *parser) {
    AstStmt stmt = {0};

    skip_newlines(parser);

    if (match(parser, TOK_RETURN)) {
        stmt.location = token_location(parser, parser->previous);
        stmt.kind = AST_STMT_RETURN;
        stmt.expr = consume_expr_until(parser, TOK_NEWLINE, TOK_SEMICOLON, TOK_RBRACE);
        match(parser, TOK_SEMICOLON);
        return stmt;
    }

    if (match(parser, TOK_IF)) {
        stmt.location = token_location(parser, parser->previous);
        stmt.kind = AST_STMT_IF;
        stmt.condition = consume_expr_until(parser, TOK_LBRACE, TOK_EOF, TOK_EOF);
        stmt.then_block = parse_block(parser);
        skip_newlines(parser);
        if (match(parser, TOK_ELSE)) {
            stmt.else_block = parse_block(parser);
        }
        return stmt;
    }

    if (match(parser, TOK_WHILE)) {
        stmt.location = token_location(parser, parser->previous);
        stmt.kind = AST_STMT_WHILE;
        stmt.condition = consume_expr_until(parser, TOK_LBRACE, TOK_EOF, TOK_EOF);
        stmt.then_block = parse_block(parser);
        return stmt;
    }

    if (match(parser, TOK_VAR) || match(parser, TOK_LET)) {
        const Token keyword = parser->previous;
        stmt.location = token_location(parser, keyword);
        stmt.kind = AST_STMT_LOCAL;
        stmt.is_mutable = keyword.kind == TOK_VAR;
        if (expect(parser, TOK_IDENTIFIER, "expected local name")) {
            stmt.name = parser->previous.text;
        }
        if (match(parser, TOK_COLON)) {
            stmt.type = parse_type_until(parser, TOK_EQUAL, TOK_NEWLINE, TOK_SEMICOLON);
        }
        if (match(parser, TOK_EQUAL)) {
            stmt.expr = consume_expr_until(parser, TOK_NEWLINE, TOK_SEMICOLON, TOK_RBRACE);
        }
        match(parser, TOK_SEMICOLON);
        return stmt;
    }

    stmt.kind = AST_STMT_EXPR;
    stmt.location = token_location(parser, parser->current);
    stmt.expr = consume_expr_until(parser, TOK_NEWLINE, TOK_SEMICOLON, TOK_RBRACE);
    match(parser, TOK_SEMICOLON);
    return stmt;
}

static AstBlock parse_block(Parser *parser) {
    AstBlock block = {0};
    if (!expect(parser, TOK_LBRACE, "expected '{' to start block")) {
        return block;
    }
    skip_newlines(parser);
    while (!check(parser, TOK_RBRACE) && !check(parser, TOK_EOF)) {
        const AstStmt stmt = parse_statement(parser);
        if (stmt.kind != AST_STMT_EXPR || stmt.expr.length > 0) {
            ast_block_push(parser->ctx, &block, stmt);
        }
        skip_newlines(parser);
    }
    expect(parser, TOK_RBRACE, "expected '}' to end block");
    return block;
}

static AstDecl parse_function(Parser *parser) {
    AstDecl decl = {0};
    decl.kind = AST_DECL_FUNCTION;
    decl.source = parser->source;
    expect(parser, TOK_FN, "expected 'fn'");
    decl.location = token_location(parser, parser->previous);
    if (expect(parser, TOK_IDENTIFIER, "expected function name")) {
        decl.name = parser->previous.text;
    }
    expect(parser, TOK_LPAREN, "expected '(' after function name");
    skip_newlines(parser);
    while (!check(parser, TOK_RPAREN) && !check(parser, TOK_EOF)) {
        AstParam param = {0};
        if (expect(parser, TOK_IDENTIFIER, "expected parameter name")) {
            param.location = token_location(parser, parser->previous);
            param.name = parser->previous.text;
        }
        expect(parser, TOK_COLON, "expected ':' after parameter name");
        param.type = parse_type_until(parser, TOK_COMMA, TOK_RPAREN, TOK_EOF);
        ast_param_push(parser->ctx, &decl, param);
        if (!match(parser, TOK_COMMA)) break;
        skip_newlines(parser);
    }
    expect(parser, TOK_RPAREN, "expected ')' after parameters");
    expect(parser, TOK_ARROW, "expected '->' before return type");
    decl.return_type = parse_type_until(parser, TOK_LBRACE, TOK_NEWLINE, TOK_EOF);
    decl.body = parse_block(parser);
    return decl;
}

static AstDecl make_file_decl(const AstDeclKind kind, const TekoString name, const TekoSource *source) {
    AstDecl decl = {0};
    decl.kind = kind;
    decl.name = name;
    decl.source = source;
    decl.location.source = source;
    decl.location.offset = 0;
    decl.location.line = 1;
    decl.location.column = 1;
    return decl;
}

static void parse_file_struct(Parser *parser, AstProgram *program) {
    AstDecl decl = make_file_decl(AST_DECL_STRUCT, parser->source->type_name, parser->source);
    skip_newlines(parser);
    while (!check(parser, TOK_EOF)) {
        AstField field = {0};
        if (expect(parser, TOK_IDENTIFIER, "expected field name")) {
            field.location = token_location(parser, parser->previous);
            field.name = parser->previous.text;
        }
        expect(parser, TOK_COLON, "expected ':' after field name");
        field.type = parse_type_until(parser, TOK_NEWLINE, TOK_SEMICOLON, TOK_EOF);
        ast_field_push(parser->ctx, &decl, field);
        skip_newlines(parser);
    }
    ast_program_push(parser->ctx, program, decl);
}

static void parse_file_enum(Parser *parser, AstProgram *program) {
    AstDecl decl = make_file_decl(AST_DECL_ENUM, parser->source->type_name, parser->source);
    skip_newlines(parser);
    while (!check(parser, TOK_EOF)) {
        if (expect(parser, TOK_IDENTIFIER, "expected enum variant")) {
            AstVariant variant = {0};
            variant.location = token_location(parser, parser->previous);
            variant.name = parser->previous.text;
            ast_variant_push(parser->ctx, &decl, variant);
        }
        if (match(parser, TOK_COMMA)) {
        }
        skip_newlines(parser);
    }
    ast_program_push(parser->ctx, program, decl);
}

static AstDecl parse_braced_struct(Parser *parser) {
    AstDecl decl = {0};
    decl.kind = AST_DECL_STRUCT;
    decl.source = parser->source;
    expect(parser, TOK_STRUCT, "expected 'struct'");
    decl.location = token_location(parser, parser->previous);
    if (expect(parser, TOK_IDENTIFIER, "expected struct name")) {
        decl.name = parser->previous.text;
    }
    expect(parser, TOK_LBRACE, "expected '{' after struct name");
    skip_newlines(parser);
    while (!check(parser, TOK_RBRACE) && !check(parser, TOK_EOF)) {
        AstField field = {0};
        if (expect(parser, TOK_IDENTIFIER, "expected field name")) {
            field.location = token_location(parser, parser->previous);
            field.name = parser->previous.text;
        }
        expect(parser, TOK_COLON, "expected ':' after field name");
        field.type = parse_type_until(parser, TOK_NEWLINE, TOK_SEMICOLON, TOK_RBRACE);
        ast_field_push(parser->ctx, &decl, field);
        skip_newlines(parser);
    }
    expect(parser, TOK_RBRACE, "expected '}' after struct");
    return decl;
}

static AstDecl parse_braced_enum(Parser *parser) {
    AstDecl decl = {0};
    decl.kind = AST_DECL_ENUM;
    decl.source = parser->source;
    expect(parser, TOK_ENUM, "expected 'enum'");
    decl.location = token_location(parser, parser->previous);
    if (expect(parser, TOK_IDENTIFIER, "expected enum name")) decl.name = parser->previous.text;
    expect(parser, TOK_LBRACE, "expected '{' after enum name");
    skip_newlines(parser);
    while (!check(parser, TOK_RBRACE) && !check(parser, TOK_EOF)) {
        if (expect(parser, TOK_IDENTIFIER, "expected enum variant")) {
            AstVariant variant = {0};
            variant.location = token_location(parser, parser->previous);
            variant.name = parser->previous.text;
            ast_variant_push(parser->ctx, &decl, variant);
        }
        match(parser, TOK_COMMA);
        skip_newlines(parser);
    }
    expect(parser, TOK_RBRACE, "expected '}' after enum");
    return decl;
}

static void parse_regular_source(Parser *parser, AstProgram *program) {
    skip_newlines(parser);
    while (!check(parser, TOK_EOF)) {
        if (check(parser, TOK_FN)) {
            ast_program_push(parser->ctx, program, parse_function(parser));
        } else if (check(parser, TOK_STRUCT)) {
            ast_program_push(parser->ctx, program, parse_braced_struct(parser));
        } else if (check(parser, TOK_ENUM)) {
            ast_program_push(parser->ctx, program, parse_braced_enum(parser));
        } else {
            teko_add_diagnostic(parser->ctx, TEKO_DIAG_ERROR, parser->source,
                                parser->current.offset, parser->current.line, parser->current.column,
                                "expected top-level declaration");
            while (!check(parser, TOK_NEWLINE) && !check(parser, TOK_EOF)) {
                advance(parser);
            }
        }
        skip_newlines(parser);
    }
}

static void check_duplicates(TekoContext *ctx, const AstProgram *program) {
    for (size_t i = 0; i < program->decl_count; i++) {
        for (size_t j = i + 1; j < program->decl_count; j++) {
            if (teko_string_equal(program->decls[i].name, program->decls[j].name)) {
                teko_add_diagnostic(ctx, TEKO_DIAG_ERROR, 0, 0, 0, 0, "duplicate top-level declaration");
            }
        }
    }
}

int teko_parse_sources(TekoContext *ctx,
                       const TekoSource *sources,
                       const size_t source_count,
                       AstProgram *program) {
    memset(program, 0, sizeof(*program));
    for (size_t i = 0; i < source_count; i++) {
        Parser parser;
        parser_init(&parser, ctx, &sources[i]);
        if (sources[i].kind == TEKO_SOURCE_STRUCT) {
            parse_file_struct(&parser, program);
        } else if (sources[i].kind == TEKO_SOURCE_ENUM) {
            parse_file_enum(&parser, program);
        } else if (sources[i].kind == TEKO_SOURCE_TEKO || sources[i].kind == TEKO_SOURCE_STATIC) {
            parse_regular_source(&parser, program);
        } else {
            teko_add_diagnostic(ctx, TEKO_DIAG_ERROR, &sources[i], 0, 1, 1, "unsupported Teko source kind");
        }
    }
    check_duplicates(ctx, program);
    return ctx->diagnostic_count == 0;
}

static void free_block(TekoContext *ctx, const AstBlock *block) {
    for (size_t i = 0; i < block->count; i++) {
        free_block(ctx, &block->items[i].then_block);
        free_block(ctx, &block->items[i].else_block);
    }
    teko_free(ctx, block->items);
}

void teko_free_program(TekoContext *ctx, const AstProgram *program) {
    for (size_t i = 0; i < program->decl_count; i++) {
        teko_free(ctx, program->decls[i].params);
        teko_free(ctx, program->decls[i].fields);
        teko_free(ctx, program->decls[i].variants);
        free_block(ctx, &program->decls[i].body);
    }
    teko_free(ctx, program->decls);
}
