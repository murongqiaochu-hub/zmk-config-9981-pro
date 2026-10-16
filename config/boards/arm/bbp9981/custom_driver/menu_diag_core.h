/* SPDX-License-Identifier: MIT -- bounded, allocation-free diagnostics only. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "menu_click_diag.h"
#define MD_CAPACITY 128U
#define MD_PER_SECOND 64U
struct md_event { uint32_t at; int32_t a,b,c,d; uint8_t kind; uint32_t seq; };
struct md_ring { struct md_event items[MD_CAPACITY]; uint32_t dropped,bucket,seq; uint16_t head,count,rate,raw_rate; };
static inline bool md_push(struct md_ring *s,uint32_t now,uint8_t kind,int32_t a,int32_t b,int32_t c,int32_t d) {
 uint32_t bucket=now/1000U;
 if(bucket!=s->bucket){s->bucket=bucket;s->rate=s->raw_rate=0;}
 bool raw=kind==1U||kind==2U;
 if((raw?s->raw_rate>=32U:s->rate>=MD_PER_SECOND)||s->count>=MD_CAPACITY){s->dropped++;return false;}
 if(raw)s->raw_rate++;else s->rate++;
 s->items[(s->head+s->count)%MD_CAPACITY]=(struct md_event){now,a,b,c,d,kind,++s->seq};s->count++;return true;
}
/* Called under one short lock: same timestamp and adjacent modulo-u32 seq.
 * A saturated FIFO/rate budget drops/retries the WHOLE pair, never half a pair.
 * queued is the depth before either heartbeat is pushed, so it may be zero. */
static inline bool md_push_heartbeats(struct md_ring *s,uint32_t now,
                                     struct md_endpoint_snapshot ep,bool active) {
 uint32_t bucket=now/1000U;
 if(bucket!=s->bucket){s->bucket=bucket;s->rate=s->raw_rate=0;}
 if(s->count>MD_CAPACITY-2U||s->rate>MD_PER_SECOND-2U){s->dropped+=2U;return false;}
 int32_t dropped=(int32_t)s->dropped,queued=(int32_t)s->count;
 (void)md_push(s,now,MD_ROUTE,ep.selected,ep.preferred,ep.ready_bits,ep.active_ble_index);
 (void)md_push(s,now,MD_QUALITY,dropped,queued,active,MD_PROTOCOL_VERSION);
 return true;
}
static inline bool md_pop(struct md_ring *s,struct md_event *out) {
 if(s->count==0U)return false;
 *out=s->items[s->head];s->head=(uint16_t)((s->head+1U)%MD_CAPACITY);s->count--;return true;
}
/* FNV-1a32, seven LE u32 words. Explicit shifts are host-endian independent;
 * casting signed fields to u32 specifies their two's-complement wire bytes.
 * This detects damaged frames, NOT a cryptographic hash/device identity. */
static inline uint32_t md_crc(const struct md_event *e) {
 const uint32_t words[7]={e->at,e->seq,e->kind,(uint32_t)e->a,(uint32_t)e->b,
                          (uint32_t)e->c,(uint32_t)e->d};
 uint32_t hash=UINT32_C(2166136261);
 for(unsigned i=0;i<7U;i++)for(unsigned shift=0;shift<32U;shift+=8U){
  hash^=(words[i]>>shift)&UINT32_C(255);hash*=UINT32_C(16777619);
 }
 return hash;
}
