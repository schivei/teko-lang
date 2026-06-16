#ifndef TEKO_OBJECT_H
#define TEKO_OBJECT_H

// Phase 15 (15.A) — bare-metal object model: a handle-based field-cell store backing the
// `class` surface. This portable C runtime is the SINGLE SOURCE OF TRUTH for object instances
// (native teko_rt_object_* wrappers + the wasm32 reactor), the same pattern as the Phase 14
// channel runtimes.
//
// ZERO RUNTIME REFLECTION: an object is just a fixed-size vector of register-width cells. There
// is NO name->field lookup, NO type tag, NO attribute bag at runtime — field *indices* are
// resolved at COMPILE TIME from the class declaration, and a method is a plain function that
// takes the object handle. The store only allocates the cells and reads/writes them by index.
//
// Cells are `long` (register width), NOT int32: a field may hold a plain integer OR another
// object's handle (a pointer, 64-bit on native / 32-bit on wasm32). `long` holds either on both
// targets without truncation, matching the duplex/atomic handle ABI.

typedef struct TekoObject TekoObject;

// Allocate an object with `nfields` zero-initialized cells (nfields clamped to [0, MAX]).
// Returns NULL on allocation failure. Free with teko_object_free.
TekoObject* teko_object_new(int nfields);
void        teko_object_free(TekoObject* o);

// Field access by COMPILE-TIME index. Out-of-range / NULL are defensive no-ops (set) / 0 (get) —
// never crash (the compiler always emits valid indices; this just hardens the boundary).
void        teko_object_set(TekoObject* o, int idx, long value);
long        teko_object_get(const TekoObject* o, int idx);
int         teko_object_nfields(const TekoObject* o); // 0 if NULL

#define TEKO_OBJECT_MAX_FIELDS 256 // bounded field count (allocation-free upper bound)

#endif // TEKO_OBJECT_H
