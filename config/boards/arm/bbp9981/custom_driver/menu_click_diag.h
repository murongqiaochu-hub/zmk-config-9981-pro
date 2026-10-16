/* SPDX-License-Identifier: MIT -- no transport, GPIO, key or settings mutation. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
enum md_kind {MD_RAW=1,MD_DB,MD_POS,MD_HID,MD_KBD,MD_ENQUEUE,MD_NOTIFY,MD_CANCEL,MD_MICRO,MD_BEGIN};
#if defined(CONFIG_BBP9981_MENU_DIAG) && CONFIG_BBP9981_MENU_DIAG
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
