---
seq: 0114
crumb-id: NAT-N2
milestone: M4
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [NAT-N1]
sources:
  - "docs/design/recon-native-n1n2-gaps-strategy.md:66-116"          # families 2-4 + KNOWN-WRONG + completion-by-family
  - "src/lir/lower.tks:239"                                          # integer/float/unary operator not yet lowered
  - "src/lir/lower.tks:2174"                                         # fat-pointer interface-dispatch result
  - "src/lir/lower.tks:2322"                                         # fat-pointer closure-call result
  - "src/lir/lower.tks:2376"                                         # mutable str/slice capture
  - "src/lir/lower.tks:2102"                                         # `parse` float result KNOWN-WRONG (XMM0/D0 vs int reg)
  - "src/lir/lower.tks:3918-4081"                                    # match-pattern residual (range/alt/group/float/slice/field)
  - "docs/design/plano-mestre-0.3.1-implementacao.md:282"          # M4 NAT-A1 row (this crumb extends it)
---

# 0114 · NAT-N2 — fat-pointer dispatch/closure + match-pattern + operator residual + KNOWN-WRONG

> Drain the remaining native-lowering N1/N2 honest-stops (families 2-4 of the recon) AND flip the 21
> KNOWN-WRONG miscompiles to correct — the residual after NAT-A1 (`0097`) and NAT-N1 (`0113`) so the
> compiler's own corpus lowers natively without a stop and without a silently-wrong result.

## Goal

`recon-native-n1n2-gaps-strategy.md:33` measures the native subset as **84 KNOWN-STOP · 21 KNOWN-WRONG
· 20 BLOCKED**. NAT-A1 opened the frontier and NAT-N1 cleared the largest (union/nullable) family;
this crumb closes the rest of the KNOWN-STOP families and — the part no completeness crumb covers — the
**KNOWN-WRONG** items, where lowering does NOT stop but emits a wrong result (the sharpest example:
`lower.tks:2102`, a `teko::float::parse` result returned in XMM0/D0 that `select_call_result_*` would
capture from an integer register, handing back a silently wrong float). Byte-preserving on the C route;
drives a **fixpoint-rebuild**. The 20 BLOCKED items are blocked on other legs (`§10` concurrency
surface, `with_cap`/`push` which M3 `COL-F2` removes) and are tracked, not closed here.

## Where

- `src/lir/lower.tks:239` / `:255` / `:290` — float / integer / unary "operator not yet lowered (N2)":
  fill the residual `LBinOp`/`LUnOp` arms for the operators `src/` actually uses.
- `src/lir/lower.tks:2174` / `:4267` / `:4273` — fat-pointer interface-dispatch result: a method
  returning a `str`/`[]T` through a vtable slot must thread the two-VReg `{ptr,len}` result.
- `src/lir/lower.tks:2322` — fat-pointer closure-call result (same two-VReg threading through
  `LCallIndirect`).
- `src/lir/lower.tks:2376` — capturing a MUTABLE `str`/`slice` local: the env slot must carry BOTH
  halves of the fat pair, not the frame-slot address.
- `src/lir/lower.tks:2187` — dynamic dispatch through a polymorphic base class (a class instance
  carries no vtable): give the class instance a vtable slot or route to the monomorphic call.
- `src/lir/lower.tks:2807` — destructuring binding (`var [a, b] = arr`) native lowering.
- `src/lir/lower.tks:2830` / `:2856` — fat-typed lambda return; diverging panic/exit inside a
  fat-returning function.
- `src/lir/lower.tks:3918-4081` — match-pattern residual: range pattern, alternation `|`, grouped bind
  `(A | B) as v`, float pattern, slice pattern over a non-variant subject, field pattern over a
  non-variant subject.
- `src/lir/lower.tks:2102` — the **KNOWN-WRONG** `parse` float-result register class: teach `LCall`
  to record the result register CLASS (int vs XMM/D) so `select_call_result_*` reads the right one.
- `src/backend/isel_*.tks` / `regalloc*.tks` — where a KNOWN-WRONG traces to a mis-selected result
  register or ABI slot rather than to `lower.tks`, fix at the isel/regalloc site (audit against the
  `bulk` KNOWN-WRONG list, `docs/memory/bulk-native-verdicts-0.3.1.md`).

## How

Per `recon:105-110`, complete by FAMILY, each gate-able alone and byte-identical to the C route.

1. **Operator residual (small, mechanical).** Add the missing `lbinop_of`/`lunop_of` arms for the
   operators the corpus uses; each maps to an existing `LBinOp`/`LUnOp` — no new op.
2. **Fat-pointer through indirection.** Thread the `{ptr, len}` pair through interface-dispatch and
   closure-call results and through a mutable-fat capture, reusing the `FatVReg` side-table NAT-A1
   introduced (never promote a fat value through a single VReg / block-arg).
3. **Class vtable dispatch.** A polymorphic base-class instance gains a vtable pointer at offset 0 (the
   fat-header item-14 shape already ratified); dispatch loads the slot and `LCallIndirect`s.
4. **Match-pattern residual.** Lower range (`ICmp` low/high), alternation (OR the arm tests), grouped
   bind (test the group, bind on match), float (bit-compare), and slice/field patterns over a
   non-variant subject (length/offset tests) — reusing NAT-A1's per-arm merge.
5. **KNOWN-WRONG: result register class.** The correctness keystone. Extend `LCall` to record the
   callee's result class:

```teko
/**
 * LResultClass — where a call's result comes back: an integer/pointer register, or a floating-point
 * register (XMM on x86-64, D/V on arm64). `LCall`/`LCallIndirect` record it so result capture reads
 * the correct physical register instead of assuming the integer bank.
 *
 * @since 0.3.1
 */
exp type LResultClass = variant { IntReg; FloatReg; FatPair; Void }
```

   Set `LResultClass` at the lowering of every call from the callee's return type; teach
   `select_call_result_*` (isel) and the ABI descriptors to capture from the matching bank. This
   fixes `lower.tks:2102` and every KNOWN-WRONG that is a float/fat result read from the wrong bank.
6. **Audit the KNOWN-WRONG list.** Walk `bulk-native-verdicts-0.3.1.md`'s 21 KNOWN-WRONG; each must
   either become PASS or be re-classed to a named honest-stop (never a silent wrong result) — the
   `.32 work list` discipline (`recon:35`).
7. **BLOCKED items** (20): record each as blocked-on-`<leg>` (`§10` surface = `S10-SURF` `0115`;
   `push`/`with_cap` = removed by `COL-F2` `0093`); do NOT close here.

## Rulings & laws

- **Teko-only:** `src/lir/*.tks` + `src/backend/*.tks`; no C twin.
- **Comment convention (W15, owner 2026-08-19):** `/** */` only on `exp` decls; no `//` or `/* */`;
  never larger than the code (W15 reviewer, not the compiler).
- **Fork protocol (owner 2026-08-19):** the fat-header vtable-at-offset-0 and the result-class fix are
  ratified shapes (item-14 fat header, banked `6f4d78ba`; the recon names the register-bank bug) — no
  undecided fork; do NOT HALT.
- **W15 full Javadoc** on every new `exp` type/fn; flatten; no `//`.
- **No silent wrong result:** a construct that cannot be lowered correctly must honest-stop with a
  named message, NEVER emit a wrong result (the KNOWN-WRONG discipline; owner "fail-loud").
- **Safety:** NEVER `teko test .`; build under `ulimit -v 6815744`; commit each family; `[fixpoint]`
  `gen2==gen3` byte-identical; sweep `.tkt` after any `LCall`/`LResultClass` widening. **Conditional
  (owner 2026-08-19):** DRY build ≤ 1.5 GB unlocks full `teko build .` — measure/report when crossed.
- Rests on: `recon-native-n1n2-gaps-strategy.md:66-116` + `bulk-native-verdicts-0.3.1.md`.

## Fixtures

Mostly self-build exercised. Add ONLY the KNOWN-WRONG correctness anchors the self-build does not
pin as a wrong VALUE (a stop the fixpoint catches; a wrong value it does not):

| fixture | asserts | expected |
|---|---|---|
| `native_float_parse_result` | native `teko::float::parse("1.5")` returns `1.5` (reads XMM0/D0, not the int bank) | `0` |
| `native_iface_dispatch_str` | a native vtable method returning `str` threads `{ptr,len}` correctly | `0` |
| `native_match_alt_range` | native `match` with an alternation `|` and a range pattern selects the right arm | `0` |

## Gate

`[fixpoint]` — `gen2==gen3` byte-identity. "Green" = the compiler's own corpus lowers natively with no
reachable KNOWN-STOP in families 2-4, every KNOWN-WRONG is PASS or a named honest-stop (never a silent
wrong value), and the native rebuild is byte-identical. Reseed-class: `fixpoint-rebuild`.

## Deps

`NAT-N1` (`0113`) — the union/nullable family lands first. **Logical predecessor of `RM-C15`
(`0105`)** — see the flagged INDEX dep edit.

## Done when

Families 2-4 of the native-lowering subset have no reachable honest-stop on `src/`, the 21 KNOWN-WRONG
are resolved (PASS or named stop), and the `gen2==gen3` native rebuild is byte-identical; remaining
BLOCKED items are recorded against their blocking leg.
