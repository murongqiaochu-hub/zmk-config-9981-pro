#include "test_env.h"
#include "../config/boards/arm/bbp9981/custom_driver/keyboard_backlight.c"
static ssize_t read_value(void *arg,void *data,size_t len) {
    assert(len==1U);*(uint8_t *)data=*(uint8_t *)arg;return 1;
}
static ssize_t partial(void *arg,void *data,size_t len) {(void)arg;(void)data;(void)len;return 0;}
static ssize_t failure(void *arg,void *data,size_t len) {(void)arg;(void)data;(void)len;return -EIO;}
static void reboot_ram(void) {
    kb_mem_loaded=kb_mem_ready=kb_mem_dirty=false;
    kb_mem_value=kb_mem_observed=kb_mem_failures=0;
    fake_brightness=40;fake_rgb_on=true;fake_rgb_set_error=0;fake_flash_error=0;
    assert(keyboardbacklight_init()==0);
}
int main(void) {
    reboot_ram();fake_brightness=70;kb_mem_observe();assert(!kb_mem_dirty&&fake_flash_writes==0U);
    uint8_t saved=17;
    assert(kb_mem_set("bad",1,read_value,&saved)==-ENOENT);
    assert(kb_mem_set("brightness",2,read_value,&saved)==-EINVAL);
    assert(kb_mem_set("brightness",1,partial,&saved)==-EINVAL);
    assert(kb_mem_set("brightness",1,failure,&saved)==-EIO);
    uint8_t corrupt=101;assert(kb_mem_set("brightness",1,read_value,&corrupt)==-EINVAL);
    assert(kb_mem_set("brightness",1,read_value,&saved)==0);
    assert(kb_mem_commit()==0&&fake_brightness==17&&kb_mem_ready&&!kb_mem_dirty);
    fake_brightness=27;kb_mem_observe();assert(kb_mem_dirty&&kb_mem_work.delay==250);
    fake_brightness=37;kb_mem_observe();kb_mem_save(&kb_mem_work.work);
    assert(fake_flash_has_value&&fake_flash_value==37&&fake_flash_writes==1U&&!kb_mem_dirty);
    reboot_ram();assert(kb_mem_set("brightness",1,read_value,&fake_flash_value)==0);
    assert(kb_mem_commit()==0&&fake_brightness==37);
    unsigned writes=fake_flash_writes;
    fake_rgb_on=false;kb_mem_observe();
    fake_activity=ZMK_ACTIVITY_IDLE;polling_work_handler(&polling_work.work);
    for(unsigned i=0;i<1000U;i++){cycle_work_handler(&cycle_work.work);blink_work_handler(&blink_work.work);kb_mem_observe();}
    kb_mem_save(&kb_mem_work.work);assert(fake_flash_writes==writes&&!kb_mem_dirty);
    fake_brightness=0;kb_mem_observe();kb_mem_save(&kb_mem_work.work);assert(fake_flash_value==0);
    reboot_ram();assert(kb_mem_set("brightness",1,read_value,&fake_flash_value)==0);
    assert(kb_mem_commit()==0&&fake_brightness==0);
    fake_brightness=77;kb_mem_observe();fake_flash_error=-EIO;
    kb_mem_save(&kb_mem_work.work);kb_mem_save(&kb_mem_work.work);kb_mem_save(&kb_mem_work.work);
    assert(kb_mem_dirty&&kb_mem_failures==3U&&kb_mem_work.delay==1000);
    fake_flash_error=0;fake_brightness=67;kb_mem_observe();assert(kb_mem_failures==0);
    kb_mem_save(&kb_mem_work.work);assert(fake_flash_value==67&&!kb_mem_dirty);
    reboot_ram();assert(kb_mem_commit()==0&&kb_mem_dirty&&fake_brightness==40);
    kb_mem_save(&kb_mem_work.work);assert(fake_flash_value==40);
    puts("PASS keyboard durable brightness: post-load restore, zero, debounce/coalesce, simulated reboot, invalid/partial record, bounded write retry; no frame/idle/B-off writes");
    return 0;
}
