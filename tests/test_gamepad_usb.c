/* SPDX-License-Identifier: MIT
 * Standalone HID_1 + unmodified pinned ZMK USB/HID_0 fixtures. The complete
 * official ZMK hid.h is reused; mocks faithfully model hid_ops/public cfg ABI
 * and defer consumption of interrupt-IN source pointers until completion.
 *
 * cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter
 *    -Wno-unused-function -Wno-unused-variable -fsanitize=undefined
 *    -fno-sanitize-recover=all -DCONFIG_ZMK_USB_BOOT=1
 *    -Itests/gamepad_usb_mocks -Itests/hid_snapshot_mocks -Itests/sdk_snapshot_mocks
 *    tests/test_gamepad_usb.c -o /tmp/test_gamepad_usb
 * Run BOOT=0/1: normal abort submit-reset submit-reset-abort error-ready stale-ready
 * endpoints missing bad-cfg init-fail
 * These are host-model tests, NOT real enumeration/firmware/hardware validation.
 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifndef CONFIG_ZMK_USB_BOOT
#define CONFIG_ZMK_USB_BOOT 1
#endif
#define CONFIG_USB_HID_BOOT_PROTOCOL CONFIG_ZMK_USB_BOOT
#define CONFIG_USB_DEVICE_SOF CONFIG_ZMK_USB_BOOT
#define CONFIG_ZMK_USB 1
#define CONFIG_SOC_NRF52840_QIAA 1
#define CONFIG_USB_NRFX 1
#define CONFIG_USB_HID_DEVICE_COUNT 2
#define CONFIG_USB_HID_REPORTS 4
#define CONFIG_HID_INTERRUPT_EP_MPS 16
#define CONFIG_USB_DEVICE_INITIALIZE_AT_BOOT 0
#define CONFIG_KERNEL_INIT_PRIORITY_DEFAULT 40
#define CONFIG_APPLICATION_INIT_PRIORITY 90
#define CONFIG_ZMK_USB_INIT_PRIORITY 94
#define CONFIG_ZMK_USB_HID_INIT_PRIORITY 95
#define CONFIG_ZMK_POINTING 1
#define CONFIG_ZMK_POINTING_SMOOTH_SCROLLING 1
#define CONFIG_ZMK_HID_INDICATORS 1
#define CONFIG_ZMK_HID_REPORT_TYPE_NKRO 1
#define CONFIG_ZMK_HID_CONSUMER_REPORT_USAGES_FULL 1
#define CONFIG_ZMK_HID_CONSUMER_REPORT_SIZE 6
#define CONFIG_ENABLE_HID_INT_OUT_EP 1
#define LOG_MODULE_DECLARE(...)
#define LOG_ERR(...)
#define LOG_DBG(...)
struct test_init_entry { int (*fn)(void); const char *level; int priority; };
#define SYS_INIT(fn_, level_, priority_) \
    static const struct test_init_entry test_init_##fn_ = {fn_, #level_, priority_}
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>
#include <nrfx_usbd.h>
#include <zmk/hid.h>
#include <zmk/usb.h>
#include <zmk/usb_hid.h>
#include <zmk/hid_indicators.h>
#include <zmk/pointing/resolution_multipliers.h>
#include "../config/boards/arm/bbp9981/custom_driver/gamepad_runtime.h"

static struct usb_if_descriptor stock_iface = {
    .bLength = sizeof(struct usb_if_descriptor), .bDescriptorType = USB_DESC_INTERFACE,
    .bNumEndpoints = 2, .bInterfaceClass = USB_BCC_HID,
    .bInterfaceSubClass = CONFIG_ZMK_USB_BOOT, .bInterfaceProtocol = 0};
static struct usb_if_descriptor pad_iface = {
    .bLength = sizeof(struct usb_if_descriptor), .bDescriptorType = USB_DESC_INTERFACE,
    .bNumEndpoints = 2, .bInterfaceClass = USB_BCC_HID,
    .bInterfaceSubClass = CONFIG_ZMK_USB_BOOT, .bInterfaceProtocol = 0};
static struct usb_ep_cfg_data stock_eps[] = {{.ep_addr = 0x85}, {.ep_addr = 0x03}};
/* Deliberately NOT 0x81. Endpoint0 is fixed dynamically by the real USB stack. */
static struct usb_ep_cfg_data pad_eps[] = {{.ep_addr = 0x86}, {.ep_addr = 0x04}};
static struct usb_cfg_data stock_cfg = {.interface_descriptor = &stock_iface,
    .num_endpoints = 2, .endpoint = stock_eps};
static struct usb_cfg_data pad_cfg = {.interface_descriptor = &pad_iface,
    .num_endpoints = 2, .endpoint = pad_eps};
static struct device devices[] = {{.id = 0, .config = &stock_cfg}, {.id = 1, .config = &pad_cfg}};
static unsigned int irq_depth, takes, gives, work_submits;
static unsigned int registration_calls[2], hid_init_calls[2], binding_calls[2], proto_code_calls[2];
static unsigned int enable_calls, wakeup_calls, context_checks;
static int take_error, write_error[2], pad_init_error;
static bool missing_pad, mock_in_isr, nrf_busy[256];
static unsigned int busy_queries;
static nrfx_usbd_ep_t last_busy_ep;
static void (*during_wait)(void), (*during_driver)(void), (*during_context_check)(void);
static void (*before_driver_accept)(void), (*before_irq_lock)(void);
static unsigned int context_hook_at;
static usb_dc_status_callback actual_usb_callback;
static const struct hid_ops *registered_ops[2];
static const uint8_t *registered_desc[2];
static size_t registered_desc_len[2];
/* The nRF controller has separate endpoint context per HID instance. Neither
 * submission copies data. In particular, pad must NEVER pass a stack address. */
static struct dma_model {
    const uint8_t *source;
    uint32_t len;
    uint8_t ep;
    bool pending;
    unsigned int writes;
    uint8_t completed[128];
    size_t completed_len;
} dma[2];
static uint32_t runtime_context_epoch = 1U;
static bool media_active = true, usb_selected = true;
static struct bbp_gamepad_report_body runtime_body;
static struct zmk_hid_keyboard_report keyboard = {.report_id = ZMK_HID_REPORT_ID_KEYBOARD};
static struct zmk_hid_consumer_report consumer = {.report_id = ZMK_HID_REPORT_ID_CONSUMER};
static struct zmk_hid_mouse_report mouse = {.report_id = ZMK_HID_REPORT_ID_MOUSE};
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
static zmk_hid_boot_report_t boot_keyboard;
#endif
static uint8_t last_leds, last_wheel, last_hwheel;
static unsigned int led_sets, resolution_sets;

bool k_is_in_isr(void) { return mock_in_isr; }
bool nrfx_usbd_ep_is_busy(nrfx_usbd_ep_t ep) {
    assert(irq_depth && !mock_in_isr && (unsigned int)ep >= 0x81U && (unsigned int)ep <= 0x87U);
    busy_queries++; last_busy_ep = ep; return nrf_busy[(unsigned int)ep];
}
unsigned int irq_lock(void) {
    if (!irq_depth && before_irq_lock) {
        void (*cb)(void) = before_irq_lock; before_irq_lock = NULL; cb();
    }
    return irq_depth++;
}
void irq_unlock(unsigned int key) { assert(irq_depth == key + 1U); irq_depth = key; }
int k_sem_take(struct k_sem *sem, int timeout) {
    assert(!irq_depth && timeout == 30); takes++;
    if (during_wait) { void (*cb)(void) = during_wait; during_wait = NULL; cb(); }
    if (take_error) { return take_error; }
    if (!sem->count) { return -EAGAIN; }
    sem->count--; return 0;
}
void k_sem_give(struct k_sem *sem) {
    if (sem->count < sem->limit) { sem->count++; }
    gives++;
}
int k_work_submit(struct k_work *work) { assert(work); work_submits++; return 0; }
const struct device *device_get_binding(const char *name) {
    int id = !strcmp(name, "HID_0") ? 0 : !strcmp(name, "HID_1") ? 1 : -1;
    assert(id >= 0); binding_calls[id]++;
    return id == 1 && missing_pad ? NULL : &devices[id];
}
int usb_enable(usb_dc_status_callback cb) {
    /* The pad registers/initializes in POST_KERNEL, before this APPLICATION entry. */
    assert(cb && registration_calls[1] == 1U && hid_init_calls[1] == 1U);
    enable_calls++; actual_usb_callback = cb; return 0;
}
int usb_wakeup_request(void) { wakeup_calls++; return 0; }
void usb_hid_register_device(const struct device *dev, const uint8_t *desc, size_t len,
                              const struct hid_ops *ops_) {
    assert((dev == &devices[0] || dev == &devices[1]) && desc && ops_);
    int id = dev->id; registration_calls[id]++;
    registered_desc[id] = desc; registered_desc_len[id] = len; registered_ops[id] = ops_;
}
int usb_hid_set_proto_code(const struct device *dev, uint8_t code) {
    proto_code_calls[dev->id]++;
    if (!IS_ENABLED(CONFIG_USB_HID_BOOT_PROTOCOL)) { return -ENOTSUP; }
    const struct usb_cfg_data *cfg = dev->config;
    ((struct usb_if_descriptor *)cfg->interface_descriptor)->bInterfaceProtocol = code;
    return 0;
}
int usb_hid_init(const struct device *dev) {
    assert(registration_calls[dev->id] == 1U); hid_init_calls[dev->id]++;
    return dev->id == 1 ? pad_init_error : 0;
}
int hid_int_ep_write(const struct device *dev, const uint8_t *source, uint32_t len,
                      uint32_t *bytes_ret) {
    assert(!irq_depth && (dev == &devices[0] || dev == &devices[1]));
    struct dma_model *d = &dma[dev->id];
    assert(source && len <= sizeof(d->completed)); d->writes++;
    if (dev->id == 1 && before_driver_accept) {
        void (*cb)(void) = before_driver_accept; before_driver_accept = NULL; cb();
    }
    /* Like the fixed nRF driver, a live endpoint rejects another transfer. */
    if (d->pending) { return -EAGAIN; }
    if (dev->id == 1) {
        assert(((uintptr_t)source % sizeof(uint32_t)) == 0U);
        assert(len == 10U); /* ID4 + body9, NEVER the 12-byte aligned union. */
    }
    if (!write_error[dev->id]) {
        d->source = source; d->len = len; d->pending = true;
        const struct usb_cfg_data *cfg = dev->config;
        d->ep = cfg->endpoint[0].ep_addr;
        if (dev->id == 1) { assert(!nrf_busy[d->ep]); nrf_busy[d->ep] = true; }
    }
    if (dev->id == 1 && during_driver) {
        void (*cb)(void) = during_driver; during_driver = NULL; cb();
    }
    if (bytes_ret) { *bytes_ret = write_error[dev->id] ? 0U : len; }
    return write_error[dev->id];
}
struct zmk_hid_keyboard_report *zmk_hid_get_keyboard_report(void) { return &keyboard; }
struct zmk_hid_consumer_report *zmk_hid_get_consumer_report(void) { return &consumer; }
struct zmk_hid_mouse_report *zmk_hid_get_mouse_report(void) { return &mouse; }
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
zmk_hid_boot_report_t *zmk_hid_get_boot_report(void) { return &boot_keyboard; }
#endif
void zmk_hid_indicators_process_report(struct zmk_hid_led_report_body *body,
                                       struct zmk_endpoint_instance endpoint) {
    assert(endpoint.transport == ZMK_TRANSPORT_USB); last_leds = body->leds; led_sets++;
}
struct zmk_pointing_resolution_multipliers
zmk_pointing_resolution_multipliers_get_profile(struct zmk_endpoint_instance endpoint) {
    assert(endpoint.transport == ZMK_TRANSPORT_USB);
    return (struct zmk_pointing_resolution_multipliers){.wheel = 2, .hor_wheel = 3};
}
void zmk_pointing_resolution_multipliers_process_report(
    struct zmk_hid_mouse_resolution_feature_report_body *body,
    struct zmk_endpoint_instance endpoint) {
    assert(endpoint.transport == ZMK_TRANSPORT_USB);
    last_wheel = body->wheel_res; last_hwheel = body->hwheel_res; resolution_sets++;
}
#ifndef BBP_TEST_REAL_NEUTRAL
uint32_t bbp_gamepad_context_epoch(void) { return runtime_context_epoch; }
bool bbp_gamepad_context_valid(uint32_t expected) {
    context_checks++;
    if (during_context_check && context_checks == context_hook_at) {
        void (*cb)(void) = during_context_check; during_context_check = NULL; cb();
    }
    return expected == runtime_context_epoch;
}
void bbp_gamepad_transport_invalidate(void) { runtime_context_epoch++; }
void bbp_gamepad_get_report(uint8_t transport, struct bbp_gamepad_report_body *out) {
    assert(transport == 0U);
    *out = media_active && usb_selected ? runtime_body : bbp_gamepad_neutral();
}
#endif
struct zmk_usb_conn_state_changed { enum zmk_usb_conn_state conn_state; };
static int raise_zmk_usb_conn_state_changed(struct zmk_usb_conn_state_changed event) { return 0; }

/* Original HID_0 prefixes and SDK fixtures are unchanged. The new header is
 * included as standalone code; it must NOT redirect any stock API or symbol. */
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_usb_epoch.h"
#include "../config/boards/arm/bbp9981/custom_driver/gamepad_usb_lifecycle.h"
#include "sdk_v0_3_0_usb.c"
#undef usb_enable
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_usb_snapshot.h"
#include "sdk_v0_3_0_usb_hid.c"
#include "../config/boards/arm/bbp9981/custom_driver/gamepad_usb_extension.h"
#ifdef hid_int_ep_write
#error "Standalone Game Pad must not intercept HID_0 writes"
#endif
#ifdef usb_hid_register_device
#error "Standalone Game Pad must not intercept HID_0 registration"
#endif

static void bus_event(enum usb_dc_status_code status) {
    uint8_t config = 1U;
    actual_usb_callback(status, status == USB_DC_CONFIGURED ? &config : NULL);
    assert(zmk_usb_get_status() == status || status == USB_DC_SOF);
}
static void reset_and_configure(void) { bus_event(USB_DC_RESET); bus_event(USB_DC_CONFIGURED); }
static void exit_media(void) { media_active = false; bbp_gamepad_transport_invalidate(); }
static void enter_media(void) { media_active = true; bbp_gamepad_transport_invalidate(); }
static void suspend_bus(void) { bus_event(USB_DC_SUSPEND); }
static void pad_boot(void) { registered_ops[1]->protocol_change(&devices[1], HID_PROTOCOL_BOOT); }
static void pad_report(void) { registered_ops[1]->protocol_change(&devices[1], HID_PROTOCOL_REPORT); }
static void complete_dma(unsigned int id) {
    struct dma_model *d = &dma[id]; assert(d->pending && d->len <= sizeof(d->completed));
    memcpy(d->completed, d->source, d->len); d->completed_len = d->len; d->pending = false;
    if (id == 1U) { nrf_busy[d->ep] = false; }
    registered_ops[id]->int_in_ready(&devices[id]);
}
static void complete_pad(void) {
    complete_dma(1U);
    if (bbp_usb_tx_state == BBP_USB_TX_SUBMITTING) { assert(!bbp_usb_gamepad_sem.count); }
}
static void ready_without_dma(void) { registered_ops[1]->int_in_ready(&devices[1]); }
static void expect_pad(const struct bbp_gamepad_report *expected) {
    assert(sizeof(*expected) == 10U && sizeof(bbp_usb_tx_storage) == 12U);
    assert(dma[1].pending && dma[1].len == 10U);
    assert(dma[1].source == (const uint8_t *)&bbp_usb_tx_storage.report);
    assert(!memcmp(dma[1].source, expected, sizeof(*expected)));
}
static void finish_pad(const struct bbp_gamepad_report *expected) {
    expect_pad(expected);
    memset(dma[1].completed, 0xA5, sizeof(dma[1].completed));
    complete_pad();
    assert(dma[1].completed_len == 10U && dma[1].completed[10] == 0xA5U &&
           dma[1].completed[11] == 0xA5U); /* Aligned-union tail must not reach the wire. */
    assert(!memcmp(dma[1].completed, expected, sizeof(*expected)));
    assert(bbp_usb_gamepad_sem.count == 1U && bbp_usb_tx_state == BBP_USB_TX_IDLE);
}
static void clobber_stack(void) {
    volatile uint8_t bytes[2048];
    for (size_t i = 0; i < sizeof(bytes); i++) { bytes[i] = (uint8_t)(0xA5U ^ i); }
    assert(bytes[17] == (uint8_t)(0xA5U ^ 17U));
}
static int send_body(const struct bbp_gamepad_report_body *body) {
    return bbp_gamepad_usb_send(body, bbp9981_usb_hid_connection_epoch(), runtime_context_epoch);
}
static struct bbp_gamepad_report pressed_report(void) {
    return (struct bbp_gamepad_report){.report_id = 4U,
        .body = {.buttons_lo = 0x41U, .buttons_hi = 0x08U, .hat = 1U,
                 .x = -127, .y = 127, .z = -73, .rz = 42, .lt = 255U, .rt = 255U}};
}
static int send_stack_body(const struct bbp_gamepad_report_body *expected) {
    struct bbp_gamepad_report_body source = *expected;
    int err = send_body(&source);
    volatile uint8_t *overwrite = (volatile uint8_t *)&source;
    for (size_t i = 0U; i < sizeof(source); ++i) { overwrite[i] = 0xCCU; }
    clobber_stack();
    return err;
}
static void assert_stock_works(void) {
    assert(!dma[0].pending && registered_ops[0] == &ops);
    unsigned int before = dma[0].writes;
    assert(zmk_usb_hid_send_keyboard_report() == 0 && dma[0].writes == before + 1U);
    complete_dma(0U);
    assert(hid_sem.count == 1U);
}
static void init_fixture(void) {
    runtime_body = pressed_report().body;
    keyboard.body.keys[0] = 42; consumer.body.keys[0] = 0x123;
    mouse.body = (struct zmk_hid_mouse_report_body){.buttons = 1, .d_x = 812, .d_y = -34,
                                                  .d_scroll_y = 6, .d_scroll_x = -7};
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    boot_keyboard.keys[0] = 12;
#endif
    struct usb_if_descriptor untouched = stock_iface;
    assert(!strcmp(test_init_bbp_gamepad_usb_init.level, "POST_KERNEL") &&
           test_init_bbp_gamepad_usb_init.priority == 80);
    assert(!strcmp(test_init_zmk_usb_init.level, "APPLICATION"));
    assert(test_init_bbp_gamepad_usb_init.fn() == 0);
    assert(!enable_calls && !memcmp(&stock_iface, &untouched, sizeof(untouched)));
    assert(pad_iface.bInterfaceSubClass == 0U && pad_iface.bInterfaceProtocol == 0U);
    assert(bbp_gamepad_usb_init() == 0 && registration_calls[1] == 1U && hid_init_calls[1] == 1U);
    assert(binding_calls[1] == 1U && !proto_code_calls[1]);
    /* Follow the stock initialization entry order: USB enable 94, HID_0 95. */
    assert(zmk_usb_init() == 0 && enable_calls == 1U);
    assert(zmk_usb_hid_init() == 0 && registration_calls[0] == 1U && hid_init_calls[0] == 1U);
    assert(binding_calls[0] == 1U && registered_ops[0] == &ops);
    assert(proto_code_calls[0] == (unsigned int)IS_ENABLED(CONFIG_ZMK_USB_BOOT));
    assert(stock_iface.bInterfaceSubClass == (uint8_t)IS_ENABLED(CONFIG_ZMK_USB_BOOT));
    assert(stock_iface.bInterfaceProtocol == (uint8_t)IS_ENABLED(CONFIG_ZMK_USB_BOOT));
    bus_event(USB_DC_CONFIGURED); assert(zmk_usb_is_hid_ready() && work_submits);
}
struct descriptor_info { unsigned int ids[256], input[256], tlcs, gamepads; };
static struct descriptor_info parse_descriptor(const uint8_t *desc, size_t len) {
    struct descriptor_info result = {0};
    struct globals { unsigned int id, size, count, page; } g = {0}, stack[8];
    unsigned int sp = 0, depth = 0, usage = 0;
    for (size_t at = 0; at < len;) {
        uint8_t tag = desc[at++]; assert(tag != 0xFEU);
        unsigned int n = tag & 3U; if (n == 3U) { n = 4U; }
        unsigned int type = (tag >> 2U) & 3U, item = tag >> 4U, val = 0;
        assert(at + n <= len);
        for (unsigned int b = 0; b < n; b++) { val |= (unsigned int)desc[at++] << (8U * b); }
        if (type == 1U) {
            switch (item) {
            case 0: g.page = val; break;
            case 7: g.size = val; break;
            case 8: assert(val < 256U); g.id = val; result.ids[val]++; break;
            case 9: g.count = val; break;
            case 10: assert(sp < 8U); stack[sp++] = g; break;
            case 11: assert(sp); g = stack[--sp]; break;
            default: break;
            }
        } else if (type == 2U && item == 0U) { usage = val; }
        else if (type == 0U) {
            if (item == 8U) { result.input[g.id] += g.size * g.count; }
            if (item == 10U) {
                if (!depth) {
                    result.tlcs++;
                    if (g.page == 1U && usage == 5U && val == 1U) { result.gamepads++; }
                }
                depth++;
            }
            if (item == 12U) { assert(depth); depth--; }
            usage = 0;
        }
    }
    assert(!depth && !sp); return result;
}
static void test_descriptors(void) {
    assert(sizeof(zmk_hid_report_desc) > 200U && registered_desc[0] == zmk_hid_report_desc);
    assert(registered_desc_len[0] == sizeof(zmk_hid_report_desc));
    assert(registered_desc[1] == bbp_usb_gamepad_desc && registered_desc_len[1] == sizeof(bbp_usb_gamepad_desc));
    struct descriptor_info old = parse_descriptor(registered_desc[0], registered_desc_len[0]);
    struct descriptor_info pad = parse_descriptor(registered_desc[1], registered_desc_len[1]);
    assert(old.tlcs == 3U && !old.gamepads && !old.ids[4]);
    assert(old.ids[1] == 1U && old.ids[2] == 1U && old.ids[3] == 1U);
    assert(old.input[1] == sizeof(keyboard.body) * 8U);
    assert(old.input[2] == sizeof(consumer.body) * 8U && old.input[3] == sizeof(mouse.body) * 8U);
    assert(pad.tlcs == 1U && pad.gamepads == 1U && pad.ids[4] == 1U && pad.input[4] == 72U);
    for (unsigned int id = 0; id < 256; id++) {
        if (id != 4U) { assert(!pad.ids[id] && !pad.input[id]); }
    }
    assert(sizeof(struct bbp_gamepad_report_body) == 9U && sizeof(struct bbp_gamepad_report) == 10U);
    assert(sizeof(struct bbp_gamepad_report) * 8U == 80U && sizeof(bbp_usb_tx_storage) == 12U);
    assert(sizeof(bbp_usb_control_snapshot) == 10U && offsetof(struct bbp_gamepad_report, body) == 1U);
    assert(offsetof(struct bbp_gamepad_report_body, buttons_lo) == 0U &&
           offsetof(struct bbp_gamepad_report_body, buttons_hi) == 1U &&
           offsetof(struct bbp_gamepad_report_body, hat) == 2U &&
           offsetof(struct bbp_gamepad_report_body, x) == 3U &&
           offsetof(struct bbp_gamepad_report_body, y) == 4U &&
           offsetof(struct bbp_gamepad_report_body, z) == 5U &&
           offsetof(struct bbp_gamepad_report_body, rz) == 6U &&
           offsetof(struct bbp_gamepad_report_body, lt) == 7U &&
           offsetof(struct bbp_gamepad_report_body, rt) == 8U);
}
static int control_get(unsigned int id, uint16_t value, int32_t *len, uint8_t **data) {
    struct usb_setup_packet setup = {.bmRequestType = 0xA1, .bRequest = 1, .wValue = value};
    return registered_ops[id]->get_report(&devices[id], &setup, len, data);
}
static void test_control(void) {
    /* Exact stock table identity proves no callback substitution/delegation drift. */
    assert(registered_ops[0] == &ops && registered_ops[0]->get_report == get_report_cb &&
           registered_ops[0]->set_report == set_report_cb && registered_ops[0]->int_in_ready == in_ready_cb);
    int32_t len = 0; uint8_t *data = NULL;
    assert(control_get(0, 0x101, &len, &data) == 0 && data == (uint8_t *)&keyboard);
    assert(control_get(0, 0x102, &len, &data) == 0 && data == (uint8_t *)&consumer);
    assert(control_get(0, 0x104, &len, &data) == -EINVAL);
    assert(control_get(0, 0x303, &len, &data) == 0 && len == sizeof(struct zmk_hid_mouse_resolution_feature_report));
    struct zmk_hid_led_report leds = {.report_id = 1, .body = {.leds = 0x15}};
    struct usb_setup_packet setup = {.wValue = HID_REPORT_TYPE_OUTPUT | ZMK_HID_REPORT_ID_LEDS};
    len = sizeof(leds); data = (uint8_t *)&leds;
    assert(registered_ops[0]->set_report(&devices[0], &setup, &len, &data) == 0);
    assert(last_leds == 0x15 && led_sets == 1U);
    struct zmk_hid_mouse_resolution_feature_report feature = {.report_id = 3,
        .body = {.wheel_res = 4, .hwheel_res = 5}};
    setup.wValue = HID_REPORT_TYPE_FEATURE | ZMK_HID_REPORT_ID_MOUSE;
    len = sizeof(feature); data = (uint8_t *)&feature;
    assert(registered_ops[0]->set_report(&devices[0], &setup, &len, &data) == 0);
    assert(last_wheel == 4 && last_hwheel == 5 && resolution_sets == 1U);
    assert(registered_ops[1]->set_report(&devices[1], &setup, &len, &data) == -ENOTSUP);
    for (uint16_t type = 1; type <= 3; type++) {
        for (uint16_t id = 1; id <= 5; id++) {
            if (type == 1 && id == 4) { continue; }
            assert(control_get(1, (uint16_t)((type << 8) | id), &len, &data) == -ENOTSUP);
        }
    }
    assert(control_get(1, 0x104, &len, &data) == 0 && len == 10);
    assert(data == (uint8_t *)&bbp_usb_control_snapshot);
    const uint8_t pressed_bytes[10] = {4U,0x41U,0x08U,1U,0x81U,0x7fU,0xb7U,0x2aU,0xffU,0xffU};
    const uint8_t neutral_bytes[10] = {4U,0U,0U,8U,0U,0U,0U,0U,0U,0U};
    struct bbp_gamepad_report expected = pressed_report();
    assert(!memcmp(data, &expected, 10U) && !memcmp(data, pressed_bytes, 10U));
    media_active = false; assert(control_get(1, 0x104, &len, &data) == 0 && len == 10);
    assert(bbp_gamepad_is_neutral(&((struct bbp_gamepad_report *)data)->body));
    assert(!memcmp(data, neutral_bytes, 10U));
    media_active = true; usb_selected = false;
    assert(control_get(1, 0x104, &len, &data) == 0 && len == 10);
    assert(bbp_gamepad_is_neutral(&((struct bbp_gamepad_report *)data)->body));
    assert(!memcmp(data, neutral_bytes, 10U)); usb_selected = true;
}
static void test_deferred_and_independent(void) {
    struct bbp_gamepad_report gp = pressed_report();
    /* All four axes + both digital trigger bytes survive a returned caller
     * frame, explicit source overwrite, and another stack clobber. */
    assert(send_stack_body(&gp.body) == 0 && !bbp_usb_gamepad_sem.count && hid_sem.count == 1U);
    clobber_stack(); expect_pad(&gp);
    int32_t len; uint8_t *data;
    /* Intermediate trigger bytes exercise transport only, not physical half-press. */
    runtime_body = (struct bbp_gamepad_report_body){.buttons_lo = 0x12U, .buttons_hi = 0x40U,
        .hat = 6U, .x = 91, .y = -92, .z = 93, .rz = -94, .lt = 0x12U, .rt = 0xe7U};
    const struct bbp_gamepad_report control_expected = {.report_id = 4U, .body = runtime_body};
    assert(control_get(1, 0x104, &len, &data) == 0 && len == 10 && data != dma[1].source);
    assert(data == (uint8_t *)&bbp_usb_control_snapshot &&
           !memcmp(data, &control_expected, 10U));
    struct bbp_gamepad_report control_copy = *(struct bbp_gamepad_report *)data;
    runtime_body = bbp_gamepad_neutral();
    clobber_stack(); assert(!memcmp(data, &control_copy, 10U)); expect_pad(&gp);
    assert(zmk_usb_hid_send_keyboard_report() == 0 && dma[0].pending && dma[1].pending);
    unsigned int before = dma[1].writes;
    /* Preserve (do not extend) stock's known ignored-timeout behavior. Its
     * false semaphore give must NOT grant ownership of the pad endpoint. */
    assert(zmk_usb_hid_send_consumer_report() == -EAGAIN && hid_sem.count == 1U);
    assert(!bbp_usb_gamepad_sem.count && send_body(&gp.body) == -EAGAIN && dma[1].writes == before);
    registered_ops[1]->int_in_ready(&devices[0]); /* Wrong-device event cannot unlock pad. */
    assert(!bbp_usb_gamepad_sem.count); expect_pad(&gp);
    complete_dma(0); assert(!bbp_usb_gamepad_sem.count); finish_pad(&gp);
    assert(!memcmp(data, &control_copy, 10U));
    /* Deliberately DIFFERENT data: a new IN must not mutate any EP0 field. */
    struct bbp_gamepad_report raw_wire = {.report_id = 4U,
        .body = {.buttons_lo = 0xc1U, .buttons_hi = 0x10U, .hat = 2U,
                 .x = -91, .y = 92, .z = -93, .rz = 94, .lt = 0xe7U, .rt = 0x12U}};
    assert(send_stack_body(&raw_wire.body) == 0); clobber_stack();
    assert(!memcmp(data, &control_copy, 10U)); finish_pad(&raw_wire);
    assert(!memcmp(data, &control_copy, 10U)); runtime_body = gp.body;
    /* Original mouse and consumer remain on HID_0, even while HID_1 is busy. */
    assert(send_body(&gp.body) == 0);
    assert(zmk_usb_hid_send_mouse_report() == 0 && dma[0].source == (uint8_t *)&mouse);
    complete_dma(0); assert(zmk_usb_hid_send_consumer_report() == 0); complete_dma(0); finish_pad(&gp);
    assert_stock_works();
}
static void test_wait_cancellation(void) {
    struct bbp_gamepad_report gp = pressed_report();
    struct bbp_gamepad_report neutral = {.report_id = 4, .body = bbp_gamepad_neutral()};
    uint32_t epoch = bbp9981_usb_hid_connection_epoch(), context = runtime_context_epoch;
    unsigned int before = dma[1].writes;
    during_wait = exit_media;
    assert(bbp_gamepad_usb_send(&gp.body, epoch, context) == -ESTALE && dma[1].writes == before);
    assert(bbp_usb_gamepad_sem.count == 1U);
    assert(bbp_gamepad_usb_send(&neutral.body, epoch, context) == 0); finish_pad(&neutral); enter_media();
    context = runtime_context_epoch; during_wait = reset_and_configure;
    assert(bbp_gamepad_usb_send(&gp.body, epoch, context) == -ESTALE && dma[1].writes == before + 1U);
    assert(bbp_gamepad_usb_send(&neutral.body, epoch, context) == -ESTALE);
    assert(bbp_usb_tx_state == BBP_USB_TX_IDLE);
    assert(send_body(&gp.body) == 0); finish_pad(&gp);
    before = dma[1].writes; during_wait = suspend_bus;
    assert(send_body(&gp.body) == -ESTALE && dma[1].writes == before && bbp_usb_gamepad_sem.count == 1U); /* Raw SUSPEND cancels the captured context immediately. */
    bus_event(USB_DC_CONFIGURED);
    context_checks = 0; context_hook_at = 4; during_context_check = exit_media;
    assert(send_body(&gp.body) == -ESTALE && context_checks == 4U && dma[1].writes == before);
    assert(bbp_usb_tx_state == BBP_USB_TX_IDLE && bbp_usb_gamepad_sem.count == 1U); enter_media();
    /* A real completion while the next sender waits allows safe reuse. */
    assert(send_body(&gp.body) == 0); during_wait = complete_pad;
    assert(send_body(&gp.body) == 0); finish_pad(&gp);
}
static void test_trigger_only_requires_current_context(void) {
    /* 255 is the physical digital press. Other values are raw-wire probes. */
    static const uint8_t trigger_bytes[][2] = {
        {255U,0U}, {0U,255U}, {255U,255U}, {1U,0U}, {0U,128U}, {0x12U,0xe7U},
    };
    for (size_t i = 0U; i < sizeof(trigger_bytes) / sizeof(trigger_bytes[0]); ++i) {
        struct bbp_gamepad_report gp = {.report_id = 4U, .body = bbp_gamepad_neutral()};
        gp.body.lt = trigger_bytes[i][0]; gp.body.rt = trigger_bytes[i][1];
        assert(!gp.body.buttons_lo && !gp.body.buttons_hi && !gp.body.x && !gp.body.y &&
               !gp.body.z && !gp.body.rz && gp.body.hat == 8U && !bbp_gamepad_is_neutral(&gp.body));
        uint32_t usb_epoch = bbp9981_usb_hid_connection_epoch(), context = runtime_context_epoch;
        exit_media();
        unsigned int before_writes = dma[1].writes, before_takes = takes;
        assert(bbp_gamepad_usb_send(&gp.body, usb_epoch, context) == -ESTALE);
        assert(dma[1].writes == before_writes && takes == before_takes &&
               bbp_usb_gamepad_sem.count == 1U && bbp_usb_tx_state == BBP_USB_TX_IDLE);
        struct bbp_gamepad_report neutral = {.report_id = 4U, .body = bbp_gamepad_neutral()};
        assert(bbp_gamepad_usb_send(&neutral.body, usb_epoch, context) == 0); finish_pad(&neutral);
        enter_media();
        assert(send_stack_body(&gp.body) == 0); clobber_stack(); finish_pad(&gp);
    }
    /* Trigger-only also retains its context requirement across the sem wait. */
    struct bbp_gamepad_report gp = {.report_id = 4U, .body = bbp_gamepad_neutral()};
    gp.body.rt = 255U;
    unsigned int before = dma[1].writes;
    during_wait = exit_media;
    assert(send_body(&gp.body) == -ESTALE && dma[1].writes == before);
    assert(bbp_usb_gamepad_sem.count == 1U && bbp_usb_tx_state == BBP_USB_TX_IDLE);
    enter_media();
}
static void arm_exit_at_final_irq(void) { before_irq_lock = exit_media; }
static void arm_boot_at_final_irq(void) { before_irq_lock = pad_boot; }
static void test_final_commit_window(void) {
    struct bbp_gamepad_report gp = pressed_report();
    unsigned int before = dma[1].writes;
    context_checks = 0; context_hook_at = 4; during_context_check = arm_exit_at_final_irq;
    assert(send_body(&gp.body) == -ESTALE && dma[1].writes == before);
    assert(context_checks == 5U && bbp_usb_tx_state == BBP_USB_TX_IDLE && bbp_usb_gamepad_sem.count == 1U);
    enter_media();
    context_checks = 0; context_hook_at = 4; during_context_check = arm_boot_at_final_irq;
    assert(send_body(&gp.body) == -ENOTSUP && dma[1].writes == before);
    assert(bbp_usb_tx_state == BBP_USB_TX_IDLE && bbp_usb_gamepad_sem.count == 1U);
    pad_report(); assert(send_body(&gp.body) == 0); finish_pad(&gp);
}
static void test_raw_configuration_and_protocol_reset(void) {
    struct bbp_gamepad_report gp = pressed_report();
    uint8_t zero = 0U;
    unsigned int before = dma[1].writes, before_takes = takes;
    actual_usb_callback(USB_DC_CONFIGURED, &zero);
    assert(zmk_usb_is_hid_ready()); /* Exposes stock CONFIGURED(0) ambiguity. */
    assert(!bbp_gamepad_usb_is_configured());
    assert(send_body(&gp.body) == -ENODEV && dma[1].writes == before && takes == before_takes);
    bus_event(USB_DC_CONFIGURED); assert(bbp_gamepad_usb_is_configured());
    uint32_t context = runtime_context_epoch; bus_event(USB_DC_SOF);
    assert(runtime_context_epoch == context && bbp_gamepad_usb_is_configured());
    pad_boot(); bus_event(USB_DC_RESET);
    assert(bbp_usb_gamepad_protocol == HID_PROTOCOL_REPORT && !bbp_gamepad_usb_is_configured());
    bus_event(USB_DC_CONFIGURED); assert(send_body(&gp.body) == 0); finish_pad(&gp);
    assert(enable_calls == 1U && registration_calls[0] == 1U && registration_calls[1] == 1U);
    assert_stock_works();
}
static void test_status_and_errors(void) {
    struct bbp_gamepad_report gp = pressed_report();
    unsigned int before = dma[1].writes, before_takes = takes;
    assert(bbp_gamepad_usb_send(NULL, 0, 0) == -EINVAL);
    bus_event(USB_DC_SUSPEND);
    assert(send_body(&gp.body) == -EAGAIN && takes == before_takes && dma[1].writes == before);
    assert(!wakeup_calls); bus_event(USB_DC_CONFIGURED);
    const enum usb_dc_status_code invalid[] = {USB_DC_RESET, USB_DC_DISCONNECTED,
                                               USB_DC_UNKNOWN, USB_DC_ERROR, USB_DC_CONNECTED};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        bus_event(invalid[i]); before_takes = takes;
        assert(send_body(&gp.body) == -ENODEV && takes == before_takes && dma[1].writes == before);
        bus_event(USB_DC_CONFIGURED);
    }
    take_error = -ETIMEDOUT; assert(send_body(&gp.body) == -ETIMEDOUT && dma[1].writes == before);
    take_error = 0; write_error[1] = -EIO;
    assert(send_body(&gp.body) == -EIO && dma[1].writes == before + 1U && !dma[1].pending);
    assert(bbp_usb_gamepad_sem.count == 1U && bbp_usb_tx_state == BBP_USB_TX_IDLE);
    write_error[1] = 0; ready_without_dma();
    assert(bbp_usb_gamepad_sem.count == 1U); /* Stray ready cannot invent extra slots. */
    assert(send_body(&gp.body) == 0); finish_pad(&gp);
    during_driver = complete_pad;
    assert(send_body(&gp.body) == 0 && !dma[1].pending && bbp_usb_gamepad_sem.count == 1U);
    assert(dma[1].completed_len == sizeof(gp) && !memcmp(dma[1].completed, &gp, sizeof(gp)));
    assert(send_body(&gp.body) == 0); finish_pad(&gp);
}
static void test_protocol_independence(void) {
    struct bbp_gamepad_report gp = pressed_report();
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    uint32_t old_context = runtime_context_epoch;
    registered_ops[0]->protocol_change(&devices[0], HID_PROTOCOL_BOOT);
    assert(hid_protocol == HID_PROTOCOL_BOOT && runtime_context_epoch == old_context);
    assert(bbp_usb_gamepad_protocol == HID_PROTOCOL_REPORT);
    /* BOOT on the original keyboard interface does not turn the pad into BOOT. */
    assert(send_body(&gp.body) == 0); finish_pad(&gp);
    assert(zmk_usb_hid_send_consumer_report() == -ENOTSUP && zmk_usb_hid_send_mouse_report() == -ENOTSUP);
    assert_stock_works(); assert(dma[0].completed_len == sizeof(boot_keyboard));
    registered_ops[0]->protocol_change(&devices[0], HID_PROTOCOL_REPORT);
#endif
    assert(send_body(&gp.body) == 0); uint32_t context = runtime_context_epoch;
    pad_boot(); assert(runtime_context_epoch == context + 1U); expect_pad(&gp);
    unsigned int before = dma[1].writes;
    assert(send_body(&gp.body) == -ENOTSUP && dma[1].writes == before);
    int32_t len; uint8_t *data; assert(control_get(1, 0x104, &len, &data) == -ENOTSUP);
    assert_stock_works(); finish_pad(&gp); pad_report();
    during_wait = pad_boot;
    assert(send_body(&gp.body) == -ENOTSUP && dma[1].writes == before && bbp_usb_gamepad_sem.count == 1U);
    pad_report(); during_driver = pad_boot;
    assert(send_body(&gp.body) == 0); /* Already-entered DMA cannot be revoked. */
    finish_pad(&gp); pad_report();
}
static void abort_pad_no_ready(void) {
    assert(dma[1].pending); dma[1].pending = false; nrf_busy[dma[1].ep] = false;
}
static void old_ready_during_submit(void) {
    assert(bbp_usb_tx_state == BBP_USB_TX_SUBMITTING && !bbp_usb_gamepad_sem.count);
    assert(!nrf_busy[(unsigned int)bbp_usb_tx_ep]);
    unsigned int before_queries = busy_queries, before_writes = dma[1].writes;
    ready_without_dma();
    assert(bbp_usb_tx_state == BBP_USB_TX_SUBMITTING && !bbp_usb_gamepad_sem.count);
    assert(busy_queries == before_queries); /* No busy=false oracle during SUBMITTING. */
    struct bbp_gamepad_report gp = pressed_report();
    assert(send_body(&gp.body) == -EAGAIN && dma[1].writes == before_writes);
}
static void complete_during_submit(void) {
    assert(bbp_usb_tx_state == BBP_USB_TX_SUBMITTING);
    unsigned int before_queries = busy_queries;
    complete_pad();
    assert(bbp_usb_tx_state == BBP_USB_TX_SUBMITTING && !bbp_usb_gamepad_sem.count);
    assert(busy_queries == before_queries);
}
static void abort_reset_during_submit(void) {
    abort_pad_no_ready(); reset_and_configure(); old_ready_during_submit();
}
static void test_reset_recovery(void) {
    struct bbp_gamepad_report gp = pressed_report();
    struct bbp_gamepad_report neutral = {.report_id = 4, .body = bbp_gamepad_neutral()};
    uint32_t old_epoch = bbp9981_usb_hid_connection_epoch(), old_context = runtime_context_epoch;
    assert(send_body(&gp.body) == 0 && bbp_usb_tx_ep == NRFX_USBD_EPIN6);
    unsigned int before = dma[1].writes; reset_and_configure();
    assert(bbp_gamepad_usb_send(&neutral.body, old_epoch, old_context) == -ESTALE);
    ready_without_dma(); /* Old callback + busy=true must not release old RAM. */
    assert(!bbp_usb_gamepad_sem.count && bbp_usb_tx_state == BBP_USB_TX_INFLIGHT);
    assert(send_body(&neutral.body) == -EAGAIN && dma[1].writes == before); expect_pad(&gp);
    assert_stock_works(); abort_pad_no_ready(); exit_media();
    /* Current USB epoch + neutral may recover RAM and release an old context. */
    assert(bbp_gamepad_usb_send(&neutral.body, bbp9981_usb_hid_connection_epoch(), old_context) == 0);
    assert(dma[1].writes == before + 1U); expect_pad(&neutral);
    ready_without_dma(); /* Another old event cannot retire NEW busy transfer. */
    assert(!bbp_usb_gamepad_sem.count && bbp_usb_tx_state == BBP_USB_TX_INFLIGHT);
    expect_pad(&neutral); finish_pad(&neutral); enter_media(); assert_stock_works();
    assert(registration_calls[0] == 1U && registration_calls[1] == 1U);
}
static void test_abort(void) {
    struct bbp_gamepad_report gp = pressed_report();
    struct bbp_gamepad_report neutral = {.report_id = 4, .body = bbp_gamepad_neutral()};
    assert(send_body(&gp.body) == 0); unsigned int before = dma[1].writes;
    abort_pad_no_ready(); pad_boot();
    assert(send_body(&neutral.body) == -ENOTSUP && bbp_usb_tx_state == BBP_USB_TX_INFLIGHT);
    pad_report(); suspend_bus(); unsigned int before_queries = busy_queries;
    assert(send_body(&neutral.body) == -EAGAIN && dma[1].writes == before);
    ready_without_dma(); assert(busy_queries == before_queries && !bbp_usb_gamepad_sem.count);
    bus_event(USB_DC_CONFIGURED); /* No completion event was necessary. */
    assert(send_body(&neutral.body) == 0 && dma[1].writes == before + 1U);
    finish_pad(&neutral); assert_stock_works();
}
static void test_submit_reset(void) {
    struct bbp_gamepad_report gp = pressed_report();
    struct bbp_gamepad_report neutral = {.report_id = 4, .body = bbp_gamepad_neutral()};
    during_driver = reset_and_configure;
    assert(send_body(&gp.body) == -ESTALE && dma[1].pending && !bbp_usb_gamepad_sem.count);
    unsigned int before = dma[1].writes; expect_pad(&gp); ready_without_dma();
    assert(send_body(&neutral.body) == -EAGAIN && dma[1].writes == before);
    abort_pad_no_ready(); assert(send_body(&neutral.body) == 0); finish_pad(&neutral); assert_stock_works();
}
static void test_submit_reset_abort(void) {
    struct bbp_gamepad_report gp = pressed_report(); during_driver = abort_reset_during_submit;
    assert(send_body(&gp.body) == -ESTALE && !dma[1].pending);
    assert(bbp_usb_tx_state == BBP_USB_TX_IDLE && bbp_usb_gamepad_sem.count == 1U);
    struct bbp_gamepad_report neutral = {.report_id = 4, .body = bbp_gamepad_neutral()};
    assert(send_body(&neutral.body) == 0); finish_pad(&neutral); assert_stock_works();
}
static void test_error_ready(void) {
    struct bbp_gamepad_report gp = pressed_report(); write_error[1] = -EIO;
    during_driver = old_ready_during_submit;
    assert(send_body(&gp.body) == -EIO && !dma[1].pending && bbp_usb_tx_state == BBP_USB_TX_IDLE);
    assert(bbp_usb_gamepad_sem.count == 1U); write_error[1] = 0;
    assert(send_body(&gp.body) == 0 && dma[1].writes == 2U); finish_pad(&gp); assert_stock_works();
}
static void test_stale_ready(void) {
    struct bbp_gamepad_report gp = pressed_report();
    before_driver_accept = old_ready_during_submit;
    assert(send_body(&gp.body) == 0 && bbp_usb_tx_state == BBP_USB_TX_INFLIGHT);
    unsigned int before = dma[1].writes; ready_without_dma();
    assert(!bbp_usb_gamepad_sem.count && send_body(&gp.body) == -EAGAIN && dma[1].writes == before);
    expect_pad(&gp); finish_pad(&gp);
    assert(send_body(&gp.body) == 0); ready_without_dma();
    assert(!bbp_usb_gamepad_sem.count); expect_pad(&gp); finish_pad(&gp);
    during_driver = complete_during_submit;
    assert(send_body(&gp.body) == 0 && bbp_usb_tx_state == BBP_USB_TX_IDLE && bbp_usb_gamepad_sem.count == 1U);
    assert_stock_works();
}
static void test_endpoint_and_isr(void) {
    struct bbp_gamepad_report gp = pressed_report();
    unsigned int before = dma[1].writes, before_queries = busy_queries;
    const uint8_t invalid[] = {0x00, 0x01, 0x80, 0x88, 0x89, 0x90, 0xFF};
    for (size_t i = 0; i < sizeof(invalid); i++) {
        pad_eps[0].ep_addr = invalid[i]; assert(send_body(&gp.body) == -EINVAL);
    }
    pad_eps[0].ep_addr = 0x86; pad_cfg.num_endpoints = 0;
    assert(send_body(&gp.body) == -ENODEV); pad_cfg.num_endpoints = 2;
    pad_cfg.endpoint = NULL; assert(send_body(&gp.body) == -ENODEV); pad_cfg.endpoint = pad_eps;
    devices[1].config = NULL; assert(send_body(&gp.body) == -ENODEV); devices[1].config = &pad_cfg;
    assert(dma[1].writes == before && busy_queries == before_queries);
    mock_in_isr = true; assert(send_body(&gp.body) == -EWOULDBLOCK); ready_without_dma();
    assert(dma[1].writes == before && busy_queries == before_queries); mock_in_isr = false;
    assert(send_body(&gp.body) == 0); abort_pad_no_ready(); before_queries = busy_queries;
    mock_in_isr = true; ready_without_dma();
    assert(!bbp_usb_gamepad_sem.count && busy_queries == before_queries); mock_in_isr = false;
    /* Store/retire the ORIGINAL EP, even if a later configuration uses EP7. */
    pad_eps[0].ep_addr = 0x87; nrf_busy[0x87] = true; ready_without_dma();
    assert(last_busy_ep == NRFX_USBD_EPIN6 && bbp_usb_tx_state == BBP_USB_TX_IDLE);
    assert(send_body(&gp.body) == -EBUSY && last_busy_ep == NRFX_USBD_EPIN7);
    nrf_busy[0x87] = false; assert(send_body(&gp.body) == 0 && bbp_usb_tx_ep == NRFX_USBD_EPIN7);
    ready_without_dma(); assert(!bbp_usb_gamepad_sem.count); expect_pad(&gp); finish_pad(&gp);
    pad_eps[0].ep_addr = 0x86; assert_stock_works();
}
static void test_init_failure(const char *mode) {
    int expected = -ENODEV;
    if (!strcmp(mode, "missing")) { missing_pad = true; }
    else if (!strcmp(mode, "bad-cfg")) { pad_iface.bInterfaceClass = 0xFF; expected = -EINVAL; }
    else { assert(!strcmp(mode, "init-fail")); pad_init_error = expected = -EIO; }
    struct usb_if_descriptor untouched = stock_iface;
    assert(bbp_gamepad_usb_init() == expected && bbp_gamepad_usb_init() == expected);
    assert(binding_calls[1] == 1U && !registration_calls[0] && !enable_calls);
    assert(registration_calls[1] == (unsigned int)(pad_init_error != 0));
    assert(hid_init_calls[1] == registration_calls[1] && !bbp_usb_gamepad_dev);
    assert(!memcmp(&stock_iface, &untouched, sizeof(untouched)));
    struct bbp_gamepad_report gp = pressed_report(); assert(send_body(&gp.body) == -ENODEV);
}
int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "normal";
    if (!strcmp(mode, "missing") || !strcmp(mode, "bad-cfg") || !strcmp(mode, "init-fail")) {
        test_init_failure(mode);
    } else {
        init_fixture();
        if (!strcmp(mode, "abort")) { test_abort(); }
        else if (!strcmp(mode, "submit-reset")) { test_submit_reset(); }
        else if (!strcmp(mode, "submit-reset-abort")) { test_submit_reset_abort(); }
        else if (!strcmp(mode, "error-ready")) { test_error_ready(); }
        else if (!strcmp(mode, "stale-ready")) { test_stale_ready(); }
        else if (!strcmp(mode, "endpoints")) { test_endpoint_and_isr(); }
        else {
            assert(!strcmp(mode, "normal")); test_descriptors(); test_control();
            test_deferred_and_independent(); test_wait_cancellation();
            test_trigger_only_requires_current_context(); test_status_and_errors();
            test_protocol_independence(); test_stale_ready(); test_endpoint_and_isr(); test_reset_recovery();
            test_final_commit_window(); test_raw_configuration_and_protocol_reset();
        }
    }
    assert(!irq_depth);
    printf("PASS standalone USB HID_1 BOOT=%d %s: real SDK HID_0 unchanged, 72-bit body/80-bit wire, "
           "12-byte aligned staging/10-byte EP0, active sticks+LT/RT, deferred DMA/context/epoch gate\n",
           IS_ENABLED(CONFIG_ZMK_USB_BOOT), mode);
    return 0;
}
