/* SPDX-License-Identifier: MIT
 * Dedicated host mocks for test_cursor_speed.c only. No legacy fixture changes.
 */
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdatomic.h>
#include <sys/types.h>

#ifndef CONFIG_SETTINGS
#define CONFIG_SETTINGS 1
#endif
#ifndef CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE
#define CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE 1000
#endif
#ifndef CONFIG_ZMK_BEHAVIOR_METADATA
#define CONFIG_ZMK_BEHAVIOR_METADATA 1
#endif
#define CONFIG_KERNEL_INIT_PRIORITY_DEFAULT 40
#define IS_ENABLED(option) (option)
#define DT_HAS_COMPAT_STATUS_OKAY(compat) 1
#define DT_INST_FOREACH_STATUS_OKAY(fn) fn(0)
#define POST_KERNEL 0
/* Deliberately nonzero to catch a wrapper that returns literal 0 instead. */
#define ZMK_BEHAVIOR_OPAQUE 17

static unsigned mock_lock_depth;
struct k_spinlock { bool held; };
typedef unsigned k_spinlock_key_t;
static inline k_spinlock_key_t k_spin_lock(struct k_spinlock *lock) {
    assert(!lock->held);
    lock->held = true;
    ++mock_lock_depth;
    return 0U;
}
static inline void k_spin_unlock(struct k_spinlock *lock, k_spinlock_key_t key) {
    (void)key;
    assert(lock->held && mock_lock_depth > 0U);
    lock->held = false;
    --mock_lock_depth;
}

typedef _Atomic(int32_t) atomic_t;
typedef int32_t atomic_val_t;
#define ATOMIC_INIT(value) ATOMIC_VAR_INIT(value)
static inline atomic_val_t atomic_get(const atomic_t *value) {
    return atomic_load_explicit(value, memory_order_seq_cst);
}
static inline atomic_val_t atomic_set(atomic_t *value, atomic_val_t next) {
    return atomic_exchange_explicit(value, next, memory_order_seq_cst);
}

static int64_t mock_now;
static inline int64_t k_uptime_get(void) { return mock_now; }
#define K_MSEC(ms) ((int64_t)(ms))
#define K_NO_WAIT INT64_C(0)
struct k_work { void (*handler)(struct k_work *); };
struct k_work_delayable {
    struct k_work work;
    bool pending;
    int64_t due;
    unsigned schedules;
};
#define K_WORK_DELAYABLE_DEFINE(name, handler_fn) \
    struct k_work_delayable name = {.work = {.handler = (handler_fn)}}
static struct k_work_delayable *mock_scheduled_work;
static unsigned mock_work_runs;
static inline int k_work_reschedule(struct k_work_delayable *work, int64_t delay) {
    assert(delay >= 0);
    assert(mock_scheduled_work == NULL || mock_scheduled_work == work);
    mock_scheduled_work = work;
    work->pending = true;
    work->due = mock_now + delay;
    ++work->schedules;
    return 0;
}
static inline void mock_advance_to(int64_t target) {
    assert(target >= mock_now);
    unsigned guard = 0U;
    while (mock_scheduled_work != NULL && mock_scheduled_work->pending &&
           mock_scheduled_work->due <= target) {
        assert(++guard < 100U); /* An unbounded immediate-work loop fails fast. */
        struct k_work_delayable *work = mock_scheduled_work;
        assert(work->due >= mock_now);
        mock_now = work->due;
        work->pending = false;
        ++mock_work_runs;
        work->work.handler(&work->work);
        assert(mock_lock_depth == 0U);
    }
    mock_now = target;
}

struct device { int unused; };
struct zmk_behavior_binding { const char *behavior_dev; uint32_t param1, param2; };
struct zmk_behavior_binding_event { int layer; uint32_t position; int64_t timestamp; };
struct behavior_parameter_metadata { int unused; };
static inline int zmk_behavior_get_empty_param_metadata(
    const struct device *dev, struct behavior_parameter_metadata *metadata) {
    (void)dev;
    (void)metadata;
    return 0;
}
struct behavior_driver_api {
    int (*binding_pressed)(struct zmk_behavior_binding *, struct zmk_behavior_binding_event);
    int (*binding_released)(struct zmk_behavior_binding *, struct zmk_behavior_binding_event);
    int (*get_parameter_metadata)(const struct device *, struct behavior_parameter_metadata *);
};
#define BEHAVIOR_DT_INST_DEFINE(inst, init, pm, data, config, level, priority, api_ptr) \
    static const struct behavior_driver_api *const mock_cursor_behavior_api_##inst = (api_ptr)

/* Settings-off builds deliberately define no settings API at all. A backend
 * reference accidentally escaping the production guard fails to compile.
 */
#if CONFIG_SETTINGS
typedef ssize_t (*settings_read_cb)(void *, void *, size_t);
struct mock_settings_handler {
    const char *subtree;
    int (*set)(const char *, size_t, settings_read_cb, void *);
    int (*commit)(void);
};
#define SETTINGS_STATIC_HANDLER_DEFINE(name, path, get_fn, set_fn, commit_fn, export_fn) \
    static const struct mock_settings_handler mock_handler_##name = { \
        .subtree = (path), .set = (set_fn), .commit = (commit_fn), \
    }

static unsigned mock_save_attempts, mock_save_successes;
static int mock_save_error;
static bool mock_flash_present;
static uint8_t mock_flash_record[2];
static uint8_t mock_save_records[64][2];
static int64_t mock_save_times[64];
static void (*mock_save_hook)(void);
static inline int settings_save_one(const char *key, const void *data, size_t len) {
    assert(mock_lock_depth == 0U); /* Backend must never run under either lock. */
    assert(strcmp(key, "bbp9981_cursor/gain_v1") == 0 && len == 2U);
    assert(mock_save_attempts < 64U);
    uint8_t captured[2];
    memcpy(captured, data, sizeof(captured));
    memcpy(mock_save_records[mock_save_attempts], captured, sizeof(captured));
    mock_save_times[mock_save_attempts++] = mock_now;
    int result = mock_save_error;
    if (mock_save_hook != NULL) {
        void (*hook)(void) = mock_save_hook;
        mock_save_hook = NULL;
        hook();
    }
    if (result < 0) {
        return result;
    }
    memcpy(mock_flash_record, captured, sizeof(captured));
    mock_flash_present = true;
    ++mock_save_successes;
    return 0;
}
#endif
