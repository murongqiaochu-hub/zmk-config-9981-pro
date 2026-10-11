#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_contact_probe.h"
static uint8_t registers[256], trace[16];
static unsigned calls;
static int fail_reg=-1;
static int read_one(void *ctx,uint8_t reg,uint8_t *out) {
    (void)ctx;assert(calls<sizeof(trace));trace[calls++]=reg;
    if(reg==fail_reg){return -5;}*out=registers[reg];return 0;
}
static void reset_fixture(struct tp_probe_cache *cache) {
    memset(registers,0,sizeof(registers));registers[0]=0x83;registers[1]=1;
    registers[0x60]=0x84;registers[0x75]=0x50;registers[5]=42;
    registers[6]=7;registers[7]=9;fail_reg=-1;calls=0;tp_probe_reset(cache);
}
int main(void) {
    struct tp_probe_cache c;struct tp_probe_sample s;
    reset_fixture(&c);s=tp_probe_sample(&c,1000,read_one,0);
    const uint8_t expected[]={0,1,0x60,0x75,2,5,6,7};
    assert(calls==8&&!memcmp(trace,expected,8));assert(s.valid&&s.contact==0);
    assert(s.matched&&s.quality==42&&s.shutter==1801&&s.error==0);
    calls=0;registers[2]=1;s=tp_probe_sample(&c,1020,read_one,0);
    assert(calls==6&&trace[0]==0x60&&s.valid&&s.contact==1);
    calls=0;s=tp_probe_sample(&c,2000,read_one,0);assert(calls==8&&trace[0]==0);
    for(unsigned engine=0;engine<256;engine++)for(unsigned polarity=0;polarity<2;polarity++)
    for(unsigned status=0;status<256;status++){
        reset_fixture(&c);registers[0x60]=engine;registers[0x75]=polarity?0x40:0;
        registers[2]=status;s=tp_probe_sample(&c,1000,read_one,0);
        bool enabled=(engine&0x84)==0x84;assert(s.valid==enabled);
        assert(s.contact==(enabled?(((status&1)!=0)==(polarity!=0)):-1));
    }
    for(unsigned pid=0;pid<256;pid++){
        reset_fixture(&c);registers[0]=pid;s=tp_probe_sample(&c,1000,read_one,0);
        if(pid!=0x83){assert(calls==1&&!s.valid&&s.contact==-1&&!s.matched);}
    }
    for(unsigned rev=0;rev<256;rev++){
        reset_fixture(&c);registers[1]=rev;s=tp_probe_sample(&c,1000,read_one,0);
        if(rev!=1){assert(calls==2&&!s.valid&&s.contact==-1&&!s.matched);}
    }
    for(unsigned i=0;i<8;i++){
        reset_fixture(&c);fail_reg=expected[i];s=tp_probe_sample(&c,1000,read_one,0);
        assert(s.error==-5&&!s.valid&&s.contact==-1&&!c.checked&&calls==i+1);
        fail_reg=-1;calls=0;s=tp_probe_sample(&c,1020,read_one,0);
        assert(s.valid&&calls==8&&trace[0]==0);
    }
    reset_fixture(&c);s=tp_probe_sample(&c,UINT32_MAX-500,read_one,0);assert(s.valid);
    calls=0;s=tp_probe_sample(&c,499,read_one,0);assert(s.valid&&calls==8);
    tp_probe_reset(&c);assert(!c.checked&&!c.matched);calls=0;
    s=tp_probe_sample(&c,520,read_one,0);assert(s.valid&&calls==8);
    puts("PASS Contact probe: documented read-only order, 131072 engine/polarity/status cases, all PID/revision guards, every read fault UNKNOWN (not UP), retry, cache, rollover and reopen reset; no click outputs");
}
