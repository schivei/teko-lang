# `bulk` on the native backend — the complete verdict, all 203 fixtures

MEASURED, not estimated. Every fixture was scaffolded into its OWN project and compiled by gen1
(`cc(bootstrap/teko.c)` -> the tip) with `TEKO_BACKEND=native`; the ones that built were then RUN and
their exit compared against the `Then exit = N` their scenario declares in `bulk.tkr`. 203 isolated
builds, 44 seconds — against the 113.6 s the single `bulk` build spends to return ONE verdict.

## Why `bulk.tkr` left `teko.tkp`'s regression list

Not because its fixtures are bad, and not as a skip: THE PROJECT CANNOT BUILD ON THE NATIVE BACKEND
IN ANY CONFIGURATION. Pruned down to only the 76 that pass, it still fails — and with no `[in ns::fn]`
marker, because the stop is not in a fixture at all. It is in the dispatcher `main.tks` is:

    let regr_entry = match teko::env::var("TEKO_REGR_ENTRY") { str as v => v; error => "" }

a `match` over `str | error` whose result is `str`. Two stacked gaps: as a VALUE it is
`fat-pointer receiver \`match-expression\` not yet lowered`; rewritten as a STATEMENT it becomes
`unknown variant case \`str\` (internal)`. `bulk` is the only regression project using this
env-dispatch shape — `own_native`, which is green on native, calls each function directly and encodes
the failing one in its exit code. The C emitter used to lower the dispatcher; the channel died with it.

So this file IS the coverage, until the fixtures are re-homed as isolated projects. Nothing here is
silently dropped: every one of the 203 is listed with the verdict it produces today.

## Counts

| verdict | n |
|---|---:|
| KNOWN-STOP | 84 |
| PASS | 76 |
| KNOWN-WRONG | 21 |
| BLOCKED | 20 |
| NO-EXPECT | 2 |

## The gaps, by weight — this is the `.32` work list, one wagon per row

| n | what the native backend cannot lower |
|---:|---|
| 17 | ``str` has no single PrimKind, asked by the comparison chain (N2)` |
| 8 | `integer operator not yet lowered (N2)` |
| 7 | `a `null` in this position needs the null-union wrapper the placement does not declare a type for (N2)` |
| 6 | `reference deref-assignment not yet lowered (N2)` |
| 5 | `fat-pointer interface-dispatch result not yet lowered (N2)` |
| 4 | `field/index access on a non-struct receiver not yet lowered (N2)` |
| 3 | ``null` match pattern not yet lowered (N2)` |
| 3 | `a push whose element is an AGGREGATE (`struct`/`class`/variant/optional/closure) needs slice elements held BY VALUE — this backend's aggregate value is the address of a per-INSTRUCTION frame slot, so a push in a loop would store the same address every iteration (N2)` |
| 3 | `fat-pointer receiver `index` not yet lowered (N2)` |
| 2 | `adopt not yet lowered (N2)` |
| 2 | `builtin `bytes_of_str` not yet lowered (N2)` |
| 2 | `fat-pointer receiver `string interpolation` not yet lowered (N2)` |
| 1 | ``char` has no single PrimKind, asked by the cast source (N2)` |
| 1 | ``i64 | null` has no single PrimKind, asked by the cast target (N2)` |
| 1 | ``s` is not a fat-pointer local (internal)` |
| 1 | ``str` has no single PrimKind, asked by the in-expression lhs (N2)` |
| 1 | ``teko::list::with_cap` has no native lowering yet (N2)` |
| 1 | ``v` is not a fat-pointer local (internal)` |
| 1 | `borrow not yet lowered (N2)` |
| 1 | `builtin `len_chars` not yet lowered (N2)` |
| 1 | `builtin `str_from_utf8` not yet lowered (N2)` |
| 1 | `compound assignment operator not yet lowered (N2)` |
| 1 | `dynamic dispatch through the polymorphic base class `q057_constrained_or_dispatch::q057_constrained_or_dispatch::OnlyFirst` is not yet lowered — a class instance carries no vtable (N2)` |
| 1 | `dynamic dispatch through the polymorphic base class `q155_struct_vs_class_constraint::q155_struct_vs_class_constraint::ClassValue` is not yet lowered — a class instance carries no vtable (N2)` |
| 1 | `dynamic dispatch through the polymorphic base class `q158_trait_constraint::q158_trait_constraint::Box2_mcni` is not yet lowered — a class instance carries no vtable (N2)` |
| 1 | `fat-pointer receiver `match-expression` not yet lowered (N2)` |
| 1 | `field match pattern over a non-variant subject not yet lowered (N2)` |
| 1 | `if-expression arm's tail statement does not yield a value (internal)` |
| 1 | `non-enum named-type (struct/class/flags) arithmetic operand not yet lowered (N2)` |
| 1 | `static interpolation format spec not yet lowered (N2)` |
| 1 | `unbound local `q118_member_const_inherit_shadow::Base::K` (internal)` |
| 1 | `unknown variant case `str` (internal)` |
| 1 | `unknown variant case `u32` (internal)` |
| 1 | `value's type is not a member of its declared variant (internal)` |

## Every fixture

| fixture | verdict | detail |
|---|---|---|
| `p_di_same_name_cross_ns` | BLOCKED | teko: .: cc failed to link the own-backend object |
| `p_iface_value_name_collision_factory` | BLOCKED | teko: .: src/stream/stream.tks:81:17: unknown function: spread |
| `q018_width_float_widen` | BLOCKED | teko: .: isel x86-64: B1-fp — a float literal materializes into an XMM register via the SSE move family, the float honest-stop (0.3.1) |
| `q023_arena_manual_ok` | BLOCKED | teko: .: cc failed to link the own-backend object |
| `q026_buf_ptr_memset_roundtrip` | BLOCKED | teko: .: cc failed to link the own-backend object |
| `q065_di_lazy` | BLOCKED | teko: .: cc failed to link the own-backend object |
| `q066_di_recursive` | BLOCKED | teko: .: cc failed to link the own-backend object |
| `q067_di_scoped` | BLOCKED | teko: .: cc failed to link the own-backend object |
| `q068_di_singleton` | BLOCKED | teko: .: cc failed to link the own-backend object |
| `q069_di_transient` | BLOCKED | teko: .: cc failed to link the own-backend object |
| `q077_flags_64_members` | BLOCKED | teko: stack trace: |
| `q116_mem_free` | BLOCKED | teko: .: cc failed to link the own-backend object |
| `q121_must_free_ok` | BLOCKED | teko: .: cc failed to link the own-backend object |
| `q161_trait_derive_hash` | BLOCKED | teko: .: cc failed to link the own-backend object |
| `q163_trait_derive_ord` | BLOCKED | teko: .: cc failed to link the own-backend object |
| `q170_unsafe_rawbuf_roundtrip` | BLOCKED | teko: .: cc failed to link the own-backend object |
| `q189_tilde_concat` | BLOCKED | teko: .: cc failed to link the own-backend object |
| `q195_c_string_embedded_nul_truncat` | BLOCKED | teko: .: cc failed to link the own-backend object |
| `q196_mem_region_buf_roundtrip` | BLOCKED | teko: .: cc failed to link the own-backend object |
| `q198_c_null_union_extern_return` | BLOCKED | teko: .: cc failed to link the own-backend object |
| `q006_writeback_mutates_a_null_union_field_v` | KNOWN-STOP | a `null` in this position needs the null-union wrapper the placement does not declare a type for (N2) |
| `q008_self_referential_linked_list_walk_over` | KNOWN-STOP | a `null` in this position needs the null-union wrapper the placement does not declare a type for (N2) |
| `q009_small_inline_tag_null_union_field_fold` | KNOWN-STOP | a `null` in this position needs the null-union wrapper the placement does not declare a type for (N2) |
| `q010_large_box_in_arena_null_union_field_fo` | KNOWN-STOP | a `null` in this position needs the null-union wrapper the placement does not declare a type for (N2) |
| `q013_recursion_through_a_variant_payload_s` | KNOWN-STOP | a `null` in this position needs the null-union wrapper the placement does not declare a type for (N2) |
| `q015_comptime_fold_interp_hex` | KNOWN-STOP | `str` has no single PrimKind, asked by the comparison chain (N2) |
| `q021_adopt_cyclic_bulk_drop` | KNOWN-STOP | adopt not yet lowered (N2) |
| `q024_array_literal_narrow_binding` | KNOWN-STOP | compound assignment operator not yet lowered (N2) |
| `q027_casting_native_roundtrip` | KNOWN-STOP | unknown variant case `u32` (internal) |
| `q030_chars` | KNOWN-STOP | `char` has no single PrimKind, asked by the cast source (N2) |
| `q031_chars_iter` | KNOWN-STOP | builtin `len_chars` not yet lowered (N2) |
| `q032_class_base_binding` | KNOWN-STOP | fat-pointer interface-dispatch result not yet lowered (N2) |
| `q033_class_basics` | KNOWN-STOP | `str` has no single PrimKind, asked by the comparison chain (N2) |
| `q036_class_fallible_factory` | KNOWN-STOP | `str` has no single PrimKind, asked by the comparison chain (N2) |
| `q037_class_identity` | KNOWN-STOP | `str` has no single PrimKind, asked by the comparison chain (N2) |
| `q038_class_inheritance` | KNOWN-STOP | fat-pointer interface-dispatch result not yet lowered (N2) |
| `q039_class_invariant_construction` | KNOWN-STOP | `str` has no single PrimKind, asked by the comparison chain (N2) |
| `q041_class_slices` | KNOWN-STOP | fat-pointer receiver `index` not yet lowered (N2) |
| `q042_class_upcast` | KNOWN-STOP | fat-pointer interface-dispatch result not yet lowered (N2) |
| `q043_class_visibility` | KNOWN-STOP | `str` has no single PrimKind, asked by the comparison chain (N2) |
| `q044_closure_shadows_fn` | KNOWN-STOP | `s` is not a fat-pointer local (internal) |
| `q045_collections_list` | KNOWN-STOP | `str` has no single PrimKind, asked by the comparison chain (N2) |
| `q046_collections_map` | KNOWN-STOP | `str` has no single PrimKind, asked by the comparison chain (N2) |
| `q047_compress_roundtrip` | KNOWN-STOP | a push whose element is an AGGREGATE (`struct`/`class`/variant/optional/closure) needs slice elements held BY VALUE — this backend's aggregate value is the address of a per-INSTRUCTION frame slot, so a push in a loop would store the same address every iteration (N2) |
| `q048_comptime_fold_format_oracle` | KNOWN-STOP | `str` has no single PrimKind, asked by the comparison chain (N2) |
| `q057_constrained_or_dispatch` | KNOWN-STOP | dynamic dispatch through the polymorphic base class `q057_constrained_or_dispatch::q057_constrained_or_dispatch::OnlyFirst` is not yet lowered — a class instance carries no vtable (N2) |
| `q060_crypto_hash_kat` | KNOWN-STOP | a push whose element is an AGGREGATE (`struct`/`class`/variant/optional/closure) needs slice elements held BY VALUE — this backend's aggregate value is the address of a per-INSTRUCTION frame slot, so a push in a loop would store the same address every iteration (N2) |
| `q061_crypto_rand_secure_bytes` | KNOWN-STOP | integer operator not yet lowered (N2) |
| `q062_datetime_format_spec` | KNOWN-STOP | `str` has no single PrimKind, asked by the comparison chain (N2) |
| `q063_default_args_named_call` | KNOWN-STOP | `str` has no single PrimKind, asked by the comparison chain (N2) |
| `q064_defer_cross_block_return` | KNOWN-STOP | if-expression arm's tail statement does not yield a value (internal) |
| `q073_encodings_roundtrip` | KNOWN-STOP | a push whose element is an AGGREGATE (`struct`/`class`/variant/optional/closure) needs slice elements held BY VALUE — this backend's aggregate value is the address of a per-INSTRUCTION frame slot, so a push in a loop would store the same address every iteration (N2) |
| `q078_flags_methods` | KNOWN-STOP | non-enum named-type (struct/class/flags) arithmetic operand not yet lowered (N2) |
| `q079_format_precision_large` | KNOWN-STOP | fat-pointer receiver `string interpolation` not yet lowered (N2) |
| `q080_format_spec_families` | KNOWN-STOP | static interpolation format spec not yet lowered (N2) |
| `q082_generic_constraints` | KNOWN-STOP | `str` has no single PrimKind, asked by the comparison chain (N2) |
| `q093_inline_attr_parse` | KNOWN-STOP | a `null` in this position needs the null-union wrapper the placement does not declare a type for (N2) |
| `q094_interface_conformance` | KNOWN-STOP | `str` has no single PrimKind, asked by the comparison chain (N2) |
| `q095_interface_dispatch` | KNOWN-STOP | fat-pointer interface-dispatch result not yet lowered (N2) |
| `q096_intern_table_roundtrip` | KNOWN-STOP | `str` has no single PrimKind, asked by the comparison chain (N2) |
| `q097_io_file_copy` | KNOWN-STOP | builtin `bytes_of_str` not yet lowered (N2) |
| `q098_io_stream_pipeline` | KNOWN-STOP | builtin `bytes_of_str` not yet lowered (N2) |
| `q099_iter_protocol` | KNOWN-STOP | field/index access on a non-struct receiver not yet lowered (N2) |
| `q100_json_roundtrip` | KNOWN-STOP | integer operator not yet lowered (N2) |
| `q105_list_with_cap_matches_push` | KNOWN-STOP | `teko::list::with_cap` has no native lowering yet (N2) |
| `q115_match_pattern_bindings` | KNOWN-STOP | field match pattern over a non-variant subject not yet lowered (N2) |
| `q118_member_const_inherit_shadow` | KNOWN-STOP | unbound local `q118_member_const_inherit_shadow::Base::K` (internal) |
| `q128_nested_slice_empty_row` | KNOWN-STOP | fat-pointer receiver `index` not yet lowered (N2) |
| `q129_nested_slices` | KNOWN-STOP | fat-pointer receiver `index` not yet lowered (N2) |
| `q132_optionals` | KNOWN-STOP | `i64 | null` has no single PrimKind, asked by the cast target (N2) |
| `q136_raw_multiline_strings` | KNOWN-STOP | fat-pointer receiver `string interpolation` not yet lowered (N2) |
| `q137_ref_local_write_through` | KNOWN-STOP | reference deref-assignment not yet lowered (N2) |
| `q138_ref_null_admit` | KNOWN-STOP | borrow not yet lowered (N2) |
| `q139_ref_null_local` | KNOWN-STOP | field/index access on a non-struct receiver not yet lowered (N2) |
| `q140_ref_param_write_through` | KNOWN-STOP | reference deref-assignment not yet lowered (N2) |
| `q141_ref_return_passdown` | KNOWN-STOP | reference deref-assignment not yet lowered (N2) |
| `q142_ref_selfhost_ok` | KNOWN-STOP | reference deref-assignment not yet lowered (N2) |
| `q143_ref_transparent_field` | KNOWN-STOP | field/index access on a non-struct receiver not yet lowered (N2) |
| `q145_repr_niche` | KNOWN-STOP | `v` is not a fat-pointer local (internal) |
| `q148_selfref_class_optional` | KNOWN-STOP | a `null` in this position needs the null-union wrapper the placement does not declare a type for (N2) |
| `q149_skip_level_match` | KNOWN-STOP | value's type is not a member of its declared variant (internal) |
| `q150_stored_borrow_sound` | KNOWN-STOP | field/index access on a non-struct receiver not yet lowered (N2) |
| `q151_str_from_utf8` | KNOWN-STOP | builtin `str_from_utf8` not yet lowered (N2) |
| `q152_str_in_match` | KNOWN-STOP | `str` has no single PrimKind, asked by the in-expression lhs (N2) |
| `q155_struct_vs_class_constraint` | KNOWN-STOP | dynamic dispatch through the polymorphic base class `q155_struct_vs_class_constraint::q155_struct_vs_class_constraint::ClassValue` is not yet lowered — a class instance carries no vtable (N2) |
| `q157_trait_basics` | KNOWN-STOP | `str` has no single PrimKind, asked by the comparison chain (N2) |
| `q158_trait_constraint` | KNOWN-STOP | dynamic dispatch through the polymorphic base class `q158_trait_constraint::q158_trait_constraint::Box2_mcni` is not yet lowered — a class instance carries no vtable (N2) |
| `q159_trait_derive_clone_default` | KNOWN-STOP | integer operator not yet lowered (N2) |
| `q160_trait_derive_eq` | KNOWN-STOP | integer operator not yet lowered (N2) |
| `q162_trait_derive_nested` | KNOWN-STOP | `str` has no single PrimKind, asked by the comparison chain (N2) |
| `q164_trait_dispatch` | KNOWN-STOP | fat-pointer interface-dispatch result not yet lowered (N2) |
| `q165_trait_field_derive` | KNOWN-STOP | `str` has no single PrimKind, asked by the comparison chain (N2) |
| `q166_union_member_widen_base_first` | KNOWN-STOP | `null` match pattern not yet lowered (N2) |
| `q167_union_member_widen_sub_first` | KNOWN-STOP | `null` match pattern not yet lowered (N2) |
| `q183_op_of_fold_opaque_operands_yields_out` | KNOWN-STOP | integer operator not yet lowered (N2) |
| `q184_an_explicit_env_token_reaches_the_comp` | KNOWN-STOP | unknown variant case `str` (internal) |
| `q186_d1_cast_pair_parity_between_arith_and` | KNOWN-STOP | integer operator not yet lowered (N2) |
| `q187_ref_forwarding_same_referent_compiles` | KNOWN-STOP | reference deref-assignment not yet lowered (N2) |
| `q188_scenario_runs_with_cwd_in_its_own_scra` | KNOWN-STOP | `null` match pattern not yet lowered (N2) |
| `q190_list_grow_bridge` | KNOWN-STOP | reference deref-assignment not yet lowered (N2) |
| `q193_c_str_from_c_totality` | KNOWN-STOP | integer operator not yet lowered (N2) |
| `q194_c_string_roundtrip` | KNOWN-STOP | integer operator not yet lowered (N2) |
| `q197_c_string_outlives_enclosing_r` | KNOWN-STOP | adopt not yet lowered (N2) |
| `q199_c_probe_setenv_typed` | KNOWN-STOP | fat-pointer receiver `match-expression` not yet lowered (N2) |
| `np_oop` | KNOWN-WRONG | declara exit 0, devolve 12 |
| `q003_class_counter_null_present_and_absent` | KNOWN-WRONG | declara exit 42, devolve 249 |
| `q011_null_union_field_nested_one_struct_dee` | KNOWN-WRONG | declara exit 42, devolve 0 |
| `q012_self_referential_list_through_a_boxed` | KNOWN-WRONG | declara exit 42, devolve 0 |
| `q014_full_mutation_cycle_of_a_null_union_fi` | KNOWN-WRONG | declara exit 42, devolve 90 |
| `q034_class_destruct_effects` | KNOWN-WRONG | declara exit 7, devolve 0 |
| `q040_class_slice_element` | KNOWN-WRONG | declara exit 0, devolve 10 |
| `q053_const_agg_variant_rodata` | KNOWN-WRONG | declara exit 12, devolve 8 |
| `q056_const_variant_match_subject` | KNOWN-WRONG | declara exit 33, devolve 11 |
| `q076_field_assignment` | KNOWN-WRONG | declara exit 0, devolve 9 |
| `q081_generic_class_factory` | KNOWN-WRONG | declara exit 9, devolve 8 |
| `q086_if_arm_variant_widen` | KNOWN-WRONG | declara exit 30, devolve 0 |
| `q088_iface_value_field` | KNOWN-WRONG | declara exit 0, devolve 139  (SIGSEGV) |
| `q089_iface_value_hetero_slice` | KNOWN-WRONG | declara exit 0, devolve 139  (SIGSEGV) |
| `q090_iface_value_param_dispatch` | KNOWN-WRONG | declara exit 7, devolve 139  (SIGSEGV) |
| `q091_iface_value_return` | KNOWN-WRONG | declara exit 0, devolve 139  (SIGSEGV) |
| `q092_iface_value_stateful_class` | KNOWN-WRONG | declara exit 3, devolve 132 |
| `q101_lambda_bound_error_union_wrap` | KNOWN-WRONG | declara exit 30, devolve 139  (SIGSEGV) |
| `q102_lambda_opt_typedef` | KNOWN-WRONG | declara exit 48, devolve 139  (SIGSEGV) |
| `q146_safe_field_access_class` | KNOWN-WRONG | declara exit 34, devolve 139  (SIGSEGV) |
| `q153_struct_methods` | KNOWN-WRONG | declara exit 0, devolve 2 |
| `q103_len_narrow_needs_guard` | NO-EXPECT | o runbook nao declara exit; devolve 0 |
| `q180_wasm_panic_hook` | NO-EXPECT | o runbook nao declara exit; devolve 139 |
| `native_gate_coercions` | PASS | exit 21 |
| `p_member_const_cross_ns_pub_inherit` | PASS | exit 42 |
| `p_qualified_optional` | PASS | exit 22 |
| `p_same_bare_method_dispatch` | PASS | exit 10 |
| `q002_struct_point_null_present_and_absent` | PASS | exit 42 |
| `q007_forward_an_un_narrowed_null_union_retu` | PASS | exit 42 |
| `q016_comptime_fold_local` | PASS | exit 42 |
| `q017_len_is_u64_nocast` | PASS | exit 3 |
| `q019_width_literal_adopt` | PASS | exit 1 |
| `q020_width_rule_mixed_sign_peer_ok` | PASS | exit 3 |
| `q022_arena_commit_keeps_alloc` | PASS | exit 61 |
| `q025_assert_native` | PASS | exit 0 |
| `q028_cf4_field_len_fold` | PASS | exit 50 |
| `q029_cf4_index_fold` | PASS | exit 31 |
| `q035_class_destruction` | PASS | exit 0 |
| `q049_comptime_fold_local_guard` | PASS | exit 42 |
| `q050_comptime_fold_scalar` | PASS | exit 42 |
| `q051_const_agg_bytes_rodata` | PASS | exit 4 |
| `q052_const_agg_struct_rodata` | PASS | exit 104 |
| `q054_const_pub_export_survives` | PASS | exit 42 |
| `q055_const_scalar_inline` | PASS | exit 42 |
| `q058_cov_control_flow` | PASS | exit 42 |
| `q059_crossfile_fn` | PASS | exit 0 |
| `q070_discard_assign_ok` | PASS | exit 9 |
| `q071_discard_param_impl` | PASS | exit 0 |
| `q072_doc_comment_fields_and_test` | PASS | exit 0 |
| `q074_enum_member_shadows_primkind` | PASS | exit 15 |
| `q075_exit_success_path` | PASS | exit 5 |
| `q083_generic_method_self_construct` | PASS | exit 42 |
| `q084_generic_struct_method` | PASS | exit 42 |
| `q085_global_print` | PASS | exit 7 |
| `q087_iface_constraint_struct_ok` | PASS | exit 7 |
| `q106_lit_arg_if_over_i64` | PASS | exit 7 |
| `q107_lit_byte_ctx` | PASS | exit 73 |
| `q108_lit_if_match_ctx` | PASS | exit 14 |
| `q109_lit_over_i64_u64` | PASS | exit 7 |
| `q110_lit_redundant_cast` | PASS | exit 5 |
| `q111_loop_range` | PASS | exit 0 |
| `q112_loop_three_part` | PASS | exit 0 |
| `q113_loop_while_and_label` | PASS | exit 0 |
| `q114_masked_shift_counts` | PASS | exit 129 |
| `q117_member_const_aggregate` | PASS | exit 42 |
| `q119_member_const_scalar` | PASS | exit 42 |
| `q120_member_const_trait_fold` | PASS | exit 42 |
| `q122_namespaced_div_not_builtin` | PASS | exit 7 |
| `q123_native_lir_call_add` | PASS | exit 42 |
| `q124_native_lir_div` | PASS | exit 14 |
| `q125_native_lir_exit` | PASS | exit 42 |
| `q126_native_lir_let_mul` | PASS | exit 42 |
| `q127_native_lir_neg` | PASS | exit 42 |
| `q130_noncapturing_lambda_lift` | PASS | exit 7 |
| `q131_null_union_basic` | PASS | exit 8 |
| `q133_own_if_mut_shadow_no_leak` | PASS | exit 1 |
| `q134_own_if_reassign_exit` | PASS | exit 5 |
| `q135_own_match_pattern_binding_no_collision` | PASS | exit 1 |
| `q144_repr_box` | PASS | exit 19 |
| `q147_self_recursive_aggregate_arg` | PASS | exit 7 |
| `q154_struct_through_constraint` | PASS | exit 5 |
| `q156_time_types` | PASS | exit 51 |
| `q168_unsafe_containment` | PASS | exit 42 |
| `q169_unsafe_path_segment` | PASS | exit 7 |
| `q171_unsafe_type_modifier` | PASS | exit 42 |
| `q172_use_list_import` | PASS | exit 21 |
| `q173_wasm64_arith_exit` | PASS | exit 42 |
| `q174_wasm_break_in_if` | PASS | exit 3 |
| `q175_wasm_continue_step` | PASS | exit 6 |
| `q176_wasm_defer_arm_scope` | PASS | exit 7 |
| `q177_wasm_if_both_diverge` | PASS | exit 1 |
| `q178_wasm_labeled_break` | PASS | exit 7 |
| `q179_wasm_loop_count` | PASS | exit 6 |
| `q181_wasm_print_exit` | PASS | exit 7 |
| `q182_width_rule_same_sign_widen` | PASS | exit 232 |
| `q185_d1_boundary_widen_cast_compiles` | PASS | exit 42 |
| `q191_c_extern_unsafe_accepted` | PASS | exit 42 |
| `q192_c_types_alias_identity` | PASS | exit 7 |
| `q200_arena_size_directive_ok` | PASS | exit 28 |
