#include "teko_broadcast.h"
#include <stdlib.h>

// Phase 14 (14.D) — broadcast channel runtime. See teko_broadcast.h. A bounded overwriting ring
// of published values keyed by a monotonic write sequence, plus one read cursor per subscriber.
// publish appends once; each subscriber advances its own cursor, so all subscribers observe
// every value (non-destructive 1:N). Pure C → native + the wasm reactor.

struct TekoBroadcast {
    int32_t  ring[TEKO_BCAST_MAX_CAP];
    uint32_t cap;
    uint64_t write_seq;                  // total values ever published
    uint64_t cursor[TEKO_BCAST_MAX_SUBS]; // per-subscriber next-to-read sequence
    uint32_t sub_count;
    int      closed;
};

TekoBroadcast* teko_broadcast_open(uint32_t capacity) {
    TekoBroadcast* b = (TekoBroadcast*)malloc(sizeof(TekoBroadcast));
    if (!b) return NULL;
    b->cap = (capacity == 0) ? 1u : (capacity > TEKO_BCAST_MAX_CAP ? TEKO_BCAST_MAX_CAP : capacity);
    b->write_seq = 0;
    b->sub_count = 0;
    b->closed = 0;
    return b;
}

void teko_broadcast_free(TekoBroadcast* b) { free(b); }

int teko_broadcast_subscribe(TekoBroadcast* b) {
    if (!b) return -1;
    if (b->closed) return -2;
    if (b->sub_count >= TEKO_BCAST_MAX_SUBS) return -1;
    int id = (int)b->sub_count++;
    b->cursor[id] = b->write_seq; // see values published from now on
    return id;
}

TekoBroadcastStatus teko_broadcast_publish(TekoBroadcast* b, int32_t value) {
    if (!b) return TEKO_BC_BADARG;
    if (b->closed) return TEKO_BC_CLOSED;
    b->ring[b->write_seq % b->cap] = value; // write once; overwrites the oldest if it wrapped
    b->write_seq++;
    return TEKO_BC_OK;
}

TekoBroadcastStatus teko_broadcast_recv(TekoBroadcast* b, int sub_id, int32_t* out_value) {
    if (!b || !out_value || sub_id < 0 || (uint32_t)sub_id >= b->sub_count) return TEKO_BC_BADARG;
    uint64_t c = b->cursor[sub_id];
    if (c >= b->write_seq) return b->closed ? TEKO_BC_CLOSED : TEKO_BC_EMPTY; // caught up
    // Overwritten? If the cursor is more than `cap` behind, the oldest kept value is
    // write_seq - cap; lost values are skipped and we report LAGGED once.
    if (b->write_seq - c > b->cap) {
        b->cursor[sub_id] = b->write_seq - b->cap;
        return TEKO_BC_LAGGED;
    }
    *out_value = b->ring[b->cursor[sub_id] % b->cap];
    b->cursor[sub_id]++;
    return TEKO_BC_OK;
}

TekoBroadcastStatus teko_broadcast_poll(const TekoBroadcast* b, int sub_id) {
    if (!b || sub_id < 0 || (uint32_t)sub_id >= b->sub_count) return TEKO_BC_BADARG;
    uint64_t c = b->cursor[sub_id];
    if (c >= b->write_seq) return b->closed ? TEKO_BC_CLOSED : TEKO_BC_EMPTY;
    if (b->write_seq - c > b->cap) return TEKO_BC_LAGGED;
    return TEKO_BC_OK;
}

void teko_broadcast_close(TekoBroadcast* b) { if (b) b->closed = 1; }
