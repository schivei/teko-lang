/* scripts/theory_ar_consumer.c — THEORY validation consumer (branch
 * theory/kp16-ar-macho-coff, NEVER merged): links against the own-native static
 * archive built from examples/theory/ar_static_lib_demo (BSD __.SYMDEF .a on
 * macOS, MS two-linker-member .lib on Windows) and calls its exported `add`
 * function. Exit code must be 42 (add(40, 2)) for the theory CI job to accept
 * the archive as REAL-toolchain linker-consumable, not just byte-golden-consumable.
 * Lives under scripts/, not examples/ (any .c under examples/ is gitignored as
 * a `teko build` C-backend output; this is a hand-authored C source, not one).
 *
 * SYMBOL NAME: `pub fn add` in a project-root file (no subfolder under `source`)
 * is namespaced to the project's own canonical root name (`discover.tks`: "a
 * file directly under `source` is the bare root `name`" — here "theory_ar_lib"),
 * so the own-native backend's internal mangler (`mangle_fn_symbol`, LIR
 * lower.tks — the SAME mangling the C backend's `cb_fn_name` uses) emits
 * `theory_ar_lib__add`, NOT bare `add`. `pub` (project-wide Teko visibility)
 * is NOT the reverse-FFI `exp` export surface (`src/codegen/ffi_export.tks`,
 * `flatten_canonical` -> `theory_ar_lib_add` single-underscore, or an
 * `exp("add")` override for an exact name) — that mechanism is wired ONLY into
 * the C-backend `abi="c"` binary path today (`src/build/project.tks::backend`
 * dispatches `Backend::Native` straight to `emit_native`/`emit_static_lib`
 * BEFORE the `m.abi == "c"` gate), so it has no effect on a `TEKO_BACKEND=native`
 * static-lib build. Verified locally (own-native COFF/Mach-O objects built on
 * this sandbox, `objdump -t`/`strings -a`): the real symbol is
 * `theory_ar_lib__add` on Windows/COFF, `_theory_ar_lib__add` on macOS/Mach-O
 * (the platform's own leading-underscore C-symbol decoration, transparent to
 * this `extern` declaration). This file references the REAL emitted name — it
 * proves the ARCHIVE CONTAINER (the THEORY scope) is genuinely linker- and
 * loader-consumable, not a claim that the (separate, out-of-scope) FFI-export
 * clean-naming problem is solved for the native backend.
 */
extern long long theory_ar_lib__add(long long a, long long b);

int main(void) {
    return (int)theory_ar_lib__add(40, 2);
}
