// main.c — the C SEED of the executable entry point (`teko`). Co-located at the project
// root next to its Teko original main.tks (the C/H + .tks pair): the seed compiles the
// .tks sources to reproduce the compiler in Teko (self-hosting). Keep this and main.tks
// semantically equivalent.
//
// F1 (path-to-first-binary): the pipeline is WIRED. `teko <file.tks>` runs
// read → lex → parse → check end-to-end (see src/driver.c) and reports the verdict.
// main() stays minimal: argument handling + delegate to tk_compile.
#include "driver.h"   // tk_compile

#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fputs("usage: teko <file.tks>\n", stderr);
        return 2;
    }
    return tk_compile(argv[1]);
}
