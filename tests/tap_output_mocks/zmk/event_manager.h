/* Register actual lifecycle callback/subscriptions without linker sections. */
#pragma once
#define ZMK_EV_EVENT_BUBBLE 0
typedef struct { unsigned int kind; } zmk_event_t;
#define ZMK_LISTENER(name, callback) \
    static int (*const name##_registered)(const zmk_event_t *) __attribute__((unused)) = callback
#define ZMK_SUBSCRIPTION(name, event) \
    static const char name##_subscription_##event[] __attribute__((unused)) = #event
