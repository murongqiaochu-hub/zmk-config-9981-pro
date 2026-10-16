/* Actual driver + actual micro runtime; only transport/device operations mocked. */
#define main micro_runtime_unit_main
#define fixture runtime_fixture
#include "test_micro_runtime.c"
#undef fixture
#undef main
#define CONFIG_A320_TAP_OUTPUT 1
#define CONFIG_A320_MICROTAP 1
void tp_microtap_shoulder_motion(uint32_t now){(void)now;}
#define main stock_pointer_suite
#include "test_pointer.c"
#undef main
int main(void){
 runtime_fixture(false);stock_pointer_suite();
 fixture();runtime_fixture(true);poll(3,0,10);assert(report_count==0);
 for(uint32_t t=20;t<=70;t+=10)idle(t);
 assert(submissions==1&&cursor_motion.x.remainder==0&&cursor_motion.y.remainder==0);
 complete();idle(80);
 poll(20,0,90);assert(report_count==2); /* Guard-period motion is not hidden for a later click. */
 poll(1,0,100);poll(1,0,110);assert(report_count==2&&submissions==1);
 assert(report_values[INPUT_REL_X-1]==1&&report_values[INPUT_REL_Y-1]==0);
 fixture();runtime_fixture(true);
 poll(2,0,10);poll(20,0,20);poll(1,0,30);assert(report_count==0);
 poll(1,0,40);assert(report_count==8&&submissions==0);
 assert(report_values[INPUT_REL_X-1]==1&&report_values[INPUT_REL_Y-1]==0);
 fixture();runtime_fixture(true);
 poll(-128,-128,10);poll(-128,-128,20);assert(report_count==0);
 for(uint32_t t=30;t<=80;t+=10)idle(t);
 assert(submissions==1&&report_count==0&&cursor_motion.x.remainder==0&&cursor_motion.y.remainder==0);
 fixture();runtime_fixture(true);poll(-1,-1,10);
 for(uint32_t t=20;t<=60;t+=10)idle(t);
 poll(0,0,70);assert(submissions==0&&report_count==2);
 puts("PASS Actual driver/runtime: full source68 OFF suite, first60ms click, path512 cluster discarded before pointer fractions, weak touch replay, fourth-frame early bypass, no guard-period synthetic drag");
 return 0;
}
