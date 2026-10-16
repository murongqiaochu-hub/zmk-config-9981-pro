/* Real trackpad driver, independent fractional PWM/tick assertions. */
#define main legacy_trackpad_suite
#include "test_trackpad_led.c"
#undef main

static void all_raw_pattern_pairs(void) {
    reset_usb(35000U,100);
    unsigned pairs=0;
    /* Includes every required raw0..100/pattern0..100 pair and uint8 clamps. */
    for(unsigned raw=0;raw<=255U;raw++) {
        unsigned bounded_raw=raw>100U?100U:raw;
        unsigned limit=(bounded_raw*bounded_raw+50U)/100U;
        if(bounded_raw>0U&&limit==0U){limit=1U;}
        fake_tp_brightness=last_valid_brt=(uint8_t)raw;
        for(unsigned pattern=0;pattern<=255U;pattern++) {
            unsigned bounded_pattern=pattern>100U?100U:pattern;
            uint32_t original_ns=(uint32_t)((uint64_t)500000U*bounded_pattern*limit/10000U);
            set_led_brightness((uint8_t)pattern);
            assert_tp_output(pattern,raw);
            assert(fake_pwm_pulse_ns==original_ns/3U);
            assert(original_ns-3U*fake_pwm_pulse_ns<=2U); /* Only integer-ns truncation. */
            assert(last_valid_brt==raw&&fake_tp_brightness==raw&&fake_brightness==73);
            assert(requested_pattern==pattern&&last_led_level==indicator_pattern_level((uint8_t)pattern,(uint8_t)raw));
            if(raw==0U||pattern==0U){assert(fake_pwm_pulse_ns==0U&&fake_pwm_ticks==0U);}
            pairs++;
        }
    }
    last_valid_brt=1;set_led_brightness(1);
    assert(fake_pwm_pulse_ns==16U&&fake_pwm_ticks==0U); /* No artificial minimum clamp. */
    set_led_brightness(100);
    assert(fake_pwm_pulse_ns==1666U&&fake_pwm_ticks==26U);
    assert((uint64_t)fake_pwm_period_ns*16U/1000U==8000U); /* Still 2 kHz. */
    printf("PASS %u real-leaf raw/pattern pairs: exact old PWM /3, true zero, unchanged raw/gamma/KB, 500us carrier; minimum peak 1666ns/26ticks, sub-tick animation points may remain zero\n",pairs);
}

static void low_brightness_curve(uint8_t setting) {
    const uint32_t start=40000U;
    bool seen[27]={0},seen_subtick=false;unsigned distinct=0;
    uint32_t previous=0;
    reset_usb(start,setting);
    for(uint32_t dt=0;dt<=1000U;dt+=20U) {
        breathe_at(start+dt);
        uint32_t expected_ns=(uint32_t)((uint64_t)500000U*usb_breathing_pattern(dt)/30000U);
        uint32_t ticks=(uint32_t)((uint64_t)expected_ns*16U/1000U);
        assert_tp_output(requested_pattern,setting);
        assert(fake_pwm_period_ns==500000U&&fake_pwm_pulse_ns==expected_ns);
        assert(fake_pwm_ticks==ticks&&ticks<=26U);
        if(dt<=500U)assert(ticks>=previous);else assert(ticks<=previous);
        uint32_t change=ticks>previous?ticks-previous:previous-ticks;
        assert(change<=2U);previous=ticks;
        if(!seen[ticks]){seen[ticks]=true;distinct++;}
        if(requested_pattern>0U&&ticks==0U){seen_subtick=true;}
        uint32_t ns=fake_pwm_pulse_ns;unsigned writes=fake_pwm_writes;
        fake_now=start+dt+5U;polling_work_handler(&polling_work.work);
        assert(fake_pwm_pulse_ns==ns&&fake_pwm_writes==writes); /* no integer overwrite */
    }
    assert(distinct==20U&&seen[0]&&seen[26]&&seen_subtick&&fake_pwm_pulse_ns==0U);
    printf("PASS minimum setting=%u: %u distinct actual tick levels, peak26/8000, monotonic 1s curve, max tick step2, no 5ms integer overwrite\n",(unsigned)setting,distinct);
}
static void all_caps_and_live_changes(void) {
    for(unsigned setting=0;setting<=255U;setting++) {
        reset_usb(50000U,(uint8_t)setting);
        for(uint32_t dt=0;dt<=1000U;dt+=20U) {
            breathe_at(50000U+dt);
            uint32_t expected=expected_tp_pulse(usb_breathing_pattern(dt),setting);
            assert_tp_output(requested_pattern,setting);
            assert(fake_pwm_ticks<=(uint32_t)((uint64_t)expected_tp_pulse(100,setting)*16U/1000U));
            if(setting==0U||dt==0U||dt==1000U)assert(fake_pwm_pulse_ns==0U);
            if(dt==500U)assert(fake_pwm_pulse_ns==expected_tp_pulse(100,setting));
            for(unsigned i=0;i<(unsigned)INDICATOR_LED_NUM_LEDS;i++)assert(fake_pwm_pulses[i]==expected);
        }
    }
    reset_usb(60000U,1);breathe_at(60250U);assert(fake_pwm_ticks==13U);
    fake_tp_brightness=0;polling_work_handler(&polling_work.work);assert(fake_pwm_ticks==0U);
    fake_tp_brightness=10;polling_work_handler(&polling_work.work);assert(fake_pwm_ticks==13U);
    unsigned writes=fake_pwm_writes;polling_work_handler(&polling_work.work);assert(fake_pwm_writes==writes);
    for(unsigned bad=0;bad<(unsigned)INDICATOR_LED_NUM_LEDS;bad++) {
        set_led_brightness(50);assert_tp_output(50,10);
        writes=fake_pwm_writes;fake_led_error=-EIO;fake_led_error_index=(int)bad;
        breathe_at(60500U);assert(!pwm_cache_valid&&last_led_level==-1);
        assert(fake_pwm_writes==writes+INDICATOR_LED_NUM_LEDS);
        for(unsigned i=0;i<(unsigned)INDICATOR_LED_NUM_LEDS;i++) {
            assert(last_pwm_pulse_ns[i]==833U); /* Partial success must not commit the cache. */
            assert(fake_pwm_pulses[i]==(i==bad?833U:1666U));
        }
        writes=fake_pwm_writes;
        fake_led_error=0;polling_work_handler(&polling_work.work);
        assert(pwm_cache_valid&&fake_pwm_ticks==26U&&fake_pwm_writes==writes+INDICATOR_LED_NUM_LEDS);
        assert_tp_output(100,10);
        writes=fake_pwm_writes;polling_work_handler(&polling_work.work);assert(fake_pwm_writes==writes);
    }
    fake_pwm_ready=false;breathe_at(60750U);
    assert(!pwm_cache_valid&&last_led_level==-1&&fake_pwm_writes==writes);
    fake_pwm_ready=true;polling_work_handler(&polling_work.work);
    assert(pwm_cache_valid&&fake_pwm_ticks==13U&&fake_pwm_writes==writes+INDICATOR_LED_NUM_LEDS);
    assert_tp_output(50,10);
    fake_pwm_ready=false;assert(indicator_tp_init()==-ENODEV);fake_pwm_ready=true;
    reset_usb(70000U,1);breathe_at(70250U);assert(fake_pwm_ticks==13U);
    fake_transport=ZMK_TRANSPORT_BLE;fake_touch=false;polling_work_handler(&polling_work.work);
    assert(!usb_mode&&fake_pwm_ticks==0U);
    fake_touch=true;polling_work_handler(&polling_work.work);assert(fake_pwm_ticks==26U);assert_tp_output(100,1);
    fake_transport=ZMK_TRANSPORT_USB;polling_work_handler(&polling_work.work);
    assert(usb_mode&&fake_pwm_ticks==0U&&usb_breath_phase_ms==0U);
    puts("PASS all256 caps/subpercent duty, zero, live cap, per-channel partial write/ready retry, cache, BLE fractional output and USB dark reentry");
}
int main(void) {
    assert(legacy_trackpad_suite()==0);
    all_raw_pattern_pairs();
    low_brightness_curve(1);low_brightness_curve(10);all_caps_and_live_changes();
    fake_transport=ZMK_TRANSPORT_BLE;fake_scroll=true;fake_touch=false;
    fake_tp_brightness=1;fake_activity=ZMK_ACTIVITY_ACTIVE;
    polling_work_handler(&polling_work.work);
    bool seen[27]={0};unsigned distinct=0;
    for(unsigned i=0;i<200U;i++) {
        animation_work_handler(&animation_work.work);
        uint32_t ns=(uint32_t)((uint64_t)500000U*requested_pattern/30000U);
        assert_tp_output(requested_pattern,1);
        assert(fake_pwm_pulse_ns==ns&&fake_pwm_ticks>0U&&fake_pwm_ticks<=26U);
        if(!seen[fake_pwm_ticks]){seen[fake_pwm_ticks]=true;distinct++;}
    }
    assert(distinct>=15U);
    printf("PASS BLE/scroll fractional duty: %u distinct low-brightness tick levels, not integer OFF/ON\n",distinct);
    puts("PASS fractional trackpad PWM; host mock only, optical brightness/hardware response/visual smoothness not asserted");
    return 0;
}
