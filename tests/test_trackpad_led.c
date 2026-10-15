#include "test_env.h"
static bool fake_scroll,fake_touch;
bool tp_scroll_mode_active(void) { return fake_scroll; }
bool tp_is_touched(void) { return fake_touch; }
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_led.c"

static void reset_usb(uint32_t now, uint8_t setting) {
    fake_now=now;
    fake_activity=ZMK_ACTIVITY_ACTIVE;
    fake_transport=ZMK_TRANSPORT_USB;
    fake_scroll=fake_touch=false;
    fake_tp_brightness=setting;
    fake_brightness=73; /* Independent keyboard/RGB setting must not be changed. */
    fake_led_error=0;fake_led_error_index=-1;
    assert(indicator_tp_init()==0);
    assert(fake_led_level==0);
    unsigned writes=fake_led_writes;
    polling_work_handler(&polling_work.work);
    assert(usb_mode && usb_breath_work.pending && !animation_work.pending);
    assert(usb_breath_work.delay==20 && fake_led_level==0 && requested_pattern==0);
    assert(fake_led_writes==writes); /* Not even a transient full-bright startup write. */
}

static void breathe_at(uint32_t now) {
    fake_now=now;
    usb_breath_work_handler(&usb_breath_work.work);
    assert(usb_breath_work.delay==20);
    assert(fake_brightness==73);
}

static void test_existing_modes_and_scaling(void) {
    assert(indicator_tp_init()==0);
    unsigned previous=0;
    for(unsigned setting=0;setting<=255;setting++) {
        unsigned limit=indicator_user_limit((uint8_t)setting);
        assert(limit>=previous && limit<=100);previous=limit;
        for(unsigned pattern=0;pattern<=255;pattern++) {
            unsigned value=indicator_pattern_level((uint8_t)pattern,(uint8_t)setting);
            assert(value<=limit);if(setting==0||pattern==0){assert(value==0);}
        }
    }
    assert(indicator_user_limit(0)==0 && indicator_user_limit(1)==1);
    assert(indicator_user_limit(10)==1);
    assert(indicator_user_limit(20)==4 && indicator_user_limit(40)==16);
    assert(indicator_user_limit(100)==100);
    for(int mode=0;mode<3;mode++) {
        fake_activity=ZMK_ACTIVITY_ACTIVE;
        fake_transport=mode==2?ZMK_TRANSPORT_USB:ZMK_TRANSPORT_BLE;
        fake_scroll=mode==1;fake_touch=mode==0;
        for(int setting=0;setting<=100;setting++) {
            fake_tp_brightness=(uint8_t)setting;fake_brightness=100;
            polling_work_handler(&polling_work.work);
            if(mode==1){assert(animation_work.pending && !usb_breath_work.pending);}
            if(mode==2){assert(usb_breath_work.pending && !animation_work.pending);}
            int peak=0;
            for(int i=0;i<150;i++) {
                if(mode==1){animation_work_handler(&animation_work.work);assert(animation_work.delay==20);}
                if(mode==2){fake_now+=20;usb_breath_work_handler(&usb_breath_work.work);}
                assert(fake_led_level<=indicator_user_limit((uint8_t)setting));
                if(fake_led_level>peak){peak=fake_led_level;}
            }
            assert(peak==indicator_user_limit((uint8_t)setting));
            assert(fake_brightness==100 && fake_tp_brightness==setting);
        }
    }
    /* A live zero at USB's peak extinguishes immediately; 1% remains usable. */
    fake_now+=500;usb_breath_work_handler(&usb_breath_work.work);
    assert(requested_pattern==100);
    fake_tp_brightness=1;polling_work_handler(&polling_work.work);assert(fake_led_level==1);
    fake_tp_brightness=0;polling_work_handler(&polling_work.work);assert(fake_led_level==0);
    fake_tp_brightness=10;polling_work_handler(&polling_work.work);assert(fake_led_level==1);
    /* Upstream idle auto-off returns zero, not a request to forget the user's cap. */
    fake_activity=ZMK_ACTIVITY_IDLE;fake_tp_brightness=0;
    polling_work_handler(&polling_work.work);assert(last_valid_brt==10 && fake_led_level<=1);
    fake_activity=ZMK_ACTIVITY_ACTIVE;fake_tp_brightness=10;
    fake_transport=ZMK_TRANSPORT_BLE;fake_scroll=true;fake_touch=false;
    polling_work_handler(&polling_work.work);
    assert(!usb_mode && animation_work.pending && !usb_breath_work.pending);
    /* Preserve the old scroll triangle: 10 -> 100 -> 20, step 5 every 20 ms. */
    for(int i=1;i<=18;i++) {
        animation_work_handler(&animation_work.work);
        assert(requested_pattern==10+5*i && animation_work.delay==20);
    }
    for(int i=1;i<=16;i++) {
        animation_work_handler(&animation_work.work);assert(requested_pattern==100-5*i);
    }
    animation_work_handler(&animation_work.work);assert(requested_pattern==25);
    fake_scroll=false;fake_touch=true;polling_work_handler(&polling_work.work);
    assert(touch_active && fake_led_level==1);
    fake_touch=false;polling_work_handler(&polling_work.work);assert(auto_off_work.pending);
    assert(auto_off_work.delay==5000);
    auto_off_work_handler(&auto_off_work.work);assert(fake_led_level==0);
    /* Invalid caches retry write failures on the next poll. */
    fake_touch=true;fake_tp_brightness=20;fake_led_error=-EIO;
    polling_work_handler(&polling_work.work);assert(last_led_level==-1);
    unsigned writes=fake_led_writes;fake_led_error=0;
    polling_work_handler(&polling_work.work);assert(fake_led_level==4 && fake_led_writes>writes);
    writes=fake_led_writes;polling_work_handler(&polling_work.work);assert(fake_led_writes==writes);
}

static void test_usb_curve_and_timing(void) {
    const uint32_t start=10000;
    const unsigned offsets[]={0,250,500,750,1000};
    const unsigned levels[]={0,50,100,50,0};
    reset_usb(start,100);
    for(unsigned cycle=0;cycle<5;cycle++) {
        for(unsigned i=0;i<5;i++) {
            breathe_at(start+cycle*1000+offsets[i]);
            assert(requested_pattern==levels[i] && fake_led_level==(int)levels[i]);
        }
    }

    reset_usb(start,100);
    int samples[51],previous=0,max_step=0;
    for(unsigned t=0;t<=1000;t+=20) {
        breathe_at(start+t);
        int level=fake_led_level;
        int step=abs(level-previous);
        assert(level>=0 && level<=100 && step<=6);
        if(step>max_step){max_step=step;}
        if(t<=500){assert(level>=previous);}else{assert(level<=previous);}
        samples[t/20]=level;previous=level;
    }
    assert(samples[0]==0 && samples[25]==100 && samples[50]==0 && max_step==6);
    for(unsigned t=0;t<=1000;t+=20) {
        breathe_at(start+1000+t);assert(fake_led_level==samples[t/20]);
    }
    /* A late callback catches up from real elapsed uptime, not callback counts. */
    const uint32_t late_offsets[]={2237,3750,12750,16500,16500,17000};
    for(unsigned i=0;i<sizeof(late_offsets)/sizeof(late_offsets[0]);i++) {
        uint32_t elapsed=late_offsets[i];
        breathe_at(start+elapsed);
        assert(usb_breath_phase_ms==elapsed%1000);
        assert(requested_pattern==usb_breathing_pattern(elapsed%1000));
    }
    assert(fake_led_level==0);
    /* Cross uint32 uptime rollover, then repeat without changing the period. */
    uint32_t wrap_start=UINT32_MAX-249U;
    reset_usb(wrap_start,100);
    for(unsigned cycle=0;cycle<3;cycle++) {
        for(unsigned i=0;i<5;i++) {
            breathe_at(wrap_start+cycle*1000+offsets[i]);
            assert(fake_led_level==(int)levels[i]);
        }
    }
}

static void test_usb_limits_retry_and_reentry(void) {
    reset_usb(30000,100);
    breathe_at(30500);assert(fake_led_level==100);
    unsigned writes=fake_led_writes;
    auto_off_work_handler(&auto_off_work.work);assert(fake_led_writes==writes);
    fake_tp_brightness=20;fake_led_error=-EIO;
    fake_led_error_index=INDICATOR_LED_NUM_LEDS-1; /* Also exercise partial multi-LED failure. */
    polling_work_handler(&polling_work.work);assert(last_led_level==-1);
    writes=fake_led_writes;fake_led_error=0;
    polling_work_handler(&polling_work.work);
    assert(fake_led_level==4 && last_led_level==4);
    assert(fake_led_writes==writes+INDICATOR_LED_NUM_LEDS);
    writes=fake_led_writes;polling_work_handler(&polling_work.work);assert(fake_led_writes==writes);
    fake_led_error=-EIO;breathe_at(30750);assert(last_led_level==-1 && requested_pattern==50);
    fake_led_error=0;polling_work_handler(&polling_work.work);assert(fake_led_level==2);
    fake_tp_brightness=1;polling_work_handler(&polling_work.work);assert(fake_led_level==1);
    fake_tp_brightness=0;polling_work_handler(&polling_work.work);assert(fake_led_level==0);
    fake_tp_brightness=1;polling_work_handler(&polling_work.work);assert(fake_led_level==1);
    breathe_at(31000);assert(fake_led_level==0);
    breathe_at(31500);assert(fake_led_level==1);

    /* Stale callback cannot write or reschedule even before polling notices BLE. */
    fake_transport=ZMK_TRANSPORT_BLE;fake_now=31750;
    writes=fake_led_writes;
    unsigned schedules=usb_breath_work.schedules;
    usb_breath_work_handler(&usb_breath_work.work);
    assert(fake_led_writes==writes && usb_breath_work.schedules==schedules);
    assert(usb_breath_phase_ms==500 && requested_pattern==100);
    fake_touch=true;fake_tp_brightness=20;polling_work_handler(&polling_work.work);
    assert(!usb_mode && !usb_breath_work.pending && fake_led_level==4 && touch_active);
    writes=fake_led_writes;schedules=usb_breath_work.schedules;
    usb_breath_work_handler(&usb_breath_work.work);
    assert(fake_led_writes==writes && usb_breath_work.schedules==schedules && fake_led_level==4);

    /* USB reentry resets phase and writes only dark, not the BLE peak at the new cap. */
    fake_transport=ZMK_TRANSPORT_USB;fake_tp_brightness=100;
    polling_work_handler(&polling_work.work);
    assert(usb_mode && requested_pattern==0 && fake_led_level==0 && usb_breath_phase_ms==0);
    assert(fake_led_writes==writes+INDICATOR_LED_NUM_LEDS && usb_breath_work.delay==20);
    writes=fake_led_writes;
    breathe_at(31750);assert(fake_led_writes==writes && fake_led_level==0);
    breathe_at(31770);assert(fake_led_level==0);
    breathe_at(32000);assert(fake_led_level==50);
    breathe_at(32250);assert(fake_led_level==100);
    assert(fake_brightness==73 && fake_tp_brightness==100);
}

int main(void) {
    test_existing_modes_and_scaling();
    test_usb_curve_and_timing();
    test_usb_limits_retry_and_reentry();
    puts("PASS Trackpad LEDs: 1000ms smoothstep USB 0/50/100/50/0, 20ms max step 6, repeat/late/wrap, full scaling, zero/1%, cache/retry, stale BLE/reentry, no startup flash, unchanged scroll/touch/auto-off/independent controls");
}
