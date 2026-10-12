/* Real pinned v0.3.0 HID replay, not a reimplementation of its ref logic.
 * Compile with -include config/.../trackpad_hid_owners.h (as the target HID TU).
 * Replay: PYTHONDONTWRITEBYTECODE=1 python3 tests/hid_snapshot_mocks/replay.py
 * No endpoint, send, wait, hardware, or contact-provider API exists in this TU.
 * SPDX-License-Identifier: MIT */
#include "sdk_v0_3_0_hid.c"

/* Callers in other TUs do NOT receive the prefix's renaming macros. */
#undef zmk_hid_mouse_button_press
#undef zmk_hid_mouse_button_release
#undef zmk_hid_mouse_buttons_press
#undef zmk_hid_mouse_buttons_release

#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>

_Static_assert(IS_ENABLED(CONFIG_ZMK_HID_REPORT_TYPE_HKRO), "Use the real HKRO layout");
_Static_assert(CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE == 6, "Standard six-key HKRO");
_Static_assert(sizeof(struct zmk_hid_keyboard_report_body) == 8, "HKRO body ABI");
_Static_assert(sizeof(struct zmk_hid_keyboard_report) == 9, "HKRO report ABI");
_Static_assert(sizeof(zmk_hid_boot_report_t) == 8, "Boot report ABI");
_Static_assert(sizeof(struct zmk_hid_consumer_report_body) ==
               CONFIG_ZMK_HID_CONSUMER_REPORT_SIZE *
               (IS_ENABLED(CONFIG_ZMK_HID_CONSUMER_REPORT_USAGES_FULL) ? 2 : 1),
               "Consumer report ABI must follow the selected official configuration");

#if IS_ENABLED(CONFIG_ZMK_POINTING)
_Static_assert(_Generic((zmk_mouse_button_t)0, uint16_t: 1, default: 0), "Single is uint16_t");
_Static_assert(_Generic((zmk_mouse_button_flags_t)0, uint8_t: 1, default: 0), "Flags are uint8_t");
_Static_assert(sizeof(struct zmk_hid_mouse_report_body) == 9, "Packed mouse body ABI");
_Static_assert(sizeof(struct zmk_hid_mouse_report) == 10, "Packed mouse report ABI");
_Static_assert(offsetof(struct zmk_hid_mouse_report_body, d_x) == 1, "X offset");
_Static_assert(offsetof(struct zmk_hid_mouse_report_body, d_y) == 3, "Y offset");
_Static_assert(offsetof(struct zmk_hid_mouse_report_body, d_scroll_y) == 5, "Wheel offset");
_Static_assert(offsetof(struct zmk_hid_mouse_report_body, d_scroll_x) == 7, "Hwheel offset");

static unsigned int irq_depth, max_irq_depth;
static unsigned int locks, unlocks, probe_locks, probe_unlocks;
static unsigned int ref_mutations, locked_stock_logs, clear_calls;
static unsigned int expected_stock_depth = 1;
static bool in_getter_probe;
static void (*pending_interrupt)(void);
static unsigned int blocked_preemptions, interrupt_runs;

unsigned int hid_mock_irq_lock(void) {
    /* A nested Zephyr-style key remembers whether IRQs were already disabled. */
    unsigned int key = irq_depth != 0;
    ++irq_depth;
    ++locks;
    if (in_getter_probe) { ++probe_locks; }
    if (irq_depth > max_irq_depth) { max_irq_depth = irq_depth; }
    return key;
}

void hid_mock_irq_unlock(unsigned int key) {
    assert(irq_depth > 0);
    assert(key == (unsigned int)(irq_depth > 1));
    --irq_depth;
    ++unlocks;
    if (in_getter_probe) { ++probe_unlocks; }
    if (!irq_depth && pending_interrupt) {
        void (*callback)(void) = pending_interrupt;
        pending_interrupt = NULL;
        ++interrupt_runs;
        callback();
    }
}

void hid_mock_log(const char *function, int error, const char *format, ...) {
    bool stock_single =
        strcmp(function, "bbp9981_stock_zmk_hid_mouse_button_press") == 0 ||
        strcmp(function, "bbp9981_stock_zmk_hid_mouse_button_release") == 0;
    (void)error;
    if (strcmp(function, "zmk_hid_mouse_clear") == 0) { ++clear_calls; }
    if (!stock_single) { return; }

    /* The actual SDK logs immediately after every ref increment/decrement,
     * plus report writes and release-underflow errors. Multi calls must still
     * be inside ONE outer wrapper lock, not recursive public single wrappers. */
    assert(irq_depth == expected_stock_depth);
    ++locked_stock_logs;
    if (strncmp(format, "Button %d count", strlen("Button %d count")) == 0) {
        va_list args;
        va_start(args, format);
        unsigned int button = (unsigned int)va_arg(args, int);
        int sdk_count = va_arg(args, int);
        va_end(args);
        assert(button < ZMK_HID_MOUSE_NUM_BUTTONS);
        unsigned int depth = irq_depth;
        in_getter_probe = true;
        int readback = bbp9981_hid_mouse_button_references(button);
        in_getter_probe = false;
        assert(readback == sdk_count && readback == explicit_button_counts[button]);
        assert(irq_depth == depth);
        ++ref_mutations;
    }
    if (strcmp(format, "Mouse buttons set to 0x%02X") == 0) {
        unsigned int flags = 0;
        for (unsigned int i = 0; i < 5; ++i) {
            if (explicit_button_counts[i] > 0) { flags |= BIT(i); }
        }
        assert(explicit_buttons == flags);
        assert(mouse_report.body.buttons == flags);
    }
    if (pending_interrupt) {
        /* Attempt preemption at every stock mutation/log boundary. */
        assert(irq_depth > 0);
        ++blocked_preemptions;
    }
}

struct unaffected_state {
    struct zmk_hid_keyboard_report keyboard;
    struct zmk_hid_consumer_report consumer;
    int modifier_counts[8];
    zmk_mod_flags_t explicit_mods, implicit_mods, masked_mods;
    int16_t x, y, wheel, hwheel;
    uint8_t mouse_report_id;
};

static struct unaffected_state unaffected(void) {
    struct unaffected_state result;
    memset(&result, 0, sizeof(result));
    result.keyboard = keyboard_report;
    result.consumer = consumer_report;
    memcpy(result.modifier_counts, explicit_modifier_counts, sizeof(result.modifier_counts));
    result.explicit_mods = explicit_modifiers;
    result.implicit_mods = implicit_modifiers;
    result.masked_mods = masked_modifiers;
    result.x = mouse_report.body.d_x;
    result.y = mouse_report.body.d_y;
    result.wheel = mouse_report.body.d_scroll_y;
    result.hwheel = mouse_report.body.d_scroll_x;
    result.mouse_report_id = mouse_report.report_id;
    return result;
}

static void assert_unaffected(const struct unaffected_state *before) {
    struct unaffected_state after = unaffected();
    assert(memcmp(before, &after, sizeof(after)) == 0);
}

enum operation { SINGLE_PRESS, SINGLE_RELEASE, MULTI_PRESS, MULTI_RELEASE };

static int invoke_public(enum operation op, unsigned int arg) {
    switch (op) {
    case SINGLE_PRESS: return zmk_hid_mouse_button_press((zmk_mouse_button_t)arg);
    case SINGLE_RELEASE: return zmk_hid_mouse_button_release((zmk_mouse_button_t)arg);
    case MULTI_PRESS: return zmk_hid_mouse_buttons_press((zmk_mouse_button_flags_t)arg);
    case MULTI_RELEASE: return zmk_hid_mouse_buttons_release((zmk_mouse_button_flags_t)arg);
    }
    assert(false);
    return -EINVAL;
}

static int invoke_stock(enum operation op, unsigned int arg) {
    switch (op) {
    case SINGLE_PRESS: return bbp9981_stock_zmk_hid_mouse_button_press((zmk_mouse_button_t)arg);
    case SINGLE_RELEASE: return bbp9981_stock_zmk_hid_mouse_button_release((zmk_mouse_button_t)arg);
    case MULTI_PRESS: return bbp9981_stock_zmk_hid_mouse_buttons_press((zmk_mouse_button_flags_t)arg);
    case MULTI_RELEASE: return bbp9981_stock_zmk_hid_mouse_buttons_release((zmk_mouse_button_flags_t)arg);
    }
    assert(false);
    return -EINVAL;
}

static int call_public(enum operation op, unsigned int arg) {
    assert(!pending_interrupt);
    unsigned int depth = irq_depth;
    unsigned int before_locks = locks - probe_locks;
    unsigned int before_unlocks = unlocks - probe_unlocks;
    unsigned int before_clears = clear_calls;
    struct unaffected_state before = unaffected();
    int result = invoke_public(op, arg);
    assert(irq_depth == depth);
    assert(locks - probe_locks == before_locks + 1);
    assert(unlocks - probe_unlocks == before_unlocks + 1);
    assert(clear_calls == before_clears);
    assert_unaffected(&before);
    return result;
}

static void expect_refs(int left, int right, int middle, int fourth, int fifth,
                        zmk_mouse_button_flags_t flags) {
    const int expected[5] = {left, right, middle, fourth, fifth};
    for (unsigned int i = 0; i < 5; ++i) {
        unsigned int before_locks = locks;
        unsigned int before_unlocks = unlocks;
        unsigned int depth = irq_depth;
        assert(bbp9981_hid_mouse_button_references(i) == expected[i]);
        assert(locks == before_locks + 1 && unlocks == before_unlocks + 1);
        assert(irq_depth == depth);
    }
    assert(mouse_report.body.buttons == flags);
}

static void release_all_owned(void) {
    for (unsigned int i = 0; i < 5; ++i) {
        while (bbp9981_hid_mouse_button_references(i) > 0) {
            assert(call_public(SINGLE_RELEASE, i) == 0);
        }
    }
    expect_refs(0, 0, 0, 0, 0, 0);
}

static void test_symbols_and_getter(void) {
    assert(zmk_hid_mouse_button_press != bbp9981_stock_zmk_hid_mouse_button_press);
    assert(zmk_hid_mouse_button_release != bbp9981_stock_zmk_hid_mouse_button_release);
    assert(zmk_hid_mouse_buttons_press != bbp9981_stock_zmk_hid_mouse_buttons_press);
    assert(zmk_hid_mouse_buttons_release != bbp9981_stock_zmk_hid_mouse_buttons_release);
    expect_refs(0, 0, 0, 0, 0, 0);
    unsigned int before = locks;
    assert(bbp9981_hid_mouse_button_references(5) == -EINVAL);
    assert(bbp9981_hid_mouse_button_references(UINT_MAX) == -EINVAL);
    assert(locks == before && irq_depth == 0);
    puts("PASS official ABI, distinct public/stock symbols, short-lock Getter");
}

static void test_owners_and_foreign(void) {
    assert(call_public(MULTI_PRESS, RCLK | MCLK | MB4 | MB5) == 0);
    assert(call_public(SINGLE_PRESS, 0) == 0); /* Physical owner. */
    assert(call_public(SINGLE_PRESS, 0) == 0); /* Synthetic owner. */
    expect_refs(2, 1, 1, 1, 1, LCLK | RCLK | MCLK | MB4 | MB5);
    assert(call_public(SINGLE_RELEASE, 0) == 0); /* Only synthetic's ONE ref. */
    expect_refs(1, 1, 1, 1, 1, LCLK | RCLK | MCLK | MB4 | MB5);
    assert(call_public(SINGLE_RELEASE, 0) == 0); /* Physical release. */
    expect_refs(0, 1, 1, 1, 1, RCLK | MCLK | MB4 | MB5);
    assert(call_public(MULTI_RELEASE, RCLK | MCLK) == 0);
    expect_refs(0, 0, 0, 1, 1, MB4 | MB5);
    release_all_owned();
    puts("PASS physical+synthetic 2->1 remains pressed; foreign R/M and one-ref-only");
}

static void test_error_semantics(void) {
    assert(call_public(SINGLE_RELEASE, 0) == -EINVAL);
    assert(call_public(SINGLE_PRESS, 5) == -EINVAL);
    assert(call_public(SINGLE_RELEASE, 5) == -EINVAL);
    assert(call_public(SINGLE_PRESS, UINT16_MAX) == -EINVAL);
    assert(call_public(SINGLE_RELEASE, UINT16_MAX) == -EINVAL);
    expect_refs(0, 0, 0, 0, 0, 0);
    assert(call_public(MULTI_PRESS, 0) == 0);
    assert(call_public(MULTI_RELEASE, 0) == 0);
    assert(call_public(MULTI_PRESS, 0xE0) == 0); /* Stock ignores unsupported flags. */
    assert(call_public(MULTI_RELEASE, 0xFF) == 0); /* Stock swallows single errors. */
    expect_refs(0, 0, 0, 0, 0, 0);
    assert(call_public(SINGLE_PRESS, 0) == 0);
    assert(call_public(MULTI_RELEASE, LCLK | MB5 | 0xE0) == 0);
    expect_refs(0, 0, 0, 0, 0, 0);
    puts("PASS stock invalid-button/underflow errors, zero/foreign masks, multi error returns");
}

static void test_all_masks_locked(void) {
    for (unsigned int mask = 0; mask <= UINT8_MAX; ++mask) {
        assert(call_public(MULTI_PRESS, mask) == 0);
        for (unsigned int i = 0; i < 5; ++i) {
            assert(bbp9981_hid_mouse_button_references(i) == ((mask & BIT(i)) ? 1 : 0));
        }
        assert(mouse_report.body.buttons == (mask & 0x1F));
        assert(call_public(MULTI_RELEASE, mask) == 0);
        expect_refs(0, 0, 0, 0, 0, 0);
    }
    for (unsigned int i = 0; i < 5; ++i) {
        assert(call_public(SINGLE_PRESS, i) == 0);
        assert(call_public(SINGLE_RELEASE, i) == 0);
    }
    puts("PASS all 256 multi masks and all singles: mutation IRQ held, Getter consistent, no clear");
}

static void interrupt_after_multi_press(void) {
    assert(irq_depth == 0);
    expect_refs(2, 1, 1, 0, 0, LCLK | RCLK | MCLK);
    assert(call_public(SINGLE_RELEASE, 0) == 0); /* Bridge releases only its ref. */
}

static void interrupt_after_multi_release(void) {
    assert(irq_depth == 0);
    expect_refs(1, 0, 0, 0, 0, LCLK);
    assert(call_public(SINGLE_PRESS, 2) == 0); /* New physical middle input. */
}

static void test_multi_preemption_and_nested_irq(void) {
    assert(call_public(SINGLE_PRESS, 0) == 0);
    unsigned int runs = interrupt_runs;
    unsigned int blocks = blocked_preemptions;
    pending_interrupt = interrupt_after_multi_press;
    assert(zmk_hid_mouse_buttons_press(LCLK | RCLK | MCLK) == 0);
    assert(irq_depth == 0 && interrupt_runs == runs + 1);
    assert(blocked_preemptions >= blocks + 6);
    expect_refs(1, 1, 1, 0, 0, LCLK | RCLK | MCLK);
    pending_interrupt = interrupt_after_multi_release;
    assert(zmk_hid_mouse_buttons_release(RCLK | MCLK) == 0);
    assert(irq_depth == 0 && interrupt_runs == runs + 2);
    expect_refs(1, 0, 1, 0, 0, LCLK | MCLK);
    release_all_owned();

    unsigned int key = irq_lock();
    expected_stock_depth = 2;
    assert(call_public(SINGLE_PRESS, 0) == 0);
    expect_refs(1, 0, 0, 0, 0, LCLK);
    pending_interrupt = interrupt_after_multi_press;
    assert(zmk_hid_mouse_buttons_press(LCLK | RCLK | MCLK) == 0);
    assert(irq_depth == 1 && pending_interrupt && interrupt_runs == runs + 2);
    expected_stock_depth = 1;
    irq_unlock(key);
    assert(irq_depth == 0 && interrupt_runs == runs + 3);
    expect_refs(1, 1, 1, 0, 0, LCLK | RCLK | MCLK);
    release_all_owned();
    puts("PASS deferred input/bridge preemption, shared multi lock, nested IRQ key restored");
}

static void test_keyboard_pointer_preserved(void) {
    unsigned int before_locks = locks;
    assert(zmk_hid_press(A) == 0);
    assert(zmk_hid_press(C_VOL_UP) == 0);
    assert(zmk_hid_register_mod(0) == 1);
    assert(zmk_hid_register_mod(0) == 0);
    assert(zmk_hid_implicit_modifiers_press(MOD_LSFT) == 1);
    assert(zmk_hid_masked_modifiers_set(MOD_LCTL) == 1);
    zmk_hid_mouse_movement_set(1234, -2345);
    zmk_hid_mouse_movement_update(11, -22);
    zmk_hid_mouse_scroll_set(-123, 234);
    zmk_hid_mouse_scroll_update(-3, 4);
    assert(locks == before_locks); /* None of these APIs acquired a NEW lock. */
    assert(zmk_hid_is_pressed(A) && zmk_hid_is_pressed(C_VOL_UP));
    assert(zmk_hid_get_explicit_mods() == MOD_LCTL);
    assert(keyboard_report.body.modifiers == MOD_LSFT);
    assert(zmk_hid_get_boot_report()->keys[0] == HID_USAGE_KEY_KEYBOARD_A);
    assert(mouse_report.body.d_x == 1245 && mouse_report.body.d_y == -2367);
    assert(mouse_report.body.d_scroll_x == -126 && mouse_report.body.d_scroll_y == 238);

    assert(call_public(SINGLE_PRESS, 0) == 0);
    assert(call_public(MULTI_PRESS, RCLK | MCLK) == 0);
    assert(call_public(SINGLE_RELEASE, 0) == 0);
    assert(call_public(MULTI_RELEASE, RCLK | MCLK) == 0);
    assert(call_public(SINGLE_RELEASE, 0) == -EINVAL);
    assert(zmk_hid_unregister_mod(0) == 0); /* Original modifier ref 2->1. */
    assert(zmk_hid_get_explicit_mods() == MOD_LCTL);
    assert(zmk_hid_masked_modifiers_clear() == 1);
    assert(keyboard_report.body.modifiers == (MOD_LCTL | MOD_LSFT));
    assert(zmk_hid_implicit_modifiers_release() == 1);
    assert(zmk_hid_unregister_mod(0) == 1);
    assert(zmk_hid_release(A) == 0);
    assert(zmk_hid_release(C_VOL_UP) == 0);
    puts("PASS original keyboard/modifier/consumer and pointer XY/wheel behavior unchanged");
}

struct button_state {
    int refs[5];
    zmk_mod_flags_t flags;
    struct zmk_hid_mouse_report report;
};

static struct button_state button_snapshot(void) {
    struct button_state state;
    memcpy(state.refs, explicit_button_counts, sizeof(state.refs));
    state.flags = explicit_buttons;
    state.report = mouse_report;
    return state;
}

static void restore_button_snapshot(const struct button_state *state) {
    /* Test-only branch replay; production state is never directly assigned. */
    unsigned int key = irq_lock();
    memcpy(explicit_button_counts, state->refs, sizeof(state->refs));
    explicit_buttons = state->flags;
    mouse_report = state->report;
    irq_unlock(key);
}

static void compare_stock_step(enum operation op, unsigned int arg) {
    struct button_state before = button_snapshot();
    int public_ret = call_public(op, arg);
    struct button_state public_after = button_snapshot();
    restore_button_snapshot(&before);
    unsigned int key = irq_lock(); /* Testing original stock with the required lock. */
    int stock_ret = invoke_stock(op, arg);
    irq_unlock(key);
    struct button_state stock_after = button_snapshot();
    assert(public_ret == stock_ret);
    assert(memcmp(public_after.refs, stock_after.refs, sizeof(stock_after.refs)) == 0);
    assert(public_after.flags == stock_after.flags);
    assert(memcmp(&public_after.report, &stock_after.report, sizeof(stock_after.report)) == 0);
}

static void test_golden_stock_equivalence(void) {
    /* Explicit shared-ownership/error cases, followed by a deterministic trace. */
    compare_stock_step(SINGLE_PRESS, 0);
    compare_stock_step(SINGLE_PRESS, 0);
    compare_stock_step(SINGLE_RELEASE, 0);
    compare_stock_step(MULTI_PRESS, RCLK | MCLK);
    compare_stock_step(MULTI_RELEASE, LCLK | RCLK | MCLK | MB5);
    compare_stock_step(SINGLE_RELEASE, 0);
    compare_stock_step(SINGLE_PRESS, UINT16_MAX);
    compare_stock_step(SINGLE_RELEASE, 5);
    uint32_t random = 0x38195208U;
    for (unsigned int i = 0; i < 4096; ++i) {
        random = random * 1664525U + 1013904223U;
        enum operation op = (enum operation)((random >> 24) & 3U);
        unsigned int arg = op <= SINGLE_RELEASE ? ((random >> 8) % 8U) : (random >> 8) & 0xFFU;
        compare_stock_step(op, arg);
    }
    release_all_owned();
    puts("PASS 4104-step public-vs-stock replay: returns, original refs and report bytes identical");
}

static void test_stock_clear_not_redefined(void) {
    assert(call_public(MULTI_PRESS, LCLK | RCLK | MCLK) == 0);
    assert(call_public(SINGLE_PRESS, 0) == 0);
    unsigned int before = locks;
    zmk_hid_mouse_clear(); /* SDK clear changes ONLY report body, not refs/explicit flags. */
    assert(locks == before);
    expect_refs(2, 1, 1, 0, 0, 0);
    assert(explicit_buttons == (LCLK | RCLK | MCLK));
    assert(mouse_report.body.d_x == 0 && mouse_report.body.d_y == 0);
    assert(mouse_report.body.d_scroll_x == 0 && mouse_report.body.d_scroll_y == 0);
    assert(call_public(SINGLE_RELEASE, 0) == 0);
    expect_refs(1, 1, 1, 0, 0, LCLK | RCLK | MCLK);
    release_all_owned();
    assert(clear_calls == 1); /* Only the explicit test call, never a new API. */
    puts("PASS stock mouse_clear report-only semantics retained; no ref reset or new global clear");
}

int main(void) {
    assert(sizeof(zmk_hid_report_desc) > 0 && zmk_hid_report_desc[0] == 0x05);
    test_symbols_and_getter();
    test_owners_and_foreign();
    test_error_semantics();
    test_all_masks_locked();
    test_multi_preemption_and_nested_irq();
    test_keyboard_pointer_preserved();
    test_golden_stock_equivalence();
    test_stock_clear_not_redefined();
    assert(irq_depth == 0 && !pending_interrupt);
    assert(locks == unlocks && probe_locks == probe_unlocks);
    assert(ref_mutations == probe_locks && ref_mutations > 0 && locked_stock_logs > 0);
    assert(max_irq_depth == 3);
    printf("PASS SDK HID entire TU: 8 groups, %u mutation Getter checks, IRQ balanced, UBSan clean\n",
           ref_mutations);
    return 0;
}
#else
void hid_mock_log(const char *function, int error, const char *format, ...) {
    (void)function;
    (void)error;
    (void)format;
}

int main(void) {
    assert(sizeof(zmk_hid_report_desc) > 0 && zmk_hid_report_desc[0] == 0x05);
    assert(zmk_hid_press(A) == 0 && zmk_hid_is_pressed(A));
    assert(zmk_hid_release(A) == 0 && !zmk_hid_is_pressed(A));
    assert(zmk_hid_register_mod(0) == 1);
    assert(zmk_hid_register_mod(0) == 0);
    assert(zmk_hid_unregister_mod(0) == 0);
    assert(zmk_hid_unregister_mod(0) == 1);
    assert(zmk_hid_unregister_mod(0) == -EINVAL);
    assert(zmk_hid_press(C_VOL_UP) == 0 && zmk_hid_is_pressed(C_VOL_UP));
    assert(zmk_hid_release(C_VOL_UP) == 0 && !zmk_hid_is_pressed(C_VOL_UP));
    puts("PASS pointing disabled: original keyboard/consumer/modifier TU; no button symbols");
    return 0;
}
#endif
