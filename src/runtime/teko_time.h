#ifndef TEKO_TIME_H
#define TEKO_TIME_H

#include <stdint.h>

// Phase 14 (wall-clock / timezone surface) — civil (wall-clock) time, OS-sourced. Distinct from
// the real MONOTONIC clock (teko_rt_now_ns, for elapsed/deadlines): this is CIVIL time — a current
// Unix timestamp + timezone-aware formatting that honors the system's local zone + DST, defaulting
// to system-local, while leaving the user free to do time math on the (string) epoch.
//
// The FORMATTING is portable C (this file — the single source of truth, deterministic + KAT-able,
// and reactor-safe: no libc time/tz calls). Only the two PLATFORM facts are sourced outside it:
//   • the current Unix timestamp (native time(); WASM host env.teko_now_unix), and
//   • the local UTC offset for an instant, DST-correct (native localtime; WASM host
//     env.teko_tz_offset) — exchanged with the OS, as the owner requires.
// The teko_rt_time_* wrappers (teko_rt.c) combine those with the formatter below.

// Days since 1970-01-01 for a civil date (proleptic Gregorian; Howard Hinnant's algorithm).
long long teko_time_days_from_civil(long y, unsigned m, unsigned d);

// Format an epoch (Unix seconds) at `offset_s` seconds from UTC as ISO-8601:
//   offset 0    -> "YYYY-MM-DDTHH:MM:SSZ"        (UTC)
//   offset != 0 -> "YYYY-MM-DDTHH:MM:SS±HH:MM"   (civil fields = epoch + offset)
// Returns a fresh heap string (caller-owned), or NULL on OOM. Years are rendered with >= 4 digits.
char* teko_time_format(long long epoch_s, long offset_s);

// The integer epoch as a fresh decimal string (e.g. for time.now_unix() — avoids i32/2038 limits).
char* teko_time_epoch_str(long long epoch_s);

// Parse a leading signed decimal integer (a teko_time_epoch_str output) back to seconds.
long long teko_time_parse(const char* s);

#endif // TEKO_TIME_H
