#include "teko_internal.h"

#include <stdlib.h>
#include <string.h>

static void *default_alloc(void *user, size_t size) {
    (void)user;
    return malloc(size);
}

static void *default_realloc(void *user, void *ptr, size_t size) {
    (void)user;
    return realloc(ptr, size);
}

static void default_free(void *user, void *ptr) {
    (void)user;
    free(ptr);
}

static TekoAllocator normalized_allocator(const TekoCompileOptions *options) {
    TekoAllocator allocator;
    if (options && options->allocator.alloc && options->allocator.realloc && options->allocator.free) {
        return options->allocator;
    }
    allocator.user = 0;
    allocator.alloc = default_alloc;
    allocator.realloc = default_realloc;
    allocator.free = default_free;
    return allocator;
}

void teko_compile_sources(const TekoSource *sources,
                          const size_t source_count,
                          const TekoCompileOptions *options,
                          TekoCompileResult *result) {
    TekoContext ctx;
    AstProgram program;
    TekoOutput output;
    memset(result, 0, sizeof(*result));
    memset(&ctx, 0, sizeof(ctx));
    memset(&program, 0, sizeof(program));
    memset(&output, 0, sizeof(output));
    ctx.allocator = normalized_allocator(options);

    if (!sources || source_count == 0) {
        teko_add_diagnostic(&ctx, TEKO_DIAG_ERROR, 0, 0, 0, 0, "no sources provided");
    } else if (teko_parse_sources(&ctx, sources, source_count, &program) &&
               teko_check_program(&ctx, &program)) {
        result->outputs = (TekoOutput *)teko_alloc(&ctx, sizeof(TekoOutput));
        if (result->outputs && teko_backend_emit_c(&ctx, &program, &output)) {
            result->outputs[0] = output;
            result->output_count = 1;
            result->success = 1;
        }
    }

    result->diagnostics = ctx.diagnostics;
    result->diagnostic_count = ctx.diagnostic_count;
    if (ctx.diagnostic_count) {
        result->success = 0;
    }
    teko_free_program(&ctx, &program);
}

void teko_free_compile_result(const TekoCompileOptions *options,
                              TekoCompileResult *result) {
    const TekoAllocator allocator = normalized_allocator(options);
    TekoContext ctx;
    size_t i;
    memset(&ctx, 0, sizeof(ctx));
    ctx.allocator = allocator;
    for (i = 0; i < result->diagnostic_count; i++) {
        teko_free(&ctx, (void *)result->diagnostics[i].message.data);
    }
    for (i = 0; i < result->output_count; i++) {
        teko_free(&ctx, (void *)result->outputs[i].text.data);
    }
    teko_free(&ctx, result->diagnostics);
    teko_free(&ctx, result->outputs);
    memset(result, 0, sizeof(*result));
}
