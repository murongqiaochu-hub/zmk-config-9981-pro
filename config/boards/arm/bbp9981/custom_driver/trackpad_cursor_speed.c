/* SPDX-License-Identifier: MIT
 * Ordinary mouse gain service. This is not a hardware-DPI setting.
 *
 * One short spinlock serializes writers and persistence bookkeeping. Readers
 * take one atomic snapshot. No settings backend/read callback runs locked.
 * The delayable work is statically initialized, so there is no SYS_INIT that
 * can reset an early settings commit or an early user action.
 */
#include "trackpad_cursor_speed.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

static struct k_spinlock cursor_speed_lock;
static atomic_t cursor_speed_state = ATOMIC_INIT(BBP_CURSOR_SPEED_DEFAULT_INDEX);
/* Only the non-reentrant A320 poll advances this latch. Cycle/load writers
 * publish selected state above, never mutate an in-flight motion frame.
 */
static atomic_t cursor_speed_motion_state = ATOMIC_INIT(BBP_CURSOR_SPEED_DEFAULT_INDEX);

/* Caller holds cursor_speed_lock. Unsigned arithmetic gives defined wrap at
 * the 29-bit generation boundary, never an index-only five-cycle alias.
 */
static void cursor_speed_publish(uint8_t index) {
    uint32_t previous = (uint32_t)atomic_get(&cursor_speed_state);
    uint32_t next = ((previous + BBP_CURSOR_SPEED_GENERATION_STEP) &
                     ~BBP_CURSOR_SPEED_INDEX_MASK) | index;
    atomic_set(&cursor_speed_state, (atomic_val_t)next);
}

uint32_t bbp_cursor_speed_snapshot(void) {
    return (uint32_t)atomic_get(&cursor_speed_state);
}

uint16_t bbp_cursor_speed_gain(bool slow) {
    return bbp_cursor_speed_table_gain(
        bbp_cursor_speed_snapshot_index(bbp_cursor_speed_snapshot()), slow);
}

uint32_t bbp_cursor_speed_begin_motion_frame(void) {
    uint32_t captured = bbp_cursor_speed_snapshot();
    atomic_set(&cursor_speed_motion_state, (atomic_val_t)captured);
    return captured;
}

uint16_t bbp_cursor_speed_motion_gain(bool slow) {
    uint32_t captured = (uint32_t)atomic_get(&cursor_speed_motion_state);
    return bbp_cursor_speed_table_gain(bbp_cursor_speed_snapshot_index(captured), slow);
}

#if IS_ENABLED(CONFIG_SETTINGS)
#include <errno.h>
#include <string.h>
#include <zephyr/settings/settings.h>

#define CURSOR_SPEED_SETTINGS_SUBTREE "bbp9981_cursor"
#define CURSOR_SPEED_SETTINGS_LEAF "gain_v1"
#define CURSOR_SPEED_SETTINGS_KEY CURSOR_SPEED_SETTINGS_SUBTREE "/" CURSOR_SPEED_SETTINGS_LEAF
#define CURSOR_SPEED_RECORD_VERSION 1U
#define CURSOR_SPEED_RECORD_SIZE 2U
#define CURSOR_SPEED_MAX_FAILURES 3U

/* All fields below are protected by cursor_speed_lock. The first h_commit is
 * the boot restore barrier. A later settings_load/commit is intentionally not
 * a live restore command. No saved/default value is automatically rewritten.
 */
static bool cursor_load_committed;
static bool cursor_staged_valid;
static uint8_t cursor_staged_index;
static bool cursor_user_changed;
static bool cursor_save_dirty;
static bool cursor_save_running;
static uint8_t cursor_save_failures;
static int64_t cursor_save_after;

static void cursor_speed_save_worker(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(cursor_speed_save_work, cursor_speed_save_worker);

/* Caller holds cursor_speed_lock. k_work_reschedule is ISR-safe and does not
 * call the handler inline. Only queue/timer bookkeeping occurs under our lock.
 */
static void cursor_speed_schedule_locked(void) {
    if (!cursor_load_committed || !cursor_save_dirty ||
        cursor_save_failures >= CURSOR_SPEED_MAX_FAILURES) {
        return;
    }
    int64_t remaining = cursor_save_after - k_uptime_get();
    (void)k_work_reschedule(&cursor_speed_save_work,
                           remaining > 0 ? K_MSEC(remaining) : K_NO_WAIT);
}

static void cursor_speed_save_worker(struct k_work *work) {
    (void)work;
    k_spinlock_key_t key = k_spin_lock(&cursor_speed_lock);
    if (!cursor_load_committed || !cursor_save_dirty || cursor_save_running ||
        cursor_save_failures >= CURSOR_SPEED_MAX_FAILURES) {
        k_spin_unlock(&cursor_speed_lock, key);
        return;
    }
    if (k_uptime_get() < cursor_save_after) {
        cursor_speed_schedule_locked();
        k_spin_unlock(&cursor_speed_lock, key);
        return;
    }
    uint32_t captured = bbp_cursor_speed_snapshot();
    uint8_t record[CURSOR_SPEED_RECORD_SIZE] = {
        CURSOR_SPEED_RECORD_VERSION, bbp_cursor_speed_snapshot_index(captured),
    };
    cursor_save_running = true;
    k_spin_unlock(&cursor_speed_lock, key);

    /* A backend may block or even reenter cycle/load. It never owns our lock. */
    int ret = settings_save_one(CURSOR_SPEED_SETTINGS_KEY, record, sizeof(record));

    key = k_spin_lock(&cursor_speed_lock);
    cursor_save_running = false;
    if (bbp_cursor_speed_snapshot() != captured) {
        /* A newer user generation owns its own debounce and retry budget.
         * Even five cycles back to the captured index must stay dirty. An old
         * success cannot mark it saved; an old error cannot spend its retries.
         */
        cursor_speed_schedule_locked();
    } else if (ret >= 0) {
        cursor_save_dirty = false;
        cursor_save_failures = 0U;
    } else {
        ++cursor_save_failures;
        if (cursor_save_failures < CURSOR_SPEED_MAX_FAILURES) {
            cursor_save_after = k_uptime_get() + CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE;
            cursor_speed_schedule_locked();
        }
        /* At failure 3 leave dirty but do not reschedule or log repeatedly.
         * Only another explicit user cycle rearms this generation's budget.
         */
    }
    k_spin_unlock(&cursor_speed_lock, key);
}

static int cursor_speed_settings_set(const char *name, size_t len,
                                     settings_read_cb read_cb, void *cb_arg) {
    if (name == NULL || strcmp(name, CURSOR_SPEED_SETTINGS_LEAF) != 0) {
        return -ENOENT;
    }
    if (len != CURSOR_SPEED_RECORD_SIZE || read_cb == NULL) {
        return -EINVAL;
    }
    uint8_t record[CURSOR_SPEED_RECORD_SIZE];
    ssize_t n = read_cb(cb_arg, record, sizeof(record));
    if (n < 0) {
        return (int)n;
    }
    if (n != (ssize_t)sizeof(record) || record[0] != CURSOR_SPEED_RECORD_VERSION ||
        record[1] >= BBP_CURSOR_SPEED_COUNT) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&cursor_speed_lock);
    if (!cursor_load_committed) {
        cursor_staged_index = record[1];
        cursor_staged_valid = true;
    }
    k_spin_unlock(&cursor_speed_lock, key);
    return 0;
}

static int cursor_speed_settings_commit(void) {
    k_spinlock_key_t key = k_spin_lock(&cursor_speed_lock);
    if (!cursor_load_committed) {
        cursor_load_committed = true;
        if (cursor_staged_valid && !cursor_user_changed &&
            bbp_cursor_speed_snapshot_index(bbp_cursor_speed_snapshot()) != cursor_staged_index) {
            cursor_speed_publish(cursor_staged_index);
        }
        cursor_staged_valid = false; /* Consume once, never replay an old load. */
        cursor_speed_schedule_locked();
    }
    k_spin_unlock(&cursor_speed_lock, key);
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(bbp9981_cursor_speed, CURSOR_SPEED_SETTINGS_SUBTREE, NULL,
                               cursor_speed_settings_set, cursor_speed_settings_commit, NULL);
#endif /* CONFIG_SETTINGS */

int bbp_cursor_speed_cycle(void) {
    k_spinlock_key_t key = k_spin_lock(&cursor_speed_lock);
    uint8_t index = bbp_cursor_speed_snapshot_index(bbp_cursor_speed_snapshot());
    index = (uint8_t)((index + 1U) % BBP_CURSOR_SPEED_COUNT);
    cursor_speed_publish(index);
#if IS_ENABLED(CONFIG_SETTINGS)
    cursor_user_changed = true;
    cursor_save_dirty = true;
    cursor_save_failures = 0U;
    cursor_save_after = k_uptime_get() + CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE;
    cursor_speed_schedule_locked();
#endif
    /* CONFIG_SETTINGS=n is explicitly volatile: no work or backend references. */
    k_spin_unlock(&cursor_speed_lock, key);
    return 0;
}
