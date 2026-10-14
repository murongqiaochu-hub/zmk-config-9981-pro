/* Read timestamped raw observations from stdin; synthesize zero-motion poll ticks
 * between logged reads. This interpolation is NOT a new sensor observation.
 * SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include "../config/boards/arm/bbp9981/custom_driver/trackpad_microtap.h"
int main(void) {
    struct tp_microtap_state s={0};
    uint32_t t=0,last=0;
    int x,y;unsigned clicks=0,frames=0,replayed=0;
    bool seen=false;
    while(scanf("%" SCNu32 " %d %d",&t,&x,&y)==3) {
        if(seen) {
            for(uint32_t tick=last+10U;tick<t;tick+=10U) {
                struct tp_microtap_state before=s;
                struct tp_microtap_result r=tp_microtap_step(&s,tick,0,0,true,true,false);
                if(r.click)fprintf(stderr,"C %" PRIu32 " %" PRIu32 " %u %" PRIu32 " %" PRIu32 "\n",before.first_ms,before.last_motion_ms,before.count,before.path,before.max_offset);
                clicks+=r.click?1U:0U;replayed+=r.count;
            }
        }
        struct tp_microtap_result r=tp_microtap_step(&s,t,(int16_t)x,(int16_t)y,true,true,false);
        frames++;clicks+=r.click?1U:0U;replayed+=r.count;last=t;seen=true;
    }
    if(seen) {
        for(uint32_t tick=last+10U;tick<=last+110U;tick+=10U) {
            struct tp_microtap_state before=s;
            struct tp_microtap_result r=tp_microtap_step(&s,tick,0,0,true,true,false);
            if(r.click)fprintf(stderr,"C %" PRIu32 " %" PRIu32 " %u %" PRIu32 " %" PRIu32 "\n",before.first_ms,before.last_motion_ms,before.count,before.path,before.max_offset);
            clicks+=r.click?1U:0U;replayed+=r.count;
        }
    }
    printf("%u %u %u\n",frames,clicks,replayed);
    return 0;
}
