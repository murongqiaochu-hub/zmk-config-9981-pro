/* User setting -> perceptual LED limit; zero is truly off.
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdint.h>

static inline uint8_t indicator_user_limit(uint8_t setting) {
    unsigned value = setting > 100U ? 100U : setting;
    if (value == 0U) { return 0; }
    unsigned level = (value * value + 50U) / 100U;
    return level == 0U ? 1U : (uint8_t)level;
}

/* Pattern remains 0..100%; its peak cannot exceed the user's limit. */
static inline uint8_t indicator_pattern_level(uint8_t pattern, uint8_t setting) {
    unsigned level = pattern > 100U ? 100U : pattern;
    return (uint8_t)((level * indicator_user_limit(setting) + 50U) / 100U);
}
