/* Host test is deterministic; no substitute for real SMP verification. */
#pragma once
typedef long atomic_t;
#define ATOMIC_INIT(value) (value)
static inline long atomic_get(const atomic_t *value) { return *value; }
static inline long atomic_inc(atomic_t *value) { return (*value)++; }
static inline void atomic_set(atomic_t *value, long v) { *value = v; }
