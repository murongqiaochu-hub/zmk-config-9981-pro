#include "test_env.h"

/* Instrument the real leaf without changing the shared host mocks. */
static bool backend_record;
static int backend_unready = -1;
static unsigned backend_order[4], backend_calls;
static void backend_record_call(unsigned call) {
    if (!backend_record) { return; }
    assert(backend_calls < sizeof(backend_order) / sizeof(backend_order[0]));
    backend_order[backend_calls++] = call;
}
static bool backend_ready(const struct pwm_dt_spec *pwm) {
    backend_record_call(2U * pwm->channel);
    return (int)pwm->channel != backend_unready && pwm_is_ready_dt(pwm);
}
static int backend_write(const struct pwm_dt_spec *pwm, uint32_t period, uint32_t pulse) {
    backend_record_call(2U * pwm->channel + 1U);
    return pwm_set_dt(pwm, period, pulse);
}
#define pwm_is_ready_dt backend_ready
#define pwm_set_dt backend_write
#include "../config/boards/arm/bbp9981/custom_driver/keyboard_backlight.c"
#undef pwm_set_dt
#undef pwm_is_ready_dt

static void begin_backend_trace(int unready) {
    backend_calls = 0; backend_unready = unready; backend_record = true;
}
static void assert_backend_order(int unready) {
    unsigned call = 0;
    for (unsigned i = 0; i < (unsigned)INDICATOR_LED_NUM_LEDS; i++) {
        assert(call < backend_calls && backend_order[call++] == 2U * i);
        if ((int)i != unready) {
            assert(call < backend_calls && backend_order[call++] == 2U * i + 1U);
        }
    }
    assert(call == backend_calls);
    backend_record = false; backend_unready = -1;
}
static void test_backend_contract(void) {
    fake_brightness = 100; fake_rgb_on = true; fake_pwm_ready = true;
    fake_led_error = 0; fake_led_error_index = -1;
    kb_pwm_valid = false; set_mode_brightness(25);
    assert(kb_pwm_valid && last_led_level == 25);
    /* The old cache-hit return must not recompute even diagnostic state. */
    last_led_level = 123;
    begin_backend_trace(-1); set_mode_brightness(25);
    assert(backend_calls == 0U && last_led_level == 123 && kb_pwm_valid);
    backend_record = false;
    for (int ready_failure = 0; ready_failure <= 1; ready_failure++) {
        for (int bad = 0; bad < INDICATOR_LED_NUM_LEDS; bad++) {
            uint32_t cached[INDICATOR_LED_NUM_LEDS];
            memcpy(cached, kb_last_pulses, sizeof(cached));
            unsigned writes = fake_pwm_writes;
            fake_led_error = ready_failure ? 0 : -EIO; fake_led_error_index = bad;
            begin_backend_trace(ready_failure ? bad : -1); set_mode_brightness(50);
            assert_backend_order(ready_failure ? bad : -1);
            assert(!kb_pwm_valid && last_led_level == -1 && requested_pattern == 50U);
            assert(fake_pwm_writes == writes + INDICATOR_LED_NUM_LEDS - (unsigned)ready_failure);
            for (int i = 0; i < INDICATOR_LED_NUM_LEDS; i++) {
                assert(kb_last_pulses[i] == cached[i]);
                assert(fake_pwm_pulses[i] == (i == bad ? 125000U : 250000U));
            }
            /* Retry the old cached value: invalidation still rewrites BOTH. */
            writes = fake_pwm_writes; fake_led_error = 0;
            begin_backend_trace(-1); set_mode_brightness(25); assert_backend_order(-1);
            assert(kb_pwm_valid && last_led_level == 25);
            assert(fake_pwm_writes == writes + INDICATOR_LED_NUM_LEDS);
            for (int i = 0; i < INDICATOR_LED_NUM_LEDS; i++) {
                assert(kb_last_pulses[i] == 125000U && fake_pwm_pulses[i] == 125000U);
            }
        }
    }
    begin_backend_trace(-1); kb_set_fraction(UINT32_MAX); assert_backend_order(-1);
    assert(kb_pwm_valid && last_led_level == 100);
    for (int i = 0; i < INDICATOR_LED_NUM_LEDS; i++) { assert(kb_last_pulses[i] == 500000U); }
    set_led_brightness(UINT8_MAX); assert(last_led_level == 100);
    puts("PASS keyboard backend contract: per-channel ready/write order, unchanged diagnostic cache hit, no partial cache commit, old-value full retry and fraction clamp");
}
int main(void) {
    assert(INDICATOR_LED_NUM_LEDS==2&&keyboardbacklight_init()==0);
    fake_brightness=1;fake_rgb_on=true;fake_activity=ZMK_ACTIVITY_ACTIVE;fake_layer=2;
    zmk_event_t key={.valid=true,.data={.position=41,.state=true}};key_listener_cb(&key);
    polling_work_handler(&polling_work.work);
    bool seen[81]={0};unsigned count=0;
    for(unsigned i=0;i<200U;i++) {
        cycle_work_handler(&cycle_work.work);
        uint32_t ns=(uint32_t)((uint64_t)500000U*requested_pattern/10000U);
        assert(fake_pwm_pulse_ns==ns&&fake_pwm_ticks>=8U&&fake_pwm_ticks<=80U);
        assert(fake_pwm_period_ns==500000U);
        if(!seen[fake_pwm_ticks]){seen[fake_pwm_ticks]=true;count++;}
        unsigned writes=fake_pwm_writes;polling_work_handler(&polling_work.work);
        assert(fake_pwm_writes==writes);
        assert(fake_pwm_pulses[0]==ns&&fake_pwm_pulses[1]==ns);
    }
    assert(count>=18U&&seen[8]&&seen[80]);
    for(unsigned setting=0;setting<=255U;setting++)for(unsigned p=0;p<=100U;p++) {
        fake_brightness=(uint8_t)setting;fake_rgb_on=true;set_mode_brightness((uint8_t)p);
        uint32_t expected=(uint32_t)((uint64_t)500000U*p*indicator_user_limit((uint8_t)setting)/10000U);
        assert(fake_pwm_pulse_ns==expected&&fake_pwm_ticks<=80U*indicator_user_limit((uint8_t)setting));
        if(setting==0U||p==0U)assert(fake_pwm_pulse_ns==0U);
    }
    fake_brightness=1;fake_rgb_on=false;set_mode_brightness(100);assert(fake_pwm_pulse_ns==0U);
    fake_rgb_on=true;fake_led_error=-EIO;fake_led_error_index=1;set_mode_brightness(25);
    assert(!kb_pwm_valid&&last_led_level==-1);fake_led_error=0;set_mode_brightness(25);
    assert(kb_pwm_valid&&fake_pwm_pulses[0]==1250U&&fake_pwm_pulses[1]==1250U);
    printf("PASS keyboard all-path fractional PWM: %u low-cycle levels on BOTH channels, 2kHz, all256 caps, zero/B-off, poll cache and partial retry; no hardware visual claim\n",count);
    test_backend_contract();
    return 0;
}
