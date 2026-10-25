/* SPDX-License-Identifier: MIT -- caller holds its short lock; no SDK calls. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define IPDIAG_CAPACITY 64U
#define IPDIAG_BUILD "ipad_enum_diag1_20261025"
struct ipdiag_record {
    uint32_t t, seq, epoch;
    int32_t a, b, d, e, f, g;
    uint8_t kind, conn;
};
struct ipdiag_ring {
    struct ipdiag_record records[IPDIAG_CAPACITY];
    uint32_t sequence, dropped;
    unsigned head, count;
};
static inline bool ipdiag_push(struct ipdiag_ring *q, struct ipdiag_record record) {
    record.seq = ++q->sequence;
    if (q->count == IPDIAG_CAPACITY) { q->dropped++; return false; }
    q->records[(q->head + q->count) % IPDIAG_CAPACITY] = record;
    q->count++;
    return true;
}
static inline bool ipdiag_pop(struct ipdiag_ring *q, struct ipdiag_record *record) {
    if (!q->count) { return false; }
    *record = q->records[q->head];
    q->head = (q->head + 1U) % IPDIAG_CAPACITY;
    q->count--;
    return true;
}
struct ipdiag_rate {
    uint32_t calls, errors, last;
    bool seen;
};
static inline bool ipdiag_sample(struct ipdiag_rate *r, uint32_t now, int result) {
    r->calls++;
    if (result) { r->errors++; }
    /* Even persistent errors are bounded to one sample/second/report/connection. */
    if (r->seen && (uint32_t)(now - r->last) < 1000U) { return false; }
    r->seen = true;
    r->last = now;
    return true;
}
