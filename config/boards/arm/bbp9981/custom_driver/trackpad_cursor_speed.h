/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Ordinary mouse gain only. All gains use the existing pointer denominator 400. */
#define BBP_CURSOR_SPEED_DENOMINATOR 400U
#define BBP_CURSOR_SPEED_COUNT 5U
#define BBP_CURSOR_SPEED_DEFAULT_INDEX 2U
#define BBP_CURSOR_SPEED_INDEX_MASK UINT32_C(7)
#define BBP_CURSOR_SPEED_GENERATION_STEP UINT32_C(8)

/* Pure helpers: this header deliberately has no Zephyr/ZMK dependency. */
static inline uint8_t bbp_cursor_speed_snapshot_index(uint32_t snapshot) {
    return (uint8_t)(snapshot & BBP_CURSOR_SPEED_INDEX_MASK);
}

static inline uint32_t bbp_cursor_speed_snapshot_generation(uint32_t snapshot) {
    return snapshot >> 3U;
}

static inline uint16_t bbp_cursor_speed_table_gain(uint8_t index, bool slow) {
    static const uint16_t gains[BBP_CURSOR_SPEED_COUNT] = {240U, 360U, 480U, 600U, 720U};
    if (index >= BBP_CURSOR_SPEED_COUNT) {
        index = BBP_CURSOR_SPEED_DEFAULT_INDEX;
    }
    return (uint16_t)(gains[index] / (slow ? 2U : 1U));
}

/* Atomic index (bits 0..2) + generation (bits 3..31). Each user cycle advances
 * generation, including five cycles returning to the same index. Generation
 * wraps only after 2^29 changes; a changed settings restore also advances it.
 * Getters are read-only and do not initialize work, load or save settings.
 */
uint32_t bbp_cursor_speed_snapshot(void);
uint16_t bbp_cursor_speed_gain(bool slow);

/* One non-reentrant A320 poll is the sole motion-frame writer. Capture once
 * BEFORE its epoch checks; actual cursor and RAW tap shadow then read the same
 * immutable frame gain even if a user/settings writer runs during I2C/output.
 * Such a new selection takes effect at the next complete sensor poll.
 */
uint32_t bbp_cursor_speed_begin_motion_frame(void);
uint16_t bbp_cursor_speed_motion_gain(bool slow);

/* Immediately updates RAM; persistence, when CONFIG_SETTINGS=y, is debounced.
 * Returns 0. CONFIG_SETTINGS=n is explicitly volatile and never writes flash.
 */
int bbp_cursor_speed_cycle(void);
