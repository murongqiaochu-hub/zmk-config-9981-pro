/* SPDX-License-Identifier: MIT
 * Read-transaction observations only; no sensor I/O, cache replay or contact inference.
 * One owner (the existing driver poll work). Callbacks consume stack events immediately. */
#ifndef TRACKPAD_RAW_CAPTURE_H
#define TRACKPAD_RAW_CAPTURE_H
#include <stdbool.h>
#include <stdint.h>
#define TP_RAW_CAPTURE_MAX_MS 180000U
#define TP_RAW_CAPTURE_HEARTBEAT_MS 1000U
enum tp_raw_capture_event_kind { TP_RAW_CAPTURE_BEGIN, TP_RAW_CAPTURE_FRAME,
    TP_RAW_CAPTURE_END, TP_RAW_CAPTURE_GPIO, TP_RAW_CAPTURE_HEARTBEAT };
enum tp_raw_capture_read_stage { TP_RAW_CAPTURE_POINTER, TP_RAW_CAPTURE_BURST, TP_RAW_CAPTURE_OK };
enum tp_raw_capture_reason { TP_RAW_CAPTURE_REASON_NONE, TP_RAW_CAPTURE_REASON_DTR_CLOSED,
    TP_RAW_CAPTURE_REASON_USB_OFFLINE, TP_RAW_CAPTURE_REASON_WINDOW_LIMIT,
    TP_RAW_CAPTURE_REASON_CLOCK_DISCONTINUITY };
struct tp_raw_capture_event {
    enum tp_raw_capture_event_kind kind;
    enum tp_raw_capture_read_stage read_stage;
    enum tp_raw_capture_reason reason;
    uint32_t session,seq,first_seq,last_seq,t_ms,attempts,successes,errors;
    uint8_t raw[7];
    int8_t dx,dy;
    bool scroll,raw_valid;
    int irq,error;
};
typedef void (*tp_raw_capture_emit_fn)(void *ctx,const struct tp_raw_capture_event *event);
struct tp_raw_capture_state {
    uint32_t seq,session,first_seq,attempts,successes,errors;
    uint32_t started_ms,last_poll_ms,last_status_ms;
    bool active,wait_close,have_time,irq_known;
    int last_irq;
};
static inline void tp_raw_capture_reset(struct tp_raw_capture_state *s) {
    *s=(struct tp_raw_capture_state){0};
}
static inline struct tp_raw_capture_event tp_raw_capture_event(
        const struct tp_raw_capture_state *s,enum tp_raw_capture_event_kind kind,uint32_t now) {
    return (struct tp_raw_capture_event){.kind=kind,.session=s->session,.seq=s->seq,
        .first_seq=s->first_seq,.last_seq=s->seq,.t_ms=now,.attempts=s->attempts,
        .successes=s->successes,.errors=s->errors};
}
static inline void tp_raw_capture_end(struct tp_raw_capture_state *s,uint32_t now,
        enum tp_raw_capture_reason reason,tp_raw_capture_emit_fn emit,void *ctx) {
    struct tp_raw_capture_event e=tp_raw_capture_event(s,TP_RAW_CAPTURE_END,now);
    e.reason=reason;s->active=false;emit(ctx,&e);
}
static inline void tp_raw_capture_poll(struct tp_raw_capture_state *s,uint32_t now,
        bool online,bool dtr,int irq,bool scroll,tp_raw_capture_emit_fn emit,void *ctx) {
    if(!online||!dtr) {
        if(s->active)tp_raw_capture_end(s,now,online?TP_RAW_CAPTURE_REASON_DTR_CLOSED:
                                      TP_RAW_CAPTURE_REASON_USB_OFFLINE,emit,ctx);
        s->wait_close=false;s->have_time=true;s->last_poll_ms=now;return;
    }
    bool clock_fault=s->have_time&&(uint32_t)(now-s->last_poll_ms)>=UINT32_C(0x80000000);
    s->have_time=true;s->last_poll_ms=now;
    if(clock_fault) {
        if(s->active)tp_raw_capture_end(s,now,TP_RAW_CAPTURE_REASON_CLOCK_DISCONTINUITY,emit,ctx);
        s->wait_close=true;return;
    }
    if(s->active&&(uint32_t)(now-s->started_ms)>=TP_RAW_CAPTURE_MAX_MS) {
        tp_raw_capture_end(s,now,TP_RAW_CAPTURE_REASON_WINDOW_LIMIT,emit,ctx);
        s->wait_close=true;return;
    }
    if(s->wait_close)return;
    if(!s->active) {
        s->session++;s->active=true;s->first_seq=s->seq+1U;
        s->attempts=s->successes=s->errors=0U;s->started_ms=s->last_status_ms=now;
        s->irq_known=false;
        struct tp_raw_capture_event e=tp_raw_capture_event(s,TP_RAW_CAPTURE_BEGIN,now);
        emit(ctx,&e);
    }
    if(!s->irq_known||irq!=s->last_irq) {
        s->irq_known=true;s->last_irq=irq;
        struct tp_raw_capture_event e=tp_raw_capture_event(s,TP_RAW_CAPTURE_GPIO,now);
        e.irq=irq;e.error=irq<0?irq:0;e.scroll=scroll;emit(ctx,&e);
    }
    if((uint32_t)(now-s->last_status_ms)>=TP_RAW_CAPTURE_HEARTBEAT_MS) {
        s->last_status_ms=now;
        struct tp_raw_capture_event e=tp_raw_capture_event(s,TP_RAW_CAPTURE_HEARTBEAT,now);
        e.irq=irq;e.error=irq<0?irq:0;e.scroll=scroll;emit(ctx,&e);
    }
}
/* Exactly once per existing read attempt, including pointer/burst failures.
 * seq counts transactions, NOT a hardware frame counter or proof of new contact. */
static inline void tp_raw_capture_read(struct tp_raw_capture_state *s,uint32_t started,
        enum tp_raw_capture_read_stage stage,int error,const uint8_t *raw,bool scroll,
        tp_raw_capture_emit_fn emit,void *ctx) {
    s->seq++;
    if(!s->active)return;
    bool valid=stage==TP_RAW_CAPTURE_OK&&error==0&&raw!=0;
    s->attempts++;
    if(valid)s->successes++;else s->errors++;
    struct tp_raw_capture_event e=tp_raw_capture_event(s,TP_RAW_CAPTURE_FRAME,started);
    e.read_stage=stage;e.error=valid?0:(error<0?error:-22);e.raw_valid=valid;e.scroll=scroll;
    if(valid) {
        for(unsigned int i=0;i<7U;i++)e.raw[i]=raw[i];
        e.dy=(int8_t)raw[1];e.dx=(int8_t)raw[3];
    } else if(e.read_stage==TP_RAW_CAPTURE_OK)e.read_stage=TP_RAW_CAPTURE_BURST;
    emit(ctx,&e);
}
#endif
