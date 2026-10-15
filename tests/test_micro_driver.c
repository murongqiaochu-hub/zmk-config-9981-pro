/* Actual driver + actual micro runtime, with only transport/device operations mocked. */
#define main micro_runtime_unit_main
#define fixture runtime_fixture
#include "test_micro_runtime.c"
#undef fixture
#undef main
#define CONFIG_A320_TAP_OUTPUT 1
#define CONFIG_A320_MICROTAP 1
void tp_microtap_shoulder_motion(uint32_t now) { (void)now; }
#define main stock_pointer_suite
#include "test_pointer.c"
#undef main
int main(void) {
    runtime_fixture(false);
    stock_pointer_suite(); /* entire source68 arithmetic, wheel, fault and LED independence */
    fixture();runtime_fixture(true);
    poll(-1,-1,10);assert(report_count==0);
    idle(20);idle(30);idle(40);idle(50);idle(60);idle(70);assert(submissions==1);
    assert(cursor_motion.x.remainder==0&&cursor_motion.y.remainder==0);
    complete();idle(80);
    poll(20,0,90);expect_xy(24,0);
    assert(submissions==1);
    puts("PASS Actual MICRO ON driver + runtime: full source68 suite while runtime OFF, discarded raw never reaches fractions, click at unchanged coordinates, normal movement early bypass");
    return 0;
}
