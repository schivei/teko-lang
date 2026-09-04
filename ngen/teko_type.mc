// teko_type.mc -- the primitives the mc core does not already give the teko
// surface. Every one of them is an alias over a core type id (`type_alias`,
// M12): the word is new, the representation is not (D213, dono 2026-09-04 --
// reuse the mc core, teach only the delta). `u8/u16/u32/u64/i64/uptr/void`
// need no entry here at all, they are already the same word for the same
// type; `isize`/`usize` collapse to `i64`/`u64` for the same reason
// (DECISION_LOG D131: 64-bit today, implicit coercion is free because alias
// IS identity, not a distinct type needing a conversion). `char` is the
// scalar `u32` the mc host uses (not teko-classic's fat char, D213/D205:
// functionality over syntactic fidelity), and `'a'`/`'\n'` already fold to
// an integer literal usable at that width (core-language.md), so no new
// token is needed for it. `byte` is `u8`. `ptr` collapses to the same
// opaque, single pointer type mc already names `uptr` -- the ISA distinction
// teko-classic drew between them does not exist on this core, so both words
// resolve to one representation.
void tk_types_init() {
    type_alias("bool",  TY_U8);
    type_alias("char",  TY_U32);
    type_alias("byte",  TY_U8);
    type_alias("isize", TY_I64);
    type_alias("usize", TY_U64);
    type_alias("ptr",   TY_UPTR);
    type_alias("str",   TY_UPTR);
}
