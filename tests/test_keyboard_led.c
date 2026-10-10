#include "test_env.h"
#include "../config/boards/arm/bbp9981/custom_driver/keyboard_backlight.c"
int main(void) {
    assert(keyboardbacklight_init()==0);
    zmk_event_t key={.valid=true,.data={.position=41,.state=true,.timestamp=0}};
    key_listener_cb(&key);
    fake_layer=2;polling_work_handler(&polling_work.work);
    for(int i=0;i<200;i++) {
        cycle_work_handler(&cycle_work.work);
        assert(cycle_brightness>=CYCLE_BRT_MIN && cycle_brightness<=CYCLE_BRT_MAX);
        assert(fake_led_level>=0 && fake_led_level<=indicator_user_limit(fake_brightness));
    }
    for(int layer=1;layer<=3;layer++) {
        fake_activity=ZMK_ACTIVITY_ACTIVE;fake_layer=layer;key_listener_cb(&key);
        polling_work_handler(&polling_work.work);
        fake_activity=ZMK_ACTIVITY_IDLE;polling_work_handler(&polling_work.work);
        assert(fake_led_level==0 && !blink_work.pending && !cycle_work.pending);
        unsigned writes=fake_led_writes;
        blink_work_handler(&blink_work.work);cycle_work_handler(&cycle_work.work);
        assert(fake_led_writes==writes);
        fake_activity=ZMK_ACTIVITY_ACTIVE;key_listener_cb(&key);polling_work_handler(&polling_work.work);
        assert(blink_work.pending || cycle_work.pending);
    }
    /* Every layer must respect the same limit and the B switch, even mid-animation. */
    for(int layer=0;layer<=3;layer++){
        fake_layer=layer;fake_activity=ZMK_ACTIVITY_ACTIVE;fake_rgb_on=true;key_listener_cb(&key);
        for(int setting=0;setting<=100;setting++){
            fake_brightness=(uint8_t)setting;fake_tp_brightness=0;
            polling_work_handler(&polling_work.work);int peak=0;
            for(int i=0;i<180;i++){
                if(layer==1||layer==3){blink_work_handler(&blink_work.work);}
                if(layer==2){cycle_work_handler(&cycle_work.work);}
                assert(fake_led_level<=indicator_user_limit((uint8_t)setting));
                if(fake_led_level>peak){peak=fake_led_level;}
            }
            assert(peak==indicator_user_limit((uint8_t)setting));
        }
        fake_rgb_on=false;polling_work_handler(&polling_work.work);assert(fake_led_level==0);
        blink_work_handler(&blink_work.work);cycle_work_handler(&cycle_work.work);assert(fake_led_level==0);
        fake_rgb_on=true;fake_brightness=10;polling_work_handler(&polling_work.work);
        for(int i=0;i<180;i++){
            if(layer==1||layer==3){blink_work_handler(&blink_work.work);}
            if(layer==2){cycle_work_handler(&cycle_work.work);}
            assert(fake_led_level<=1);
        }
    }
    set_led_brightness(30);unsigned writes=fake_led_writes;
    set_led_brightness(30);assert(fake_led_writes==writes);
    assert(INDICATOR_LED_NUM_LEDS==2);
    for(int bad=0;bad<2;bad++) {
        fake_led_error=-EIO;fake_led_error_index=bad;set_led_brightness(40);
        assert(last_led_level==-1 && fake_led_levels[bad]==30 && fake_led_levels[1-bad]==40);
        writes=fake_led_writes;fake_led_error=0;set_led_brightness(30);
        assert(fake_led_writes==writes+2 && fake_led_levels[0]==30 && fake_led_levels[1]==30);
    }
    fake_led_error=-EIO;fake_led_error_index=-1;set_led_brightness(40);assert(last_led_level==-1);
    writes=fake_led_writes;fake_led_error=0;set_led_brightness(40);
    assert(fake_led_writes>writes && last_led_level==40 && fake_led_level==40);
    puts("PASS Keyboard LEDs: every layer 0..100 cap, zero/B-off, live changes, 1% low end, channel isolation, idle cancellation on Layers 1/2/3, late callbacks, wake recovery, cached writes and error retry");
}
