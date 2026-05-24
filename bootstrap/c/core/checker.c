#include "teko_internal.h"

#include <string.h>

typedef struct LocalSymbol {
    TekoString name;
    AstType type;
} LocalSymbol;

typedef struct CheckScope {
    TekoContext *ctx;
    const AstProgram *program;
    const AstDecl *function;
    LocalSymbol *locals;
    size_t local_count;
    size_t local_capacity;
    int saw_return;
} CheckScope;

typedef struct ExprType {
    AstType type;
    int valid;
} ExprType;

typedef struct ExprParser {
    CheckScope *scope;
    const AstStmt *stmt;
    TekoSource source;
    Lexer lexer;
    Token current;
    size_t base_offset;
} ExprParser;

static int type_is(const TekoString type, const char *name) {
    return teko_string_equal_cstr(teko_string_trim(type), name);
}

static AstType type_from_cstr(const char *name) {
    AstType type;
    type.text = teko_string_from_cstr(name);
    return type;
}

static ExprType expr_type_error(void) {
    ExprType result;
    result.type = type_from_cstr("<error>");
    result.valid = 0;
    return result;
}

static ExprType expr_type_ok(const AstType type) {
    ExprType result;
    result.type = type;
    result.valid = 1;
    return result;
}

static int starts_with(const TekoString value, const char *prefix) {
    const TekoString p = teko_string_from_cstr(prefix);
    return value.length >= p.length && memcmp(value.data, p.data, p.length) == 0;
}

static int ends_with_char(const TekoString value, const char c) {
    return value.length > 0 && value.data[value.length - 1] == c;
}

static const AstDecl *find_decl(const AstProgram *program, const TekoString name) {
    for (size_t i = 0; i < program->decl_count; i++) {
        if (teko_string_equal(program->decls[i].name, name)) {
            return &program->decls[i];
        }
    }
    return 0;
}

static const AstDecl *find_function(const AstProgram *program, const TekoString name) {
    const AstDecl *decl = find_decl(program, name);
    if (decl && decl->kind == AST_DECL_FUNCTION) {
        return decl;
    }
    return 0;
}

static const AstType *find_local_type(const CheckScope *scope, const TekoString name) {
    for (size_t i = 0; i < scope->local_count; i++) {
        if (teko_string_equal(scope->locals[i].name, name)) {
            return &scope->locals[i].type;
        }
    }
    return 0;
}

static const AstType *find_param_type(const CheckScope *scope, const TekoString name) {
    for (size_t i = 0; i < scope->function->param_count; i++) {
        if (teko_string_equal(scope->function->params[i].name, name)) {
            return &scope->function->params[i].type;
        }
    }
    return 0;
}

static int is_builtin_type(const TekoString type) {
    const TekoString t = teko_string_trim(type);
    return type_is(t, "void") ||
           type_is(t, "bool") ||
           type_is(t, "byte") ||
           type_is(t, "int") ||
           type_is(t, "uint") ||
           type_is(t, "long") ||
           type_is(t, "ulong") ||
           type_is(t, "usize") ||
           type_is(t, "isize") ||
           type_is(t, "string");
}

static int type_equal(const AstType a, const AstType b) {
    return teko_string_equal(teko_string_trim(a.text), teko_string_trim(b.text));
}

static int is_numeric_type(const AstType type) {
    const TekoString t = teko_string_trim(type.text);
    return type_is(t, "byte") ||
           type_is(t, "int") ||
           type_is(t, "uint") ||
           type_is(t, "long") ||
           type_is(t, "ulong") ||
           type_is(t, "usize") ||
           type_is(t, "isize");
}

static void line_column_for_offset(const TekoSource *source, size_t offset, unsigned *line, unsigned *column) {
    *line = 1;
    *column = 1;
    if (!source || !source->text.data) {
        return;
    }
    if (offset > source->text.length) {
        offset = source->text.length;
    }
    for (size_t i = 0; i < offset; i++) {
        if (source->text.data[i] == '\n') {
            (*line)++;
            *column = 1;
        } else {
            (*column)++;
        }
    }
}

static size_t offset_from_string(const TekoSource *source, const TekoString text) {
    if (!source || !source->text.data || !text.data ||
        text.data < source->text.data ||
        text.data > source->text.data + source->text.length) {
        return 0;
    }
    return (size_t)(text.data - source->text.data);
}

static void diag_at(TekoContext *ctx,
                    const TekoSource *source,
                    const size_t offset,
                    const char *message) {
    unsigned line;
    unsigned column;
    line_column_for_offset(source, offset, &line, &column);
    teko_add_diagnostic(ctx, TEKO_DIAG_ERROR, source, offset, line, column, message);
}

static void diag_location(TekoContext *ctx, const AstLocation location, const char *message) {
    if (location.line == 0 || location.column == 0) {
        diag_at(ctx, location.source, location.offset, message);
        return;
    }
    teko_add_diagnostic(ctx,
                        TEKO_DIAG_ERROR,
                        location.source,
                        location.offset,
                        location.line,
                        location.column,
                        message);
}

static void diag(const CheckScope *scope, const AstStmt *stmt, const char *message) {
    diag_location(scope->ctx, stmt->location, message);
}

static void diag_decl(TekoContext *ctx, const AstDecl *decl, const char *message) {
    if (decl) {
        diag_location(ctx, decl->location, message);
    } else {
        diag_at(ctx, 0, 0, message);
    }
}

static int check_type(TekoContext *ctx,
                      const AstProgram *program,
                      const AstDecl *owner,
                      const AstType type,
                      const AstLocation location,
                      const int allow_void) {
    const TekoString t = teko_string_trim(type.text);
    if (t.length == 0) {
        diag_location(ctx, location, "missing type");
        return 0;
    }
    if (type_is(t, "void")) {
        if (!allow_void) {
            diag_location(ctx, location, "void is only allowed as a function return type");
            return 0;
        }
        return 1;
    }
    if (is_builtin_type(t)) {
        return 1;
    }
    if (starts_with(t, "ptr<") && ends_with_char(t, '>')) {
        AstType inner;
        inner.text = teko_string_slice(t, 4, t.length - 5);
        return check_type(ctx, program, owner, inner, location, 0);
    }
    if (starts_with(t, "Slice<") && ends_with_char(t, '>')) {
        AstType inner;
        inner.text = teko_string_slice(t, 6, t.length - 7);
        return check_type(ctx, program, owner, inner, location, 0);
    }
    if (find_decl(program, t)) {
        return 1;
    }
    diag_location(ctx, location, "unknown type");
    return 0;
}

static int local_exists(const CheckScope *scope, const TekoString name) {
    return find_local_type(scope, name) != 0;
}

static void add_local(CheckScope *scope, const TekoString name, const AstType type) {
    if (scope->local_count == scope->local_capacity) {
        const size_t next = scope->local_capacity ? scope->local_capacity * 2 : 8;
        scope->locals = (LocalSymbol *)teko_realloc(scope->ctx, scope->locals, next * sizeof(LocalSymbol));
        scope->local_capacity = next;
    }
    scope->locals[scope->local_count].name = name;
    scope->locals[scope->local_count].type = type;
    scope->local_count++;
}

static void expr_diag(const ExprParser *parser, const Token token, const char *message) {
    diag_at(parser->scope->ctx,
            parser->stmt->location.source,
            parser->base_offset + token.offset,
            message);
}

static void expr_advance(ExprParser *parser) {
    parser->current = teko_lexer_next(&parser->lexer);
}

static int expr_match(ExprParser *parser, const TokenKind kind) {
    if (parser->current.kind != kind) {
        return 0;
    }
    expr_advance(parser);
    return 1;
}

static ExprType parse_expression(ExprParser *parser);

static void require_type_compatible(ExprParser *parser, const Token token, const AstType expected, const AstType actual, const char *message) {
    if (!type_equal(expected, actual)) {
        expr_diag(parser, token, message);
    }
}

static ExprType parse_primary(ExprParser *parser) {
    const Token token = parser->current;
    if (expr_match(parser, TOK_INT_LITERAL)) {
        return expr_type_ok(type_from_cstr("int"));
    }
    if (expr_match(parser, TOK_STRING_LITERAL)) {
        return expr_type_ok(type_from_cstr("string"));
    }
    if (expr_match(parser, TOK_TRUE) || expr_match(parser, TOK_FALSE)) {
        return expr_type_ok(type_from_cstr("bool"));
    }
    if (expr_match(parser, TOK_LPAREN)) {
        const ExprType inner = parse_expression(parser);
        if (!expr_match(parser, TOK_RPAREN)) {
            expr_diag(parser, parser->current, "expected ')' after expression");
        }
        return inner;
    }
    if (expr_match(parser, TOK_IDENTIFIER)) {
        if (expr_match(parser, TOK_LPAREN)) {
            const AstDecl *fn = find_function(parser->scope->program, token.text);
            size_t arg_count = 0;
            if (!fn) {
                expr_diag(parser, token, "unknown function call");
            }
            if (parser->current.kind != TOK_RPAREN) {
                for (;;) {
                    const ExprType arg = parse_expression(parser);
                    if (fn && arg_count < fn->param_count && arg.valid) {
                        require_type_compatible(parser,
                                                token,
                                                fn->params[arg_count].type,
                                                arg.type,
                                                "function argument type mismatch");
                    }
                    arg_count++;
                    if (!expr_match(parser, TOK_COMMA)) {
                        break;
                    }
                }
            }
            if (!expr_match(parser, TOK_RPAREN)) {
                expr_diag(parser, parser->current, "expected ')' after call arguments");
            }
            if (fn && arg_count != fn->param_count) {
                expr_diag(parser, token, "function call arity mismatch");
            }
            return fn ? expr_type_ok(fn->return_type) : expr_type_error();
        }
        const AstType* local_type = find_local_type(parser->scope, token.text);
        if (local_type) {
            return expr_type_ok(*local_type);
        }
        const AstType* param_type = find_param_type(parser->scope, token.text);
        if (param_type) {
            return expr_type_ok(*param_type);
        }
        expr_diag(parser, token, "unknown identifier");
        return expr_type_error();
    }
    expr_diag(parser, token, "expected expression");
    expr_advance(parser);
    return expr_type_error();
}

static ExprType parse_postfix(ExprParser *parser) {
    ExprType left = parse_primary(parser);
    while (parser->current.kind == TOK_DOT || parser->current.kind == TOK_LBRACKET) {
        if (expr_match(parser, TOK_DOT)) {
            if (!expr_match(parser, TOK_IDENTIFIER)) {
                expr_diag(parser, parser->current, "expected field name after '.'");
            }
            left = expr_type_error();
        } else if (expr_match(parser, TOK_LBRACKET)) {
            const ExprType index = parse_expression(parser);
            if (index.valid && !is_numeric_type(index.type)) {
                expr_diag(parser, parser->current, "index expression must be numeric");
            }
            if (!expr_match(parser, TOK_RBRACKET)) {
                expr_diag(parser, parser->current, "expected ']' after index");
            }
            left = expr_type_error();
        }
    }
    return left;
}

static ExprType parse_unary(ExprParser *parser) {
    const Token token = parser->current;
    if (expr_match(parser, TOK_BANG)) {
        const ExprType right = parse_unary(parser);
        if (right.valid && !type_is(right.type.text, "bool")) {
            expr_diag(parser, token, "operator '!' requires bool");
        }
        return expr_type_ok(type_from_cstr("bool"));
    }
    if (expr_match(parser, TOK_MINUS)) {
        const ExprType right = parse_unary(parser);
        if (right.valid && !is_numeric_type(right.type)) {
            expr_diag(parser, token, "unary '-' requires a numeric value");
        }
        return right;
    }
    return parse_postfix(parser);
}

static ExprType parse_factor(ExprParser *parser) {
    ExprType left = parse_unary(parser);
    while (parser->current.kind == TOK_STAR ||
           parser->current.kind == TOK_SLASH ||
           parser->current.kind == TOK_PERCENT) {
        const Token op = parser->current;
        expr_advance(parser);
        ExprType right = parse_unary(parser);
        if (left.valid && right.valid && (!is_numeric_type(left.type) || !type_equal(left.type, right.type))) {
            expr_diag(parser, op, "binary arithmetic operands must have the same numeric type");
        }
        if (!left.valid || !right.valid) {
            left = expr_type_error();
        }
    }
    return left;
}

static ExprType parse_term(ExprParser *parser) {
    ExprType left = parse_factor(parser);
    while (parser->current.kind == TOK_PLUS ||
           parser->current.kind == TOK_MINUS) {
        const Token op = parser->current;
        expr_advance(parser);
        ExprType right = parse_factor(parser);
        if (left.valid && right.valid) {
            if (type_is(left.type.text, "string") && type_is(right.type.text, "string") && op.kind == TOK_PLUS) {
            } else if (!is_numeric_type(left.type) || !type_equal(left.type, right.type)) {
                expr_diag(parser, op, "binary arithmetic operands must have the same numeric type");
            }
        }
        if (!left.valid || !right.valid) {
            left = expr_type_error();
        }
    }
    return left;
}

static ExprType parse_comparison(ExprParser *parser) {
    ExprType left = parse_term(parser);
    while (parser->current.kind == TOK_LT ||
           parser->current.kind == TOK_LTE ||
           parser->current.kind == TOK_GT ||
           parser->current.kind == TOK_GTE) {
        const Token op = parser->current;
        expr_advance(parser);
        ExprType right = parse_term(parser);
        if (left.valid && right.valid && (!is_numeric_type(left.type) || !type_equal(left.type, right.type))) {
            expr_diag(parser, op, "comparison operands must have the same numeric type");
        }
        left = (left.valid && right.valid) ? expr_type_ok(type_from_cstr("bool")) : expr_type_error();
    }
    return left;
}

static ExprType parse_equality(ExprParser *parser) {
    ExprType left = parse_comparison(parser);
    while (parser->current.kind == TOK_EQUAL_EQUAL ||
           parser->current.kind == TOK_BANG_EQUAL) {
        const Token op = parser->current;
        expr_advance(parser);
        ExprType right = parse_comparison(parser);
        if (left.valid && right.valid && !type_equal(left.type, right.type)) {
            expr_diag(parser, op, "equality operands must have the same type");
        }
        left = (left.valid && right.valid) ? expr_type_ok(type_from_cstr("bool")) : expr_type_error();
    }
    return left;
}

static ExprType parse_and(ExprParser *parser) {
    ExprType left = parse_equality(parser);
    while (parser->current.kind == TOK_AMP_AMP) {
        const Token op = parser->current;
        expr_advance(parser);
        ExprType right = parse_equality(parser);
        if (left.valid && !type_is(left.type.text, "bool")) {
            expr_diag(parser, op, "operator '&&' requires bool operands");
        }
        if (right.valid && !type_is(right.type.text, "bool")) {
            expr_diag(parser, op, "operator '&&' requires bool operands");
        }
        left = (left.valid && right.valid) ? expr_type_ok(type_from_cstr("bool")) : expr_type_error();
    }
    return left;
}

static ExprType parse_or(ExprParser *parser) {
    ExprType left = parse_and(parser);
    while (parser->current.kind == TOK_PIPE_PIPE) {
        const Token op = parser->current;
        expr_advance(parser);
        ExprType right = parse_and(parser);
        if (left.valid && !type_is(left.type.text, "bool")) {
            expr_diag(parser, op, "operator '||' requires bool operands");
        }
        if (right.valid && !type_is(right.type.text, "bool")) {
            expr_diag(parser, op, "operator '||' requires bool operands");
        }
        left = (left.valid && right.valid) ? expr_type_ok(type_from_cstr("bool")) : expr_type_error();
    }
    return left;
}

static ExprType parse_assignment(ExprParser *parser) {
    const ExprType left = parse_or(parser);
    if (parser->current.kind == TOK_EQUAL) {
        const Token op = parser->current;
        expr_advance(parser);
        ExprType right = parse_assignment(parser);
        if (left.valid && right.valid && !type_equal(left.type, right.type)) {
            expr_diag(parser, op, "assignment value type mismatch");
        }
        return right.valid ? right : expr_type_error();
    }
    return left;
}

static ExprType parse_expression(ExprParser *parser) {
    return parse_assignment(parser);
}

static ExprType check_expr(CheckScope *scope, const AstStmt *stmt, const TekoString expr) {
    ExprParser parser;
    memset(&parser, 0, sizeof(parser));
    parser.scope = scope;
    parser.stmt = stmt;
    parser.base_offset = offset_from_string(stmt->location.source, expr);
    parser.source.path = scope->function->source ? scope->function->source->path : teko_string_from_cstr("<expr>");
    parser.source.type_name = teko_string_from_cstr("<expr>");
    parser.source.kind = TEKO_SOURCE_TEKO;
    parser.source.text = expr;
    teko_lexer_init(&parser.lexer, &parser.source);
    expr_advance(&parser);
    ExprType result = parse_expression(&parser);
    if (parser.current.kind != TOK_EOF) {
        expr_diag(&parser, parser.current, "unexpected token in expression");
        return expr_type_error();
    }
    return result;
}

static void check_block(CheckScope *scope, const AstBlock *block);

static void check_stmt(CheckScope *scope, const AstStmt *stmt) {
    if (stmt->kind == AST_STMT_RETURN) {
        const int returns_void = type_is(scope->function->return_type.text, "void");
        ExprType value = expr_type_error();
        scope->saw_return = 1;
        if (returns_void && stmt->expr.length) {
            diag(scope, stmt, "void function cannot return a value");
        }
        if (!returns_void && !stmt->expr.length) {
            diag(scope, stmt, "non-void function must return a value");
        }
        if (stmt->expr.length) {
            value = check_expr(scope, stmt, stmt->expr);
            if (!returns_void && value.valid && !type_equal(scope->function->return_type, value.type)) {
                diag(scope, stmt, "return value type mismatch");
            }
        }
    } else if (stmt->kind == AST_STMT_IF) {
        const ExprType condition = check_expr(scope, stmt, stmt->condition);
        if (condition.valid && !type_is(condition.type.text, "bool")) {
            diag(scope, stmt, "if condition must be bool");
        }
        check_block(scope, &stmt->then_block);
        check_block(scope, &stmt->else_block);
    } else if (stmt->kind == AST_STMT_WHILE) {
        const ExprType condition = check_expr(scope, stmt, stmt->condition);
        if (condition.valid && !type_is(condition.type.text, "bool")) {
            diag(scope, stmt, "while condition must be bool");
        }
        check_block(scope, &stmt->then_block);
    } else if (stmt->kind == AST_STMT_LOCAL) {
        ExprType value = expr_type_error();
        int local_type_ok = 0;
        if (!stmt->type.text.length) {
            diag(scope, stmt, "Core 0 locals require an explicit type");
        } else {
            AstLocation type_location = stmt->location;
            type_location.offset = offset_from_string(stmt->location.source, stmt->type.text);
            line_column_for_offset(type_location.source, type_location.offset, &type_location.line, &type_location.column);
            local_type_ok = check_type(scope->ctx, scope->program, scope->function, stmt->type, type_location, 0);
        }
        if (stmt->expr.length) {
            value = check_expr(scope, stmt, stmt->expr);
            if (local_type_ok && value.valid && !type_equal(stmt->type, value.type)) {
                diag(scope, stmt, "local initializer type mismatch");
            }
        }
        if (local_exists(scope, stmt->name)) {
            diag(scope, stmt, "duplicate local declaration");
        } else {
            add_local(scope, stmt->name, stmt->type);
        }
    } else if (stmt->kind == AST_STMT_EXPR) {
        check_expr(scope, stmt, stmt->expr);
    }
}

static void check_block(CheckScope *scope, const AstBlock *block) {
    for (size_t i = 0; i < block->count; i++) {
        check_stmt(scope, &block->items[i]);
    }
}

static void check_function(TekoContext *ctx, const AstProgram *program, const AstDecl *decl) {
    CheckScope scope;
    memset(&scope, 0, sizeof(scope));
    scope.ctx = ctx;
    scope.program = program;
    scope.function = decl;

    {
        AstLocation return_location = decl->location;
        return_location.offset = offset_from_string(decl->source, decl->return_type.text);
        line_column_for_offset(return_location.source, return_location.offset, &return_location.line, &return_location.column);
        check_type(ctx, program, decl, decl->return_type, return_location, 1);
    }
    for (size_t i = 0; i < decl->param_count; i++) {
        AstLocation param_type_location = decl->params[i].location;
        param_type_location.offset = offset_from_string(decl->source, decl->params[i].type.text);
        line_column_for_offset(param_type_location.source, param_type_location.offset, &param_type_location.line, &param_type_location.column);
        check_type(ctx, program, decl, decl->params[i].type, param_type_location, 0);
        if (i + 1 < decl->param_count) {
            for (size_t j = i + 1; j < decl->param_count; j++) {
                if (teko_string_equal(decl->params[i].name, decl->params[j].name)) {
                    diag_location(ctx, decl->params[j].location, "duplicate parameter name");
                }
            }
        }
    }

    check_block(&scope, &decl->body);
    if (!type_is(decl->return_type.text, "void") && !scope.saw_return) {
        diag_decl(ctx, decl, "non-void function must contain a return statement");
    }
    teko_free(ctx, scope.locals);
}

static void check_struct(TekoContext *ctx, const AstProgram *program, const AstDecl *decl) {
    for (size_t i = 0; i < decl->field_count; i++) {
        AstLocation field_type_location = decl->fields[i].location;
        field_type_location.offset = offset_from_string(decl->source, decl->fields[i].type.text);
        line_column_for_offset(field_type_location.source, field_type_location.offset, &field_type_location.line, &field_type_location.column);
        check_type(ctx, program, decl, decl->fields[i].type, field_type_location, 0);
        for (size_t j = i + 1; j < decl->field_count; j++) {
            if (teko_string_equal(decl->fields[i].name, decl->fields[j].name)) {
                diag_location(ctx, decl->fields[j].location, "duplicate struct field");
            }
        }
    }
}

static void check_enum(TekoContext *ctx, const AstDecl *decl) {
    for (size_t i = 0; i < decl->variant_count; i++) {
        for (size_t j = i + 1; j < decl->variant_count; j++) {
            if (teko_string_equal(decl->variants[i].name, decl->variants[j].name)) {
                diag_location(ctx, decl->variants[j].location, "duplicate enum variant");
            }
        }
    }
}

int teko_check_program(TekoContext *ctx, const AstProgram *program) {
    const size_t before = ctx->diagnostic_count;
    for (size_t i = 0; i < program->decl_count; i++) {
        const AstDecl *decl = &program->decls[i];
        if (decl->kind == AST_DECL_FUNCTION) {
            check_function(ctx, program, decl);
        } else if (decl->kind == AST_DECL_STRUCT) {
            check_struct(ctx, program, decl);
        } else if (decl->kind == AST_DECL_ENUM) {
            check_enum(ctx, decl);
        }
    }
    return ctx->diagnostic_count == before;
}
