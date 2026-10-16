#define CONFIG_SETTINGS 1
#define TEST_NATIVE_BACKLIGHT 1
#include "test_env.h"
static bool fake_scroll,fake_touch;
bool tp_scroll_mode_active(void){return fake_scroll;}
bool tp_is_touched(void){return fake_touch;}
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_led.c"
static ssize_t read_value(void *a,void *d,size_t n){assert(n==1);*(uint8_t *)d=*(uint8_t *)a;return 1;}
static ssize_t partial(void *a,void *d,size_t n){(void)a;(void)d;(void)n;return 0;}
static ssize_t fail_read(void *a,void *d,size_t n){(void)a;(void)d;(void)n;return -EIO;}
static void reboot(bool on){
 tp_mem_initialized=tp_mem_driver_ready=tp_mem_loaded=tp_mem_ready=tp_mem_dirty=false;
 tp_mem_value=tp_mem_observed=tp_mem_failures=0;
 fake_tp_brightness=40;fake_tp_on=on;fake_tp_set_error=0;fake_pwm_ready=true;
 fake_transport=ZMK_TRANSPORT_USB;fake_activity=ZMK_ACTIVITY_ACTIVE;fake_scroll=fake_touch=false;
 assert(indicator_tp_init()==0&&!polling_work.pending&&!tp_mem_ready);
}
int main(void){
 reboot(true);unsigned writes=fake_tp_flash_writes;
 polling_work_handler(&polling_work.work);assert(fake_tp_flash_writes==writes&&!usb_mode);
 uint8_t saved=17,bad=101;
 assert(tp_mem_set("bad",1,read_value,&saved)==-ENOENT);
 assert(tp_mem_set("brightness",2,read_value,&saved)==-EINVAL);
 assert(tp_mem_set("brightness",1,partial,&saved)==-EINVAL);
 assert(tp_mem_set("brightness",1,fail_read,&saved)==-EIO);
 assert(tp_mem_set("brightness",1,read_value,&bad)==-EINVAL);
 assert(tp_mem_set("brightness",1,read_value,&saved)==0);
 assert(tp_mem_commit()==0&&tp_mem_ready&&fake_tp_brightness==17&&last_valid_brt==17);
 fake_tp_brightness=27;polling_work_handler(&polling_work.work);
 fake_tp_brightness=37;polling_work_handler(&polling_work.work);
 assert(tp_mem_dirty&&tp_mem_work.delay==250);tp_mem_save(&tp_mem_work.work);
 assert(fake_tp_flash_has_value&&fake_tp_flash_value==37&&fake_tp_flash_writes==writes+1);
 reboot(false);assert(tp_mem_set("brightness",1,read_value,&fake_tp_flash_value)==0);
 assert(tp_mem_commit()==0&&fake_tp_brightness==37&&!fake_tp_on&&last_valid_brt==37);
 writes=fake_tp_flash_writes;
 fake_activity=ZMK_ACTIVITY_IDLE;fake_tp_on=false;
 for(unsigned i=0;i<1000;i++){
  fake_now=i*20U;polling_work_handler(&polling_work.work);usb_breath_work_handler(&usb_breath_work.work);
  assert(last_valid_brt==37&&tp_mem_value==37&&!tp_mem_dirty);
 }
 tp_mem_save(&tp_mem_work.work);assert(fake_tp_flash_writes==writes);
 fake_tp_brightness=0;polling_work_handler(&polling_work.work);tp_mem_save(&tp_mem_work.work);
 assert(fake_tp_flash_value==0&&last_valid_brt==0&&fake_pwm_pulse_ns==0);
 reboot(true);assert(tp_mem_set("brightness",1,read_value,&fake_tp_flash_value)==0);
 assert(tp_mem_commit()==0&&fake_tp_brightness==0&&!fake_tp_on&&last_valid_brt==0);
 uint8_t kb=73;assert(settings_save_one("bbp9981/kb/brightness",&kb,1)==0);
 fake_tp_brightness=77;tp_mem_observe();fake_tp_flash_error=-EIO;
 tp_mem_save(&tp_mem_work.work);tp_mem_save(&tp_mem_work.work);tp_mem_save(&tp_mem_work.work);
 writes=fake_tp_flash_writes;tp_mem_save(&tp_mem_work.work);
 assert(tp_mem_failures==3&&tp_mem_dirty&&fake_tp_flash_writes==writes&&fake_flash_value==73);
 fake_tp_flash_error=0;fake_tp_brightness=67;tp_mem_observe();tp_mem_save(&tp_mem_work.work);
 assert(fake_tp_flash_value==67&&!tp_mem_dirty&&fake_flash_value==73);
 reboot(true);assert(tp_mem_commit()==0&&tp_mem_dirty);tp_mem_save(&tp_mem_work.work);assert(fake_tp_flash_value==40);
 reboot(true);assert(tp_mem_set("brightness",1,read_value,&saved)==0);
 fake_tp_set_error=-EIO;assert(tp_mem_commit()==-EIO&&!tp_mem_ready);fake_tp_set_error=0;
 assert(tp_mem_commit()==0&&last_valid_brt==17);
 tp_mem_driver_ready=false;fake_pwm_ready=false;assert(indicator_tp_init()==-ENODEV);
 assert(tp_mem_initialized&&!tp_mem_driver_ready&&tp_mem_commit()==-ENODEV);
 puts("PASS TP memory: raw/effective distinction, pre-load gate, 250ms coalesce, simulated reboot/on/off/zero, 1000 idle animation frames no flash, invalid reads, bounded retries, KB isolation, failed native/PWM init");
 return 0;
}
