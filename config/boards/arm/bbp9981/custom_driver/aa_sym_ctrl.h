/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdint.h>

/* Release only the Ctrl reference owned by aA+SYM. Repeated calls are no-ops. */
int tp_sym_ctrl_release(int64_t timestamp);
