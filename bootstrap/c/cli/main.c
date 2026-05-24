#include "../core/teko_core.h"
#include "../platform/filesystem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *cli_alloc(void *user, size_t size) {
    (void)user;
    return malloc(size);
}

static void *cli_realloc(void *user, void *ptr, size_t size) {
    (void)user;
    return realloc(ptr, size);
}

static void cli_free(void *user, void *ptr) {
    (void)user;
    free(ptr);
}

static TekoAllocator cli_allocator(void) {
    TekoAllocator allocator;
    allocator.user = 0;
    allocator.alloc = cli_alloc;
    allocator.realloc = cli_realloc;
    allocator.free = cli_free;
    return allocator;
}

static void usage(const char *argv0) {
    fprintf(stderr, "usage: %s [-o output.c] input.teko [More.struct.teko ...]\n", argv0);
}

static int streq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

static int load_sources(const int argc, char **argv, const int first_input, const TekoAllocator allocator, TekoSource **sources, size_t *source_count) {
    const size_t count = (size_t)(argc - first_input);
    TekoSource *items = (TekoSource *)allocator.alloc(allocator.user, count * sizeof(TekoSource));
    if (!items) return 0;
    memset(items, 0, count * sizeof(TekoSource));
    for (int i = first_input; i < argc; i++) {
        const TekoString path = teko_string_from_cstr(argv[i]);
        TekoSource *source = &items[i - first_input];
        source->path = path;
        source->type_name = teko_type_name_from_path(path);
        source->kind = teko_source_kind_from_path(path);
        if (!teko_platform_read_file(argv[i], allocator, &source->text)) {
            fprintf(stderr, "teko: failed to read '%s'\n", argv[i]);
            *sources = items;
            *source_count = (size_t)(i - first_input);
            return 0;
        }
    }
    *sources = items;
    *source_count = count;
    return 1;
}

static void free_sources(const TekoAllocator allocator, TekoSource *sources, const size_t source_count) {
    for (size_t i = 0; i < source_count; i++) {
        teko_platform_free_string(allocator, &sources[i].text);
    }
    allocator.free(allocator.user, sources);
}

static const TekoSource *find_source(const TekoSource *sources, const size_t source_count, const TekoString path) {
    for (size_t i = 0; i < source_count; i++) {
        if (sources[i].path.length == path.length &&
            memcmp(sources[i].path.data, path.data, path.length) == 0) {
            return &sources[i];
        }
    }
    return 0;
}

static void print_source_line(const TekoSource *source, const unsigned line, const unsigned column) {
    size_t start = 0;
    size_t end = 0;
    unsigned current_line = 1;
    size_t i;
    if (!source || line == 0) return;
    for (i = 0; i < source->text.length; i++) {
        if (current_line == line) {
            start = i;
            break;
        }
        if (source->text.data[i] == '\n') {
            current_line++;
        }
    }
    if (current_line != line) return;
    end = start;
    while (end < source->text.length && source->text.data[end] != '\n' && source->text.data[end] != '\r') {
        end++;
    }
    fprintf(stderr, "  %.*s\n", (int)(end - start), source->text.data + start);
    if (column > 0) {
        fprintf(stderr, "  ");
        for (i = 1; i < column; i++) {
            fputc(' ', stderr);
        }
        fprintf(stderr, "^\n");
    }
}

static void print_diagnostics(const TekoCompileResult *result, const TekoSource *sources, const size_t source_count) {
    for (size_t i = 0; i < result->diagnostic_count; i++) {
        const TekoDiagnostic *diag = &result->diagnostics[i];
        const char *severity = diag->severity == TEKO_DIAG_ERROR ? "error" : "warning";
        const TekoSource *source = find_source(sources, source_count, diag->path);
        fprintf(stderr, "%.*s:%u:%u: %s: %.*s\n",
                (int)diag->path.length, diag->path.data,
                diag->line, diag->column,
                severity,
                (int)diag->message.length, diag->message.data);
        print_source_line(source, diag->line, diag->column);
    }
}

int main(const int argc, char **argv) {
    const char *output_path = "out.c";
    int first_input = 1;
    const TekoAllocator allocator = cli_allocator();
    TekoSource *sources = 0;
    size_t source_count = 0;
    TekoCompileOptions options;
    TekoCompileResult result;
    int exit_code = 1;

    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }

    if (argc >= 4 && streq(argv[1], "-o")) {
        output_path = argv[2];
        first_input = 3;
    }

    if (first_input >= argc) {
        usage(argv[0]);
        return 2;
    }

    if (!load_sources(argc, argv, first_input, allocator, &sources, &source_count)) {
        free_sources(allocator, sources, source_count);
        return 1;
    }

    options.allocator = allocator;
    teko_compile_sources(sources, source_count, &options, &result);
    print_diagnostics(&result, sources, source_count);

    if (result.success && result.output_count > 0) {
        if (!teko_platform_write_file(output_path, result.outputs[0].text)) {
            fprintf(stderr, "teko: failed to write '%s'\n", output_path);
        } else {
            exit_code = 0;
        }
    }

    teko_free_compile_result(&options, &result);
    free_sources(allocator, sources, source_count);
    return exit_code;
}
