// core_teko.mc -- the taught compiler's own main(), naming the parts of
// D64.1 (docs/design/plano-ngen-entrega4.md §64) instead of the whole
// `<mc/core>` bundle: `<mc/core_min>` comes from `[compiler].core` in
// `mc.toml` (this file assumes it is already included), and this file adds
// the four parts teko actually uses --
//
//   <mc/core_machines>  arm64 and x86-64, both machines every CI leg needs
//   <mc/core_writers>   macho/backend_exe, backend_elf/backend_elf_exe,
//                       backend_coff -- every writer a leg links with
//   <mc/core_build>     `mc build --entry-only`, what the CI (and this
//                       file's own `mc.toml`) compiles each fixture with
//   <mc/core_bundle>    `#include <name>`, which `lib/rt.mc` needs for
//                       `<sys>`
//
// `<mc/core_pkg>` and `<mc/core_sandbox>` are left out on purpose: nothing
// under `ngen/` calls `mc pkg`, `mc update` or `mc sandbox`, and no fixture
// exercises them either. `main()` below is `src/main.mc`'s own list with
// those two omitted and `mc_pkg_init()`/`mc_sandbox_init()` gone with them;
// `mc_build_init()` stays for S1 (D64.3 moves it to teko's own subcommand
// table in S2 -- S1 is "fewer parts", nothing else).
#include <mc/core_machines>
#include <mc/core_writers>
#include <mc/core_build>
#include <mc/core_bundle>

i64 main(i64 argc, uptr argv, uptr envp) {
    host_init(envp);
    mc_machines_init();
    mc_writers_init();
    mc_bundle_init();
    mc_build_init();
    return mc_main(argc, argv, envp);
}
