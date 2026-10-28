/* SPDX-License-Identifier: MIT
 * HOST-ONLY composite Report Map/codec experiment. NOT a GATT service, not
 * wired to firmware, and not proof of any host or device compatibility.
 *
 * Preserve the exact 196-byte fixed stock HOG map, then append the existing
 * 182-byte pure-gamepad map with input Report ID 3 -> 4 and an explicit
 * Logical Minimum 0 after that ID. Output ID 5 remains unchanged. The reset
 * matters because HID global state survives End Collection: the stock mouse
 * leaves Logical Minimum -32768 and the pure map otherwise inherits it.
 */
#pragma once
#if !defined(BBP_COMPOSITE_HOST_ONLY) || BBP_COMPOSITE_HOST_ONLY != 1
#error "composite_map_host.h requires BBP_COMPOSITE_HOST_ONLY=1 (host-only experiment)"
#endif
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "stadia_descriptor.h"
#include "stadia_codec.h"

#define BBP_COMPOSITE_STOCK_MAP_SIZE 196U
#define BBP_COMPOSITE_INPUT_ID 4U
#define BBP_COMPOSITE_OUTPUT_ID 5U
#define BBP_COMPOSITE_EXTRA_BYTES 2U
#define BBP_COMPOSITE_MAP_SIZE \
    (BBP_COMPOSITE_STOCK_MAP_SIZE + sizeof(bbp_exp_report_map) + BBP_COMPOSITE_EXTRA_BYTES)

_Static_assert(sizeof(bbp_exp_report_map) == 182U, "pinned pure map changed");
_Static_assert(BBP_EXP_INPUT_ID == 3U && BBP_EXP_OUTPUT_ID == BBP_COMPOSITE_OUTPUT_ID,
               "pure report references changed");
_Static_assert(BBP_EXP_INPUT_SIZE == BBP_EXPERIMENT_STADIA_BODY_SIZE && BBP_EXP_INPUT_SIZE == 10U,
               "pure descriptor and encoder body sizes must agree");
_Static_assert(BBP_EXP_OUTPUT_SIZE == 4U, "pure output body size changed");
_Static_assert(BBP_COMPOSITE_MAP_SIZE <= UINT16_MAX, "map exceeds ATT read length");

/* These values are test-only reference candidates, not registered GATT attrs. */
static const uint8_t bbp_composite_input_ref[2] = {BBP_COMPOSITE_INPUT_ID, 1U};
static const uint8_t bbp_composite_output_ref[2] = {BBP_COMPOSITE_OUTPUT_ID, 2U};

static inline bool bbp_composite_overlaps(const void *a, size_t an, const void *b, size_t bn) {
    if (!an || !bn) { return false; }
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x <= y ? y - x < an : x - y < bn;
}

/* Output must not alias either source map; reject rather than corrupt a later
 * slice. This simple host API is not Zephyr's ATT error ABI. */
static inline int bbp_composite_map_read(const uint8_t *stock, size_t stock_size,
                                         size_t offset, void *buffer, size_t length) {
    static const uint8_t expected_prefix[] = {0x05,0x01,0x09,0x05,0xa1,0x01,0x85,0x03};
    if (!stock || stock_size != BBP_COMPOSITE_STOCK_MAP_SIZE || (!buffer && length)) {
        return -EINVAL;
    }
    if (memcmp(bbp_exp_report_map, expected_prefix, sizeof(expected_prefix)) != 0) {
        return -EINVAL;
    }
    if (offset > BBP_COMPOSITE_MAP_SIZE) { return -ERANGE; }
    size_t count = BBP_COMPOSITE_MAP_SIZE - offset;
    if (count > length) { count = length; }
    if (bbp_composite_overlaps(stock, stock_size, buffer, count) ||
        bbp_composite_overlaps(bbp_exp_report_map, sizeof(bbp_exp_report_map), buffer, count)) {
        return -EINVAL;
    }
    uint8_t *out = buffer;
    for (size_t i = 0; i < count; ++i) {
        size_t pos = offset + i;
        if (pos < stock_size) {
            out[i] = stock[pos];
        } else {
            size_t p = pos - stock_size;
            if (p < 7U) { out[i] = bbp_exp_report_map[p]; }
            else if (p == 7U) { out[i] = BBP_COMPOSITE_INPUT_ID; }
            else if (p == 8U) { out[i] = 0x15; }
            else if (p == 9U) { out[i] = 0x00; }
            else { out[i] = bbp_exp_report_map[p - BBP_COMPOSITE_EXTRA_BYTES]; }
        }
    }
    return (int)count;
}

/* BLE input is a 10-byte body: the Report ID is in Report Reference, not here.
 * Reject overlap rather than permitting the original encoder to overwrite its
 * 9-byte input while initialising the output. An invalid input is written as
 * the existing neutral body and returns -EINVAL; invalid output/overlap writes
 * nothing. The caller is still responsible for an actually allocated capacity.
 */
static inline int bbp_composite_encode(const struct bbp_gamepad_report_body *input,
                                       uint8_t *out, size_t capacity) {
    if (!out || capacity < BBP_EXP_INPUT_SIZE) { return -EINVAL; }
    if (input) {
        uintptr_t a = (uintptr_t)(const void *)input;
        uintptr_t b = (uintptr_t)(void *)out;
        if ((a <= b && b - a < sizeof(*input)) ||
            (b < a && a - b < BBP_EXP_INPUT_SIZE)) { return -EINVAL; }
    }
    uint8_t body[BBP_EXP_INPUT_SIZE];
    bool ok = bbp_experiment_stadia_encode(input, body);
    memcpy(out, body, sizeof(body));
    return ok ? 0 : -EINVAL;
}
