/* SPDX-License-Identifier: MIT
 * Host-only support for test_aa_layer_owner.c.
 *
 * Mocked: kernel work/time, device lookup, DT instantiation, layer bits and event
 * dispatch/link order. Real: the pinned sticky/mo/to sources and AA owner source.
 * No matrix, full keymap/hold-tap, RTOS concurrency, settings, HID or device I/O.
 */
#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define CONFIG_ZMK_LOG_LEVEL 0
#define CONFIG_A320_LOG_LEVEL 0
#define CONFIG_ZMK_BEHAVIOR_METADATA 0
#define CONFIG_ZMK_BEHAVIOR_STICKY_KEY_MAX_HELD 8
#define CONFIG_ZMK_SPLIT 0
#define CONFIG_KERNEL_INIT_PRIORITY_DEFAULT 40
#define CONFIG_APPLICATION_INIT_PRIORITY 90
#define IS_ENABLED(option) (option)
#define DT_HAS_COMPAT_STATUS_OKAY(compat) 1
#define DT_INST_FOREACH_STATUS_OKAY(fn)
#define DT_INST(inst, compat) 0
#define DEVICE_DT_NAME(node) "kp"
#define BEHAVIOR_DT_INST_DEFINE(...)
#define POST_KERNEL 0
#define APPLICATION 0
#define SYS_INIT(...)
#define LOG_MODULE_DECLARE(...)
#define LOG_MODULE_REGISTER(...)
#define LOG_DBG(...)
#define LOG_INF(...)
#define LOG_WRN(...)
#define LOG_ERR(...)
#define BUILD_ASSERT(condition, ...) _Static_assert(condition, "host build assertion")
#define CONTAINER_OF(pointer, type, member) \
    ((type *)((char *)(pointer) - offsetof(type, member)))
#define ARG_UNUSED(value) ((void)(value))
#define BIT(index) (1U << (index))
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

#define ZMK_BEHAVIOR_OPAQUE 0
#define ZMK_BEHAVIOR_TRANSPARENT 1
#define ZMK_EV_EVENT_BUBBLE 0
#define ZMK_EV_EVENT_HANDLED 1
#define ZMK_EV_EVENT_CAPTURED 2
#define K_MSEC(value) (value)
#define K_NO_WAIT 0
#define K_FOREVER (-1)

struct device {
    const char *name;
    const void *config;
    void *data;
};
struct zmk_behavior_binding {
    const char *behavior_dev;
    uint32_t param1;
    uint32_t param2;
};
struct zmk_behavior_binding_event {
    int layer;
    uint32_t position;
    int64_t timestamp;
};
struct behavior_parameter_metadata { int unused; };
struct behavior_driver_api {
    int (*binding_pressed)(struct zmk_behavior_binding *, struct zmk_behavior_binding_event);
    int (*binding_released)(struct zmk_behavior_binding *, struct zmk_behavior_binding_event);
    int (*get_parameter_metadata)(const struct device *, struct behavior_parameter_metadata *);
    const struct behavior_parameter_metadata *parameter_metadata;
};
const struct device *zmk_behavior_get_binding(const char *name);
int zmk_behavior_invoke_binding(const struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event, bool pressed);
static inline int zmk_behavior_get_empty_param_metadata(
    const struct device *device, struct behavior_parameter_metadata *metadata) {
    (void)device;
    (void)metadata;
    return 0;
}

typedef uint8_t zmk_keymap_layer_id_t;
typedef uint32_t zmk_keymap_layers_state_t;

static int64_t mock_now;
static uint32_t mock_layer_bits;
static unsigned mock_layer_activate_calls;
static unsigned mock_layer_deactivate_calls;
static unsigned mock_layer_to_calls;
static unsigned mock_layer_events;
static unsigned mock_upper_false_events;
static unsigned mock_upper_true_events;
static unsigned mock_keycode_sink_calls;
static unsigned mock_keycode_down_calls;
static unsigned mock_keycode_up_calls;
static unsigned mock_keycode_reraises;
static uint32_t mock_keycode_sink_layers;
static int mock_activate_error;
static int mock_to_error;

struct k_work {
    void (*handler)(struct k_work *);
};
struct k_work_delayable {
    struct k_work work;
    bool pending;
    bool running;
    int64_t due;
    unsigned schedules;
    unsigned cancels;
};
static inline int64_t k_uptime_get(void) { return mock_now; }
static inline uint32_t k_uptime_get_32(void) { return (uint32_t)mock_now; }
static inline struct k_work_delayable *k_work_delayable_from_work(struct k_work *work) {
    return CONTAINER_OF(work, struct k_work_delayable, work);
}
static inline void k_work_init_delayable(struct k_work_delayable *work,
                                          void (*handler)(struct k_work *)) {
    memset(work, 0, sizeof(*work));
    work->work.handler = handler;
}
static inline int k_work_schedule(struct k_work_delayable *work, int64_t delay) {
    assert(delay >= 0);
    if (work->pending) { return 0; }
    work->pending = true;
    work->due = mock_now + delay;
    work->schedules++;
    return 1;
}
static inline int k_work_cancel_delayable(struct k_work_delayable *work) {
    bool was_pending = work->pending;
    work->pending = false;
    work->cancels++;
    return work->running ? -EINPROGRESS : (was_pending ? 1 : 0);
}

enum mock_event_type {
    MOCK_EVENT_zmk_keycode_state_changed = 1,
    MOCK_EVENT_zmk_layer_state_changed,
};
typedef struct { enum mock_event_type type; } zmk_event_t;
struct zmk_keycode_state_changed {
    uint8_t usage_page;
    uint32_t keycode;
    bool state;
    uint8_t implicit_modifiers;
    uint8_t explicit_modifiers;
    int64_t timestamp;
};
struct zmk_keycode_state_changed_event {
    zmk_event_t header;
    struct zmk_keycode_state_changed data;
};
struct zmk_layer_state_changed {
    zmk_keymap_layer_id_t layer;
    bool state;
    int64_t timestamp;
};
struct zmk_layer_state_changed_event {
    zmk_event_t header;
    struct zmk_layer_state_changed data;
};
struct zmk_listener { int (*callback)(const zmk_event_t *); };
struct mock_subscription {
    enum mock_event_type type;
    const struct zmk_listener *listener;
};
static struct mock_subscription mock_subscriptions[16];
static size_t mock_subscription_count;
static unsigned mock_dispatch_depth;

static inline void mock_register_subscription(enum mock_event_type type,
                                                const struct zmk_listener *listener) {
    assert(mock_subscription_count < ARRAY_SIZE(mock_subscriptions));
    mock_subscriptions[mock_subscription_count++] = (struct mock_subscription){type, listener};
}
/* Constructors register actual source subscriptions; no SDK ELF/link-order claim. */
#define ZMK_LISTENER(name, callback_fn) \
    const struct zmk_listener zmk_listener_##name = {.callback = callback_fn}
#define ZMK_SUBSCRIPTION(name, event_name) \
    static void mock_register_##name##_##event_name(void) __attribute__((constructor)); \
    static void mock_register_##name##_##event_name(void) { \
        mock_register_subscription(MOCK_EVENT_##event_name, &zmk_listener_##name); \
    }

static inline struct zmk_keycode_state_changed *as_zmk_keycode_state_changed(
    const zmk_event_t *event) {
    return event->type == MOCK_EVENT_zmk_keycode_state_changed ?
        &((struct zmk_keycode_state_changed_event *)(void *)event)->data : NULL;
}
static inline struct zmk_layer_state_changed *as_zmk_layer_state_changed(
    const zmk_event_t *event) {
    return event->type == MOCK_EVENT_zmk_layer_state_changed ?
        &((struct zmk_layer_state_changed_event *)(void *)event)->data : NULL;
}
static inline struct zmk_keycode_state_changed_event copy_raised_zmk_keycode_state_changed(
    const struct zmk_keycode_state_changed *event) {
    return *CONTAINER_OF(event, struct zmk_keycode_state_changed_event, data);
}

static int mock_dispatch_from(zmk_event_t *event, size_t start) {
    assert(mock_dispatch_depth < 32U); /* A recursive layer listener fails deterministically. */
    mock_dispatch_depth++;
    for (size_t i = start; i < mock_subscription_count; ++i) {
        if (mock_subscriptions[i].type != event->type) { continue; }
        int result = mock_subscriptions[i].listener->callback(event);
        if (result == ZMK_EV_EVENT_BUBBLE) { continue; }
        mock_dispatch_depth--;
        if (result == ZMK_EV_EVENT_CAPTURED || result == ZMK_EV_EVENT_HANDLED) { return 0; }
        return result;
    }
    if (event->type == MOCK_EVENT_zmk_keycode_state_changed) {
        struct zmk_keycode_state_changed *key = as_zmk_keycode_state_changed(event);
        mock_keycode_sink_calls++;
        if (key->state) { mock_keycode_down_calls++; }
        else { mock_keycode_up_calls++; }
        mock_keycode_sink_layers = mock_layer_bits;
    }
    mock_dispatch_depth--;
    return 0;
}
static int mock_dispatch_after(zmk_event_t *event, const struct zmk_listener *listener) {
    mock_keycode_reraises++;
    for (size_t i = 0; i < mock_subscription_count; ++i) {
        if (mock_subscriptions[i].type == event->type &&
            mock_subscriptions[i].listener == listener) {
            return mock_dispatch_from(event, i + 1U);
        }
    }
    assert(!"RAISE_AFTER listener was not registered");
    return -EINVAL;
}
#define ZMK_EVENT_RAISE_AFTER(event, listener_name) \
    mock_dispatch_after(&(event).header, &zmk_listener_##listener_name)

static inline bool zmk_keymap_layer_active(zmk_keymap_layer_id_t layer) {
    return layer == 0U || (mock_layer_bits & BIT(layer)) != 0U;
}
static inline zmk_keymap_layers_state_t zmk_keymap_layer_state(void) {
    return mock_layer_bits;
}
static inline zmk_keymap_layer_id_t zmk_keymap_highest_layer_active(void) {
    for (int layer = 3; layer > 0; --layer) {
        if ((mock_layer_bits & BIT(layer)) != 0U) { return (zmk_keymap_layer_id_t)layer; }
    }
    return 0U;
}
static int mock_set_layer(zmk_keymap_layer_id_t layer, bool active) {
    assert(layer < 4U);
    if (layer == 0U && !active) { return 0; }
    uint32_t old = mock_layer_bits;
    if (active) { mock_layer_bits |= BIT(layer); }
    else { mock_layer_bits &= ~BIT(layer); }
    if (old == mock_layer_bits) { return 0; }
    mock_layer_events++;
    if (layer == 2U && active) { mock_upper_true_events++; }
    if (layer == 2U && !active) { mock_upper_false_events++; }
    struct zmk_layer_state_changed_event event = {
        .header = {.type = MOCK_EVENT_zmk_layer_state_changed},
        .data = {.layer = layer, .state = active, .timestamp = mock_now},
    };
    return mock_dispatch_from(&event.header, 0U);
}
static inline int zmk_keymap_layer_activate(zmk_keymap_layer_id_t layer) {
    mock_layer_activate_calls++;
    if (mock_activate_error != 0) {
        int error = mock_activate_error;
        mock_activate_error = 0;
        return error;
    }
    return mock_set_layer(layer, true);
}
static inline int zmk_keymap_layer_deactivate(zmk_keymap_layer_id_t layer) {
    mock_layer_deactivate_calls++;
    return mock_set_layer(layer, false);
}
static inline int zmk_keymap_layer_to(zmk_keymap_layer_id_t layer) {
    mock_layer_to_calls++;
    if (mock_to_error != 0) {
        int error = mock_to_error;
        mock_to_error = 0;
        return error;
    }
    /* Pinned keymap.c ordering, including synchronous false -> true callbacks. */
    for (int current = 3; current >= 0; --current) {
        (void)zmk_keymap_layer_deactivate((zmk_keymap_layer_id_t)current);
    }
    (void)zmk_keymap_layer_activate(layer);
    return 0;
}

#define ZMK_HID_USAGE_ID(encoded) ((uint32_t)(encoded) & 0xFFFFU)
#define ZMK_HID_USAGE_PAGE(encoded) (((uint32_t)(encoded) >> 16) & 0xFFU)
#define SELECT_MODS(encoded) (((uint32_t)(encoded) >> 24) & 0xFFU)
static inline bool is_mod(uint8_t page, uint32_t keycode) {
    return page == 7U && keycode >= 0xE0U && keycode <= 0xE7U;
}
