---
seq: 0156
crumb-id: MEM-Wt
milestone: M5
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [MEM-S1]
sources:
  - "DECISION_LOG.md:D132"                                            # escalation 1 (wrap/unwrap = intrinsic, not Teko body) + escalation 3 (provenance)
  - "DECISION_LOG.md:D133"                                            # reserved-name enforcement = provenance (form 3)
  - "DECISION_LOG.md:D131"                                            # wrap/unwrap exp, no gate; reserved NAMES protected
  - "src/sys/marshall.tks:8,16"                                       # opaque ptr/uptr newtypes (EMPTY body — methods absent)
  - "src/checker/scope.tks:471,610,623"                              # builtin_fn intrinsic name-dispatch (the mold)
  - "src/codegen/codegen.tks:2904,3425,3741"                         # emit_buf_ptr/emit_load_u64 (the emit mold)
  - "src/lir/lower.tks:1256,1741,1746"                               # is_buf_ptr_call/lower_buf_ptr_call (the native mold)
  - "src/checker/collect.tks:1354,1366"                              # check_no_duplicate_types / duplicates_of_reg
  - "src/checker/check_modules.tks:227"                              # global_type_collision_at
  - "src/build/project.tks:296,368"                                  # rt_prelude_units / inject_runtime_prelude (provenance origin)
  - "src/build/discover.tks:2"                                       # SourceFile { path; namespace }
---

# 0156 · MEM-Wt — `wrap`/`unwrap` INTRINSIC (esc.1) + provenance reserved-name enforcement (esc.3)

> The base-type-surface flip: two escalations that are pure type-surface (inert for valid programs → the
> emitted C is unchanged for every path a valid program takes; only a NEW rejection path and two new
> intrinsics appear). Lands FIRST in the sweep because it is the lowest-risk byte-mover and a prerequisite
> for the reball (`MEM-W6` needs `wrap`/`unwrap`; `MEM-W5` needs the `uptr` marshal). No behavioral flip.
> ESCALATION 1a (wrap/unwrap as compiler intrinsic) + ESCALATION 3 (D133 provenance reserved-name).

## Goal

Two independent, low-risk surface additions, sequenced here so the dangerous flips (`W0`..`W6`) build on a
settled type surface:

**A — `wrap`/`unwrap` as a compiler INTRINSIC (escalation 1a, D131/D132).** `src/sys/marshall.tks` already
declares the opaque newtypes with EMPTY bodies (`exp global type ptr = isize { }` / `uptr = usize { }`) —
the two canonical methods do NOT exist yet, and they CANNOT be Teko bodies: `wrap<T>`/`unwrap<T>` are
generic reinterprets and the `.tkh` does not serialize a generic method body. They become INTRINSICS in the
exact mold the arena/marshall already uses (`buf_ptr`/`as_ptr`/`load_u64`/`store_u64`): the checker
recognizes the method by receiver-type (`ptr`/`uptr`) + name and synthesizes the generic-instantiated
signature; the two backends lower it to a BARE reinterpret (zero instructions beyond the type change). The
owner signatures, VERBATIM, identical on `ptr` and `uptr`:

- `ptr::unwrap<T>(ref T): ptr` — STATIC: typed reference → the opaque pointer of that reference.
- `ptr.wrap<T>(): T` — INSTANCE: the opaque pointer → the `T` at it (reinterpret).

USE stays deferred to `MEM-W6` (the mass reball) and `MEM-W5` (`uptr` in the fat pointer) — this crumb only
lands the MECHANISM, inert until a caller appears, so the build is byte-preserving on 64-bit.

**B — provenance reserved-name enforcement (escalation 3, D133 form 3).** A user program may NOT define a
`type` whose name collides with a keyword-RESERVED base type (`str`/`char`/`byte`/`bool`/`ptr`/`uptr`/
`isize`/`usize`/`u8..u64`/`i8..i64`/`f32`/`f64`). The marker is PROVENANCE, not project-name nor a build
flag: a decl whose source FILE is one of the prelude-injected paths (`inject_runtime_prelude`, under the
shipped runtime tree — a path a user cannot forge) is base-origin and MAY define a reserved name once; a
user-origin decl of a reserved name is an ERROR. The user keeps D131's freedom to write
`type meustr = []byte { … }` (a NON-reserved name) — only the reserved NAMES are protected.

## Where

### A — the intrinsic
- `src/checker/scope.tks` — a `marshall_method_signature(recv, name, type_args)` beside `builtin_fn`
  (`scope.tks:471`): recognize `unwrap` (static on `ptr`/`uptr`) and `wrap` (instance on `ptr`/`uptr`),
  returning the `<T>`-instantiated `Func` (unwrap: `ref T → ptr`; wrap: `() → T`). The method-call
  resolution for a `ptr`/`uptr` receiver consults it before expecting a Teko body.
- `src/codegen/codegen.tks` — `emit_marshall_wrap`/`emit_marshall_unwrap` beside `emit_buf_ptr`
  (`codegen.tks:2904`), wired into the intrinsic dispatch (`codegen.tks:3741`): emit the operand reinterpret
  (the C is the value in the target representation — a same-word cast / identity; NO copy, NO computation).
- `src/lir/lower.tks` — `is_marshall_wrap_call`/`lower_marshall_wrap_call` beside `lower_buf_ptr_call`
  (`lower.tks:1746`): lower to the bare reinterpret (the LIR value with the target type; no op).
- `src/sys/marshall.tks:8,16` — keep the newtype decls; the methods are intrinsic (NOT Teko bodies), so the
  bodies STAY empty. Opacity = the nominal newtype + no arithmetic operators (`p+n`/`p[n]` still reject).

### B — provenance reserved-name
- `src/build/project.tks` — `inject_runtime_prelude` (`:368`) already builds the prelude `SourceFile`s;
  expose their paths as `base_paths(): []str` (deterministic, from `rt_prelude_units`), threaded into the
  checker collision pass.
- `src/checker/collect.tks` — a `reserved_type_diags(table, base_paths)` beside `check_no_duplicate_types`
  (`:1354`): for each `TypeDecl` reg whose `name ∈ RESERVED_TYPE_NAMES` and whose `file ∉ base_paths`, emit
  `diag_at(file, line, col, $"type '{name}' is reserved")`. `RESERVED_TYPE_NAMES` is the keyword base-type
  set (co-located with `builtin_type`, `scope.tks:242`).
- The existing `duplicates_of_reg`/`global_type_collision_at` stay for same-name collisions; the reserved
  check is the NEW, provenance-gated layer.

## How

1. Add the intrinsic signatures (checker) + reinterpret lowering (both engines); the newtype bodies stay
   empty. A `wrap`/`unwrap` call type-checks and lowers to a same-word reinterpret; arithmetic on `ptr`
   still rejects (opacity preserved).
2. Expose the prelude paths from `inject_runtime_prelude`; thread `base_paths` into the collision pass.
3. Add `reserved_type_diags`: user-origin reserved-name decl → error; base-origin (prelude path) exempt.
4. Both are INERT for valid programs (no caller of `wrap`/`unwrap` yet; no valid user redefines a reserved
   name) → the emitted C is byte-preserving → `[fixpoint]` proves determinism.

```teko
/**
 * reserved_type_diags — reject a user-origin type declaration whose name collides with a keyword-reserved
 * base type. Provenance (D133): a declaration whose source file is one of the prelude-injected base paths
 * defines the reserved name legitimately (one shot); a declaration from any other file is a user program
 * illegally redefining a reserved name. The compiler-base plays by the same rule — it is a base-path
 * consumer of the injected definition, not an exception hardcoded by project name.
 *
 * @param table       the collected type table
 * @param base_paths  the prelude-injected source paths (base provenance), from inject_runtime_prelude
 * @return            one diagnostic per user-origin reserved-name collision (empty when none)
 * @since 0.3.1
 */
fn reserved_type_diags(table: TypeTable, base_paths: []str): []str
```

## Rulings & laws

- **Teko-only:** `scope.tks`/`codegen.tks`/`lower.tks`/`collect.tks`/`project.tks`.
- **Escalation 1 (D131/D132):** `wrap`/`unwrap` are `exp`, EXPOSED, NO privilege gate (§6 aposentar-unsafe
  stands); the safety is the user's. They are INTRINSICS (generic reinterpret does not serialize as a Teko
  body) in the `buf_ptr` mold, lowering to a bare reinterpret. Recover the owner signatures VERBATIM.
- **Escalation 3 (D133):** the marker is PROVENANCE (base-path), NOT `name=="teko"` (breaks multi-project)
  nor a build flag (forgeable). Only reserved NAMES are protected; D131 keeps user newtypes over any base
  with any non-reserved name free.
- **BOUNDARY — base-def consolidation (D133 part c), bounded law-first:** `ptr`/`uptr` are ALREADY
  base-origin (`marshall.tks`, namespace `teko::sys`, prelude-injected); `isize`/`usize` are `PrimKind`
  names (no `TypeDecl`, reserved by name). The full D131 vision (`str=[]byte`/`char=u32`/`u8` as prelude
  `TypeDecl`s) is NOT byte-identical (it re-founds the most-used type mid-sweep) → it is OUT of the sweep's
  fixpoint envelope and stays the post-model retro-feed. This crumb delivers the ENFORCEMENT in full (every
  reserved name protected by provenance); it does NOT move `str`/`char`/`u8` into the prelude. Law-first
  boundary (fixpoint + D68 ratchet govern), NOT a fork.
- **Opacity structural:** nominal newtype + no arithmetic operators; `wrap`/`unwrap` bridge same-base types
  (compile-time safe); the 0011 arena-liveness tag stays the OPTIONAL FFI layer, unused here.
- **W15 full Javadoc; no `//`.** **Safety:** never `teko test .`; subshell `ulimit -v 4718592`.

## Fixtures

Rejection oracles allowed (paths the self-build never drives); the mass reball is `MEM-W6`.

| fixture | asserts | expected |
|---|---|---|
| `marshall_unwrap_wrap_roundtrip` | `ptr::unwrap<T>(ref x).wrap<T>()` yields the original, SAME address (no copy) | 0 |
| `marshall_str_bytes_zerocopy` | `ptr::unwrap<[]str>(ref xs).wrap<[]byte>()` shares the base ptr (flagship) | 0 |
| `marshall_uptr_pair` | the identical pair works on `uptr` | 0 |
| `opaque_ptr_no_arithmetic` | `p + 1` / `p[0]` on `ptr` rejected | EXPECT_COMPILE_FAIL |
| `reserved_name_user_reject` | a user file `type str = …` → `"type 'str' is reserved"` | EXPECT_COMPILE_FAIL |
| `reserved_name_base_ok` | the prelude-origin `ptr`/`uptr` defs compile (base provenance exempt) | 0 |
| `user_newtype_free` | a user `type meustr = []byte { … }` (non-reserved) compiles (D131 freedom) | 0 |

## Gate

`[fixpoint]` — compile + the oracles + `gen2==gen3` (the intrinsics have no caller yet and the reserved
check adds only a rejection path → the emitted C is byte-preserving on 64-bit). "Green" = `wrap`/`unwrap`
type + lower to a bare reinterpret, arithmetic on `ptr` rejects, a user-origin reserved-name is rejected
while the base-origin defs pass, `gen2==gen3`. Reseed-class: `fixpoint-rebuild` (folds into the `MEM-W0`
convention seed — RESEED-2 — or its own harvest).

## Deps

`MEM-S1` (shadow-proven `mem_ptr_bytes_zerocopy` for the flagship).

## Done when

`ptr::unwrap<T>(ref T): ptr` and `ptr.wrap<T>(): T` (and the `uptr` pair) are compiler intrinsics lowering
to a bare reinterpret, arithmetic on `ptr` is rejected, a user program may not redefine a reserved base
type name (provenance-gated) while the prelude-origin base defs are exempt, and the `[fixpoint]` build is
`gen2==gen3`.
