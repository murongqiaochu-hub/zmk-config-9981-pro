#define CONFIG_A320_RAW_CAPTURE 1
#define main old_output_main
#include "test_tap_output.c"
#undef main
int main(void) {
    cold_boot();
    const unsigned initial_queue_threads=mock_queue_starts;
    for(unsigned i=0;i<100;i++) {
        assert(tp_tap_output_update_context(context(i+1))==0);
        assert(tp_tap_output_submit()==-EPERM);
        assert_idle();
    }
    assert(mock_press_calls==0&&mock_release_calls==0&&mock_send_calls==0);
    assert(mock_queue_starts==initial_queue_threads&&!tap_output.work.pending);
    puts("PASS production raw output barrier:100 allowed contexts rejected with EPERM, no HID DOWN/UP, send or click work scheduled");
    return 0;
}
