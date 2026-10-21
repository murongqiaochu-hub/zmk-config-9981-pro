/* SPDX-License-Identifier: MIT
 * Input-mode publication, independent of sensor polling and LED rendering.
 * Read each snapshot at its original consumption point; these are cancellation
 * epochs, not a transaction across the independently published fields.
 */
#pragma once

#include <stdint.h>

uint32_t tp_mode_scroll_snapshot(void);
uint32_t tp_mode_speed_snapshot(void);
uint32_t tp_mode_shoulder_snapshot(void);
/* Match the sensor's original successful-init reset: shoulder modifiers only. */
void tp_mode_init(void);
