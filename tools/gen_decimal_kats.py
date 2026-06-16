#!/usr/bin/env python3
# =====================================================================================
# gen_decimal_kats.py — Phase 17.F.1 KAT generator (the Python `decimal` oracle).
#
# Emits tests/runtime/teko_decimal_kat_vectors.h: a static const table of
#   { op, a[256], b[256], expect_ok, result[256] }
# whose operands AND expected results are the EXACT 256-byte little-endian encoding of the
# teko_decimal struct, computed by a Python `decimal.Decimal` oracle that MIRRORS the C
# arithmetic semantics in teko_decimal.c BYTE-FOR-BYTE:
#   * exact compute at high precision (getcontext().prec >= 600),
#   * if the result needs > 38 fractional digits -> quantize to scale 38, ROUND_HALF_EVEN,
#   * a case is expect_fail when the result coefficient would not fit 1984 bits (>= 2**1984)
#     or on div/mod by zero,
#   * results stored UN-NORMALIZED at the rule's natural scale (add/sub = max scale,
#     mul = sa+sb capped at 38, div = 38), matching the C.
#
# Deterministic: a fixed-seed LCG drives all random operands (no os.urandom / random module),
# so re-running regenerates the identical header. Commit BOTH this script and the header.
#
# Usage:  python3 tools/gen_decimal_kats.py > tests/runtime/teko_decimal_kat_vectors.h
# =====================================================================================
import sys
from decimal import Decimal, getcontext, ROUND_HALF_EVEN, localcontext

LIMBS = 31
MAX_SCALE = 38
COEFF_MAX = 1 << (64 * LIMBS)   # 2**1984
getcontext().prec = 600

# Op tags — MUST match the TEKO_DEC_OP_* enum in test_decimal.c.
OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_CMP = range(6)
OP_NAME = {OP_ADD: "ADD", OP_SUB: "SUB", OP_MUL: "MUL",
           OP_DIV: "DIV", OP_MOD: "MOD", OP_CMP: "CMP"}

# --- deterministic LCG (numerical recipes constants) --------------------------------
class LCG:
    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFFFFFFFFFF
    def next(self):
        self.s = (self.s * 6364136223846793005 + 1442695040888963407) & 0xFFFFFFFFFFFFFFFF
        return self.s
    def below(self, n):
        return self.next() % n
    def digits(self, ndigits):
        # a decimal string of exactly ndigits digits (no leading-zero suppression needed;
        # the C from_components accepts leading zeros).
        out = []
        for _ in range(ndigits):
            out.append(chr(ord('0') + self.below(10)))
        return ''.join(out)

# --- 256-byte LE encoding of a (sign, scale, coeff-int) triple ----------------------
def encode(sign, scale, coeff):
    assert 0 <= scale <= MAX_SCALE
    assert 0 <= coeff < COEFF_MAX
    if coeff == 0:
        sign = 0   # canonical zero sign
    b = bytearray(256)
    b[0] = sign & 1
    b[1] = scale
    b[2] = 0
    # _pad[5] already zero
    for i in range(LIMBS):
        limb = (coeff >> (64 * i)) & 0xFFFFFFFFFFFFFFFF
        for j in range(8):
            b[8 + i * 8 + j] = (limb >> (8 * j)) & 0xFF
    return bytes(b)

# A decimal operand expressed as the (sign, scale, coeff) triple the C builds via
# teko_decimal_from_components. value = (-1)^sign * coeff * 10^-scale.
class Op:
    def __init__(self, sign, scale, coeff):
        if coeff == 0:
            sign = 0
        self.sign = sign & 1
        self.scale = scale
        self.coeff = coeff
    def to_decimal(self):
        d = Decimal(self.coeff).scaleb(-self.scale)
        return -d if self.sign else d
    def encode(self):
        return encode(self.sign, self.scale, self.coeff)

# Decompose a Decimal into (sign, coeff-int, exponent). For a value v = c * 10^e.
def decompose(d):
    s, digits, exp = d.as_tuple()
    coeff = 0
    for dd in digits:
        coeff = coeff * 10 + dd
    return s, coeff, exp

# Given an exact Decimal `val` and a TARGET scale, produce (ok, sign, scale, coeff) under the
# C semantics: store at `target_scale`; if the exact value has MORE than target_scale frac
# digits, banker's-round to target_scale; then check the coefficient fits 1984 bits.
def reduce_to_scale(val, target_scale):
    q = Decimal(1).scaleb(-target_scale)
    with localcontext() as ctx:
        ctx.prec = 10000
        r = val.quantize(q, rounding=ROUND_HALF_EVEN)
    sgn, coeff, exp = decompose(r)
    # exp should be -target_scale (quantize forces it); coeff is the integer at that scale.
    # Normalize exp to exactly -target_scale by padding (quantize guarantees this, but be safe).
    if exp > -target_scale:
        coeff *= 10 ** (exp + target_scale)
        exp = -target_scale
    scale = -exp
    if scale < 0 or scale > MAX_SCALE:
        return (False, 0, 0, 0)
    if coeff >= COEFF_MAX:
        return (False, 0, 0, 0)
    return (True, sgn, scale, coeff)

# --- oracle per op (mirrors teko_decimal.c EXACTLY) ---------------------------------
def oracle(op, a, b):
    da, db = a.to_decimal(), b.to_decimal()
    if op == OP_CMP:
        # compare returns -1/0/1; encode the result as a scale-0 decimal in [-1,0,1].
        if da < db:
            c = -1
        elif da > db:
            c = 1
        else:
            c = 0
        sign = 1 if c < 0 else 0
        return (True, Op(sign, 0, abs(c)))
    if op == OP_ADD:
        s = max(a.scale, b.scale)
        ok, sgn, scale, coeff = reduce_to_scale(da + db, s)
    elif op == OP_SUB:
        s = max(a.scale, b.scale)
        ok, sgn, scale, coeff = reduce_to_scale(da - db, s)
    elif op == OP_MUL:
        s = min(a.scale + b.scale, MAX_SCALE)
        ok, sgn, scale, coeff = reduce_to_scale(da * db, s)
    elif op == OP_DIV:
        if db == 0:
            return (False, None)
        ok, sgn, scale, coeff = reduce_to_scale(da / db, MAX_SCALE)
    elif op == OP_MOD:
        if db == 0:
            return (False, None)
        # Mirror teko_decimal_mod EXACTLY: result = a - trunc(a/b)*b, where trunc(a/b) is the
        # integer (scale-0) quotient toward zero. In the C, t has scale 0, so t*b has scale
        # b.scale (<=38, no rounding), and a - (t*b) has the NATURAL scale max(a.scale,b.scale).
        # The result value is exact at that scale; we store it there (NOT at Python's chosen
        # zero-exponent, which would otherwise pick a different scale for a zero remainder).
        with localcontext() as ctx:
            ctx.prec = 10000
            t = (da / db).to_integral_value(rounding='ROUND_DOWN')  # trunc toward zero
            tb = t * db
            r = da - tb
        # C fails if the intermediate t*b coefficient overflows 1984 bits (mul fail-loud).
        _, tb_coeff, _ = decompose(tb)
        if tb_coeff >= COEFF_MAX:
            return (False, None)
        result_scale = min(max(a.scale, b.scale), MAX_SCALE)
        ok, sgn, scale, coeff = reduce_to_scale(r, result_scale)
    else:
        raise ValueError(op)
    if not ok:
        return (False, None)
    return (True, Op(sgn, scale, coeff))

# --- 17.F.2 scale-preserving plain `.`-decimal canonicalization ----------------------
# Render a (sign, scale, coeff) triple to the EXACT plain `.`-decimal, scale-preserving form
# the C teko_decimal_to_string produces. Python's str(Decimal) can emit scientific notation
# (e.g. '1E+3') and its own scale; we NEVER use it — we build the string ourselves from the
# (sign, scale, coeff) the C stores, so C and oracle agree byte-for-byte.
#   scale==0          -> integer digits (no point); coeff==0 -> "0"
#   scale>0           -> integer part '.' fractional part; the last `scale` digits are the
#                        fraction, left-padded with '0' when len(digits) <= scale
#                        (coeff=5,scale=3 -> "0.005"); coeff=0,scale=s -> "0." + s zeros
#   negative (coeff!=0) -> leading '-'  (zero is always '+', no leading '-')
def canon_string(sign, scale, coeff):
    digits = str(coeff)  # decimal digits, no leading zeros (coeff>=0); "0" for zero
    is_zero = (coeff == 0)
    neg = (sign == 1 and not is_zero)
    pre = '-' if neg else ''
    if scale == 0:
        return pre + digits
    if len(digits) <= scale:
        frac = digits.rjust(scale, '0')
        return pre + '0.' + frac
    int_part = digits[:len(digits) - scale]
    frac = digits[len(digits) - scale:]
    return pre + int_part + '.' + frac

# --- vector construction -------------------------------------------------------------
VECTORS = []  # list of (op, Op a, Op b, expect_ok, result-Op-or-None)
FMT_VECTORS = []   # list of (Op d, expected_string)              -- format KATs
PARSE_VECTORS = [] # list of (input_string, expect_ok, Op-or-None)-- parse KATs

def add_case(op, a, b):
    ok, res = oracle(op, a, b)
    VECTORS.append((op, a, b, ok, res))

# Add a format KAT for `d`; the expected string is the C-canonical scale-preserving form.
def add_fmt(d):
    FMT_VECTORS.append((d, canon_string(d.sign, d.scale, d.coeff)))

# Add a parse KAT. If expect_ok, the expected Op is computed under the parse semantics:
# build the exact coefficient at `scale` frac digits, then round to 38 if scale>38, then the
# 1984-bit fit check (overflow -> expect_fail override). Pass the raw (sign, scale, coeff) the
# string denotes (BEFORE reduction); this helper applies the same reductions the C does.
def add_parse_ok(s, sign, scale, coeff):
    # Apply reduction (1): >38 frac digits -> round-half-even to scale 38.
    if scale > MAX_SCALE:
        drop = scale - MAX_SCALE
        val = Decimal(coeff)
        q = Decimal(1)
        with localcontext() as ctx:
            ctx.prec = 10000
            # round coeff / 10^drop to nearest even integer
            scaled = (val / (Decimal(10) ** drop)).quantize(q, rounding=ROUND_HALF_EVEN)
        coeff = int(scaled)
        scale = MAX_SCALE
    # Reduction (2): 1984-bit fit.
    if coeff >= COEFF_MAX:
        PARSE_VECTORS.append((s, False, None))
        return
    PARSE_VECTORS.append((s, True, Op(sign, scale, coeff)))

def add_parse_fail(s):
    PARSE_VECTORS.append((s, False, None))

def main():
    rng = LCG(0x1F2E3D4C5B6A7988)

    # 1. Hand-picked exact/boundary cases.
    fixed = [
        # add/sub aligning scales
        (OP_ADD, Op(0, 2, 150), Op(0, 0, 0)),     # 1.50 + 0 -> 1.50 (scale 2, un-normalized)
        (OP_ADD, Op(0, 1, 15), Op(0, 2, 25)),     # 1.5 + 0.25 -> 1.75
        (OP_SUB, Op(0, 0, 5), Op(0, 0, 5)),       # 5 - 5 -> 0
        (OP_SUB, Op(0, 0, 3), Op(0, 0, 5)),       # 3 - 5 -> -2
        (OP_ADD, Op(1, 0, 2), Op(0, 0, 2)),       # -2 + 2 -> 0 (sign canonicalized)
        (OP_ADD, Op(0, 38, 1), Op(0, 0, 1)),      # 1e-38 + 1
        # mul scaling
        (OP_MUL, Op(0, 2, 125), Op(0, 1, 2)),     # 1.25 * 0.2 -> 0.250 (scale 3)
        (OP_MUL, Op(0, 20, 3), Op(0, 20, 7)),     # 3e-20 * 7e-20 -> scale 40 -> round to 38
        (OP_MUL, Op(1, 1, 5), Op(0, 1, 5)),       # -0.5 * 0.5 -> -0.25
        # banker's boundary cases via div / mul-round
        (OP_DIV, Op(0, 0, 5), Op(0, 0, 2)),       # 5/2 -> 2.5 -> stored scale 38 = 2.5
        (OP_DIV, Op(0, 0, 1), Op(0, 0, 3)),       # 1/3 -> 0.333...3 (38 places, round even)
        (OP_DIV, Op(0, 0, 2), Op(0, 0, 7)),       # 2/7 -> non-terminating at 38
        (OP_DIV, Op(0, 0, 1), Op(0, 0, 8)),       # 0.125 exact
        (OP_DIV, Op(0, 0, 7), Op(0, 0, 1)),       # 7/1 -> 7.000...0
        (OP_DIV, Op(0, 0, 10), Op(0, 0, 4)),      # 10/4 -> 2.5
        # round-half-to-even ties through mul (scale-cap rounding):
        # 0.0...025 @ extreme scales -> rounds to even at 38
        (OP_MUL, Op(0, 19, 25), Op(0, 19, 1)),    # 25e-19 * 1e-19 = 25e-38 (scale 38 exact)
        (OP_MUL, Op(0, 19, 25), Op(0, 20, 1)),    # 25e-19 * 1e-20 = 25e-39 -> 2e-38? (2.5->2)
        (OP_MUL, Op(0, 19, 35), Op(0, 20, 1)),    # 35e-39 -> 4e-38 (3.5->4)
        # mod (Python semantics)
        (OP_MOD, Op(0, 0, 7), Op(0, 0, 3)),       # 7 % 3 -> 1
        (OP_MOD, Op(1, 0, 7), Op(0, 0, 3)),       # -7 % 3 -> -1 (sign of a)
        (OP_MOD, Op(0, 0, 7), Op(1, 0, 3)),       # 7 % -3 -> 1
        (OP_MOD, Op(0, 1, 75), Op(0, 1, 2)),      # 7.5 % 0.2 -> 0.1
        (OP_MOD, Op(0, 2, 1000), Op(0, 0, 3)),    # 10.00 % 3 -> 1.00
        # cmp
        (OP_CMP, Op(0, 1, 15), Op(0, 2, 150)),    # 1.5 == 1.50
        (OP_CMP, Op(1, 0, 1), Op(0, 0, 1)),       # -1 < 1
        (OP_CMP, Op(0, 0, 0), Op(1, 0, 0)),       # +0 == -0
        (OP_CMP, Op(0, 0, 3), Op(0, 1, 25)),      # 3 > 2.5
        (OP_CMP, Op(1, 0, 3), Op(1, 1, 25)),      # -3 < -2.5
        # div/mod by zero -> expect_fail
        (OP_DIV, Op(0, 0, 5), Op(0, 0, 0)),
        (OP_MOD, Op(0, 0, 5), Op(0, 0, 0)),
        # coefficient overflow -> expect_fail (huge * huge)
        (OP_MUL, Op(0, 0, (10 ** 300)), Op(0, 0, (10 ** 300))),  # ~600 digit product >= 2^1984
        (OP_ADD, Op(0, 0, (10 ** 596) - 1), Op(0, 0, (10 ** 596) - 1)),  # near-limit add
    ]
    for op, a, b in fixed:
        add_case(op, a, b)

    # 2. Every scale 0..38 in an add and a div (exercise alignment + 38-place rounding).
    for sc in range(0, MAX_SCALE + 1):
        a = Op(0, sc, (rng.next() % (10 ** 12)) + 1)
        b = Op(0, (sc * 7) % (MAX_SCALE + 1), (rng.next() % (10 ** 9)) + 1)
        add_case(OP_ADD, a, b)
        add_case(OP_SUB, a, b)
        add_case(OP_CMP, a, b)
        # division by a small nonzero -> scale-38 rounding everywhere
        d = Op(0, sc % 5, (rng.next() % 97) + 1)
        add_case(OP_DIV, a, d)
        add_case(OP_MOD, a, d)
        add_case(OP_MUL, a, d)

    # 3. Random small & near-590-digit operands across all ops.
    for _ in range(120):
        nd_a = 1 + rng.below(40)
        nd_b = 1 + rng.below(40)
        sa = rng.below(MAX_SCALE + 1)
        sb = rng.below(MAX_SCALE + 1)
        sga = rng.below(2)
        sgb = rng.below(2)
        a = Op(sga, sa, int(rng.digits(nd_a)))
        b = Op(sgb, sb, int(rng.digits(nd_b)))
        for op in (OP_ADD, OP_SUB, OP_MUL, OP_CMP):
            add_case(op, a, b)
        # avoid div/mod by zero in the random stream (separate fail cases above)
        if b.coeff != 0:
            add_case(OP_DIV, a, b)
            add_case(OP_MOD, a, b)

    # 4. Near-limit coefficients (~590 digits) for add/sub/cmp (mul would overflow).
    for _ in range(20):
        nd = 580 + rng.below(10)
        a = Op(rng.below(2), rng.below(MAX_SCALE + 1), int(rng.digits(nd)))
        b = Op(rng.below(2), rng.below(MAX_SCALE + 1), int(rng.digits(nd)))
        add_case(OP_ADD, a, b)
        add_case(OP_SUB, a, b)
        add_case(OP_CMP, a, b)

    # 5. Non-terminating divisions in bulk (the Knuth-division safety net).
    for n in range(1, 41):
        for d in (3, 7, 9, 11, 13, 17, 23):
            add_case(OP_DIV, Op(0, 0, n), Op(0, 0, d))

    # ===== 17.F.2 FORMAT KATs (256B -> expected scale-preserving plain `.`-decimal) =====
    fmt_fixed = [
        Op(0, 2, 150),    # "1.50"  trailing-zero preservation
        Op(0, 0, 150),    # "150"   scale 0
        Op(0, 3, 5),      # "0.005" leading-zero fraction
        Op(1, 1, 5),      # "-0.5"  negative
        Op(0, 0, 100),    # "100"
        Op(0, 0, 0),      # "0"     scale-0 zero
        Op(0, 3, 0),      # "0.000" scale-3 zero
        Op(0, 38, 0),     # "0." + 38 zeros
        Op(1, 0, 0),      # "-0" canonicalizes to "0" (sign dropped for zero)
        Op(0, 2, 12345),  # "123.45"
        Op(1, 2, 12345),  # "-123.45"
        Op(0, 1, 10),     # "1.0"
        Op(0, 38, 1),     # "0." + 37 zeros + "1"
        Op(0, 38, 12345), # 5 sig frac digits, padded to scale 38
        Op(0, 5, 100000), # "1.00000"
        Op(1, 4, 30000),  # "-3.0000"
        Op(0, 0, 7),      # "7"
    ]
    for d in fmt_fixed:
        add_fmt(d)
    # Every scale 0..38 with a fixed nonzero coefficient (exercise padding + point insertion).
    for sc in range(0, MAX_SCALE + 1):
        add_fmt(Op(0, sc, 123456789))
        add_fmt(Op(1, sc, 1))            # tiny coeff at each scale (max leading-zero padding)
    # A ~590-digit integer (scale 0) and the same digits at scale 38.
    big = int(rng.digits(590))
    add_fmt(Op(0, 0, big))
    add_fmt(Op(1, 38, big))
    # Random round-trip seeds (format then parse-back, checked below).
    for _ in range(40):
        nd = 1 + rng.below(50)
        add_fmt(Op(rng.below(2), rng.below(MAX_SCALE + 1), int(rng.digits(nd))))

    # ===== 17.F.2 PARSE KATs (string -> expect_ok + expected 256B) =====
    # Valid forms (string, sign, frac-scale, coeff) — coeff is the integer of ALL digits.
    add_parse_ok("0", 0, 0, 0)
    add_parse_ok("150", 0, 0, 150)
    add_parse_ok("1.50", 0, 2, 150)
    add_parse_ok("0.005", 0, 3, 5)
    add_parse_ok("-0.5", 1, 1, 5)
    add_parse_ok("+42", 0, 0, 42)
    add_parse_ok("+0.25", 0, 2, 25)
    add_parse_ok("-123.45", 1, 2, 12345)
    add_parse_ok("123.45", 0, 2, 12345)
    add_parse_ok("  7  ", 0, 0, 7)          # surrounding whitespace
    add_parse_ok("\t-3.0000\n", 1, 4, 30000) # ws + trailing-zero fraction
    add_parse_ok("100", 0, 0, 100)
    add_parse_ok("1.0", 0, 1, 10)
    add_parse_ok("0.000", 0, 3, 0)           # all-zero fraction
    add_parse_ok("0.00000000000000000000000000000000000001", 0, 38, 1) # 38 frac digits exact
    add_parse_ok("00123", 0, 0, 123)         # leading zeros in integer part
    add_parse_ok("000.5", 0, 1, 5)           # leading zeros + fraction
    # 590-digit integer (valid, < 2^1984 ~ 597 digits).
    big_s = rng.digits(590).lstrip('0') or '0'
    add_parse_ok(big_s, 0, 0, int(big_s))
    # >38 fractional digits -> rounds to scale 38 (expect_ok with the rounded 256B).
    # 40 frac digits, last two ".._25" round-half-to-even on the 39th/40th dropped digits.
    s40 = "0." + ("1" * 38) + "25"           # 40 frac digits
    add_parse_ok(s40, 0, 40, int("1" * 38 + "25"))
    s39 = "1." + ("0" * 38) + "5"            # 39 frac, dropped '5' exact tie -> to even (0 stays)
    add_parse_ok(s39, 0, 39, int("1" + "0" * 38 + "5"))

    # Reject forms.
    for bad in ["", ".", "1.2.3", "abc", "1 2", "1e3", "1E3", "1.5e2", "+", "-", "+.", "-.",
                ".5x", "5.", "1..2", "0x1F", "1,000", " ", "12 34", "+-5", "1.2 3"]:
        add_parse_fail(bad)
    # >597-digit integer -> coefficient overflow (>= 2^1984) -> expect_fail.
    over = rng.digits(620).lstrip('0') or '0'
    if int(over) < COEFF_MAX:           # ensure it truly overflows (620 digits always does)
        over = "9" * 620
    add_parse_fail(over)

    # Round-trips: parse(format(d)) == d for every format vector (added as parse KATs whose
    # input is the canonical string and whose expected Op is d itself — already reduced/valid).
    for (d, sstr) in FMT_VECTORS:
        # d came from Op(...) with scale<=38 and coeff<COEFF_MAX, so it's a valid parse target.
        PARSE_VECTORS.append((sstr, True, d))

    emit()

def emit():
    out = sys.stdout
    out.write("// AUTO-GENERATED by tools/gen_decimal_kats.py — DO NOT EDIT BY HAND.\n")
    out.write("// Phase 17.F.1 decimal KAT vectors: Python `decimal` oracle (ROUND_HALF_EVEN,\n")
    out.write("// prec>=600), 256-byte little-endian teko_decimal encoding, byte-for-byte.\n")
    out.write("#ifndef TEKO_DECIMAL_KAT_VECTORS_H\n#define TEKO_DECIMAL_KAT_VECTORS_H\n")
    out.write("#include <stdint.h>\n\n")
    out.write("typedef struct {\n")
    out.write("    int      op;          // TEKO_DEC_OP_*\n")
    out.write("    uint8_t  a[256];\n")
    out.write("    uint8_t  b[256];\n")
    out.write("    int      expect_ok;   // 1 = op returns 1 (and result matches); 0 = fail-loud\n")
    out.write("    uint8_t  result[256]; // expected 256-byte encoding when expect_ok (else zeros)\n")
    out.write("    const char* label;\n")
    out.write("} teko_decimal_kat;\n\n")

    def barr(b):
        return "{" + ",".join(str(x) for x in b) + "}"

    out.write("static const teko_decimal_kat TEKO_DECIMAL_KATS[] = {\n")
    n_ok = 0
    n_fail = 0
    for (op, a, b, ok, res) in VECTORS:
        ae = a.encode()
        be = b.encode()
        if ok:
            re = res.encode()
            n_ok += 1
        else:
            re = bytes(256)
            n_fail += 1
        label = "%s s%d/s%d" % (OP_NAME[op], a.scale, b.scale)
        out.write("  { %d, %s, %s, %d, %s, \"%s\" },\n" %
                  (op, barr(ae), barr(be), 1 if ok else 0, barr(re), label))
    out.write("};\n\n")
    out.write("#define TEKO_DECIMAL_KAT_COUNT (sizeof(TEKO_DECIMAL_KATS)/sizeof(TEKO_DECIMAL_KATS[0]))\n\n")

    # C string literal escaping for the parse inputs (whitespace, quotes, backslash, non-print).
    def cstr(s):
        r = ['"']
        for ch in s:
            o = ord(ch)
            if ch == '\\': r.append('\\\\')
            elif ch == '"': r.append('\\"')
            elif ch == '\t': r.append('\\t')
            elif ch == '\n': r.append('\\n')
            elif ch == '\r': r.append('\\r')
            elif ch == '\f': r.append('\\f')
            elif ch == '\v': r.append('\\v')
            elif 32 <= o < 127: r.append(ch)
            else: r.append('\\x%02x' % o)
        r.append('"')
        return ''.join(r)

    # ----- 17.F.2 FORMAT vectors: {256B input -> expected scale-preserving string} -----
    out.write("// 17.F.2 — FORMAT KATs: teko_decimal_to_string(d) == str (scale-preserving plain\n")
    out.write("// `.`-decimal; the expected string is built by the generator, NEVER Python str()).\n")
    out.write("typedef struct {\n")
    out.write("    uint8_t     d[256];\n")
    out.write("    const char* str;     // expected NUL-terminated output\n")
    out.write("} teko_decimal_fmt_kat;\n\n")
    out.write("static const teko_decimal_fmt_kat TEKO_DECIMAL_FMT_KATS[] = {\n")
    for (d, sstr) in FMT_VECTORS:
        out.write("  { %s, %s },\n" % (barr(d.encode()), cstr(sstr)))
    out.write("};\n")
    out.write("#define TEKO_DECIMAL_FMT_KAT_COUNT "
              "(sizeof(TEKO_DECIMAL_FMT_KATS)/sizeof(TEKO_DECIMAL_FMT_KATS[0]))\n\n")

    # ----- 17.F.2 PARSE vectors: {string -> expect_ok + expected 256B} -----
    out.write("// 17.F.2 — PARSE KATs: teko_decimal_parse(str,&out) ok-flag == expect_ok AND (on ok)\n")
    out.write("// memcmp(out, result, 256) == 0. Includes reject forms + >38-frac rounding + round-trips.\n")
    out.write("typedef struct {\n")
    out.write("    const char* str;\n")
    out.write("    int         expect_ok;\n")
    out.write("    uint8_t     result[256]; // expected encoding when expect_ok (else zeros)\n")
    out.write("} teko_decimal_parse_kat;\n\n")
    out.write("static const teko_decimal_parse_kat TEKO_DECIMAL_PARSE_KATS[] = {\n")
    n_pok = 0
    n_pfail = 0
    for (sstr, ok, res) in PARSE_VECTORS:
        if ok:
            re = res.encode()
            n_pok += 1
        else:
            re = bytes(256)
            n_pfail += 1
        out.write("  { %s, %d, %s },\n" % (cstr(sstr), 1 if ok else 0, barr(re)))
    out.write("};\n")
    out.write("#define TEKO_DECIMAL_PARSE_KAT_COUNT "
              "(sizeof(TEKO_DECIMAL_PARSE_KATS)/sizeof(TEKO_DECIMAL_PARSE_KATS[0]))\n\n")

    out.write("#endif // TEKO_DECIMAL_KAT_VECTORS_H\n")
    sys.stderr.write("generated %d arith vectors (%d ok, %d expect_fail)\n" %
                     (len(VECTORS), n_ok, n_fail))
    sys.stderr.write("generated %d format vectors\n" % len(FMT_VECTORS))
    sys.stderr.write("generated %d parse vectors (%d ok, %d expect_fail)\n" %
                     (len(PARSE_VECTORS), n_pok, n_pfail))

if __name__ == "__main__":
    main()
