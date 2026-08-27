---
seq: 0157
crumb-id: MEM-W0
milestone: M5
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [MEM-Wt]
sources:
  - "DECISION_LOG.md:D132"                                            # escalation 2 — C-route region=param is the byte-mover of highest risk
  - "DECISION_LOG.md:D130 refinement 1"                              # region = implicit PARAMETER (not thread-local)
  - "docs/design/plano-fiacao-modelo-memoria-por-escopo-0.3.1.md:§3" # control reached via region (law-first)
  - "src/lir/lower.tks:454,474,6182"                                 # LowerCtx.region_param/plan (native, ALREADY landed — the mirror)
  - "src/codegen/codegen.tks:7733"                                   # emit_function_sig (the C signature — NO region param today)
  - "src/codegen/codegen.tks:844"                                    # cg_type_table(prog) — TypeTable IS derivable (thread it, don't recompute per fn)
  - "src/runtime/arena.tks:963"                                      # region_control(r) (E1, delegates to ar_control — the ambient fallback)
---

# 0157 · MEM-W0 — thread the implicit region PARAMETER through the C route (escalation 2 core) — RESEED-2

> ESCALATION 2, the byte-mover of HIGHEST risk: the C route SELF-HOSTS today and has NO region parameter at
> all — E4/E5 threaded `region_param`/`plan` into the NATIVE `LowerCtx` only (`lower.tks:454`), never into
> codegen. So `MEM-W1`..`MEM-W4` ("forward the parent's param", "build into the caller's region param")
> have NO param to consume on the C route. This crumb lands that param FIRST and ALONE — a pervasive
> calling-convention change — with the AMBIENT still driving allocation, so semantics are unchanged and any
> regression is bisectable. It is a byte-MOVER (every emitted C signature/call gains an argument) → the gate
> is fixpoint determinism, not byte-identity to the prior seed. This is RESEED-2 (the convention seed).

## Goal

Thread ONE implicit region parameter through every emitted C function, mirroring the native side that
already carries it:

1. **The C emit context.** Introduce a `CgFnCtx` (or extend the threaded emit state) carrying
   `region_param` (the current region handle expression, `ptr`) and `plan: checker::ResidencePlan` and the
   `TypeTable` — mirroring `LowerCtx { region_param; plan }` (`lower.tks:454`). The `TypeTable` is computed
   ONCE via `cg_type_table(prog)` (`codegen.tks:844`) and threaded (NOT recomputed per function — that is
   O(n²) on the hot path and would fight the ratchet).
2. **Signatures.** `emit_function_sig` (`codegen.tks:7733`) prepends the implicit region param to every
   non-extern C signature: `tk_region *__rg` as the FIRST parameter (before `self`), so the `void`-param
   case becomes `(tk_region *__rg)`. Extern/`main` handled per the rules below.
3. **Calls.** Every emitted internal call passes the CURRENT region param as the first argument
   (`region_from_param(ctx)`); the default at the top is the ambient region.
4. **`region_from_param` fallback (coexistence).** A sentinel param value (the `0` sentinel the native side
   uses, `lower.tks:6182 region_param = 0`) means "no explicit region yet → read the ambient":
   `region_from_param` returns the threaded param when set, else `region_control(<ambient>)` = today's
   `ar_control()` chain. So allocation still lands exactly where it does today — byte-preserving SEMANTICS,
   only the plumbing is new. The ambient COEXISTS with the param through the whole sweep; `MEM-W6` removes
   the fallback when `_start` supplies the real root.

`main`, extern functions, function pointers, and vtable thunks: `main` receives no explicit region yet
(its param is `_start`'s root in `MEM-W6`; here it opens/uses the ambient as today); extern C functions
(FFI) do NOT gain the param (their ABI is fixed); function-pointer and vtable signatures gain the param
uniformly so indirect calls stay ABI-consistent (thread it through `cg_emit_fnptr_sig_ex`,
`codegen.tks:1453`, and the vtable thunk emitters, `codegen.tks:9464`).

## Where

- `src/codegen/codegen.tks:7733` (`emit_function_sig`) + the proto emitters (`cg_emit_fn_proto_*`,
  `:9880/:9909`) — prepend `tk_region *__rg` to non-extern, non-main signatures/protos.
- `src/codegen/codegen.tks` (the emit state threaded through `emit_function`/`emit_stmt`/`emit_call`) — add
  `region_param`/`plan`/`table` to the ctx; a `region_from_param(ctx): str` helper returns the current
  region expression (param when set, ambient fallback otherwise).
- `src/codegen/codegen.tks` (call emission) — pass `region_from_param(ctx)` as the first argument of every
  internal call; extern calls unchanged.
- `src/codegen/codegen.tks:1453` (`cg_emit_fnptr_sig_ex`) + `:9464/:9498` (vtable thunks/defs) — thread the
  region param through indirect-call signatures for ABI consistency.
- `src/codegen/codegen.tks:844` (`cg_type_table`) — call ONCE at the emit entrypoint; thread the result.
- `src/runtime/arena.tks:963` (`region_control`) — already delegates to `ar_control()` (the ambient
  fallback); no change here (retired in `MEM-W6`).

## How

1. Compute `TypeTable` once; build `plan = checker::residence_plan(f, table)` per function at emit entry
   (mirror `lower.tks:6182`); seed `region_param` to the sentinel.
2. Prepend `tk_region *__rg` to every non-extern, non-main signature/proto and every indirect signature.
3. Pass `region_from_param(ctx)` at every internal call; sentinel → ambient fallback keeps allocation
   identical.
4. Build gen2/gen3; confirm `gen2==gen3` (the added param is deterministic) and `TEKO_MEM_PARANOID` exit 0
   (semantics unchanged — the ambient still governs).
5. **RESEED-2** — harvest the new calling convention into `bootstrap/teko.c` so the sweep flips build on a
   seed that already speaks the region param. (Reseed is unconditional per the by-agent reseed law.)

## Rulings & laws

- **Teko-only:** `codegen.tks`/`arena.tks`.
- **D130 refinement 1:** region is an implicit PARAMETER, NEVER thread-local/global-var/tid-table. This is
  the C-route realization of what the native side already has; §5-A of the docs (ambient current) is the
  one doc error — ignore it.
- **Coexistence (expurgo law):** the ambient and the param COEXIST during migration; the fallback keeps the
  build green at every step; the ambient dies only in `MEM-W6`.
- **Ratchet (D68) — foundation exception, stated honestly:** this is PLUMBING, not the reclaim; it may move
  the peak by measurement noise (a param per frame; the plan/TypeTable threaded). The gate here is FIXPOINT
  determinism; the strict RSS DOWN is asserted at the behavioral flips (`W2`/`W3`/`W4`/`W6`) where the
  reclaim actually lands. If W0 moves the peak beyond noise, that is a real regression to root-cause (same
  posture as D118's expurgo-grows-transiently), NOT a ratchet waiver.
- **W15 full Javadoc; no `//`.** **Safety:** never `teko test .`; subshell `ulimit -v 4718592`; `[RITUAL]`
  gen2==gen3 + MEM_PARANOID.

## Fixtures

Shadow only (`mem_no_root_leak`, `mem_move_return` re-run to confirm the param is threaded and the ambient
fallback keeps behavior). Non-versioned. No affirmative `.tkr` (the self-build exercises this).

## Gate

`[RITUAL]` — **RESEED-2 (convention seed)**: build gen2, `gen2==gen3` byte-identity (the added region param
is deterministic), `TEKO_MEM_PARANOID` exit 0 (semantics unchanged), peak within measurement noise of the
prior seed (foundation plumbing). "Green" = every emitted C function carries `tk_region *__rg`, every
internal call passes it, `region_from_param` falls back to the ambient, `gen2==gen3`. Reseed-class:
`fixpoint-rebuild` (its OWN harvest — RESEED-2 — because the convention change is the sweep's riskiest and
must be bisectable before the behavioral flips).

## Deps

`MEM-Wt` (the type surface settled).

## Done when

Every emitted C signature/proto/indirect-signature carries the implicit region param, every internal call
passes `region_from_param(ctx)`, the ambient fallback keeps allocation byte-identical in behavior, the
`TypeTable`/`ResidencePlan` are threaded into the C emit ctx (computed once), and RESEED-2 is `gen2==gen3`
under MEM_PARANOID.
