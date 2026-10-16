/* SPDX-License-Identifier: MIT -- bounded, allocation-free diagnostics only. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#define MD_CAPACITY 128U
#define MD_PER_SECOND 64U
struct md_event { uint32_t at; int32_t a,b,c,d; uint8_t kind; };
struct md_ring { struct md_event items[MD_CAPACITY]; uint32_t dropped,bucket; uint16_t head,count,rate,raw_rate; };
static inline bool md_push(struct md_ring *s,uint32_t now,uint8_t kind,int32_t a,int32_t b,int32_t c,int32_t d) {
 uint32_t bucket=now/1000U;
 if(bucket!=s->bucket){s->bucket=bucket;s->rate=s->raw_rate=0;}
 bool raw=kind==1U||kind==2U;
 if((raw?s->raw_rate>=32U:s->rate>=MD_PER_SECOND)||s->count>=MD_CAPACITY){s->dropped++;return false;}
 if(raw)s->raw_rate++;else s->rate++;
 s->items[(s->head+s->count)%MD_CAPACITY]=(struct md_event){now,a,b,c,d,kind};s->count++;return true;
}
static inline bool md_pop(struct md_ring *s,struct md_event *out) {
 if(s->count==0U)return false;
 *out=s->items[s->head];s->head=(uint16_t)((s->head+1U)%MD_CAPACITY);s->count--;return true;
}
