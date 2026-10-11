/* Read-only ADBS-A320 diagnostic, NOT a tap recognizer or mouse-button source.
 * Register definitions: AV02-1859EN, 16Nov2011, pp.33-36,45,53.
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef int (*tp_probe_read_fn)(void *ctx, uint8_t reg, uint8_t *out);
struct tp_probe_cache {
    uint32_t identified_at;
    uint8_t pid, revision;
    bool checked, matched;
};
struct tp_probe_sample {
    uint32_t time;
    int error;
    int8_t contact; /* -1 UNKNOWN; never replace failed/disabled reads with UP. */
    uint8_t pid, revision, engine, fpd_control, motion, quality;
    uint16_t shutter;
    bool matched, valid;
};
static inline void tp_probe_reset(struct tp_probe_cache *cache) {
    *cache = (struct tp_probe_cache){0};
}
static inline struct tp_probe_sample tp_probe_sample(struct tp_probe_cache *cache, uint32_t now,
                                                     tp_probe_read_fn read, void *ctx) {
    struct tp_probe_sample s = {.time = now, .contact = -1};
    int err;
    if (!cache->checked || (uint32_t)(now - cache->identified_at) >= 1000U) {
        cache->checked = false;
        cache->matched = false;
        err = read(ctx, 0x00, &cache->pid);
        if (err) { s.error = err; return s; }
        s.pid = cache->pid;
        /* Do not probe any other register on an unrecognized device. */
        if (cache->pid == 0x83U) {
            err = read(ctx, 0x01, &cache->revision);
            if (err) { s.error = err; return s; }
        } else { cache->revision = 0; }
        cache->matched = cache->pid == 0x83U && cache->revision == 0x01U;
        cache->identified_at = now;
        cache->checked = true;
    }
    s.pid = cache->pid;
    s.revision = cache->revision;
    s.matched = cache->matched;
    if (!s.matched) { return s; }
    uint8_t hi = 0, lo = 0;
    const uint8_t regs[] = {0x60, 0x75, 0x02, 0x05, 0x06, 0x07};
    uint8_t *outs[] = {&s.engine, &s.fpd_control, &s.motion, &s.quality, &hi, &lo};
    for (unsigned i = 0; i < sizeof(regs); ++i) {
        err = read(ctx, regs[i], outs[i]);
        if (err) { s.error = err; cache->checked = false; return s; }
    }
    /* Shutter bytes are separate snapshots, diagnostic only; not a contact fallback. */
    s.shutter = ((uint16_t)hi << 8) | lo;
    if ((s.engine & 0x84U) != 0x84U) { return s; } /* Engine + Finger must already be ON. */
    bool gpio_high = (s.motion & 1U) != 0U;
    bool active_high = (s.fpd_control & 0x40U) != 0U;
    s.contact = gpio_high == active_high ? 1 : 0;
    s.valid = true;
    return s;
}
