// main.c — the C SEED of the executable entry point (`teko`). Co-located at the project
// root next to its Teko original main.tks (the C/H + .tks pair): the seed compiles the
// .tks sources to reproduce the compiler in Teko (self-hosting). Keep this and main.tks
// semantically equivalent.
//
// F1 (path-to-first-binary): the pipeline is WIRED. `teko <file.tks>` runs
// read → lex → parse → check end-to-end (see src/driver.c) and reports the verdict.
// main() stays minimal: argument handling + delegate to tk_compile.
#include "driver.h"   // tk_compile, tk_compile_project

#include <stdio.h>
#include <string.h>     // strlen, strcmp
#include <sys/stat.h>   // stat — is the argument a directory? (A3 project dispatch)

// Is `arg` a project entry — a directory, or a path ending in ".tkp"? Then drive the
// multi-file project path (A3); otherwise compile the single `.tks` file (F1).
static int is_project_arg(const char *arg) {
    size_t n = strlen(arg);
    if (n >= 4 && strcmp(arg + n - 4, ".tkp") == 0) return 1;
    struct stat st;
    return stat(arg, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fputs("usage: teko <file.tks | project-dir>\n"
              "       teko run <file.tks>   (interpret on the VM — debug profile)\n", stderr);
        return 2;
    }
    // `teko run <file.tks>` — the VM (debug) path: check then interpret, no codegen.
    if (strcmp(argv[1], "run") == 0) {
        if (argc < 3) { fputs("usage: teko run <file.tks>\n", stderr); return 2; }
        return tk_run(argv[2]);
    }
    const char *arg = argv[1];
    if (!is_project_arg(arg)) return tk_compile(arg);

    // Project dispatch. A directory is the project root as-is; a `.tkp` path resolves
    // to its containing directory ("." when it has no path separator).
    size_t n = strlen(arg);
    if (n >= 4 && strcmp(arg + n - 4, ".tkp") == 0) {
        const char *slash = strrchr(arg, '/');
        if (slash == NULL) return tk_compile_project(".");
        static char dir[4096];
        size_t dn = (size_t)(slash - arg);
        if (dn >= sizeof(dir)) { fputs("teko: project path too long\n", stderr); return 2; }
        memcpy(dir, arg, dn); dir[dn] = '\0';
        return tk_compile_project(dir);
    }
    return tk_compile_project(arg);
}
