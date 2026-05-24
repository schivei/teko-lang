#ifndef TEKO_CORE_H
#define TEKO_CORE_H

#include <stddef.h>

typedef struct TekoString {
    const char *data;
    size_t length;
} TekoString;

typedef enum TekoSourceKind {
    TEKO_SOURCE_TEKO,
    TEKO_SOURCE_STRUCT,
    TEKO_SOURCE_ENUM,
    TEKO_SOURCE_STATIC,
    TEKO_SOURCE_UNKNOWN
} TekoSourceKind;

typedef struct TekoSource {
    TekoString path;
    TekoString type_name;
    TekoSourceKind kind;
    TekoString text;
} TekoSource;

typedef enum TekoDiagnosticSeverity {
    TEKO_DIAG_ERROR,
    TEKO_DIAG_WARNING
} TekoDiagnosticSeverity;

typedef struct TekoDiagnostic {
    TekoDiagnosticSeverity severity;
    TekoString path;
    size_t offset;
    unsigned line;
    unsigned column;
    TekoString message;
} TekoDiagnostic;

typedef enum TekoOutputKind {
    TEKO_OUTPUT_C_SOURCE
} TekoOutputKind;

typedef struct TekoOutput {
    TekoOutputKind kind;
    TekoString path;
    TekoString text;
} TekoOutput;

typedef void *(*TekoAllocFn)(void *user, size_t size);
typedef void *(*TekoReallocFn)(void *user, void *ptr, size_t size);
typedef void (*TekoFreeFn)(void *user, void *ptr);

typedef struct TekoAllocator {
    void *user;
    TekoAllocFn alloc;
    TekoReallocFn realloc;
    TekoFreeFn free;
} TekoAllocator;

typedef struct TekoCompileOptions {
    TekoAllocator allocator;
} TekoCompileOptions;

typedef struct TekoCompileResult {
    int success;
    TekoOutput *outputs;
    size_t output_count;
    TekoDiagnostic *diagnostics;
    size_t diagnostic_count;
} TekoCompileResult;

void teko_compile_sources(const TekoSource *sources,
                          size_t source_count,
                          const TekoCompileOptions *options,
                          TekoCompileResult *result);

void teko_free_compile_result(const TekoCompileOptions *options,
                              TekoCompileResult *result);

TekoString teko_string_from_cstr(const char *text);
TekoSourceKind teko_source_kind_from_path(TekoString path);
TekoString teko_type_name_from_path(TekoString path);

#endif
