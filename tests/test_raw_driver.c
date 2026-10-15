/* Actual driver + runtime + logging adapter, no real hardware or USB. */
#define CONFIG_A320_RAW_CAPTURE 1
#define main old_micro_main
#define fixture runtime_fixture
#include "test_micro_runtime.c"
#undef fixture
#undef main
#define CONFIG_A320_MICROTAP 1
#define CONFIG_A320_TAP_OUTPUT 1
#include <stdarg.h>
static char logs[128][512];static unsigned log_count,writes,bursts;
static uint32_t test_dtr;static bool usb_ready=true;static int uart_error,write_error,read_error;
static uint8_t raw_bytes[7]={255,255,255,255,1,36,154};
static void capture_info(const char *format,...) {
    assert(log_count<128U);va_list ap;va_start(ap,format);
    int len=vsnprintf(logs[log_count++],512,format,ap);va_end(ap);assert(len>0&&len<512);
}
#undef LOG_INF
#define LOG_INF(...) capture_info(__VA_ARGS__)
#define UART_LINE_CTRL_DTR 1
static bool zmk_usb_is_hid_ready(void) { return usb_ready; }
static int uart_line_ctrl_get(const struct device *d,int ctrl,uint32_t *out) {
    (void)d;assert(ctrl==1);*out=test_dtr;return uart_error;
}
static int raw_write(const struct i2c_dt_spec *s,const void *buf,size_t size) {
    (void)s;assert(size==1&&*(const uint8_t *)buf==0x0a);writes++;return write_error;
}
static int raw_burst(const struct i2c_dt_spec *s,int reg,uint8_t *buf,size_t size) {
    (void)s;assert(reg==0x0a&&size==7);bursts++;
    if(read_error)return read_error;
    memcpy(buf,raw_bytes,7);return 0;
}
void tp_microtap_shoulder_motion(uint32_t t) { (void)t; }
int tp_sym_ctrl_release(int64_t t) { (void)t;return 0; }
#define i2c_write_dt raw_write
#define i2c_burst_read_dt raw_burst
#include "../config/boards/arm/bbp9981/custom_driver/a320_0x57.c"
#undef i2c_write_dt
#undef i2c_burst_read_dt
static struct a320_dev_config cfg={.i2c={.bus=&fake_device}};
static struct a320_data data;
static struct device sensor={.name="raw-test",.config=&cfg,.data=&data};
static void step(uint32_t t,int irq) {
    fake_now=t;fake_gpio_state=irq;a320_poll_work_handler(&data.poll_work.work);
}
static bool logged(const char *text) {
    for(unsigned i=0;i<log_count;i++)if(strstr(logs[i],text))return true;
    return false;
}
int main(void) {
    runtime_fixture(false);data.dev=&sensor;tp_raw_capture_reset(&raw_capture);
    step(0,1);assert(writes==0&&bursts==0&&log_count==0);
    step(10,0);assert(writes==1&&bursts==1&&log_count==0&&raw_capture.seq==1);
    test_dtr=1;step(20,1);assert(logged("TPRAW_BEGIN proto=1")&&logged("first_seq=2"));
    assert(writes==1&&bursts==1);step(30,0);
    assert(writes==2&&bursts==2&&logged("seq=2 t_ms=30 read=ok"));
    assert(logged("raw=ff,ff,ff,ff,01,24,9a obtained=7 dx=-1 dy=-1 scroll=0 err=0"));
    write_error=-5;step(40,0);assert(writes==3&&bursts==2);
    assert(logged("read=error stage=pointer raw=none obtained=0 dx=na dy=na scroll=0 err=-5"));
    write_error=0;read_error=-6;step(50,0);assert(writes==4&&bursts==3);
    assert(logged("read=error stage=burst raw=none"));read_error=0;
    step(60,-5);assert(writes==4&&bursts==3&&logged("irq=-5 gpio_err=-5"));
    test_dtr=0;step(70,1);assert(logged("reason=dtr_closed first_seq=2 last_seq=4 attempts=3 ok=1 errors=2"));
    test_dtr=1;step(80,1);assert(raw_capture.session==2&&raw_capture.first_seq==5);
    tp_microtap_runtime_toggle();atomic_set(&enabled,1);assert(!tp_microtap_runtime_enabled());
    for(uint32_t t=90;t<190;t+=10)step(t,t==90?0:1);
    assert(submissions==0&&!status.owns_left&&!tp_microtap_runtime_enabled());
    usb_ready=false;step(200,1);assert(logged("reason=usb_offline"));
    if(getenv("TPRAW_EMIT_FIXTURE"))for(unsigned i=0;i<log_count;i++)puts(logs[i]);
    else puts("PASS actual raw driver: no added I2C, exact7byte success/error frames, first sample, session END, GPIO not frames, runtime toggle/state cannot enable click");
    return 0;
}
