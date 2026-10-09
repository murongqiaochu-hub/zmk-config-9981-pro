#include "test_env.h"
static bool fake_scroll,fake_touch;
bool tp_scroll_mode_active(void) { return fake_scroll; }
bool tp_is_touched(void) { return fake_touch; }
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_led.c"
int main(void) {
    assert(indicator_tp_init()==0);
    fake_scroll=true;polling_work_handler(&polling_work.work);
    assert(scroll_mode_on && animation_work.pending);
    for(int i=0;i<150;i++) { animation_work_handler(&animation_work.work);assert(fake_led_level>=BRT_MIN && fake_led_level<=BRT_MAX); }
    fake_transport=ZMK_TRANSPORT_USB;polling_work_handler(&polling_work.work);
    assert(usb_mode && !animation_work.pending && usb_flash_work.pending);
    fake_transport=ZMK_TRANSPORT_BLE;polling_work_handler(&polling_work.work);
    assert(!usb_mode && scroll_mode_on && animation_work.pending && !usb_flash_work.pending);
    fake_scroll=false;fake_touch=true;polling_work_handler(&polling_work.work);
    assert(touch_active && fake_led_level>0);
    fake_touch=false;polling_work_handler(&polling_work.work);assert(auto_off_work.pending);
    auto_off_work_handler(&auto_off_work.work);assert(fake_led_level==0);
    puts("PASS Trackpad LEDs: scroll indication, USB precedence, BLE restoration, touch-release auto-off");
}
