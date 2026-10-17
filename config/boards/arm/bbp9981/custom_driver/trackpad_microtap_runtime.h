/* Opt-in motion gesture; NOT a verified finger-presence implementation.
 * SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef void (*tp_microtap_emit_fn)(void *ctx, int16_t dx, int16_t dy, uint32_t at_ms);
void tp_microtap_runtime_init(void);
void tp_microtap_runtime_poll(uint32_t now, int16_t dx, int16_t dy, bool valid,
                              bool scroll, uint32_t mode_epoch, uint32_t speed_epoch,
                              tp_microtap_emit_fn emit, void *ctx);
bool tp_microtap_runtime_enabled(void);
bool tp_microtap_runtime_pending(void);
void tp_microtap_runtime_toggle(void);
void tp_microtap_runtime_interrupt(void);
/* Auto-only native activity guard. Repeated false does not renew it; a true
 * level blocks continuously and true->false starts a fresh 600ms quiet window.
 * Position0/4 notifications and HID ownership checks supplement this signal.
 * Gesture resets/toggles/host changes deliberately do not clear native activity.
 * A missed/captured position-UP fails auto closed; motion/native dispatch is not
 * filtered. No motion-only heuristic can retract an auto click already sent
 * before a later native edge; this guard is not verified contact detection. */
void tp_microtap_runtime_shoulder_busy(bool busy);
