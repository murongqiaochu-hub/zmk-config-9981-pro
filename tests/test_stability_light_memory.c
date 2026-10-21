/* Serialized workqueue model for sustained raw-preference persistence. This
 * tests retry/write invariants, not flash wear, power-loss NVS or real hardware. */
#define CONFIG_SETTINGS 1
#define main existing_light_memory_suite
#if STABILITY_TP
#include "test_trackpad_light_memory.c"
#else
#include "test_keyboard_light_memory.c"
#endif
#undef main

#if STABILITY_TP
#define mem_work tp_mem_work
#define mem_save tp_mem_save
#define mem_observe tp_mem_observe
#define mem_dirty tp_mem_dirty
#define mem_failures tp_mem_failures
#define flash_writes fake_tp_flash_writes
#define flash_error fake_tp_flash_error
#define flash_value fake_tp_flash_value
#define raw_value fake_tp_brightness
#else
#define mem_work kb_mem_work
#define mem_save kb_mem_save
#define mem_observe kb_mem_observe
#define mem_dirty kb_mem_dirty
#define mem_failures kb_mem_failures
#define flash_writes fake_flash_writes
#define flash_error fake_flash_error
#define flash_value fake_flash_value
#define raw_value fake_brightness
#endif
static unsigned dispatch_saves(void) {
    unsigned ran=0;
    while(mem_work.pending) {
        assert(ran<3U);
        assert(mem_work.delay==250||mem_work.delay==1000);
        fake_now+=(uint32_t)mem_work.delay;
        mem_work.pending=false;mem_save(&mem_work.work);ran++;
    }
    return ran;
}
static void boot_saved(void) {
    uint8_t saved=flash_value;
#if STABILITY_TP
    reboot(false);
    assert(tp_mem_set("brightness",1,read_value,&saved)==0);
    assert(tp_mem_commit()==0 && tp_mem_ready && !fake_tp_on);
#else
    reboot_ram();
    assert(kb_mem_set("brightness",1,read_value,&saved)==0);
    assert(kb_mem_commit()==0 && kb_mem_ready);
#endif
    assert(raw_value==saved && !mem_dirty && !mem_work.pending);
}
int main(void) {
    uint8_t other=73;
#if STABILITY_TP
    reboot(true);assert(tp_mem_commit()==0);
    assert(settings_save_one("bbp9981/kb/brightness",&other,1)==0);
#else
    reboot_ram();assert(kb_mem_commit()==0);
    assert(settings_save_one("bbp9981/tp/brightness",&other,1)==0);
#endif
    (void)dispatch_saves();
    unsigned failures=0,reboots=0;
    for(unsigned i=0;i<10000U;i++) {
        uint8_t desired=(uint8_t)((i*17U)%101U);
        raw_value=desired;mem_observe();
        bool changed=mem_dirty;
        unsigned before=flash_writes;
        if(i%97U==0 && changed) {
            flash_error=-EIO;assert(dispatch_saves()==3U);
            assert(mem_dirty && mem_failures==3U && flash_writes==before+3U);
            for(unsigned n=0;n<100U;n++) { mem_observe();assert(!mem_work.pending); }
            assert(flash_writes==before+3U);failures++;
            /* Only a new user preference renews the documented retry budget. */
            flash_error=0;desired=(uint8_t)((desired+1U)%101U);raw_value=desired;mem_observe();
            assert(mem_failures==0 && dispatch_saves()==1U);
        } else {
            flash_error=0;assert(dispatch_saves()==(changed?1U:0U));
        }
        assert(flash_value==desired && !mem_dirty && !mem_work.pending);
        before=flash_writes;
        for(unsigned n=0;n<200U;n++) {
            fake_now+=20;fake_activity=(n&1U)?ZMK_ACTIVITY_IDLE:ZMK_ACTIVITY_ACTIVE;
#if STABILITY_TP
            fake_tp_on=(n&2U)!=0;fake_transport=(n&4U)?ZMK_TRANSPORT_USB:ZMK_TRANSPORT_BLE;
            fake_scroll=(n&8U)!=0;fake_touch=(n&16U)!=0;
            polling_work_handler(&polling_work.work);
            usb_breath_work_handler(&usb_breath_work.work);
            animation_work_handler(&animation_work.work);
            assert(fake_flash_value==other && fake_flash_writes==1U);
#else
            fake_rgb_on=(n&2U)!=0;fake_layer=(int)(n%4U);
            polling_work_handler(&polling_work.work);cycle_work_handler(&cycle_work.work);
            blink_work_handler(&blink_work.work);
            assert(fake_tp_flash_value==other && fake_tp_flash_writes==1U);
#endif
            assert(!mem_dirty && !mem_work.pending && raw_value==desired && flash_writes==before);
        }
        if(i%250U==249U) { boot_saved();reboots++; }
    }
    printf("PASS stability %s memory: 10000 user updates, 2000000 idle/animation polls, %u exhausted-three-write/rearmed cycles and %u simulated RAM reboots; no frame writes, unchanged raw records or cross-key corruption; NOT flash wear/power-cut testing\n",
           STABILITY_TP?"TP":"KB",failures,reboots);
    return 0;
}
