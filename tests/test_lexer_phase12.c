#include "unity.h"
#include "../src/lexer.h"
#include <string.h>

// Lex the first token of `src` and return its type.
static TokenType tok1(const char* src) {
    Lexer lex;
    lexer_init(&lex, src);
    Token t = lexer_next_token(&lex);
    return t.type;
}

// Lex the first token of `src` and return the whole token.
static Token first(const char* src) {
    Lexer lex;
    lexer_init(&lex, src);
    return lexer_next_token(&lex);
}

// Phase 12 P12-A: the reserved keyword matrix lexes to dedicated tokens, and the
// keywords[] sentinel-ordering bug is fixed (so `required` is matched again).
void test_phase12_reserved_keywords(void) {
    // Regression for the {NULL,…}-before-"required" sentinel bug.
    TEST_ASSERT_EQUAL_INT(TOKEN_REQD, tok1("required"));

    // Resilience
    TEST_ASSERT_EQUAL_INT(TOKEN_CIRCUIT, tok1("circuit"));
    TEST_ASSERT_EQUAL_INT(TOKEN_FALLBACK, tok1("fallback"));
    TEST_ASSERT_EQUAL_INT(TOKEN_DELAYED, tok1("delayed"));
    TEST_ASSERT_EQUAL_INT(TOKEN_RETRY, tok1("retry"));
    TEST_ASSERT_EQUAL_INT(TOKEN_EXPONENTIAL, tok1("exponential"));
    TEST_ASSERT_EQUAL_INT(TOKEN_LOGARITHMIC, tok1("logarithmic"));
    TEST_ASSERT_EQUAL_INT(TOKEN_ATTEMPTS, tok1("attempts"));
    TEST_ASSERT_EQUAL_INT(TOKEN_TIMEOUT, tok1("timeout"));

    // OOP & concurrency
    TEST_ASSERT_EQUAL_INT(TOKEN_CLASS, tok1("class"));
    TEST_ASSERT_EQUAL_INT(TOKEN_ABSTRACT, tok1("abstract"));
    TEST_ASSERT_EQUAL_INT(TOKEN_TRAIT, tok1("trait"));
    TEST_ASSERT_EQUAL_INT(TOKEN_EVENT, tok1("event"));
    TEST_ASSERT_EQUAL_INT(TOKEN_RAISE, tok1("raise"));
    TEST_ASSERT_EQUAL_INT(TOKEN_SUBSCRIBE, tok1("subscribe"));
    TEST_ASSERT_EQUAL_INT(TOKEN_FANOUT, tok1("fanout"));
    TEST_ASSERT_EQUAL_INT(TOKEN_FIRE_AND_FORGET, tok1("fire_and_forget"));
    TEST_ASSERT_EQUAL_INT(TOKEN_SHARED, tok1("shared"));
    TEST_ASSERT_EQUAL_INT(TOKEN_ATOMIC, tok1("atomic"));
    TEST_ASSERT_EQUAL_INT(TOKEN_ROUTINES, tok1("routines"));
    TEST_ASSERT_EQUAL_INT(TOKEN_DUPLEX, tok1("duplex"));

    // Web
    TEST_ASSERT_EQUAL_INT(TOKEN_API, tok1("api"));
    TEST_ASSERT_EQUAL_INT(TOKEN_MIDDLEWARE, tok1("middleware"));
    TEST_ASSERT_EQUAL_INT(TOKEN_GET, tok1("get"));
    TEST_ASSERT_EQUAL_INT(TOKEN_POST, tok1("post"));
    TEST_ASSERT_EQUAL_INT(TOKEN_PUT, tok1("put"));
    TEST_ASSERT_EQUAL_INT(TOKEN_DELETE, tok1("delete"));
    TEST_ASSERT_EQUAL_INT(TOKEN_RPC, tok1("rpc"));
    TEST_ASSERT_EQUAL_INT(TOKEN_WEBSOCKET, tok1("websocket"));

    // Tooling
    TEST_ASSERT_EQUAL_INT(TOKEN_PARSE, tok1("parse"));
    TEST_ASSERT_EQUAL_INT(TOKEN_JSON, tok1("json"));
    TEST_ASSERT_EQUAL_INT(TOKEN_CSV, tok1("csv"));
    TEST_ASSERT_EQUAL_INT(TOKEN_XML, tok1("xml"));
    TEST_ASSERT_EQUAL_INT(TOKEN_HTML, tok1("html"));
    TEST_ASSERT_EQUAL_INT(TOKEN_BUNDLE, tok1("bundle"));
    TEST_ASSERT_EQUAL_INT(TOKEN_MINIFY, tok1("minify"));
    TEST_ASSERT_EQUAL_INT(TOKEN_CRYPTO, tok1("crypto"));
    TEST_ASSERT_EQUAL_INT(TOKEN_HASH, tok1("hash"));
    TEST_ASSERT_EQUAL_INT(TOKEN_ENCRYPT, tok1("encrypt"));

    // Core
    TEST_ASSERT_EQUAL_INT(TOKEN_COMPTIME, tok1("comptime"));
    TEST_ASSERT_EQUAL_INT(TOKEN_SOA, tok1("soa"));

    // Symmetry audit (P12 1A): crypto counterpart + base-encoding surface.
    TEST_ASSERT_EQUAL_INT(TOKEN_DECRYPT, tok1("decrypt"));
    TEST_ASSERT_EQUAL_INT(TOKEN_ENCODE, tok1("encode"));
    TEST_ASSERT_EQUAL_INT(TOKEN_DECODE, tok1("decode"));
    TEST_ASSERT_EQUAL_INT(TOKEN_BASE64, tok1("base64"));
    TEST_ASSERT_EQUAL_INT(TOKEN_BASE32, tok1("base32"));
    TEST_ASSERT_EQUAL_INT(TOKEN_HEX, tok1("hex"));

    // Reserved-with-target tokens (no dead tokens): sign/verify → Phase 13,
    // serialize/stringify → Phase 18, patch/head/options → Phase 17.
    TEST_ASSERT_EQUAL_INT(TOKEN_SIGN, tok1("sign"));
    TEST_ASSERT_EQUAL_INT(TOKEN_VERIFY, tok1("verify"));
    TEST_ASSERT_EQUAL_INT(TOKEN_SERIALIZE, tok1("serialize"));
    TEST_ASSERT_EQUAL_INT(TOKEN_STRINGIFY, tok1("stringify"));
    TEST_ASSERT_EQUAL_INT(TOKEN_PATCH, tok1("patch"));
    TEST_ASSERT_EQUAL_INT(TOKEN_HEAD, tok1("head"));
    TEST_ASSERT_EQUAL_INT(TOKEN_OPTIONS, tok1("options"));

    // Pre-existing keywords still resolve, and a non-keyword stays an identifier.
    TEST_ASSERT_EQUAL_INT(TOKEN_FN, tok1("fn"));
    TEST_ASSERT_EQUAL_INT(TOKEN_RETURN, tok1("return"));
    TEST_ASSERT_EQUAL_INT(TOKEN_IDENTIFIER, tok1("not_a_keyword_xyz"));
}

// Phase 12 P12-B: native literal unit suffixes, longest-match, lexeme excludes suffix.
void test_phase12_literal_suffixes(void) {
    Token t;

    // Time
    t = first("500ms"); TEST_ASSERT_EQUAL_INT(TOKEN_LIT_INT, t.type);
    TEST_ASSERT_EQUAL_INT(LIT_UNIT_MS, t.literal_unit);
    TEST_ASSERT_EQUAL_STRING("500", t.lexeme);          // suffix not in lexeme
    TEST_ASSERT_EQUAL_INT(LIT_UNIT_S, first("5s").literal_unit);
    TEST_ASSERT_EQUAL_INT(LIT_UNIT_M, first("2m").literal_unit);   // minutes, not "ms"
    TEST_ASSERT_EQUAL_INT(LIT_UNIT_H, first("1h").literal_unit);
    TEST_ASSERT_EQUAL_INT(LIT_UNIT_D, first("7d").literal_unit);

    // Data — longest-match: "kb"/"mb"/"gb" win over "b"
    TEST_ASSERT_EQUAL_INT(LIT_UNIT_B, first("8b").literal_unit);
    TEST_ASSERT_EQUAL_INT(LIT_UNIT_KB, first("3kb").literal_unit);
    TEST_ASSERT_EQUAL_INT(LIT_UNIT_MB, first("2mb").literal_unit);
    TEST_ASSERT_EQUAL_INT(LIT_UNIT_GB, first("1gb").literal_unit);

    // Bandwidth — longest-match: "kbps" wins over "kb"
    TEST_ASSERT_EQUAL_INT(LIT_UNIT_KBPS, first("10kbps").literal_unit);
    TEST_ASSERT_EQUAL_INT(LIT_UNIT_MBPS, first("20mbps").literal_unit);
    TEST_ASSERT_EQUAL_INT(LIT_UNIT_GBPS, first("1gbps").literal_unit);

    // Float carries a unit too.
    t = first("1.5s"); TEST_ASSERT_EQUAL_INT(TOKEN_LIT_FLOAT, t.type);
    TEST_ASSERT_EQUAL_INT(LIT_UNIT_S, t.literal_unit);
    TEST_ASSERT_EQUAL_STRING("1.5", t.lexeme);

    // Bare number → no unit.
    t = first("42"); TEST_ASSERT_EQUAL_INT(LIT_UNIT_NONE, t.literal_unit);

    // A non-unit trailing run is NOT consumed: the number lexes unit-less and the
    // run becomes a separate identifier token.
    Lexer lex; lexer_init(&lex, "5xyz");
    Token n = lexer_next_token(&lex);
    TEST_ASSERT_EQUAL_INT(TOKEN_LIT_INT, n.type);
    TEST_ASSERT_EQUAL_INT(LIT_UNIT_NONE, n.literal_unit);
    TEST_ASSERT_EQUAL_STRING("5", n.lexeme);
    Token id = lexer_next_token(&lex);
    TEST_ASSERT_EQUAL_INT(TOKEN_IDENTIFIER, id.type);
    TEST_ASSERT_EQUAL_STRING("xyz", id.lexeme);
}
