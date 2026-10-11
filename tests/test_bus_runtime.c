#include "test_env.h"
#define CONFIG_A320_CONTACT_PROBE 1
#define CONFIG_A320_CONTACT_PROBE_INTERVAL_MS 100
#define CONFIG_A320_BUS_COMPARE 1
#define CONFIG_A320_BUS_COMPARE_INTERVAL_MS 500
#define UART_LINE_CTRL_DTR 1
static uint32_t test_dtr;
static bool test_usb_ready=true;
static bool zmk_usb_is_hid_ready(void){return test_usb_ready;}
static int uart_line_ctrl_get(const struct device *d,int ctrl,uint32_t *v){(void)d;assert(ctrl==1);*v=test_dtr;return 0;}
static char bus_trace[8];static unsigned bus_calls;
static void note(char c){assert(bus_calls<sizeof(bus_trace));bus_trace[bus_calls++]=c;}
static int compare_write(const struct i2c_dt_spec *s,const void *b,size_t n){
    assert(n==1);uint8_t reg=*(const uint8_t *)b;assert(reg==0||reg==0x0a);
    if(reg==0) { note('P'); }
    return i2c_write_dt(s,b,n);
}
static int i2c_read_dt(const struct i2c_dt_spec *s,uint8_t *out,size_t n){(void)s;assert(n==1);note('R');*out=0x83;return 0;}
static int i2c_reg_read_byte_dt(const struct i2c_dt_spec *s,uint8_t reg,uint8_t *out){(void)s;assert(reg==0);note('C');*out=0x0c;return 0;}
#define i2c_write_dt compare_write
#define main baseline_main
#include "test_pointer.c"
#undef main
#undef i2c_write_dt
static void expect(const char *s){assert(bus_calls==5);assert(memcmp(bus_trace,s,5)==0);bus_calls=0;}
int main(void){
    test_dtr=0;baseline_main();assert(bus_calls==0);
    fixture();idle(1000);assert(bus_calls==0);
    test_dtr=1;idle(1010);expect("CPRPC");assert(probe_open&&bus_compare_round==1);
    idle(1110);assert(bus_calls==0); /* Slow compare; no standard profile reads. */
    idle(1510);expect("PRPCC");idle(2010);expect("PCCPR");
    poll(4,-2,2110);expect_xy(4,-2);assert(bus_calls==0&&legacy_seq==1);
    test_dtr=0;idle(2210);assert(!probe_open&&bus_calls==0);
    test_dtr=1;idle(2310);expect("CPRPC");assert(bus_compare_round==1);
    test_usb_ready=false;idle(2810);assert(!probe_open&&bus_calls==0);
    test_usb_ready=true;idle(UINT32_MAX-400);expect("CPRPC");
    idle(99);expect("PRPCC"); /* 500ms across unsigned uptime wrap. */
    puts("PASS Bus compare runtime: original pointer suite with compare ON; only reg00 address pointer writes, rotated combined/STOP/direct/legacy order, 500ms cadence, DTR/USB close, raw movement preserved and rollover; no contacts decoded or clicks");
    return 0;
}
