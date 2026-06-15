#ifndef TEKO_BROADCAST_H
#define TEKO_BROADCAST_H

#include <stdint.h>

// Phase 14 (14.D) — broadcast channel: a non-destructive 1:N pub-sub. A publish writes a value
// ONCE into a shared ring; each subscriber holds its own read cursor and reads independently, so
// every subscriber observes every value published after it subscribed (the register-based 1:N
// pub-sub from docs/plan.md). This portable C runtime is the SINGLE SOURCE OF TRUTH for the
// `broadcast.*` surface (native teko_rt_bcast_* + the wasm32 reactor), same pattern as the
// duplex/delayed runtimes. Non-blocking: recv returns a structured status so a cooperative
// caller yields/retries on EMPTY and gets a definite CLOSED instead of hanging.
//
// Subscribers see values published AFTER they subscribe (cursor starts at the current write
// position). The ring is bounded and OVERWRITING: a subscriber that lags more than `capacity`
// behind the writer skips the lost values (recv reports LAGGED once, then resumes).

typedef enum {
    TEKO_BC_OK     = 0, // a value was delivered
    TEKO_BC_EMPTY  = 1, // caught up to the writer (nothing new) — yield/retry
    TEKO_BC_CLOSED = 2, // closed + caught up (structured end-of-stream)
    TEKO_BC_LAGGED = 3, // this subscriber fell > capacity behind; lost values were skipped
    TEKO_BC_BADARG = 4  // NULL channel / bad subscriber id
} TekoBroadcastStatus;

typedef struct TekoBroadcast TekoBroadcast;

TekoBroadcast* teko_broadcast_open(uint32_t capacity);
void           teko_broadcast_free(TekoBroadcast* b);

// Register a subscriber; returns its id (>= 0), or -1 if the subscriber table is full, -2 if
// the channel is closed. Its cursor starts at the current write position (future-only).
int teko_broadcast_subscribe(TekoBroadcast* b);

// Publish a value to all subscribers (writes once). CLOSED if closed, BADARG on NULL.
TekoBroadcastStatus teko_broadcast_publish(TekoBroadcast* b, int32_t value);

// Deliver the next value for subscriber `sub_id` into *out. OK / EMPTY / CLOSED / LAGGED.
TekoBroadcastStatus teko_broadcast_recv(TekoBroadcast* b, int sub_id, int32_t* out_value);

// Non-consuming probe for subscriber `sub_id`: OK if a value is available, else EMPTY/CLOSED.
TekoBroadcastStatus teko_broadcast_poll(const TekoBroadcast* b, int sub_id);

void teko_broadcast_close(TekoBroadcast* b);

#define TEKO_BCAST_MAX_CAP  64u // ring capacity (bounded)
#define TEKO_BCAST_MAX_SUBS 16u // max concurrent subscribers

#endif // TEKO_BROADCAST_H
