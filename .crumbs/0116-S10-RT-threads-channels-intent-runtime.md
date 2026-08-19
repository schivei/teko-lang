---
seq: 0116
crumb-id: S10-RT
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [S10-SURF, S16-SYNC]
sources:
  - "docs/design/plano-s10-concorrencia-crumbs.md:44-88"           # runtime spine C0a-C5/S3/A4/CN1/J1 + §16 boundary
  - "docs/design/plano-s10-await-opcao-c-crumbs.md"               # OPTION (c): stackful coroutines model (ratified 4d6170dd)
  - "docs/design/mudancas-superficie-0.3.1.md"                    # surface changes overview
  - "docs/design/plano-s10-multithread-nativo.md"                 # native thread runtime detail
  - "docs/design/arena-especificacao-unica-0.3.1.md:298-333"      # Doc-1 §7.2/§7.3 region-per-thread (per-lane)
  - "src/threads/threads.tks:1-68"                                # the type surface to fill
  - "src/runtime/teko_rt.tks"                                     # tk_task_begin/end seam; tk_thread_spawn sibling
---

# 0116 · S10-RT — §10 runtime: threads + channel transports + Intent + region-per-thread

> Deliver the RUNTIME half of §10: the `tk_thread_spawn` primitive, the `MemChan`/`OsChan` transports,
> the `teko::threads` stdlib (`IChannelKind`/`Rx`/`Tx`/`Ctx`/WaitGroup bodies), the spawn codegen
> trampoline, and `await` lowering — over the `S16-SYNC` sync FFI and the region-per-thread arena.

## Goal

`S10-SURF` (`0115`) taught the compiler to ACCEPT §10; this crumb makes it RUN. It fills the skeleton
`teko::threads` (today all-stub, `src/threads/threads.tks`) and adds the maintained-C runtime siblings
`tk_thread_spawn` + `tk_memchan_*` + `tk_oschan_*` (s10 C0a/C0b/C0c) — the arena discipline forces the
`tk_task_begin()`/`tk_task_end()` bracket into a C trampoline (s10 D1, a CONSTRAINT-forced maintained-C
seam, later migrated to Teko/FFI by `§16`/`RT-L5`). It lands the spawn codegen (s10 S3, the ctx-blob +
`cabi` trampoline), the channel stdlib leaves (C1-C5), the `Intent<T>` runtime (A1), and `await`
lowering (A4). The arena runs **region-per-thread / per-lane** (Doc-1 §7.2-7.3) with the F2 immortal
program region for names that cross a task (Doc-1 §7.6-7.7). §10 stays a **LEAF** (s10:84-88): the
compiler never instantiates it, so the compiler-facing reseeds (S3/A4) are mechanical byte-identity;
the stdlib leaves are `[dry]`.

## Where

- `src/runtime/teko_rt.{c,h}` (maintained-C exception) — add `tk_thread_spawn` (detached + joinable
  twin) bracketing `tk_task_begin`/`tk_task_end` (s10 C0a); `tk_memchan_*` (F2 FIFO + futex, s10 C0b);
  `tk_oschan_*` (AF_UNIX SOCK_DGRAM, from `examples/probes/chan_dgram`, s10 C0c). These are the
  `teko_rt` maintained-C exception until `§16`/`RT-L5` migrate them to Teko-over-FFI.
- `src/codegen/codegen.tks` — spawn trampoline (s10 S3): pack the copied args into a ctx-blob, emit the
  `cabi` trampoline entry, call `tk_thread_spawn`. The hardest §10 crumb.
- `src/threads/threads.tks:1-68` — fill `MemChan<T>` (C2) + `OsChan<T>` (C3) implementing
  `IChannelKind<T>`; the `Rx<T>::pop`/`Tx<T>::send`/`close` bodies; `chan<T>::make<K>` (C4);
  `Ctx`/WaitGroup `add`/`wait`/`done` (C5, over `tk_futex`/`S16-SYNC`).
- `src/threads/intent.tks` (new leaf) — `Intent<T>`/`Intent` protected structs (A1) + `await` lowering
  (A4) per the D2 ruling.
- `src/lir/lower.tks` — `await` lowering arm (A4); `cancel()` (CN1). BLOCKED on D2 (see rulings).

## How

Follow the s10 spine bottom-up (runtime → codegen → stdlib → await):

1. **`tk_thread_spawn` (C0a) — FIRST.** Maintained-C, zero deps, unblocks the spine. Detached + a
   joinable twin (for the thread-per-await A4). Its body brackets the new thread with
   `tk_task_begin()`/`tk_task_end()` (Doc-1 §7.2 region-per-lane).
2. **Transports (C0b/C0c).** `tk_memchan_*` (pure F2 FIFO + futex, no syscall) and `tk_oschan_*`
   (AF_UNIX DGRAM). Both on the CURRENT runtime — NOT `§16` (s10:67-71); only user-pluggable transports
   (Kafka/WS via dynamic-FFI) are `§16`/`§17`-gated and out of this crumb.
3. **Spawn codegen (S3).** Ctx-blob of the copied args + `cabi` trampoline + `tk_thread_spawn` call.
   Compiler-touching → mechanical reseed (the compiler never spawns, s10:84-88).
4. **Channel stdlib (C1-C5).** Fill the `teko::threads` bodies; `chan<T>::make<K>` binds the transport
   `K` to the channel's constant key; `svc<Rx/Tx>` resolves by key (DI-by-key). WaitGroup over
   `S16-SYNC` futex/ulock/WaitOnAddress.

```teko
/**
 * region_per_thread — bind a freshly spawned lane to its OWN arena region (Doc-1 §7.2), rooted so that
 * a value crossing the task boundary is copied into the F2 immortal program region by NAME, never
 * shared by pointer (Doc-1 §7.7). Returned to the pool at `tk_task_end`.
 *
 * @param lane  the spawned lane's identity
 * @return      the lane's root region handle
 * @since 0.3.1
 */
exp fn region_per_thread(lane: LaneId): RegionHandle
```

5. **`Intent<T>` (A1) + `await` surface.** `Intent<T>` protected struct with `_value: T | null`
   (verify `Intent__g__i32` stamps, s10:77-78). **The `await` surface (parser/checker/Intent<T> type) is
   model-independent** and lands in this crumb. The lowering (A4) is OPTION (c)-specific (stackful
   coroutines via ucontext/Fibers) and deferred post-§16 (ratified `4d6170dd`). `cancel()` (CN1):
   panic-outside unblocked in this crumb; under-await specifics deferred with A4.
6. **`teko::journal` (J1).** Already substantially present (`src/journal/`, ~876 lines) — VERIFY it
   mirrors the chan DI-by-key/F2 model and wire it to the finished channel layer; do NOT rewrite.

## Rulings & laws

- **Teko-only (2026-07-04):** new work `.tks`; the `tk_thread_spawn`/`tk_memchan_*`/`tk_oschan_*`
  primitives are the **maintained-C `teko_rt` exception** (s10 D1 constraint-forced), migrated to
  Teko-over-FFI by `§16`/`RT-L5` — flagged, not a Teko-only breach.
- **Comment convention (W15, owner 2026-08-19):** `/** */` only on `exp` decls; no `//` or `/* */`.
- **Await model (owner 2026-08-16, ratified commit 4d6170dd):** **OPTION (c): stackful coroutines over
  ucontext (POSIX) / Fibers (Windows).** The `await` surface (`Intent<T>`, parser/checker) is
  model-independent and completes in this crumb. The suspension-model-specific lowering (A4) and
  `cancel` under-await (CN1) are deferred post-§16, when the OS-FFI infra (`ucontext_wrap`, Fibers
  bindings) lands.
- **Concurrent collections PHASE-2 gated in M2 via §10 F1+F2:** `S10-RT` gates the deferred PHASE-2
  concurrent collections (ConcurrentDictionary, BlockingCollection, ConcurrentQueue/Stack, etc.) to
  align with surface teaching and runtime wire-up; they ship in M2 alongside §10.
- **Leaf discipline (owner, s10:84-88):** the compiler must NEVER instantiate `spawn`/`chan` → S3 reseed
  is byte-identical/mechanical; the stdlib leaves are `[dry]`. `await` lowering (A4) is deferred.
- **W15 full Javadoc** on every new `exp` decl; flatten; no `//`.
- **Safety:** NEVER `teko test .`; build under `ulimit -v 6815744`; commit each spine step; S3 is
  `[fixpoint]` `gen2==gen3`; A4 specifics deferred; sweep `.tkt` after any AST/sig change.
- Rests on: s10 spine (44-88) + Doc-1 §7.2-7.9 + `S16-SYNC` (`0056`) +
  `plano-s10-await-opcao-c-crumbs.md`.

## Fixtures

Isolated `.tkr` for the concurrency boundaries the self-build never exercises (§10 is a leaf):

| fixture | asserts | expected |
|---|---|---|
| `thread_spawn_paranoid` | 100 threads × task_begin/alloc/task_end under `TEKO_MEM_PARANOID=1`; `tk_names_live_count` returns to baseline (s10:64-65) | `0` |
| `memchan_roundtrip` | `MemChan<i32>` send/recv fan-in across 4 producers → 1 reader, all values arrive | `0` |
| `oschan_dgram_roundtrip` | `OsChan<i32>` over AF_UNIX DGRAM roundtrips a message | `0` |
| `await_intent_surface` | `Intent<T>` surface works (type stamps, struct packing); `await` syntax/checker accept the surface; model-specific lowering deferred | `0` |

## Gate

`[fixpoint]` — the spawn codegen step (S3) rebuilds `gen2==gen3` byte-identical (leaf-inert: the
compiler never spawns). The stdlib leaves are `[dry]`. "Green" = the runtime primitives pass the
paranoid fixture, the transports roundtrip, `Intent<T>` surface works, the `await` parser/checker accept
the model-independent surface, and the compiler rebuild is byte-identical. Reseed-class:
`fixpoint-rebuild` (mechanical). **Lowering specifics (A4) deferred post-§16.**

## Deps

`S10-SURF` (`0115`, the accepted surface) + `S16-SYNC` (`0056`, the sync FFI). The maintained-C
primitives are later migrated by `RT-L5` (`0063`)/`§16`.

## Done when

`spawn` creates an OS-thread lane with its own arena region, `MemChan`/`OsChan` transports roundtrip,
`chan<T>`/`svc<Rx/Tx>` resolve, WaitGroup joins, `Intent<T>` surface works and `await` syntax/checker
accept it (model-independent), `teko::journal` is wired, concurrent collections PHASE-2 gate is set,
and the compiler rebuild is byte-identical (§10 kept a leaf). Lowering (A4) and under-await `cancel`
specifics ship post-§16 with the OS-FFI infra.
