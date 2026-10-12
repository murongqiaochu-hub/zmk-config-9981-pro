/* Minimal host utility surface for the unchanged v0.3.0 HID TU. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define __packed __attribute__((__packed__))
#define BIT(n) (1UL << (n))
#define WRITE_BIT(var, bit, set) do { \
    if (set) { (var) |= BIT(bit); } else { (var) &= ~BIT(bit); } \
} while (0)
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define HID_MOCK_ENABLED_1 HID_MOCK_ENABLED_COMMA,
#define HID_MOCK_ENABLED_PICK(ignore, value, ...) value
#define HID_MOCK_ENABLED_EXPAND(args) HID_MOCK_ENABLED_PICK(args 1, 0)
#define HID_MOCK_ENABLED_TEST(config) HID_MOCK_ENABLED_EXPAND(HID_MOCK_ENABLED_##config)
#define IS_ENABLED(config) HID_MOCK_ENABLED_TEST(config)
