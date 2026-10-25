/* SPDX-License-Identifier: MIT -- dedicated host-only SDK mock. */
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define BIT(n) (1UL << (n))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define IS_ENABLED(x) (x)
#define BUILD_ASSERT(expr, ...) _Static_assert(expr, "mock BUILD_ASSERT")
#define __ASSERT_NO_MSG(expr) assert(expr)
#define __packed __attribute__((packed))
