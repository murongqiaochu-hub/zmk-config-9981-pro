#include "test_env.h"
static bool fake_scroll,fake_touch;
bool tp_scroll_mode_active(void) { return fake_scroll; }
bool tp_is_touched(void) { return fake_touch; }
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_led.c"
int main(void) {
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
    assert(indicator_user_limit(0)==0 && indicator_user_limit(10)==1);
    assert(indicator_user_limit(20)==4 && indicator_user_limit(40)==16);
    assert(indicator_user_limit(100)==100);
    for(int mode=0;mode<3;mode++) {
        fake_activity=ZMK_ACTIVITY_ACTIVE;
        fake_transport=mode==2?ZMK_TRANSPORT_USB:ZMK_TRANSPORT_BLE;
        fake_scroll=mode==1;fake_touch=mode==0;
        for(int setting=0;setting<=100;setting++) {
            fake_tp_brightness=(uint8_t)setting;fake_brightness=100;
            polling_work_handler(&polling_work.work);
            if(mode==1){assert(animation_work.pending && !usb_flash_work.pending);}
            if(mode==2){assert(usb_flash_work.pending && !animation_work.pending);}
            int peak=0;
            for(int i=0;i<150;i++) {
                if(mode==1){animation_work_handler(&animation_work.work);}
                if(mode==2){usb_flash_work_handler(&usb_flash_work.work);}
                assert(fake_led_level<=indicator_user_limit((uint8_t)setting));
                if(fake_led_level>peak){peak=fake_led_level;}
            }
            assert(peak==indicator_user_limit((uint8_t)setting));
        }
    }
    /* A live zero while USB's ON phase is active must extinguish immediately. */
    fake_tp_brightness=10;polling_work_handler(&polling_work.work);
    if(!usb_flash_state){usb_flash_work_handler(&usb_flash_work.work);}
    assert(fake_led_level==1);
    fake_tp_brightness=0;polling_work_handler(&polling_work.work);assert(fake_led_level==0);
    fake_tp_brightness=10;polling_work_handler(&polling_work.work);assert(fake_led_level==1);
    /* Upstream idle auto-off returns zero, not a request to forget the user's cap. */
    fake_activity=ZMK_ACTIVITY_IDLE;fake_tp_brightness=0;
    polling_work_handler(&polling_work.work);assert(last_valid_brt==10 && fake_led_level<=1);
    fake_activity=ZMK_ACTIVITY_ACTIVE;fake_tp_brightness=10;
    fake_transport=ZMK_TRANSPORT_BLE;fake_scroll=true;fake_touch=false;
    polling_work_handler(&polling_work.work);
    assert(!usb_mode && animation_work.pending && !usb_flash_work.pending);
    fake_scroll=false;fake_touch=true;polling_work_handler(&polling_work.work);
    assert(touch_active && fake_led_level==1);
    fake_touch=false;polling_work_handler(&polling_work.work);assert(auto_off_work.pending);
    auto_off_work_handler(&auto_off_work.work);assert(fake_led_level==0);
    /* Invalid caches retry write failures on the next poll. */
    fake_touch=true;fake_tp_brightness=20;fake_led_error=-EIO;
    polling_work_handler(&polling_work.work);assert(last_led_level==-1);
    unsigned writes=fake_led_writes;fake_led_error=0;
    polling_work_handler(&polling_work.work);assert(fake_led_level==4 && fake_led_writes>writes);
    writes=fake_led_writes;polling_work_handler(&polling_work.work);assert(fake_led_writes==writes);
    puts("PASS Trackpad LEDs: full scaling bounds, 1%/zero, live BLE/scroll/USB limits, independent controls, idle cache, auto-off and write retry");
}
