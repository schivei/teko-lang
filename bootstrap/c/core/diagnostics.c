#include "teko_internal.h"

#include <stdlib.h>
#include <string.h>

void *teko_alloc(TekoContext *ctx, const size_t size) {
    if (!ctx || !ctx->allocator.alloc) {
        return 0;
    }
    return ctx->allocator.alloc(ctx->allocator.user, size);
}

void *teko_realloc(TekoContext *ctx, void *ptr, const size_t size) {
    if (!ctx || !ctx->allocator.realloc) {
        return 0;
    }
    return ctx->allocator.realloc(ctx->allocator.user, ptr, size);
}

void teko_free(const TekoContext *ctx, void *ptr) {
    if (ptr && ctx && ctx->allocator.free) {
        ctx->allocator.free(ctx->allocator.user, ptr);
    }
}

void teko_add_diagnostic(TekoContext *ctx,
                         const TekoDiagnosticSeverity severity,
                         const TekoSource *source,
                         const size_t offset,
                         const unsigned line,
                         const unsigned column,
                         const char *message) {
    if (ctx->diagnostic_count == ctx->diagnostic_capacity) {
        const size_t next = ctx->diagnostic_capacity ? ctx->diagnostic_capacity * 2 : 8;
        ctx->diagnostics = (TekoDiagnostic *)teko_realloc(ctx, ctx->diagnostics, next * sizeof(TekoDiagnostic));
        ctx->diagnostic_capacity = next;
    }
    TekoDiagnostic* diag = &ctx->diagnostics[ctx->diagnostic_count++];
    diag->severity = severity;
    diag->path = source ? source->path : teko_string_from_cstr("");
    diag->offset = offset;
    diag->line = line;
    diag->column = column;
    size_t len = strlen(message);
    char* copy = (char*)teko_alloc(ctx, len + 1);
    if (copy) {
        memcpy(copy, message, len + 1);
    }
    diag->message.data = copy;
    diag->message.length = len;
}
