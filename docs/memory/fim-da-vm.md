---
section: history
created: 2026-07-29
source: VM retirement (2026-07-13, ruling #524)
---

# Virtual Machine Retirement — Historical Record

This is the authoritative record of the VM's end-of-life. **This file is the ONLY location where "VM" may appear in active documentation.** All other references have been expunged; comparisons, historical asides, and design notes mentioning the VM belong here.

## VM Retirement (2026-07-13, ruling #524)

The Teko VM interpreter, REPL, and C bootstrap were retired in favour of native AOT compilation as the sole engine. The VM was an in-process functional interpreter that served as the oracle for language-level behavior validation.

## Empirical Memory Findings

From D29/D31 benchmarks (VM vs. native path): **1.5GB was attributed to the VM interpreter (in-process functional environment), not the arena.** The compiler's arena budget is ~366 MB (codegen + front-end), leak-to-root batch-safe with right-sizing (free-list + first-rung tuning + free-old-on-grow).

## Historical Design References

- **VM Oracle Role**: The VM was to language-level testing what the LIR interpreter (`lir_interp.tks`) is to native lowering — a validation oracle independent of machine code generation (codegen, encoding, ELF). The LIR interpreter replaced the VM's role for the lowering path (TAST → LIR).
- **Test Resolution**: The VM resolved test modules by last path segment (e.g., `test_foo` in `src/module/file.tkt` resolved as `module::test_foo`). This was historical test infrastructure.
- **Char Representation**: The VM carried `char` as a `{ptr,len}` pair; the native backends (C codegen + native AOT) represent it as a scalar codepoint. The LIR, being scalar-oriented, uses the native representation.
- **Rodata Relocation**: The VM resolved internal pointer relocations in rodata analogously to how `wasm_relocate_rodata` (T-B4) and native writers handle `.rela.rodata`/`__const`/`.rdata` relocations — a single relocation pass at module load time.
- **Seed-VM Match Quirk**: An historical workaround in the VM's pattern-match compiler where `checker::Byte` as a match arm would misfire if the subject was a container type like `Slice{element: Byte}`. This quirk is no longer relevant, but certain code paths remain conservative to avoid similar issues.

## Backend Architecture After Retirement

The compiler now has two independent backends:

1. **ROTA C (Route C)**: Emits C code + compiles with `cc` (gcc/clang). Used for self-hosting and fallback compilation.
2. **Native AOT**: Directly emits machine code (x86_64, arm64). The production path for tests and distributed binaries.

The LIR (Low-level Intermediate Representation) is shared between both backends and validated by `lir_interp.tks`, the permanent oracle for the native lowering path.
