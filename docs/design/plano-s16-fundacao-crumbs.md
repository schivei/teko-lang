# §16 FOUNDATION — crumb plan (enablers + easy leaf subsystems)

> Scope of THIS pass: the ENABLERS that unblock §16 (`extern type` C-ABI struct,
> `teko::sys` constants, the FFI-binding pattern) plus the four EASY leaf-subsystem
> conversions (string/text, float-bits, char/UTF-8, env+os/arch+time+random). The
> ARENA (phase 5, the keystone) and threads/process/test/crash are NAMED-DEFERRED
> here, not designed. HEAD `5f24ad9f`. Author: architect. Design-doc only — no product edits.

---

## 0. Ground truth (verified on HEAD `5f24ad9f`)

C surface to retire (7861 lines): `src/runtime/teko_rt.c` (5515), `teko_rt.h` (1751),
`src/assert/assert.c` (256), `src/win32_compat.h` (339).

Mechanisms that ALREADY EXIST and this plan reuses:

| Fact | file:line |
|------|-----------|
| `extern fn NAME(...) : T = "sym" from "c"` parses + links + runs (libc-direct) | `src/runtime/teko_rt.tks:697` (`rt_abort() = "abort" from "c"`); parse at `src/parser/parse_decl.tks:516-542` |
| extern fn prototype emitted as `extern <ret> <c_symbol>(params);`, symbol raw (no mangling) | `src/codegen/codegen.tks:10768-10775` |
| extern call emits the raw `c_symbol` | `src/codegen/codegen.tks:5148`, call path `:5145-5151` |
| `extern type Name` (opaque) → `typedef void *tk_t_Name;` | parse `src/parser/parse_decl.tks:1551-1574`, AST `src/parser/ast.tks:786`, codegen `src/codegen/codegen.tks:11547-11553` |
| extern param/return gate `extern_type_ok` (admits `Prim`/`Byte`/`Ptr`/`Uptr`/`Named→ExternBody`/null-union) | `src/checker/typer.tks:8479-8492`, enforced `:8772-8797` |
| `teko_rt_type_ok` (the closed `from "teko_rt"` exception) | `src/checker/typer.tks:8505-8518` |
| `ptr<T>` → `<T> *`; opaque `ptr` → `void *` | `src/codegen/codegen.tks:3072`, `:1868-1877` |
| `ref T` receiver lowers to `T *` (self_is_ref) | `src/codegen/codegen.tks:10784-10791` |
| ref→ptr crossing (`&local` at extern arg) | `src/codegen/codegen.tks:5195-5196` (`if !arg_is_ref { out=cb(out,"&") }`) |
| §17 prune: EVERY top-level `Item.guard` (`fn`/`type`/`const`/extern) pruned per-target BEFORE checker | `src/build/prune.tks:70-104` |
| §17 env axes: os ∈ {linux,macos,windows,unknown}, arch ∈ {x86_64,arm64,unknown} | `src/build/prune.tks:9-21,52-55` |
| `#os(...)`/`#arch(...)` attributes parse to a `Pred` on the item | `src/parser/parse_decl.tks:1609-1621,1647-1652`; `pred_os`/`pred_arch` `src/parser/ast.tks:1146,1158` |
| module-level `const NAME: T = init` (#594), guardable, a valid `@Decl()` | AST `src/parser/ast.tks:934`; parse `src/parser/parse_decl.tks:1858-1876` |
| Teko struct emits a FAT header `tk_struct_hdr __hdr` as member 0 | `src/codegen/codegen.tks:11413` |
| `teko::f64_bits` / `teko::f64_from_bits` are compiler-visible builtins (used in comptime_fold) | `src/checker/comptime_fold.tks:480,1643` → C `tk_f64_bits`/`tk_f64_from_bits` at `src/runtime/teko_rt.c:5264-5265` |
| the extern-struct pain point that §16.E1 SOLVES, stated in prose today | `src/time/time.tks:20-36` |

The `src/time/time.tks:20-36` doc-comment is the load-bearing evidence for why the
KEYSTONE below is needed. It documents that a same-shaped *Teko* struct used across an
extern boundary is emitted under the mangled `tk_t_<Name>` typedef which is NOMINALLY
DISTINCT from `teko_rt.h`'s own typedef, so "a value fresh off an extern call can NEVER
be stored in a `let`/field or passed to a non-extern fn (a C COMPILE ERROR)". The whole
module works around this by using only bare-scalar externs. `extern type = struct`
removes the workaround: there is ONE canonical `tk_t_<Name>` typedef, and — crucially —
it is emitted WITHOUT the fat header, so it is layout-identical to the real C struct.

---

## 1. KEYSTONE ENABLER — `extern type Name = struct { … }` (compiler-touching)

### 1.1 Surface

```
extern type Timespec = struct { tv_sec: i64; tv_nsec: i64 }
extern type Stat = struct { dev: u64; ino: u64; mode: u32; size: i64 }
```

A C-ABI struct-layout type: field ORDER and PADDING are C's, there is NO fat header, no
methods, no self-construct, no inheritance. It exists to name a foreign struct at the FFI
boundary. It is NOMINALLY DISTINCT from a normal Teko `struct` (which is a fat value type)
and from the opaque `extern type Name` (which is `void *`).

### 1.2 Parser addition (`src/parser/parse_decl.tks`)

Today `parse_type_decl` (`:1530-1581`), on seeing `extern` (`:1552-1555`), returns the
opaque `ExternBody` UNCONDITIONALLY without consuming a `= body` (`:1572-1574`). Change:
after `is_extern` and the type-params, BRANCH on whether an `=` follows.

- No `=` → opaque `ExternBody { }` (unchanged; back-compatible).
- `= struct { … }` → parse the field list with the EXISTING `parse_fields`
  (`:949-997`) and wrap in a new `ExternStructBody`. REJECT methods/consts/`implements`
  in this position (an extern struct is fields-only): if `parse_fields` returns any
  method or const, error `an extern struct declares only fields (no methods, consts, or interfaces)`.
- `= <anything-but-struct>` → error `an \`extern type\` is either opaque (\`extern type Name\`) or a C-ABI struct (\`extern type Name = struct { … }\`)`.

New AST node (`src/parser/ast.tks`, beside `ExternBody` at `:786`):

```teko
/**
 * ExternStructBody — an `extern type Name = struct { … }` body: a C-ABI struct LAYOUT
 * for the FFI boundary. Distinct from `StructBody` (a fat Teko value type, which prepends
 * a `tk_struct_hdr` header word) and from `ExternBody` (an opaque `void *` handle). The
 * fields are laid out in DECLARATION ORDER with C's own padding/alignment — codegen emits
 * a header-less `typedef struct` so the layout is byte-identical to the foreign C struct
 * the symbol expects. Fields-only: no methods, consts, or `implements`.
 *
 * @field fields  the C-ABI fields, in declaration order (each type must be C-ABI-admissible)
 * @since §16 (FFI foundation)
 * @see extern_struct_field_ok — the checker gate on admissible field types
 */
pub type ExternStructBody = struct { fields: []Field }
```

Add `parser::ExternStructBody` to the `TypeBody()` macro union (`src/parser/ast.tks:875`).
Every `@TypeBody()` match in checker/codegen/macro_expand then gets a new arm (the
compiler's exhaustiveness will list them; the load-bearing ones are enumerated in §1.3-1.4).

### 1.3 Checker representation (`src/checker/`)

The type is registered like any nominal `TypeDecl`, but treated as C-ABI-opaque-layout:

1. **`resolve.tks` / `collect.tks`** — an `ExternStructBody` becomes a resolvable `Named`
   type. It contributes NO trait surface, NO methods, NO auto-conformance (mirror how
   `ExternBody` is handled at `resolve.tks:1201`, `check_modules.tks:494`, `borrow.tks:110`).
   Add the `ExternStructBody` arm ALONGSIDE each existing `ExternBody` arm — same
   "opaque, honest-stop" treatment, PLUS the field list is retained for two things:
   codegen layout, and field-access typing.

2. **New field-type gate** (`src/checker/typer.tks`, beside `extern_type_ok` at `:8479`):

```teko
/**
 * extern_struct_field_ok — is `t` admissible as a field of an `extern type … = struct`?
 * A C-ABI struct field must itself have a stable, header-less C representation: a numeric
 * `Prim`, a `Byte`, an opaque/typed `Ptr`, a `Uptr`, or ANOTHER extern struct (nested
 * C-ABI layout). A `Str` (fat `{ptr,len}`), a `Slice`, a normal Teko `Named` (fat header),
 * an `Error`, a `Void`, or a `Func` are REJECTED — each would inject a Teko-internal layout
 * that does not match the foreign struct.
 *
 * @param t      the field's resolved type
 * @param table  the resolved type table (to probe a `Named` field's body)
 * @return       true iff `t` may appear as an extern-struct field
 * @since §16
 */
fn extern_struct_field_ok(t: @Type(), table: TypeTable): bool {
    match t {
        Prim => true
        Byte => true
        Ptr => true
        Uptr => true
        Named as nm => match type_table_find(table, nm.name, "") {
            parser::TypeDecl as td => match td.body { parser::ExternStructBody => true; parser::ExternBody => true; _ => false }
            error => false
        }
        _ => false
    }
}
```

   Enforce it where each extern struct is collected (a new validator pass over the decl's
   fields, reported with the field's `line`/`col`).

3. **Admit the extern struct at the FFI boundary** — `extern_type_ok`
   (`typer.tks:8479-8492`) currently admits a `Named` only when its body is `ExternBody`
   (`:8486`). WIDEN that arm:

```teko
        Named as nm => match type_table_find(table, nm.name, "") {
            parser::TypeDecl as td => match td.body {
                parser::ExternBody => true
                parser::ExternStructBody => true   // §16: a C-ABI struct passed BY VALUE
                _ => false
            }
            error => false
        }
```

   With that, `extern fn f(s: Stat): i32 = "f" from "c"` (by value) and
   `extern fn g(p: ptr<Stat>): i32 = "g" from "c"` (by pointer — `Ptr` already admitted at
   `:8483`) both pass the gate. `teko_rt_type_ok` already admits `Named` (`:8515`) so no
   change is needed for the `from "teko_rt"` path (irrelevant here — leaf conversions use
   `from "c"`).

4. **Field access typing** — reading `ts.tv_sec` must type as the field's declared type.
   The extern struct carries a `[]Field` exactly like `StructBody`, so field-projection
   typing reuses the struct-field path; only the arm dispatch (`StructBody | ExternStructBody`)
   is added. WRITING a field (`ts.tv_sec = 0`) and zero-initialising a local
   (`var ts: Timespec`) are both allowed — an extern struct is a plain mutable aggregate
   with no invariant to protect. NO `.{}` self-construct, NO methods.

### 1.4 Codegen (`src/codegen/codegen.tks` — `emit_type_decl` at `:11404`)

Add an `ExternStructBody` arm. It is the `StructBody` arm (`:11406-11428`) MINUS the fat
header line (`:11413`):

```teko
        parser::ExternStructBody as esb => {
            /* (§16) a C-ABI struct: emit a HEADER-LESS `typedef struct` so the layout is
               byte-identical to the foreign C struct. UNLIKE a Teko `StructBody`, NO
               `tk_struct_hdr __hdr` member is prepended — field offsets are exactly C's. */
            var out = cb(buf, "typedef struct ")
            out = mangle_type_name(out, "", d.name)
            out = cb(out, " {\n")
            var i = 0
            loop {
                if i >= esb.fields.len { break }
                out = cb(out, "    ")
                out = match emit_type_expr(out, prog, esb.fields[i].type_ann, checker::name_qualifier(d.name)) { []byte as o => o; error as err => return err }
                out = cb(out, " ")
                out = cb_str(out, esb.fields[i].name)
                out = cb(out, ";\n")
                i++
            }
            out = cb(out, "} ")
            out = mangle_type_name(out, "", d.name)
            cb(out, ";\n\n")
        }
```

Forward-declaration pass (`:13317-13329`) gets an `ExternStructBody => { typedef struct … }`
arm alongside `StructBody` so a `ptr<Stat>` field/param can forward-reference it.

**Why this is ABI-correct (grounded reasoning).** The emitted `teko.c` includes only
`teko_rt.h`; it does NOT `#include <time.h>`. So OUR emitted extern prototype is the
authoritative C declaration of the symbol. The C compiler type-checks `clock_gettime(...,
&ts)` against `extern int clock_gettime(int, tk_t_Timespec *)` and `&ts` is
`tk_t_Timespec *` — types agree. The LINKER binds the raw symbol `clock_gettime` to libc.
Runtime correctness reduces to a SINGLE obligation: `tk_t_Timespec`'s layout must equal
`struct timespec`'s. Since our emit is header-less and both fields are naturally-aligned
`int64_t`, the layouts are identical. This is the classic FFI-by-redeclaration contract.

**Risk / law tension (documented, resolved).** FFI-by-redeclaration bypasses the real C
header, so a WRONG field type in the Teko decl is silent UB, not a compile error. This is
inherent to any libc-direct FFI and is exactly the "the marshalling is TOTAL / the boundary
is unsafe by contract" posture already ratified in `c-types-and-marshalling-0.3.1.md`
(§1, PIN-2). Resolution: extern structs live behind `extern unsafe fn` wrappers (the
`unsafe` marker parses and survives today — `c-types-and-marshalling-0.3.1.md §5`), and the
per-field type is asserted against the platform C struct by a REGRESSION fixture (§5 below)
that reads a field written by the real libc call. No new law needed; passes Teko-only
(all `.tks`), passes the unsafe-boundary law. Bitfields / non-scalar packing are OUT of
scope for the leaf subsystems and are flagged where they would matter (none of phases 1-4
need one).

### 1.5 Reseed impact

`extern type = struct` touches grammar + checker + codegen → **compiler-touching →
fixpoint + reseed** (owner rule). It is INERT until the FIRST extern struct is used by a
monomorphized/emitted path, but the grammar/checker/codegen delta itself changes the
compiler image, so it reseeds as its own crumb. Bootstrap-seed ordering: this feature must
land and reseed BEFORE any leaf subsystem that declares an extern struct (phase 4 time).
The corpus itself introduces NO `extern type = struct` in the same crumb (keep the delta
inert-in-corpus for a clean fixpoint, exactly like the 9-ops keystone landed).

---

## 2. `teko::sys` — curated per-platform constants module (stdlib + §17)

A HAND-WRITTEN module (`src/sys/sys.tks`, namespace `teko::sys`). NO C-macro import: every
libc/syscall constant the runtime needs is a Teko `const` whose value is written out
literally, guarded per-`#os`/`#arch` with the §17 prune (`src/build/prune.tks`). One const
per value; `#os`/`#arch` blocks select the per-target literal. Because the prune runs
BEFORE the checker (`prune_cc`), only the matching-target consts survive — there is never a
duplicate-symbol clash between the linux and macos block.

### 2.1 Convention

```teko
/**
 * teko::sys — curated libc/syscall CONSTANTS for the FFI runtime. Each value is written as
 * a literal Teko `const`, per-`#os`/`#arch`-guarded (§17 prune). NO C header/macro is ever
 * imported: the numbers below are transcribed from the platform ABI and pinned by a
 * regression that round-trips each through the real syscall. One const per value; the
 * `#os` prune keeps exactly the matching-target definition.
 *
 * @since §16
 */
```

Convention rules:
- One `pub const NAME: i32 = <literal>` per value (width = the syscall's parameter type;
  `i32` for `open`/`clock` flags, `u32`/`i64` as the ABI dictates).
- Values that DIFFER by OS get one `#os("linux")` block and one `#os("macos")` block, each a
  full definition of the same-named const. The prune keeps one.
- Values that differ by ARCH (rare — some `SYS_*` syscall numbers) get `#arch(...)` guards.
- A value IDENTICAL across targets gets ONE unguarded const.

### 2.2 First constants (exactly what phases 1-4 need)

Only phase 4 needs any `teko::sys` constant at all (phases 1-3 are pure Teko / no libc
constant). The MINIMAL first set:

```teko
/** CLOCK_MONOTONIC — monotonic clock id for `clock_gettime`. */
#os("linux") pub const CLOCK_MONOTONIC: i32 = 1
/** CLOCK_REALTIME — wall-clock id for `clock_gettime`. */
#os("linux") pub const CLOCK_REALTIME: i32 = 0
#os("macos") pub const CLOCK_MONOTONIC: i32 = 6
#os("macos") pub const CLOCK_REALTIME: i32 = 0
```

- `env` (getenv/setenv/unsetenv): NO constant needed (`setenv`'s overwrite arg is a plain
  `i32` the caller passes as `1`/`0`).
- `random` (getentropy / getrandom): NO constant needed for the base call (`getrandom`'s
  flags arg is `0`; `getentropy` takes none). `GRND_NONBLOCK`/`GRND_RANDOM` are deferred to
  the crypto-rand hardening pass, not the leaf.
- `os`/`arch`: NO libc constant — these are compile-time strings (§4.4).
- float-bits: NO constant.

The larger `teko::sys` surface (`O_RDONLY`/`O_WRONLY`/`O_CREAT`/`O_TRUNC`,
`SEEK_SET/CUR/END`, `AF_UNIX`/`SOCK_DGRAM`, `MAP_ANONYMOUS`/`MAP_PRIVATE`/`PROT_READ`/
`PROT_WRITE`, `EAGAIN`/`EEXIST`/`EINTR`, `O_NONBLOCK`) is CATALOGUED here for the fs / net /
arena subsystems but is NOT built in this pass (those subsystems are later phases). The
module SKELETON + the four time consts above are what land now.

### 2.3 Reseed impact

`teko::sys` is a stdlib leaf module the COMPILER does not consume → **no reseed** (owner's
"leaf modules don't require a reseed"). It ships as its own crumb, gated only on the §17
prune already in the seed.

---

## 3. The FFI-binding PATTERN (the reusable recipe)

> **⚠️ SUPERSEDED IN PART by §11 REFRESH (2026-08-16, post-C1).** The rules below that name
> `extern unsafe fn`, `ptr<byte>`/`ptr<T>`, and a call-site `ref` operator are STALE — C1
> landed a different (simpler) real surface. Read §11 for the corrected str↔cstr recipe, the
> real `extern fn … from "c"` + `ref T` parameter shape, and the worked `getenv` example.
> The historical text is kept for provenance only.

The recipe for turning ONE C-subsystem function into Teko+FFI. Each numbered rule cites the
mechanism it rides.

1. **A raw libc/syscall call becomes an `extern fn … from "c"`.** Symbol emitted raw, no
   mangling (`codegen.tks:10768-10775`). Wrap in `extern unsafe fn` so the unsafe colour is
   honest (`c-types-and-marshalling-0.3.1.md §5`); a plain safe Teko wrapper then calls it.
2. **Scalar in / scalar out** — the simplest edge: params + return are `Prim`/`Byte`/`Uptr`;
   admitted by `extern_type_ok` unchanged. (e.g. `getpid`, `close`, `f64_bits`.)
3. **`ptr<byte>` for buffers** — a `char*`/`void*` buffer is `ptr<byte>`, lowering to
   `char *`/`void *` (`codegen.tks:3072`), and is SAFE-carrying today
   (`c-types-and-marshalling-0.3.1.md`, `resolve.tks:1163`). Proven twice in the regressor
   (`q026`, `q170`).
4. **`ref T` → typed out-pointer for fat returns.** The out-param convention: declare the
   parameter `ptr<T>` where `T` is an `extern type = struct` (or a scalar), lowering to
   `tk_t_T *` (`codegen.tks:3072`). The caller takes the address of a stack local via the
   ref→ptr crossing (`marshall-spec.md`, issue #498; codegen `&local` at `:5195-5196`). This
   is how `clock_gettime(clk, &ts)` is expressed without a C compile error — the ONE thing
   the KEYSTONE (§1) unlocks.
5. **`extern type = struct` for a struct arg/return** — by VALUE when small
   (`extern fn f(s: Stat)`), by POINTER for out-params (rule 4). By-value passes the
   register/stack ABI identically because the layout matches (§1.4).
6. **u64-handle / opaque-`ptr` ABI** — a `FILE*`/`DIR*`/`pthread_t` we never dereference in
   Teko stays an opaque `extern type Name` → `void *` (`codegen.tks:11547-11553`), or a
   `uptr` word. (Used by fs/threads — later phases; noted for completeness.)

### 3.1 Concrete example — `clock_gettime(CLOCK_MONOTONIC, &ts)` → Teko

```teko
/** Timespec — the C `struct timespec` (two 64-bit words) for `clock_gettime`. */
extern type Timespec = struct {
    /** seconds since the clock's epoch */
    tv_sec: i64
    /** nanoseconds within the second (0..999_999_999) */
    tv_nsec: i64
}

/**
 * clock_gettime — the raw POSIX clock read (libc-direct). Fills `*ts` for clock id `clk`.
 *
 * @param clk  the clock id (`teko::sys::CLOCK_MONOTONIC` / `CLOCK_REALTIME`)
 * @param ts   an out-pointer to a caller-owned `Timespec` the call fills
 * @return     0 on success, -1 on error (errno set — unread here)
 * @since §16
 */
extern unsafe fn clock_gettime(clk: i32, ts: ptr<Timespec>): i32 = "clock_gettime" from "c"

/**
 * monotonic_ns — nanoseconds from the monotonic clock as a signed `i64` span. A safe Teko
 * wrapper over the unsafe libc read: it owns the `Timespec`, passes its address, and folds
 * the two words into one `i64` on the Teko side (fully storable — no extern-struct value
 * escapes). Replaces C `tk_rt_monotonic_ns` (`src/runtime/teko_rt.c:5365-5376`).
 *
 * @return  ns since an unspecified monotonic epoch (only differences are meaningful)
 * @since §16
 */
pub fn monotonic_ns(): i64 {
    var ts: Timespec = Timespec { tv_sec = 0 to i64; tv_nsec = 0 to i64 }
    var _ = clock_gettime(teko::sys::CLOCK_MONOTONIC, ref ts)
    ts.tv_sec * (1000000000 to i64) + ts.tv_nsec
}
```

`ref ts` at the `ptr<Timespec>` position rides the ref→ptr crossing (issue #498); codegen
emits `&ts` (`:5195-5196`); the local `ts` is a header-less `tk_t_Timespec`, so `&ts` is a
`tk_t_Timespec *` that libc fills. NOTE for the implementer: if the ref→ptr crossing does
not yet accept an `extern type = struct` local as its `&`-able source, the fallback is a
`teko::mem` address-of builtin — this is the ONE integration point to probe first when the
keystone lands (see §7 blocker note).

---

## 4. Easy leaf-subsystem conversions (phases 1-4, proof of pattern)

Each is a stdlib/runtime module conversion. For each: C fns retired, FFI target, `teko::sys`
consts, reseed impact, and any hard blocker.

### Phase 1 — string/text helpers (pure Teko, NO FFI)

- **C retired:** the float-to-text renderers `tk_f64_g17` / `tk_ftoa` and their `_len`
  twins (`teko_rt.c:1070-1090, 609-612`), `rt_valid_utf8` (`:450-489`) and any `<ctype.h>`
  `isalpha` use (`:13`).
- **FFI target:** NONE. These are byte arithmetic over `[]byte`/`str`. `valid_utf8` is a
  pure RFC-3629 scan; `%.17g` float formatting is a pure Grisu/Ryū-style routine (or the
  simpler already-present dtoa ported to Teko).
- **`teko::sys` consts:** none.
- **Reseed:** the COMPILER calls `str_from_utf8` / float rendering during diagnostics — so
  if the replacement changes the emitted `teko.c`'s CALLS (the builtin currently lowers to
  `tk_rt_str_from_utf8`), this is a two-legs concern: the C `tk_rt_*` symbol can only be
  DELETED once every caller (compiler + corpus) uses the Teko path. Treat float-rendering
  as leaf (no reseed) ONLY if the compiler does not depend on it; `str_from_utf8` IS
  compiler-facing → its swap is compiler-touching → reseed. Sequence: land Teko renderer,
  migrate callers, THEN delete the C fn (separate crumb).
- **Blocker:** none for the text scans. Float formatting is large but pure — no blocker,
  just volume; can be split into its own sub-crumb.

### Phase 2 — float utilities (`f64_bits` etc.)

- **C retired:** `tk_f64_bits` / `tk_f64_from_bits` (`teko_rt.c:5264-5265`), `tk_fdiv`
  (`:5263`).
- **FFI target:** these are a BIT-REINTERPRET (`memcpy(&b,&x,8)`), not a libc call.
- **`teko::sys` consts:** none.
- **HARD BLOCKER (flagged).** A pure-Teko bit-reinterpret between `f64` and `u64` needs
  EITHER (a) a compiler intrinsic (`bit_cast`/`transmute`), OR (b) a 1-line memcpy extern.
  Today `teko::f64_bits`/`teko::f64_from_bits` are COMPILER-VISIBLE builtins the checker's
  own comptime-fold depends on (`comptime_fold.tks:480,1643,805`). Removing the C backing
  without an intrinsic breaks the compiler's own constant folding. **Recommended resolution:
  keep `f64_bits`/`f64_from_bits` as a compiler intrinsic that lowers to an INLINE union/
  memcpy in emitted C (no `teko_rt.c` symbol) — this is a codegen intrinsic, not an FFI
  edge.** This is the single leaf that is NOT pure-`.tks` and NOT an FFI edge; it is a
  codegen-intrinsic sub-task. Flag to owner: "float-bits is a bitcast intrinsic, not FFI —
  confirm intrinsic-inlining over a memcpy extern." Compiler-touching (checker builtin +
  codegen) → reseed. `tk_fdiv` (div-by-zero-checked) is trivially pure Teko (guard + `/`).

### Phase 3 — char / UTF-8 (pure Teko, NO FFI)

- **C retired:** the ROUND-0 UTF-8 codepoint ops backed by `<ctype.h>` and `rt_valid_utf8`
  (shares Phase 1's scan).
- **FFI target:** NONE — codepoint decode/encode, category tests are pure `[]byte` logic.
- **`teko::sys` consts:** none.
- **Reseed:** leaf UNLESS a converted fn is one the compiler calls (e.g. `str_from_utf8`);
  same two-legs rule as Phase 1. Pure category helpers are leaf (no reseed).
- **Blocker:** none.

### Phase 4 — env + os/arch + time + random (the FIRST real FFI edges)

- **C retired:** `tk_rt_getenv` (`teko_rt.c:3687-3689`), setenv/unsetenv wrappers;
  `tk_rt_os`/`tk_rt_arch` (`:4393,4419`); `tk_rt_monotonic_ns` (`:5365`), `tk_wall_now_ns`
  (`:5281`); `tk_rt_secure_bytes` (getentropy/getrandom, `:4525-4541`).
- **FFI targets & shapes (all `from "c"`, all `extern unsafe fn`):**
  - `getenv(name: ptr<byte>): ptr<byte>` — `ptr<byte>` in, `ptr<byte>` out (NUL-terminated
    C string → Teko `str` via the existing `str_from_cstr` builtin, `teko_rt.c:352`). Safe
    wrapper NUL-terminates the name (rule 3).
  - `setenv(name: ptr<byte>, value: ptr<byte>, overwrite: i32): i32` /
    `unsetenv(name: ptr<byte>): i32`.
  - `clock_gettime(clk: i32, ts: ptr<Timespec>): i32` — the §3.1 example (needs the
    KEYSTONE `extern type Timespec`).
  - `getrandom(buf: ptr<byte>, n: u64, flags: u32): i64` (Linux) /
    `getentropy(buf: ptr<byte>, n: u64): i32` (macOS) — `#os`-guarded, chunked to 256 in the
    safe wrapper (mirrors `teko_rt.c:4525-4541`).
  - **os/arch:** NOT FFI. `tk_rt_os`/`tk_rt_arch` return COMPILE-TIME constant strings. In
    Teko they become `#os`/`#arch`-guarded `pub fn os(): str { "linux" }` etc. (one guarded
    body per target, prune keeps one) — pure Teko, zero libc.
- **`teko::sys` consts:** `CLOCK_MONOTONIC`, `CLOCK_REALTIME` (§2.2). getrandom's `flags` is
  `0` (no const). env: none.
- **Reseed:** these are stdlib leaves EXCEPT that `time` declares an `extern type = struct`
  → the KEYSTONE must be seeded first. The subsystem itself does not enter the compiler's
  own image, so its conversion is leaf (no reseed) ONCE the keystone is in the seed — BUT
  deleting the C symbols (`tk_rt_getenv`, `tk_rt_monotonic_ns`, `tk_rt_secure_bytes`,
  `tk_rt_os`, `tk_rt_arch`) from `teko_rt.c` is a SEPARATE crumb that runs only after every
  caller (compiler + corpus) is migrated (two-legs rule). `tk_rt_os`/`tk_rt_arch` are used
  by the COMPILER (target selection) — migrating those is compiler-adjacent; verify no
  compiler call path before deleting.
- **Blocker:** the ref→ptr crossing accepting an `extern type = struct` local as its `&`
  source (§3.1 note) is the one integration risk. If it does not, add a `teko::mem`
  address-of for extern structs — a small, additive codegen crumb, NOT a redesign.

---

## 5. Regression fixtures (inputs → expected native exit codes)

All fixtures compile to a native binary whose `main` returns an exit code; the harness
asserts the code. NEVER `teko test .`.

| # | fixture (under `examples/regressions/`) | body | expected exit |
|---|------|------|:---:|
| F1 | `extern_struct_layout` | declare `extern type Two = struct { a: i64; b: i64 }`; `var t = Two { a=7; b=35 }`; `return (t.a + t.b) to i32` | `42` |
| F2 | `extern_struct_field_reject` | `extern type Bad = struct { s: str }` | checker error `an extern type struct field must be a numeric prim, byte, ptr, uptr, or another extern struct` (compile-fail) |
| F3 | `extern_struct_method_reject` | `extern type Bad = struct { a: i64; fn m() {} }` | parse error `an extern struct declares only fields` (compile-fail) |
| F4 | `ffi_clock_monotonic` | call `monotonic_ns()` twice, assert `b >= a`; `return 0` else `1` | `0` |
| F5 | `ffi_clock_realtime_ordering` | wall `clock_gettime(CLOCK_REALTIME)`; assert `tv_sec > 1_700_000_000`; exit `0`/`1` | `0` |
| F6 | `ffi_getenv_roundtrip` | `setenv("TEKO_FX","42",1)`; parse `getenv("TEKO_FX")`; return it as i32 | `42` |
| F7 | `ffi_getrandom_fills` | fill 32 bytes; assert not-all-zero; exit `0`/`1` | `0` |
| F8 | `ffi_os_arch_string` | assert `teko::os()` ∈ {linux,macos,windows} and `teko::arch()` ∈ {x86_64,arm64}; exit `0`/`1` | `0` |
| F9 | `float_bits_roundtrip` | `f64_from_bits(f64_bits(3.14)) == 3.14`; also `f64_bits(0.0)==0`; exit `0`/`1` | `0` |
| F10 | `utf8_valid_reject` | pure-Teko `valid_utf8` accepts "áé", rejects an overlong `C0 80`; exit `0`/`1` | `0` |

F1 is the KEYSTONE proof (header-less layout: `sizeof == 16`, offsets 0/8 — a
Teko-struct-with-header would be 24). F4/F5 prove the ref→out-pointer crossing on a real
extern struct. F2/F3 pin the checker/parser gates.

---

## 6. Ritual points (where the FULL gate must pass)

1. **After the KEYSTONE (§1)** — compiler-touching. FULL gate: tri-generation fixpoint
   (gen1==gen2==gen3 byte-identical), `provenance_gate.sh`, reseed `bootstrap/teko.c`. F1-F3
   must pass on the reseeded compiler; F1 must LINK-fail (or mis-size) on the pre-seed
   compiler (the shape that failed before).
2. **After the float-bits intrinsic (Phase 2)** — compiler-touching (checker builtin +
   codegen intrinsic) → fixpoint + reseed. F9 green.
3. **After each C-symbol DELETION crumb** — the two-legs gate: BOTH the C leg (cc
   `bootstrap/teko.c` → gen0 builds the tree green) AND the native leg (emitted `teko.c`
   links with the shrunken `teko_rt.c`) must build. Run the full C-route chain under the
   memory ulimit, as the drain does.
4. **`teko::sys` + each pure-Teko leaf (Phases 1/3, os/arch)** — leaf ritual: build the
   native binary, run the phase's fixtures, confirm byte-identical self-image (no reseed).

---

## 7. Ordered crumb sequence + the FIRST implementable crumb

Dependency spine: KEYSTONE → `teko::sys` skeleton → leaf conversions → C-symbol deletions.

1. **C1 — KEYSTONE `extern type = struct`** (grammar `parse_decl.tks` + AST
   `ExternStructBody` + checker `extern_struct_field_ok`/`extern_type_ok` widen + codegen
   header-less typedef). Fixtures F1-F3. **compiler-touching → fixpoint + reseed.** ← the
   ritual keystone; everything else waits on its seed.
2. **C2 — `teko::sys` module skeleton** + the four time consts (§2.2). Leaf, no reseed.
3. **C3 — Phase 1 string/text renderers to Teko** (pure). Leaf for the non-compiler fns;
   the `str_from_utf8`/float-render swap that the compiler consumes is deferred to its own
   compiler-touching migration crumb.
4. **C4 — Phase 3 char/UTF-8 to Teko** (pure, shares the C3 scan). Leaf.
5. **C5 — Phase 2 float-bits INTRINSIC + `tk_fdiv` to Teko.** Compiler-touching (intrinsic)
   → reseed. Fixture F9. (Ordering: after C1's reseed to avoid double-reseed churn; may be
   batched with a later compiler-touching crumb.)
6. **C6 — Phase 4 env + os/arch (pure) + time + random (FFI).** Declares
   `extern type Timespec` (needs C1's seed). Fixtures F4-F8, F10. Leaf given the seed.
7. **C7 — C-symbol DELETION sweep** (per two-legs rule): remove `tk_rt_getenv`,
   `tk_rt_os`/`tk_rt_arch` (after verifying the compiler's target-selection no longer calls
   them), `tk_rt_monotonic_ns`, `tk_wall_now_ns`, `tk_rt_secure_bytes`, `tk_f64_bits`/
   `tk_f64_from_bits`, `tk_fdiv`, the float renderers, `rt_valid_utf8` — each only after ALL
   its callers are migrated. Full two-legs gate per deletion.

### FIRST implementable crumb — C1, precisely

In one branch, in order:
1. `src/parser/ast.tks` — add `pub type ExternStructBody = struct { fields: []Field }`
   (§1.2 snippet); add it to the `TypeBody()` union at `:875`.
2. `src/parser/parse_decl.tks:1572-1574` — branch on `=`/`struct` after `is_extern`;
   reuse `parse_fields`; reject methods/consts; emit `ExternStructBody` or the opaque
   `ExternBody`. Add the `ExternStructBody` arm to `attach_doc`'s TypeDecl passthrough and
   `macro_expand.tks` body-walk.
3. `src/checker/` — new arms mirroring every `ExternBody` site (`resolve.tks:1201`,
   `check_modules.tks:494`, `borrow.tks:110`, `collect.tks`); add `extern_struct_field_ok`
   + its enforcement pass; widen `extern_type_ok` (`typer.tks:8486`); add the
   `StructBody | ExternStructBody` field-projection arm.
4. `src/codegen/codegen.tks:11547` — add the header-less `ExternStructBody` arm to
   `emit_type_decl` (§1.4 snippet) and to the forward-decl pass at `:13317-13329`.
5. Fixtures F1-F3.
6. Fixpoint (gen1==gen2==gen3) + reseed `bootstrap/teko.c` + `provenance_gate.sh`.

The delta is INERT in the corpus (no `extern type = struct` in the tree yet), so the
fixpoint is reached at tc1 exactly as the 9-ops keystone was — clean reseed.

---

## 8. Explicitly DEFERRED (named, NOT designed here)

- **The ARENA (phase 5, the KEYSTONE of §16)** — `tk_region_*` / `tk_chunk_*` / `mmap` /
  `_Thread_local` (`teko_rt.c` region allocator). EVERYTHING allocates through it, so it is
  the true center of gravity; it gets its OWN design pass WITH the owner's memory-model input
  (the wrap-refcount / escape-analysis discussion). WHERE it sits: below every subsystem in
  this plan — `teko::mem::buf_ptr` / `region_new` / struct construction all bottom out in it.
  This pass deliberately touches NONE of it. See `arena-em-teko.md` /
  `arena-especificacao-unica-0.3.1.md` for the prior art the owner pass will build on.
- **Threads / sync** — pthread transitional (`-pthread` in the C ladder); `clone`/
  `CreateThread` via §17 per-target FFI. Deferred.
- **process / exec** — `fork`/`execvp`/`posix_spawn`, `win32_compat` spawn. Deferred.
- **test infra** — `setjmp`/`longjmp` harness. Deferred.
- **crash handling** — `signal`/`backtrace` (`teko_rt.c:644`), and `assert.c` wholesale.
  Deferred.

---

## 9. Risks + law tensions (with resolution)

| # | risk / tension | resolution |
|---|------|-----------|
| R1 | FFI-by-redeclaration bypasses the real C header — a wrong field type is silent UB | inherent to libc-direct FFI; ratified unsafe boundary (`c-types-and-marshalling-0.3.1.md §1`). Confine behind `extern unsafe fn`; pin layout by fixture F1/F4. No new law. |
| R2 | float-bits cannot be pure `.tks` and is a COMPILER dependency | make it a codegen intrinsic (inline union/memcpy), NOT an FFI edge; flag to owner for confirm. §4-Phase-2. |
| R3 | ref→ptr crossing may not accept an extern-struct local as `&`-source | probe first when C1 lands; fallback is an additive `teko::mem` address-of, not a redesign. §3.1 / §4-Phase-4. |
| R4 | deleting a C symbol still called by the compiler (`tk_rt_os`, `str_from_utf8`) | two-legs gate: migrate ALL callers, then delete in a separate crumb (C7). §6 ritual 3. |
| R5 | `#os`-divergent `teko::sys` consts could double-define a symbol | the §17 prune runs BEFORE the checker (`prune_cc`), keeping exactly one — no clash. §2. |
| R6 | double-reseed churn (C1 then C5 both compiler-touching) | order C5 after C1's reseed; optionally batch C5 with a later compiler-touching crumb. §7. |

No UNRESOLVED law tension → no HALT. The one owner-confirmation item (R2: float-bits as
intrinsic vs. memcpy-extern) is a recommendation, not a blocker — the plan proceeds on the
intrinsic reading, which passes all Laws (Teko-only: the intrinsic is codegen, no C symbol;
no `teko_rt.c` dependency remains).

---

## 10. Blocked-on-dependency status (design-ahead honesty)

NOTHING in THIS pass is blocked. §17 (`#os`/`#arch` + the prune) is landed
(`src/build/prune.tks`), `from "c"` externs are landed (`teko_rt.tks:697`), and the
`extern type` opaque grammar is landed — so the KEYSTONE is a pure additive extension of
existing, working machinery. The DEFERRED arena is blocked on the owner's memory-model pass
(named, not designed). Everything designed above compiles against machinery that exists on
HEAD `5f24ad9f`.

---

# 11. REFRESH 2026-08-16 (post-C1 surface) — C3–C6 against the REAL FFI surface

> **Why this section exists.** C1 (`extern type = struct`, landed `c7ac134b`, drained
> `03f2766d`) and C2 (`teko::sys`, landed `1cb6e5f7`) revealed that §3/§3.1/§4 above name
> a FFI surface that DOES NOT EXIST. This section supersedes those parts for crumbs C3–C6.
> Verified on `fix/retirement` HEAD `2b720bfe`. The historical text above is kept for
> provenance; where it conflicts with §11, §11 wins.

## 11.0 The three stale-surface corrections (verified against C1's landed code)

| Stale (§3/§3.1/§4) | REAL surface (verified) | Evidence |
|---|---|---|
| `extern unsafe fn NAME(...)` | plain `extern fn NAME(p): ret = "symbol" from "c"` — `unsafe` retired (§6) | fixture `examples/regressions/extern_type_struct/src/ts/ts.tks:30` |
| `ptr<byte>` / `ptr<Timespec>` | `ptr` is OPAQUE, takes NO type arg; an out-pointer to an extern struct is a `ref T` PARAMETER | `checker::scope.tks:646` (`ptr` → `Ptr{inner=null}`); `ts.tks:30` (`ts: ref Timespec`) |
| call-site `ref ts` operator | `ref` is BINDING-only; the call-site argument is a plain local | `ts.tks:42` (`_ = clock_gettime(0, ts)`) |
| `: ref T` virtual return | NO return-by-reference; every return allocates in the caller's arena | (no ref-return anywhere; arena is caller-owned) |

The `ref T` parameter rides the EXISTING auto-`&` crossing at `codegen.tks:5194-5197`: a
`Ref`-kind parameter (`pn == "Ref"`) with a non-reference argument emits `&arg`. A
header-less `extern type = struct` local is the `&`-able source (the C1 keystone), so
`clock_gettime(0, ts)` emits `clock_gettime(0, &ts)` with `ts` a `tk_t_…Timespec` — layout
byte-identical to libc's `struct timespec`. This is proven GREEN by the C1 fixture.

## 11.1 The str↔cstr FFI marshalling recipe (the load-bearing pattern)

**The recipe is ALREADY FULLY WIRED — no new compiler work is needed for str↔cstr.** Both
directions are `teko::mem` builtins that emit INLINE (GNU statement-expressions), needing
NO `teko_rt.c` symbol:

**(a) Teko `str` → C `char*` argument — `teko::mem::as_cstr(s: str): ptr`.**
- Checker signature: `(str): ptr` (opaque `Ptr{inner=null}` return) — `scope.tks:686`,
  registered at `scope.tks:1196`.
- Codegen: `emit_as_cstr` (`codegen.tks:4471`). It bump-allocates `s.len + 1` octets **into
  the ENCLOSING region** (`tk_region_alloc`, NOT `malloc`), copies the bytes byte-by-byte,
  and writes `0` at index `s.len`. Returns the buffer address as an opaque `ptr` (a
  `uint8_t *` cast). The NUL-terminated buffer lives in the ARENA; its address IS the `ptr`
  — there is no `&`, no separate local. Bulk-freed with the enclosing region.
- Pass that `ptr` directly at the extern fn's `ptr` parameter → lowers to `void *`; the
  `char *` mismatch is absorbed by the emitted `#pragma GCC diagnostic ignored
  "-Wincompatible-pointer-types"` (`codegen.tks:13337`).

**(b) C `char*` return → Teko `str` — `teko::mem::str_from_c(p: ptr, max: u64): str`.**
- Checker signature: `(ptr, u64): str` — `scope.tks:696`, registered at `scope.tks:1197`.
- Codegen: `emit_str_from_c` (`codegen.tks:4496`). A BOUNDED inbound scan: reads at most
  `max` octets from `p`, stops at the first `0`, copies into a fresh `str`. TOTAL — `p`
  null OR `max == 0` both yield the empty `str` with no read; no `0` in `[0, max)` yields
  exactly `max` octets (truncation, not a panic). Inline stmt-expr, no runtime symbol.
- **NOTE — the brief said "via `str_from_cstr`".** `str_from_cstr` (the UNBOUNDED
  `tk_str_from_cstr`, `teko_rt.c:353`) is NOT a checker-exposed Teko builtin (only `as_cstr`
  and `str_from_c` are registered — `scope.tks:1196-1197`). Use the bounded **`str_from_c`**;
  it is strictly better (no unbounded `strlen` on foreign memory) AND needs no C symbol. The
  C `tk_str_from_cstr` symbol only backs the OLD `tk_rt_getenv` path and is retired in C7.

**Adjacent buffer builtins (for `getrandom`, already wired):** `teko::mem::buf_ptr(len: u64):
ptr` (allocate `len` arena octets, `scope.tks:672` / `emit_buf_ptr`) and
`teko::mem::bytes_from_ptr(p: ptr, n: u64): []byte` (lift `n` foreign octets into a fresh
`[]byte`, `codegen.tks:5017` → `tk_bytes_from_ptr`).

### 11.1.1 Worked `getenv` example (REAL surface — copy verbatim)

```teko
/**
 * MAX_ENV — the inbound-scan bound for an environment value. A value longer than this is
 * truncated (never a panic); 128 KiB comfortably exceeds any realistic env var.
 *
 * @since §16 (C6)
 */
const MAX_ENV: u64 = 131072 to u64

/**
 * c_getenv — the raw libc `getenv(3)` (libc-direct). Takes a NUL-terminated C string by
 * opaque `ptr` and returns the value as a NUL-terminated C string by opaque `ptr`, or the
 * null pointer when the name is unset. FFI boundary (§16): the returned pointer borrows
 * libc's `environ` storage and is only marshalled — never stored — on the Teko side.
 *
 * @param name  a NUL-terminated C string (produced by `teko::mem::as_cstr`)
 * @return      the value as a C `char*` by opaque `ptr`, or the null pointer if unset
 * @since §16 (C6)
 */
extern fn c_getenv(name: ptr): ptr = "getenv" from "c"

/**
 * get — read environment variable `name`, returning its value as an owned Teko `str`. The
 * name crosses the boundary via `teko::mem::as_cstr` (a NUL-terminated copy bump-allocated
 * in the enclosing region); the returned C string is copied back via `teko::mem::str_from_c`
 * (a bounded inbound scan). TOTAL: an unset OR empty variable both yield the empty `str` — no
 * panic, no foreign pointer escapes into Teko storage. (A future `str | null` form that
 * distinguishes unset from empty waits on a `teko::mem::ptr_is_null` predicate — §11.2 C5.)
 *
 * @param name  the variable name
 * @return      the value, or the empty `str` when unset/empty
 * @since §16 (C6)
 */
pub fn get(name: str): str {
    teko::mem::str_from_c(c_getenv(teko::mem::as_cstr(name)), MAX_ENV)
}

/**
 * c_setenv — the raw libc `setenv(3)` (libc-direct). Both strings cross as NUL-terminated
 * C strings by opaque `ptr`; `overwrite` is a plain `i32` flag (1 = replace, 0 = keep).
 *
 * @param name       a NUL-terminated C string (the variable name)
 * @param value      a NUL-terminated C string (the new value)
 * @param overwrite  1 to overwrite an existing value, 0 to keep it
 * @return           0 on success, -1 on error (errno set — unread here)
 * @since §16 (C6)
 */
extern fn c_setenv(name: ptr, value: ptr, overwrite: i32): i32 = "setenv" from "c"

/**
 * set — set environment variable `name` to `value`, overwriting any existing value. Both
 * strings are marshalled with `teko::mem::as_cstr`.
 *
 * @param name   the variable name
 * @param value  the new value
 * @return       0 on success, -1 on error
 * @since §16 (C6)
 */
pub fn set(name: str, value: str): i32 {
    c_setenv(teko::mem::as_cstr(name), teko::mem::as_cstr(value), 1 to i32)
}
```

That is the ENTIRE recipe: `as_cstr` outbound, `str_from_c` inbound, opaque `ptr` transport,
`extern fn … from "c"`. It gates `getenv`/`setenv`/`unsetenv` and, generalised, most of libc.

## 11.2 C5 — the float-bits CODEGEN INTRINSIC ladder

Owner ruling (LAW §11.2, 2026-08-16): `f64_bits`/`f64_from_bits` become a **codegen
intrinsic** — lower DIRECT to an inline union/`memcpy` in the C backend, **never** via an FP
register (x87 signalling-NaN canonicalisation hazard). Today they are checker-recognised
`teko::` builtins lowered by NAME-SUBSTITUTION to `tk_f64_bits`/`tk_f64_from_bits`
(`codegen.tks:5047-5048`); the checker already types them, so no `.tks` façade FILE is needed
— the "thin façade" is the already-recognised builtin name.

**The change (C5, one crumb):**
1. `codegen.tks` preamble (after `codegen.tks:13333`, right after the `#include`s) — emit
   three `static inline` helpers with names DISTINCT from the `teko_rt.c` symbols, doing the
   pun IN MEMORY (satisfies the x87 hazard rule):
   ```c
   static inline uint64_t tk_f64bits_i(double x){ uint64_t b; memcpy(&b,&x,sizeof b); return b; }
   static inline double   tk_f64frombits_i(uint64_t u){ double x; memcpy(&x,&u,sizeof x); return x; }
   ```
   (`string.h` for `memcpy` — the `spawn_sites` guard at `codegen.tks:13331` already shows
   the include pattern; make `memcpy` unconditional or add a small always-on include.)
2. `codegen.tks:5047-5048` — repoint the two dispatch arms from `tk_f64_bits`/
   `tk_f64_from_bits` to `tk_f64bits_i`/`tk_f64frombits_i`.
3. `tk_fdiv` (`codegen.tks:5046`, `teko_rt.c:5263`) — the owner called it "trivially pure
   Teko (guard + `/`)". Two admissible resolutions; **recommended = the inline twin** for a
   single uniform ladder: emit `static inline double tk_fdiv_i(double a,double b){ if(b==0.0)
   tk_panic_div0(); return a/b; }` in the same preamble and repoint arm 5046. (`tk_panic_div0`
   is NOT retired, so it stays.) The alternative — a real `.tks` `fdiv` fn in `teko::float` +
   deleting dispatch arm 5046 — also works but re-opens the builtin-shadow question, so the
   inline twin is cleaner.
4. **(recommended batch-in) `teko::mem::ptr_is_null(p: ptr): bool`** — a tiny new checker
   builtin (`scope.tks`, beside `as_cstr`) + codegen arm emitting `((p) == NULL)`. It costs
   one dispatch line, rides C5's reseed for free, and unblocks the richer `str | null`
   `getenv`/`get` in C6. Optional but recommended here so C6 stays a pure leaf.

**The ladder (one step — do NOT expect tc1==tc2).** `f64_bits`/`f64_from_bits` are used by
the COMPILER itself (`codegen.tks:313`, `math.tks:94-178`, `comptime_fold.tks:480,805,1643`).
- **gen0** = the current seed `bootstrap/teko.c` (old codegen). It compiles the C5 tree, but
  its OWN codegen still lowers `teko::f64_bits` → `tk_f64_bits`. So **tc1's emitted C still
  calls `tk_f64_bits`** → tc1 LINKS against `teko_rt.c`'s `tk_f64_bits`. **The symbol MUST
  STAY in `teko_rt.c` for this crumb.**
- **tc1** (the new-codegen binary) recompiles the tree: NOW it lowers `teko::f64_bits` →
  `tk_f64bits_i` (inline preamble). **tc2's emitted C has no `tk_f64_bits` call.** tc2 == tc3
  (stable inline). **One-step ladder: tc1 ≠ tc2 == tc3.** Reseed lands the stable tc2.
- **`tk_f64_bits`/`tk_f64_from_bits`/`tk_fdiv` are DELETED from `teko_rt.c` only in a LATER
  crumb (C7)**, after the reseed, once no leg references them. Deletion is ALWAYS a separate
  two-legs crumb.

Files changed by C5: `src/codegen/codegen.tks` (preamble + arms 5046-5048; optional
`ptr_is_null` arm), `src/checker/scope.tks` (optional `ptr_is_null` signature). `teko_rt.c`
UNCHANGED in C5 (symbols retained). Compiler-touching → **fixpoint (one-step ladder) +
reseed**. Fixture F9 (`float_bits_roundtrip`, §5) green on the reseeded compiler.

## 11.3 Leaf-vs-reseed verdict — C3, C4, C5, C6

Two-legs rule (invariant): **deleting any hand-written C symbol is ALWAYS its own separate
post-migration crumb (C7)**, gated on BOTH legs (cc `bootstrap/teko.c` green AND emitted
`teko.c` links the shrunken `teko_rt.c`). Never fold a deletion into the migration crumb.

| Crumb | Scope | Verdict | Why |
|---|---|---|---|
| **C3** str/text float renderers + `valid_utf8` → Teko | The float renderers (`ftoa`/`f64_g17`/`fmt_*`) and `valid_utf8`/`str_from_utf8` are **compiler-consumed** (`codegen.tks:314,5018,5112-5126`). Repointing them to a Teko renderer bakes the renderer into the compiler image. | **RESEED** (compiler-touching, one-step ladder like C5). | A pure ADD of a Teko renderer module (compiler still calls the C `tk_ftoa`) would be leaf, but that delivers nothing — the useful C3 deliverable repoints the compiler's own float formatting. C-symbol deletion (`tk_ftoa`/`tk_f64_g17`/`rt_valid_utf8`) deferred to C7. |
| **C4** char/UTF-8 codepoint + category helpers → Teko | Pure `[]byte`/codepoint logic added as stdlib. The compiler's own `is_alpha`/`is_digit` stay on their byte-typed builtins (guarded `call_ns.len==0`, `codegen.tks:5102`) — NOT repointed. | **LEAF** (add-only; compiler self-image byte-identical, like C2). | New char ops are corpus-facing; the compiler does not consume them, so they dead-code-eliminate from the compiler binary. Any piece that IS compiler-consumed (`str_from_utf8`) rides C3's reseed, not C4. |
| **C5** float-bits INTRINSIC + `fdiv` (+ opt. `ptr_is_null`) | §11.2. Codegen intrinsic + repointed builtin arms — compiler-consumed. | **RESEED** (compiler-touching, one-step ladder). | `f64_bits` is used by the compiler's own const-fold/codegen. Symbol deletion deferred to C7. |
| **C6** env + os/arch + time + random FFI stdlib | ADDS `teko::env` (getenv/setenv/unsetenv), `teko::time` (clock_gettime via `extern type Timespec`), `teko::rand` (getrandom/getentropy), and pure `#os`/`#arch`-guarded `os()`/`arch()` fns. The compiler KEEPS its existing C path (`tk_rt_getenv`/`tk_rt_os`/`tk_rt_monotonic_ns`/… via its own builtin dispatch, UNCHANGED). | **LEAF** — UNBLOCKED (C1 keystone + C2 consts landed). | Same shape as C2: the new modules are not consumed by the compiler, so they dead-code-eliminate → emitted `teko.c` byte-identical → no reseed. **The migration** (repoint the compiler's own os/arch/getenv builtins to the Teko modules + remove the codegen builtin arms) **and the C-symbol deletion are C7** (compiler-touching + two-legs). os()/arch() as pure Teko fns MUST live behind the C7 migration to repoint the `os`/`arch` builtin arms (`codegen.tks:5128-5129`); in C6 they are new modules proven by a regression mirror, exactly like C2's `sys::` mirror. |

## 11.4 The ordered next crumbs (implementer-ready) + the recommended FIRST

Landed: **C1 ✅, C2 ✅.** Remaining foundation: **C3, C5, C6, then C7 (deletions).** These
three are mutually independent (no ordering constraint between them); C7 depends on all.

### ⭐ RECOMMENDED FIRST — **C6a: `teko::env` (getenv/setenv/unsetenv)** — LEAF, zero reseed

**Why first:** (1) it is a pure LEAF (no reseed ceremony, no ladder — the safest immediate
dispatch); (2) it is UNBLOCKED (C1+C2 landed); (3) it exercises the **load-bearing str↔cstr
recipe (§11.1) end-to-end** on a real libc edge — validating the pattern that gates most of
libc, so it de-risks all of C6/fs/net downstream for the least risk; (4) it is small and
self-contained (one module, one fixture).

- **Files:** new `src/env/env.tks` (namespace `teko::env`). NO compiler files touched. NO
  `teko_rt.c` change (the old `tk_rt_getenv` stays until C7).
- **Signatures (REAL surface, §11.1.1 verbatim):**
  `const MAX_ENV: u64` · `extern fn c_getenv(name: ptr): ptr = "getenv" from "c"` ·
  `pub fn get(name: str): str` · `extern fn c_setenv(name: ptr, value: ptr, overwrite: i32):
  i32 = "setenv" from "c"` · `pub fn set(name: str, value: str): i32` ·
  `extern fn c_unsetenv(name: ptr): i32 = "unsetenv" from "c"` · `pub fn unset(name: str):
  i32`.
- **Regression fixture** (`examples/regressions/ffi_env_roundtrip/`, with a local `src/env/`
  mirror addressed bare as `env::` — the C2 precedent): `main.tks` does
  `env::set("TEKO_FX", "42")`; then `var v = env::get("TEKO_FX")`; parse `v` to i64; `if v ==
  42 { exit(0) } exit(1)`. **Expected native exit: 0.**
- **Leaf-or-reseed:** LEAF. Validate COMPILE-only (`--no-verify --release`, `TEKO_BACKEND=c`,
  `ulimit -v 6291456`); confirm the emitted `teko.c` for the tree+C6a is byte-identical to the
  current seed (the C2 leaf proof). NO reseed.
- **Ritual point:** none (leaf). The coordinator's leaf gate: build the fixture native binary,
  run it (exit 0), confirm byte-identical compiler self-image.
- **Native leg:** `extern fn … from "c"` already lowers on the C leg; native lowering honest-
  stops per Doc-2's terminal-native phase (not this crumb).

### SECOND — **C6b: `teko::time` (clock_gettime via `extern type Timespec`)** — LEAF

The C1 fixture `extern_type_struct` ALREADY proves this exact shape. C6b promotes it to a
stdlib module: `extern type Timespec = struct { sec: i64; nsec: i64 }`;
`extern fn clock_gettime(clk: i32, ts: ref Timespec): i32 = "clock_gettime" from "c"`;
`pub fn monotonic_ns(): i64` / `pub fn wall_ns(): i64` using `teko::sys::CLOCK_MONOTONIC`/
`CLOCK_REALTIME` (C2). Call-site passes the plain local `ts` (NOT `ref ts`). Fixtures F4/F5
(§5). LEAF (adds a module; compiler keeps `tk_rt_monotonic_ns`/`tk_wall_now_ns` until C7).

### THIRD — **C5: float-bits intrinsic + `fdiv` (+ `ptr_is_null`)** — RESEED

§11.2. The one compiler-touching foundation crumb; do it when a reseed is being spent anyway.
Batching `ptr_is_null` here lets a LATER C6 revision give `env::get` the richer `str | null`
return. Fixture F9. One-step ladder + reseed `bootstrap/teko.c` + `provenance_gate.sh`.

(C6c `teko::rand` via `buf_ptr`/`bytes_from_ptr`, C6d pure `os()`/`arch()`, C3 renderers, and
C7 the deletion sweep follow; C7 is gated on C3/C5/C6 migrations landing.)

## 11.5 Risks / open items (post-C1)

| # | item | resolution |
|---|---|---|
| R7 | `env::get` cannot distinguish "unset" from "empty" without a ptr-null test | v1 returns `str` (empty = both), TOTAL, needs nothing — LEAF-safe. The `str \| null` form waits on `teko::mem::ptr_is_null`, recommended to batch into C5 (§11.2 step 4). No HALT. |
| R8 | C5 preamble helper name must NOT collide with the retained `teko_rt.c` symbol | use DISTINCT names (`tk_f64bits_i`/`tk_f64frombits_i`/`tk_fdiv_i`); the `teko_rt.c` symbol is deleted only in C7. §11.2. |
| R9 | brief named `str_from_cstr` for inbound; only `str_from_c` (bounded) is checker-exposed | use `str_from_c(p, max)` — strictly better (no unbounded foreign `strlen`) and no C symbol. §11.1(b). |

No unresolved law tension → no HALT. All C3–C6 designs compile against machinery that exists
on HEAD `2b720bfe`; C6a/C6b are LEAF and dispatchable immediately.
