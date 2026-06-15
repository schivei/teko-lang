#include "unity.h"
#include "../../src/runtime/teko_broadcast.h"

// Phase 14 (14.D) — broadcast channel runtime (source of truth). Validates non-destructive 1:N
// fan-out (every subscriber sees every value), future-only subscription, structured statuses,
// the lag/overwrite path, and close.

void test_teko_broadcast_fanout_all_see_all(void) {
    TekoBroadcast* b = teko_broadcast_open(8);
    TEST_ASSERT_NOT_NULL(b);
    int s0 = teko_broadcast_subscribe(b);
    int s1 = teko_broadcast_subscribe(b);
    int s2 = teko_broadcast_subscribe(b);
    TEST_ASSERT_EQUAL_INT(0, s0); TEST_ASSERT_EQUAL_INT(1, s1); TEST_ASSERT_EQUAL_INT(2, s2);

    TEST_ASSERT_EQUAL_INT(TEKO_BC_OK, teko_broadcast_publish(b, 10));
    TEST_ASSERT_EQUAL_INT(TEKO_BC_OK, teko_broadcast_publish(b, 20));

    int32_t v = 0;
    int subs[3] = { s0, s1, s2 };
    for (int i = 0; i < 3; i++) {
        // Each subscriber independently observes BOTH values (non-destructive).
        TEST_ASSERT_EQUAL_INT(TEKO_BC_OK, teko_broadcast_recv(b, subs[i], &v)); TEST_ASSERT_EQUAL_INT32(10, v);
        TEST_ASSERT_EQUAL_INT(TEKO_BC_OK, teko_broadcast_recv(b, subs[i], &v)); TEST_ASSERT_EQUAL_INT32(20, v);
        TEST_ASSERT_EQUAL_INT(TEKO_BC_EMPTY, teko_broadcast_recv(b, subs[i], &v)); // caught up
    }
    teko_broadcast_free(b);
}

void test_teko_broadcast_subscribe_is_future_only(void) {
    TekoBroadcast* b = teko_broadcast_open(8);
    int32_t v = 0;
    teko_broadcast_publish(b, 1);            // before anyone subscribes — lost to future subs
    int s = teko_broadcast_subscribe(b);
    teko_broadcast_publish(b, 2);            // this one the subscriber sees
    TEST_ASSERT_EQUAL_INT(TEKO_BC_OK, teko_broadcast_recv(b, s, &v)); TEST_ASSERT_EQUAL_INT32(2, v);
    TEST_ASSERT_EQUAL_INT(TEKO_BC_EMPTY, teko_broadcast_recv(b, s, &v));
    teko_broadcast_free(b);
}

void test_teko_broadcast_lag_and_close(void) {
    TekoBroadcast* b = teko_broadcast_open(2);  // tiny ring to force overwrite
    int32_t v = 0;
    int s = teko_broadcast_subscribe(b);
    for (int i = 1; i <= 5; i++) teko_broadcast_publish(b, i); // writer laps the slow reader
    // The reader is now > capacity behind: recv reports LAGGED once, then resumes at the oldest
    // still-kept value (write_seq - cap = 5 - 2 = 3 → values 4 then 5).
    TEST_ASSERT_EQUAL_INT(TEKO_BC_LAGGED, teko_broadcast_recv(b, s, &v));
    TEST_ASSERT_EQUAL_INT(TEKO_BC_OK, teko_broadcast_recv(b, s, &v)); TEST_ASSERT_EQUAL_INT32(4, v);
    TEST_ASSERT_EQUAL_INT(TEKO_BC_OK, teko_broadcast_recv(b, s, &v)); TEST_ASSERT_EQUAL_INT32(5, v);
    // Close: publish rejected; a caught-up recv reports CLOSED.
    teko_broadcast_close(b);
    TEST_ASSERT_EQUAL_INT(TEKO_BC_CLOSED, teko_broadcast_publish(b, 6));
    TEST_ASSERT_EQUAL_INT(TEKO_BC_CLOSED, teko_broadcast_recv(b, s, &v));
    teko_broadcast_free(b);
}

void test_teko_broadcast_badarg(void) {
    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(TEKO_BC_BADARG, teko_broadcast_publish(NULL, 1));
    TEST_ASSERT_EQUAL_INT(TEKO_BC_BADARG, teko_broadcast_recv(NULL, 0, &v));
    TekoBroadcast* b = teko_broadcast_open(4);
    TEST_ASSERT_EQUAL_INT(TEKO_BC_BADARG, teko_broadcast_recv(b, 0, &v)); // no such subscriber
    teko_broadcast_free(b);
}
