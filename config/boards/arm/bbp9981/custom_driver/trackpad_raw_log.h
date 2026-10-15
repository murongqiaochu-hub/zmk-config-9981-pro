/* SPDX-License-Identifier: MIT
 * Driver-private USB log adapter. Include after LOG_MODULE_REGISTER.
 * Every LOG argument is a scalar or static string: no deferred stack strings. */
#ifndef TRACKPAD_RAW_LOG_H
#define TRACKPAD_RAW_LOG_H
#include <zephyr/drivers/uart.h>
#include <zmk/usb.h>
#include "trackpad_raw_capture.h"
static struct tp_raw_capture_state raw_capture;
static const char *raw_capture_reason(enum tp_raw_capture_reason r) {
    switch(r) {
    case TP_RAW_CAPTURE_REASON_DTR_CLOSED:return "dtr_closed";
    case TP_RAW_CAPTURE_REASON_USB_OFFLINE:return "usb_offline";
    case TP_RAW_CAPTURE_REASON_WINDOW_LIMIT:return "window_limit";
    default:return "clock_fault";
    }
}
static void raw_capture_emit(void *ctx,const struct tp_raw_capture_event *e) {
    (void)ctx;
    switch(e->kind) {
    case TP_RAW_CAPTURE_BEGIN:
        LOG_INF("TPRAW_BEGIN proto=1 session=%u t_ms=%u readonly=1 synthetic_click=0 gate=motion_low addr=57 reg=0a len=7 path=pointer_stop_then_burst poll_ms=%u max_ms=%u first_seq=%u",
                e->session,e->t_ms,(unsigned)CONFIG_A320_POLL_INTERVAL_MS,
                TP_RAW_CAPTURE_MAX_MS,e->first_seq);break;
    case TP_RAW_CAPTURE_FRAME:
        if(e->raw_valid) {
            LOG_INF("TPRAW_FRAME session=%u seq=%u t_ms=%u read=ok stage=complete raw=%02x,%02x,%02x,%02x,%02x,%02x,%02x obtained=7 dx=%d dy=%d scroll=%u err=0",
                    e->session,e->seq,e->t_ms,e->raw[0],e->raw[1],e->raw[2],e->raw[3],
                    e->raw[4],e->raw[5],e->raw[6],e->dx,e->dy,(unsigned)e->scroll);
        } else {
            LOG_INF("TPRAW_FRAME session=%u seq=%u t_ms=%u read=error stage=%s raw=none obtained=0 dx=na dy=na scroll=%u err=%d",
                    e->session,e->seq,e->t_ms,
                    e->read_stage==TP_RAW_CAPTURE_POINTER?"pointer":"burst",(unsigned)e->scroll,e->error);
        }
        break;
    case TP_RAW_CAPTURE_END:
        LOG_INF("TPRAW_END session=%u t_ms=%u reason=%s first_seq=%u last_seq=%u attempts=%u ok=%u errors=%u",
                e->session,e->t_ms,raw_capture_reason(e->reason),e->first_seq,e->last_seq,
                e->attempts,e->successes,e->errors);break;
    case TP_RAW_CAPTURE_GPIO:
        LOG_INF("TPRAW_GPIO session=%u t_ms=%u irq=%d gpio_err=%d last_seq=%u",
                e->session,e->t_ms,e->irq,e->error,e->last_seq);break;
    case TP_RAW_CAPTURE_HEARTBEAT:
        LOG_INF("TPRAW_HEARTBEAT session=%u t_ms=%u irq=%d gpio_err=%d last_seq=%u",
                e->session,e->t_ms,e->irq,e->error,e->last_seq);break;
    }
}
static void raw_capture_poll(int irq,bool scroll) {
    const struct device *console=DEVICE_DT_GET(DT_NODELABEL(debug_cdc_acm_uart));
    uint32_t dtr=0;
    bool online=zmk_usb_is_hid_ready()&&device_is_ready(console);
    if(online&&uart_line_ctrl_get(console,UART_LINE_CTRL_DTR,&dtr)!=0)dtr=0;
    tp_raw_capture_poll(&raw_capture,k_uptime_get_32(),online,dtr!=0,irq,scroll,raw_capture_emit,NULL);
}
static void raw_capture_read(uint32_t started,enum tp_raw_capture_read_stage stage,
                              int error,const uint8_t *raw) {
    tp_raw_capture_read(&raw_capture,started,stage,error,raw,tp_scroll_mode_active(),raw_capture_emit,NULL);
}
#endif
