#include "test_env.h"
#define CONFIG_A320_CONTACT_PROBE 1
#define CONFIG_A320_CONTACT_PROBE_INTERVAL_MS 20
#define UART_LINE_CTRL_DTR 1
static unsigned register_reads;
static uint8_t probe_registers[256];
static uint32_t test_dtr;
static int test_uart_error;
static int uart_line_ctrl_get(const struct device *dev,int ctrl,uint32_t *out) {
    (void)dev;assert(ctrl==UART_LINE_CTRL_DTR);*out=test_dtr;return test_uart_error;
}
static int i2c_reg_read_byte_dt(const struct i2c_dt_spec *bus,uint8_t reg,uint8_t *out) {
    (void)bus;register_reads++;*out=probe_registers[reg];return 0;
}
#define main baseline_main
#include "test_pointer.c"
#undef main
int main(void) {
    test_dtr=0;baseline_main();assert(register_reads==0); /* Entire pointer suite with diagnostics compiled ON. */
    fixture();probe_registers[0]=0x83;probe_registers[1]=1;
    probe_registers[0x60]=0x84;probe_registers[0x75]=0x50;
    idle(1000);assert(register_reads==0&&!probe_open);
    test_dtr=1;idle(1010);assert(register_reads==8&&probe_open&&probe_session==1);
    assert(contact_probe.checked&&contact_probe.matched&&legacy_seq==0);
    idle(1020);assert(register_reads==8);idle(1030);assert(register_reads==14);
    poll(4,-2,1040);expect_xy(4,-2);assert(legacy_seq==1&&legacy_bytes[3]==4&&legacy_bytes[1]==(uint8_t)-2);
    test_dtr=0;idle(1050);assert(register_reads==14&&!probe_open&&!contact_probe.checked);
    test_dtr=1;idle(1060);assert(register_reads==22&&probe_session==2&&legacy_seq==0);
    test_uart_error=-5;idle(1070);assert(register_reads==22&&!probe_open&&!contact_probe.checked);
    test_uart_error=0;idle(UINT32_MAX-10);assert(register_reads==30&&probe_session==3);
    idle(9);assert(register_reads==36); /* 20ms across rollover; no forced re-identification */
    puts("PASS Diagnostic runtime: source68 pointer suite unchanged with probe ON, no reads when DTR closed/error, independent idle polling, 20ms cadence, raw frame snapshot, reopen and rollover; zero click generation");
    return 0;
}
