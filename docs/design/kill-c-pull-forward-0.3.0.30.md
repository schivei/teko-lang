# Kill-C PULL-FORWARD into 0.3.0.30 — own-backend maturity inventory + FFI own-native validation

> **Status:** DESIGN-AHEAD, doc-only. **NOT implemented — the owner ratifies before code.** Addendum to
> `docs/design/star-ref-and-ffi-0.3.1.md`. **Owner standing rule:** *everything that can be pulled
> forward into .30 toward KILLING the C backend must be — to shrink the future waves.* This doc maps
> that CONCRETELY, anchored on the **current** own-backend code.
>
> **OWNER LINK STRATEGY (governs §2/§4 — the E1 dependency is DISSOLVED):**
> 1. Emit `.o` **and** `.a` **directly, own-native, with NO C** (the `objfile_*` already emit
>    ELF/Mach-O/COFF relocatable — COMPLETE the whole-program section/symbol/reloc emission + add `.a`
>    static-archive emission).
> 2. Link with the **system `ld`** (the platform linker — NOT the `cc` driver) into the final per-OS/arch
>    image: `.exe`/`.dll`/`.lib` (Windows/COFF), ELF binary/`.so` (Linux), Mach-O binary/`.dylib` (macOS).
> 3. The **own E1 linker** comes LATER (total link independence) — it **replaces the system `ld`**; it is
>    **NOT a prerequisite to kill C emission.**
>
> **Critical path to kill C is entirely own-native, no external blocker:** (1) close the ~20 **float +
> i128 isel** honest-stops; (2) complete own-native **`.o`/`.a`** emission; (3) swap the build step from
> "emit `teko.c` + `cc`" to "emit `.o`/`.a` + **system `ld`**." E1 is a later link-independence epic.
>
> **Fixed facts:** fixpoint is **gen1==gen2 (NO gen3)**; the C backend is transitional and **dies**;
> **no FFI may depend on `cc` surviving** (the reverse-FFI C *consumer* may use the host `cc`, but Teko
> codegen never does, and the LINK is the system `ld`, not `cc`); **Part A (`ref`) is executed by another
> agent — not touched here.** GATE-G for a PODE-.30 crumb = `teko build . --no-verify --release &&
> ./bin/teko test .` **+ fixpoint gen1==gen2**.
>
> **Sources (read at authoring):** `src/lir/lower.tks` (TAST→LIR, "N1 subset" #221 — 60 honest-stops),
> `src/lir/lir.tks`, `src/backend/isel_{x86_64,arm64,riscv}.tks` (32 honest-stops, dominated by the
> `B1-fp` float family + i128), `src/backend/encode_*`, `objfile_{elf,macho,coff}.tks` +
> `objfile_elf_riscv.tks`, `abi_{sysv64,aapcs64,win64,riscv64}.tks`, `regalloc*`,
> `lir_interp.tks`/`minst_interp.tks` (the oracle mirroring the C backend's honest-stops),
> `src/codegen/codegen.tks` (`cb_fn_name` the `__` mangle; `f.c_symbol` extern no-mangle at :7515),
> `src/build/regression.tks` + `docs/design/tkb-regression-format.md` (the test lane),
> `teko.tkp` `[tests]`/`[platforms]`.

---

## 1. Own-backend maturity INVENTORY today (concrete honest-stops / gaps)

The own AOT backend is **mature in its BOTTOM half, immature in its TOP half:**

| layer | file(s) | state |
|---|---|---|
| **TAST → LIR lowering** | `lir/lower.tks` | **"N1 reference subset" (#221) — the big gap.** 60 NAMED honest-stops. Lowers only: integer/float **literals**, **integer** arithmetic/unary, locals, `to` numeric casts, **direct** calls (Teko-Teko + `exit`/`panic`), `let`, `return`, expr-statements, basic `if`/`match`. |
| **isel** | `isel_{x86_64,arm64,riscv}.tks` | mature for **integer/control/memory**; 32 honest-stops in x86 dominated by the **`B1-fp` FLOAT family** (float ALU/compare/negate/convert/literal → SSE) + **i128** materialization. |
| **encode** | `encode_{x86_64,arm64,riscv}.tks` (+consts) | mature, tested (~80 KB each); full integer/mem/control instruction set. |
| **regalloc** | `regalloc{,_x86,_riscv}.tks` | present, tested. |
| **objfile** | `objfile_{elf,macho,coff}.tks`, `objfile_elf_riscv.tks` | emit **relocatable objects** with undef symbols + relocations; needs completion to the **whole-program** section/symbol/reloc set + **`.a` archive** emission. Final link is the **system `ld`**; the own **E1 linker** (`objfile_elf.tks:383`) is a LATER *link-independence* epic, **NOT a kill-C prerequisite** (§4). |
| **ABI** | `abi_{sysv64,aapcs64,win64,riscv64}.tks` | classification tables present, tested — **no varargs** rule set yet. |
| **oracle** | `lir_interp.tks`, `minst_interp.tks` | interpret the covered subset, **mirroring the C backend's honest-stops** — how the native path is validated **without producing binaries** today. |

**The 60 `lower.tks` honest-stops, categorized (what the own AOT backend does NOT yet emit):**

| class | examples (cite) | substrate ready? |
|---|---|---|
| integer **comparisons** | `<`/`<=`/`>`/`>=`/`==`/`!=` (`lower.tks:807,821`) — LIR has `ICmp*` (`lir.tks:33`) | **YES** |
| **bitwise / shifts** | `&`/`\|`/`^`/`<<`/`>>` (`lir.tks:32` `IAnd/IOr/IXor/IShl/IShrS/IShrU`) | **YES** |
| unary `~` | `lower.tks:309` | **YES** |
| **fat pointers** (str/slice) | "wider fat-pointer ABI N2 gap" (`lower.tks:936,3767`, #382) | mostly (pin the fat ABI) |
| **struct / field / index** | out-of-subset expr nodes (`lower.tks:615`) | **YES** (mem loads/stores) |
| **match patterns** | range/alt/null/slice/field (`lower.tks:3034…3389`) | mostly (variant repr exists) |
| if-without-else value; **destructuring** binding | `lower.tks:2777,1819` | **YES** |
| **interpolation** holes | integer/bool ready; float/typed/format-spec (`lower.tks:3972,3987,3998`) | integer YES; float blocked |
| **runtime builtins** (concat/format/…) | outside the N1 run subset (`lower.tks:1059,4006`) | lowering YES; native RUN needs the runtime linked (system `ld`) |
| **FLOAT family** (all float ops) | `lower.tks:277,309,784` + isel `B1-fp` (`isel_x86_64.tks:198,325,439`) | **NO — own-backend float isel** |
| **i128** | isel i128 materialization (`isel_x86_64.tks:170`) | partial — **own-backend i128 isel** |

**Headline:** of ~60 lowering honest-stops, **~40 are integer/control/memory/fat/match/struct/pointer
lowering whose substrate exists → CLOSEABLE in .30**; **~20 are the FLOAT family + i128 → own-backend
float/i128 isel** (own-native, no external dep). **Objfile emission must be completed to whole-program
+ `.a`** (own-native). **NONE needs the E1 linker.**

---

## 2. Classification — each kill-C epic item: PODE .30 AGORA vs DEPENDÊNCIA DURA

### (a) Own-backend honest-stops (lowering)

| item | verdict | detail |
|---|---|---|
| integer comparisons; bitwise/shift; `~`; remainder | **PODE .30** | LIR/isel/encode/interp ready — pure `lower.tks` completion |
| fat-pointer (str/slice) ABI + lowering | **PODE .30** | pin the fat ABI (ptr+len) once; big but substrate ready |
| struct/field/index; match patterns; if-value; destructuring; integer interpolation | **PODE .30** | mem ops / variant repr known |
| **float family** (all float ops + float interpolation/match) | **DEP DURA (own-native)** | X = **own-backend float isel (`B1-fp`) + FPR regalloc/spill** — no external dep; pull-forward where regalloc supports, residual with SW13 |
| **i128** ops | **DEP DURA (own-native)** | X = **own-backend i128 register-pair isel** (#222, SW13) |

### (b) Own-native `.o`/`.a` emission + system-`ld` link (the NEW kill-C substrate)

| item | verdict | detail |
|---|---|---|
| **complete whole-program `.o` emission** | **PODE .30** | extend `objfile_{elf,macho,coff}` from per-fixture objects to the whole program |
| **`.a` static-archive emission** | **PODE .30** | add the archive writer (ar/COFF-lib) beside the object writers |
| **build-step swap: "emit `.o`/`.a` + system `ld`"** (behind a flag; default flips when lowering=100%) | **PODE .30 (prep)** | add the alternate path + `ld` invocation per OS/arch |
| **runnable native binary / full self-host** | **ACHIEVABLE with system `ld`** (NOT E1) | gated only on lowering=100% (float+i128) + complete `.o`/`.a` emission — both own-native |

### (c) The FFI KC1–KC4 — E1 removed from the chain

| item | verdict | detail |
|---|---|---|
| **raw-pointer ops lowering** (`*p`/`p[i]`/`p->f`/arith/`addr_of`) | **PODE .30** | LIR mem loads/stores + address arithmetic |
| **macro resolver Tier 0** (constants/flags) | **PODE .30** | compiler front-end; no backend |
| **macro resolver Tier 2** (body→IR) | **PODE .30** *(after bitwise/shift lowering)* | expands to integer arithmetic/bitwise LIR |
| **macro resolver Tier 1** (symbol alias) | **binding PODE .30; link via SYSTEM `ld`** | bind to the real symbol in .30; the system `ld` resolves it |
| **KC1 vararg ABI** | **PARTIAL .30; native call via SYSTEM `ld`** | per-target vararg rule set PODE .30; the real libc variadic CALL links via the system `ld` |
| **`#repr("c")` layout / `cabi` callbacks / `exp fn` C-ABI symbol emission** | **PODE .30** | objfile emits GLOBAL/FUNC symbols + layout is codegen; export marker is the EXISTING `exp` visibility (no `#[export]` attribute), emitted under a FLATTENED C symbol when `abi="c"` (§5.2.1; codegen.tks:7515 no-mangle path) |
| **`emit_c_header` `.h`** | **PODE .30** | pure text from the checked program — backend-independent |
| **KC3 external-C-symbol resolution** | **SYSTEM `ld`** (NOT E1) | own-native undef-symbol/reloc emission (.30) + system `ld` resolves the C libs |
| **KC4 delete the C-FFI emitter** | **when lowering=100% + `.o`/`.a` + `ld`-link** (NOT E1) | the LAST kill-C step |

### (d) The own E1 linker

| item | verdict | detail |
|---|---|---|
| undef-symbol + relocation EMISSION in objfiles | **already present / complete in .30** | the linker INPUT is ready; whole-program completion is §2(b) |
| the **E1 own linker** itself (replace the system `ld`) | **LATER independence epic — NOT a kill-C blocker** | the system `ld` carries the link until E1 |

---

## 3. The .30 pull-forward crumb sequence (own-backend maturity + `.o`/`.a` + FFI own-native)

Each is own-native, additive, seed-safe, and rides **GATE-G + fixpoint gen1==gen2**; lowering crumbs are
validated by the **interp oracle** + `isel/encode/objfile` unit tests. **Ordered by dependency + impact:**

| crumb | size | closes | notes |
|---|---|---|---|
| **KP1** integer **comparison** lowering | S | `lower.tks:807,821` | `ICmp*` + setcc |
| **KP2** **bitwise + shift** lowering | S | `lower.tks:290,309` | unblocks macro Tier 2 |
| **KP3** **struct / field / index** lowering | M | `lower.tks:615` | mem loads/stores |
| **KP4** **fat-pointer ABI** (ptr+len) + str/slice literal/`.len`/index | L | `lower.tks:936,3767` | the biggest self-host lever |
| **KP5** **match-pattern** lowering (null/field/slice; range/alt) | M | `lower.tks:3034…3389` | variant repr known |
| **KP6** if-without-else-value + **destructuring** + integer **interpolation** | M | `lower.tks:2777,1819,3987` | float/bool holes stay blocked |
| **KP7** widen **direct-call / runtime-builtin** lowering | M | `lower.tks:1059` | more builtins into the run subset |
| **KP8** **raw-pointer ops** (`*p`/`p[i]`/`p->f`/arith/`addr_of`) | M | FFI | mem ops; own-native |
| **KP9** **`#repr("c")` layout** + **`extern union`** own layout | M | FFI G2 | objfile-tested |
| **KP10** **`cabi` callback** emission + **`exp fn` unmangled C-ABI export** (reuse `exp`; C symbol = FLATTENED canonical, §5.2.1; unmangled only when `abi="c"`) + FFI-safe gate | M | FFI G3 / reverse-FFI | objfile GLOBAL/FUNC; NO `#[export]` attribute |
| **KP11** **`emit_c_header` `.h`** (prototypes + `#define` consts; close `header.tks:91` G5) | M | reverse-FFI | pure text |
| **KP12** **macro resolver** Tier 0 + Tier 2 + Tier 3 honest-error | L | FFI macros | Tier 1 link via system `ld` |
| **KP13** **vararg ABI rule set** (SysV/AAPCS64/Win64) + interp | M | FFI KC1 | native call via system `ld` |
| **KP14** **`#cconv` + `teko::ffi::errno()`** emission | S–M | FFI G4 | common cconv cases |
| **KP15** **`teko_rt_init/shutdown`** + panic-not-into-C runtime | S | reverse-FFI | own-native |
| **KP16** **complete whole-program `.o`** emission + **`.a` archive** writer | L | §2(b) | the kill-C substrate |
| **KP17** **build-step swap PREP**: "emit `.o`/`.a` + system `ld`" path behind a flag (per OS/arch); DEFAULT stays C | M | §2(b) | flips when lowering=100% |

**KP1–KP17 take the own backend to "integer+memory+fat+struct+match+FFI-codegen complete, emitting
whole-program `.o`/`.a` linkable by the system `ld`."** After .30, the only residue before flipping the
default off C is the **float+i128 isel** — exactly the owner's goal.

---

## 4. Hard dependencies — re-mapped (own-native only; E1 removed as a blocker)

| item | blocked on X | own-native? | earliest |
|---|---|---|---|
| all **float** lowering + float FFI payloads | own-backend float isel (`B1-fp`) + FPR regalloc/spill | **yes** | pull-forward into .30 where regalloc supports; residual SW13 |
| **i128** ops | own-backend i128 register-pair isel (#222) | **yes** | SW13 |
| whole-program **`.o`/`.a`** emission | own-native objfile completion (KP16) | **yes** | **.30** |
| **native binary / full self-host / KC4 delete-C** | lowering=100% + `.o`/`.a` + system-`ld` link (KP17 flip) | **yes** | as soon as float+i128 isel close |
| **KC3** ext-symbol resolution · **macro Tier 1** link · **KC1** native variadic call | the **system `ld`** at link (undef-symbol/reloc emission is .30) | **yes** | with KP17 |
| **own E1 linker** (replace system `ld`) | the link-independence epic | **yes** | **LATER — NOT a blocker** |

**Re-mapped critical path to kill C:** (1) close float+i128 isel → (2) complete `.o`/`.a` emission → (3)
swap build to `.o`/`.a` + **system `ld`**. All own-native; **E1 blocks nothing** and is a subsequent
link-independence epic.

---

## 5. FFI VALIDATION — the test lane, cross-OS/arch (owner .30 requirement)

Exercised by the **gen1 binary** from the dry build — `teko build . --no-verify --release` → gen1, then
**`./bin/teko test .`** runs the fixtures. Fixtures must need **nothing beyond gen1 + the runner's host
toolchain**, and **must pass own-native** (a fixture passing only via the C emitter is a regression).

### 5.1 Where it wires

- **Surface:** each FFI fixture is a standalone project under **`examples/regressions/ffi/<name>/`**, a
  first-class **`.tkb`** regressive (`tkb-regression-format.md`), picked up by **`[tests] regression =
  ["examples/regressions"]`**. The runner (`build/regression.tks`) builds once + runs, asserting
  **exit/stdout/stderr**; `EXPECT_COMPILE_FAIL` for negatives; `trap` for panics.
- **Own-native build:** FFI fixtures build through the **own backend** (`Given backend = "own"` /
  `--backend=own`), NOT the C emitter. A fixture whose own-backend path is not yet landed is marked
  `Given pending = "<crumb>"` and **skipped-green** until its crumb lands.
- **Cross-OS/arch matrix:** the lane runs on **each CI host** (`[platforms]` × arch runners);
  `targets = ["host"]`. Platform-specific results use the `.tkb` **`on "<os-arch>"`** override.
- **Link is the system `ld`; `cc` only compiles the reverse-FFI C consumer.** A reverse-FFI fixture
  carries a minimal **`consumer.c`**; the runner: **(1)** `teko build` the `abi="c"` artifact own-native
  (own backend emits `.o`/`.a` + `emit_c_header` emits the `.h`); **(2)** compile `consumer.c` with the
  host `cc` and **link Teko `.o`/`.a` + the consumer object with the SYSTEM `ld`**; **(3)** run + assert.

### 5.2 The FFI fixture suite (inputs → expected exit, per platform)

**A. USING C from Teko** (native oracle = own backend):

| fixture | asserts | oracle | crumb |
|---|---|---|---|
| `extern_fn_libc_call` | `extern fn strlen(...)` → exit = len | exit | KP7 + system-`ld` link |
| `extern_macro_const_flag` | Tier-0 `O_RDONLY` inlined; exit = value | exit | **KP12 (.30, no link)** |
| `extern_macro_htonl_expand` | Tier-2 byte-swap → IR; exit = swapped byte | exit | **KP12 (.30)** |
| `extern_macro_complex_rejected` | Tier-3 arbitrary macro | `EXPECT_COMPILE_FAIL` | KP12 (.30) |
| `ptr_deref_index_arrow` | `*p`, `p[i]`, `p->f` in `unsafe` | exit | **KP8 (.30)** |
| `ptr_arith_addr_of` | `p+n`, `p-q`, `addr_of(x)` | exit | **KP8 (.30)** |
| `ptr_null_union` | `ptr<T>\|null` / `uptr\|null` null test | exit | **KP8 (.30)** |
| `ptr_deref_in_safe_rejected` / `ptr_void_deref_rejected` / `ptr_optional_question_rejected` | safe deref / `void*` deref / `ptr<T>?` | `EXPECT_COMPILE_FAIL` | KP8 (.30) |
| `repr_c_struct_byval` | `#repr("c")` struct to a C stub | exit | **KP9 (.30)** + link |
| `extern_union_layout` | `extern union` read/write | exit | **KP9 (.30)** |
| `cabi_callback_noncapturing` | non-capturing closure as C callback | exit | **KP10 (.30)** + link |
| `cabi_callback_capturing_rejected` | capturing closure coerced to `cabi` | `EXPECT_COMPILE_FAIL` | KP10 (.30) |
| `variadic_printf` | `extern fn printf(fmt, ...)`; stdout | stdout | KP13 tables (.30); native call green when KC1 lands |
| `cconv_stdcall` *(windows)* | `#cconv("stdcall")` call | exit | **KP14 (.30)** |
| `ffi_errno_read` | `teko::ffi::errno()` after a failing C call | exit | **KP14 (.30)** |

**B. EXPORTING Teko to C** (reverse-FFI; the marker is the EXISTING `exp` visibility + the `abi="c"`
knob — NO `#[export]` attribute; the C symbol is the FLATTENED canonical, §5.2.1):

| fixture | asserts | oracle | crumb |
|---|---|---|---|
| `revffi_export_scalar` | `exp fn add(a,b)` in a `abi="c"` artifact under root ns `demo`; `consumer.c` links + calls `demo_add(2,3)` | exit = 5 | **KP10 emit (.30); system-`ld` link** |
| `revffi_c_header_emitted` | the generated `.h` (iterating the artifact's `exp fn` + referenced `#repr("c")`) compiles under host `cc` (prototypes + `#define` consts) | consumer compiles | **KP11 (.30, pure text)** |
| `revffi_namespaced_flatten` | `exp fn demo::math::add` → C symbol `demo_math_add`; consumer calls it | exit | **KP10 (.30)** |
| `revffi_symbol_override` | `exp("teko_add") fn add` → C symbol `teko_add` (the override grafia) | exit | **KP10 (.30)** |
| `revffi_symbol_collision_rejected` | two exports flattening to the same C name | `EXPECT_COMPILE_FAIL` | KP10 (.30) |
| `revffi_repr_c_return` | an `exp fn` returns a `#repr("c")` struct; consumer reads fields | stdout | **KP9+KP10 (.30)** |
| `revffi_rt_init_contract` | `consumer.c` calls `teko_rt_init` then an `exp` allocating fn | exit | **KP15 (.30)** |
| `revffi_panic_no_unwind` | an `exp fn` that would panic returns `… \| error`, no unwind into C | exit (error code) | **KP10+KP15 (.30)** |
| `revffi_export_nonffisafe_rejected` | `abi="c"` + `exp fn` with a capturing-closure param / bare-ref return | `EXPECT_COMPILE_FAIL` | KP10 (.30) |
| `revffi_export_teko_variadic_rejected` | `abi="c"` + `exp fn` with a `params []T` variadic | `EXPECT_COMPILE_FAIL` | KP10 (.30) |

### 5.2.1 The C export SYMBOL-NAME rule (`exp` + `abi="c"`) — RATIFIABLE

**The tension:** Teko's `.tkb`/`.tkh` carry the FULL canonical namespace on an export
(`demo::math::add`); C++ expresses that via mangling, but **pure C has no namespaces — symbols are
flat**. So a namespaced `exp fn` exported to C needs a name rule. **As-built mangling (verified):** the
internal Teko fn symbol is `cb_fn_name(ns, name)` = the namespace with each `::` collapsed to `__`,
then a `__` separator, then the name — e.g. `teko::emit::foo` → **`teko__emit__foo`** (double-underscore,
collision-safe); `extern` emits its raw `c_symbol` verbatim at `codegen.tks:7515` (no mangle); types
are `tk_t_<ns__name>`.

**RECOMMENDED RULE — flatten the FULL canonical, single-underscore (owner's option (a)):**

> The C symbol of an `exp fn` in an `abi="c"` artifact = **`flatten(canonical)`**, where
> `canonical = <f.namespace> :: <f.name>` and `flatten` maps each `::` → **`_`** (one underscore),
> producing a legal, greppable C identifier. `demo::math::add` → **`demo_math_add`**.

Key property: because the canonical root **IS the project name** (`teko.tkp` `name`; the source root is
invisible in addressing), flattening the FULL canonical **auto-prefixes every export with the project
name** — so option (a) **subsumes option (c)** (`<proj>_name`) for free, with the same libc-collision
resistance hand-written C libs get from a manual prefix (`sqlite3_open`, `SDL_Init`). **Rejected: the
bare name (b)** — `add` alone invites link collisions with libc/other libs; documented as unsafe, not
the default.

**The `.tkb`/`.tkh` are UNCHANGED** — they keep the full canonical `demo::math::add` for Teko consumers
(Teko-to-Teko addressing untouched). **Only the C `.h` (`emit_c_header`) and the emitted object's
exported symbol flatten**; the `.h` prototype and the object symbol use the SAME flat name.

**Codegen wiring (where it lands):** add `cg_c_export_symbol(f) = <override if present> else
flatten_canonical(f.namespace, f.name)`. In `emit_function_sig`, when `f` is `exp` **and** the artifact
is `abi="c"`, the name branch emits `cg_c_export_symbol(f)` through the **same no-mangle path as the
`extern` arm** (`codegen.tks:7515`) — i.e. **7515 now receives the FLATTENED name, not the bare name and
not the `__` internal mangle**. Internal Teko callers of an exported fn resolve to the same flat symbol
(`cb_fn_name` consults the `exp`+`abi="c"` flag) so definition and call agree; the alternative (emit the
flat name as a thin forwarding wrapper / symbol alias over the internal `__` symbol, leaving internal
linkage untouched) is the fallback if the owner prefers to preserve the internal mangle for exports.

**Collisions (handled, not hand-waved):**
- **Intra-artifact:** two exports whose canonicals flatten to the same C symbol (`a::b_c::d` vs
  `a::b::c_d` → both `a_b_c_d`) — a **compile error at the export gate**, naming both canonicals and
  requiring an override on one. LOW probability (needs a literal `_` in a segment aligning with a `::`
  boundary). The check runs over the WHOLE emitted symbol set of the `abi="c"` artifact (flattened
  exports + `__`-mangled internals + `extern` `c_symbol`s), since a root-ns bare fn named `a_b` also
  flattens into that space.
- **Against libc / other libs:** the mandatory project-root prefix makes this rare; a genuine clash
  surfaces at the **system `ld`** link, documented as the dev's to resolve via the override.

**Explicit override (minimal grafia — NO `#[export]` attribute):** **`exp("c_name") fn foo(...)`** — a
parenthesized C-symbol string on the existing `exp` visibility keyword (the same intent as `extern`'s
`= "sym"`, without a new attribute). Meaningful **only** when the artifact is `abi="c"` (ignored with a
warning otherwise); used for a fixed C name a C API demands, or to resolve a reported collision.
`exp("teko_add") fn add(a: i64, b: i64) -> i64` → C symbol `teko_add`.

### 5.3 The OS/arch matrix

| lane host | arch | notes |
|---|---|---|
| Linux | **x86_64**, **arm64**, **riscv64** | full matrix; riscv via the riscv objfile/encode/abi |
| macOS | **arm64** | AAPCS64 (darwin vararg-on-stack variant) |
| Windows | **x86_64**, **arm64** | COFF; Win64 vararg (float-dup-into-GPR); `#cconv("stdcall")` here |

Fixtures whose native path is own-backend-coupled (`variadic_*` → KC1/C-call) are **"green when the
crumb lands"** via `Given pending`. **No fixture is gated on E1** — the link is the system `ld`
throughout; when E1 lands, the same fixtures flip to the own linker with no rewrite (expected
exit/stdout unchanged — only the link tool changes).

### 5.4 The gen1-executable + no-`cc`-for-Teko constraint

- No fixture may require a tool beyond **gen1 + the runner's host `cc`/`ld`.** The Teko side is
  **always own-native**; the LINK is the **system `ld`**; the host `cc` compiles **only** the
  reverse-FFI `consumer.c`. A fixture passing only via the C emitter is a **regression**.

---

## 6. Summary (counts + the re-mapped picture)

- **Own-backend honest-stops inventoried:** **~60 in `lir/lower.tks`** (the TAST→LIR "N1 subset" gap) +
  **~32 in `isel_x86_64.tks`** (dominated by the `B1-fp` FLOAT family + i128), mirrored in
  isel-arm64/riscv + the interp oracle.
- **Closeable in .30 (KP1–KP17):** **~40 of the 60** lowering honest-stops **PLUS the kill-C substrate**
  — whole-program **`.o`/`.a` emission** (KP16) + the **system-`ld` build-step swap prep** (KP17) — and
  the FFI own-native codegen (raw-ptr ops, `#repr("c")`/union, `cabi`/`exp`-C-ABI export, the `.h`
  emitter, macro Tiers 0+2, vararg ABI tables, cconv/errno, rt-init).
- **Hard deps (own-native, NO external blocker):** the **FLOAT family + i128** isel (~20 honest-stops)
  — the ONLY residue before the default flips off C.
- **E1 DISSOLVED as a blocker:** the link is the **system `ld`**; E1 is a LATER independence epic.
  **KC3 / macro-Tier-1 / native-variadic-call resolve via the system `ld`, not E1.**
- **Reverse-FFI symbol rule:** `exp` + `abi="c"` ⇒ the C symbol is the **flattened full canonical**
  (`demo::math::add` → `demo_math_add`), auto-prefixed by the project root; `.tkb`/`.tkh` keep the full
  canonical; intra-artifact flatten-collisions are a compile error; the minimal override is
  **`exp("c_name") fn`** (no `#[export]` attribute). Codegen: the 7515 no-mangle path receives the
  flattened name.
- **FFI validation:** a cross-OS/arch `.tkb` suite under `examples/regressions/ffi/` (both directions +
  pointers + the namespace-flatten/override/collision cases), own-native, gen1-executable,
  system-`ld`-linked (host `cc` only for the reverse-FFI C consumer).

*Design-ahead, doc-only. No product code changed. Part A (`ref`) is out of scope here (another agent).
Owner ratifies before implementation.*
