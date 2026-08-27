---
seq: 0158
crumb-id: MEM-W1
milestone: M5
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [MEM-W0]
sources:
  - "docs/design/ast-computed-arena-assessment-0.3.1.md:178-231"      # Idea 2 — arena elision
  - "DECISION_LOG.md:1163"                                            # D130 refinement 4 (slots==0 → forward parent)
  - "src/checker/residence.tks:223"                                   # scope_slot_count (MEM-E2, landed)
  - "src/codegen/codegen.tks:4735,6225,7919"                          # want_block / want_frame (the C region-open predicates)
  - "src/lir/lower.tks"                                               # native region open/close
---

# 0158 · MEM-W1 — elision: `slots==0` ⇒ do NOT open a region, forward the parent's param

> First BEHAVIORAL flip (cheapest, composes with everything): consume `scope_slot_count`. A scope that
> allocates nothing routable (`slots==0`) does NOT materialize its own region — the region PARAMETER
> (threaded by `MEM-W0`) passes straight through. D130 refinement 4 as a HARD rule (`fn a(): i32 { b() }`
> → `a` opens no arena; the caller's region flows direct to `b()`). Now that the C route carries the region
> param (`MEM-W0`), "forward the parent's param" is a real operation on both engines.

## Goal

Refinement 4: `slots==0 ⇒ elide`. Today a region opens by a predicate (`want_block`,
`cg_block_has_block_local`, capped at `regions.len < 64`) OR `#arena_size`. Replace the opening decision
with `scope_slot_count(body, table) > 0` (`residence.tks:223`, landed): when 0, emit NO region-open/close
and thread the RECEIVED region param unchanged to the children (`region_from_param` returns the inherited
param). When > 0, open a child (pre-sized in `MEM-W2`). Conservative (`scope_slot_count` doubt ⇒ non-zero ⇒
still open — never UAF-by-eliding-an-allocating-scope). This is Idea-2 elision, promoted to the uniform
rule that makes `main`/leaf functions costless.

## Where

- `src/codegen/codegen.tks:4735,6225,7919` — replace `want_block`/`want_frame` region-open with
  `scope_slot_count(...) > 0`; when 0, no `_tkbr`/`_tkfr`, forward the region param (`region_from_param`).
  The existing `regions.len < 64` cap stays (it aligns with `TK_REGION_STACK_CAP`; `MEM-W3` formalizes it).
- `src/lir/lower.tks` — the native region open/close: skip `open_native_region` when `slots==0`; forward
  the region param (the native `region_param` already exists, `lower.tks:454`).
- `src/checker/residence.tks:223` — `scope_slot_count` is landed; consume it (no change here).

## How

1. Compute `scope_slot_count(body, table)` at each scope entry (both engines, same TAST walk).
2. `> 0` → open a child region (sized by `MEM-W2`); push it as the current region param for children.
3. `== 0` → open NOTHING; children receive the SAME region param (the parent's) unchanged.
4. Conservative: doubt ⇒ non-zero ⇒ open (leak-safe, never UAF).

## Rulings & laws

- **Teko-only:** `codegen.tks`/`lower.tks`.
- **D130 refinement 4:** `slots==0` is a HARD rule, not a heuristic; uniform with refinement 6 (`main`
  `slots==0` is not special — it receives the root from `_start`, `MEM-W6`).
- **Conservative (never wrongly elide):** doubt ⇒ open ⇒ leak-safe; a missed routable site routes to the
  enclosing region, never UAF (`escape.tks` posture).
- **W15 full Javadoc; no `//`.** **Safety:** never `teko test .`; subshell `ulimit -v 4718592`;
  `[RITUAL]` gen2==gen3 + MEM_PARANOID + the shadow `mem_elide_leaf`.

## Fixtures

Shadow only (`mem_elide_leaf`, scratchpad, MEM-S1): a `slots==0` fn opens no region; the parent region
passes through (asserted via `TEKO_ARENA_OBS` region-created count, not a versioned `.tkr`).

## Gate

`[RITUAL]` — gen2==gen3 (fewer region-open instructions, deterministic) + MEM_PARANOID 0 + `TEKO_ARENA_OBS`
shows elided leaves open no region + RSS peak DOWN or flat (elision removes region headers). "Green" =
`slots==0` scopes emit no region and forward the parent param, allocating scopes still open, `gen2==gen3`.
Reseed-class: `fixpoint-rebuild` (harvested at RESEED-FINAL `MEM-W6`, or its own harvest if the coordinator
splits).

## Deps

`MEM-W0` (the C-route region param to forward).

## Done when

A `slots==0` scope opens no region and forwards the received region param unchanged, allocating scopes
still open (pre-sized in W2), the conservative posture never elides an allocating scope, and the ritual
gate is green under MEM_PARANOID.
