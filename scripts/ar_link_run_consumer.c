/* scripts/ar_link_run_consumer.c — the real-toolchain static-archive link+run consumer
 * (KP16 + the Mach-O/COFF port from `theory/kp16-ar-macho-coff`): links against the
 * own-native static archive built from examples/regressions/ar_link_run (GNU-format `.a`
 * on linux, BSD `__.SYMDEF SORTED` `.a` on macOS, MS two-linker-member `.lib` on Windows)
 * and calls its exported `add` function. Exit code must be 42 (add(40, 2)) for the
 * archive to count as REAL-toolchain linker-consumable, not just byte-golden-consumable.
 * Lives under scripts/, not examples/ (any .c under examples/ is gitignored as a
 * `teko build` C-backend output; this is a hand-authored C source, not one).
 *
 * SYMBOL NAME: `pub fn add` in a project-root file (no subfolder under `source`) is
 * namespaced to the project's own canonical root name (`discover.tks`: "a file directly
 * under `source` is the bare root `name`" — here "ar_link_run"), so the own-native
 * backend's internal mangler (`mangle_fn_symbol`, `src/lir/lower.tks` — the SAME mangling
 * the C backend's `cb_fn_name` uses) emits `ar_link_run__add`, NOT bare `add`. The real
 * symbol is `ar_link_run__add` on Windows/COFF and linux/ELF, `_ar_link_run__add` on
 * macOS/Mach-O (the platform's own leading-underscore C-symbol decoration, transparent to
 * this `extern` declaration).
 */
extern long long ar_link_run__add(long long a, long long b);

int main(void) {
    return (int)ar_link_run__add(40, 2);
}
