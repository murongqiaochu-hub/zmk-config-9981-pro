/* Reuse all original test adapters, then replay identical keyed/route timelines. */
#define main existing_gamepad_regression_main
#include FW_GAMEPAD_TEST
#undef main
static uint32_t rng=0x99814455U;
static uint64_t digest=UINT64_C(1469598103934665603);
static uint32_t random_u32(void) { rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng; }
static void fold(uint32_t v) { digest^=v;digest*=UINT64_C(1099511628211); }
int main(void) {
    for(unsigned transport=0;transport<2U;transport++) {
        fixture(transport!=0);
        for(unsigned i=0;i<60000U;i++) {
            uint32_t value=random_u32();now_ms+=(int64_t)(value&31U);
            switch(value%15U) {
            case 0: case 1: case 2:
                edge((value>>8)%42U,(value>>16)%BBP_GAMEPAD_ACTION_COUNT,(value&128U)!=0);break;
            case 3:
                bbp_gamepad_trackpad_poll((int8_t)(value>>8),(int8_t)(value>>16),
                                           value%29U!=0,now_ms-(int64_t)((value>>24)&127U));break;
            case 4: run(1,0);break;
            case 5: forced_send_error=(value&256U)?-EAGAIN:0;break;
            case 6: highest_layer=(value&128U)?3U:0U;context_changed(NULL);break;
            case 7:
                selected.transport=(value&128U)?ZMK_TRANSPORT_BLE:ZMK_TRANSPORT_USB;
                selected.ble.profile_index=(value&256U)?1U:0U;
                active_peer=(value&256U)?&peer_b:&peer_a;context_changed(NULL);break;
            case 8: usb_epoch++;usb_ready=(value&128U)!=0;bbp_gamepad_transport_invalidate();break;
            case 9: peer_a.connected=(value&128U)!=0;peer_b.connected=(value&256U)!=0;break;
            case 10: peer_a.subscribed=(value&128U)!=0;peer_b.subscribed=(value&256U)!=0;break;
            case 11: peer_a.secure=(value&128U)!=0;peer_b.secure=(value&256U)!=0;break;
            case 12: gp_security_changed(active_peer,BT_SECURITY_L2,BT_SECURITY_ERR_SUCCESS);break;
            case 13: run(1,400);break;
            case 14: {
                struct bbp_gamepad_report_body body;
                bbp_gamepad_get_report((uint8_t)((value>>8)&1U),&body);
                const uint8_t *raw=(const uint8_t*)&body;
                for(unsigned n=0;n<sizeof(body);n++) { fold(raw[n]); }
                break;
            }
            }
            fold(accepted_count);
            for(unsigned n=0;n<accepted_count;n++) {
                fold(accepted[n].transport);fold((uint32_t)accepted[n].peer);fold(accepted[n].context);
                const uint8_t *raw=(const uint8_t*)&accepted[n].body;
                for(unsigned j=0;j<sizeof(accepted[n].body);j++) { fold(raw[j]); }
            }
            accepted_count=0;
            fold((uint32_t)peer_a.refs);fold((uint32_t)peer_b.refs);fold(security_requests);
            fold(schedules);fold(scheduled_delay);fold(work_pending);
            fold(bbp_gamepad_context_epoch());fold(bbp_gamepad_pointer_epoch());fold(tap_bridge_cancels);
            fold(count);fold(pending_valid);fold(last_valid);fold(releasing);
            if(i%1000U==999U) { printf("%u %u %016llx\n",transport,i,(unsigned long long)digest); }
        }
        no_leaks();
    }
    return 0;
}
