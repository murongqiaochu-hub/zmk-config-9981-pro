/* SPDX-License-Identifier: MIT
 * Shared fractional PWM transaction; no animation or persistence state lives here.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <zephyr/drivers/pwm.h>

enum indicator_pwm_result {
    INDICATOR_PWM_UNCHANGED,
    INDICATOR_PWM_APPLIED,
    INDICATOR_PWM_FAILED,
};

/* Callers bound the numerator and supply their nonzero output denominator.
 * Keep the full fractional duty until the final, downward-rounded division. */
static inline uint32_t indicator_pwm_pulse_ns(uint32_t period, uint32_t fraction,
                                            uint32_t denominator) {
    return (uint32_t)((uint64_t)period * fraction / denominator);
}

/* The caller supplies its compile-time-sized scratch and existing cache arrays.
 * Calculate EVERY pulse before any device call, then preserve ready/write order.
 * A failed channel does not stop later writes or partially commit the cache. */
static inline enum indicator_pwm_result indicator_pwm_apply(
    const struct pwm_dt_spec *pwms, int count, uint32_t fraction, uint32_t denominator,
    uint32_t *pulses, uint32_t *last_pulses, bool *cache_valid,
    void (*report_error)(int channel, int error)) {
    bool changed = !*cache_valid;
    for (int i = 0; i < count; i++) {
        pulses[i] = indicator_pwm_pulse_ns(pwms[i].period, fraction, denominator);
        if (pulses[i] != last_pulses[i]) { changed = true; }
    }
    if (!changed) { return INDICATOR_PWM_UNCHANGED; }

    bool all_written = true;
    for (int i = 0; i < count; i++) {
        int error = pwm_is_ready_dt(&pwms[i]) ?
                    pwm_set_dt(&pwms[i], pwms[i].period, pulses[i]) : -ENODEV;
        if (error < 0) {
            all_written = false;
            report_error(i, error);
        }
    }
    *cache_valid = all_written;
    if (all_written) {
        for (int i = 0; i < count; i++) { last_pulses[i] = pulses[i]; }
    }
    return all_written ? INDICATOR_PWM_APPLIED : INDICATOR_PWM_FAILED;
}
