/* Real A320/mode service at long event times and epoch wrap boundaries.
 * The kernel, clock and input sink are host mocks, not physical-duration tests. */
#define main existing_pointer_regression_main
#include "test_pointer.c"
#undef main

static void aa_tap64(int64_t at) { key(41,true,at);key(41,false,at+30); }
int main(void) {
    fixture();
    /* Seed only counters, then use actual physical listeners across wrap. */
    scroll_keys.snapshot=UINT32_MAX & TP_MODE_EPOCH_MASK;
    atomic_set(&scroll_mode_snapshot,(int32_t)scroll_keys.snapshot);
    atomic_set(&cursor_speed_snapshot,(int32_t)UINT32_C(0xfffffffe));
    const int64_t starts[]={INT64_C(2147483500),INT64_C(4294967100),
        INT64_C(4294967500),INT64_C(1099511627776)};
    unsigned branch=0;
    int64_t at=0;
    for(unsigned i=0;i<24000U;i++) {
        if(i%6000U==0) { at=starts[branch++]; }
        else { at+=10000; }
        poll(1,0,(uint32_t)at);expect_xy(1,0);
        uint32_t before=tp_mode_speed_snapshot();
        key(37,true,at+1);poll(1,0,(uint32_t)(at+2));expect_xy(0,0);
        assert(tp_mode_speed_snapshot()!=before && (tp_mode_speed_snapshot()&1U));
        key(37,false,at+3);poll(1,0,(uint32_t)(at+4));expect_xy(1,0);
        assert(!(tp_mode_speed_snapshot()&1U));
        aa_tap64(at+100);aa_tap64(at+200);
        assert(tp_scroll_lock_is_latched() && tp_scroll_is_locked() && tp_scroll_mode_active());
        key(41,true,at+300);assert(!tp_scroll_is_locked() && tp_scroll_aa_is_pressed());
        key(41,false,at+800);assert(tp_scroll_is_locked()); /* Not a short tap. */
        aa_tap64(at+900);aa_tap64(at+1000);
        assert(!tp_scroll_mode_active() && !tp_scroll_lock_is_latched() && !tp_aa_is_pressed());
        assert(!scroll_keys.tap_pending && !scroll_keys.double_candidate);
        /* A normal idle poll retires old fractions; no drift or stale wheel. */
        idle((uint32_t)(at+1100));
        assert(!cursor_motion.x.active && !cursor_motion.y.active);
        assert(!scroll_motion.sum_dx && !scroll_motion.sum_dy && !scroll_motion.sample_cnt);
    }
    assert(!modifier_refs);
    puts("PASS stability input: 24000 uninterrupted Ctrl/aA lock/hold/unlock cycles, epoch wrap and signed/u32/2^40 event times; no sticky mode, stale fraction or idle motion; simulated clocks only");
    return 0;
}
