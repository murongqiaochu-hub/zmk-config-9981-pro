/* Sustained synthetic/native ownership cycles, with the real microtap runtime.
 * Mock reports are enqueue acceptance, NOT host ACK or physical soak duration. */
#define main existing_micro_runtime_suite
#include "test_micro_runtime.c"
#undef main

int main(void) {
    fixture(true);
    uint64_t elapsed=0;
    for(unsigned i=0;i<20000U;i++) {
        /* Observation buffers are test-only; production gesture/guard/spacing,
         * bridge counters, epochs and FIFO are NOT reset between cycles. */
        moves=trace_count=0;
        elapsed+=UINT64_C(86400000);
        uint32_t at=(uint32_t)elapsed;
        unsigned before=submissions;
        position_edge(at,4,true);position_edge(at,4,false);
        poll_at(at,0,0,true,false);assert(native_guard_observe(false).blocked);
        /* Preserve normal poll cadence inside this window; a >100ms missing
         * poll intentionally invokes a separate conservative re-arm guard. */
        for(unsigned n=10U;n<=590U;n+=10U) { poll_at(at+n,0,0,true,false); }
        poll_at(at+599U,0,0,true,false);assert(native_guard_observe(false).blocked);
        poll_at(at+600U,0,0,true,false);assert(!native_guard_observe(false).blocked);
        tap_at(at+610U,8,0);
        for(unsigned n=620U;n<=730U;n+=10U) { poll_at(at+n,0,0,true,false); }
        assert(submissions==before+1U && status.owns_left && refs[0]==1);
        complete();poll_at(at+740U,0,0,true,false);
        assert(!status.owns_left && refs[0]==0 && count==0);
        poll_at(at+1030U,0,0,true,false);
        assert(!auto_spacing.live && !native_have_activity && tp_microtap_runtime_enabled());
        assert(!guard_irq_depth && count<MICRO_QUEUE_CAPACITY);
    }
    /* A still-live guard at a half-range jump fails closed only for one local
     * guard window; a retired history must never revive on later jumps. */
    position_edge(1000,4,true);position_edge(1000,4,false);
    assert(native_guard_observe(false).blocked);
    fake_now=UINT32_C(0x800003e8);assert(native_guard_observe(false).blocked);
    fake_now+=599U;assert(native_guard_observe(false).blocked);
    fake_now+=1U;assert(!native_guard_observe(false).blocked);
    fake_now+=UINT32_C(0x80000000);assert(!native_guard_observe(false).blocked);
    assert(!status.owns_left && !refs[0] && !guard_irq_depth);
    printf("PASS stability microtap: 20000 uninterrupted native/auto DOWN-UP cycles across %llu simulated day jumps/u32 wraps, exact 599/600ms guard, 300ms spacing retirement, no held ref/FIFO leak or disabled mode; half-range guard recovers locally; NOT hardware-duration evidence\n",
           (unsigned long long)(elapsed/UINT64_C(86400000)));
    return 0;
}
