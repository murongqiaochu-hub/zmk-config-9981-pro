/* SPDX-License-Identifier: MIT
 * HOST-ONLY protocol experiment, not linked into the production firmware.
 * Wire layout reference: jfedor2/portable-gamepad-firmware main.c at
 * aa09d1d05939ab0025107f6df359e0b8a3b714c0 (report_stadia_t/fill_out_report).
 * This is a byte-oriented adapter for the existing 9981 report state, not a
 * device identity, GATT implementation, or claim of iPadOS compatibility.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "../../config/boards/arm/bbp9981/custom_driver/gamepad_report.h"

#define BBP_EXPERIMENT_STADIA_BODY_SIZE 10U

/* Explicit bytes avoid compiler-dependent C bitfield wire layout.
 * Invalid input fails closed to a neutral report, without manufacturing input.
 */
static inline bool bbp_experiment_stadia_encode(const struct bbp_gamepad_report_body *in,
                                               uint8_t out[BBP_EXPERIMENT_STADIA_BODY_SIZE]) {
    if (!out) { return false; }
    static const uint8_t neutral[BBP_EXPERIMENT_STADIA_BODY_SIZE] =
        {15U, 0U, 0U, 128U, 128U, 128U, 128U, 0U, 0U, 0U};
    for (unsigned int i = 0; i < BBP_EXPERIMENT_STADIA_BODY_SIZE; ++i) { out[i] = neutral[i]; }
    if (!in || in->hat > BBP_GAMEPAD_HAT_NEUTRAL || in->x == INT8_MIN ||
        in->y == INT8_MIN || in->z == INT8_MIN || in->rz == INT8_MIN) { return false; }
    uint16_t buttons = (uint16_t)in->buttons_lo | ((uint16_t)in->buttons_hi << 8);
    /* Input bits: A,B,X,Y,LB,RB,View,Menu,Guide,L3,R3.
     * LT/RT digital bits follow the transported axes, not redundant input bits.
     */
    static const uint8_t from[] = {0, 1, 3, 4, 6, 7, 10, 11, 12, 13, 14};
    static const uint8_t to[] =   {14,13,12,11,10,9, 6,  5,  4,  8,  7};
    uint16_t packed = 0U;
    for (unsigned int i = 0; i < sizeof(from); ++i) {
        if (buttons & (UINT16_C(1) << from[i])) { packed |= UINT16_C(1) << to[i]; }
    }
    if (in->lt > 64U) { packed |= UINT16_C(1) << 2; }
    if (in->rt > 64U) { packed |= UINT16_C(1) << 3; }
    out[0] = in->hat == BBP_GAMEPAD_HAT_NEUTRAL ? 15U : in->hat;
    out[1] = (uint8_t)packed;
    out[2] = (uint8_t)(packed >> 8);
    out[3] = (uint8_t)((int)in->x + 128);
    out[4] = (uint8_t)((int)in->y + 128);
    out[5] = (uint8_t)((int)in->z + 128);
    out[6] = (uint8_t)((int)in->rz + 128);
    out[7] = in->lt;
    out[8] = in->rt;
    /* capture, assistant, reserved and extra buttons remain absent. */
    return true;
}
