# Hand-off — Phase 17.F decimal (17.F.3 + 17.F.4 remaining)

This doc lets a FRESH session continue the 256-byte `decimal` work. The f64 core (17.A–17.E) and the
decimal **runtime cores** (17.F.1 arithmetic, 17.F.2 parse/format) are DONE & on the branch. What
remains: **17.F.3** (value model + opcodes + `dec` literal — wire decimal into the frontend/backends)
and **17.F.4** (checked casts + language surface + auto-`to_string`). All on PR #10, same branch
`feat/phase-17-floating-point` (the human merges; owner-confirmed it's one PR).

## Bootstrap (a fresh session needs folder-trust)
Read, in order: `CLAUDE.md` (root), `src/CLAUDE.md`, `runtime/wasm/CLAUDE.md`,
`.claude/agents/teko-engineer.md`, `docs/PHASE17_FLOATING_POINT.md` (the WHOLE Status section), and
this doc. The memory file `teko-phase17-floating-point.md` has the condensed state.

## Owner-LOCKED decisions (do not relitigate)
- **Same PR #10 / same branch** (not a separate PR). Owner knows #10 only merges when 17.F closes.
- **256-byte fixed value type**, no heap: `teko_decimal` = `uint8 sign, scale, flags, _pad[5]` +
  `uint64 limb[31]` (LE coefficient, 248 B). `_Static_assert(sizeof==256 && _Alignof==8)`. Value =
  (-1)^sign · COEFF · 10^(-scale). scale 0–38. Always-finite; **fail-loud on overflow** (no NaN/Inf).
- **Banker's rounding** (round-half-to-even). Arithmetic semantics pinned in `teko_decimal.c`'s header
  and **owner-APPROVED** (incl. `mod` = Python `Decimal.__mod__`, ±0 equal). Build on them.
- **Literal suffix = `dec`** (`9.99dec`, `1000dec`) — NOT `m` (collides with the time-range minutes
  suffix). Applies in 17.F.3 lexing.
- **ABI = by pointer.** A decimal is a 256-byte buffer; ops take `const teko_decimal*`/`teko_decimal*`.
  It is a **VALUE TYPE** (copy on store/assign — memcpy), NOT a handle (no aliasing).
- KAT oracle = Python `decimal` (prec 600, ROUND_HALF_EVEN), committed generator
  `tools/gen_decimal_kats.py` → `tests/runtime/teko_decimal_kat_vectors.h`, byte-for-byte.

## Already DONE (on the branch, CI-green or pending-green)
- f64 core 17.A–17.E (PR #10). Reserved opcodes `0x83–0x96` + `TEKO_VT_DECIMAL=(1<<21)` (codegen_li.h
  / frontend_interop.c) — comments only, NOT yet live.
- **17.F.1** `8f139a0` — `src/runtime/teko_decimal.{c,h}`: arith (`teko_decimal_add/sub/mul/div/mod/cmp`,
  by pointer, int 1/0 return), bn limb helpers (`umul64`, `bn_add/sub/cmp/mul`, `bn_divmod` Knuth,
  `bn_mul_pow10`, `bn_round_drop_decimals`), `teko_decimal_zero`, `teko_decimal_from_components`. 1326
  KATs. CI-green.
- **17.F.2** `34b595b` — `teko_decimal_to_string` (scale-preserving plain `.`-decimal, malloc'd) +
  `teko_decimal_parse` (checked, no exponent, bare number — the `dec` suffix is stripped by the lexer).
  316 format/parse KATs + round-trips. (CI confirm pending at hand-off time — verify
  `gh pr checks 10`.)
- `teko_decimal.c` is in CMake CORE_SOURCES + the `teko_rt` archive + the reactor `SRCS`
  (`build-crypto-reactor.sh`, compile-only — NOT yet in EXPORTS).

## 17.F.3 — value model + opcodes + `dec` literal (the big integration). NO casts/surface yet.
Bring the reserved opcodes LIVE and carry a decimal value through the frontend + both backends.
Decimal value = a **256-byte memory slot** (does NOT fit a register), passed by pointer.

### Opcodes to make live (codegen_li.h — already reserved 0x83–0x96; 17.F.3 uses these subset)
- `OP_DCONST` 0x83 (4-byte index into a NEW **decimal-constant pool**; loads pool[idx] → `$d0` slot)
- `OP_DADD/DSUB/DMUL/DDIV/DMOD` 0x84–0x88, `OP_DEQ/DNE/DLT/DLE/DGT/DGE` 0x89–0x8E (compares → i32 0/1
  in `$w0` → VT_INT), `OP_DSTORE` 0x8F (`$d1`←`$d0` memcpy), `OP_DLOAD` 0x90 (`$d0`←`$d1`),
  `OP_DSTORE_LOCAL` 0x91 / `OP_DLOAD_LOCAL` 0x92 (256-byte memcpy to/from a decimal local slot).
  (Casts `OP_I2D/D2I/F2D/D2F` 0x93–0x96 are 17.F.4.) Add emit helpers in codegen_li.c + the 4-byte-arg
  width for DCONST/DSTORE_LOCAL/DLOAD_LOCAL in **all THREE codegen_metal.c walkers** (the float ops are
  the precedent) + CSE bookkeeping (decimal compares clobber `$w0`; the others are barriers).

### Decimal-constant pool (mirror the string/float pools)
- BytecodeBuffer: `unsigned char* decimals; int decimal_count/capacity;` storing 256-byte blobs;
  `codegen_li_add_decimal_constant(buf, const teko_decimal*)` → index (dedup by 256-byte memcmp).
  `uses_decimal` flag. MetalContext: `const unsigned char* decimal_pool; int decimal_count;` +
  `wasm_emit_decimal` (gate). Setter `teko_metal_set_decimals` called in BOTH main.c (native) and
  codegen_li_wasm.c (wasm) — exactly like `teko_metal_set_floats` (17.A).

### Memory-slot infrastructure (the crux — different per backend)
- **Native (emit_native_hosted.c):** `$d0`/`$d1` = two 256-byte STACK slots (extend the frame;
  16-byte-align). Decimal locals = 256-byte frame slots (a parallel `$dvN` file; the slot index space
  can be separate from int/float locals or shared-by-distinct-offset — simplest: a dedicated decimal
  frame region sized by a decimal-local high-water count). DCONST: copy 256 B from a `.rodata` blob
  (emit the pool as bytes) into `$d0`. DADD etc.: `lea` the two slot addresses into the ABI arg regs
  (rdi=&$d0, rsi=&$d1, rdx=&$d0 for in-place out) and `call teko_rt_decimal_add` (a NEW wrapper — see
  below). Compares: `call teko_rt_decimal_cmp(&$d0,&$d1,&tmp_i32)` then map tmp to 0/1 in `$w0`.
  DSTORE/DLOAD/D*_LOCAL: 256-byte `memcpy` (emit a small copy loop or `call memcpy`).
- **WASM (emit_wasm.c):** `$d0`/`$d1` + decimal locals need **256-byte regions in Teko's linear
  memory** (NOT wasm locals — too big). Reserve fixed offsets: e.g. carve a decimal scratch region
  out of the existing layout (see the memory-map comment near the top of emit_wasm.c: run queue
  [0..64), string pool [1024..2048), arena [2048..16384), heap [16384..65536)). Add a small fixed
  **decimal slot region** (e.g. a handful of 256-B slots for $d0/$d1 + N decimal locals) — pick an
  unused sub-range or grow the reserved area; document it and keep float-free/decimal-free modules
  byte-identical (gate all of it on `wasm_emit_decimal`). The **decimal-constant pool** is emitted as
  a `(data ...)` segment (like the string pool) at known offsets; DCONST = `memory.copy` (or a
  loop) from pool offset → $d0 slot offset. DADD: push the i32 slot offsets, `call $decimal_add`
  (reactor import `(param i32 i32 i32)(result i32)` = &a,&b,&out over the SHARED memory). Compares →
  result i32 → $w0. D*_LOCAL/DSTORE/DLOAD = `memory.copy` 256 B between slot offsets.
  NOTE: the reactor shares Teko's linear memory, so passing i32 offsets into Teko's memory works
  exactly like the crypto hex-string ABI. Add `teko_rt_decimal_*` to the reactor EXPORTS.

### Runtime wrappers (fail-loud) — runtime/native/teko_rt.c (+ reactor via EXPORTS)
`teko_rt_decimal_add/sub/mul/div/mod(const teko_decimal* a, const teko_decimal* b, teko_decimal* out)`
calling the 17.F.1 core and `teko_rt_die("decimal: overflow/divide by zero")` on a 0 return
(native exit 70 / wasm reactor `__builtin_trap`). `teko_rt_decimal_cmp(a,b,out_i32)`. These are what
the opcodes call. Add to teko_rt.h + the reactor EXPORTS list.

### Frontend (frontend_interop.c)
- `TEKO_VT_DECIMAL` already defined. Add a `g_localdec` registry (parallel to `g_localflt`); reset it
  alongside the others; eval_primary returns VT_DECIMAL for a decimal local (emit DLOAD_LOCAL).
- **`dec` literal lexing:** a numeric literal with a trailing `dec` → parse the bare number via
  `teko_decimal_parse` → `codegen_li_add_decimal_constant` → `OP_DCONST <idx>`, value-type VT_DECIMAL.
  CHECK how the lexer tokenizes `9.99dec` — likely the `dec` is swept as a unit-suffix or a separate
  ident (see the 17.A finding: the float lexer swept `e9`/`f`); you may need a small lexer extension
  to recognize the `dec` suffix on a numeric (look at how time-range suffixes `ms/s/m/h` are lexed in
  `src/lexer*.c` and mirror that for `dec`). Document what you find.
- `eval_expr_prec`: decimal-only arithmetic in 17.F.3 (both operands VT_DECIMAL → spill left to a
  decimal temp via DSTORE_LOCAL, eval right, DADD/etc.; compares → VT_INT). **Mixed int/decimal (and
  float/decimal) promotion is 17.F.4** (needs I2D/F2D) — for 17.F.3, a mixed expr can stay
  unsupported/deferred (document). let/reassign store a decimal initializer via DSTORE_LOCAL + record
  in g_localdec.
- `uses_decimal` set whenever a decimal op/const is emitted (gates the WASM decimal region + reactor
  import + native frame region).

### Proof (.tks) — native + WASM byte-identical, observed WITHOUT a to_string surface (like 17.A)
`runtime/{native,wasm}/samples/decimal.tks`: decimal literals + arithmetic + COMPARES funneled to int:
    let a = 9.99dec;
    let b = 0.01dec;
    let c = a + b;                 // 10.00
    let ten = 10.00dec;
    let eq = (c == ten);           // 1
    let big = 100.00dec;
    let lt = (c < big);            // 1
    emit("eq = " + convert.int_to_str(eq));   // eq = 1
    emit("lt = " + convert.int_to_str(lt));   // lt = 1
  (Decimal arithmetic result observed via DCMP→int→int_to_str id 49, exactly as 17.A observed floats.
  The decimal.to_string SURFACE + auto-to_string is 17.F.4.) Wire into run-native.sh + a
  run-decimal.mjs + wasm.yml. Byte-identical native vs WASM.

## 17.F.4 — checked casts + surface + auto-`to_string`
- **Casts (opcodes 0x93–0x96):** `OP_I2D` (int $w0 → decimal $d0), `OP_F2D` ($f0 → $d0), `OP_D2I`
  (decimal → checked i32 in $w0, fail-loud on non-integer/overflow per the owner's checked posture),
  `OP_D2F` (decimal → f64 $f0). Each lowers to a `teko_rt_decimal_*` cast (add cores in teko_decimal.c
  + KATs vs the oracle). Surface as `convert.to_decimal(<int|float expr>)` / `convert.to_int(<decimal>)`
  (extend the 17.B floatcast head) / `convert.to_float(<decimal>)`. Enables mixed int/float/decimal
  arithmetic via promotion in eval_expr_prec (promote the non-decimal operand via I2D/F2D).
- **Surface `decimal.to_string` / `decimal.parse`:** NEW OP_CALL_RUNTIME ids (next free after 58 — use
  **59 = decimal_to_string** (decimal ptr → string), **60 = decimal_parse** (string → decimal, checked
  fail-loud)). `decimal.to_string` ABI: arg = &$d0 (i32 offset/ptr), result string in $w0 (VT_STR).
  `decimal.parse`: arg = string ptr $w0, result decimal in $d0 (VT_DECIMAL). Add to
  `teko_native_runtime_symbol` + the wasm import/lowering (pointer ABI) + reactor EXPORTS +
  `runtime_result_vt` (59→VT_STR, 60→VT_DECIMAL).
- **Auto-`to_string` for decimal:** `coerce_to_string_in_w0(VT_DECIMAL)` → emit id 59 (reads &$d0);
  thread a left-decimal concat operand back into $d0 via DLOAD_LOCAL before coercing (exactly the 17.D
  float pattern). So `"x = " + d` and `"{d}"` work.
- **Proofs:** `decimal_surface.tks` (to_string of arithmetic results, parse round-trip, auto-to_string
  in concat/interp, mixed int+decimal via to_decimal) + a fail-loud `decimal_fail.tks`
  (parse "abc" or D2I of a non-integer / overflow) → native abort / WASM trap. Byte-identical.

## Discipline (every commit, both sub-blocks)
1 increment/commit; build + suite; **ASan+UBSan BOTH dispatch paths + TSan clean**; **16 native
goldens + all float-free/decimal-free native+WASM output byte-identical** (gate ALL decimal emission
on `uses_decimal`/`wasm_emit_decimal`); **4 CI gates green incl. Windows MSVC** (256-byte struct
alignment / `_Static_assert` / by-pointer ABI — extra care); executable `.tks` proof native+WASM
byte-identical per surface; **no dead tokens**; **no `__int128`/libc** in the runtime; **no
merge/force-push** (human merges); patient CI watch (poll until all 4 gates resolve — the gate job
races ahead of the matrix, so poll the matrix jobs). Report to the owner at each sub-block close with
CI green; pause only on a NEW scope fork.

## Verification helpers (local, this machine)
Build: `cmake --build build --target teko teko_tests -j4` (Make generator; NO Ninja here). Suite:
`./build/teko_tests`. Sanitizers: `build-asan` (default), `build-asan-port`
(`-DTEKO_VM_PORTABLE_DISPATCH`), `build-tsan` (see prior commits' flags). Native proof:
`bash runtime/native/run-native.sh ./build/teko ./build/libteko_rt.a`. WASM: `bash
runtime/wasm/crypto/build-crypto-reactor.sh runtime/wasm/crypto/crypto.wasm`, then `./build/teko build
…--target=wasm`, `wat2wasm` (Homebrew, present), `node runtime/wasm/run-*.mjs`. wat2wasm + wasm32
clang/wasm-ld are installed locally.
