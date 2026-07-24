/* scripts/theory_ar_consumer.c — THEORY validation consumer (branch
 * theory/kp16-ar-macho-coff, NEVER merged): links against the own-native static
 * archive built from examples/theory/ar_static_lib_demo (BSD __.SYMDEF .a on
 * macOS, MS two-linker-member .lib on Windows) and calls its exported `add`
 * symbol. Exit code must be 42 (add(40, 2)) for the theory CI job to accept the
 * archive as REAL-toolchain linker-consumable, not just byte-golden-consumable.
 * Lives under scripts/, not examples/ (any .c under examples/ is gitignored as
 * a `teko build` C-backend output; this is a hand-authored C source, not one).
 */
extern long long add(long long a, long long b);

int main(void) {
    return (int)add(40, 2);
}
