/* Inject ONLY into pinned kscan_gpio_matrix.c. No GPIO writes/extra reads.
 * Lexical out_gpio/in_gpio are deliberate pinned-callsite dependencies. */
#pragma once
#include "kscan_gpio.h"
#include <zmk/debounce.h>
#include <zephyr/kernel.h>
#include "menu_click_diag.h"
/* Link guard: the diagnostic worker cannot link unless this TU prefix applied. */
int bbp_menu_diag_matrix_ready(void){return 1;}
static inline int md_slot(size_t col){return col==0U?0:(col==1U?1:(col==5U?2:-1));}
static inline int md_matrix_get(const struct kscan_gpio *g,struct kscan_gpio_port_state *s,size_t col) {
 int value=kscan_gpio_pin_get(g,s);int slot=md_slot(col);
 if(g->index!=4U||slot<0)return value;
 static int last[3]={-999,-999,-999};static uint32_t sample[3];uint32_t now=k_uptime_get_32();
 bool changed=last[slot]!=value;
 if(changed&&value>0)bbp_menu_diag_arm();
 if(bbp_menu_diag_active()&&(changed||now-sample[slot]>=1000U)){
  sample[slot]=now;bbp_menu_diag_note(MD_RAW,(int32_t)col,value,last[slot],0);
 }
 last[slot]=value;return value;
}
static inline void md_matrix_debounce(struct zmk_debounce_state *s,bool active,int elapsed,
                                     const struct zmk_debounce_config *c,size_t row,size_t col) {
 zmk_debounce_update(s,active,elapsed,c);int slot=md_slot(col);
 if(row!=4U||slot<0||!bbp_menu_diag_active())return;
 static int last[3]={-1,-1,-1};static uint32_t sample[3];uint32_t now=k_uptime_get_32();
 int bits=(s->pressed?1:0)|(s->changed?2:0)|(s->counter?4:0);
 if(last[slot]!=bits||now-sample[slot]>=1000U){
  last[slot]=bits;sample[slot]=now;
  bbp_menu_diag_note(MD_DB,(int32_t)col,s->pressed,s->changed,s->counter);
 }
}
#define kscan_gpio_pin_get(g,s) md_matrix_get((g),(s),out_gpio->index)
#define zmk_debounce_update(s,a,e,c) md_matrix_debounce((s),(a),(e),(c),in_gpio->index,out_gpio->index)
