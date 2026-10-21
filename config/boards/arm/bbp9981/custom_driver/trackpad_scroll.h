/* SPDX-License-Identifier: Apache-2.0
 * Pure wheel accumulator and conversion. No GPIO, clocks, work scheduling or HID.
 * Callers pass successful raw samples after the historical Ctrl truncation.
 * All thresholds, gain branches and signed rounding are the v2.2.9 policy.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "trackpad_scroll_lock.h"

struct tp_scroll_state {
    int16_t sum_dx, sum_dy;
    uint16_t sample_cnt;
    uint32_t last_read_time;
    int16_t remainder_x, remainder_y;
};

static inline void tp_scroll_reset(struct tp_scroll_state *s) {
    s->sum_dx = s->sum_dy = 0;
    s->sample_cnt = 0;
    s->last_read_time = 0;
    s->remainder_x = s->remainder_y = 0;
}

/* A signed remainder preserves odd notches across locked half-speed reports. */
static inline int16_t tp_scroll_half(int16_t value, int16_t *remainder) {
    int32_t total = (int32_t)value + *remainder;
    int16_t scaled = (int16_t)(total / 2);
    *remainder = (int16_t)(total % 2);
    return scaled;
}

static inline bool tp_scroll_push(struct tp_scroll_state *s, int16_t dx, int16_t dy,
                                  uint32_t now, uint16_t threshold, bool locked,
                                  uint8_t rotation, bool shoulder_slow,
                                  int16_t *wheel_x, int16_t *wheel_y) {
    if (dx == 0 && dy == 0) {
        s->sum_dx = 0;
        s->sum_dy = 0;
        s->sample_cnt = 0;
    } else if (now - s->last_read_time > 50) {
        s->sum_dx = 0;
        s->sum_dy = 0;
        s->sample_cnt = 0;
    }
    s->last_read_time = now;
    s->sum_dx += dx;
    s->sum_dy += dy;
    s->sample_cnt++;
    if (s->sample_cnt < threshold) { return false; }

    int16_t sx = s->sum_dx;
    int16_t sy = s->sum_dy;
    s->sum_dx = 0;
    s->sum_dy = 0;
    s->sample_cnt = 0;
    int16_t scroll_x = 0, scroll_y = 0;
    if (abs(sy) >= 128) {
        scroll_x = -sx / 24;
        scroll_y = -sy / 24;
    } else if (abs(sy) >= 64) {
        scroll_x = -sx / 16;
        scroll_y = -sy / 16;
    } else if (abs(sy) >= 32) {
        scroll_x = -sx / 12;
        scroll_y = -sy / 12;
    } else if (abs(sy) >= 21) {
        scroll_x = -sx / 8;
        scroll_y = -sy / 8;
    } else if (abs(sy) >= 3) {
        scroll_x = (sx > 0) ? -1 : (sx < 0) ? 1 : 0;
        scroll_y = (sy > 0) ? -1 : (sy < 0) ? 1 : 0;
    } else {
        scroll_x = (sx > 0) ? -1 : (sx < 0) ? 1 : 0;
        scroll_y = 0;
    }
    *wheel_x = -scroll_x;
    *wheel_y = scroll_y;
    if (locked) { tp_scroll_apply_rotation(rotation, wheel_x, wheel_y); }
    if (shoulder_slow) {
        *wheel_x = tp_scroll_half(*wheel_x, &s->remainder_x);
        *wheel_y = tp_scroll_half(*wheel_y, &s->remainder_y);
    }
    return true;
}
