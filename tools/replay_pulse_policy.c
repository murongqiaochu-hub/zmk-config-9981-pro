/* SPDX-License-Identifier: Apache-2.0
 * Offline replay only. Missing poll ticks are MODELLED zero motion, NOT sensor
 * readings or finger-up evidence. Input: uptime_ms dx dy valid; one stage/run.
 * Compile with -I pointing at either baseline or candidate driver directory. */
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include "trackpad_microtap.h"
static struct tp_microtap_state s;
static uint64_t inputs, replayed, discarded, clicks, polls;
static void step(uint32_t t, int16_t x, int16_t y, bool valid) {
    uint8_t old_count = s.count;
    uint32_t first = s.first_ms, path = s.path;
    int32_t nx = s.sum_x, ny = s.sum_y;
    struct tp_microtap_result r = tp_microtap_step(&s,t,x,y,false,valid,true,false);
    if (valid && (x != 0 || y != 0)) { inputs++; }
    replayed += r.count;
    if (r.click) {
        clicks++; discarded += old_count;
        fprintf(stderr,"{\"first_ms\":%" PRIu32 ",\"click_ms\":%" PRIu32 ",\"frames\":%u,\"path\":%" PRIu32 ",\"net_x\":%" PRId32 ",\"net_y\":%" PRId32 "}\n",first,t,(unsigned int)old_count,path,nx,ny);
    }
    polls++;
    if (inputs != replayed + discarded + s.count) {
        fputs("raw frame conservation failure\n",stderr); exit(2);
    }
}
int main(void) {
    uint32_t t=0,last=0; int x,y,valid; bool seen=false;
    while (scanf("%" SCNu32 " %d %d %d",&t,&x,&y,&valid)==4) {
        if (x < INT16_MIN || x > INT16_MAX || y < INT16_MIN || y > INT16_MAX) { return 3; }
        if (seen) {
            uint32_t gap = t-last;
            if (gap >= UINT32_C(0x80000000) || gap > 3600000U) { return 4; }
            for (uint32_t dt=10U; dt<gap; dt+=10U) { step(last+dt,0,0,true); }
        }
        step(t,(int16_t)x,(int16_t)y,valid!=0); last=t; seen=true;
    }
    if (seen) { for (uint32_t dt=10U;dt<=130U;dt+=10U) { step(last+dt,0,0,true); } }
    printf("{\"input_motion_frames\":%" PRIu64 ",\"click_candidates\":%" PRIu64 ",\"replayed_frames\":%" PRIu64 ",\"discarded_frames\":%" PRIu64 ",\"pending\":%u,\"polls\":%" PRIu64 "}\n",inputs,clicks,replayed,discarded,(unsigned int)s.count,polls);
    return 0;
}
