/* SPDX-License-Identifier: MIT -- no transport, GPIO, key or settings mutation. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#define MD_PROTOCOL_VERSION 2
#define MD_BUILD_ID "tap200_route_20261016"
#define MD_Q_WINDOW_MS 5000U
/* Values 1..10 are wire-compatible with the previous observation kinds. */
enum md_kind {
 MD_RAW=1,MD_DB,MD_POS,MD_HID,MD_KBD,MD_ENQUEUE,MD_NOTIFY,MD_CANCEL,MD_MICRO,MD_BEGIN,
 MD_ROUTE,MD_ROUTE_REQUEST,MD_Q_BINDING,MD_ENDPOINT,MD_QUALITY
};
struct md_endpoint_snapshot {
 int32_t selected,preferred,ready_bits,active_ble_index;
};
#if defined(CONFIG_BBP9981_MENU_DIAG) && CONFIG_BBP9981_MENU_DIAG
int bbp_menu_diag_matrix_ready(void);
/* Defined ONLY by the endpoints.c source preinclude: also a required link guard. */
struct md_endpoint_snapshot bbp_menu_diag_endpoints_snapshot(void);
bool bbp_menu_diag_active(void);
void bbp_menu_diag_arm(void);
void bbp_menu_diag_note(uint8_t kind,int32_t a,int32_t b,int32_t c,int32_t d);
void bbp_menu_diag_tx(uint8_t kind,uint8_t buttons,int result,unsigned outstanding);
#else
static inline bool bbp_menu_diag_active(void){return false;}
static inline void bbp_menu_diag_arm(void){}
static inline void bbp_menu_diag_note(uint8_t k,int32_t a,int32_t b,int32_t c,int32_t d){(void)k;(void)a;(void)b;(void)c;(void)d;}
static inline void bbp_menu_diag_tx(uint8_t k,uint8_t b,int r,unsigned o){(void)k;(void)b;(void)r;(void)o;}
#endif
