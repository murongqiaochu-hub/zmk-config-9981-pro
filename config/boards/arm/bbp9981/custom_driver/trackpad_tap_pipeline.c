/* Real driver -> verified-contact adapter -> tap FSM -> actual output bridge.
 * The current 9380 contact contract is unknown. NEVER enable on motion guesses.
 * SPDX-License-Identifier: MIT */
#include <errno.h>
#include "trackpad_tap_pipeline.h"
#include "trackpad_tap_output.h"
extern int bbp9981_hid_mouse_button_references(unsigned int button) __attribute__((weak));
__attribute__((weak)) struct tp_tap_verified_contact tp_tap_contact_provider_read(uint32_t now) {
    return (struct tp_tap_verified_contact){.contact=TP_TAP_UNKNOWN,.observed_at=now};
}
static struct tp_tap_fsm tap;
static struct tp_tap_config limits;
static struct tp_tap_result result;
static bool started,have_epochs;
static uint32_t last_mode,last_source,epoch=1;
void tp_tap_pipeline_init(void) {
    if (started) { return; }
    started=true;tp_tap_reset(&tap);limits=tp_tap_default_config();
    result=tp_tap_snapshot(&tap,false);
    (void)tp_tap_output_init();
}
void tp_tap_pipeline_poll(uint32_t now,int32_t dx,int32_t dy,bool motion_fault,
                          bool scroll,uint32_t mode_epoch) {
    if (!started) { return; }
    struct tp_tap_verified_contact contact=tp_tap_contact_provider_read(now);
    struct tp_tap_output_inflight out=tp_tap_output_read_inflight();
    int refs=bbp9981_hid_mouse_button_references ? bbp9981_hid_mouse_button_references(0) : -ENOTSUP;
    bool left_known=refs>=0 && refs>=(out.owns_left?1:0);
    bool foreign_left=left_known && refs>(out.owns_left?1:0);
    bool changed=have_epochs && (last_mode!=mode_epoch || last_source!=contact.source_epoch);
    if (changed) { epoch++;tp_tap_reset(&tap);(void)tp_tap_output_cancel(); }
    have_epochs=true;last_mode=mode_epoch;last_source=contact.source_epoch;
    bool credible=contact.protocol_verified && contact.valid &&
                  (contact.contact==TP_TAP_UP || contact.contact==TP_TAP_DOWN) &&
                  contact.observed_at==now; /* Cached status is not a new debounce sample. */
    bool enabled=contact.protocol_verified && left_known && !motion_fault;
    limits.enabled=enabled;
    if (!credible || motion_fault) { epoch++; }
    struct tp_tap_output_context context={.epoch=epoch,.enabled=enabled&&credible&&!scroll,
        .physical_left_known=left_known,.physical_left_down=foreign_left};
    (void)tp_tap_output_update_context(context);
    result=tp_tap_step(&tap,&limits,(struct tp_tap_input){.t_ms=now,.valid=credible&&!motion_fault,
        .contact=credible?contact.contact:TP_TAP_UNKNOWN,.dx=dx,.dy=dy,
        .suppress=scroll||foreign_left||!left_known||changed});
    if (result.click_ready && context.enabled) {
        if (tp_tap_output_submit()!=0) {
            tp_tap_reset(&tap);
            result=tp_tap_snapshot(&tap,false);
        }
    }
}
struct tp_tap_result tp_tap_pipeline_status(void) { return result; }
