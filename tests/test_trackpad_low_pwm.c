/* Real trackpad driver, independent fractional PWM/tick assertions. */
#define main legacy_trackpad_suite
#include "test_trackpad_led.c"
#undef main

static void low_brightness_curve(uint8_t setting) {
    const uint32_t start=40000U;
    bool seen[81]={0};unsigned distinct=0;
    uint32_t previous=0;
    reset_usb(start,setting);
    for(uint32_t dt=0;dt<=1000U;dt+=20U) {
        breathe_at(start+dt);
        uint32_t expected_ns=(uint32_t)((uint64_t)500000U*usb_breathing_pattern(dt)/10000U);
        uint32_t ticks=(uint32_t)((uint64_t)expected_ns*16U/1000U);
        assert(fake_pwm_period_ns==500000U&&fake_pwm_pulse_ns==expected_ns);
        assert(fake_pwm_ticks==ticks&&ticks<=80U);
        if(dt<=500U)assert(ticks>=previous);else assert(ticks<=previous);
        uint32_t change=ticks>previous?ticks-previous:previous-ticks;
        assert(change<=5U);previous=ticks;
        if(!seen[ticks]){seen[ticks]=true;distinct++;}
        uint32_t ns=fake_pwm_pulse_ns;unsigned writes=fake_pwm_writes;
        fake_now=start+dt+5U;polling_work_handler(&polling_work.work);
        assert(fake_pwm_pulse_ns==ns&&fake_pwm_writes==writes); /* no integer overwrite */
    }
    assert(distinct>=20U&&seen[0]&&seen[80]&&fake_pwm_pulse_ns==0U);
    printf("PASS minimum setting=%u: %u distinct actual tick levels, peak80/8000, monotonic 1s curve, no 5ms integer overwrite\n",(unsigned)setting,distinct);
}
static void all_caps_and_live_changes(void) {
    for(unsigned setting=0;setting<=255U;setting++) {
        reset_usb(50000U,(uint8_t)setting);
        uint32_t limit=indicator_user_limit((uint8_t)setting);
        for(uint32_t dt=0;dt<=1000U;dt+=20U) {
            breathe_at(50000U+dt);
            uint32_t expected=(uint32_t)((uint64_t)500000U*usb_breathing_pattern(dt)*limit/10000U);
            assert(fake_pwm_pulse_ns==expected);
            assert(fake_pwm_ticks<=80U*limit);
            if(setting==0U||dt==0U||dt==1000U)assert(fake_pwm_pulse_ns==0U);
            if(dt==500U)assert(fake_pwm_ticks==80U*limit);
            for(unsigned i=0;i<(unsigned)INDICATOR_LED_NUM_LEDS;i++)assert(fake_pwm_pulses[i]==expected);
        }
    }
    reset_usb(60000U,1);breathe_at(60250U);assert(fake_pwm_ticks==40U);
    fake_tp_brightness=0;polling_work_handler(&polling_work.work);assert(fake_pwm_ticks==0U);
    fake_tp_brightness=10;polling_work_handler(&polling_work.work);assert(fake_pwm_ticks==40U);
    unsigned writes=fake_pwm_writes;polling_work_handler(&polling_work.work);assert(fake_pwm_writes==writes);
    fake_led_error=-EIO;fake_led_error_index=INDICATOR_LED_NUM_LEDS-1;
    breathe_at(60500U);assert(!pwm_cache_valid&&last_led_level==-1);
    fake_led_error=0;polling_work_handler(&polling_work.work);
    assert(pwm_cache_valid&&fake_pwm_ticks==80U);
    for(unsigned i=0;i<(unsigned)INDICATOR_LED_NUM_LEDS;i++)assert(fake_pwm_pulses[i]==5000U);
    fake_pwm_ready=false;breathe_at(60750U);assert(!pwm_cache_valid&&last_led_level==-1);
    fake_pwm_ready=true;polling_work_handler(&polling_work.work);assert(pwm_cache_valid&&fake_pwm_ticks==40U);
    fake_pwm_ready=false;assert(indicator_tp_init()==-ENODEV);fake_pwm_ready=true;
    reset_usb(70000U,1);breathe_at(70250U);assert(fake_pwm_ticks==40U);
    fake_transport=ZMK_TRANSPORT_BLE;fake_touch=false;polling_work_handler(&polling_work.work);
    assert(!usb_mode&&fake_pwm_ticks==0U);
    fake_touch=true;polling_work_handler(&polling_work.work);assert(fake_pwm_ticks==80U);
    fake_transport=ZMK_TRANSPORT_USB;polling_work_handler(&polling_work.work);
    assert(usb_mode&&fake_pwm_ticks==0U&&usb_breath_phase_ms==0U);
    puts("PASS all256 caps/subpercent duty, zero, live cap, partial write/ready retry, cache, BLE integer level and USB dark reentry");
}
int main(void) {
    assert(legacy_trackpad_suite()==0);
    low_brightness_curve(1);low_brightness_curve(10);all_caps_and_live_changes();
    fake_transport=ZMK_TRANSPORT_BLE;fake_scroll=true;fake_touch=false;
    fake_tp_brightness=1;fake_activity=ZMK_ACTIVITY_ACTIVE;
    polling_work_handler(&polling_work.work);
    bool seen[81]={0};unsigned distinct=0;
    for(unsigned i=0;i<200U;i++) {
        animation_work_handler(&animation_work.work);
        uint32_t ns=(uint32_t)((uint64_t)500000U*requested_pattern/10000U);
        assert(fake_pwm_pulse_ns==ns&&fake_pwm_ticks>0U&&fake_pwm_ticks<=80U);
        if(!seen[fake_pwm_ticks]){seen[fake_pwm_ticks]=true;distinct++;}
    }
    assert(distinct>=15U);
    printf("PASS BLE/scroll fractional duty: %u distinct low-brightness tick levels, not integer OFF/ON\n",distinct);
    puts("PASS fractional trackpad PWM; hardware response/visual smoothness not asserted");
    return 0;
}
