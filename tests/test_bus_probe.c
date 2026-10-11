#include <assert.h>
#include <stdio.h>
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_bus_probe.h"
static char trace[3];static unsigned calls;static int fault;
static int operation(char name,uint8_t reg,uint8_t *out){
    assert(reg==0);assert(calls<sizeof(trace));trace[calls++]=name;
    if(out)*out=name=='C'?0x0c:0x83;
    return (int)calls==fault?-5:0;
}
static int p(void *ctx,uint8_t reg){(void)ctx;return operation('P',reg,0);}
static int r(void *ctx,uint8_t *out){(void)ctx;return operation('R',0,out);}
static int c(void *ctx,uint8_t reg,uint8_t *out){(void)ctx;return operation('C',reg,out);}
int main(void){
    const struct tp_bus_ops ops={p,r,c};
    for(int m=0;m<TP_BUS_MODE_COUNT;m++){
        calls=0;fault=0;struct tp_bus_observation s=tp_bus_observe(&ops,0,(enum tp_bus_mode)m,0);
        assert(s.obtained&&s.error==0);
        if(m==TP_BUS_COMBINED)assert(calls==1&&trace[0]=='C'&&s.value==0x0c);
        else assert(calls==2&&trace[0]=='P'&&trace[1]==(m==TP_BUS_STOP_READ?'R':'C'));
        for(int f=1;f<=(m==TP_BUS_COMBINED?1:2);f++){
            calls=0;fault=f;s=tp_bus_observe(&ops,0,(enum tp_bus_mode)m,0);
            assert(!s.obtained&&s.error==-5&&s.value==0&&calls==(unsigned)f);
        }
    }
    calls=0;struct tp_bus_observation s=tp_bus_observe(&ops,0,TP_BUS_MODE_COUNT,0);
    assert(!s.obtained&&s.error==-22&&calls==0);
    puts("PASS Bus probe: combined/STOP-read/legacy-prefix exact address-only order, every failure UNKNOWN, no value writes or contact interpretation");
    return 0;
}
