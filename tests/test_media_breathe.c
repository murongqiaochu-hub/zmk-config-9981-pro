/* Real production driver + frozen host fixture. This is a serialized callback
 * model, not Zephyr scheduling, nRF PWM/visual, flash-wear or device acceptance.
 * Build with TEST_TWO_LEDS=1; the feature is selected only by the compiler.
 * OFF entries include the old oracles verbatim, one independent TU per suite.
 */
#ifndef CONFIG_BBP9981_MEDIA_BREATHE
#if defined(TEST_MEDIA_OFF_LOW_PWM)
#include "test_keyboard_low_pwm.c"
#elif defined(TEST_MEDIA_OFF_LIGHT_MEMORY)
#include "test_keyboard_light_memory.c"
#else
#include "test_keyboard_led.c"
#endif
#else
#ifndef TEST_NATIVE_BACKLIGHT
#define TEST_NATIVE_BACKLIGHT 1
#endif
#include "test_env.h"

/* Observe both accepted channels without changing the shared fixture. */
static uint32_t observed_periods[2], observed_pulses[2], observed_ticks[2];
static int unready_channel = -1;
static bool media_pwm_ready(const struct pwm_dt_spec *pwm) {
    return (int)pwm->channel != unready_channel && pwm_is_ready_dt(pwm);
}
static int media_pwm_write(const struct pwm_dt_spec *pwm, uint32_t period, uint32_t pulse) {
    int ret = pwm_set_dt(pwm, period, pulse);
    if (ret >= 0) {
        observed_periods[pwm->channel] = fake_pwm_period_ns;
        observed_pulses[pwm->channel] = fake_pwm_pulses[pwm->channel];
        observed_ticks[pwm->channel] = fake_pwm_ticks;
    }
    return ret;
}
#define pwm_is_ready_dt media_pwm_ready
#define pwm_set_dt media_pwm_write
#include "../config/boards/arm/bbp9981/custom_driver/keyboard_backlight.c"
#undef pwm_set_dt
#undef pwm_is_ready_dt

static ssize_t read_saved(void *arg, void *data, size_t len) {
    assert(len == 1U);
    *(uint8_t *)data = *(uint8_t *)arg;
    return 1;
}
static uint8_t saved_limit(uint8_t raw) { return raw > 100U ? 100U : raw; }
/* Literal acceptance oracle, independent of the production animation macros. */
static uint8_t pattern_at(unsigned step) {
    unsigned phase = step % 180U;
    return (uint8_t)(10U + (phase <= 90U ? phase : 180U - phase));
}
static void reset_saved(uint8_t raw) {
    uint8_t saved = saved_limit(raw);
    fake_now = 0;
    fake_activity = ZMK_ACTIVITY_ACTIVE; fake_layer = 0;
    fake_rgb_on = true; fake_brightness = saved; fake_rgb_set_error = 0;
    fake_pwm_ready = true; unready_channel = -1;
    fake_led_error = 0; fake_led_error_index = -1;
    fake_pwm_writes = fake_led_writes = 0;
    fake_pwm_period_ns = fake_pwm_pulse_ns = fake_pwm_ticks = 0;
    fake_led_level = 0;
    memset(fake_led_levels, 0, sizeof(fake_led_levels));
    memset(fake_pwm_pulses, 0, sizeof(fake_pwm_pulses));
    memset(observed_periods, 0, sizeof(observed_periods));
    memset(observed_pulses, 0, sizeof(observed_pulses));
    memset(observed_ticks, 0, sizeof(observed_ticks));
    memset(kb_last_pulses, 0, sizeof(kb_last_pulses));
    last_led_level = -1; kb_pwm_valid = false;
    prev_active = false; prev_layer = -1; backlight_allowed = false;
    blink_on = false; blink_start_high = true;
    cycle_brightness = 10; cycle_direction_up = true;
    media_phase = 0; requested_pattern = 0;
    kb_mem_loaded = kb_mem_ready = kb_mem_dirty = false;
    kb_mem_value = kb_mem_observed = kb_mem_failures = 0;
    fake_flash_error = 0; fake_flash_writes = 0;
    fake_flash_has_value = true; fake_flash_value = saved;
    /* The other native setting and durable key are independent sentinels. */
    fake_tp_brightness = 73; fake_tp_on = false; fake_tp_set_calls = 0;
    fake_tp_set_error = fake_tp_flash_error = 0;
    fake_tp_flash_has_value = true; fake_tp_flash_value = 73; fake_tp_flash_writes = 0;
    assert(INDICATOR_LED_NUM_LEDS == 2 && keyboardbacklight_init() == 0);
    assert(kb_mem_set("brightness", 1, read_saved, &saved) == 0);
    assert(kb_mem_commit() == 0 && !kb_mem_work.pending);
    /* Also exercise corrupted native values 101..255 and the existing clamp. */
    fake_brightness = raw;
}
static void poll(void) {
    polling_work.pending = false;
    polling_work_handler(&polling_work.work);
    assert(polling_work.pending && polling_work.delay == 100);
}
static void assert_memory(uint8_t raw, unsigned writes) {
    assert(fake_brightness == raw && kb_mem_loaded && kb_mem_ready);
    assert(!kb_mem_dirty && !kb_mem_work.pending && kb_mem_failures == 0U);
    assert(kb_mem_value == saved_limit(raw) && kb_mem_observed == saved_limit(raw));
    assert(fake_flash_has_value && fake_flash_value == saved_limit(raw));
    assert(fake_flash_writes == writes);
    assert(fake_tp_brightness == 73U && !fake_tp_on && fake_tp_set_calls == 0U);
    assert(fake_tp_flash_has_value && fake_tp_flash_value == 73U && fake_tp_flash_writes == 0U);
}
static void assert_output(uint8_t pattern, uint8_t raw, bool on) {
    unsigned value = raw > 100U ? 100U : raw;
    unsigned limit = value == 0U ? 0U : (value * value + 50U) / 100U;
    if (value != 0U && limit == 0U) { limit = 1U; }
    uint32_t ns = on ? (uint32_t)((uint64_t)500000U * pattern * limit / 10000U) : 0U;
    uint32_t ticks = (uint32_t)((uint64_t)ns * 16U / 1000U);
    assert(requested_pattern == pattern && kb_pwm_valid);
    for (unsigned channel = 0; channel < 2U; channel++) {
        assert(observed_periods[channel] == 500000U);
        assert(observed_pulses[channel] == ns && observed_ticks[channel] == ticks);
        assert(fake_pwm_pulses[channel] == ns && kb_last_pulses[channel] == ns);
        assert(observed_ticks[channel] <= 80U * limit);
    }
    assert(fake_pwm_pulse_ns == ns && fake_pwm_ticks == ticks);
}
static void enter_media(void) {
    if (prev_active && prev_layer == 3) { fake_layer = 0; poll(); }
    fake_activity = ZMK_ACTIVITY_ACTIVE; fake_layer = 3; poll();
    assert(prev_active && prev_layer == 3 && media_phase == 0U && requested_pattern == 10U);
    assert(cycle_work.pending && cycle_work.delay == 20 && !blink_work.pending);
    assert(cycle_brightness == 10U && cycle_direction_up);
}
static void dispatch_media(void) {
    assert(prev_active && prev_layer == 3 && cycle_work.pending && cycle_work.delay == 20);
    unsigned phase = ((unsigned)media_phase + 1U) % 180U;
    unsigned schedules = cycle_work.schedules;
    cycle_work.pending = false;
    cycle_work_handler(&cycle_work.work);
    assert(media_phase == phase && requested_pattern == pattern_at(phase));
    assert(cycle_work.pending && cycle_work.delay == 20 && cycle_work.schedules == schedules + 1U);
    assert(cycle_brightness == 10U && cycle_direction_up && !blink_work.pending);
}
static void advance_media(unsigned steps) {
    for (unsigned i = 0; i < steps; i++) { fake_now += 20U; dispatch_media(); }
}
static void stale_blink(void) {
    unsigned writes = fake_pwm_writes, schedules = blink_work.schedules;
    unsigned cycle_schedules = cycle_work.schedules;
    bool pending = blink_work.pending, old_on = blink_on;
    uint8_t pattern = requested_pattern, phase = media_phase;
    blink_work_handler(&blink_work.work);
    assert(fake_pwm_writes == writes && blink_work.schedules == schedules);
    assert(blink_work.pending == pending && blink_on == old_on);
    assert(cycle_work.schedules == cycle_schedules && requested_pattern == pattern && media_phase == phase);
}
static void inactive_callbacks(void) {
    unsigned writes = fake_pwm_writes, blinks = blink_work.schedules, cycles = cycle_work.schedules;
    uint8_t pattern = requested_pattern, phase = media_phase;
    blink_work_handler(&blink_work.work); cycle_work_handler(&cycle_work.work);
    assert(fake_pwm_writes == writes && blink_work.schedules == blinks && cycle_work.schedules == cycles);
    assert(requested_pattern == pattern && media_phase == phase);
    assert(!blink_work.pending && !cycle_work.pending);
}
static void test_full_period(void) {
    reset_saved(100); enter_media(); assert_output(10, 100, true);
    unsigned initial_schedules = cycle_work.schedules, peaks = 0, troughs = 0;
    /* 10ms observation resolves both the 20ms frames and old 250ms blink edges.
     * Entry t=0 is 10%; t=1800 is 100%; t=3600 is 10%, without endpoint holds. */
    for (unsigned ms = 10; ms <= 7200U; ms += 10U) {
        fake_now = ms;
        unsigned writes = fake_pwm_writes;
        uint8_t before = requested_pattern;
        if (ms % 20U == 0U) {
            dispatch_media();
            unsigned phase = (ms / 20U) % 180U;
            int delta = (int)requested_pattern - (int)before;
            assert(delta == (phase > 0U && phase <= 90U ? 1 : -1));
            assert(fake_pwm_writes == writes + 2U);
            if (phase == 90U) { assert(requested_pattern == 100U); peaks++; }
            if (phase == 0U) { assert(requested_pattern == 10U); troughs++; }
        } else {
            assert(fake_pwm_writes == writes && requested_pattern == before);
        }
        assert_output(pattern_at(ms / 20U), 100, true);
        if (ms % 100U == 0U) {
            writes = fake_pwm_writes; poll(); assert(fake_pwm_writes == writes);
        }
        if (ms % 250U == 0U) {
            stale_blink();
            assert_output(pattern_at(ms / 20U), 100, true);
        }
        assert_memory(100, 0);
    }
    assert(peaks == 2U && troughs == 2U && media_phase == 0U);
    assert(cycle_work.schedules == initial_schedules + 360U && blink_work.schedules == 0U);
}
static void test_caps_and_low_pwm(void) {
    bool seen[81] = {false}; unsigned levels = 0;
    for (unsigned setting = 0; setting <= 255U; setting++) {
        reset_saved((uint8_t)setting); enter_media();
        for (unsigned step = 0; step <= 180U; step++) {
            assert_output(pattern_at(step), (uint8_t)setting, true);
            if (setting == 1U) {
                for (unsigned channel = 0; channel < 2U; channel++) {
                    assert(observed_pulses[channel] >= 500U && observed_pulses[channel] <= 5000U);
                    assert(observed_ticks[channel] >= 8U && observed_ticks[channel] <= 80U);
                    if (!seen[observed_ticks[channel]]) { seen[observed_ticks[channel]] = true; levels++; }
                }
            }
            unsigned writes = fake_pwm_writes; poll(); assert(fake_pwm_writes == writes);
            assert_memory((uint8_t)setting, 0);
            if (step < 180U) { advance_media(1); }
        }
    }
    assert(levels == 73U && seen[8] && seen[80]);
}
static void test_controls_and_memory(void) {
    reset_saved(40); fake_rgb_on = false; enter_media(); assert_output(10, 40, false);
    stale_blink(); fake_rgb_on = true; poll(); assert_output(10, 40, true);
    advance_media(37);
    uint8_t phase = media_phase, pattern = requested_pattern;
    fake_rgb_on = false; poll(); assert_output(pattern, 40, false);
    assert(media_phase == phase && cycle_work.pending && !blink_work.pending);
    advance_media(10); stale_blink(); assert_output(pattern_at(47), 40, false);
    fake_rgb_on = true; poll(); assert_output(pattern_at(47), 40, true);
    assert(media_phase == 47U); assert_memory(40, 0);

    /* Only an actual user preference edit schedules the existing raw-value save. */
    phase = media_phase; pattern = requested_pattern;
    assert(zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.b = 1}) == 0);
    poll(); assert_output(pattern, 1, true);
    assert(media_phase == phase && kb_mem_dirty && kb_mem_value == 1U);
    assert(kb_mem_work.pending && kb_mem_work.delay == 250 && fake_flash_writes == 0U);
    unsigned save_schedules = kb_mem_work.schedules;
    for (unsigned step = 0; step < 12U; step++) {
        advance_media(1); poll(); stale_blink();
        assert(kb_mem_dirty && kb_mem_value == 1U && kb_mem_work.schedules == save_schedules);
        assert(fake_flash_writes == 0U && fake_flash_value == 40U);
    }
    fake_now += 10U; /* Existing 250ms debounce, not a new animation timer. */
    kb_mem_work.pending = false; kb_mem_save(&kb_mem_work.work);
    assert_memory(1, 1);
    for (unsigned step = 0; step < 720U; step++) {
        fake_rgb_on = (step % 37U) != 0U;
        advance_media(1); poll(); stale_blink();
        assert_output(pattern_at(media_phase), 1, fake_rgb_on);
        kb_mem_save(&kb_mem_work.work); /* A stale clean save callback cannot write. */
        assert_memory(1, 1);
        assert(kb_mem_work.schedules == save_schedules);
    }
    fake_rgb_on = true; poll();
    /* Restore the same durable raw value through the real settings API. */
    uint8_t saved = fake_flash_value;
    fake_brightness = 90; kb_mem_loaded = kb_mem_ready = kb_mem_dirty = false;
    assert(kb_mem_set("brightness", 1, read_saved, &saved) == 0 && kb_mem_commit() == 0);
    assert_memory(1, 1);
}
static void test_other_layers(void) {
    zmk_event_t key = {.valid = true, .data = {.position = 41, .state = true}};
    reset_saved(100); enter_media(); advance_media(13);
    fake_layer = 0; poll(); assert_output(0, 100, true); inactive_callbacks();
    assert(!backlight_allowed);
    key.data.state = false; key_listener_cb(&key); poll(); assert_output(0, 100, true);
    key.data.state = true; assert(key_listener_cb(&key) == ZMK_EV_EVENT_BUBBLE);
    poll(); assert_output(100, 100, true);
    fake_rgb_on = false; poll(); assert_output(100, 100, false);
    fake_rgb_on = true; poll(); assert_output(100, 100, true); assert_memory(100, 0);

    reset_saved(100); enter_media(); advance_media(13);
    fake_layer = 1; poll(); assert_output(10, 100, true);
    assert(blink_work.pending && blink_work.delay == 250 && !cycle_work.pending);
    unsigned writes = fake_pwm_writes, cycles = cycle_work.schedules;
    cycle_work_handler(&cycle_work.work);
    assert(fake_pwm_writes == writes && cycle_work.schedules == cycles);
    for (unsigned i = 0; i < 8U; i++) {
        blink_work.pending = false; blink_work_handler(&blink_work.work);
        assert_output((i & 1U) ? 10 : 100, 100, true);
        assert(blink_work.pending && blink_work.delay == 500 && !cycle_work.pending);
    }
    fake_layer = 0; poll(); fake_rgb_on = false; fake_layer = 1; poll();
    assert(blink_start_high && blink_on && blink_work.delay == 250);
    assert_output(100, 100, false);
    blink_work.pending = false; blink_work_handler(&blink_work.work);
    assert_output(10, 100, false); assert(blink_work.delay == 500);
    fake_rgb_on = true; poll(); assert_output(10, 100, true); assert_memory(100, 0);
    enter_media(); assert_output(10, 100, true); stale_blink();
    advance_media(1); assert_output(11, 100, true); assert_memory(100, 0);

    reset_saved(100); enter_media(); advance_media(37);
    uint8_t media_before = media_phase, inherited = requested_pattern;
    fake_layer = 2; poll(); assert_output(inherited, 100, true);
    assert(cycle_work.pending && cycle_work.delay == 100 && !blink_work.pending);
    stale_blink();
    for (unsigned i = 0; i < 200U; i++) {
        unsigned phase = i % 36U;
        uint8_t expected = (uint8_t)(10U + 5U * (phase <= 18U ? phase : 36U - phase));
        cycle_work.pending = false; cycle_work_handler(&cycle_work.work);
        assert_output(expected, 100, true);
        unsigned next = (i + 1U) % 36U;
        assert(cycle_brightness == 10U + 5U * (next <= 18U ? next : 36U - next));
        assert(cycle_direction_up == (next < 18U));
        assert(cycle_work.pending && cycle_work.delay == 20 && media_phase == media_before);
    }
    enter_media(); assert_output(10, 100, true); assert(media_phase == 0U);
    fake_layer = 4; poll(); assert_output(0, 100, true); inactive_callbacks(); assert_memory(100, 0);
}
static void test_non_media_caps(void) {
    for (unsigned setting = 0; setting <= 255U; setting++) {
        uint8_t raw = (uint8_t)setting;
        reset_saved(raw);
        zmk_event_t key = {.valid = true, .data = {.position = 41, .state = true}};
        key_listener_cb(&key); poll(); assert_output(100, raw, true);
        fake_rgb_on = false; poll(); assert_output(100, raw, false);
        fake_rgb_on = true; poll(); assert_output(100, raw, true);
        inactive_callbacks();

        fake_layer = 1; poll(); assert_output(10, raw, true);
        assert(blink_work.pending && blink_work.delay == 250 && !cycle_work.pending);
        for (unsigned i = 0; i < 3U; i++) {
            blink_work.pending = false; blink_work_handler(&blink_work.work);
            assert_output((i & 1U) ? 10 : 100, raw, true);
            assert(blink_work.pending && blink_work.delay == 500);
        }
        fake_rgb_on = false; poll(); assert_output(100, raw, false);
        blink_work.pending = false; blink_work_handler(&blink_work.work);
        assert_output(10, raw, false);
        fake_rgb_on = true; poll(); assert_output(10, raw, true);

        fake_layer = 2; poll(); assert_output(10, raw, true);
        assert(cycle_work.pending && cycle_work.delay == 100 && !blink_work.pending);
        for (unsigned i = 0; i <= 36U; i++) {
            unsigned phase = i % 36U;
            uint8_t expected = (uint8_t)(10U + 5U * (phase <= 18U ? phase : 36U - phase));
            cycle_work.pending = false; cycle_work_handler(&cycle_work.work);
            assert_output(expected, raw, true);
            assert(cycle_work.pending && cycle_work.delay == 20 && !blink_work.pending);
        }
        fake_rgb_on = false; poll(); assert_output(10, raw, false);
        cycle_work.pending = false; cycle_work_handler(&cycle_work.work);
        assert_output(15, raw, false);
        fake_rgb_on = true; poll(); assert_output(15, raw, true);
        fake_layer = 0; poll(); assert_output(100, raw, true);
        inactive_callbacks(); assert_memory(raw, 0);
    }
}
static void test_idle_and_wake(void) {
    for (int layer = 0; layer <= 3; layer++) {
        reset_saved(67); enter_media(); advance_media(29);
        fake_layer = layer; poll();
        fake_activity = ZMK_ACTIVITY_IDLE; poll();
        assert_output(0, 67, true);
        assert(!prev_active && !backlight_allowed && !blink_work.pending && !cycle_work.pending);
        inactive_callbacks(); assert_memory(67, 0);
        fake_activity = ZMK_ACTIVITY_ACTIVE; poll();
        if (layer == 0) {
            assert_output(0, 67, true); inactive_callbacks();
            zmk_event_t key = {.valid = true, .data = {.position = 41, .state = true}};
            key_listener_cb(&key); poll(); assert_output(100, 67, true);
        } else if (layer == 1) {
            assert_output(10, 67, true);
            assert(blink_work.pending && blink_work.delay == 250 && !cycle_work.pending);
        } else if (layer == 2) {
            assert_output(0, 67, true);
            assert(cycle_work.pending && cycle_work.delay == 100 && !blink_work.pending);
        } else {
            assert_output(10, 67, true); assert(media_phase == 0U);
            stale_blink(); advance_media(1); assert_output(11, 67, true);
        }
        assert_memory(67, 0);
    }
}
static void test_pwm_error_recovery(void) {
    for (unsigned readiness_failure = 0; readiness_failure < 2U; readiness_failure++) {
        for (int bad = -1; bad < 2; bad++) {
            reset_saved(1); enter_media(); assert_output(10, 1, true);
            if (readiness_failure) {
                if (bad < 0) { fake_pwm_ready = false; } else { unready_channel = bad; }
            } else {
                fake_led_error = -EIO; fake_led_error_index = bad;
            }
            unsigned writes = fake_pwm_writes;
            advance_media(1);
            assert(!kb_pwm_valid && last_led_level == -1 && requested_pattern == 11U);
            unsigned attempts = readiness_failure ? (bad < 0 ? 0U : 1U) : 2U;
            assert(fake_pwm_writes == writes + attempts);
            for (int channel = 0; channel < 2; channel++) {
                bool failed = bad < 0 || bad == channel;
                assert(kb_last_pulses[channel] == 500U); /* No partial cache commit. */
                assert(fake_pwm_pulses[channel] == (failed ? 500U : 550U));
            }
            fake_pwm_ready = true; unready_channel = -1; fake_led_error = 0;
            unsigned schedules = cycle_work.schedules; writes = fake_pwm_writes;
            poll(); assert_output(11, 1, true);
            assert(fake_pwm_writes == writes + 2U && media_phase == 1U && cycle_work.schedules == schedules);
            writes = fake_pwm_writes; poll(); assert(fake_pwm_writes == writes);
            /* Recovery on the next frame works even without a polling retry. */
            fake_led_error = -EIO; fake_led_error_index = -1;
            advance_media(1); assert(!kb_pwm_valid && requested_pattern == 12U);
            fake_led_error = 0; advance_media(1); assert_output(13, 1, true);
            stale_blink(); assert_memory(1, 0);
        }
    }
}
int main(void) {
    test_full_period();
    test_caps_and_low_pwm();
    test_controls_and_memory();
    test_other_layers();
    test_non_media_caps();
    test_idle_and_wake();
    test_pwm_error_recovery();
    puts("PASS MEDIA production ON: two 3.6s periods, 20ms/1% monotone 10..100 triangle, no 250ms blink, all256 caps, both-channel 1% ns/ticks, on/off, unchanged layers0/1/2, idle/wake/cancellation/stale blink, partial PWM failures/retries, raw KB restore/no frame saves/TP key isolation; serialized host model only, not device/visual acceptance");
    return 0;
}
#endif
