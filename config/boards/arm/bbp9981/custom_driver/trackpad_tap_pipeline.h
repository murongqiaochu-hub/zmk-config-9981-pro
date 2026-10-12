/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "trackpad_tap.h"
struct tp_tap_verified_contact {
    bool protocol_verified,valid;
    enum tp_tap_contact contact;
    uint32_t observed_at,source_epoch;
};
/* An adapter must prove protocol/identity, status validity and actual fresh reads.
 * Motion GPIO, no frames, stale packet cache, and I/O error are NOT contact UP.
 * Weak default provider below is deliberately UNKNOWN; no guessing hardware. */
struct tp_tap_verified_contact tp_tap_contact_provider_read(uint32_t now);
void tp_tap_pipeline_init(void);
void tp_tap_pipeline_poll(uint32_t now,int32_t raw_dx,int32_t raw_dy,bool motion_fault,
                          bool scroll_mode,uint32_t mode_epoch);
struct tp_tap_result tp_tap_pipeline_status(void);
