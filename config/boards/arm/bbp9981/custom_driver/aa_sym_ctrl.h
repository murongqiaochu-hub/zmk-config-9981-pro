/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Physical pair state, independent of the layer-bound behavior's press order. */
int tp_sym_ctrl_update(bool aa_down, bool sym_down, int64_t timestamp);

/* Release only the Ctrl reference owned by aA+SYM. Repeated calls are no-ops. */
int tp_sym_ctrl_release(int64_t timestamp);
