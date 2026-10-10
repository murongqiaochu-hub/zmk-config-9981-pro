/*
 * Copyright (c) 2025 ZitaoTech
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get cached user brightness setting (zero allowed; not used for pointer speed)
 *
 * @return uint8_t user setting, before perceptual scaling
 */
uint8_t indicator_tp_get_last_valid_brightness(void);

#ifdef __cplusplus
}
#endif
