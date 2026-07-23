# Kill-C PULL-FORWARD into 0.3.0.30 — own-backend maturity inventory + FFI own-native validation

> **Status:** DESIGN-AHEAD, doc-only. **NOT implemented — the owner ratifies before code.** Addendum to
> `docs/design/star-ref-and-ffi-0.3.1.md`. **Owner standing rule:** *everything that can be pulled
> forward into .30 toward KILLING the C backend must be — to shrink the future waves* (kill-C .31–.32;
> own linker .33–.34). This doc maps that CONCRETELY, anchored on the **current** own-backend code.
>
> **Fixed facts:** fixpoint is **gen1==gen2 (NO gen3)**; the C backend is transitional and **dies**;
> **no FFI may depend on `cc` surviving**; **Part A (`ref`) is being executed by another agent — do not
> touch it here.** GATE-G for a PODE-.30 crumb = `teko build . --no-verify --release && ./bin/teko
> test .` **+ fixpoint gen1==gen2**.
>
> **Sources (read at authoring):** `src/lir/lower.tks` (TAST→LIR, "N1 subset" #221 — 60 honest-stops),
> `src/lir/lir.tks` (the LIR opcode set), `src/backend/isel_{x86_64,arm64,riscv}.tks` (32 honest-stops
> in x86, dominated by the `B1-fp` float family + i128), `src/backend/encode_*`, `objfile_{elf,macho,
> coff}.tks` (relocatable objects + the referenced-but-UNBUILT "E1 linker", `objfile_elf.tks:383`),
> `abi_{sysv64,aapcs64,win64,riscv64}.tks`, `regalloc*`, `lir_interp.tks`/`minst_interp.tks` (the
> differential oracle that mirrors the C backend's honest-stops), `src/build/regression.tks` +
> `docs/design/tkb-regression-format.md` (the test lane), `teko.tkp` `[tests]`/`[platforms]`.

---

## 1. Own-backend maturity INVENTORY today (concrete honest-stops / gaps)

The own AOT backend is **mature in its BOTTOM half, immature in its TOP half:**

| layer | file(s) | state |
|---|---|---|
| **TAST → LIR lowering** | `lir/lower.tks` | **"N1 reference subset" (#221) — the big gap.** 60 NAMED honest-stops. Lowers only: integer/float **literals**, **integer** arithmetic/unary, locals, `to` numeric casts, **direct** calls (Teko-Teko + `exit`/`panic`), `let`, `return`, expr-statements, basic `if`/`match`. |
| **isel** | `isel_{x86_64,arm64,riscv}.tks` | mature for **integer/control/memory**; 32 honest-stops in x86 dominated by the **`B1-fp` FLOAT family** (float ALU/compare/negate/convert/literal → SSE) + **i128** materialization. |
| **encode** | `encode_{x86_64,arm64,riscv}.tks` (+consts) | mature, tested (~80 KB each); the full integer/mem/control instruction set present. |
| **regalloc** | `regalloc{,_x86,_riscv}.tks` | present, tested. |
| **objfile** | `objfile_{elf,macho,coff}.tks`, `objfile_elf_riscv.tks` | emit **relocatable objects** with undef symbols + relocations; **NO linker** — the final link is still `cc`/`ld`; the "future **E1 linker**" is named but **UNBUILT** (`objfile_elf.tks:383`). |
| **ABI** | `abi_{sysv64,aapcs64,win64,riscv64}.tks` | classification tables present, tested — but **no varargs** rule set yet. |
| **oracle** | `lir_interp.tks`, `minst_interp.tks` | interpret the covered subset, **mirroring the C backend's honest-stops** so a fixture honest-stops identically on both — this is how the native path is validated **without producing binaries** today. |

**The 60 `lower.tks` honest-stops, categorized (what the own AOT backend does NOT yet emit):**

| class | examples (cite) | substrate ready? |
|---|---|---|
| integer **comparisons** | `<`/`<=`/`>`/`>=`/`==`/`!=` (`lower.tks:807,821`) — LIR has `ICmp*` (`lir.tks:33`) | **YES** (opcodes + encode + interp exist) |
| **bitwise / shifts** | `&`/`\|`/`^`/`<<`/`>>` (`lir.tks:32` `IAnd/IOr/IXor/IShl/IShrS/IShrU`) — declared, not lowered | **YES** |
| unary `~` | `lower.tks:309` | **YES** |
| **fat pointers** (str/slice) | the "wider fat-pointer ABI N2 gap" (`lower.tks:936,3767`, #382) | mostly (regalloc/encode ready; the fat ABI must be pinned) |
| **struct / field / index** | out-of-subset expr nodes (`lower.tks:615`) | **YES** (mem loads/stores exist) |
| **match patterns** | range/alt/null/slice/field (`lower.tks:3034,3035,3036,3203,3283`) | mostly (variant repr exists; needs LIR lowering) |
| if-without-else as value; **destructuring** binding | `lower.tks:2777,1819` | **YES** |
| **interpolation** holes | integer/bool ready; float/typed/format-spec (`lower.tks:3972,3987,3998`) | integer YES; **float blocked** |
| **runtime builtins** (concat/format/…) | outside the N1 run subset (`lower.tks:1059,4006`) | lowering YES; native RUN needs the runtime linked (E1) |
| **FLOAT family** (all float ops) | binop/unop/cmp/cast/literal (`lower.tks:277,309,784`) + isel `B1-fp` (`isel_x86_64.tks:198,325,439`) | **NO — hard dep on own-backend float isel** |
| **i128** | isel i128 materialization (`isel_x86_64.tks:170`) | partial — **hard dep on own-backend i128 isel** |

**Headline:** of the ~60 lowering honest-stops, **~40 are integer/control/memory/fat/match/struct/
pointer lowering whose bottom-half substrate already exists → CLOSEABLE in .30**; **~20 are the FLOAT
family + i128 → HARD-blocked on own-backend float/i128 isel.** The **E1 own linker is the single
overarching hard dep** for producing native binaries + self-host + external-C-symbol resolution.

---

## 2. Classification — each kill-C epic item: PODE .30 AGORA vs DEPENDÊNCIA DURA

### (a) Own-backend honest-stops

| item | verdict | detail |
|---|---|---|
| integer comparisons lowering | **PODE .30** | LIR/isel/encode/interp ready — pure `lower.tks` completion |
| bitwise / shift lowering | **PODE .30** | `IAnd/IOr/IXor/IShl/IShrS/IShrU` ready |
| unary `~`, remainder, missing integer ops | **PODE .30** | ready |
| fat-pointer (str/slice) ABI + lowering | **PODE .30** | pin the fat ABI (ptr+len) once; then `.len`/index/literal — big but substrate ready |
| struct/field/index lowering | **PODE .30** | mem ops ready |
| match patterns (null/field/slice/range/alt over int+variant) | **PODE .30** | variant repr known; LIR lowering only |
| if-without-else-value, destructuring binding, integer interpolation | **PODE .30** | ready |
| **float family** (all float ops + float interpolation/match) | **HARD DEP** | X = **own-backend float isel (`B1-fp`) + FPR regalloc/spill**; genuinely blocked |
| **i128** ops | **HARD DEP** | X = **own-backend i128 isel** (register-pair; #222 in SW13) |

### (b) Self-host coverage on the own backend

| item | verdict | detail |
|---|---|---|
| each construct-class lowering (above) | **PODE .30** (per class) | every closed class shrinks the self-host gap; validate via the interp oracle + backend unit tests |
| **producing a runnable native binary / full self-host** | **HARD DEP** | X = **E1 own linker** (unbuilt) — the own backend emits `.o` but cannot LINK; self-host also needs 100% lowering (float/i128 included) |

### (c) The FFI KC1–KC4 (from the star-ref plan)

| item | verdict | detail |
|---|---|---|
| **raw-pointer ops lowering** (`*p`/`p[i]`/`p->f`/arith/`addr_of`) | **PODE .30** | LIR mem loads/stores + address arithmetic; validate via interp |
| **macro resolver Tier 0** (constants/flags) | **PODE .30** | compiler front-end (header read + constant eval); no backend at all |
| **macro resolver Tier 2** (body→IR) | **PODE .30** *(after bitwise/shift lowering, itself .30)* | expands to integer arithmetic/bitwise LIR — rides the .30 lowering completion |
| **macro resolver Tier 1** (symbol alias) | **surface/binding .30; link HARD DEP** | binding to a real symbol is .30; RESOLVING it needs the linker (X = E1) |
| **KC1 vararg ABI** | **PARTIAL .30** | the per-target vararg rule set (SysV/AAPCS64/Win64) is own-native and PODE .30 as a lowering+interp crumb; a real native variadic CALL to libc couples with C-call lowering + link (E1) |
| **`#repr("c")` layout / `cabi` callbacks / `#[export]` symbol emission** | **PODE .30** | objfile already emits GLOBAL/FUNC symbols + the layout is codegen; validated via objfile tests + the reverse-FFI fixture (§5) |
| **`emit_c_header` `.h`** | **PODE .30** | pure text from the checked program — backend-independent |
| **KC3 external-C-symbol resolution** | **HARD DEP** | X = **E1 own linker** (resolve `extern`/`extern from lib` against `.a`/`.o`/`.so`) |
| **KC4 delete the C-FFI emitter** | **HARD DEP** | X = full own-backend self-host + E1 — the LAST kill-C step, never .30 |

### (d) Own-linker prep

| item | verdict | detail |
|---|---|---|
| undef-symbol + relocation EMISSION in objfiles | **already present / PODE .30** | `objfile_*` emit undef GLOBAL/NOTYPE + relocs (the linker INPUT is ready) |
| the **E1 linker** itself (resolve + produce image) | **HARD DEP** | X = the own-linker epic (.33–.34); interim link bridges via the host `ld`/`cc`-driver **as throwaway scaffolding only** |

---

## 3. The .30 pull-forward crumb sequence (own-backend maturity + FFI own-native)

Each is own-native, additive, seed-safe, and rides **GATE-G + fixpoint gen1==gen2**; each is validated
by the **interp oracle** (`lir_interp`/`minst_interp`) + the relevant `isel/encode/objfile` unit tests
(the corpus still builds via the C backend in .30 — these crumbs CLOSE own-backend honest-stops and
are proven on the oracle, shrinking kill-C). **Ordered by dependency + impact:**

| crumb | size | closes | notes |
|---|---|---|---|
| **KP1** integer **comparison** lowering | S | `lower.tks:807,821` | `ICmp*` + setcc; interp oracle |
| **KP2** **bitwise + shift** lowering (`&`/`\|`/`^`/`~`/`<<`/`>>`) | S | `lower.tks:290,309` | unblocks macro Tier 2 (KP12) |
| **KP3** **struct / field / index** lowering | M | `lower.tks:615` | mem loads/stores |
| **KP4** **fat-pointer ABI** (ptr+len) + str/slice literal/`.len`/index lowering | L | `lower.tks:936,3767` (#382) | pin the fat ABI once; the biggest single self-host lever |
| **KP5** **match-pattern** lowering (null/field/slice over int+variant; range/alt) | M | `lower.tks:3034…3389` | variant repr known |
| **KP6** if-without-else-value + **destructuring** binding + integer **interpolation** | M | `lower.tks:2777,1819,3987` | float/bool-format holes stay blocked (KP-float) |
| **KP7** widen the **direct-call/runtime-builtin** lowering surface | M | `lower.tks:1059` | more builtins into the N1 run subset |
| **KP8** **raw-pointer ops** lowering (`*p`/`p[i]`/`p->f`/arith/`addr_of`) | M | FFI (star-ref §2/§4.1) | mem ops; own-native |
| **KP9** **`#repr("c")` layout** + **`extern union`** own-backend layout | M | FFI G2 | own layout algo; objfile-tested |
| **KP10** **`cabi` callback** emission + **`#[export]`** unmangled C-ABI symbol emission + FFI-safe gate | M | FFI G3 / reverse-FFI | objfile GLOBAL/FUNC; reverse-FFI fixture (§5) |
| **KP11** **`emit_c_header` `.h`** (prototypes + `#define` consts; close `header.tks:91` G5) | M | reverse-FFI | pure text; backend-independent |
| **KP12** **macro resolver Tier 0** (constant/flag) + **Tier 2** (body→IR, on KP2) + Tier 3 honest-error | L | FFI macros | Tier 1 symbol-link + KC3 stay HARD DEP (E1) |
| **KP13** **vararg ABI rule set** (SysV/AAPCS64/Win64) as own-native tables + interp | M | FFI KC1 | the native libc variadic CALL couples with C-call lowering + link (E1) |
| **KP14** **`#cconv` + `teko::ffi::errno()`** own-backend emission | S–M | FFI G4 | common cconv cases |
| **KP15** **`teko_rt_init/shutdown`** + panic-not-into-C runtime | S | reverse-FFI | own-native runtime |

**Not in .30 (the hard deps — §4).** KP1–KP15 take the own backend from "N1 integer subset" to
"integer + memory + fat-pointer + struct + match + FFI-codegen complete," leaving only the FLOAT/i128
isel and the E1 linker between .30 and self-host — exactly the owner's goal: **make kill-C the
smallest possible step.**

---

## 4. Hard dependencies (genuinely blocked — deferred only because of X)

| blocked item | blocked on X | earliest |
|---|---|---|
| all **float** lowering + float FFI payloads | **own-backend float isel (`B1-fp`) + FPR regalloc/spill** | own-backend float crumb (SW13-adjacent, `#222`/FPR-spill) |
| **i128** ops | **own-backend i128 register-pair isel** (`#222`) | SW13 |
| native **binary** production / **full self-host** on own backend | **E1 own linker** (unbuilt) + 100% lowering | .33–.34 (linker epic) |
| **KC3** external-C-symbol resolution (`extern`/`extern from lib` against `.a`/`.o`/`.so`) | **E1 own linker** | .33–.34 |
| **macro Tier 1** actual symbol link | **E1 own linker** (the binding/reference is .30) | .33–.34 |
| **KC1** real native libc **variadic call** | own-backend **C-call lowering** + E1 link (the ABI tables are .30, KP13) | with C-call lowering / E1 |
| **KC4** delete the C-FFI emitter | full self-host + E1 (the LAST kill-C step) | end of kill-C |

**Interim link bridge:** until E1, the final link uses the host `ld`/`cc`-driver **as throwaway
scaffolding**; every own-backend crumb above is validated on the **interp oracle** (no link needed),
and the FFI fixtures that require a real link are marked "green when the KC/E1 crumb lands" (§5). **No
capability is designed to depend on `cc` surviving.**

---

## 5. FFI VALIDATION — the test lane, cross-OS/arch (owner .30 requirement)

FFI validation lives in the **regression test lane** and is exercised by the **gen1 binary from the
dry build** — `teko build . --no-verify --release` produces gen1, then **`./bin/teko test .`** runs the
fixtures. The fixtures therefore must need **nothing beyond gen1 + the runner's own toolchain**, and
**must pass own-native** (a fixture that only passes via the C emitter is a regression, star-ref §11).

### 5.1 Where it wires (mechanism)

- **Surface:** each FFI fixture is a standalone project under **`examples/regressions/ffi/<name>/`**, a
  first-class **`.tkb`** regressive (Gherkin over the reused `Tkr` model — `tkb-regression-format.md`);
  it is picked up by **`[tests] regression = ["examples/regressions"]`** in `teko.tkp` (add the `ffi`
  subtree to the spec-bearing set alongside P1 `rt_behavior`). The runner (`build/regression.tks`:
  `run_regression_sources` → `check_run`/`check_compile_fail`) builds each once and runs it,
  asserting **exit / stdout / stderr**; `EXPECT_COMPILE_FAIL` for negatives; `trap` for panics.
- **Own-native build selection:** FFI fixtures build through the **own backend** (a `.tkb` `Given
  backend = "own"` step / the `--backend=own` selection), NOT the C emitter — so validation is
  own-native. A fixture whose own-backend path is not yet landed (KC/E1-coupled) is marked with an
  **`on "<os-arch>"` expectation** of the honest-stop / `Given pending = "KC1"` and is **skipped-green
  until its crumb lands** (the runner already supports a skip verdict + per-platform overrides).
- **Cross-OS/arch matrix:** the lane runs on **each CI host** (`[platforms] targets =
  ["macos","linux","windows"]` × the arch runners); `targets = ["host"]` means each fixture runs on
  whatever OS/arch the CI job is, so the SAME `.tkb` yields per-platform coverage. Platform-specific
  exit/stdout differences use the `.tkb` **`on "<os-arch>"`** override (`[expect.<os-arch>]`).
- **Reverse-FFI C consumer:** a reverse-FFI fixture carries a minimal **`consumer.c`**; the runner
  (extend `check_run`) does: **(1)** `teko build` the fixture's `abi="c"` artifact **own-native** (own
  backend emits the `.o` + `emit_c_header` emits the `.h`); **(2)** compile+link `consumer.c` against
  the emitted object+header using the **runner's host `cc`** — **used ONLY for the C-consumer side**
  (a real C consumer inherently uses the host C toolchain); **(3)** run the linked executable and
  assert exit/stdout. The Teko side is own-native; `cc` never touches Teko codegen.

### 5.2 The FFI fixture suite (inputs → expected exit, per platform)

**A. USING C from Teko** (native oracle = own backend):

| fixture (`examples/regressions/ffi/…`) | asserts | oracle | own-native crumb |
|---|---|---|---|
| `extern_fn_libc_call` | `extern fn strlen(...)` → exit = len | exit | KP7 + link (interim host / E1) |
| `extern_macro_const_flag` | Tier-0 `O_RDONLY`/`SOCK_STREAM` inlined; exit = value | exit | **KP12 (.30, no link)** |
| `extern_macro_htonl_expand` | Tier-2 byte-swap expanded to IR; exit = swapped byte | exit | **KP12 (.30)** |
| `extern_macro_complex_rejected` | Tier-3 arbitrary macro | `EXPECT_COMPILE_FAIL` | **KP12 (.30)** |
| `ptr_deref_index_arrow` | `*p`, `p[i]`, `p->f` inside `unsafe` | exit = summed | **KP8 (.30)** |
| `ptr_arith_addr_of` | `p+n`, `p-q`, `addr_of(x)` | exit | **KP8 (.30)** |
| `ptr_null_union` | `ptr<T>\|null` / `uptr\|null` null test | exit | **KP8 (.30)** |
| `ptr_deref_in_safe_rejected` / `ptr_void_deref_rejected` / `ptr_optional_question_rejected` | safe-ctx deref / `void*` deref / `ptr<T>?` | `EXPECT_COMPILE_FAIL` | KP8 (.30) |
| `repr_c_struct_byval` | `#repr("c")` struct passed to a C stub | exit | **KP9 (.30)** + consumer link |
| `extern_union_layout` | `extern union` read/write | exit | **KP9 (.30)** |
| `cabi_callback_noncapturing` | non-capturing closure as C callback; C stub invokes it | exit | **KP10 (.30)** + consumer link |
| `cabi_callback_capturing_rejected` | capturing closure coerced to `cabi` | `EXPECT_COMPILE_FAIL` | KP10 (.30) |
| `variadic_printf` | `extern fn printf(fmt, ...)`; stdout oracle | stdout | **KP13 tables (.30); native call green when KC1/C-call lands** |
| `cconv_stdcall` *(windows)* | `#cconv("stdcall")` call | exit | **KP14 (.30)** |
| `ffi_errno_read` | `teko::ffi::errno()` after a failing C call | exit | **KP14 (.30)** |

**B. EXPORTING Teko to C** (reverse-FFI; C consumer links + calls the exported symbol):

| fixture | asserts | oracle | own-native crumb |
|---|---|---|---|
| `revffi_export_scalar` | `#[export("teko_add")]`; `consumer.c` links + calls `teko_add(2,3)` | exit = 5 | **KP10 emit (.30); link via host cc (consumer side)** |
| `revffi_c_header_emitted` | the generated `.h` compiles under the host `cc` (prototypes + `#define` consts) | consumer compiles | **KP11 (.30, pure text)** |
| `revffi_repr_c_return` | export returns a `#repr("c")` struct; `consumer.c` reads fields | stdout | **KP9+KP10 (.30)** |
| `revffi_rt_init_contract` | `consumer.c` calls `teko_rt_init` then an exported allocating fn | exit | **KP15 (.30)** |
| `revffi_panic_no_unwind` | an exported fn that would panic returns `… \| error`, does NOT unwind into C | exit (error code, not abort-through-C) | **KP10+KP15 (.30)** |
| `revffi_export_nonffisafe_rejected` | `#[export]` on a capturing-closure param / bare-ref return | `EXPECT_COMPILE_FAIL` | KP10 (.30) |
| `revffi_export_teko_variadic_rejected` | `#[export]` on a `params []T` fn | `EXPECT_COMPILE_FAIL` | KP10 (.30) |

### 5.3 The OS/arch matrix

Every fixture above runs on each lane host; the `.tkb` carries `on "<os-arch>"` overrides where a
result differs (e.g. Win64 stdcall, macOS-arm64 vararg-on-stack):

| lane host | arch | notes |
|---|---|---|
| Linux | **x86_64**, **arm64**, **riscv64** | full matrix; riscv via the riscv objfile/encode/abi |
| macOS | **arm64** | AAPCS64 (darwin vararg-on-stack variant) |
| Windows | **x86_64**, **arm64** | COFF; Win64 vararg (float-dup-into-GPR); `#cconv("stdcall")` here |

Fixtures whose native path is **own-backend-coupled** (`variadic_*` → KC1/C-call; anything needing a
real external link → KC3/E1) are marked **"green when the KC/E1 crumb lands"** via a `Given pending`
step and skipped-green until then — so the suite is authored NOW (design-ahead) and turns green
incrementally as KP13/KC1/KC3/E1 arrive, on every platform at once.

### 5.4 The gen1-executable constraint (explicit)

- No fixture may require a tool beyond **gen1 + the runner's host toolchain**. The Teko side is
  **always own-native** (built by gen1's own backend). The host `cc` is invoked **only** to compile
  the reverse-FFI `consumer.c` (the C side of a genuinely-C consumer) — never for Teko codegen.
- A fixture that passes only via the C emitter is a **regression** and fails review (star-ref §11).
- The reverse-FFI link step uses the host `ld`/`cc`-driver as the **interim** image linker; when **E1**
  lands, the `.tkb` flips those fixtures to the own-linker path with no fixture rewrite (the expected
  exit/stdout is unchanged — only the link tool changes).

---

## 6. Summary (counts)

- **Own-backend honest-stops inventoried:** **~60 in `lir/lower.tks`** (the TAST→LIR "N1 subset" gap)
  + **~32 in `isel_x86_64.tks`** (dominated by the `B1-fp` FLOAT family + i128), mirrored in
  isel-arm64/riscv and in the `lir_interp`/`minst_interp` oracle.
- **Closeable in .30 (KP1–KP15):** **~40 of the 60** lowering honest-stops (integer comparisons,
  bitwise/shifts, struct/field/index, fat-pointer str/slice, match patterns, if-value/destructuring/
  integer-interpolation, raw-pointer ops, `#repr("c")`/union layout, `cabi`/`#[export]`, the `.h`
  emitter, macro Tiers 0+2, vararg ABI tables, cconv/errno, rt-init) — proven on the interp oracle +
  backend unit tests, shrinking kill-C to nearly "float isel + linker."
- **Hard deps (NOT .30):** the **FLOAT family + i128** (X = own-backend float/i128 isel, ~20
  honest-stops) and the **E1 own linker** (X = the linker epic; gates native binaries, self-host,
  KC3 symbol resolution, macro Tier-1 link, real variadic call, and KC4 delete-C).
- **FFI validation:** a cross-OS/arch `.tkb` suite under `examples/regressions/ffi/` (both directions +
  pointers), own-native, gen1-executable, host-`cc` only for the reverse-FFI C consumer, wired into
  `[tests] regression` and fired by `./bin/teko test .` on every platform; KC/E1-coupled fixtures
  authored now and skipped-green until their crumb lands.

*Design-ahead, doc-only. No product code changed. Part A (`ref`) is out of scope here (another agent).
Owner ratifies before implementation.*
