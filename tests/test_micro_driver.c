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
 fixture();runtime_fixture(true);poll(6,0,10);assert(report_count==0);
 for(uint32_t t=20;t<=120;t+=10)idle(t);
 idle(129);assert(submissions==0);idle(130);
 assert(submissions==1&&cursor_motion.x.remainder==0&&cursor_motion.y.remainder==0);
 complete();idle(140);
 poll(20,0,150);assert(report_count==2); /* Auto rearm motion stays ordinary movement. */
 poll(1,0,160);assert(report_count==2&&submissions==1);
 poll(1,0,170);assert(report_count==2&&submissions==1);
 assert(report_values[INPUT_REL_X-1]==1&&report_values[INPUT_REL_Y-1]==0);

 fixture();runtime_fixture(true);
 poll(2,0,10);poll(20,0,20);poll(1,0,30);assert(report_count==0);
 poll(1,0,40);assert(report_count==8&&submissions==0);
 assert(report_values[INPUT_REL_X-1]==1&&report_values[INPUT_REL_Y-1]==0);

 fixture();runtime_fixture(true);
 poll(-128,-128,10);poll(-128,-128,20);assert(report_count==0);
 for(uint32_t t=30;t<=130;t+=10)idle(t);
 idle(139);assert(submissions==0);idle(140);
 assert(submissions==1&&report_count==0&&cursor_motion.x.remainder==0&&cursor_motion.y.remainder==0);

 for(int8_t weak=1;weak<6;weak++) {
  fixture();runtime_fixture(true);poll(weak,0,10);assert(report_count==0);
  for(uint32_t t=20;t<=120;t+=10)poll(0,0,t);
  poll(0,0,129);assert(report_count==0&&submissions==0);
  /* Both poll()/idle() reset report_count each time. A weak candidate replays
   * at quiet expiry; do NOT use idle()'s no-report assertion for this poll. */
  poll(0,0,130);assert(submissions==0&&report_count==2);
 }
 fixture();runtime_fixture(true);poll(-1,-1,10);
 for(uint32_t t=20;t<=120;t+=10)poll(0,0,t);
 poll(0,0,130);assert(submissions==0&&report_count==2);
 assert(report_values[INPUT_REL_X-1]==-1&&report_values[INPUT_REL_Y-1]==-1);

 fixture();runtime_fixture(true);position_edge(15,4,true);refs[0]++;
 position_edge(17,4,false);refs[0]--;poll(6,0,20);
 assert(report_count==2&&submissions==0); /* Two-poll short native click was observed. */
 for(uint32_t t=30;t<=610;t+=10)idle(t);
 idle(619);assert(native_guard_observe(false).blocked);
 poll(6,0,620);assert(report_count==0&&!native_guard_observe(false).blocked);
 for(uint32_t t=630;t<=730;t+=10)idle(t);
 idle(740);assert(submissions==1&&report_count==0); /* No extra auto queue delay. */

 fixture();runtime_fixture(true);refs[0]=1;
 poll(6,0,10);assert(report_count==2&&refs[0]==1&&submissions==0);
 for(uint32_t t=20;t<=710;t+=10)idle(t);
 poll(8,-3,720);assert(report_count==2&&refs[0]==1&&submissions==0);
 puts("PASS Actual driver/runtime: full OFF pointer suite, floor6/first120ms, path512 discard before fractions, weak1..5 quiet replay, fourth-frame FIFO, native short D/U and held drag/move, 599/600 guard; report_count checked per poll");
 return 0;
}
