/* Fixed-seed old/new driver trace. Host I/O mocks; not hardware acceptance. */
#include "test_env.h"
static uint8_t fake_scroll_rotation;
static unsigned rotation_resets, gesture_cancels, ctrl_updates;
void tp_scroll_rotation_reset(void) { fake_scroll_rotation = 0; rotation_resets++; }
uint8_t tp_scroll_rotation_quadrants(void) { return fake_scroll_rotation; }
void tp_scroll_gesture_cancel(void) { gesture_cancels++; }
int tp_sym_ctrl_update(bool aa, bool sym, int64_t t) {
    (void)aa; (void)sym; (void)t; ctrl_updates++; return 0;
}
int tp_sym_ctrl_release(int64_t t) { (void)t; return 0; }
#if REFACTORED
#include FW_MODE_SOURCE
#endif
#include FW_A320_SOURCE
static uint32_t rng = 0x99812290U;
static uint64_t digest = UINT64_C(1469598103934665603);
static uint32_t random_u32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static void fold(uint32_t v) { digest ^= v; digest *= UINT64_C(1099511628211); }
static void key_event(uint32_t position, bool down, int64_t at) {
    zmk_event_t event = {.valid = true, .data = {.position=position,.state=down,.timestamp=at}};
    fold((uint32_t)ctrl_listener_cb(&event));
}
int main(void) {
    struct a320_dev_config cfg = {.i2c={.bus=&fake_device}};
    struct a320_data data = {0};
    struct device dev = {.name="trace",.config=&cfg,.data=&data};
    fold((uint32_t)a320_init(&dev));
    fake_now = UINT32_MAX - 10000U;
    const unsigned positions[] = {0,1,3,5,34,37,40,41,7,41,41,37};
    for (unsigned i=0; i<240000U; i++) {
        uint32_t value=random_u32();
        fake_now += (value & 31U) + ((i % 991U == 0) ? 61U : 0U);
        if ((value & 7U) == 0U) {
            key_event(positions[(value >> 4) % 12U], (value & 0x100U) != 0, fake_now);
        }
        if ((value & 63U) == 3U) { (void)tp_mouse_orientation_toggle(); }
        if ((value & 63U) == 5U) { fake_scroll_rotation=(uint8_t)((value >> 8) & 3U); }
        if ((value & 63U) == 7U) {
            tp_scroll_shoulder_modifier_update((value & 0x80U) ? 0U : 1U, (value & 0x100U)!=0);
        }
        fake_dx=(int8_t)(value >> 16); fake_dy=(int8_t)(value >> 24);
        fake_gpio_state=(value % 19U == 0U) ? -EIO : ((value % 7U == 0U) ? 1 : 0);
        fake_i2c_error=(value % 37U == 0U) ? -EIO : 0;
        report_count=0;
        for (unsigned n=0;n<4U;n++) { report_values[n]=0; }
        a320_poll_work_handler(&data.poll_work.work);
        fold(report_count);
        for (unsigned n=0;n<4U;n++) { fold((uint32_t)report_values[n]); }
        fold(tp_is_touched()); fold(tp_scroll_mode_active()); fold(tp_aa_is_pressed());
        fold(tp_scroll_lock_is_latched()); fold(tp_scroll_is_locked());
        fold(tp_mouse_orientation_reversed()); fold(tp_scroll_shoulder_slow_active());
        fold((uint32_t)atomic_get(&scroll_mode_snapshot));
        fold((uint32_t)atomic_get(&cursor_speed_snapshot));
        fold(rotation_resets); fold(gesture_cancels); fold(ctrl_updates);
        fold(data.poll_work.schedules); fold((uint32_t)data.poll_work.delay);
        if (i % 1000U == 999U) { printf("%u %016llx\n",i,(unsigned long long)digest); }
    }
    return 0;
}
