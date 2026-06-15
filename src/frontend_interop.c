#include "frontend_interop.h"
#include "lexer.h"
#include "parser.h"
#include "parser_ffi.h"
#include <stdlib.h>
#include <string.h>

// Local token advance (parser.c's is static): current <- peek <- next.
static void fe_advance(Parser* p) {
    p->current_token = p->peek_token;
    p->peek_token = lexer_next_token(p->lexer);
}

// String-literal lexemes arrive with their surrounding delimiters ("…", `…`, '…').
// Return a malloc'd copy with one matching leading/trailing delimiter removed.
static char* strip_quotes(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    if (len >= 2) {
        char q = s[0];
        if ((q == '"' || q == '`' || q == '\'') && s[len - 1] == q) {
            char* out = (char*)malloc(len - 1);
            memcpy(out, s + 1, len - 2);
            out[len - 2] = '\0';
            return out;
        }
    }
    return strdup(s);
}

// Teko-visible callee name -> import-table index.
typedef struct { char* name; int idx; } ImportBinding;

static void bind_add(ImportBinding** binds, int* n, int* cap, const char* name, int idx) {
    if (!name) return;
    if (*n >= *cap) {
        *cap = (*cap == 0) ? 8 : (*cap * 2);
        *binds = (ImportBinding*)realloc(*binds, sizeof(ImportBinding) * (*cap));
    }
    (*binds)[*n].name = strdup(name);
    (*binds)[*n].idx = idx;
    (*n)++;
}

static int bind_lookup(ImportBinding* binds, int n, const char* name) {
    for (int i = 0; i < n; i++) {
        if (strcmp(binds[i].name, name) == 0) return binds[i].idx;
    }
    return -1;
}

// Register one extern function as an import + a name->index binding.
static void register_extern_fn(BytecodeBuffer* buffer, const char* from_lib,
                               const FFIFunctionNode* fn,
                               ImportBinding** binds, int* nb, int* capb) {
    if (!fn || !fn->fn_name) return;
    // from_lib / alias are string-literal lexemes (quoted); the fn_name is a bare
    // identifier. Strip delimiters off the literals before they become ns/name.
    char* ns = from_lib ? strip_quotes(from_lib) : strdup("env");
    char* name = fn->alias ? strip_quotes(fn->alias) : strdup(fn->fn_name);
    int has_result = (fn->return_type && strcmp(fn->return_type, "void") != 0) ? 1 : 0;
    int idx = codegen_li_add_import(buffer, ns, name, fn->param_count, has_result);
    bind_add(binds, nb, capb, fn->fn_name, idx);
    free(ns);
    free(name);
}

static void register_extern(BytecodeBuffer* buffer, const FFIASTNode* node,
                            ImportBinding** binds, int* nb, int* capb) {
    if (!node) return;
    if (node->type == NODE_FFI_FUNCTION) {
        register_extern_fn(buffer, node->from_lib, &node->data.ffi_function, binds, nb, capb);
    } else if (node->type == NODE_FFI_BLOCK) {
        for (int i = 0; i < node->data.ffi_block.function_count; i++) {
            const FFIASTNode* inner = node->data.ffi_block.functions[i];
            if (inner && inner->type == NODE_FFI_FUNCTION) {
                // Block-level `from "ns"` applies to the inner functions.
                register_extern_fn(buffer, node->from_lib, &inner->data.ffi_function, binds, nb, capb);
            }
        }
    }
}

// A literal call argument.
typedef struct { int is_string; char* sval; int ival; } CallArg;

// Lower a resolved call: args 0..n-2 staged via OP_SETARG, the last left in $w0,
// then OP_CALL_IMPORT. String args are pooled; int args are immediates.
static void lower_call(BytecodeBuffer* buffer, int import_index, CallArg* args, int n) {
    for (int i = 0; i < n; i++) {
        if (args[i].is_string) {
            int s = codegen_li_add_string_constant(buffer, args[i].sval);
            codegen_li_emit_sconst(buffer, s);
        } else {
            codegen_li_emit_iconst(buffer, args[i].ival);
        }
        if (i != n - 1) codegen_li_emit_setarg(buffer, i);
    }
    codegen_li_emit_call_import(buffer, import_index);
}

int teko_compile_interop(const char* source, BytecodeBuffer* buffer) {
    if (!source || !buffer) return 1;

    Lexer lexer;
    lexer_init(&lexer, source);
    Parser parser;
    parser_init(&parser, &lexer);

    ImportBinding* binds = NULL;
    int nb = 0, capb = 0;

    while (parser.current_token.type != TOKEN_EOF) {
        if (parser.current_token.type == TOKEN_EXTERN) {
            FFIASTNode* node = parse_extern_declaration(&parser);
            if (node) {
                register_extern(buffer, node, &binds, &nb, &capb);
                free_ffi_ast_node(node);
            }
        } else if (parser.current_token.type == TOKEN_IDENTIFIER &&
                   parser.peek_token.type == TOKEN_LPAREN) {
            // Top-level call statement: NAME ( arg, … )
            char* callee = strdup(parser.current_token.lexeme);
            fe_advance(&parser); // consume NAME
            fe_advance(&parser); // consume '('

            CallArg args[16];
            int nargs = 0;
            while (parser.current_token.type != TOKEN_RPAREN &&
                   parser.current_token.type != TOKEN_EOF) {
                if (nargs < 16 &&
                    (parser.current_token.type == TOKEN_LIT_STR ||
                     parser.current_token.type == TOKEN_STRING_LIT)) {
                    args[nargs].is_string = 1;
                    args[nargs].sval = strip_quotes(parser.current_token.lexeme);
                    args[nargs].ival = 0;
                    nargs++;
                    fe_advance(&parser);
                } else if (nargs < 16 && parser.current_token.type == TOKEN_LIT_INT) {
                    args[nargs].is_string = 0;
                    args[nargs].sval = NULL;
                    args[nargs].ival = atoi(parser.current_token.lexeme);
                    nargs++;
                    fe_advance(&parser);
                } else if (parser.current_token.type == TOKEN_COMMA) {
                    fe_advance(&parser);
                } else {
                    fe_advance(&parser); // skip anything else in the interop subset
                }
            }
            if (parser.current_token.type == TOKEN_RPAREN) fe_advance(&parser);
            if (parser.current_token.type == TOKEN_SEMICOLON) fe_advance(&parser);

            int idx = bind_lookup(binds, nb, callee);
            if (idx >= 0) lower_call(buffer, idx, args, nargs);

            for (int i = 0; i < nargs; i++) if (args[i].sval) free(args[i].sval);
            free(callee);
        } else {
            fe_advance(&parser);
        }
    }

    codegen_li_emit_halt(buffer); // close main

    for (int i = 0; i < nb; i++) free(binds[i].name);
    free(binds);
    return 0;
}
