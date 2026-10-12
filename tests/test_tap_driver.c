#include "test_env.h"
#define CONFIG_A320_TAP_OUTPUT 1
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_tap_pipeline.h"
static unsigned tap_polls;
static int32_t last_tap_dx,last_tap_dy;
static bool last_fault,last_scroll;
void tp_tap_pipeline_init(void){}
void tp_tap_pipeline_poll(uint32_t now,int32_t dx,int32_t dy,bool fault,bool scroll,uint32_t epoch){
 (void)now;(void)epoch;tap_polls++;last_tap_dx=dx;last_tap_dy=dy;last_fault=fault;last_scroll=scroll;
}
#define main pointer_baseline_main
#include "test_pointer.c"
#undef main
int main(void){
 pointer_baseline_main();assert(tap_polls>0);
 fixture();poll(4,-2,100);expect_xy(4,-2);assert(last_tap_dx==4&&last_tap_dy==-2&&!last_fault&&!last_scroll);
 idle(110);assert(last_tap_dx==0&&last_tap_dy==0&&!last_fault);
 fake_i2c_error=-5;poll(4,-2,120);assert(last_fault);fake_i2c_error=0;
 fake_gpio_state=-5;fake_now=130;report_count=0;
 a320_poll_work_handler(&sensor_data.poll_work.work);assert(last_fault);
 puts("PASS Actual driver with TAP_OUTPUT ON: complete source68 pointer/wheel suite, unscaled raw deltas forwarded, idle not UP, I2C/GPIO faults preserved; fake pipeline performs no clicks");return 0;
}
