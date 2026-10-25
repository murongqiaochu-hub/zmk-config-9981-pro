/* SPDX-License-Identifier: MIT
 * FIRST source-only prefix for pinned hog.c. Load official declarations before
 * aliasing; the implementation lives in a different TU with no aliases. Includes
 * subsequent mouse/gamepad prefixes, not just the original SDK worker calls.
 */
#pragma once
#include "ipad_diag.h"
#if defined(CONFIG_BBP9981_IPAD_DIAG) && CONFIG_BBP9981_IPAD_DIAG
#define bt_gatt_notify_cb bbp_ipdiag_notify
#endif
