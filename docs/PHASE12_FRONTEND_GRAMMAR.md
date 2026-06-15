# Phase 12 — Frontend Grammar & Lexer Extension

> Branch `feat/phase-12-frontend-grammar` (PR #5, draft). Builds on Phase 11 (Browser
> FFI + the real `.tks → IL → WASM` interop frontend), merged via PR #4.
>
> Goal (from `docs/plan.md` §Phase 12): get every new **token**, **AST node**, and
> **literal form** into the frontend so the later feature phases (13–16: concurrency,
> OOP, optionals/comptime, networking/web/crypto) have a grammar to compile against.
> This is a *frontend-grammar* phase: recognize and represent the surface; lowering and
> semantics of each feature land in their own phases.

## Scope (what this phase delivers)

### 1. Reserved keyword matrix → lexer tokens
The full token table the lexer must recognize, grouped (per the roadmap):
- **Resilience:** `circuit`, `fallback`, `delayed`, `retry`, `exponential`, `logarithmic`, `attempts`, `timeout`
- **OOP & concurrency:** `class`, `abstract`, `trait`, `event`, `raise`, `subscribe`, `fanout`, `fire_and_forget`, `shared`, `atomic`, `routines`, `duplex`
- **Web:** `api`, `middleware`, `get`, `post`, `put`, `delete`, `rpc`, `websocket` (`use` already exists)
- **Tooling:** `parse`, `json`, `csv`, `xml`, `html`, `bundle`, `minify`, `crypto`, `hash`, `encrypt`
- **Core:** `comptime`, `soa` (`defer`, `null` already exist)

Each becomes a `TOKEN_*` and a row in `keywords[]` (`src/lexer.c`). Purely additive —
recognition only; no parser action yet beyond not erroring.

> **Latent bug to fix here:** `keywords[]` currently has its `{NULL, …}` sentinel
> *before* `{"required", TOKEN_REQD}`, so the lookup loop stops early and `required`
> is never matched. The sentinel must be last.

### 2. Native literal suffixes (zero runtime cost, captured in the lexer)
- **Time:** `ms`, `s`, `m`, `h`, `d`
- **Data:** `b`, `kb`, `mb`, `gb`
- **Bandwidth:** `kbps`, `mbps`, `gbps`
`lex_number` captures an optional trailing unit suffix and tags the literal token with
its unit/category, so a `500ms` or `2mb` literal carries its dimension into the AST.

### 3. AST node mapping
Extend the AST with nodes representing the new Web / OOP / crypto / resilience
expressions so the rest of the compiler has a representation to lower in later phases.
(The repo's AST is split across `parser.h` / `parser_statements.h` / `parser_ffi.h`
rather than a single `ast.h`; this phase consolidates the *new* nodes coherently.)

### 4. Foundational frontend gaps carried over from Phase 11
Phase 11's interop frontend was deliberately bounded (literal/handle args only). These
foundational pieces unblock a *real* expression frontend and belong early in Phase 12:
- **Named local variables** (`let`/`mut` bindings) with a frontend symbol table and slot
  assignment, lowered to IL load/store.
- **General expressions / arithmetic** from source (precedence-climbing or Pratt parser
  over the existing operator tokens) → IL.
- **Multiple nested handle args** (lift the Phase 11 "one leading nested call" limit) via
  spilling intermediates to named temporaries.

## Increment plan (status filled in as we go)
1. **P12-A** keyword tokens + sentinel fix — ✅ done (`TOKEN_*` + `keywords[]`; `required` fixed).
2. **P12-B** native literal suffixes (time/data/bandwidth) — ✅ done (`Token.literal_unit`,
   longest-match in `lex_number`, suffix excluded from the lexeme, non-units rewound).
3. **P12-C** AST node scaffolding for the new surfaces — *medium; extend the existing split
   AST (no rewrite). Reported before landing.*
4. **P12-D..** the carried-over frontend gaps (locals → expressions → nested handles) —
   *foundational, larger; sequenced after the bounded parts and reported before landing.*

Discipline (per `CLAUDE.md`): 1 increment per commit; ASan/UBSan on both dispatch paths +
TSan; the 16 native emitter goldens never regress; 4 CI gates green; docs/CLAUDE.md kept
current; no merge/force-push.
