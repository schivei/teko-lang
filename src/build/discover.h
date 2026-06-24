// src/build/discover.h — teko::build: file-set enumeration + namespace mapping,
// C23 mirror of discover.tks. The SECOND step of project-based compilation (after
// the manifest read, A1): walk the source tree and map each `.tks`/`.tkt` to its
// namespace (LEGISLATION §181–188; namespaces = directories, source root invisible).
#ifndef TK_BUILD_DISCOVER_H
#define TK_BUILD_DISCOVER_H

#include "../core.h"        // TK_LIST, TK_RESULT, tk_error
#include "../text/text.h"   // tk_str

// a discovered source file. `path` / `namespace` are HEAP-OWNED, NUL-terminated-
// backed strings with process lifetime (arena-style — M.5); the str views point
// into those buffers. .tkt tests live beside the code they test (same namespace).
typedef struct {
    tk_str path;        // path on disk (e.g. "src/lexer/lexer.tks")
    tk_str namespace;   // canonical namespace (e.g. "teko::lexer")
} tk_source_file;

// a list of discovered source files — teko::list realized over tk_source_file.
TK_LIST(tk_source_file, tk_source_files);

// `[]SourceFile | error` — the result of discover.
TK_RESULT(tk_source_files, tk_source_files_result);

// tk_discover — walk the `source` dir recursively, collect every `.tks`/`.tkt`,
// and map each to its namespace. THE RULE: namespace = `name` + the file's
// directory path RELATIVE to `source`, segments joined by `::`; the `source` root
// is INVISIBLE (stripped). A file directly under `source` is the bare root `name`.
// (Walks only `source` — main.tks at the project root is out of scope.) The host
// filesystem is the contained IO boundary (M.1 — see discover.c). On failure (the
// source dir cannot be opened) → tk_error. Allocation failure aborts (M.5).
tk_source_files_result tk_discover(tk_str name, tk_str source);

#endif // TK_BUILD_DISCOVER_H
