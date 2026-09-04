// teko_type.mc -- the one primitive the mc core does not already give the
// teko surface: `bool`. Every other primitive teko names (u8/u16/u32/u64/
// i64/uptr/void) is the SAME word for the SAME core type, so D213 (dono
// 2026-09-04) says not to re-teach it: `type_alias` only needs to add the
// delta, not restate the base. Comparisons already produce 0/1 (core-
// language.md), so `bool` costs one alias and nothing else.

void tk_types_init() {
    type_alias("bool", TY_U8);
}
