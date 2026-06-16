#include "unity.h"
#include "../../src/runtime/teko_time.h"
#include <stdlib.h>
#include <string.h>

// Phase 14 (wall-clock / timezone surface) — the portable civil-time formatter (source of truth).
// Fully deterministic: epoch + offset -> ISO-8601 is pure integer math (the OS only supplies the
// current timestamp + the local offset at runtime, which these tests pass explicitly). KAT vectors.

static void expect_fmt(long long epoch, long off, const char* want) {
    char* s = teko_time_format(epoch, off);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_STRING(want, s);
    free(s);
}

void test_teko_time_format_utc(void) {
    expect_fmt(0, 0, "1970-01-01T00:00:00Z");                 // the epoch
    expect_fmt(1000000000, 0, "2001-09-09T01:46:40Z");        // 1e9 seconds
    expect_fmt(1577836800, 0, "2020-01-01T00:00:00Z");        // 2020-01-01 midnight UTC
    expect_fmt(-1, 0, "1969-12-31T23:59:59Z");                // one second before the epoch
}

void test_teko_time_format_offset(void) {
    expect_fmt(0, -10800, "1969-12-31T21:00:00-03:00");       // UTC-3 (e.g. America/Sao_Paulo, no DST)
    expect_fmt(0, 19800, "1970-01-01T05:30:00+05:30");        // UTC+5:30 (India)
    expect_fmt(1000000000, 3600, "2001-09-09T02:46:40+01:00"); // UTC+1
}

void test_teko_time_days_from_civil_roundtrip(void) {
    TEST_ASSERT_EQUAL_INT64(0, teko_time_days_from_civil(1970, 1, 1));
    TEST_ASSERT_EQUAL_INT64(-1, teko_time_days_from_civil(1969, 12, 31));
    // 1000000000 / 86400 = 11574 (floor), so 2001-09-09 is day 11574.
    TEST_ASSERT_EQUAL_INT64(11574, teko_time_days_from_civil(2001, 9, 9));
    TEST_ASSERT_EQUAL_INT64(18262, teko_time_days_from_civil(2020, 1, 1)); // 1577836800/86400
}

void test_teko_time_epoch_str_and_parse(void) {
    char* s = teko_time_epoch_str(1000000000);
    TEST_ASSERT_EQUAL_STRING("1000000000", s); free(s);
    s = teko_time_epoch_str(0); TEST_ASSERT_EQUAL_STRING("0", s); free(s);
    s = teko_time_epoch_str(-42); TEST_ASSERT_EQUAL_STRING("-42", s); free(s);
    TEST_ASSERT_EQUAL_INT64(1000000000, teko_time_parse("1000000000"));
    TEST_ASSERT_EQUAL_INT64(-42, teko_time_parse("-42"));
    TEST_ASSERT_EQUAL_INT64(0, teko_time_parse("0"));
}
