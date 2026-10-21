/* Old/new ordered PWM, scheduling and settings trace. Host models only. */
#ifndef CONFIG_SETTINGS
#define CONFIG_SETTINGS 1
#endif
#define TEST_NATIVE_BACKLIGHT 1
#include "test_env.h"
#include <stdarg.h>
static uint64_t digest=UINT64_C(1469598103934665603);
static uint32_t rng=0x99810011U;
static void fold(uint32_t v) { digest^=v; digest*=UINT64_C(1099511628211); }
static uint32_t random_u32(void) { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }
static const void *work_ids[16];
static unsigned work_count;
static unsigned work_id(const void *work) {
    for(unsigned i=0;i<work_count;i++) { if(work_ids[i]==work) { return i; } }
    assert(work_count<16);work_ids[work_count]=work;return work_count++;
}
static bool trace_ready(const struct pwm_dt_spec *p) {
    bool result=pwm_is_ready_dt(p);fold(1);fold(p->channel);fold(result);return result;
}
static int trace_write(const struct pwm_dt_spec *p,uint32_t period,uint32_t pulse) {
    int result=pwm_set_dt(p,period,pulse);
    fold(2);fold(p->channel);fold(period);fold(pulse);fold(p->flags);fold((uint32_t)result);return result;
}
static int trace_reschedule(struct k_work_delayable *work,int delay) {
    fold(3);fold(work_id(work));fold((uint32_t)delay);return k_work_reschedule(work,delay);
}
static int trace_cancel(struct k_work_delayable *work) {
    fold(4);fold(work_id(work));return k_work_cancel_delayable(work);
}
static int trace_save(const char *name,const void *data,size_t size) {
    fold(5);for(size_t i=0;name[i];i++) { fold((unsigned char)name[i]); }
    fold((uint32_t)size);for(size_t i=0;i<size;i++) { fold(((const uint8_t*)data)[i]); }
    int result=settings_save_one(name,data,size);fold((uint32_t)result);return result;
}
/* Preserve observable log level/body/order; internal __func__/line metadata is
 * intentionally not part of the user-visible behavior contract. */
static void trace_log(unsigned level,const char *format,...) {
    char body[1024];va_list arguments;va_start(arguments,format);
    int size=vsnprintf(body,sizeof(body),format,arguments);va_end(arguments);
    assert(size>=0&&(size_t)size<sizeof(body));fold(6);fold(level);
    for(int i=0;i<size;i++) { fold((unsigned char)body[i]); }
}
#undef LOG_ERR
#undef LOG_DBG
#undef LOG_INF
#define LOG_ERR(...) trace_log(1,__VA_ARGS__)
#define LOG_DBG(...) trace_log(2,__VA_ARGS__)
#define LOG_INF(...) trace_log(3,__VA_ARGS__)
#define pwm_is_ready_dt trace_ready
#define pwm_set_dt trace_write
#define k_work_reschedule trace_reschedule
#define k_work_cancel_delayable trace_cancel
#define settings_save_one trace_save
#if TRACE_TP
static bool fake_scroll,fake_touch;
bool tp_scroll_mode_active(void) { return fake_scroll; }
bool tp_is_touched(void) { return fake_touch; }
#endif
#include FW_LIGHT_SOURCE
static ssize_t read_value(void *ctx,void *data,size_t n) {
    assert(n==1);*(uint8_t*)data=*(uint8_t*)ctx;return 1;
}
int main(void) {
    fake_now=UINT32_MAX-2000U;
#if TRACE_TP
    fold((uint32_t)indicator_tp_init());
#else
    fold((uint32_t)keyboardbacklight_init());
#endif
    uint8_t saved=17;
#if TRACE_TP && CONFIG_SETTINGS
    fold((uint32_t)tp_mem_set("brightness",1,read_value,&saved));fold((uint32_t)tp_mem_commit());
#elif !TRACE_TP
    fold((uint32_t)kb_mem_set("brightness",1,read_value,&saved));fold((uint32_t)kb_mem_commit());
#endif
    (void)saved;
    for(unsigned i=0;i<80000U;i++) {
        uint32_t value=random_u32();fake_now+=(value&63U);
        fake_pwm_ready=value%113U!=0;fake_led_error=value%71U==0?-EIO:0;
        fake_led_error_index=(int)((value>>7)%3U)-1;
        fake_flash_error=value%61U==0?-EIO:0;fake_tp_flash_error=fake_flash_error;
        fake_activity=(value&256U)?ZMK_ACTIVITY_ACTIVE:ZMK_ACTIVITY_IDLE;
        switch(value%9U) {
        case 0:
            fake_brightness=(uint8_t)(value>>16);fake_tp_brightness=(uint8_t)(value>>24);
            fake_rgb_on=(value&512U)!=0;fake_tp_on=(value&1024U)!=0;
            break;
        case 1: fake_transport=(value&128U)?ZMK_TRANSPORT_USB:ZMK_TRANSPORT_BLE;break;
        case 2: polling_work_handler(&polling_work.work);break;
#if TRACE_TP
        case 3: animation_work_handler(&animation_work.work);break;
        case 4: usb_breath_work_handler(&usb_breath_work.work);break;
        case 5: auto_off_work_handler(&auto_off_work.work);break;
        case 6: fake_scroll=(value&128U)!=0;fake_touch=(value&256U)!=0;break;
#if CONFIG_SETTINGS
        case 7: tp_mem_observe();break;
        case 8: tp_mem_save(&tp_mem_work.work);break;
#endif
#else
        case 3: cycle_work_handler(&cycle_work.work);break;
        case 4: blink_work_handler(&blink_work.work);break;
        case 5: {
            zmk_event_t event={.valid=true,.data={.position=41,.state=(value&128U)!=0}};
            fold((uint32_t)key_listener_cb(&event));break;
        }
        case 6: fake_layer=(int)((value>>8)%4U);break;
        case 7: kb_mem_observe();break;
        case 8: kb_mem_save(&kb_mem_work.work);break;
#endif
        default: break;
        }
        fold((uint32_t)last_led_level);fold(requested_pattern);fold(fake_pwm_writes);
        fold(fake_pwm_pulses[0]);fold(fake_pwm_pulses[1]);
        fold(fake_flash_writes);fold(fake_tp_flash_writes);
        fold(fake_flash_value);fold(fake_tp_flash_value);
        if(i%1000U==999U) { printf("%u %016llx\n",i,(unsigned long long)digest); }
    }
    return 0;
}
