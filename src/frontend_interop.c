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

// --- @dom/@js intrinsics (FE-E) -------------------------------------------------
// A `@dom.method(args)` / `@js.method(args)` call auto-registers a host import in the
// `dom`/`js` namespace and lowers like an extern call, with two refinements:
//   • a string argument expands to TWO wasm params (ptr, len) — the (ptr,len) ABI the
//     dom.* glue marshals — so n_params is computed from the args, and
//   • the FIRST argument may be a nested `@dom.…()` call (its result handle feeds the
//     outer call), which covers e.g. setText(getElementById("out"), "…").
// Only the leading arg may be nested (one level of staging-slot reuse is safe); other
// args must be string/int literals. The call result (a handle, when any) lands in $w0.

// Which dom/js intrinsics return a value (an i32 handle).
static int intrinsic_has_result(const char* method) {
    return (strcmp(method, "getElementById") == 0 ||
            strcmp(method, "createElement") == 0) ? 1 : 0;
}

// A flat literal "producer" (one wasm param): a pooled string ptr, a string length,
// or an integer immediate.
typedef struct { int is_sconst; int payload; } Prod; // is_sconst: 1=SCONST pool idx, 0=ICONST val

static void lower_intrinsic_call(BytecodeBuffer* buffer, Parser* p); // recursive

static void lower_intrinsic_call(BytecodeBuffer* buffer, Parser* p) {
    // current token is the MACRO_IDENT, e.g. "@dom.setText". Split into ns + method.
    char full[128];
    strncpy(full, p->current_token.lexeme, sizeof(full) - 1);
    full[sizeof(full) - 1] = '\0';
    char* dot = strchr(full, '.');
    if (full[0] != '@' || !dot) { fe_advance(p); return; }
    char ns[32];
    size_t nslen = (size_t)(dot - (full + 1));
    if (nslen >= sizeof(ns)) nslen = sizeof(ns) - 1;
    memcpy(ns, full + 1, nslen);
    ns[nslen] = '\0';
    char method[96];
    strncpy(method, dot + 1, sizeof(method) - 1);
    method[sizeof(method) - 1] = '\0';

    fe_advance(p); // consume the macro ident
    if (p->current_token.type != TOKEN_LPAREN) return;
    fe_advance(p); // consume '('

    // Optional leading nested intrinsic call as arg0 (its result -> $w0).
    int has_nested = 0;
    if (p->current_token.type == TOKEN_MACRO_IDENT) {
        lower_intrinsic_call(buffer, p); // emits inner; result in $w0
        has_nested = 1;
        if (p->current_token.type == TOKEN_COMMA) fe_advance(p);
    }

    // Collect the remaining literal args, expanded to flat producers (string -> ptr+len).
    Prod prods[32];
    int np = 0;
    while (p->current_token.type != TOKEN_RPAREN && p->current_token.type != TOKEN_EOF) {
        if ((p->current_token.type == TOKEN_LIT_STR || p->current_token.type == TOKEN_STRING_LIT) && np + 2 <= 32) {
            char* s = strip_quotes(p->current_token.lexeme);
            int idx = codegen_li_add_string_constant(buffer, s);
            int len = (int)strlen(s);
            free(s);
            prods[np].is_sconst = 1; prods[np].payload = idx; np++;   // ptr
            prods[np].is_sconst = 0; prods[np].payload = len; np++;   // len
            fe_advance(p);
        } else if (p->current_token.type == TOKEN_LIT_INT && np + 1 <= 32) {
            prods[np].is_sconst = 0; prods[np].payload = atoi(p->current_token.lexeme); np++;
            fe_advance(p);
        } else if (p->current_token.type == TOKEN_COMMA) {
            fe_advance(p);
        } else {
            fe_advance(p); // skip unsupported tokens in this subset
        }
    }
    if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);
    if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);

    int total = (has_nested ? 1 : 0) + np;

    // Stage the nested result (param 0) unless it is the only/last param.
    if (has_nested && total > 1) codegen_li_emit_setarg(buffer, 0);

    // Emit the literal producers at absolute slots [has_nested .. total-1]; the last
    // param stays in $w0.
    for (int k = 0; k < np; k++) {
        int slot = (has_nested ? 1 : 0) + k;
        if (prods[k].is_sconst) codegen_li_emit_sconst(buffer, prods[k].payload);
        else codegen_li_emit_iconst(buffer, prods[k].payload);
        if (slot < total - 1) codegen_li_emit_setarg(buffer, slot);
    }

    int import_index = codegen_li_add_import(buffer, ns, method, total, intrinsic_has_result(method));
    codegen_li_emit_call_import(buffer, import_index);
}

static int is_dom_macro(const char* lexeme) {
    return lexeme && (strncmp(lexeme, "@dom.", 5) == 0 || strncmp(lexeme, "@js.", 4) == 0);
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
        } else if (parser.current_token.type == TOKEN_MACRO_IDENT &&
                   is_dom_macro(parser.current_token.lexeme) &&
                   parser.peek_token.type == TOKEN_LPAREN) {
            // Top-level @dom/@js intrinsic call statement.
            lower_intrinsic_call(buffer, &parser);
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
