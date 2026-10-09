/*
 * Copyright (c) 2023 ZitaoTech
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef A320_0x57_H
#define A320_0x57_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/**
 * @brief check if the touchpad is touched
 *
 * @return true if touched
 */
bool tp_is_touched(void);

/** Scroll mode while aA is held or latched by an aA double tap. */
bool tp_scroll_mode_active(void);

/** Physical aA state, independent of sticky Layer 2 and scroll locking. */
bool tp_aa_is_pressed(void);

#ifdef __cplusplus
}
#endif

#endif // A320__0x57_H
