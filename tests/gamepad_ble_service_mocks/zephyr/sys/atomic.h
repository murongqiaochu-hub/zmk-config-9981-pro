/* SPDX-License-Identifier: MIT -- C11 atomic bit model, host tests only. */
#pragma once
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
typedef atomic_uint atomic_t;
#define ATOMIC_DEFINE(name, bits) atomic_t name[((bits) + 31) / 32]
static inline bool atomic_test_bit(const atomic_t *a, int bit) {
    return (atomic_load(&a[bit / 32]) & (UINT32_C(1) << (bit % 32))) != 0;
}
static inline void atomic_set_bit(atomic_t *a, int bit) {
    atomic_fetch_or(&a[bit / 32], UINT32_C(1) << (bit % 32));
}
static inline void atomic_clear_bit(atomic_t *a, int bit) {
    atomic_fetch_and(&a[bit / 32], ~(UINT32_C(1) << (bit % 32)));
}
