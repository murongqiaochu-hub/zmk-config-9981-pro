#include "test_env.h"
static bool fake_aa;
bool tp_aa_is_pressed(void) { return fake_aa; }
#include "../config/boards/arm/bbp9981/custom_driver/behavior_aa_sym_ctrl.c"

int main(void) {
    struct zmk_behavior_binding b={0};
    struct zmk_behavior_binding_event e={.layer=2,.position=40,.timestamp=100};
    assert(ref_behavior_0==&sym_ctrl_api);
    fake_layer=2;fake_aa=false;
    assert(sym_pressed(&b,e)==0 && fake_layer==0 && modifier_refs==0);
    assert(sym_released(&b,e)==0 && key_up_calls==0);
    /* aA held: layer survives and actual Ctrl event is sent exactly once. */
    fake_layer=2;fake_aa=true;
    assert(sym_pressed(&b,e)==0 && fake_layer==2 && modifier_refs==1);
    sym_pressed(&b,e);assert(key_down_calls==1);
    sym_released(&b,e);assert(modifier_refs==0 && key_up_calls==1);
    tp_sym_ctrl_release(200);assert(key_up_calls==1);
    /* aA first release, then SYM: never release another Ctrl owner. */
    modifier_refs=1; /* unrelated original Ctrl key */
    sym_pressed(&b,e);assert(modifier_refs==2);
    fake_aa=false;tp_sym_ctrl_release(300);assert(modifier_refs==1);
    sym_released(&b,e);assert(modifier_refs==1 && last_key_timestamp==300);
    assert(layer_to_calls==1);
    /* A new aA press alone must not reactivate a still-held SYM. */
    fake_aa=true;assert(!ctrl_owned && modifier_refs==1);
    key_event_error=-EIO;sym_pressed(&b,e);assert(!ctrl_owned && modifier_refs==1);
    key_event_error=0;sym_released(&b,e);assert(modifier_refs==1);
    puts("PASS Ctrl: sticky fallback, both release orders, duplicate release, independent Ctrl ownership, failure");
}
