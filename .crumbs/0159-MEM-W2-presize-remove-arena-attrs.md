---
seq: 0159
crumb-id: MEM-W2
milestone: M5
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [MEM-W1]
sources:
  - "DECISION_LOG.md:1162"                                            # D130 refinement 3 (compile-time sizing removes #arena_*)
  - "docs/design/ast-computed-arena-assessment-0.3.1.md:120-175"     # Idea 1 (the floor)
  - "src/checker/residence.tks:257"                                  # region_slots (MEM-E2, landed)
  - "src/codegen/codegen.tks:7904,7919,7928"                         # cg_emit_arena_presize + has_arena_size path (to REMOVE)
  - "src/codegen/codegen.tks:9072,9144"                              # TFunction.has_arena_size/arena_size field sites (to clean)
  - "src/parser/parse_decl.tks"                                      # #arena_size/#arena_depth attribute parse (to REMOVE)
---

# 0159 · MEM-W2 — pre-sizing from `region_slots`; REMOVE `#arena_size`/`#arena_depth` surface

> Consume `region_slots` (`residence.tks:257`, landed): each opened region (`MEM-W1`, when `slots>0`) is
> born at its COMPILE-TIME size, not a fixed default. D130 refinement 3: because the compiler now computes
> the peak simultaneous-live slots + sizes, the MANUAL `#arena_size` (floor) and `#arena_depth`
> (flattening) attributes are REDUNDANT and REMOVED from the language. Byte-mover (presize constants change)
> → fixpoint. First step where the RSS ratchet DOWN is expected.

## Goal

Refinement 3: the AST number IS the arena size, set at initialization. A region opened by `MEM-W1` is sized
via `region_slots(body, table)` (as `usize`) instead of a default. Since the compiler computes the size,
the two manual knobs lose their reason to exist: `#arena_size(N)` (manual floor) and `#arena_depth(N)`
(manual flattening; the default depth-1 + `MEM-W1` elision already handle granularity) are DELETED from the
surface (parse + checker + codegen). A runtime-sized slot still chunk-grows past the floor (never UAF;
over-floor leak-safe) — "sizing" is the exact initial floor, not a cap.

## Where

- `src/codegen/codegen.tks` (region-open, ~`4758`/`6230`/`7613`) + `src/lir/lower.tks` (native
  region-open) — pass `region_slots(body, table)` (as `usize`) to the sized region-new instead of the
  default.
- `src/codegen/codegen.tks:7904` (`cg_emit_arena_presize`) + `:7919/:7928` (`has_arena_size` branches) —
  DELETE the `#arena_size` presize path.
- `src/codegen/codegen.tks:9072,9144` — the `TFunction` constructions that carry `has_arena_size`/
  `arena_size` — remove those fields from `TFunction` and every construction site (checker `type.tks`
  included).
- `src/parser/parse_decl.tks` — REMOVE the `#arena_size`/`#arena_depth` attribute parse.
- `src/checker/*` — REMOVE any `#arena_size`/`#arena_depth` checking / AST fields.
- Fold `0108 D1-T1` (`arena_floor`) — SUBSUMED by `region_slots` (the peak, not just the floor).

## How

1. Route the sized region-new through `region_slots` (the computed peak) on both engines.
2. Delete `#arena_size`/`#arena_depth`: parse, AST field, checker, `TFunction` fields, codegen presize. A
   program using them now fails to parse (the new rejection — `EXPECT_COMPILE_FAIL` oracle).
3. Confirm the chunk-list still grows a runtime-sized alloc beyond the floor (unchanged).

## Rulings & laws

- **Teko-only:** parser/checker/codegen/lir `.tks`.
- **D130 refinement 3:** compile-time sizing is the SOLE sizing; `#arena_size`/`#arena_depth` REMOVED
  (surface removal, like `-> ref T`).
- **Floor is the initial chunk, not a cap:** runtime-sized allocs chunk-grow; sub-floor never UAF;
  over-floor leak-safe.
- **W15 full Javadoc; no `//`.** **Safety:** never `teko test .`; subshell `ulimit -v 4718592`;
  `[RITUAL]` gen2==gen3 + MEM_PARANOID + **RSS ratchet DOWN (D68 — this is a REDUCTION; measure `teko:
  memory: peak <N> MB` of the dry build, must be strictly below the `MEM-W0` seed)**.

## Fixtures

- Shadow (scratchpad): the `mem_*` peak/tail-waste drop measured via `TEKO_ARENA_OBS`.
- Rejection oracle (allowed): `arena_size_attr_removed` — `#arena_size(N)` / `#arena_depth(N)` no longer
  parse → `EXPECT_COMPILE_FAIL`.

## Gate

`[RITUAL]` — gen2==gen3 (presize constants deterministic) + MEM_PARANOID 0 + **RSS peak strictly DOWN vs
the `MEM-W0` seed** (measurement point: the canonical `teko: memory: peak <N> MB` of the dry build).
"Green" = regions are born at `region_slots`, `#arena_size`/`#arena_depth` are gone (reject), tail-waste
drops, `gen2==gen3`. Reseed-class: `fixpoint-rebuild` (harvested at RESEED-FINAL `MEM-W6`).

## Deps

`MEM-W1` (the region-open decision it sizes).

## Done when

Each opened region is born at its `region_slots` compile-time size, `#arena_size`/`#arena_depth` (and their
`TFunction` fields) are removed from the language (reject), runtime-sized allocs still chunk-grow,
peak/tail-waste drop (strict ratchet DOWN), and the ritual gate is green under MEM_PARANOID.
