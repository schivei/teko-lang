# Phase 16 — Casting / Type Conversions & Parsing

Branch `feat/phase-16-casting` (PR #9, draft). Builds the connective tissue between Phase 15's
type model and every surface that serializes or displays a value: **universal, culture-invariant
conversions between types and to/from strings**, plus the **auto-`to_string` on
concatenation / interpolation** that the Phase-15 OOP model left a hook for (`to_string` resolved
by name through the class registry / vtable, dispatched via `OP_CALL_FUNC` — zero runtime
reflection).

Follows the proven Phase-13/14/15 pattern: a **single C runtime is the source of truth**
(`src/runtime/teko_convert.c`, KAT-tested in the Unity suite), lowered to native `teko_rt_*`
wrappers AND compiled into the WASM reactor — every reserved surface gets an executable `.tks`
proof on **both** native and WASM (no dead tokens).

## Owner-defined scope
1. **Conversions between types** — primitive ↔ primitive (int/float/bool/char), and to/from
   complex & user-defined types. Explicit and **checked** — fail loudly, no silent
   truncation/UB.
2. **Parsing to/from strings, WITH or WITHOUT formatting.**
   - **No format → ONE universal, culture-invariant default** that IGNORES the OS locale:
     `.`-decimal, no digit grouping, canonical integer/float/bool grammar — identical bytes on
     every machine/region. Deliberately distinct from Phase 14's `time.format_local` (which is
     OS-locale/DST-aware). The C runtime never calls `setlocale`, so the C library's default
     `"C"` locale formatting **is** the culture-invariant representation.
   - **Explicit format → the developer supplies a spec** (radix, precision, grouped digits,
     custom masks). Only then does output deviate from the universal default.
3. **Universal `to_string` on EVERY type**, auto-invoked when a value is concatenated with /
   interpolated into a string (`"x = " + p`, `"{p}"`). For user-defined types this dispatches to
   the Phase-15 `to_string` method (resolved by name → `OP_CALL_FUNC` for a concrete static
   receiver, vtable slot for an abstract/trait-typed one); when a type defines none, the compiler
   **synthesizes** the culture-invariant default (Go-style per-type generator over the fields,
   compile-time — never a reflective runtime walk). **This auto-call is the core deliverable.**

## Runtime id allocation (OP_CALL_RUNTIME)
Used ids today: 0–12, 15–34, 37–48. Phase 16 conversion block starts at **49** (contiguous):

| id | symbol                       | signature                         |
|----|------------------------------|-----------------------------------|
| 49 | `teko_rt_int_to_string`      | (i64 as str) → decimal str        |
| 50 | `teko_rt_float_to_string`    | (f64 as str) → canonical decimal  |
| 51 | `teko_rt_bool_to_string`     | (0/1 as str) → `"true"`/`"false"` |
| 52 | `teko_rt_str_concat`         | (a, b) → `ab`                     |
| 53 | `teko_rt_parse_int`          | (str) → i64 (checked)             |
| 54 | `teko_rt_parse_float`        | (str) → f64 (checked)             |
| 55 | `teko_rt_parse_bool`         | (str) → 0/1 (checked)             |
| 56 | `teko_rt_int_to_string_radix`| (i64, radix) → str (explicit fmt) |
| 57 | `teko_rt_float_to_string_fmt`| (f64, spec) → str (explicit fmt)  |

(ids extended as sub-blocks land; the table here is the contract.) All string args/results cross
as NUL-terminated pointers through the shared linear memory on WASM, exactly like the crypto/time
surface; integer/float values are marshalled as their canonical string form into the same ABI
(keeps the uniform pointer-passing convention — the runtime parses/formats).

## Sub-blocks & dependency order
- **16.A — Conversion runtime foundation + primitive `to_string`/parse (explicit-call).**
  `teko_convert.c` source of truth (Unity KATs: int/float/bool round-trips, edge cases —
  INT64_MIN, ±inf/nan policy, leading/trailing whitespace, overflow → fail-loud). New
  OP_CALL_RUNTIME ids 49–55, native wrappers, WASM reactor. Surface = explicit builtins. Proof
  `.tks` native + WASM. **BOUNDED** (mirrors the crypto/time runtime pattern exactly). **Start here.**
- **16.B — String-concat + auto-`to_string` on `+` for primitives.** Value-type tracking in
  `frontend_interop.c` (each producer carries int/float/bool/char/string/object); `"a" + n`
  auto-converts the non-string operand via the 16.A ids, then `teko_rt_str_concat`. Proof.
  **MEDIUM** (new value-type plumbing in the frontend).
- **16.C — Interpolation auto-`to_string`.** `"pre{e}post"` (the lexer already tokenizes
  `TOKEN_STRING_INTERPOLATED` / `_RAW_INTERP`) lowers to nested concat + per-hole `to_string`.
  **MEDIUM** (size depends on how much interpolation parsing already exists — to be scoped at 16.C open).
- **16.D — User-defined-type `to_string` in concat/interpolation.** Concrete receiver →
  `OP_CALL_FUNC` to the `to_string` slot; abstract/trait receiver → vtable slot. When absent,
  synthesize the culture-invariant default (per-type field-walk generator). **MEDIUM** (reuses the
  Phase-15 method/vtable resolution; the hook is already in place).
- **16.E — Explicit-format spec.** Radix (hex/oct/bin), float precision, grouped digits, custom
  masks via a format string (ids 56/57+). **BOUNDED–MEDIUM.**
- **16.F — Checked inter-type conversions.** Primitive casts that fail loudly (narrowing range
  checks, float→int truncation policy), complex/user-defined casts. **MEDIUM.**

## Discipline (unchanged, non-negotiable)
One increment per commit; build + Unity suite; **ASan + UBSan on BOTH dispatch paths + TSan**
clean each commit; the **16 native emitter goldens never regress**; all four CI gates green
(incl. Windows MSVC — guard POSIX/LLP64) before any sub-block is "done"; patient CI watch (≥90s);
**no dead tokens** (executable `.tks` proof per surface, native + WASM); **no merge / force-push**
— the human merges.
