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
        assert(fake_led_level>=CYCLE_BRT_MIN && fake_led_level<=CYCLE_BRT_MAX);
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
    set_led_brightness(30);unsigned writes=fake_led_writes;
    set_led_brightness(30);assert(fake_led_writes==writes);
    fake_led_error=-EIO;set_led_brightness(40);assert(last_led_level==30);
    writes=fake_led_writes;fake_led_error=0;set_led_brightness(40);
    assert(fake_led_writes>writes && last_led_level==40 && fake_led_level==40);
    puts("PASS Keyboard LEDs: brightness bounds, idle cancellation on Layers 1/2/3, late callbacks, wake recovery, cached writes and error retry");
}
