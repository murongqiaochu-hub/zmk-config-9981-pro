/* Read-only transaction comparison for the SAME documented identity pointer.
 * Unknown 0x0c is an observation, never an identity whitelist or contact UP.
 * No register-value writes, reset pins, configuration or timing sleeps.
 * SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
enum tp_bus_mode { TP_BUS_COMBINED, TP_BUS_STOP_READ, TP_BUS_LEGACY_PREFIX, TP_BUS_MODE_COUNT };
struct tp_bus_ops {
    int (*pointer_stop)(void *ctx, uint8_t reg);
    int (*read_only)(void *ctx, uint8_t *out);
    int (*combined)(void *ctx, uint8_t reg, uint8_t *out);
};
struct tp_bus_observation { uint8_t value; int error; bool obtained; };
static inline const char *tp_bus_mode_name(enum tp_bus_mode mode) {
    switch (mode) {
    case TP_BUS_COMBINED: return "combined";
    case TP_BUS_STOP_READ: return "stop_read";
    case TP_BUS_LEGACY_PREFIX: return "legacy_prefix";
    default: return "invalid";
    }
}
static inline struct tp_bus_observation tp_bus_observe(const struct tp_bus_ops *ops, void *ctx,
                                                       enum tp_bus_mode mode, uint8_t reg) {
    struct tp_bus_observation s = {0};
    if (mode == TP_BUS_COMBINED) { s.error=ops->combined(ctx,reg,&s.value); }
    else if (mode==TP_BUS_STOP_READ || mode==TP_BUS_LEGACY_PREFIX) {
        s.error=ops->pointer_stop(ctx,reg);
        if (!s.error) { s.error=mode==TP_BUS_STOP_READ ? ops->read_only(ctx,&s.value)
                                                       : ops->combined(ctx,reg,&s.value); }
    } else { s.error=-22; }
    s.obtained=s.error==0;
    if (!s.obtained) { s.value=0; } /* Explicit obtained flag: not real zero on failure. */
    return s;
}
