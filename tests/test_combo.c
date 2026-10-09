#include "test_env.h"
uint8_t indicator_tp_get_last_valid_brightness(void) { return 40; }
#include "../config/boards/arm/bbp9981/custom_driver/a320_0x57.c"
#undef DT_DRV_COMPAT
#include "../config/boards/arm/bbp9981/custom_driver/behavior_aa_sym_ctrl.c"
static void key(uint32_t p,bool down,int64_t t) {
    zmk_event_t e={.valid=true,.data={.position=p,.state=down,.timestamp=t}};
    ctrl_listener_cb(&e);
}
int main(void) {
    struct zmk_behavior_binding b={0};
    struct zmk_behavior_binding_event e={.layer=2,.position=40,.timestamp=50};
    key(41,true,0);fake_layer=2; /* mock keymap: existing hold-preferred resolves on SYM down */
    sym_pressed(&b,e);key(40,true,50);
    assert(modifier_refs==1 && fake_layer==2 && tp_scroll_mode_active());
    struct a320_dev_config cfg={.i2c={.bus=&fake_device}};
    struct a320_data data={0};struct device dev={.name="sensor",.config=&cfg,.data=&data};data.dev=&dev;
    motion_gpio_dev=&fake_device;fake_gpio_state=0;fake_dx=5;fake_dy=6;
    for(int i=0;i<4;i++){fake_now=60+i*10;a320_poll_work_handler(&data.poll_work.work);}
    assert(report_count==2 && report_values[2]!=0 && report_values[3]!=0);
    assert(report_modifier_snapshot[2]==1 && report_modifier_snapshot[3]==1);
    /* Real driver listener calls real behavior release, even while SYM stays down. */
    key(41,false,100);assert(modifier_refs==0 && !ctrl_owned && !tp_scroll_mode_active());
    sym_released(&b,e);key(40,false,110);assert(modifier_refs==0 && key_up_calls==1);
    key(41,true,120);key(41,false,150);assert(!scroll_keys.latched);
    /* Reverse release order leaves momentary scrolling until aA is also released. */
    key(41,true,200);sym_pressed(&b,e);key(40,true,220);
    sym_released(&b,e);key(40,false,230);assert(modifier_refs==0 && tp_scroll_mode_active());
    key(41,false,240);assert(modifier_refs==0 && !scroll_keys.tap_pending);
    puts("PASS Driver+behavior integration: Ctrl accompanies both wheel axes, first-aA-release reaches actual Ctrl owner, reverse release order, no chord-as-double-tap");
}
