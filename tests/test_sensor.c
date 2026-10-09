#include "test_env.h"
static unsigned sym_release_count;
int tp_sym_ctrl_release(int64_t t) { (void)t;sym_release_count++;return 0; }
uint8_t indicator_tp_get_last_valid_brightness(void) { return 40; }
#include "../config/boards/arm/bbp9981/custom_driver/a320_0x57.c"

static void key(uint32_t p,bool down,int64_t t) {
    zmk_event_t e={.valid=true,.data={.position=p,.state=down,.timestamp=t}};
    assert(ctrl_listener_cb(&e)==0);
}
static void reset_keys(void) { memset(&scroll_keys,0,sizeof(scroll_keys));atomic_set(&scroll_mode_snapshot,0); }
int main(void) {
    /* Actual listener uses original timestamp, independent of dispatch clock. */
    fake_now=90000;key(41,true,0);key(41,false,100);
    assert(!tp_scroll_mode_active() && scroll_keys.tap_pending);
    key(41,true,200);key(41,false,250);
    assert(tp_scroll_mode_active() && scroll_keys.latched && !tp_aa_is_pressed());
    uint32_t epoch=(uint32_t)atomic_get(&scroll_mode_snapshot)&TP_MODE_EPOCH_MASK;
    key(41,true,300);key(41,false,800);
    assert(scroll_keys.latched && (((uint32_t)atomic_get(&scroll_mode_snapshot)&TP_MODE_EPOCH_MASK)==epoch));
    key(41,true,1000);key(41,false,1050);key(41,true,1200);key(41,false,1250);
    assert(!scroll_keys.latched);
    reset_keys();key(41,true,0);key(40,true,10);key(40,false,20);key(41,false,50);
    assert(!scroll_keys.tap_pending);key(41,true,80);key(41,false,100);assert(!scroll_keys.latched);
    reset_keys();key(41,true,0);key(41,false,50);key(41,true,100);key(7,true,110);key(41,false,150);
    assert(!scroll_keys.latched && !scroll_keys.tap_pending);
    reset_keys();key(41,true,0);key(41,false,50);key(41,true,400);key(41,false,450);
    assert(scroll_keys.latched); /* inclusive 350 ms gap */
    reset_keys();key(41,true,0);key(41,false,50);key(41,true,401);key(41,false,451);
    assert(!scroll_keys.latched);
    reset_keys();key(41,true,0);key(41,true,10);key(41,false,450);key(41,false,460);
    assert(!scroll_keys.tap_pending && !scroll_keys.latched);
    reset_keys();key(41,true,4294967290LL);key(41,false,4294967340LL);
    key(41,true,4294967440LL);key(41,false,4294967490LL);assert(scroll_keys.latched);
    /* A complete mode round-trip between sensor polls still invalidates old motion. */
    reset_keys();key(41,true,0);key(41,false,500);assert(!tp_scroll_mode_active());
    assert(((uint32_t)atomic_get(&scroll_mode_snapshot)&TP_MODE_EPOCH_MASK)!=0);
    struct a320_dev_config cfg={.i2c={.bus=&fake_device}};
    struct a320_data data={0};struct device dev={.name="sensor",.config=&cfg,.data=&data};data.dev=&dev;
    sum_dx=40;sum_dy=40;sample_cnt=3;last_read_time=10;fake_gpio_state=1;
    a320_poll_work_handler(&data.poll_work.work);assert(sum_dx==0 && sample_cnt==0);
    fake_gpio_state=0;fake_i2c_error=0;fake_dx=2;fake_dy=3;
    a320_poll_work_handler(&data.poll_work.work);assert(tp_is_touched() && report_count==2);
    sum_dx=30;sum_dy=30;sample_cnt=2;fake_i2c_error=-EIO;
    a320_poll_work_handler(&data.poll_work.work);assert(!tp_is_touched() && sample_cnt==0 && sum_dx==0);
    fake_i2c_error=0;fake_gpio_config_error=-EIO;assert(a320_init(&dev)==-EIO);
    fake_gpio_config_error=0;assert(a320_init(&dev)==0);
    assert(sym_release_count>0);
    puts("PASS Sensor/listener: event time, single/double/hold, interrupted taps, 350/450 ms boundaries, uptime rollover, coherent epochs, I2C/GPIO faults");
}
