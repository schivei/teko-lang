# Retire `checker::Optional` — `T | null` (`Variant{Null,T}`) for values/FFI, `uptr` for `void*` — 0.3.1

> STATUS: **DESIGN — executable map (design-ahead).** Author: architect. Owner: schivei.
> Date: 2026-08-04. No product `.tks` changed BY THIS DOCUMENT — it is the plan the
> implementer follows next. Every code snippet is full-Javadoc (W15), copy-paste ready.
>
> **Owner ruling (verbatim, PT-BR).** *"Para FFI pode se usar a união `| null` para
> isso e para ponteiros `void*` temos `uptr`."* Reading: retire the `checker::Optional`
> type-former; model value/FFI absence as the union `T | null` (already represented as
> `Variant{Null,T}`), and model raw `void*`-style pointers as `uptr` (`checker::Uptr`).
>
> **Scope fence.** A CONCURRENT agent (`theory/null-sugar`) is deleting the null-SUGAR
> dead code (`SafeFieldAccess`/`Coalesce`/`SafeMethodCall`/`parser::OptionalType` +
> handling, the `?`/`?.`/`??` tokens + parser diagnostics). This document designs the
> retirement of the `checker::Optional` TYPE-FORMER against the POST-sugar-removal tree.
> It does NOT design `T?`/`?.`/`??`. The two efforts touch no shared code (proof: §7 R-6).
>
> **Ratified basis.** `docs/design/null-as-union-type.md` (owner-RATIFIED 2026-07-19, D2–D10)
> and `docs/design/null-union-c3-c7-0.3.0.30.md`. This doc is the remaining Optional-former
> slice of that ratified wave (its C5–C7), narrowed to exclude the sugar and re-grounded on
> the AS-BUILT tree (C1–C4 already landed — §1).

---

## 0. TL;DR

- **`null` literal's new type = `checker::Null{}`** (the 1-byte unit type; it ALREADY
  EXISTS, `type.tks:124`). Retire the `Optional{inner=Void}` bare-null SENTINEL: a bare
  `null` is BORN `Null{}` instead of `Optional{Void}` (`typer.tks:47`). Assignability is
  ordinary union widening, already wired (`null_widens_into` `resolve.tks:951`,
  `from_is_bare_null` `resolve.tks:935`). This is the load-bearing core.
- **FFI returns split.** All existing FFI `Optional` returns are VALUE-optionals
  (error-as-value) → the `T | null` union. `host_error_optional()` (`scope.tks:434`,
  `Optional{Error}`) → `Variant{Null,Error}` (`null | error`). **ZERO** FFI `Optional`
  models a `void*` pointer today, so the `uptr` half is a FORWARD STANDING RULE for FFI
  authors, not a migration of any existing site (proof: §3.3). `checker::Ptr` (opaque
  `Ptr{inner=null}`) and `checker::Uptr` already exist for the pointer cases.
- **~105 raw `Optional` occurrences** across the four hot files (resolve 28, typer 21,
  codegen 49, lower 7) split into: 2 PRODUCERS (sentinel + FFI), ~30 SENTINEL/plumbing
  arms that fold into the existing `Null`/`Variant` rails, and ~40 SUGAR-COUPLED arms the
  concurrent agent removes. §4 is the site-by-site map. **6 crumbs** (O1–O6).
- **Byte-preservation verdict: YES for the compiler's own corpus emission** — provable,
  no theory-ladder rebuild of the native seed required, IF the per-crumb fixpoint gate
  holds. The FFI migration is byte-transparent (the compiler binds host calls through its
  own `error | null` externs, NOT the `Optional` fallback — `scope.tks:456-459`). The
  null-literal pivot is destination-driven in emission, so expected byte-identical; the
  fixpoint ritual is the proof, and the single site to watch is a bare `null` emitted with
  no expected-type context (§5.3). Evidence in §5.

---

## 1. AS-BUILT state (read from the tree at authoring time — verified file:line)

The 0.3.0.30 null-union wave PARTIALLY landed. What ALREADY EXISTS (so this doc does NOT
re-plan it):

- **`Null` type case exists.** `pub type Null = struct { }` (`type.tks:124`), wired into
  `type_eq` (`type.tks` Null arm), `checker::Null → uint8_t` in codegen
  (`codegen.tks:1556`), `type_mangle → "null"` / `cg_opt_mangle → "null"`
  (`codegen.tks:1651,1688`), and the `Type` variant lists `… | Reference | Null`
  (`type.tks:126`).
- **Union accepts `null` + canonical null-first order.** `union_normalize_null`
  (`resolve.tks:1603`) collapses either bare-null representation to ONE leading `Null`;
  `variant_member_admissible` (`resolve.tks:1662`) admits `Null`, rejects `Optional` as a
  member (`resolve.tks:1665`), and relaxes R2 for the two-member `Ref<T> | null` shape.
- **Null widening + niche codegen (C3) landed.** `null_widens_into` (`resolve.tks:951`),
  `from_is_bare_null` (`resolve.tks:935`, recognizes BOTH `Null` and the `Optional{Void}`
  sentinel), `cg_union_niche_member` / `cg_type_is_niche_able` / `cg_niche_*`
  (`codegen.tks:1858-1995`) emit a two-member `X | null` as bare `X` (NULL/`{ptr=NULL}` =
  `null`). `null | error` niche-fills to bare `tk_str` (16B), `ClassRef|null`/`ptr<T>|null`
  to a bare pointer (8B).
- **The FFI externs ALREADY use the union form.** `env.tks:34,38`, `fs.tks:27,34`,
  `io.tks:41,46` all declare `-> error | null` (e.g. `pub extern fn chdir(path: str) ->
  error | null = "tk_rt_chdir" from "teko_rt"`). The real C ABI is the FIXED marshalling
  struct `tk_ffi_ures = { bool ok; tk_str err; }` (`teko_rt.h:358`), lifted by
  `cg_emit_ffi_ures` (`codegen.tks:5344`) — a DEDICATED FFI lift, independent of the
  generic `Optional`/`Variant` representation.

What HAS NOT been done (this doc's work — the remaining Optional-former slice):

1. **The bare `null` literal is STILL born as the sentinel** `Optional{inner=Void}`
   (`typer.tks:47`).
2. **`host_error_optional()` STILL returns `Optional{Error}`** (`scope.tks:434`), used by
   the standalone-user host-surface FALLBACK (`scope.tks:474,475,479`).
3. **The `Optional` former + its ~30 non-sugar arms remain** across
   type/resolve/codegen/lower/emit/monomorph/borrow (§4).

---

## 2. The bare `null` literal's new type (the load-bearing core)

### 2.1 Decision: `null` is born `checker::Null{}`

Post-Optional, a bare `null` literal's `.type` is **`checker::Null{}`** — the concrete
1-byte unit type — NOT `Optional{inner=Void}`. `Null` is a leaf type (no inner), already a
first-class `Type` case (`type.tks:124-126`). This retires the "sentinel optional that
unifies with any concrete optional" dance in favor of ordinary union set-membership.

```teko
/**
 * type_nulllit — a bare `null` literal is the concrete unit value `null : Null`. It
 * WIDENS by set-membership into any union listing `null` (see `null_widens_into`); the
 * destination type supplies the surrounding union at the binding/return/arm slot, exactly
 * as a variant case widens into its variant. Retires the legacy `Optional{inner=Void}`
 * bare-null sentinel (REBOOT_PLAN §202), which required a special "sentinel inner == Void
 * unifies with any optional" arm in `type_eq`.
 *
 * @param n  the parsed `null` literal node
 * @return   a `TExpr` whose `.type` is `Null{}` (never fails; the `| error` is the
 *           uniform typer return shape)
 * @since 0.3.1 (retire-Optional)
 */
fn type_nulllit(n: parser::NullLit) -> TExpr | error {
    TExpr { kind = TNullLit { }; type = Null { }; line = 0; col = 0 }
}
```

### 2.2 Why this is safe — the assignability rails already exist

A `null` value never carries its own emission shape to a use site; the DESTINATION drives
both checking and codegen:

- **Checking.** `widens_into_at` (`resolve.tks:858`) already routes a bare null through
  `from_is_bare_null(from) && null_widens_into(to, table)` (`resolve.tks:861`).
  `from_is_bare_null` (`resolve.tks:935`) ALREADY accepts BOTH `Null` and `Optional{Void}`,
  so flipping the birth type from `Optional{Void}` to `Null` changes nothing in the widen
  decision — the `Null` arm is already there. The legacy `T → T?` present-wrap arm
  (`resolve.tks:866-868`) and the `type_eq` sentinel arm (`type.tks:166-172`) become dead
  once no `Optional` destination exists (removed in O5/O6).
- **Codegen.** A `null` value is emitted per the EXPECTED slot type via `emit_as`/`cg_wrap`
  (niche → `cg_emit_niche_null` `codegen.tks:1957`; tagged union → the union tag=0 path;
  legacy `Optional` slot → `(tk_opt_<inner>){.present=false}`). None of these read the
  null literal's BIRTH type; they read the destination. So retyping the birth is
  emission-neutral wherever a destination exists.

### 2.3 `union_normalize_null` already collapses both representations

`union_normalize_null` (`resolve.tks:1603`) uses `from_is_bare_null` to fold EITHER
representation into one leading `Null` member. Its doc-comment (`resolve.tks:1583-1598`)
records exactly the miscompile that motivated recognizing the sentinel here. After O5 the
sentinel is never produced, so the function keeps working on pure `Null` and the
`Optional{Void}` recognition inside `from_is_bare_null` becomes dead (removed in O6).

---

## 3. FFI returns — the value/pointer split

### 3.1 Every existing FFI `Optional` is a VALUE-optional → `T | null`

`host_error_optional()` (`scope.tks:434`) builds `Optional { inner = Error {} }` — the
`error?` success/failure result of the effect host primitives. Migration: return the union
`Variant { members = [Null{}, Error{}] }` (canonical null-first, matching the externs'
`error | null`).

```teko
/**
 * host_error_optional — the effect-or-error result type (`tk_ffi_ures`-shaped) returned by
 * the host primitives `teko::env::set_var` / `teko::env::chdir` / `teko::io::write_file` /
 * `teko::io::write_file_bytes` / `teko::fs::mkdir` / `teko::fs::remove_file`. Now the
 * canonical union `null | error` (`Variant{Null,Error}`), matching the discovered extern
 * declarations (`env.tks:34,38`, `fs.tks:27,34`, `io.tks:41,46`) EXACTLY, so the checked
 * type of a fallback-resolved host call is identical to the extern-resolved one.
 *
 * @return  a two-member `Variant { Null, error }` (null = success, error = failure)
 * @since 0.3.1 (retire-Optional)
 */
fn host_error_optional() -> Type {
    mut m: []Type = teko::list::push(teko::list::empty(), Null { })
    m = teko::list::push(m, Error { })
    Variant { members = m }
}
```

The three call sites (`scope.tks:474,475,479`) are UNCHANGED (they call
`host_error_optional()` by name). No renaming needed — the name stays honest (it builds the
error-absence type).

### 3.2 The real C ABI is untouched (this is why the FFI half is byte-transparent)

The host functions marshal through the FIXED-ABI struct `tk_ffi_ures = { bool ok; tk_str
err; }` (`teko_rt.h:358`), lifted by `cg_emit_ffi_ures` (`codegen.tks:5344`, callers
`5544/5596`). This lift is keyed on the FFI-result KIND (`"ures"`, `codegen.tks:5578`),
NOT on whether the Teko return type is `Optional{Error}` or `Variant{Null,Error}`. So the
emitted C for a host call is identical under either spelling — the migration does not touch
`teko_rt.{c,h}` (which is FROZEN maintained-C anyway) and does not touch the lift.

### 3.3 The `void*`/`uptr` half — a forward rule, NOT a site migration

Searched the tree: there is **no `checker::Optional` whose inner is a `Ptr`** (no
`Optional{inner=Ptr}`), and no `Optional` used to model a `void*`. The only `Ptr`/`Optional`
adjacency is DOC-COMMENT prose (`type.tks:98,108`) noting the `Optional`↔`T?` naming
convention. Evidence: `grep "Optional.*Ptr\|Ptr.*Optional"` yields only comments;
`type.tks:101-104` shows `Ptr{ inner: Type | null }` — the opaque `void*` is ALREADY
`Ptr{inner=null}` (`ptr<void>`), and `checker::Uptr` (`type.tks:105`) already exists as the
word-size FFI transport. Therefore the owner's *"para ponteiros `void*` temos `uptr`"* is a
STANDING GUIDANCE recorded for FFI authors — "spell a raw address `uptr`, never an
`Optional`-of-pointer" — with ZERO existing sites to migrate. No crumb changes pointer code.

**Enumeration of every FFI `Optional` site and its target:**

| FFI site (file:line) | current | target | class |
|---|---|---|---|
| `host_error_optional()` `scope.tks:434` | `Optional{Error}` | `Variant{Null,Error}` | VALUE (union) |
| `scope.tks:474` (chdir/mkdir/remove_file) | calls above | unchanged call | VALUE (union) |
| `scope.tks:475` (set_var/write_file) | calls above | unchanged call | VALUE (union) |
| `scope.tks:479` (write_file_bytes) | calls above | unchanged call | VALUE (union) |
| any `void*` FFI `Optional` | — (NONE EXIST) | `uptr` (`Uptr`) | POINTER — forward rule only |

The externs themselves (`env/fs/io.tks`) are ALREADY `error | null` — no change.

---

## 4. The ~105 `Optional` sites, grouped by role, with per-group migration

Counts are raw `Optional` occurrences (Grep): resolve.tks 28, typer.tks 21, codegen.tks 49
(24 fully-qualified `checker::Optional`), lower.tks 7, plus type.tks 9, monomorph.tks 5,
revalidate.tks 3, match.tks 2, emit/tkb 5, borrow/spine/consteval/tast/check_modules 1 each.
Excludes false positives (`objfile_coff.tks` = PE "SizeOfOptionalHeader"; `gzip.tks` prose).

Two axes classify every site: **producer vs consumer**, and **mine vs the sugar agent's**.
A `parser::OptionalType` reference (surface `T?`) belongs to the SUGAR agent; a
`checker::Optional` reference is MINE.

### Group A — PRODUCERS (mine; the only two things that make an `Optional` post-sugar)

| site | migration | crumb |
|---|---|---|
| `typer.tks:47` `type_nulllit` births `Optional{Void}` | → born `Null{}` (§2.1) | O1 |
| `scope.tks:434` `host_error_optional` returns `Optional{Error}` | → `Variant{Null,Error}` (§3.1) | O2 |

Once Group A is migrated, NO `checker::Optional` VALUE is produced anywhere; every consumer
arm below is dead and deletable.

### Group B — SENTINEL / null-literal plumbing (mine; folds into the `Null` rail)

| site | role | migration | crumb |
|---|---|---|---|
| `resolve.tks:935` `from_is_bare_null` `Optional{Void}` arm | sentinel recognition | keep through O1–O5, delete arm in O6 (Null arm stays) | O6 |
| `resolve.tks:1508` bare-null test | sentinel | delete arm (Null path remains) | O6 |
| `resolve.tks:866-868` `widens_into_at` `T→T?`/`U?→T?` | present-wrap | delete (no `Optional` destinations after O2) | O6 |
| `resolve.tks:1387` `type_has_sentinel` `Optional` arm | sentinel recursion | delete arm | O6 |
| `type.tks:166-172` `type_eq` `Optional` + Void-unify arm | sentinel equality | delete arm | O6 |
| `match.tks:163,497` `Optional` arm in null/exhaustiveness | match-null | folds into existing `Null`-member exhaustiveness (already present) | O5→O6 |
| `consteval.tks:18`, `tast.tks:33`, `revalidate.tks:54,106,114` `Optional`/`TNullLit` arms | null-lit checking | retype to `Null` expectation; delete `Optional` arm | O5/O6 |

### Group C — GENERIC / mangle / serialize plumbing (mine; dead after producers gone)

| site | role | migration | crumb |
|---|---|---|---|
| `resolve.tks:1484` `subst_type` `Optional` arm | monomorph | delete arm (`Null` is a leaf) | O6 |
| `resolve.tks:1539` `unify` `Optional` arm | inference | delete arm | O6 |
| `resolve.tks:1555` `collect_sig_type_params` `Optional` arm | generics | delete arm | O6 |
| `resolve.tks:1165` `unsafe_carrying_at` `Optional` arm | unsafe scan | delete arm | O6 |
| `resolve.tks:1818` `type_mangle` `opt_` arm | mangling | delete arm | O6 |
| `resolve.tks:1973` `type_render` `_?`/`T?` arm | diagnostics | delete arm | O6 |
| `type.tks:87` decl, `type.tks:126` variant case, `type.tks:207` `type_contains_ref` | the FORMER itself | delete decl + case + arm | O6 |
| `borrow.tks:27` `type_reaches_ref` `Optional` arm | borrow scan | delete arm | O6 |
| `spine.tks` (1 arm), `check_modules.tks:141` (parser::OptionalType) | spine/module walk | `spine` arm delete; `check_modules` is parser::OptionalType → SUGAR agent | O6 / sugar |
| `monomorph.tks:156,225` `mono_type_mangle`/`type_to_texpr` `Optional` arms | monomorph | delete arms (156); 225 produces `parser::OptionalType` → coordinate w/ sugar (§7 R-6) | O6 |
| `emit/tkb_write.tks:42` writes tag 9 = Optional; `tkb_read.tks:130,585` reads tag 9 | .tkb serialize | delete WRITER arm; keep other tags STABLE (do NOT renumber `Null`); reader treats tag 9 as a rejected legacy tag | O6 |
| `codegen.tks` `cg_opt_typename`/`cg_opt_mangle` `Optional` arms (1509,1653,1690,3958,4002,5307,5374,5410) + `tk_opt_<inner>` struct emission | legacy Optional codegen | delete the `checker::Optional` arms + the `tk_opt_<inner>` STRUCT emission ONLY | O6 |
| `lower.tks:3768-3772` `unwrap_optional`, `:6241`, `:7479` `const_decl_is_scalar` | LIR lowering | delete `Optional` arms (Null/Variant paths remain) | O6 |

**CRITICAL non-delete (proven regression risk — §7 R-5):** `cg_opt_mangle` /
`cg_opt_mangle_str` / `cg_opt_key` (`codegen.tks:1637,1674,1730`) ALSO mangle SLICE and
UNION member keys, NOT just optionals. Delete ONLY the `checker::Optional` ARMS inside them
and the `tk_opt_<inner>` struct emission — NEVER the shared mangler functions.

### Group D — SUGAR-COUPLED (NOT mine — the concurrent `theory/null-sugar` agent deletes these)

These reference `checker::Optional` but exist ONLY to service the SUGAR forms (`?.`, `??`,
`T?` surface, safe-nav). They vanish with the sugar and MUST NOT be in my crumbs:

- `typer.tks:1276,2098` (`.`-on-Optional → "use `?.`/`??`" diagnostics),
  `typer.tks:2157,2224-2232,2246,2293,2336,2357,2385,2395,2417,2423` (safe-nav receiver,
  `optional_inner_or_self`, coalesce type-check, for-each pull), `typer.tks:4364-4365`
  (opt/opt param-fit for `?`-sugar), `typer.tks:5216` (`error?` sugar arm).
- `resolve.tks:1750-1763` (`parser::OptionalType` → `Optional{inner}` mapping),
  `resolve.tks:2079,2189,2717` (`parser::OptionalType` subst/collect/normalize).
- `codegen.tks` `parser::OptionalType` arms (1408,1460,1504,2345,2411,2483,2499-2512,3840,
  4598,4652-4658) and `cg_opt_mangle_texpr*`.
- `parser/ast.tks:*`, `parser/type.tks:8,11` (`OptionalType` node), `emit/tkb_frame.tks:198`,
  `tkb_write.tks:311`, `tkb_read.tks:585` (`parser::OptionalType` serialize).

Coordination: the sugar agent's `parser::OptionalType` removal will DELETE `resolve.tks:1750`
(the ONLY bridge from surface `T?` to `checker::Optional`). After that, plus my O1/O2, the
`checker::Optional` former has ZERO producers — the precondition for O6's clean delete.

---

## 5. Fixpoint safety / byte-preservation verdict

**Verdict: the compiler's own corpus emission is BYTE-PRESERVED; no theory-ladder rebuild
of the native seed is required, provided the per-crumb fixpoint gate holds.** Three
independent legs of evidence:

### 5.1 FFI migration — byte-transparent (proven, not merely expected)

The compiler binds its OWN host calls through the DISCOVERED extern declarations, which are
ALREADY `error | null` and which win in `lookup_call` BEFORE the `scope.tks` host-surface
fallback fires (`scope.tks:456-459`, verbatim: *"The compiler's own project keeps binding
these through its discovered extern declarations (which win in `lookup_call` BEFORE this
fallback fires) … additive, and byte-transparent for the compiler's own emission."*).
Therefore migrating `host_error_optional` from `Optional{Error}` to `Variant{Null,Error}`
changes ONLY the standalone-USER fallback path, which the compiler's own corpus never
takes. Compiler emission is unchanged ⇒ `gen1==gen2` holds trivially at O2. The C ABI
(`tk_ffi_ures`) and its lift (`cg_emit_ffi_ures`) are keyed on FFI-kind, not on the Teko
return spelling (§3.2), so even the fallback's emitted C is identical.

### 5.2 Optional-arm deletion — dead-code removal, no corpus-emission change

Every Group B/C arm deleted in O6 is dead once Group A is migrated (no `Optional` value is
produced ⇒ no arm executes). Removing an unexecuted `match` arm cannot change the C emitted
for any corpus program. Removing the `Optional` CASE from the `checker::Type` variant DOES
change the self-hosted compiler's OWN `checker::Type` layout (one fewer tag) — but that is a
SOURCE change to the compiler, not to the corpus it emits; the self-host fixpoint
(`seed→binaryA→binaryB`, both built from the SAME new src) stays internally consistent
because the seed compiles the new src (which never declares `Optional`) uniformly. `.tkb`
tag stability is preserved by NOT renumbering (keep `Null`'s tag; retire tag 9 in place —
Group C).

### 5.3 Null-literal pivot — destination-driven, expected byte-identical (fixpoint is the proof)

A `null` VALUE's emission is driven by the destination slot, not its birth type (§2.2), and
`from_is_bare_null`/`null_widens_into` already treat `Null` and `Optional{Void}` identically
in checking. So retyping the birth (O1) is expected byte-identical for every `null` that has
a destination. **The single site to watch:** a bare `null` emitted with NO expected-type
context (a standalone value). Today such a null is `Optional{Void}`; post-pivot it is
`Null{}` (emits `uint8_t 0`). This is the only place the birth type could leak into
emission. The O1 fixpoint ritual (`gen1==gen2` on the whole `src/` corpus) is the exact
detector: if the corpus contains such a site and its bytes move, the fixpoint FAILS at O1
and the crumb is not done until the site is understood. Recommended O1 fixture:
`t/null_birth_null_type.tks` asserts a bare `null` into every slot class (union member,
former-Optional-now-union, standalone let with annotation) emits identically to the recorded
pre-pivot bytes.

### 5.4 When the theory ladder WOULD be needed (and why it is not, here)

The theory ladder (C leg harvests, gate = gen2 native compiles) is required only when a
crumb changes the BYTES the compiler emits for its own corpus AND the previous native seed
cannot reproduce them. Here: O2 is byte-transparent (§5.1); O3–O6 are dead-arm deletion
(§5.2); O1 is expected byte-identical with the fixpoint as tripwire (§5.3). **So the plan is
BYTE-PRESERVING and needs only the standard self-host fixpoint at each crumb — NO ladder
rebuild** — UNLESS O1's fixpoint surfaces a no-context bare-null site (§5.3), in which case
that ONE site is corrected in O1 and the fixpoint re-run; the ladder is still not needed
because the correction re-establishes byte-identity rather than shipping a new byte layout.

---

## 6. Crumb sequence (O1–O6) — smallest safe, independently gate-able steps

Ritual vocabulary (from the ratified wave): **[fixpoint]** = self-host `gen1==gen2`
byte-identical; **[native #test]** = the affected `_test.tkt` + `scripts/*_regressions.sh`
legs green on the native backend; **[full gate]** = whole `teko test .` + every
`scripts/*_regressions.sh` leg `completed + success` (reserved for O6, the end-state).

**Seed-sequencing.** Each crumb's `src/` must build under the previous released seed. O1/O2
change PRODUCERS only (the consumers still accept `Optional`, so the seed still builds). O6
deletes the case only AFTER O1/O2 removed all producers AND the sugar agent removed the
surface bridge (§7 R-6). This ordering guarantees no intermediate build regresses layout or
breaks the fixpoint.

### O1 — bare `null` born `Null{}` (retire the sentinel producer). S. [fixpoint] + [native #test]. BEHAVIOR-PRESERVING.
`type_nulllit` (`typer.tks:47`) → `Null{}` (§2.1). Keep `from_is_bare_null`'s `Optional{Void}`
arm (a stray sentinel from any not-yet-migrated path still widens). Do NOT delete any arm.
- Function shape: §2.1 `type_nulllit`.
- Touches: `typer.tks:47`; verify `consteval.tks:18`, `tast.tks:33`, `revalidate.tks:54`
  accept a `Null`-typed `TNullLit` (retype their `Optional` expectation to `Null`, keeping
  the `Optional` arm as a tolerated legacy until O6).
- Fixtures: `t/null_birth_null_type.tks` (§5.3, byte-identity of every null slot class);
  `t/null_narrow_match.tks` (a `match x { null => …; T as t => … }` over a migrated union
  still exhausts and binds).

### O2 — `host_error_optional` → `Variant{Null,Error}` (retire the FFI producer). S. [fixpoint] + [native #test]. BYTE-TRANSPARENT (§5.1).
`scope.tks:434` returns the union (§3.1). Call sites unchanged.
- Function shape: §3.1 `host_error_optional`.
- Fixtures: `examples/regressions/host_ffi_error_union/` — a STANDALONE user program (no
  local extern) calling `teko::fs::mkdir` round-trips success (`null`) and failure (`error`)
  natively via the fallback path, `EXPECT_EXIT` distinguishing the two; proves the fallback
  now matches the extern shape.

### O3 — verify no producers remain; add the `uptr`-for-`void*` standing note. S. [fixpoint]. BEHAVIOR-PRESERVING.
A GATE crumb: assert (grep-gate fixture) that no `Optional{…}` CONSTRUCTOR remains in `src/`
outside the to-be-deleted arms — i.e. Group A is fully migrated and the sugar bridge
(`resolve.tks:1750`) is gone (§7 R-6 handshake). Add a doc-comment on `checker::Uptr`
(`type.tks:105`) recording the owner's `void*`→`uptr` rule (honest-stop / no code path
needed — §3.3). This crumb blocks O4+ until the sugar agent's `parser::OptionalType`
removal has merged.
- Fixture: `t/no_optional_producer.tks` (grep-gate: no `Optional {` construction in `src/`).

### O4 — fold the SENTINEL consumer arms into the `Null` rail. M. [fixpoint] + [native #test]. BEHAVIOR-PRESERVING.
Delete the sentinel-specific paths that are now unreachable: `widens_into_at` present-wrap
(`resolve.tks:866-868`), `type_eq` Void-unify (`type.tks:166-172` — leave the case arm for
O6), `type_has_sentinel` `Optional` arm (`resolve.tks:1387`), the `match.tks:163,497`
`Optional` arms (already backed by `Null`-member exhaustiveness). Guard each deletion with
its fixture.
- Fixtures: `t/null_narrow_ifguard.tks` (equality-guard flow-narrowing over the migrated
  union still narrows), `t/empty_slice_sentinel.tks` (the SEPARATE `Slice{Void}` empty
  sentinel is UNTOUCHED — proves I did not over-delete).

### O5 — retype the remaining checker/lower consumer arms to `Null`/`Variant`. M. [fixpoint] + [native #test]. BEHAVIOR-PRESERVING.
`consteval.tks:18`, `tast.tks:33`, `revalidate.tks:54,106,114`, `lower.tks:6241,7479`,
`match.tks` — retype the `Optional` expectations to `Null` / the `Variant` path, so each
compiles with the `Optional` case still PRESENT (last crumb before deletion). No arm that a
LIVE value reaches remains keyed on `Optional`.
- Fixture: `t/optional_free_checker.tks` — a program exercising null in match/if/binding/
  return/FFI compiles with identical native exit codes to its O0 baseline.

### O6 — DELETE the `Optional` former + all dead arms + `.tkb` tag retire. M (net deletion). [fixpoint] + [full gate]. BEHAVIOR-PRESERVING + BYTE-IDENTICAL.
Remove `pub type Optional` (`type.tks:87`), the `| Optional` case (`type.tks:126`), and
EVERY remaining `checker::Optional` arm: `type.tks:166-172,207`; `resolve.tks:935 (Optional
arm only),1165,1484,1508,1539,1555,1818,1973`; `borrow.tks:27`; `spine.tks`;
`monomorph.tks:156`; `codegen.tks` `checker::Optional` arms + the `tk_opt_<inner>` STRUCT
emission (NOT the shared `cg_opt_mangle*` functions — §7 R-5); `lower.tks:3768-3772,7479`;
`emit/tkb_write.tks:42` (writer arm — keep other tags stable). Keep `NullLit`/`NullPattern`/
`Null`. Full gate is the ratified end-state.
- Fixtures: `t/no_optional_former.tks` (the identifier `checker::Optional` no longer exists
  in `src/`; the corpus still builds); `t/tkb_roundtrip_no_opt.tks` (a `.tkb` write/read of a
  null-bearing type round-trips; tag 9 is a rejected legacy tag).

---

## 7. Risks / law tensions (each proven with file:line; owner rule: alarm only if proven)

- **R-1 — `cg_opt_mangle`/`cg_opt_key` double duty (PROVEN, high).** These mangle SLICE and
  UNION member keys too, not just optionals: `cg_opt_mangle` recurses `slice_<elem>`
  (`codegen.tks:1662`), `cg_member_key` calls it for every member
  (`codegen.tks:1730-1740`). RESOLUTION: O6 deletes ONLY the `checker::Optional` ARM inside
  these and the `tk_opt_<inner>` struct emission — NEVER the functions. A broad delete of
  `cg_opt_*` would break slice/union mangling. No tension, but a load-bearing implementer
  warning.

- **R-2 — the null-literal pivot could move bytes at a no-context bare-null site (PROVEN
  possible, low).** `null` emission is destination-driven (§2.2), but a standalone bare
  `null` with no expected type would flip `Optional{Void}`→`Null` in emission (§5.3).
  RESOLUTION: the O1 fixpoint is the exact detector; `t/null_birth_null_type.tks` pins it.
  If it fires, correct that ONE site in O1 (not a ladder rebuild — §5.4).

- **R-3 — `from_is_bare_null` must keep the `Optional{Void}` arm until O6 (PROVEN,
  medium).** `union_normalize_null` (`resolve.tks:1603`) relies on it, and its comment
  (`resolve.tks:1583-1598`) documents a SILENT MISCOMPILE (`bare_null_match_arm_optional.tks`
  KNOWN-STOP) that recognizing the sentinel here prevents. RESOLUTION: sequence — keep the
  arm through O1–O5, delete only in O6 after O1 guarantees no `Optional{Void}` is ever born.
  Deleting it earlier reopens the pinned miscompile.

- **R-4 — `.tkb` tag renumbering would break bytecode round-trip (PROVEN, medium).**
  `tkb_write.tks:42` writes tag 9 for Optional; the reader table (`tkb_read.tks:130`) is
  positional. RESOLUTION: retire tag 9 IN PLACE (do NOT renumber `Null` or any other tag);
  reader rejects a stray tag 9 as a legacy tag. Keeps every non-Optional `.tkb` byte-stable.

- **R-5 — interface-in-union honest-stop must stay intact (PROVEN, medium).**
  `variant_member_admissible` (`resolve.tks:1672-1674`) deliberately still rejects an
  interface `Named` as a union member (the #28 "S4" carve-out,
  `docs/design/interface-value-type.md` §5-S4). RESOLUTION: my crumbs touch the `Optional`
  arm (`resolve.tks:1665`) ONLY; leave the interface arm untouched so the interface-in-union
  keystone relaxes it later on its own terms. No tension.

- **R-6 — cross-agent handshake with `theory/null-sugar` (PROVEN, coordination).** The ONLY
  bridge from surface `T?` to `checker::Optional` is `resolve.tks:1750-1763`
  (`parser::OptionalType → Optional{inner}`), owned by the SUGAR agent. My O6 delete of the
  `Optional` case REQUIRES that bridge already gone (else a live producer remains).
  `monomorph.tks:225` (`type_to_texpr → parser::OptionalType`) and `check_modules.tks:141`
  reference `parser::OptionalType`, also the sugar agent's. RESOLUTION: O3 is a GATE crumb —
  it blocks O4+ until the sugar removal has merged (grep-gate proves no `parser::OptionalType`
  and no `Optional{` producer remain). The two efforts touch DISJOINT symbols
  (`parser::OptionalType` = sugar; `checker::Optional` = mine) except this one bridge, which
  is the sugar agent's to delete — so no code collision, only a merge-order dependency. No
  HALT.

- **Law M.3 (`void` is not a value) — reinforced, no tension.** Retiring the
  `Optional{inner=Void}` sentinel REMOVES the one place `Void` masqueraded inside a value
  type; `null` is the value, `void` stays return-only. The change SHARPENS M.3.

- **Law R2 (references never null) — already relaxed by owner (D3), untouched here.**
  `variant_member_admissible` (`resolve.tks:1666-1671`) already admits `Ref<T> | null`; this
  doc does not alter it. No tension.

**No unresolved tension — no HALT.** Every decision is either owner-ratified
(`null-as-union-type.md` D2–D10) or a mechanical consequence of the owner's retirement
ruling. §6 is the build order.
