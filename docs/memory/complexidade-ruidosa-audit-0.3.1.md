# Noisy-complexity audit (O(n²)+ hotspots) — backlog for the MEMORY-cleanup lane

Version: 0.3.1 · Status: assessment only (no product-code change) · Scope: `src/**.tks`

## Intro — method and what "noisy complexity" means here

"Noisy complexity" is per-item work that is asymptotically worse than it needs to be:
an O(n²) (or worse) shape where O(1)/O(log n)/O(n) is achievable with a different data
structure or a memo. It is *noise* in two senses the owner cares about: it inflates
**transient memory** (intermediate lists/allocations that scale with n²) and it burns
**time**. Because the self-host compiles ~6600 items, any per-item quadratic that scales
with program size compounds: a scan of "all globals" or "all types" done *once per
reference* is really O(references × declarations), and both factors grow with the corpus.

Concrete sizes measured in this tree (grep of `src/`): **~5343 `fn`s**, **856 `type`
decls**, **568 top-level `const`s**, 145 externs. So the two program-wide symbol tables a
per-reference scan walks are ≈ **6000 global value bindings** and ≈ **856+ type decls**
(before generic stamps). Tens of thousands of references cross those tables during a
self-build.

Method: read-only sweep of checker / lir / codegen / emit / backend / parser / runtime
`.tks` for (a) linear `find`/`contains`/`seen?` **inside** a loop, (b) list-shaped tables
that should be hash indexes, (c) re-derivation that should be memoised, (d) push-unique via
full rescan, (e) quadratic string/slice growth. Each candidate is classed **HOT** (on the
per-compilation-item path, ×~6600) vs **COLD** (one-off), ranked by *(hot? × degree × data
size)*.

### One structural fact that cleared a whole class of false positives

`teko::list::push` lowers to `tk_slice_push`, documented in `lower.tks` (§ `slice_push_symbol`,
~L10924) as **"the plain amortized runtime grow."** So the ubiquitous
`mut out = …; loop { out = teko::list::push(out, x) }` accumulator is **amortized O(n)**,
not O(n²). The thousands of push-loops in this codebase are therefore **not** on this list.
See the "considered but acceptable" appendix — please do not re-flag them.

---

## Ranked hotspot table (most impactful first)

| # | file:line · fn | what | current | achievable | HOT? | data (n) |
|---|----------------|------|---------|-----------|------|----------|
| 1 | `checker/scope.tks:320 lookup_call` · `:349 call_ns` · `:204 lookup_binding` · `:238 lookup_value_in_ns` | resolve a name/callee against sealed globals | O(globals) per ref, **×2 per call** | O(1) avg (bucketed hash) | **HOT** | ~6000 globals |
| 2 | `checker/resolve.tks:97 type_table_find` · `:139 type_table_find_path` | find a type decl by name | O(types), **two passes** per type ref | O(1) avg | **HOT** | ~856 types |
| 3 | `checker/collect.tks:558/723/737/708 find_class_body / find_field_owner / find_method_owner / is_subclass_of` | class member reach + subclass test | O(types) **per base-chain hop** | O(1)/hop | **HOT** | ~856 types × depth |
| 4 | `checker/monomorph.tks:315 find_generic_fn` · `:865 find_template_method` | locate a generic template by name | O(items) per stamped instance | O(1) avg | **HOT (mono)** | ~6000 items × insts |
| 5 | `checker/monomorph.tks:183 mono_seen` (`:1219`) | "already stamped?" dedup | O(stamped) per queue item → O(insts²) | O(1) avg | **HOT (mono)** | insts (≤5000) |
| 6 | `checker/consteval_order.tks:106 find_const_by_key` + `const_key_in` (done/onpath/keys) | topo-order consts | O(consts²) DFS | O(consts) | COLD (1×) | 568 consts |
| 7 | `backend/objfile_elf.tks:396 elf_symbol_index` (`:641`) · `objfile_macho.tks:182 sym_index` (`:837`) · COFF sibling | reloc symbol → table index | O(syms) per reloc → O(relocs×syms) | O(1) avg | COLD (1× emit) | syms≈program |
| 8 | `lir/lower.tks:174 lenv_newest_index` · `:255 lenv_lookup` · `:273 lenv_lookup_fat` · `:135/:156 slot variants` | local var → VReg during lowering | O(locals) per var ref → O(fn locals²) | O(1) avg | **HOT** (bounded/fn) | locals per fn |
| 9 | `collections/map.tks:143 map_find_index` | the "hash map"'s core lookup | **O(n) linear scan** (hash only skips byte-cmp) | O(1) avg (true buckets) | latent | map size |
| 10 | `backend/regalloc.tks:631 block_first_point` · `regalloc_x86.tks` sibling | block's first program point | O(stream) per block → O(insts×blocks)/fn | O(1) (precomputed) | **HOT** (bounded/fn) | insts per fn |
| 11 | `checker/resolve.tks:1512 union_collect` (`:1528`) | dedup union members by `type_eq` scan | O(members²) | O(members) | HOT | members (usu. small) |
| 12 | `checker/borrow.tks:853 local_def_find` · `:257 borrow_summary_env_find` | local/summary lookup in borrow pass | O(locals)/O(summaries) per ref | O(1) avg | HOT (bounded/fn) | locals / fns |

Detail for each follows; #3 is an amplifier of #2 and #11 is the owner's seed template.

---

## 1 — Global value/callee resolution scans `env.base` linearly · **highest impact**

`checker/scope.tks`: `lookup_binding` (L204), `lookup_value_in_ns` (L238), `lookup_call`
(L320), `call_ns` (L349). Each walks `env.bindings` (local scope — fine, small) and then
**`env.base`** newest-first, comparing `.name` (and for calls the namespace rule
`call_binding_matches`). `env.base` is the **sealed globals**: every top-level fn/method/const
signature. The file's own `#148` note calls it *"hundreds of KB"* and records the memory
fix (sealing, so forks don't copy it) — but the **lookups were left O(base)**.

- **Current:** O(globals) ≈ O(6000) per name reference. A *call* pays it **twice**
  (`lookup_call` for the type, then `call_ns` for the mangling namespace) — two full
  6000-entry scans per call site. Total ≈ O(references × globals): quadratic in program size.
- **Achievable:** O(1) average. Replace the flat `base: []ValBinding` with a **bucketed
  hash index** keyed on the **bare name** → a small `[]ValBinding` bucket (names collide
  across namespaces, and a name can be shadowed), preserving *newest-first* order inside a
  bucket. `lookup_*` hashes the name, walks only that bucket applying the identical
  namespace/mutability predicate.
- **HOT / degree:** maximally hot and high-degree — the single biggest compounding
  quadratic in the compiler. Attack first.
- **Impact:** plausibly the largest single time sink in the checker; also transient — every
  fork still shares `base` (good) but each lookup's linear walk touches the whole 6000-entry
  array's cache lines. A hash index cuts both.
- **Correctness caveats:** (a) **locals must still shadow globals** — keep the two-segment
  order (scan `bindings` first, then the base index). (b) Newest-first **within** a name is
  load-bearing (later global wins on legitimate re-registration); bucket order must preserve
  it. (c) `call_binding_matches` / `qualifier_selects_ns` namespace semantics must be applied
  unchanged after the bucket narrows by name. (d) `lookup_value_in_ns` and `call_ns` must
  return the *same* winning binding the linear scan would — index is an accelerator, not a
  semantics change.

## 2 — `type_table_find` two-pass linear scan per type reference

`checker/resolve.tks:97` `type_table_find` scans `table` (all `TypeReg`) matching
`name == entry.name || qualify_eq(...)`, then a **second** full pass on `name_last_segment`.
`type_table_find_path` (L139) adds another linear pass. Called on essentially every resolved
type reference (`resolve_named`, `find_class_body`, backend re-resolution, …).

- **Current:** O(types) ≈ O(856), doubled by the two passes, per type reference.
- **Achievable:** O(1) avg via **two hash indexes** built once when the table is folded:
  a primary map keyed by the **exact/canonical name** and a secondary multimap keyed by
  **last segment** (for the bare-probe fallback). `type_table_find_path`'s qualifier arm
  narrows the last-segment bucket then applies `qualifier_selects_ns`.
- **HOT / degree:** very hot; also the root that #3 amplifies.
- **Correctness caveats:** the tolerant matching is the whole point of this function — it
  must keep resolving **both** table shapes (`"ns::Name"`+`""` from `type_table_of`, and
  bare-name+real-namespace from the typing collector). The index must therefore key entries
  by *both* their raw `name` and their `qualify_eq` canonical form, and the last-segment
  multimap must be **order-independent** (post-W0 per-namespace uniqueness guarantees no
  same-canonical collision, but same-*last-segment* cousins across namespaces coexist — the
  bucket must hold all and let the caller's namespace rule pick, exactly as pass 2 does now).

## 3 — Class hierarchy walks re-scan the type table at every hop (amplifier of #2)

`checker/collect.tks`: `find_class_body` (L558) → `type_table_find`; `find_field_owner`
(L723), `find_method_owner` (L737), `is_subclass_of` (L708) recurse up the base chain,
each hop calling `find_class_body` → a fresh O(types) scan.

- **Current:** O(types × chain-depth) per field/method visibility check and per subclass
  test — and these run per member access during typing.
- **Achievable:** fixing #2 makes each hop O(1); no separate structure needed. Optionally
  memoise `(class, member) → MemberOwner` since the chain is walked repeatedly for the same
  class during a method body.
- **HOT:** yes (member-access checks are per-expression on class code).
- **Correctness caveats:** the memo (if added) must key on the **canonical** class name and
  be invalidated per program (it is derived from the immutable post-collect table, so a
  per-compilation memo is safe). Preserve the override "declaration moves to the overrider"
  semantics — the walk order (own body first, then base) is load-bearing.

## 4 — `find_generic_fn` / `find_template_method` scan all items per instantiation

`checker/monomorph.tks:315` `find_generic_fn` scans **all** `prog.items` (~6000) to find one
generic template by name, called once per stamped instance in the fixpoint loop (L1224).
`find_template_method` (L865) is the same shape for methods.

- **Current:** O(items × instances).
- **Achievable:** build **once**, before the fixpoint loop, a `name → TFunction` map of the
  generic templates (only items with `type_params.len > 0`), then O(1) per lookup.
- **HOT:** on the mono path; degree scales with generic use.
- **Correctness caveats:** key must match what the queue carries (`inst.fn_name`, bare).
  Multiple generic fns can share a bare name across namespaces — the map value is a small
  bucket, disambiguated the same way the scan does today (first `type_params>0` match; if the
  scan is namespace-blind today, keep it namespace-blind to stay byte-identical).

## 5 — `mono_seen` dedup is O(instances²)

`checker/monomorph.tks:183` `mono_seen` linear-scans the `stamped` ledger by mangled name;
called per queue item (L1219). Queue may carry duplicates, so total is O(instances²), capped
by the 5000 ceiling (→ up to 25M string compares worst case).

- **Achievable:** a **set** keyed on `mangled` (str hash). O(1) membership; the `stamped`
  list stays for ordering, the set is the gate.
- **Correctness caveats:** the ceiling check and the "skip if seen" must stay exactly
  sequenced (seen-check before ceiling before stamp) so the honest-stop on unbounded
  polymorphic recursion is preserved.

## 6 — Const dependency ordering is O(consts²)

`checker/consteval_order.tks`: `const_dep_order` (L286) runs a DFS; each `const_dep_visit`
(L256) does `const_key_in(st.done, …)` and `const_key_in(st.onpath, …)` (linear over the
done/path sets) plus `find_const_by_key` (L106, linear over all 568 consts), and
`const_dep_refs_var` (L175) tests `const_key_in(keys, …)` against the full key set per
reference.

- **Current:** O(consts²) in time; the `done`/`onpath` lists also grow to O(consts).
- **Achievable:** a `ConstKey → TConstDecl` **map** for `find_const_by_key`; a **set** for
  `done` and for `keys`; keep `onpath` as an ordered list but back it with a companion set
  for the membership test. → O(consts + edges).
- **COLD:** one pass per compile, but genuinely quadratic at 568 nodes.
- **Correctness caveats:** the key is `(name, namespace)` and must stay distinct for a
  project const vs a same-name dep const (the whole reason the DFS was re-keyed). Cycle
  reporting reads `onpath` **in order** — the ordered list must remain the source of the
  printed path; the set is only the accelerator.

## 7 — Object-file reloc → symbol index is O(relocs × syms)

`backend/objfile_elf.tks:396` `elf_symbol_index` linear-scans the symbol table by name; called
per reloc inside `elf_build_relas` (L641). Mach-O has the identical `sym_index` (L182, called
at L837); COFF has its own. At self-host scale both `syms` and `relocs` grow with the program
(thousands each).

- **Achievable:** build a `name → index` map **once** from the finalized (post-partition)
  symbol table, before the reloc loop; O(1) per reloc.
- **COLD:** one-off per object-file emission — but that one pass can be O(6000 × tens-of-
  thousands) = plausibly hundreds of millions of `str` compares, i.e. real wall-clock seconds.
- **Correctness caveats:** the index must be taken against the **post-partition** table
  (locals sorted before globals) — the map must be built *after* partitioning, exactly where
  `elf_symbol_index` is called today, or `r_info` symbol indices shift and every reloc
  mis-targets. Byte-identical object output must hold.

## 8 — LIR local-env lookups are O(locals) per variable reference

`lir/lower.tks`: `lenv_newest_index` (L174), `lenv_lookup` (L255), `lenv_lookup_fat` (L273),
`lenv_lookup_fat_slot` (L135), `lenv_lookup_scalar_slot` (L156) all scan `env.names`
newest-first. Every lowered variable read/reassign pays O(locals-in-scope).

- **Current:** O(fn locals²) per function. Bounded per function, but on the hottest lowering
  path and paid for the largest functions (this file has functions with very many locals).
- **Achievable:** maintain a `name → newest-index` map alongside the parallel arrays; a
  bind pushes and updates the map, a lookup is O(1). Shadowing = the map holds the newest
  index; `lenv_reassign`'s in-place update keeps the index stable (already relied on).
- **HOT** but per-function-bounded → rank below the program-wide scans.
- **Correctness caveats:** **newest-first / shadow-wins** is the core invariant — the map
  must always point at the *newest* binding of a name, and the fat-vs-scalar and
  slot-vs-value distinctions (`has_len`, `is_slot`, `is_scalar_slot`) must be read from the
  resolved index unchanged. `lenv_reassign`'s fat-fallback-to-append behaviour must keep the
  map consistent (append updates newest index; the stale duplicate stays out of the map).

## 9 — `collections::Map` is not actually bucketed (latent, and a trap for the fixer)

`collections/map.tks:143` `map_find_index` — despite the type's doc claiming *"hash-bucketed
so lookup is amortised O(1)"* — is a **linear scan over every key**, using the cached `u64`
hash only to skip the byte-compare on a mismatch. So `get`/`insert`/`contains` are **O(n)**,
not O(1). Worse, `insert`/`remove` call `arr_replace_at`/`arr_drop_at` which rebuild a whole
parallel array O(n) per mutation.

- **Where it bites today:** only `encoding/json`, `env`, `coverage` use it — **not** on the
  hot compiler path, so it is *latent*, not a live self-host cost. It is listed because it is
  exactly the structure a well-meaning fix for #1/#2/#4/#6 would reach for — and it would not
  help. Any of those fixes must build a **truly bucketed** index (array of buckets indexed by
  `hash % nbuckets`, grown geometrically), or this Map must be upgraded to real bucketing
  first and then reused.
- **Correctness caveats:** a true-bucket rewrite must preserve `keys()` **insertion order**
  (callers iterate it), the hash-before-key compare (collision correctness), and reference
  semantics (aliases observe mutations).

## 10 — `block_first_point` re-scans the instruction stream per block

`backend/regalloc.tks:631` (and the `regalloc_x86.tks` sibling) scans the whole numbered
stream to find a block's smallest program point; loop analysis calls it per block/edge →
O(instructions × blocks) per function.

- **Achievable:** one pass to build `block_id → first_point` (min over the stream), then O(1)
  reads. The stream is already numbered in RPO, so the first occurrence per block is the min.
- **HOT** but per-function-bounded.
- **Correctness caveats:** must still return the **smallest** point for a block whose
  instructions are non-contiguous; the precompute must take the min, not the first seen (RPO
  numbering usually makes them equal, but the current code takes the min — preserve that).

## 11 — `union_collect` dedup by linear `type_eq` (the owner's seed template)

`checker/resolve.tks:1512` `union_collect` (dedup at L1528): for each non-inline member it
linear-scans the accumulator calling `type_eq`. O(members²) in the union arity.

- **Achievable:** a **set keyed on the member's mangled type name** (the same equivalence
  `type_eq` computes), membership-checked before append → O(members).
- **HOT** on the type-join path, but n (union arity) is usually small (2–4); the pathological
  case is a wide inferred union. Included because the owner named it as the *template* and the
  fix is cheap and self-contained.
- **Correctness caveats — the critical one:** the hash key must be **exactly** `type_eq`'s
  equivalence, including the post-O1 `Null` normalization and the permissive sentinel
  behaviour (`[]Void == []i64`) that `type_eq` implements (see the `type_join` doc-comment).
  If the mangle key is *finer* than `type_eq`, dedup silently stops merging members that
  `type_eq` calls equal, changing the resulting variant. A key derived from
  `mono_type_mangle`/`type_render` must be audited against every `type_eq` arm, or the set
  must key on a canonical form produced by the same normalizer `union_normalize_null` uses.

## 12 — Borrow-pass local/summary lookups

`checker/borrow.tks:853` `local_def_find` (linear over `ctx.locals`) and `:257`
`borrow_summary_env_find` (linear over the summary env). Per variable reference in the borrow
pass. Bounded per function / per program respectively; a hash index removes the scan.
Lower priority than #1/#8 but the same shape.

---

## Considered but acceptable (do not re-flag)

- **`teko::list::push` accumulator loops** — amortized O(1) (`tk_slice_push`, geometric grow).
  The thousands of `out = push(out, x)` loops are O(n) total, **not** quadratic. This is the
  single most important "acceptable" note: do not open items for push-loops.
- **`subst_find` (resolve.tks:1591), `subst_find_inst` (monomorph.tks:302)** — linear over a
  `Subst`, whose length is the enclosing fn's **type-param count** (1–3). Bounded tiny.
- **Field/member lookups: `field_index_of` (lower_const.tks:988), `field_init_by_name`
  (comptime_fold.tks:1171), `find_variant_def` (lower_const.tks:776), `variant_member_by_name`
  (match.tks:77), `find_member_const` (typer.tks:3042)** — n is a single type's field/member/
  variant count, small and bounded. Fine.
- **Interface/trait method collection: `iface_methods_by_name`, `effective_interface_methods`
  (collect.tks, `seen: []str` scan), `find_interface_method` (typer.tks:5685)** — n is one
  interface's method set / the extends set; small. Acceptable.
- **`name_last_segment` / `name_qualifier` / `ns_last_seg`** — O(name length) per call. The
  `#148` note records ~144M calls on a self-build, but that is a **constant-factor /
  allocation** concern (already mitigated by the builtin slice), not a complexity-class one.
  Out of scope for this lane except as a micro-alloc note.
- **String mangling concat loops (`mono_mangle_name`)** — concat count bounded by type-params.
- **Linear-scan register allocation core (regalloc active/interval walks)** — inherent to the
  algorithm and per-function bounded; `block_first_point` (#10) is the extractable win, the
  rest is expected.
- **`lenv`/`local_def` per-function scans** are listed (#8/#12) but are per-function bounded —
  attack them only after the program-wide scans.

---

## Suggested order of attack for the memory lane

Ranked by *(hot-path? × degree × data size)* and by how many downstream sites one fix clears:

1. **#1 — global symbol index** (`scope.tks` `env.base`). Biggest single compounding
   quadratic; touches every identifier and every call twice. One data-structure change,
   large payoff.
2. **#2 — type-table index** (`resolve.tks` `type_table_find`/`_path`). Second program-wide
   scan; and it **auto-fixes #3** (all class-hierarchy walks) for free.
3. **#4 + #5 — monomorph indexes** (`find_generic_fn` map + `mono_seen` set). Same file,
   same pass; do together.
4. **#7 — object-file symbol index** (ELF, then Mach-O, then COFF). One-off but a real
   several-second wall-clock cost at emit; low correctness risk (build map post-partition).
5. **#6 — const topo-order** (`consteval_order`). Self-contained O(568²) → O(n).
6. **#9 — make `collections::Map` truly bucketed** — do this *before or alongside* any fix
   above that wants a ready-made hash map, so the index actually delivers O(1).
7. **#11 — `union_collect` set** (the seed). Cheap, self-contained; land it early as the
   worked template — but only after pinning the `type_eq`-exact key (its correctness caveat
   is the subtle one).
8. **#8, #10, #12 — per-function scans** (`lenv`, `block_first_point`, borrow lookups). Real
   but per-function bounded; clean up last.

Every item above is *assess-and-recommend only*; each fix is a separate memory-lane crumb
with its own regression gate (byte-identical emission for #7/#10/#11; identical
resolution/order for #1/#2/#4/#6). The correctness caveats per item are the acceptance
criteria those gates must encode.

**Total flagged: 12 ranked hotspots + 8 considered-but-acceptable classes.**
