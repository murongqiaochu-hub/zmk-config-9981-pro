/* Actual driver listener + behavior: physical pair, not a fake Ctrl injection. */
#define main legacy_combo_suite
#include "test_combo.c"
#undef main
static void reset_pair(bool external) {
    memset(&scroll_keys,0,sizeof(scroll_keys));scroll_mode_snapshot=0;
    sym_physically_pressed=false;ctrl_owned=false;fake_layer=0;
    modifier_refs=external?1:0;key_down_calls=key_up_calls=0;key_event_error=0;
}
int main(void) {
    assert(legacy_combo_suite()==0);
    unsigned cases=0;
    for(unsigned first=0;first<2U;first++)for(unsigned release_first=0;release_first<2U;release_first++)
    for(unsigned external=0;external<2U;external++)for(unsigned binding_first=0;binding_first<2U;binding_first++)
    for(unsigned gap=0;gap<=500U;gap++) {
        reset_pair(external!=0U);int baseline=modifier_refs;
        int64_t t=1000+(int64_t)gap;
        struct zmk_behavior_binding b={0};
        struct zmk_behavior_binding_event e={.layer=2,.position=40,.timestamp=t};
        if(first==0U) {
            key(41,true,1000);fake_layer=2;
            if(binding_first!=0U)(void)sym_pressed(&b,e);
            key(40,true,t);
            if(binding_first==0U)(void)sym_pressed(&b,e);
        } else {
            /* SYM was bound to stock sl_mo before aA: no sym_pressed callback. */
            key(40,true,1000);fake_layer=1;key(41,true,t);fake_layer=2;
        }
        assert(ctrl_owned&&modifier_refs==baseline+1&&key_down_calls==1);
        assert(scroll_keys.interrupted&&!scroll_keys.double_candidate&&!scroll_keys.tap_pending);
        key(40,true,t);key(41,true,t);assert(key_down_calls==1&&modifier_refs==baseline+1);
        uint32_t first_up=release_first==0U?40U:41U;
        key(first_up,false,t+10);assert(!ctrl_owned&&modifier_refs==baseline&&key_up_calls==1);
        key(first_up,false,t+10);assert(key_up_calls==1);
        key(first_up==40U?41U:40U,false,t+20);
        (void)sym_released(&b,e);
        assert(!ctrl_owned&&modifier_refs==baseline&&key_up_calls==1);
        assert(!scroll_keys.latched&&!scroll_keys.tap_pending);
        cases++;
    }
    reset_pair(false);key(40,true,0);fake_layer=1;
    for(unsigned i=0;i<1000U;i++) {
        int64_t t=100+(int64_t)i*60;
        key(41,true,t);assert(ctrl_owned&&modifier_refs==1);
        key(41,false,t+20);assert(!ctrl_owned&&modifier_refs==0);
        assert(!scroll_keys.latched&&!scroll_keys.tap_pending);
    }
    key(40,false,60100);assert(key_down_calls==1000&&key_up_calls==1000);
    reset_pair(false);key(41,true,0);fake_layer=2;
    for(unsigned i=0;i<1000U;i++) {
        int64_t t=100+(int64_t)i*60;
        key(40,true,t);assert(ctrl_owned&&modifier_refs==1);
        key(40,false,t+20);assert(!ctrl_owned&&modifier_refs==0);
    }
    key(41,false,60100);assert(!scroll_keys.latched&&!scroll_keys.tap_pending);
    assert(key_down_calls==1000&&key_up_calls==1000);
    printf("PASS symmetric Ctrl: %u order/gap/release/foreign-owner/binding-order cases; 1000 aA re-press + 1000 SYM re-press; no orphan Ctrl or scroll-lock toggles\n",cases);
    return 0;
}
