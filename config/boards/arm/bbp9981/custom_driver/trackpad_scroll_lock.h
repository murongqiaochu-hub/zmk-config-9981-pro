/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define TP_LEFT_SHOULDER_POSITION 0u
#define TP_RIGHT_SHOULDER_POSITION 6u
#define TP_SCROLL_SHOULDER_LEFT_BIT 1u
#define TP_SCROLL_SHOULDER_RIGHT_BIT 2u

/** Effective lock: latched scrolling, temporarily overridden while aA is held. */
bool tp_scroll_is_locked(void);

/** True while a locked-scroll shoulder modifier is held in the aA layer. */
bool tp_scroll_shoulder_slow_active(void);

/** Update a locked-scroll shoulder modifier owner. */
void tp_scroll_shoulder_modifier_update(uint32_t position, bool pressed);
