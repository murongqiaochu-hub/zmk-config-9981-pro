/* SPDX-License-Identifier: MIT -- retain and call the real hog initializer. */
#pragma once
#define APPLICATION 0
#define SYS_INIT(fn, level, priority) int (*gamepad_mock_hog_init)(void) = (fn)
