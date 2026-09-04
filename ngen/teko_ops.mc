// teko_ops.mc -- operator overloading (entrega 4, crumb C5), in one pass() over
// N_BINARY.
//
//   class Vec { i64 x; i64 y;
//       Vec operator+(self, Vec b) { ... }
//       i64 operator==(self, Vec b) { ... }
//   }
//   (a + b) == c
//
// `operator` is CONTEXTUAL inside the body of a type, exactly as `virtual` and
// `override` are: nothing reserves the word, and the token after it -- `+`,
// `==`, `<<` -- names the method (`op_add`, `op_eq`, `op_shl`). From there on it
// is an ordinary method: teko_class.mc reads the parameter list, keys it by
// signature, gives it the same `Vec_op_add__Vec` suffix an overload gets, and
// the declaration lands in the method table like any other.
//
// The rewrite is a pass, and it has to be. `syntax_infix("+", ...)` registers
// without complaining and NEVER fires: parse_unit() calls ops_init() as its
// first statement (mc src/parse.mc:2110 -> :293-306) and infix_set zeroes the
// handler column (:279), so the core's own table is rebuilt on top of whatever
// user_init() put there. Measured, with an unconditional err_at inside such a
// handler: `1 + 2` compiled and answered 3. A pass over N_BINARY is the one
// route, and passes run BEFORE fold(), so the node arrives in the shape the
// source wrote (hooks.md § pass()).
//
// Dispatch is by the LEFT operand, and only by it: the left has to be a teko
// type declaring the operator, and the right has to land on one of that
// declaration's signatures. A teko value on the right with a core type on the
// left is refused rather than reversed. When NEITHER side is a teko type the
// node is not touched at all -- which is what leaves the core's own arithmetic,
// and every program that never declares an operator, byte for byte as it was.
// Nor is it touched when the left type declares no such operator and the right
// side is of a core type: a teko reference is one pointer word and `v + zero`
// is the core's arithmetic over it, not an operator this pass lost.
//
// The one thing a walk over N_BINARY has to know is that this project BUILDS
// N_BINARY of its own: `p.side` is `ld64(p + SIDE)`, and `p` there is a teko
// value under a `+`. Those are addresses, not operators, and they are told
// apart structurally -- the first argument of a memory intrinsic this project
// emits is an address, and so is the left spine of an array-element address
// (`ADD(ADD(obj, off), MUL(i, w))`). The walk is top-down, so the intrinsic is
// seen before its own argument and the mark is set in time.
//
// Resolution is bottom-up inside one expression: an operand that is itself an
// operator is rewritten first, so `(a + b) == c` asks the `==` about the RETURN
// type of `operator+` rather than about the type of `a`. The rewritten node is
// registered in the type table (tk_xt_put) for the same reason.

// the method name an operator token declares, or 0 when the token is not one a
// type may overload
uptr tk_op_method(i64 tok) {
    if (tok == K_ADD) return "op_add";
    if (tok == K_SUB) return "op_sub";
    if (tok == K_MUL) return "op_mul";
    if (tok == K_DIV) return "op_div";
    if (tok == K_MOD) return "op_mod";
    if (tok == K_EQ)  return "op_eq";
    if (tok == K_NE)  return "op_ne";
    if (tok == K_LT)  return "op_lt";
    if (tok == K_LE)  return "op_le";
    if (tok == K_GT)  return "op_gt";
    if (tok == K_GE)  return "op_ge";
    if (tok == K_AND) return "op_and";
    if (tok == K_OR)  return "op_or";
    if (tok == K_XOR) return "op_xor";
    if (tok == K_SHL) return "op_shl";
    if (tok == K_SHR) return "op_shr";
    return 0;
}

// how a diagnostic spells the operator back to whoever wrote it
uptr tk_op_spell(i64 tok) {
    if (tok == K_ADD) return "+";
    if (tok == K_SUB) return "-";
    if (tok == K_MUL) return "*";
    if (tok == K_DIV) return "/";
    if (tok == K_MOD) return "%";
    if (tok == K_EQ)  return "==";
    if (tok == K_NE)  return "!=";
    if (tok == K_LT)  return "<";
    if (tok == K_LE)  return "<=";
    if (tok == K_GT)  return ">";
    if (tok == K_GE)  return ">=";
    if (tok == K_AND) return "&";
    if (tok == K_OR)  return "|";
    if (tok == K_XOR) return "^";
    if (tok == K_SHL) return "<<";
    if (tok == K_SHR) return ">>";
    return 0;
}

uptr tk_op_list() { return "+ - * / % == != < <= > >= & | ^ << >>"; }

// ---- the declaration side, called from teko_class.mc's tk_member ----
// `operator+` in the position of a method name. The word is not reserved, so a
// body that does not open with it reads its name the way it always did; one
// that does gets the operator's own token read here, and a token no type may
// declare is refused with the list of the ones it may.
uptr tk_op_name(uptr pisop) {
    st64(pisop, 0);
    if (!tk_kw("operator")) return p_ident();
    i64 line = p_line();
    uptr fl = p_file();
    p_next();
    i64 tok = p_id();
    if (tok == K_BANG || tok == K_TILDE)
        err_at(fl, line, "teko: a unary operator is not taught yet");
    uptr m = tk_op_method(tok);
    if (m == 0)
        err_at(fl, line, tk_join("teko: `operator` is followed by one of ", tk_op_list()));
    p_next();
    st64(pisop, 1);
    return m;
}

// what an operator declaration has to be for a site to reach it: one operand
// besides `self`, always passed, and a value to hand back
void tk_op_decl_check(i64 np, i64 nreq, i64 fty) {
    if (np == 0) err_at(tk_file, tk_line, "teko: a unary operator is not taught yet");
    if (np > 1) err_at(tk_file, tk_line, "teko: an operator takes `self` and one more parameter");
    if (nreq != 1) err_at(tk_file, tk_line, "teko: an operator parameter has no default; a site always passes it");
    if (fty == TY_VOID) err_at(tk_file, tk_line, "teko: an operator returns a value");
}

// ---- the address marker ----
// The node the walk is about to reach as an ADDRESS rather than as an operand.
// One slot is enough because the walk descends into a node's children right
// after visiting it, and an address spine alternates intrinsic and binary.
i64 tk_ops_addr = 0;

// the memory intrinsics this project emits for a field, an element and a vtable
// word -- their first argument is an address
i64 tk_ops_is_mem(uptr name) {
    if (str_eq(name, "ld8"))  return 1;
    if (str_eq(name, "ld16")) return 1;
    if (str_eq(name, "ld32")) return 1;
    if (str_eq(name, "ld64")) return 1;
    if (str_eq(name, "st8"))  return 1;
    if (str_eq(name, "st16")) return 1;
    if (str_eq(name, "st32")) return 1;
    if (str_eq(name, "st64")) return 1;
    return 0;
}

// `ADD(ADD(obj, off), MUL(i, w))` -- an array element. The scaled index on the
// right is what identifies the shape, and the address it is added to is the
// next node of the spine; every other address ends here, its left operand being
// the object itself.
void tk_ops_addr_step(i64 n) {
    if (nd_kind(n) != N_BINARY) return;
    if (nd_kind(nd_b(n)) != N_BINARY) return;
    tk_ops_addr = nd_a(n);
}

// ---- picking the declaration a site lands on ----
uptr tk_op_sig1(i64 ty) { return tk_join("__", type_name(ty)); }

// the core's integers, which is where an integer literal may land when the type
// it carries -- i64 -- is not the one declared
i64 tk_op_int_ty(i64 i) {
    if (i == 0) return TY_U8;
    if (i == 1) return TY_U16;
    if (i == 2) return TY_U32;
    if (i == 3) return TY_U64;
    return TY_I64;
}

// a literal against the integer signatures the type declares: exactly one is a
// pick, more than one is ambiguous (-2), none is no pick at all (-1)
i64 tk_op_pick_int(i64 si, uptr m) {
    i64 found = 0 - 1;
    i64 i = 0;
    loop {
        if (i >= 5) break;
        i64 mi = tk_method_sig_find(si, m, tk_op_sig1(tk_op_int_ty(i)));
        if (mi >= 0) {
            if (found >= 0) return 0 - 2;
            found = mi;
        }
        i = i + 1;
    }
    return found;
}

// the declaration the right operand lands on. The declared type is tried first,
// so `v + 1` reaches `operator+(self, i64)` where there is one; a literal falls
// back to the other integers of the core, under the same tie-break the free
// overload resolution uses.
i64 tk_op_pick(i64 si, uptr m, i64 b, i64 tb) {
    if (tb >= 0) {
        i64 mi = tk_method_sig_find(si, m, tk_op_sig1(tb));
        if (mi >= 0) return mi;
    }
    if (nd_kind(b) != N_INT) return 0 - 1;
    return tk_op_pick_int(si, m);
}

// ---- the diagnostics ----
uptr tk_op_side_msg(uptr side, uptr sp) {
    return tk_join3(tk_join3("teko: the type of the ", side, " side of `"), sp, "` is not known here");
}

uptr tk_op_none_msg(uptr tname, uptr sp) {
    return tk_join3(tk_join3("teko: ", tname, " declares no operator `"), sp, "`");
}

uptr tk_op_sig_msg(uptr tname, uptr sp, uptr rname) {
    return tk_join3(tk_op_none_msg(tname, sp), " for ", tk_join(rname, " on the right"));
}

// what the right operand is called in a diagnostic: the type it declares, or
// the shape of the node when nothing declares one
uptr tk_op_rname(i64 b, i64 tb) {
    if (tb >= 0) return type_name(tb);
    return "an integer literal";
}

// ---- the rewrite ----
void tk_ops_binary(i64 n);

// an operand that is itself an operator is resolved first, so the node above it
// reads the RETURN type of the call it became instead of the type of its own
// left operand
void tk_ops_operand(i64 x) {
    if (nd_kind(x) == N_BINARY) tk_ops_binary(x);
}

// the call the site becomes, put in the node's own place so the parent keeps
// pointing at it and the sibling list comes back untouched (hooks.md § pass()).
// Its type is registered because an enclosing operator resolves against it.
void tk_ops_emit(i64 n, i64 mi, i64 left, i64 right, i64 line, uptr fl) {
    i64 r = tk_emit_call(left, mi, right, 1, line, fl);
    i64 keep = nd_next(n);
    node_assign(n, r);
    set_nd_next(n, keep);
    i64 ret = mt_ret_at(mi);
    tk_xt_put(n, tk_struct_by_ty(ret), ret, 0);
}

// the left operand has a teko type. Once that type declares the operator the
// site has to resolve against one of its signatures, and nothing falls back:
// what the source wrote is a call, and calling another signature than the one
// written is what a guess would cost.
//
// A type that declares no such operator at all, with a core value on the right,
// is a different node: a teko reference IS one pointer word, `v + zero` is the
// core's own arithmetic over it, and this pass does not own it.
void tk_ops_teko_left(i64 n, i64 si, i64 sb, i64 op, i64 line, uptr fl) {
    uptr sp = tk_op_spell(op);
    uptr m = tk_op_method(op);
    if (tk_method_named_find(si, m) < 0) {
        if (sb < 0) return;
        err_at(fl, line, tk_op_none_msg(sr_name_at(si), sp));
    }
    i64 b = nd_b(n);
    i64 tb = tk_ty_of(b);
    if (tb < 0 && nd_kind(b) != N_INT) err_at(fl, line, tk_op_side_msg("right", sp));
    i64 mi = tk_op_pick(si, m, b, tb);
    if (mi == 0 - 2)
        err_at(fl, line, tk_join3("teko: more than one operator `", sp, "` accepts this literal"));
    if (mi < 0)
        err_at(fl, line, tk_op_sig_msg(sr_name_at(si), sp, tk_op_rname(b, tb)));
    tk_ops_emit(n, mi, nd_a(n), b, line, fl);
}

// One node of the walk. A binary whose two operands are both of core types is
// left exactly as it is -- the core owns it -- and that is the whole of the
// no-op guarantee for a program that declares no operator.
void tk_ops_binary(i64 n) {
    tk_ops_operand(nd_a(n));
    tk_ops_operand(nd_b(n));
    i64 sa = tk_ty_struct_of(nd_a(n));
    i64 sb = tk_ty_struct_of(nd_b(n));
    if (sa < 0 && sb < 0) return;
    i64 op = nd_op(n);
    i64 line = nd_line(n);
    uptr fl = nd_file(n);
    uptr sp = tk_op_spell(op);
    if (sp == 0) err_at(fl, line, "teko: this operator cannot be overloaded");
    if (sa >= 0) {
        tk_ops_teko_left(n, sa, sb, op, line, fl);
        return;
    }
    if (tk_ty_of(nd_a(n)) < 0) err_at(fl, line, tk_op_side_msg("left", sp));
    err_at(fl, line, tk_join3("teko: `", sp, "` needs a teko type on the left; a reversed operator is not taught"));
}

void tk_ops_visit(i64 n) {
    if (n == tk_ops_addr) {
        tk_ops_addr_step(n);
        return;
    }
    i64 k = nd_kind(n);
    if (k == N_CALL) {
        if (tk_ops_is_mem(nd_name(n))) tk_ops_addr = nd_a(n);
        return;
    }
    if (k != N_BINARY) return;
    tk_ops_binary(n);
}

i64 tk_ops_pass(i64 root) {
    tk_ty_pass_walk(root, &tk_ops_visit);
    return root;
}
