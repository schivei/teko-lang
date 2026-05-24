#include "teko_internal.h"

static void append_ident(StringBuilder *sb, TekoString name) {
    size_t i;
    for (i = 0; i < name.length; i++) {
        char c = name.data[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
            sb_append_char(sb, c);
        } else {
            sb_append_char(sb, '_');
        }
    }
}

static void append_type(StringBuilder *sb, AstType type) {
    TekoString t = teko_string_trim(type.text);
    if (t.length == 0 || teko_string_equal_cstr(t, "void")) sb_append(sb, "void");
    else if (teko_string_equal_cstr(t, "bool")) sb_append(sb, "bool");
    else if (teko_string_equal_cstr(t, "byte")) sb_append(sb, "uint8_t");
    else if (teko_string_equal_cstr(t, "int")) sb_append(sb, "int32_t");
    else if (teko_string_equal_cstr(t, "uint")) sb_append(sb, "uint32_t");
    else if (teko_string_equal_cstr(t, "long")) sb_append(sb, "int64_t");
    else if (teko_string_equal_cstr(t, "ulong")) sb_append(sb, "uint64_t");
    else if (teko_string_equal_cstr(t, "usize")) sb_append(sb, "size_t");
    else if (teko_string_equal_cstr(t, "isize")) sb_append(sb, "ptrdiff_t");
    else if (teko_string_equal_cstr(t, "string")) sb_append(sb, "const char *");
    else if (t.length > 5 && t.data[0] == 'p' && t.data[1] == 't' && t.data[2] == 'r' && t.data[3] == '<' && t.data[t.length - 1] == '>') {
        AstType inner;
        inner.text = teko_string_slice(t, 4, t.length - 5);
        append_type(sb, inner);
        sb_append(sb, " *");
    } else {
        append_ident(sb, t);
    }
}

static void indent(StringBuilder *sb, int level) {
    int i;
    for (i = 0; i < level; i++) sb_append(sb, "    ");
}

static void append_expr(StringBuilder *sb, TekoString expr) {
    sb_append_string(sb, teko_string_trim(expr));
}

static void emit_block(StringBuilder *sb, const AstBlock *block, int level);

static void emit_stmt(StringBuilder *sb, const AstStmt *stmt, int level) {
    indent(sb, level);
    if (stmt->kind == AST_STMT_RETURN) {
        sb_append(sb, "return");
        if (stmt->expr.length) {
            sb_append_char(sb, ' ');
            append_expr(sb, stmt->expr);
        }
        sb_append(sb, ";\n");
    } else if (stmt->kind == AST_STMT_IF) {
        sb_append(sb, "if (");
        append_expr(sb, stmt->condition);
        sb_append(sb, ") ");
        emit_block(sb, &stmt->then_block, level);
        if (stmt->else_block.count) {
            indent(sb, level);
            sb_append(sb, "else ");
            emit_block(sb, &stmt->else_block, level);
        }
    } else if (stmt->kind == AST_STMT_WHILE) {
        sb_append(sb, "while (");
        append_expr(sb, stmt->condition);
        sb_append(sb, ") ");
        emit_block(sb, &stmt->then_block, level);
    } else if (stmt->kind == AST_STMT_LOCAL) {
        if (stmt->type.text.length) append_type(sb, stmt->type);
        else sb_append(sb, "auto /* teko-inferred */");
        sb_append_char(sb, ' ');
        append_ident(sb, stmt->name);
        if (stmt->expr.length) {
            sb_append(sb, " = ");
            append_expr(sb, stmt->expr);
        }
        sb_append(sb, ";\n");
    } else {
        append_expr(sb, stmt->expr);
        sb_append(sb, ";\n");
    }
}

static void emit_block(StringBuilder *sb, const AstBlock *block, int level) {
    size_t i;
    sb_append(sb, "{\n");
    for (i = 0; i < block->count; i++) {
        emit_stmt(sb, &block->items[i], level + 1);
    }
    indent(sb, level);
    sb_append(sb, "}\n");
}

static void emit_struct(StringBuilder *sb, const AstDecl *decl) {
    size_t i;
    sb_append(sb, "typedef struct ");
    append_ident(sb, decl->name);
    sb_append(sb, " {\n");
    for (i = 0; i < decl->field_count; i++) {
        sb_append(sb, "    ");
        append_type(sb, decl->fields[i].type);
        sb_append_char(sb, ' ');
        append_ident(sb, decl->fields[i].name);
        sb_append(sb, ";\n");
    }
    sb_append(sb, "} ");
    append_ident(sb, decl->name);
    sb_append(sb, ";\n\n");
}

static void emit_enum(StringBuilder *sb, const AstDecl *decl) {
    size_t i;
    sb_append(sb, "typedef enum ");
    append_ident(sb, decl->name);
    sb_append(sb, " {\n");
    for (i = 0; i < decl->variant_count; i++) {
        sb_append(sb, "    ");
        append_ident(sb, decl->name);
        sb_append(sb, "_");
        append_ident(sb, decl->variants[i].name);
        if (i + 1 < decl->variant_count) sb_append_char(sb, ',');
        sb_append_char(sb, '\n');
    }
    sb_append(sb, "} ");
    append_ident(sb, decl->name);
    sb_append(sb, ";\n\n");
}

static void emit_function(StringBuilder *sb, const AstDecl *decl) {
    size_t i;
    append_type(sb, decl->return_type);
    sb_append_char(sb, ' ');
    append_ident(sb, decl->name);
    sb_append_char(sb, '(');
    if (decl->param_count == 0) {
        sb_append(sb, "void");
    }
    for (i = 0; i < decl->param_count; i++) {
        if (i) sb_append(sb, ", ");
        append_type(sb, decl->params[i].type);
        sb_append_char(sb, ' ');
        append_ident(sb, decl->params[i].name);
    }
    sb_append(sb, ") ");
    emit_block(sb, &decl->body, 0);
    sb_append_char(sb, '\n');
}

int teko_backend_emit_c(TekoContext *ctx, const AstProgram *program, TekoOutput *output) {
    StringBuilder sb;
    size_t i;
    sb_init(&sb, ctx);
    sb_append(&sb, "/* Generated by teko bootstrap C compiler. */\n");
    sb_append(&sb, "#include <stddef.h>\n#include <stdint.h>\n#include <stdbool.h>\n\n");
    for (i = 0; i < program->decl_count; i++) {
        if (program->decls[i].kind == AST_DECL_STRUCT) emit_struct(&sb, &program->decls[i]);
        else if (program->decls[i].kind == AST_DECL_ENUM) emit_enum(&sb, &program->decls[i]);
    }
    for (i = 0; i < program->decl_count; i++) {
        if (program->decls[i].kind == AST_DECL_FUNCTION) emit_function(&sb, &program->decls[i]);
    }
    output->kind = TEKO_OUTPUT_C_SOURCE;
    output->path = teko_string_from_cstr("out.c");
    output->text = sb_build(&sb);
    return 1;
}
