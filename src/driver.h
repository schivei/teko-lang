// src/driver.h — the Teko bootstrap DRIVER (F1: path-to-first-binary).
//
// Wires the front-end pipeline end-to-end: read → lex (tk_tokenize) → parse
// (tk_parse_main_file / tk_parse_module) → reconcile (file model → tk_program) →
// check (tk_type_program). Codegen/emit is F2 — success here means "type-checked".
#ifndef TK_DRIVER_H
#define TK_DRIVER_H

#include "core.h"            // tk_error, TK_RESULT
#include "text/text.h"       // tk_str, tk_str_result
#include "parser/ast.h"      // tk_main_file, tk_module, tk_program

// B1a — the IO boundary. Reads the whole file at `path` into a freshly heap-allocated
// `tk_str` (UTF-8 validated on the way in). This is the bootstrap's ONE host-IO
// function (the unsafe host boundary — M.1, contained here). On failure returns a
// result with `.ok == false` and a clear error message.
tk_str_result tk_read_file(const char *path);

// B1c — R-main reconciliation. Flatten the parsed file model into a flat `tk_item`
// list (`tk_program`) the checker consumes.
//   * MainFile: each `use` → TK_ITEM_USE; each virtual-main statement → TK_ITEM_STATEMENT.
//   * Module:   each `use` → TK_ITEM_USE; each decl → TK_ITEM_FUNCTION / TK_ITEM_TYPE_DECL.
tk_program tk_main_file_to_program(tk_main_file mf);
tk_program tk_module_to_program(tk_module m);

// B1d — the driver. read → lex → parse → reconcile → check for the file at `path`.
// The parse entry is chosen by basename: `main.tks` → main-file parser, else module.
// Returns 0 on success (prints a concise OK line to stdout); non-zero on any stage
// failure (prints `teko: <path>: <message>` to stderr).
int tk_compile(const char *path);

// A3 — the PROJECT entry path. Given a project directory `dir`, read `<dir>/.tkp`
// (tk_parse_manifest), discover the source tree (tk_discover), assemble all files into
// one merged tk_program (tk_assemble), and type-check it whole (tk_type_program — M.1).
// Reports OK / the first error. This is the multi-file analogue of tk_compile; it stops
// at "type-checked" (multi-namespace native codegen is a later crumb). Returns 0 on a
// clean check. main() dispatches here when its argument is a directory or a `.tkp`.
int tk_compile_project(const char *dir);

// Eixo D — the VM run path (debug profile). read → lex → parse → reconcile → check, then
// INTERPRET the checked typed tree on the VM (tk_vm_run) instead of codegen. No .tkb, no
// cc. The process exit code is the virtual-main's (early `return n` → n, default 0); a
// panic (÷0 / impossible cast / failed assert) goes to stderr with a non-zero exit.
int tk_run(const char *path);

#endif // TK_DRIVER_H
